# Node: Sender (Edge Node)

**Led by:** Stefan Necsoiu & Nojus Lankelis

## Role and Functionalities
The Sender node is an **edge computing device** responsible for real-time environmental data collection in remote or rural locations. Its core functionalities are:
- Reading five environmental sensors continuously.
- Packaging the readings into a compact JSON telemetry payload.
- Transmitting the data reliably over LoRa to the Gateway node every 1 second.
- Receiving and acknowledging control/ACK packets from the Gateway.

This node enables early wildfire-risk detection by feeding raw sensor data into the system’s heuristic algorithm (processed at the Gateway).

## Suitable Single Board Computers
| SBC Option       | Power Consumption | Cost per Unit | Scalability (1000+ nodes) | Justification for this project |
|------------------|-------------------|---------------|---------------------------|--------------------------------|
| **ESP32** (chosen) | ~150 mA active, <10 µA sleep | ~£4–6 | Excellent | Ultra-low power, built-in LoRa support via external module, sufficient GPIO/I2C/SPI, runs C++ firmware efficiently. Ideal for battery/solar deployments. |
| Raspberry Pi 5   | ~500–1000 mA     | ~£60+        | Poor                      | Overkill, high power draw, expensive for large-scale rollout. |
| Arduino Uno + LoRa shield | Moderate        | ~£15         | Good                      | Limited processing, no Wi-Fi fallback, slower development. |

**Recommendation:** ESP32 (Freenove ESP32-WROVER) is the most suitable for a scalable wildfire monitoring network because of its extremely low power profile and low unit cost while still providing enough performance for sensor reading and LoRa transmission.

## Sensors, Actuators and Hardware
- **GY-BMP280** (I2C): Measures temperature, barometric pressure and altitude. High accuracy (±1 hPa, ±0.5 °C). Chosen for reliable pressure/altitude data used in wildfire risk calculation.
- **DHT11** (1-Wire, GPIO4): Temperature and relative humidity. Low-cost, sufficient range (0–50 °C, 20–80 % RH). Digital output simplifies integration.
- **Soil Moisture Sensor (LM393)** (Analog GPIO34 + Digital GPIO25): Detects soil dryness. Critical for identifying prolonged dry conditions that increase fire risk.
- **Photoresistor (10 kΩ voltage divider, GPIO35)**: Measures ambient light intensity. High light + low humidity is a strong wildfire indicator.
- **LoRa Module (SX1276/78 based)**: 915 MHz, SPI interface.

All sensors were selected for low cost, low power, and proven reliability in outdoor environmental monitoring. No actuators are present on this node.

## Connectivity and Messaging Protocols
**Physical layer:** LoRa (Long Range) at 915 MHz using SPI.
- Pin mapping (ESP32 Freenove):
- **Application layer:** Custom lightweight protocol (`MSG:<id>:<JSON payload>`) with `ACK:<id>` responses. JSON payload example: `{"n":"Sender","s":3244,"u":3253944,"ba":118,"bt":24.68,"bp":1030.95,"bl":7.87,"dok":true,"dt":24.10,"dh":48.00,"sa":4095,"sd":0,"lv":3082}`.
- This combination gives >5 km range in open terrain while keeping power consumption minimal.

| LoRa Pin | ESP32 Pin | Function     |
|----------|-----------|--------------|
| 3V3      | 3.3V      | Power        |
| GND      | GND       | Ground       |
| MISO     | GPIO19    | SPI MISO     |
| MOSI     | GPIO23    | SPI MOSI     |
| SCK      | GPIO18    | SPI Clock    |
| NSS      | GPIO5     | Chip Select  |
| RST      | GPIO14    | Reset        |
| DIO0     | GPIO2     | Interrupt    |

## Additional Considerations
- **Scalability:** Thousands of Sender nodes can operate on a single Gateway thanks to LoRa’s high node capacity and low duty cycle.
- **Legal/Ethical/Privacy:** No personal data collected. All readings are environmental only. Open-source licensing allows communities to deploy and modify the system freely.
- **Security:** Basic ACK mechanism; future versions would add AES encryption on LoRa payload.
- **Advanced techniques:** Altitude is software-calibrated with offset `154.17 m` for local accuracy.

This design fully supports the overall system’s contribution to SDG 11 by providing the primary data source for wildfire-risk alerts.

---
