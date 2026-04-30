#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LoRa.h>

namespace {
constexpr char WIFI_SSID[] = "BT-8TCM82";
constexpr char WIFI_PASSWORD[] = "6RptvaDXPdm7f6";
constexpr char DEVICE_NAME[] = "esp32-lora-gateway";

constexpr long LORA_FREQUENCY = 915E6;
constexpr int LORA_SS_PIN = 5;
constexpr int LORA_RST_PIN = 14;
constexpr int LORA_DIO0_PIN = 2;
constexpr long LORA_SPI_FREQUENCY = 8E6;
constexpr size_t MAX_LORA_TEXT = 420;

constexpr int PACKET_HISTORY_SIZE = 240;
constexpr size_t MAX_PACKET_TEXT = 420;
constexpr unsigned long WIFI_RETRY_MS = 10000;
constexpr unsigned long ACK_WAIT_MS = 2500;

struct PacketRecord {
  unsigned long millisAtReceive = 0;
  int size = 0;
  int rssi = 0;
  float snr = 0.0f;
  String payload;
};

WebServer server(80);
PacketRecord packetHistory[PACKET_HISTORY_SIZE];
int packetCount = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long sentCounter = 0;
bool wifiInitialized = false;
bool loraReady = false;
unsigned long lastAckedMessageId = 0;
unsigned long lastAckAtMs = 0;
unsigned long lastRxCheckLogMs = 0;

void clearPacketHistory() {
  for (PacketRecord &record : packetHistory) {
    record = PacketRecord{};
  }

  packetCount = 0;
}

bool isLikelyTextPayload(const String &payload) {
  if (payload.isEmpty()) {
    return false;
  }

  int suspiciousBytes = 0;
  for (size_t i = 0; i < payload.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(payload[i]);
    const bool printableAscii = c >= 32 && c <= 126;
    const bool allowedControl = c == '\n' || c == '\r' || c == '\t';

    if (!printableAscii && !allowedControl) {
      suspiciousBytes++;
    }
  }

  return suspiciousBytes == 0;
}

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

String jsonEscape(const String &input) {
  String out;
  out.reserve(input.length() + 8);

  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }

  return out;
}

void rememberPacket(const String &payload, int packetSize, int rssi, float snr) {
  PacketRecord &slot = packetHistory[packetCount % PACKET_HISTORY_SIZE];
  slot.millisAtReceive = millis();
  slot.size = packetSize;
  slot.rssi = rssi;
  slot.snr = snr;
  slot.payload = payload;
  packetCount++;
}

String packetHistoryJson() {
  String json = "[";
  const int stored = min(packetCount, PACKET_HISTORY_SIZE);

  for (int i = 0; i < stored; ++i) {
    const int index = (packetCount - 1 - i + PACKET_HISTORY_SIZE) % PACKET_HISTORY_SIZE;
    const PacketRecord &record = packetHistory[index];

    if (i > 0) {
      json += ',';
    }

    json += "{\"ageMs\":";
    json += String(millis() - record.millisAtReceive);
    json += ",\"size\":";
    json += String(record.size);
    json += ",\"rssi\":";
    json += String(record.rssi);
    json += ",\"snr\":";
    json += String(record.snr, 2);
    json += ",\"payload\":\"";
    json += jsonEscape(record.payload);
    json += "\"}";
  }

  json += "]";
  return json;
}

bool sendLoRaMessage(const String &message) {
  if (!loraReady || message.isEmpty() || message.length() > MAX_LORA_TEXT) {
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

  if (success) {
    Serial.print("LoRa TX: ");
    Serial.println(message);
    Serial.println("LoRa switched back to RX mode");
  } else {
    Serial.println("LoRa TX failed");
  }

  return success;
}

bool sendApplicationMessage(const String &payload, unsigned long &messageId) {
  messageId = ++sentCounter;
  return sendLoRaMessage(makeTaggedMessage(messageId, payload));
}

void connectToWifiIfNeeded() {
  if (!wifiInitialized) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_NAME);
    wifiInitialized = true;
  }

  const unsigned long now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (now - lastWifiAttemptMs < WIFI_RETRY_MS) {
    return;
  }

  lastWifiAttemptMs = now;
  Serial.print("Connecting to WiFi SSID ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void handleRoot() {
  static const char PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Wildfire Tracker Station</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #eef6ef;
      --bg-deep: #dcebdd;
      --panel: rgba(247, 252, 247, 0.93);
      --panel-strong: #fcfffc;
      --ink: #152019;
      --muted: #55705f;
      --accent: #3f8a5d;
      --accent-dark: #2a6844;
      --line: rgba(85, 112, 95, 0.18);
      --good: #3a8b52;
      --watch: #d38b21;
      --warn: #d25a2f;
      --critical: #9b1d1d;
    }
    body {
      margin: 0;
      font-family: "Segoe UI", Arial, sans-serif;
      background:
        radial-gradient(circle at top right, rgba(73, 142, 95, 0.18), transparent 28%),
        radial-gradient(circle at left 20%, rgba(132, 188, 108, 0.15), transparent 24%),
        radial-gradient(circle at 80% 70%, rgba(61, 138, 82, 0.10), transparent 20%),
        linear-gradient(180deg, var(--bg) 0%, var(--bg-deep) 100%);
      color: var(--ink);
    }
    main {
      max-width: 1200px;
      margin: 0 auto;
      padding: 28px 18px 48px;
    }
    .card {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 24px;
      padding: 20px;
      box-shadow: 0 18px 44px rgba(60,38,18,0.09);
      backdrop-filter: blur(8px);
    }
    .stack {
      display: grid;
      gap: 16px;
    }
    .hero {
      display: grid;
      grid-template-columns: 1.5fr 1fr;
      gap: 16px;
      align-items: stretch;
    }
    .hero h1, h2, h3, h4 {
      margin: 0;
    }
    .hero-copy h1 {
      font-size: clamp(2rem, 4vw, 3.4rem);
      line-height: 0.94;
      letter-spacing: -0.04em;
      margin-bottom: 14px;
    }
    .eyebrow {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      margin-bottom: 12px;
      padding: 8px 12px;
      border-radius: 999px;
      background: rgba(63,138,93,0.10);
      border: 1px solid rgba(63,138,93,0.18);
      color: var(--accent-dark);
      font-size: 0.84rem;
      letter-spacing: 0.06em;
      text-transform: uppercase;
      font-weight: 700;
    }
    .hero-copy p {
      margin: 0;
      max-width: 42rem;
      color: var(--muted);
      line-height: 1.5;
    }
    .status-row {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      margin-top: 18px;
    }
    .pill {
      padding: 10px 14px;
      border-radius: 999px;
      background: rgba(255,255,255,0.74);
      border: 1px solid var(--line);
      font-size: 0.94rem;
    }
    .risk-card {
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      min-height: 220px;
      color: white;
      transition: background 220ms ease, transform 220ms ease;
    }
    .risk-card.tone-blue {
      background:
        radial-gradient(circle at top right, rgba(255,255,255,0.18), transparent 36%),
        linear-gradient(135deg, #1f5f98 0%, #2c88c9 52%, #67b7e8 100%);
    }
    .risk-card.tone-green {
      background:
        radial-gradient(circle at top right, rgba(255,255,255,0.18), transparent 36%),
        linear-gradient(135deg, #25643c 0%, #3d9651 52%, #78c97e 100%);
    }
    .risk-card.tone-yellow {
      background:
        radial-gradient(circle at top right, rgba(255,255,255,0.18), transparent 36%),
        linear-gradient(135deg, #8f6c1a 0%, #c39b2b 52%, #e6c55b 100%);
    }
    .risk-card.tone-orange {
      background:
        radial-gradient(circle at top right, rgba(255,255,255,0.18), transparent 36%),
        linear-gradient(135deg, #8b4318 0%, #c36224 52%, #eb8c3f 100%);
    }
    .risk-card.tone-red {
      background:
        radial-gradient(circle at top right, rgba(255,255,255,0.18), transparent 36%),
        linear-gradient(135deg, #661717 0%, #a62424 52%, #da4b3a 100%);
    }
    .risk-badge {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 8px 12px;
      border-radius: 999px;
      width: fit-content;
      background: rgba(255,255,255,0.16);
      border: 1px solid rgba(255,255,255,0.18);
      font-size: 0.88rem;
      letter-spacing: 0.06em;
      text-transform: uppercase;
    }
    .risk-card strong {
      display: block;
      font-size: clamp(2rem, 3vw, 2.8rem);
      margin-top: 12px;
      line-height: 0.95;
    }
    .risk-card p {
      margin: 10px 0 0;
      max-width: 28rem;
      line-height: 1.45;
      color: rgba(255,255,255,0.9);
    }
    .risk-reasons {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      margin-top: 14px;
    }
    .risk-reasons span {
      padding: 7px 10px;
      border-radius: 999px;
      background: rgba(255,255,255,0.14);
      border: 1px solid rgba(255,255,255,0.18);
      font-size: 0.84rem;
    }
    .section-head {
      display: flex;
      justify-content: space-between;
      align-items: baseline;
      gap: 12px;
      margin-bottom: 14px;
    }
    .section-head p {
      margin: 0;
      color: var(--muted);
      font-size: 0.95rem;
    }
    .grid {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    }
    .metric {
      background: var(--panel-strong);
      border: 1px solid var(--line);
      border-radius: 18px;
      padding: 14px;
    }
    .metric h3 {
      margin-bottom: 8px;
      font-size: 0.9rem;
      font-weight: 700;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }
    .metric strong {
      display: block;
      font-size: 1.5rem;
      line-height: 1;
    }
    .metric small {
      display: block;
      margin-top: 6px;
      color: var(--muted);
      line-height: 1.35;
    }
    .dashboard {
      display: grid;
      grid-template-columns: 2.2fr 1fr;
      gap: 16px;
      align-items: start;
    }
    .chart-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 14px;
    }
    .chart-card {
      background: var(--panel-strong);
      border: 1px solid var(--line);
      border-radius: 20px;
      padding: 14px;
      box-shadow: inset 0 1px 0 rgba(255,255,255,0.7);
    }
    .chart-card h3 {
      font-size: 1rem;
      margin-bottom: 4px;
    }
    .chart-card p {
      margin: 0 0 12px;
      color: var(--muted);
      font-size: 0.9rem;
    }
    canvas {
      width: 100%;
      height: 180px;
      display: block;
      border-radius: 16px;
      background:
        radial-gradient(circle at top right, rgba(131, 195, 132, 0.12), transparent 30%),
        linear-gradient(180deg, rgba(255,255,255,0.55), rgba(225,241,228,0.55));
    }
    .side-stack {
      display: grid;
      gap: 16px;
    }
    .summary-list {
      display: grid;
      gap: 10px;
    }
    .summary-row {
      display: flex;
      justify-content: space-between;
      gap: 12px;
      padding-bottom: 10px;
      border-bottom: 1px solid var(--line);
      font-size: 0.96rem;
    }
    .summary-row:last-child {
      padding-bottom: 0;
      border-bottom: 0;
    }
    .summary-row span {
      color: var(--muted);
    }
    .packet-list {
      display: grid;
      gap: 10px;
      max-height: 420px;
      overflow: auto;
      padding-right: 4px;
    }
    .packet-card {
      background: rgba(255,255,255,0.72);
      border: 1px solid var(--line);
      border-radius: 16px;
      padding: 12px 14px;
    }
    details summary {
      list-style: none;
      cursor: pointer;
    }
    details summary::-webkit-details-marker {
      display: none;
    }
    .packet-top {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 14px;
      flex-wrap: wrap;
    }
    .packet-title {
      font-weight: 700;
      font-size: 0.98rem;
    }
    .packet-meta {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      color: var(--muted);
      font-size: 0.88rem;
    }
    .packet-fields {
      display: grid;
      gap: 8px;
      grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
      margin-top: 12px;
    }
    .field {
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 10px;
      background: rgba(255,249,242,0.9);
    }
    .field span {
      display: block;
      font-size: 0.74rem;
      color: var(--muted);
      margin-bottom: 4px;
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }
    .field strong {
      font-size: 0.94rem;
      line-height: 1.25;
      word-break: break-word;
    }
    .raw {
      margin-top: 10px;
      font-family: "Courier New", monospace;
      font-size: 0.85rem;
      word-break: break-word;
      color: #4c3a27;
    }
    form {
      display: grid;
      gap: 10px;
    }
    textarea, button {
      font: inherit;
    }
    textarea {
      min-height: 72px;
      border-radius: 16px;
      border: 1px solid var(--line);
      padding: 12px;
      resize: vertical;
      background: rgba(255,255,255,0.8);
    }
    button {
      width: fit-content;
      border: 0;
      border-radius: 999px;
      padding: 11px 18px;
      background: var(--accent);
      color: white;
      font-weight: 700;
    }
    .compact-note {
      color: var(--muted);
      font-size: 0.9rem;
      margin: 0;
    }
    .legend {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
      margin-top: 12px;
    }
    .legend span {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      font-size: 0.86rem;
      color: var(--muted);
    }
    .legend i {
      width: 10px;
      height: 10px;
      border-radius: 999px;
      display: inline-block;
    }
    .send-box {
      background: linear-gradient(180deg, rgba(255,255,255,0.6), rgba(233,246,235,0.45));
    }
    .eco-note {
      display: grid;
      gap: 10px;
    }
    .eco-note p {
      margin: 0;
      color: var(--muted);
      line-height: 1.5;
    }
    .mini-badges {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }
    .mini-badges span {
      padding: 7px 10px;
      border-radius: 999px;
      background: rgba(63,138,93,0.10);
      border: 1px solid rgba(63,138,93,0.16);
      font-size: 0.84rem;
      color: var(--accent-dark);
    }
    @media (max-width: 980px) {
      .hero,
      .dashboard {
        grid-template-columns: 1fr;
      }
    }
    @media (max-width: 720px) {
      main {
        padding: 18px 12px 32px;
      }
      .chart-grid {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <main>
    <div class="stack">
      <section class="hero">
        <article class="card hero-copy">
          <div class="eyebrow">Field Station Demo</div>
          <h1>Wildfire Tracker Station</h1>
          <p>Real-time field telemetry from the remote LoRa sender. This dashboard watches heat, humidity, soil dryness, and light conditions to highlight sustained fire-prone weather patterns.</p>
          <div class="status-row">
            <div class="pill">WiFi <strong id="wifi">...</strong></div>
            <div class="pill">Gateway <strong id="ip">...</strong></div>
            <div class="pill">Packets <strong id="count">0</strong></div>
            <div class="pill">Node <strong id="nodeName">Waiting</strong></div>
          </div>
        </article>
        <article class="card risk-card tone-blue" id="riskCard">
          <div>
            <div class="risk-badge">Wildfire Conditions</div>
            <strong id="riskLevel">Waiting</strong>
            <p id="riskSummary">No sensor history yet. Risk guidance will appear after packets arrive.</p>
          </div>
          <div class="risk-reasons" id="riskReasons"></div>
        </article>
      </section>

      <section class="dashboard">
        <div class="stack">
          <article class="card">
            <div class="section-head">
              <div>
                <h2>Live Conditions</h2>
                <p>Current readings with demo-friendly wildfire calibration.</p>
              </div>
            </div>
            <div class="grid" id="sensorGrid">
              <div class="metric"><h3>Status</h3><strong>Waiting</strong><small>No sensor packet yet</small></div>
            </div>
          </article>

          <article class="card">
            <div class="section-head">
              <div>
                <h2>Trend Graphs</h2>
                <p>Short-term station history from recent LoRa packets.</p>
              </div>
            </div>
            <div class="chart-grid">
              <div class="chart-card">
                <h3>Temperature vs Humidity</h3>
                <p>Hotter and drier conditions increase fire spread risk.</p>
                <canvas id="tempHumidityChart" width="520" height="180"></canvas>
                <div class="legend">
                  <span><i style="background:#d45a33"></i>Temperature</span>
                  <span><i style="background:#2f7fd4"></i>Humidity</span>
                </div>
              </div>
              <div class="chart-card">
                <h3>Soil Moisture</h3>
                <p>Scaled from 0 to 100 using your dry and saturated thresholds.</p>
                <canvas id="soilChart" width="520" height="180"></canvas>
              </div>
              <div class="chart-card">
                <h3>Light Level</h3>
                <p>Scaled from 0 to 100 from the photoresistor input.</p>
                <canvas id="lightChart" width="520" height="180"></canvas>
              </div>
              <div class="chart-card">
                <h3>Pressure and Altitude</h3>
                <p>Useful for context and station sanity checking.</p>
                <canvas id="pressureAltitudeChart" width="520" height="180"></canvas>
                <div class="legend">
                  <span><i style="background:#6558b5"></i>Pressure</span>
                  <span><i style="background:#2a9d78"></i>Altitude</span>
                </div>
              </div>
            </div>
          </article>
        </div>

        <div class="side-stack">
          <article class="card">
            <div class="section-head">
              <div>
                <h2>Risk Signals</h2>
                <p>Recent averages drive the demo warning logic.</p>
              </div>
            </div>
            <div class="summary-list" id="riskStats">
              <div class="summary-row"><span>Waiting for samples</span><strong>-</strong></div>
            </div>
          </article>

          <article class="card">
            <div class="section-head">
              <div>
                <h2>Station Notes</h2>
                <p>Context for how the dashboard is interpreting the incoming environment.</p>
              </div>
            </div>
            <div class="eco-note">
              <p>This demo treats wildfire risk as a combination of prolonged dryness, low humidity, and rising heat. Single readings alone should not push the station into a warm warning band.</p>
              <div class="mini-badges">
                <span>Soil moisture anchored to your calibration</span>
                <span>Light level normalized to 100</span>
                <span>Altitude locally calibrated</span>
              </div>
            </div>
          </article>

          <article class="card">
            <div class="section-head">
              <div>
                <h2>Wildfire Likelihood</h2>
                <p>Minute-by-minute likelihood trend from recent station conditions.</p>
              </div>
            </div>
            <canvas id="riskChart" width="420" height="180"></canvas>
            <p class="compact-note">Each point is a one-minute average of the recent risk score.</p>
          </article>

          <article class="card send-box">
            <div class="section-head">
              <div>
                <h2>Send Test Packet</h2>
                <p>Quick manual LoRa command from the gateway.</p>
              </div>
            </div>
            <form id="sendForm">
              <textarea id="message" maxlength="220" placeholder="Type a LoRa message to send to the other node"></textarea>
              <button type="submit">Send</button>
            </form>
            <p class="compact-note" id="sendStatus"></p>
          </article>

          <article class="card">
            <div class="section-head">
              <div>
                <h2>Compact Packet Feed</h2>
                <p>Newest packets first. Expand a row only when you need details.</p>
              </div>
            </div>
            <div class="packet-list" id="packets"></div>
          </article>
        </div>
      </section>
    </div>
  </main>

  <script>
    function renderMetric(title, value, detail) {
      return `
        <div class="metric">
          <h3>${title}</h3>
          <strong>${value}</strong>
          <small>${detail}</small>
        </div>
      `;
    }

    function toFixedSafe(value, digits = 1) {
      const numeric = Number(value);
      if (!Number.isFinite(numeric)) {
        return 'n/a';
      }
      return numeric.toFixed(digits);
    }

    function soilMoistureScore(rawValue) {
      const saturatedValue = 1800;
      const dryValue = 3700;
      const numericValue = Number(rawValue);

      if (!Number.isFinite(numericValue)) {
        return null;
      }

      const clamped = Math.min(Math.max(numericValue, saturatedValue), dryValue);
      const normalized = 100 - (((clamped - saturatedValue) / (dryValue - saturatedValue)) * 100);
      return Math.round(normalized);
    }

    function lightLevelScore(rawValue) {
      const maxValue = 3800;
      const numericValue = Number(rawValue);

      if (!Number.isFinite(numericValue)) {
        return null;
      }

      const clamped = Math.min(Math.max(numericValue, 0), maxValue);
      return Math.round((clamped / maxValue) * 100);
    }

    function parsePayload(packet) {
      try {
        const payload = JSON.parse(packet.payload);
        return {
          packet,
          raw: payload,
          node: payload.node || payload.n || 'Unknown',
          seq: payload.seq ?? payload.s ?? null,
          uptimeMs: payload.uptimeMs ?? payload.u ?? null,
          temperatureC: payload.temperatureC ?? payload.bt ?? null,
          pressureHpa: payload.pressureHpa ?? payload.bp ?? null,
          altitudeM: payload.altitudeM ?? payload.bl ?? null,
          humidityPct: payload.dhtHumidityPct ?? payload.dh ?? null,
          dhtTemperatureC: payload.dhtTemperatureC ?? payload.dt ?? null,
          dhtOk: payload.dhtOk ?? payload.dok ?? null,
          soilAnalogRaw: payload.soilAnalogValue ?? payload.sa ?? null,
          soilDigital: payload.soilDigitalValue ?? payload.sd ?? null,
          lightRaw: payload.photoresistorValue ?? payload.lv ?? null
        };
      } catch (error) {
        return null;
      }
    }

    function average(values) {
      const valid = values.filter(value => Number.isFinite(Number(value))).map(Number);
      if (!valid.length) {
        return null;
      }
      return valid.reduce((sum, value) => sum + value, 0) / valid.length;
    }

    function latestFinite(values) {
      for (const value of values) {
        const numeric = Number(value);
        if (Number.isFinite(numeric)) {
          return numeric;
        }
      }
      return null;
    }

    function buildRiskModel(samples) {
      const avgTemp = average(samples.map(sample => sample.temperatureC));
      const avgHumidity = average(samples.map(sample => sample.humidityPct));
      const avgSoil = average(samples.map(sample => soilMoistureScore(sample.soilAnalogRaw)));
      const avgLight = average(samples.map(sample => lightLevelScore(sample.lightRaw)));

      let score = 0;
      const reasons = [];
      let drynessSignals = 0;
      let heatSignals = 0;

      if (avgSoil !== null && avgSoil < 45) {
        score += 8;
        drynessSignals += 1;
      }
      if (avgSoil !== null && avgSoil < 28) {
        score += 18;
        drynessSignals += 1;
        reasons.push('Ground moisture is trending low');
      }
      if (avgSoil !== null && avgSoil < 16) {
        score += 14;
        drynessSignals += 1;
        reasons.push('Soils are very dry');
      }
      if (avgHumidity !== null && avgHumidity < 38) {
        score += 8;
        drynessSignals += 1;
      }
      if (avgHumidity !== null && avgHumidity < 30) {
        score += 16;
        drynessSignals += 1;
        reasons.push('Humidity has stayed low');
      }
      if (avgHumidity !== null && avgHumidity < 22) {
        score += 12;
        drynessSignals += 1;
        reasons.push('Air is very dry');
      }
      if (avgTemp !== null && avgTemp > 29) {
        score += 8;
        heatSignals += 1;
      }
      if (avgTemp !== null && avgTemp > 33) {
        score += 12;
        heatSignals += 1;
        reasons.push('Heat is building');
      }
      if (avgTemp !== null && avgTemp > 37) {
        score += 12;
        heatSignals += 1;
        reasons.push('High temperatures are persisting');
      }
      if (avgLight !== null && avgLight > 82) {
        score += 4;
        reasons.push('Strong daylight exposure');
      }

      if (drynessSignals >= 2 && heatSignals >= 1) {
        score += 10;
      }
      if (drynessSignals >= 3 && heatSignals >= 2) {
        score += 10;
      }

      const likelihood = Math.max(0, Math.min(100, Math.round(score)));
      return {
        avgTemp,
        avgHumidity,
        avgSoil,
        avgLight,
        likelihood,
        reasons: reasons.slice(0, 4)
      };
    }

    function riskAssessment(samples) {
      if (!samples.length) {
        return {
          level: 'Waiting',
          summary: 'No sensor history yet. Risk guidance will appear after packets arrive.',
          reasons: [],
          tone: 'blue',
          likelihood: 0,
          stats: []
        };
      }

      const recent = samples.slice(0, Math.min(samples.length, 24));
      const latestTemp = latestFinite(samples.map(sample => sample.temperatureC));
      const latestHumidity = latestFinite(samples.map(sample => sample.humidityPct));
      const latestSoil = latestFinite(samples.map(sample => soilMoistureScore(sample.soilAnalogRaw)));
      const latestLight = latestFinite(samples.map(sample => lightLevelScore(sample.lightRaw)));
      const model = buildRiskModel(recent);

      let level = 'Blue';
      let tone = 'blue';
      let summary = 'Conditions are cool or moist enough that wildfire spread looks unlikely in this demo.';
      if (model.likelihood >= 80) {
        level = 'Red';
        tone = 'red';
        summary = 'Hot, dry, and moisture-starved conditions are strongly favoring wildfire activity in this demo.';
      } else if (model.likelihood >= 60) {
        level = 'Orange';
        tone = 'orange';
        summary = 'Conditions are becoming favorable for wildfire. Dry fuels plus heat and low humidity deserve attention.';
      } else if (model.likelihood >= 50) {
        level = 'Yellow';
        tone = 'yellow';
        summary = 'Some fire-supporting signals are present. Watch for continued drying and warming.';
      } else if (model.likelihood >= 25) {
        level = 'Green';
        tone = 'green';
        summary = 'Conditions are mostly stable, with only mild support for wildfire spread.';
      }

      return {
        level,
        summary,
        reasons: model.reasons,
        tone,
        likelihood: model.likelihood,
        stats: [
          { label: 'Wildfire likelihood', value: `${model.likelihood}/100` },
          { label: 'Recent average temperature', value: model.avgTemp !== null ? `${toFixedSafe(model.avgTemp, 1)} C` : 'n/a' },
          { label: 'Recent average humidity', value: model.avgHumidity !== null ? `${toFixedSafe(model.avgHumidity, 1)} %` : 'n/a' },
          { label: 'Recent average soil moisture', value: model.avgSoil !== null ? `${Math.round(model.avgSoil)}/100` : 'n/a' },
          { label: 'Current temperature', value: latestTemp !== null ? `${toFixedSafe(latestTemp, 1)} C` : 'n/a' },
          { label: 'Current humidity', value: latestHumidity !== null ? `${toFixedSafe(latestHumidity, 1)} %` : 'n/a' },
          { label: 'Current soil moisture', value: latestSoil !== null ? `${Math.round(latestSoil)}/100` : 'n/a' },
          { label: 'Current light level', value: latestLight !== null ? `${Math.round(latestLight)}/100` : 'n/a' }
        ]
      };
    }

    function wildfireLikelihoodTimeline(samples) {
      const minuteBuckets = new Map();

      samples.forEach(sample => {
        const minuteOffset = Math.floor(sample.packet.ageMs / 60000);
        if (!minuteBuckets.has(minuteOffset)) {
          minuteBuckets.set(minuteOffset, []);
        }
        minuteBuckets.get(minuteOffset).push(sample);
      });

      return Array.from(minuteBuckets.entries())
        .sort((a, b) => b[0] - a[0])
        .map(([minuteOffset, bucketSamples]) => ({
          label: `${minuteOffset}m`,
          value: buildRiskModel(bucketSamples).likelihood
        }));
    }

    function applyRiskView(assessment) {
      const riskCard = document.getElementById('riskCard');
      riskCard.className = `card risk-card tone-${assessment.tone}`;
      document.getElementById('riskLevel').textContent = assessment.level;
      document.getElementById('riskSummary').textContent = assessment.summary;
      document.getElementById('riskReasons').innerHTML = assessment.reasons.length
        ? assessment.reasons.map(reason => `<span>${reason}</span>`).join('')
        : '<span>No elevated fire-weather signals yet</span>';
      document.getElementById('riskStats').innerHTML = assessment.stats.length
        ? assessment.stats.map(stat => `<div class="summary-row"><span>${stat.label}</span><strong>${stat.value}</strong></div>`).join('')
        : '<div class="summary-row"><span>Waiting for samples</span><strong>-</strong></div>';
    }

    function chartSeries(samples, getter) {
      return samples
        .slice()
        .reverse()
        .map(getter)
        .filter(point => Number.isFinite(Number(point)));
    }

    function drawChart(canvasId, seriesList, options = {}) {
      const canvas = document.getElementById(canvasId);
      if (!canvas) {
        return;
      }

      const ctx = canvas.getContext('2d');
      const width = canvas.width;
      const height = canvas.height;
      ctx.clearRect(0, 0, width, height);

      ctx.fillStyle = '#fffdf9';
      ctx.fillRect(0, 0, width, height);

      const allValues = seriesList.flatMap(series => series.values).filter(value => Number.isFinite(Number(value)));
      if (!allValues.length) {
        ctx.fillStyle = '#6e5b49';
        ctx.font = '14px Segoe UI';
        ctx.fillText('Waiting for data', 18, 30);
        return;
      }

      let min = options.min ?? Math.min(...allValues);
      let max = options.max ?? Math.max(...allValues);
      const paddingRatio = options.paddingRatio ?? 0.18;
      const minSpan = options.minSpan ?? 1;
      let span = max - min;
      if (span < minSpan) {
        const midpoint = (max + min) / 2;
        min = midpoint - (minSpan / 2);
        max = midpoint + (minSpan / 2);
        span = max - min;
      }
      if (!options.lockRange) {
        const padding = span * paddingRatio;
        min -= padding;
        max += padding;
      }
      if (options.minFloor != null) {
        min = Math.max(min, options.minFloor);
      }
      if (options.maxCeil != null) {
        max = Math.min(max, options.maxCeil);
      }
      if (min >= max) {
        min -= 1;
        max += 1;
      }
      const padding = 24;
      const innerWidth = width - padding * 2;
      const innerHeight = height - padding * 2;

      ctx.strokeStyle = 'rgba(109, 91, 73, 0.16)';
      ctx.lineWidth = 1;
      for (let i = 0; i < 4; i++) {
        const y = padding + (innerHeight / 3) * i;
        ctx.beginPath();
        ctx.moveTo(padding, y);
        ctx.lineTo(width - padding, y);
        ctx.stroke();
      }

      seriesList.forEach(series => {
        if (!series.values.length) {
          return;
        }
        let seriesMin = min;
        let seriesMax = max;
        if (options.normalizeEachSeries) {
          seriesMin = Math.min(...series.values);
          seriesMax = Math.max(...series.values);
          let seriesSpan = seriesMax - seriesMin;
          const seriesMinSpan = series.minSpan ?? options.minSpan ?? 1;
          if (seriesSpan < seriesMinSpan) {
            const midpoint = (seriesMax + seriesMin) / 2;
            seriesMin = midpoint - (seriesMinSpan / 2);
            seriesMax = midpoint + (seriesMinSpan / 2);
            seriesSpan = seriesMax - seriesMin;
          }
          const seriesPadding = seriesSpan * (series.paddingRatio ?? options.paddingRatio ?? 0.18);
          seriesMin -= seriesPadding;
          seriesMax += seriesPadding;
          if (seriesMin >= seriesMax) {
            seriesMin -= 1;
            seriesMax += 1;
          }
        }
        ctx.strokeStyle = series.color;
        ctx.lineWidth = 3;
        ctx.beginPath();
        series.values.forEach((value, index) => {
          const x = padding + ((series.values.length === 1 ? 0.5 : index / (series.values.length - 1)) * innerWidth);
          const y = padding + innerHeight - (((value - seriesMin) / (seriesMax - seriesMin)) * innerHeight);
          if (index === 0) {
            ctx.moveTo(x, y);
          } else {
            ctx.lineTo(x, y);
          }
        });
        ctx.stroke();
      });

      ctx.fillStyle = '#6e5b49';
      ctx.font = '12px Segoe UI';
      ctx.fillText(`${toFixedSafe(max, 1)}`, 8, padding + 4);
      ctx.fillText(`${toFixedSafe(min, 1)}`, 8, height - padding + 4);
    }

    function renderSensors(data) {
      const samples = data.packets.map(parsePayload).filter(Boolean);
      const latest = samples[0];
      if (!latest) {
        document.getElementById('sensorGrid').innerHTML =
          '<div class="metric"><h3>Status</h3><strong>Waiting</strong><small>No sensor packet yet</small></div>';
        applyRiskView(riskAssessment([]));
        return;
      }

      const soilAnalogRaw = latest.soilAnalogRaw;
      const soilAnalogScore = soilMoistureScore(soilAnalogRaw);
      const lightRaw = latest.lightRaw;
      const lightScore = lightLevelScore(lightRaw);
      document.getElementById('nodeName').textContent = latest.node;

      const metrics = [
        renderMetric('Node', latest.node, `Packet ${latest.seq ?? '-'} | ${Math.round(latest.packet.ageMs / 1000)}s ago`),
        renderMetric('Air Temperature', latest.temperatureC != null ? `${toFixedSafe(latest.temperatureC, 2)} C` : 'n/a', 'From BMP280'),
        renderMetric('Air Pressure', latest.pressureHpa != null ? `${toFixedSafe(latest.pressureHpa, 2)} hPa` : 'n/a', 'From BMP280'),
        renderMetric('Altitude', latest.altitudeM != null ? `${toFixedSafe(latest.altitudeM, 2)} m` : 'n/a', 'Calibrated for this site'),
        renderMetric('DHT11 Temperature', latest.dhtTemperatureC != null ? `${toFixedSafe(latest.dhtTemperatureC, 2)} C` : 'n/a', latest.dhtOk ? 'Digital temperature reading' : 'Sensor unavailable'),
        renderMetric('Humidity', latest.humidityPct != null ? `${toFixedSafe(latest.humidityPct, 2)} %` : 'n/a', latest.dhtOk ? 'From DHT11' : 'Sensor unavailable'),
        renderMetric('Soil Moisture', soilAnalogScore != null ? `${soilAnalogScore}/100` : 'n/a', soilAnalogRaw != null ? `Raw ${soilAnalogRaw}` : 'No analog reading'),
        renderMetric('Soil Contact', latest.soilDigital != null ? `${latest.soilDigital}` : 'n/a', 'Digital dry/wet threshold'),
        renderMetric('Light Level', lightScore != null ? `${lightScore}/100` : 'n/a', lightRaw != null ? `Raw ${lightRaw}` : 'No light reading'),
        renderMetric('Radio Link', `${latest.packet.rssi} dBm`, `SNR ${latest.packet.snr} dB`)
      ];

      document.getElementById('sensorGrid').innerHTML = metrics.join('');
      const assessment = riskAssessment(samples);
      applyRiskView(assessment);
      const riskTimeline = wildfireLikelihoodTimeline(samples);
      drawChart('tempHumidityChart', [
        { color: '#d45a33', values: chartSeries(samples, sample => sample.temperatureC), minSpan: 1.2 },
        { color: '#2f7fd4', values: chartSeries(samples, sample => sample.humidityPct), minSpan: 4 }
      ], { normalizeEachSeries: true, paddingRatio: 0.22 });
      drawChart('soilChart', [
        { color: '#8c5b2f', values: chartSeries(samples, sample => soilMoistureScore(sample.soilAnalogRaw)) }
      ], { minFloor: 0, maxCeil: 100, minSpan: 8, paddingRatio: 0.22 });
      drawChart('lightChart', [
        { color: '#e2a22c', values: chartSeries(samples, sample => lightLevelScore(sample.lightRaw)) }
      ], { minFloor: 0, maxCeil: 100, minSpan: 8, paddingRatio: 0.22 });
      drawChart('pressureAltitudeChart', [
        { color: '#6558b5', values: chartSeries(samples, sample => sample.pressureHpa), minSpan: 0.4 },
        { color: '#2a9d78', values: chartSeries(samples, sample => sample.altitudeM), minSpan: 0.6 }
      ], { normalizeEachSeries: true, paddingRatio: 0.22 });
      drawChart('riskChart', [
        { color: assessment.tone === 'red' ? '#b92222' : assessment.tone === 'orange' ? '#cb6a1e' : assessment.tone === 'yellow' ? '#c9a129' : assessment.tone === 'green' ? '#3d9651' : '#2c88c9', values: riskTimeline.map(point => point.value), minSpan: 12 }
      ], { minFloor: 0, maxCeil: 100, minSpan: 20, paddingRatio: 0.18 });
    }

    function formatFieldValue(value) {
      if (typeof value === 'boolean') {
        return value ? 'true' : 'false';
      }
      if (value === null || value === undefined) {
        return 'n/a';
      }
      return String(value);
    }

    function displayFieldValue(key, value) {
      if (key === 'sa' || key === 'soilAnalogValue') {
        const score = soilMoistureScore(value);
        if (score == null) {
          return formatFieldValue(value);
        }
        return `${score}/100 (raw ${value})`;
      }

      if (key === 'lv' || key === 'photoresistorValue') {
        const score = lightLevelScore(value);
        if (score == null) {
          return formatFieldValue(value);
        }
        return `${score}/100 (raw ${value})`;
      }

      return formatFieldValue(value);
    }

    function renderPacket(packet) {
      const sample = parsePayload(packet);
      const payload = sample ? sample.raw : null;

      if (!payload) {
        return `
          <article class="packet-card">
            <div class="packet-top">
              <div class="packet-title">Non-JSON packet</div>
              <div class="packet-meta">
                <div>${Math.round(packet.ageMs / 1000)}s ago</div>
                <div>${packet.rssi} dBm</div>
              </div>
            </div>
            <div class="raw">${packet.payload}</div>
          </article>
        `;
      }

      const labels = {
        n: 'Node',
        node: 'Node',
        s: 'Packet Number',
        seq: 'Packet Number',
        u: 'Uptime (ms)',
        uptimeMs: 'Uptime (ms)',
        ba: 'BMP280 I2C Address',
        i2cAddr: 'BMP280 I2C Address',
        bt: 'Air Temperature (C)',
        temperatureC: 'Air Temperature (C)',
        bp: 'Air Pressure (hPa)',
        pressureHpa: 'Air Pressure (hPa)',
        bl: 'Estimated Altitude (m)',
        altitudeM: 'Estimated Altitude (m)',
        dok: 'DHT11 Reading Valid',
        dhtOk: 'DHT11 Reading Valid',
        dt: 'DHT11 Temperature (C)',
        dhtTemperatureC: 'DHT11 Temperature (C)',
        dh: 'Humidity (%)',
        dhtHumidityPct: 'Humidity (%)',
        sa: 'Soil Moisture Analog',
        soilAnalogValue: 'Soil Moisture Analog',
        sd: 'Soil Moisture Digital',
        soilDigitalValue: 'Soil Moisture Digital',
        lv: 'Light Level',
        photoresistorValue: 'Light Level'
      };
      const fields = Object.entries(payload).map(([key, value]) => `
        <div class="field">
          <span>${labels[key] || key}</span>
          <strong>${displayFieldValue(key, value)}</strong>
        </div>
      `).join('');

      const quickSummary = [
        sample.temperatureC != null ? `${toFixedSafe(sample.temperatureC, 1)} C` : null,
        sample.humidityPct != null ? `${toFixedSafe(sample.humidityPct, 0)} % RH` : null,
        sample.soilAnalogRaw != null ? `${soilMoistureScore(sample.soilAnalogRaw)}/100 soil` : null
      ].filter(Boolean).join(' | ');

      return `
        <article class="packet-card">
          <details>
            <summary>
              <div class="packet-top">
                <div>
                  <div class="packet-title">${sample.node} packet ${sample.seq ?? '-'}</div>
                  <div class="packet-meta">
                    <div>${Math.round(packet.ageMs / 1000)}s ago</div>
                    <div>${packet.rssi} dBm</div>
                    <div>SNR ${packet.snr} dB</div>
                    <div>${packet.size} bytes</div>
                  </div>
                </div>
                <div class="compact-note">${quickSummary || 'Tap to inspect packet'}</div>
              </div>
            </summary>
            <div class="packet-fields">${fields}</div>
          </details>
        </article>
      `;
    }

    async function refresh() {
      const response = await fetch('/packets');
      const data = await response.json();

      document.getElementById('wifi').textContent = data.wifiConnected ? 'connected' : 'disconnected';
      document.getElementById('ip').textContent = data.ip || 'none';
      document.getElementById('count').textContent = data.packetCount;
      renderSensors(data);

      const cards = data.packets.map(renderPacket).join('');
      document.getElementById('packets').innerHTML =
        cards || '<div class="metric"><h3>Status</h3><strong>No packets</strong><small>No received packets yet</small></div>';
    }

    document.getElementById('sendForm').addEventListener('submit', async event => {
      event.preventDefault();
      const message = document.getElementById('message').value.trim();
      if (!message) {
        return;
      }

      const response = await fetch('/send?message=' + encodeURIComponent(message), { method: 'POST' });
      const result = await response.json();
      document.getElementById('sendStatus').textContent = result.ok ? 'Sent' : 'Send failed';
      if (result.ok) {
        document.getElementById('message').value = '';
      }
      refresh();
    });

    refresh();
    setInterval(refresh, 2000);
  </script>
</body>
</html>
)HTML";

  server.send_P(200, "text/html", PAGE);
}

void handlePackets() {
  String body = "{";
  body += "\"wifiConnected\":";
  body += WiFi.status() == WL_CONNECTED ? "true" : "false";
  body += ",\"loraReady\":";
  body += loraReady ? "true" : "false";
  body += ",\"ip\":\"";
  body += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  body += "\",\"packetCount\":";
  body += String(packetCount);
  body += ",\"packets\":";
  body += packetHistoryJson();
  body += "}";

  server.send(200, "application/json", body);
}

void handleSend() {
  const String message = server.arg("message");
  unsigned long messageId = 0;
  const bool ok = sendApplicationMessage(message, messageId);

  String body = "{\"ok\":";
  body += ok ? "true" : "false";
  body += ",\"messageId\":";
  body += String(messageId);
  body += ",\"message\":\"";
  body += jsonEscape(message);
  body += "\"}";

  server.send(ok ? 200 : 400, "application/json", body);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/packets", HTTP_GET, handlePackets);
  server.on("/send", HTTP_POST, handleSend);
  server.onNotFound(handleNotFound);
  server.begin();
}

bool setupLoRa() {
  SPI.begin(18, 19, 23, LORA_SS_PIN);
  LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  LoRa.setSPIFrequency(LORA_SPI_FREQUENCY);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    loraReady = false;
    return false;
  }

  LoRa.enableCrc();
  LoRa.setTxPower(17);
  LoRa.receive();
  loraReady = true;
  return true;
}

void readLoRaPackets() {
  if (!loraReady) {
    return;
  }

  const int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) {
    const unsigned long now = millis();
    if (now - lastRxCheckLogMs >= 5000) {
      lastRxCheckLogMs = now;
      Serial.println("LoRa RX idle");
    }
    return;
  }

  String payload;
  payload.reserve(min(packetSize, static_cast<int>(MAX_PACKET_TEXT)));

  while (LoRa.available()) {
    const char c = static_cast<char>(LoRa.read());
    if (payload.length() < MAX_PACKET_TEXT) {
      payload += c;
    }
  }

  unsigned long ackId = 0;
  if (extractAckId(payload, ackId)) {
    lastAckedMessageId = ackId;
    lastAckAtMs = millis();
    Serial.print("LoRa ACK RX: ");
    Serial.println(ackId);
    rememberPacket(payload, packetSize, LoRa.packetRssi(), LoRa.packetSnr());
    return;
  }

  unsigned long messageId = 0;
  String appPayload;
  if (extractTaggedId(payload, "MSG:", messageId, appPayload)) {
    sendLoRaMessage(makeAckMessage(messageId));
    payload = appPayload;
  }

  if (!isLikelyTextPayload(payload)) {
    Serial.println("Dropped non-text payload");
    return;
  }

  rememberPacket(payload, packetSize, LoRa.packetRssi(), LoRa.packetSnr());

  Serial.print("LoRa RX [");
  Serial.print(packetSize);
  Serial.print(" bytes, RSSI ");
  Serial.print(LoRa.packetRssi());
  Serial.print(", SNR ");
  Serial.print(LoRa.packetSnr(), 2);
  Serial.print("]: ");
  Serial.println(payload);
}

void readSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  const String message = Serial.readStringUntil('\n');
  String trimmed = message;
  trimmed.trim();

  if (trimmed.isEmpty()) {
    return;
  }

  if (trimmed.equalsIgnoreCase("PING")) {
    unsigned long messageId = 0;
    sendApplicationMessage("PING", messageId);
    return;
  }

  unsigned long messageId = 0;
  sendApplicationMessage(trimmed, messageId);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Booting ESP32 LoRa gateway");
  clearPacketHistory();

  if (!setupLoRa()) {
    Serial.println("LoRa init failed. Gateway will continue without radio. Check frequency and wiring.");
  }

  WiFi.persistent(false);
  connectToWifiIfNeeded();
  setupServer();

  Serial.println("HTTP server started");
  Serial.println("Serial commands: type a line to transmit, or PING for a test packet");
  if (loraReady) {
    Serial.println("LoRa receiver armed");
  } else {
    Serial.println("LoRa unavailable");
  }
}

void loop() {
  connectToWifiIfNeeded();
  server.handleClient();
  readLoRaPackets();
  readSerialCommands();

  static wl_status_t lastWifiStatus = WL_IDLE_STATUS;
  const wl_status_t currentStatus = WiFi.status();
  if (currentStatus != lastWifiStatus) {
    lastWifiStatus = currentStatus;
    if (currentStatus == WL_CONNECTED) {
      Serial.print("WiFi connected. IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi disconnected");
    }
  }

  delay(5);
}
