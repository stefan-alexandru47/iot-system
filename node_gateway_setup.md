# Node: Gateway – Setup Guide

## Hardware Requirements
- 1 × ESP32 development board (Freenove ESP32-WROVER recommended)
- 1 × LoRa module (SX1276/78 based)
- Jumper wires, breadboard/protoboard
- Computer with Wi-Fi to connect to the Gateway’s dashboard

## Wiring
**LoRa Module (same as Sender node):**
The LoRa module wiring is **exactly the same** as on the Sender node.

Refer to:
- The full pin table in `node_sender_design.md`
- The LoRa photos in `node_sender_setup.md` (or in the `images/` folder)

**Important:** Use **3.3 V only** – **never** connect the LoRa module to 5 V.

**Gateway board overview:**
![ESP32 Board (Gateway)](images/esp32_board.jpg)

**LoRa module close-up:**
![LoRa Module](images/lora_module_closeup.jpg)

**No other sensors or hardware** are required on the Gateway node.

## Software Setup (PlatformIO)
1. Open the repository in VS Code with the PlatformIO extension.
2. The single `platformio.ini` in the root contains separate environments for `sender` and `gateway`.
3. Required libraries (installed automatically):
    - LoRa by sandeepmistry
    - WiFi / WebServer from the ESP32 Arduino framework

## Build & Upload (Gateway only)
In PowerShell / Terminal run:
```powershell
$env:PLATFORMIO_CORE_DIR="$PWD\.platformio-core"
pio run -e gateway -t upload --upload-port COM8   # ← replace COM8 with your Gateway board's port
```

## Running the Node

1. Power the Gateway board via USB.
2. Wait approximately 15–20 seconds for Wi-Fi initialisation.
3. The Gateway joins the Wi-Fi network configured in the source code.
4. Connect your laptop/phone to that same Wi-Fi network.
5. Open a browser and go to the IP shown in serial output.
5. The live dashboard will display current sensor readings, trend graphs, and the wildfire-risk status.

#### User control:

- Edit constants in `src/node_gateway/main.cpp`:
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

A third party can now set up and run the complete system using the root `platformio.ini` together with `src/node_gateway/main.cpp` and `src/node_sender/main.cpp`.

---
