# First-Time Setup Guide

This guide is for someone receiving this system for the first time, with no prior knowledge of how it was assembled.

Use this document as the step-by-step setup process.

Use the wiring maps in [`README.md`](../README.md) whenever this guide tells you to connect a module.

## What this system is

You have two ESP32 boards:

1. a `Sender` board
2. a `Gateway` board

What each one does:

- the `Sender` reads sensors and sends the data over LoRa
- the `Gateway` receives the LoRa packets and shows them on a web page over Wi-Fi

To use the system, both boards must be powered, and both LoRa modules must be connected correctly.

## Before you start

You will need:

- both ESP32 boards
- both LoRa modules
- the attached sensors for the sender
- USB cables for both boards
- a Windows computer with this project folder
- access to a Wi-Fi network that the gateway can join
- a phone or laptop on that same Wi-Fi network to view the dashboard

## Step 1: Identify the boards

Current expected working layout:

- `Sender` board: `COM7`
- `Gateway` board: `COM9`

If your COM ports are different on your machine, that is okay. The roles matter more than the COM numbers.

If you are not sure which board is which:

1. plug in one ESP32
2. see which COM port appears
3. unplug it
4. plug in the other ESP32
5. see which different COM port appears

Keep a note of which physical board is the sender and which is the gateway.

## Step 2: Wire the LoRa module on both boards

Both boards need a LoRa module wired the same way.

Open [`README.md`](../README.md) and go to:

- `LoRa module wiring`

Connect every LoRa pin exactly as shown in that table.

Important:

- do not use `5V` on the LoRa module
- use `3.3V`

Do this for:

1. the sender board
2. the gateway board

## Step 3: Wire the sender sensors

The sender board has the sensors.

Open [`README.md`](../README.md) and use these sections one by one:

1. `GY-BMP280`
2. `DHT11 temperature/humidity sensor`
3. `Soil moisture module with LM393`
4. `Photoresistor`

Connect each module according to its table.

Important BMP280 note:

For the current BMP280 board to work in I2C mode, it must also have:

- `CSB -> 3.3V`
- `SDO -> GND`

If those two wires are missing, the sensor may not be detected.

## Step 4: Connect both boards to the computer

Plug in:

1. the sender ESP32
2. the gateway ESP32

Wait a few seconds for Windows to detect both COM ports.

## Step 5: Confirm PlatformIO is available

Open PowerShell in this project folder and use:

```powershell
$env:PLATFORMIO_CORE_DIR="$PWD\.platformio-core"
& "$env:APPDATA\Python\Python312\Scripts\pio.exe" device list
```

This should list the connected serial devices.

## Step 6: Configure the gateway for your own Wi-Fi

The gateway must join your Wi-Fi network so the dashboard can be viewed in a browser.

Open:

- `src/gateway.cpp`

At the top of the file you will find:

```cpp
constexpr char WIFI_SSID[] = "...";
constexpr char WIFI_PASSWORD[] = "...";
```

Replace those values with your own Wi-Fi details:

- `WIFI_SSID` = your Wi-Fi network name
- `WIFI_PASSWORD` = your Wi-Fi password

Use a normal `2.4 GHz` or mixed Wi-Fi network that the ESP32 can join.

Important:

- your phone or laptop that will view the dashboard must be on that same Wi-Fi network

## Step 7: Build and upload the sender firmware

In PowerShell, run:

```powershell
$env:PLATFORMIO_CORE_DIR="$PWD\.platformio-core"
& "$env:APPDATA\Python\Python312\Scripts\pio.exe" run -e sender -t upload --upload-port COM7
```

If your sender is not on `COM7`, replace `COM7` with your sender COM port.

What this does:

- compiles the sender firmware
- flashes it to the sender ESP32

## Step 8: Build and upload the gateway firmware

In PowerShell, run:

```powershell
$env:PLATFORMIO_CORE_DIR="$PWD\.platformio-core"
& "$env:APPDATA\Python\Python312\Scripts\pio.exe" run -e gateway -t upload --upload-port COM9
```

If your gateway is not on `COM9`, replace `COM9` with your gateway COM port.

What this does:

- compiles the gateway firmware
- flashes it to the gateway ESP32

## Step 9: Power both boards and wait for startup

After flashing:

1. leave both boards powered
2. wait about `10 to 20 seconds`

During this time:

- the sender starts reading sensors and transmitting LoRa packets
- the gateway starts Wi-Fi, starts the web server, and listens for LoRa packets

## Step 10: Find the gateway IP address

The gateway needs to join your Wi-Fi and receive an IP address from your router.

There are two common ways to find it.

### Method A: Read serial output

Watch gateway serial output and look for a line like:

```text
WiFi connected. IP: 192.168.x.x
```

### Method B: Check your router

Open your router’s connected device list and look for the ESP32 device on the network.

## Step 11: Open the dashboard

On your phone or laptop, while connected to the same Wi-Fi network, open:

```text
http://<gateway-ip-address>
```

Example:

```text
http://192.168.1.217
```

If everything is working, the dashboard should show:

- live sensor cards
- graphs
- packet history
- wildfire condition status

## Step 12: Verify the sender is actually sending

If the page loads but no data appears:

1. confirm the sender board is powered
2. confirm the gateway board is powered
3. confirm both LoRa modules are wired according to the map in `README.md`
4. confirm the sender sensors are wired according to the map in `README.md`
5. confirm the gateway joined Wi-Fi successfully

## Step 13: Basic troubleshooting

### If the BMP280 is not working

Check:

- `SDA -> GPIO21`
- `SCL -> GPIO22`
- `CSB -> 3.3V`
- `SDO -> GND`

### If the DHT11 is not working

Check:

- `OUT -> GPIO4`

### If soil moisture is missing

Check:

- `AO -> GPIO34`
- `DO -> GPIO25`

### If light level is missing

Check:

- the divider output is really connected to `GPIO35`
- the `10k ohm` resistor is really connected from `GPIO35` to `GND`

### If the dashboard cannot be opened

Check:

- the gateway Wi-Fi credentials in `src/gateway.cpp`
- the gateway actually received an IP
- your phone/laptop is on the same Wi-Fi network

### If LoRa packets do not appear

Check:

- both LoRa modules are wired correctly
- both use `3.3V`
- both boards are powered
- antennas are attached
- sender and gateway are both running the correct firmware

## Step 14: Understanding what the dashboard means

The dashboard is a demo, not a real wildfire prediction system.

It uses:

- low soil moisture
- low humidity
- elevated temperature
- strong daylight as a supporting factor

to estimate whether conditions are becoming more favorable for wildfire.

The color band goes from:

1. blue
2. green
3. yellow
4. orange
5. red

The warmer the color, the more favorable the conditions are for wildfire in the demo logic.

## Step 15: Where to change things later

If you want to change Wi-Fi:

- edit `src/gateway.cpp`

If you want to change sensor scaling or wildfire logic:

- edit `src/gateway.cpp`

If you want to change sender timing, LoRa payloads, or altitude calibration:

- edit `src/sender.cpp`

## Final reference

For all pin maps and technical details, always refer back to:

- [`README.md`](../README.md)
