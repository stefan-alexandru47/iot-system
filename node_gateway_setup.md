# Node: Gateway – Setup Guide

## Hardware Requirements
- 1 × ESP32 development board (Freenove ESP32-WROVER recommended)
- 1 × LoRa module (SX1276/78 based)
- Jumper wires, breadboard/protoboard
- Computer with Wi-Fi to connect to the Gateway’s dashboard

## Wiring
**LoRa Module:**
Exactly the same pinout as the Sender node (see `node_sender_design.md` table). Use 3.3 V logic only.

No other sensors are required on the Gateway.

## Software Setup (PlatformIO)
1. Open the repository in VS Code with the PlatformIO extension.
2. The single `platformio.ini` in the root contains separate environments for `sender` and `gateway`.
3. Required libraries (installed automatically):
    - LoRa by sandeepmistry
    - ArduinoJson
    - ESPAsyncWebServer & AsyncTCP (for the dashboard)

## Build & Upload (Gateway only)
In PowerShell / Terminal run:
```powershell
$env:PLATFORMIO_CORE_DIR="$PWD\.platformio-core"
pio run -e gateway -t upload --upload-port COM8   # ← replace COM8 with your Gateway board's port
```

## Running the Node

1. Power the Gateway board via USB.
2. Wait approximately 15–20 seconds for Wi-Fi initialisation.
3. Connect your laptop/phone to the Wi-Fi network named "Wildfire-Gateway" (or the SSID shown in serial monitor).
4. Open a browser and go to ```http://192.168.4.1``` (or the IP shown in serial output).
5. The live dashboard will display current sensor readings, trend graphs, and the wildfire-risk status.

#### User control:

- Edit constants in ```node_gateway/gateway.cpp```:
  - Wi-Fi SSID/password (if using existing network instead of AP mode)
  - Dashboard refresh rate
  - Wildfire heuristic thresholds (if you want to tune them)


#### Verification:

- Sender packets should appear on the dashboard within 1–2 seconds.
- Graphs update in real time and the risk indicator changes colour based on the heuristic.

## Troubleshooting

- No Wi-Fi network → check serial monitor for IP/SSID output.
- Dashboard not loading → ensure you are connected to the Gateway’s Wi-Fi and using the correct IP.
- No data appearing → confirm Sender is transmitting and LoRa antennas are attached.
- High RAM usage warning → the history buffer is intentionally small for this demo (scalable with external storage in future).

A third party can now set up and run the complete system using only the files in node_gateway/ together with the Sender node.

---
