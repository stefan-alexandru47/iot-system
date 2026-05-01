#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>

namespace {
// The sender samples local sensors, packs the readings into a compact JSON
// payload, and transmits them over LoRa often enough for a live dashboard.
constexpr char NODE_NAME[] = "Sender";

constexpr long LORA_FREQUENCY = 915E6;
constexpr int LORA_SS_PIN = 5;
constexpr int LORA_RST_PIN = 14;
constexpr int LORA_DIO0_PIN = 2;
constexpr long LORA_SPI_FREQUENCY = 8E6;
constexpr size_t MAX_LORA_TEXT = 420;

constexpr unsigned long SEND_INTERVAL_MS = 1000;
constexpr uint8_t BMP_SDA_PIN = 21;
constexpr uint8_t BMP_SCL_PIN = 22;
constexpr uint8_t BMP_ADDR_PRIMARY = 0x76;
constexpr uint8_t BMP_ADDR_SECONDARY = 0x77;
constexpr float SEA_LEVEL_PRESSURE_HPA = 1013.25f;
constexpr float ALTITUDE_CALIBRATION_OFFSET_M = 154.17f;
constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t DHT_TYPE = DHT11;
constexpr uint8_t SOIL_DIGITAL_PIN = 25;
constexpr uint8_t SOIL_ANALOG_PIN = 34;
constexpr uint8_t PHOTORESISTOR_PIN = 35;

unsigned long lastSendMs = 0;
unsigned long packetCounter = 0;
unsigned long outboundMessageId = 0;
Adafruit_BMP280 bmp;
bool bmpReady = false;
uint8_t bmpAddress = 0;
DHT dht(DHT_PIN, DHT_TYPE);

String makeTaggedMessage(unsigned long id, const String &payload) {
  return "MSG:" + String(id) + ":" + payload;
}

String makeAckMessage(unsigned long id) {
  return "ACK:" + String(id);
}

bool extractTaggedId(const String &payload, const char *prefix, unsigned long &idOut, String &restOut) {
  const String prefixStr = String(prefix);
  if (!payload.startsWith(prefixStr)) {
    return false;
  }

  const int idStart = prefixStr.length();
  const int idEnd = payload.indexOf(':', idStart);
  if (idEnd < 0) {
    return false;
  }

  const String idText = payload.substring(idStart, idEnd);
  if (idText.isEmpty()) {
    return false;
  }

  idOut = strtoul(idText.c_str(), nullptr, 10);
  restOut = payload.substring(idEnd + 1);
  return true;
}

bool extractAckId(const String &payload, unsigned long &idOut) {
  if (!payload.startsWith("ACK:")) {
    return false;
  }

  const String idText = payload.substring(4);
  if (idText.isEmpty()) {
    return false;
  }

  idOut = strtoul(idText.c_str(), nullptr, 10);
  return true;
}

bool sendLoRaMessage(const String &message) {
  if (message.isEmpty() || message.length() > MAX_LORA_TEXT) {
    Serial.print("LoRa TX skipped, payload length=");
    Serial.println(message.length());
    return false;
  }

  LoRa.idle();
  if (!LoRa.beginPacket()) {
    LoRa.receive();
    return false;
  }

  LoRa.print(message);
  const bool success = LoRa.endPacket() == 1;
  LoRa.receive();

  Serial.print(success ? "LoRa TX: " : "LoRa TX failed: ");
  Serial.println(message);
  return success;
}

bool sendApplicationMessage(const String &payload, unsigned long &messageId) {
  messageId = ++outboundMessageId;
  return sendLoRaMessage(makeTaggedMessage(messageId, payload));
}

bool setupLoRa() {
  SPI.begin(18, 19, 23, LORA_SS_PIN);
  LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  LoRa.setSPIFrequency(LORA_SPI_FREQUENCY);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    return false;
  }

  LoRa.enableCrc();
  LoRa.setTxPower(17);
  LoRa.receive();
  return true;
}

void logI2cScan() {
  Serial.println("I2C scan start");
  bool foundAny = false;

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();

    if (error == 0) {
      foundAny = true;
      Serial.print("I2C device found at 0x");
      if (address < 16) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
    }
  }

  if (!foundAny) {
    Serial.println("No I2C devices found");
  }
}

bool setupBmp280() {
  Wire.begin(BMP_SDA_PIN, BMP_SCL_PIN);
  Wire.setClock(100000);
  delay(100);
  logI2cScan();

  // The BMP280 can appear at either standard I2C address depending on how the
  // breakout's SDO pin is wired, so probe both before failing startup.
  if (bmp.begin(BMP_ADDR_PRIMARY, BMP280_CHIPID)) {
    bmpAddress = BMP_ADDR_PRIMARY;
  } else if (bmp.begin(BMP_ADDR_SECONDARY, BMP280_CHIPID)) {
    bmpAddress = BMP_ADDR_SECONDARY;
  } else {
    return false;
  }

  bmp.setSampling(
      Adafruit_BMP280::MODE_NORMAL,
      Adafruit_BMP280::SAMPLING_X2,
      Adafruit_BMP280::SAMPLING_X16,
      Adafruit_BMP280::FILTER_X16,
      Adafruit_BMP280::STANDBY_MS_500);

  return true;
}

String buildSensorPayload() {
  const float temperatureC = bmp.readTemperature();
  const float pressureHpa = bmp.readPressure() / 100.0f;
  const float altitudeM = bmp.readAltitude(SEA_LEVEL_PRESSURE_HPA) + ALTITUDE_CALIBRATION_OFFSET_M;
  const float dhtTemperatureC = dht.readTemperature();
  const float dhtHumidityPct = dht.readHumidity();
  const bool dhtOk = !isnan(dhtTemperatureC) && !isnan(dhtHumidityPct);
  const int soilAnalogValue = analogRead(SOIL_ANALOG_PIN);
  const int soilDigitalValue = digitalRead(SOIL_DIGITAL_PIN);
  const int photoresistorValue = analogRead(PHOTORESISTOR_PIN);

  // Short JSON keys keep the payload inside LoRa packet limits; the gateway
  // expands them back into human-readable labels for the dashboard.
  String payload = "{";
  payload += "\"n\":\"";
  payload += NODE_NAME;
  payload += "\",\"s\":";
  payload += String(++packetCounter);
  payload += ",\"u\":";
  payload += String(millis());
  payload += ",\"ba\":";
  payload += String(bmpAddress);
  payload += ",\"bt\":";
  payload += String(temperatureC, 2);
  payload += ",\"bp\":";
  payload += String(pressureHpa, 2);
  payload += ",\"bl\":";
  payload += String(altitudeM, 2);
  payload += ",\"dok\":";
  payload += dhtOk ? "true" : "false";

  if (dhtOk) {
    payload += ",\"dt\":";
    payload += String(dhtTemperatureC, 2);
    payload += ",\"dh\":";
    payload += String(dhtHumidityPct, 2);
  }

  payload += ",\"sa\":";
  payload += String(soilAnalogValue);
  payload += ",\"sd\":";
  payload += String(soilDigitalValue);
  payload += ",\"lv\":";
  payload += String(photoresistorValue);

  payload += "}";
  return payload;
}

void sendTelemetry() {
  if (!bmpReady) {
    Serial.println("BMP280 not ready, telemetry skipped");
    return;
  }

  const String payload = buildSensorPayload();
  unsigned long messageId = 0;
  sendApplicationMessage(payload, messageId);
}

void maybeSendPeriodicPacket() {
  const unsigned long now = millis();
  if (now - lastSendMs < SEND_INTERVAL_MS) {
    return;
  }

  lastSendMs = now;
  sendTelemetry();
}

void handleIncomingPackets() {
  const int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) {
    return;
  }

  String payload;
  while (LoRa.available()) {
    payload += static_cast<char>(LoRa.read());
  }

  unsigned long ackId = 0;
  if (extractAckId(payload, ackId)) {
    Serial.print("LoRa ACK RX: ");
    Serial.println(ackId);
    return;
  }

  unsigned long messageId = 0;
  String appPayload;
  if (extractTaggedId(payload, "MSG:", messageId, appPayload)) {
    // Either node can originate a tagged application message; the receiver
    // always answers with an ACK so the sender can detect delivery.
    Serial.print("LoRa RX [");
    Serial.print(packetSize);
    Serial.print(" bytes, RSSI ");
    Serial.print(LoRa.packetRssi());
    Serial.print(", SNR ");
    Serial.print(LoRa.packetSnr(), 2);
    Serial.print("]: ");
    Serial.println(appPayload);
    sendLoRaMessage(makeAckMessage(messageId));
    return;
  }

  Serial.print("LoRa RX [");
  Serial.print(packetSize);
  Serial.print(" bytes, RSSI ");
  Serial.print(LoRa.packetRssi());
  Serial.print(", SNR ");
  Serial.print(LoRa.packetSnr(), 2);
  Serial.print("]: ");
  Serial.println(payload);
}

void handleSerialInput() {
  if (!Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.isEmpty()) {
    return;
  }

  if (line.equalsIgnoreCase("SEND")) {
    sendTelemetry();
    return;
  }

  unsigned long messageId = 0;
  sendApplicationMessage(line, messageId);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Booting ESP32 LoRa sender");

  if (!setupLoRa()) {
    Serial.println("LoRa init failed. Check frequency and wiring.");
    while (true) {
      delay(1000);
    }
  }

  bmpReady = setupBmp280();
  if (!bmpReady) {
    Serial.println("BMP280 init failed. Check I2C wiring and address.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Sender ready");
  Serial.println("Commands: SEND for telemetry, or type any line to transmit");
  Serial.print("BMP280 I2C address: 0x");
  Serial.println(bmpAddress, HEX);
  Serial.print("BMP280 pins SDA/SCL: ");
  Serial.print(BMP_SDA_PIN);
  Serial.print("/");
  Serial.println(BMP_SCL_PIN);
  Serial.print("DHT11 data pin: ");
  Serial.println(DHT_PIN);
  analogReadResolution(12);
  pinMode(SOIL_DIGITAL_PIN, INPUT);
  Serial.print("Soil sensor AO pin: ");
  Serial.println(SOIL_ANALOG_PIN);
  Serial.print("Soil sensor DO pin: ");
  Serial.println(SOIL_DIGITAL_PIN);
  Serial.print("Photoresistor analog pin: ");
  Serial.println(PHOTORESISTOR_PIN);
  dht.begin();
  delay(1000);
  sendTelemetry();
  lastSendMs = millis();
}

void loop() {
  handleIncomingPackets();
  handleSerialInput();
  maybeSendPeriodicPacket();
  delay(5);
}
