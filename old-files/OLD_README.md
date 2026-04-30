# ESP32 LoRa Wildfire Tracker

This project uses two ESP32 boards and two LoRa radio modules:

- a `Sender` node that reads sensors and transmits telemetry over LoRa
- a `Gateway` node that receives LoRa packets, joins Wi-Fi, and serves a browser dashboard

The dashboard is designed as a wildfire-tracker demo. It shows live sensor values, trend graphs, packet history, and a derived `Wildfire Conditions` status based on sustained dryness, humidity, temperature, and light conditions.

## Current board roles

Current development/test mapping:

| Role | Firmware | Port | Notes |
|---|---|---|---|
| Gateway / base ESP32 | `src/gateway.cpp` | `COM9` | Hosts the Wi-Fi dashboard |
| Sender ESP32 | `src/sender.cpp` | `COM7` | Reads sensors and transmits packets |

Current USB note:

- the sender on `COM7` is the board currently using the `USB-C to USB-A` adapter

## System overview

### Sender responsibilities

- reads all attached sensors
- formats telemetry as compact JSON
- sends a LoRa packet every `1000 ms`
- receives gateway messages and replies with `ACK:<id>`

### Gateway responsibilities

- receives LoRa packets from the sender
- replies with acknowledgements
- stores recent packet history in memory
- joins a Wi-Fi network as a client
- serves the dashboard and packet API over HTTP

## Repository structure

| Path | Purpose |
|---|---|
| `src/sender.cpp` | Sender firmware |
| `src/gateway.cpp` | Gateway firmware |
| `platformio.ini` | PlatformIO environments and dependencies |
| `modules.md` | Short hardware mapping reference |
| `FIRST_TIME_SETUP.md` | Beginner-friendly first setup guide |

## PlatformIO environments

The project currently uses these PlatformIO environments:

- `sender`
- `gateway`

Example upload commands:

```powershell
$env:PLATFORMIO_CORE_DIR="$PWD\.platformio-core"
& "$env:APPDATA\Python\Python312\Scripts\pio.exe" run -e sender -t upload --upload-port COM7
& "$env:APPDATA\Python\Python312\Scripts\pio.exe" run -e gateway -t upload --upload-port COM9
```

Example build commands:

```powershell
$env:PLATFORMIO_CORE_DIR="$PWD\.platformio-core"
& "$env:APPDATA\Python\Python312\Scripts\pio.exe" run -e sender
& "$env:APPDATA\Python\Python312\Scripts\pio.exe" run -e gateway
```

## Radio configuration

Both boards currently use the same LoRa settings:

| Setting | Value |
|---|---|
| Frequency | `915E6` |
| SPI frequency | `8E6` |
| CRC | enabled |
| TX power | `17` |

The packet protocol is:

- application packet: `MSG:<id>:<payload>`
- acknowledgement: `ACK:<id>`

## Gateway Wi-Fi configuration

The gateway Wi-Fi settings are currently hardcoded in `src/gateway.cpp`.

Current fields:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `DEVICE_NAME`

Current test IP during development has been:

- `http://192.168.1.217`

Important:

- the IP address may change if the router assigns a different address after reboot
- the phone viewing the dashboard must be on the same Wi-Fi network as the gateway ESP32

## Hardware maps

### LoRa module wiring

This wiring applies to both the sender and the gateway.

| LoRa pin | ESP32 pin | Function |
|---|---|---|
| `3V3` | `3.3V` | Power |
| `GND` | `GND` | Ground |
| `MISO` | `GPIO19` | SPI MISO |
| `MOSI` | `GPIO23` | SPI MOSI |
| `SCK` | `GPIO18` | SPI clock |
| `NSS` | `GPIO5` | Chip select |
| `RST` | `GPIO14` | Reset |
| `DIO0` | `GPIO2` | LoRa interrupt |

Important:

- use `3.3V`, not `5V`

### Sender sensor map

#### GY-BMP280

This is the pressure/temperature sensor on the sender.

| BMP280 pin | ESP32 pin | Function |
|---|---|---|
| `VCC` | `3.3V` | Power |
| `GND` | `GND` | Ground |
| `SCL` | `GPIO22` | I2C clock |
| `SDA` | `GPIO21` | I2C data |
| `CSB` | `3.3V` | Forces I2C mode |
| `SDO` | `GND` | Sets I2C address `0x76` |

Firmware notes:

- primary I2C address checked: `0x76`
- secondary I2C address checked: `0x77`
- current working board is a `GY-BMP280`, not a `BME280`
- altitude is software-calibrated with an offset in `src/sender.cpp`

#### DHT11 temperature/humidity sensor

| DHT11 pin | ESP32 pin | Function |
|---|---|---|
| `VCC` | `3.3V` or module VCC input | Power |
| `GND` | `GND` | Ground |
| `OUT` | `GPIO4` | Digital data |

#### Soil moisture module with LM393

| Soil module pin | ESP32 pin | Function |
|---|---|---|
| `VCC` | `3.3V` | Power |
| `GND` | `GND` | Ground |
| `DO` | `GPIO25` | Digital threshold output |
| `AO` | `GPIO34` | Analog moisture output |

Current moisture calibration used by the dashboard:

- raw `3700` = `0/100` moisture
- raw `1800` = `100/100` moisture

#### Photoresistor

Current wiring assumption:

- one side to `3.3V`
- analog read point to `GPIO35`
- `10k ohm` resistor from `GPIO35` to `GND`

| Photoresistor connection | ESP32 pin | Function |
|---|---|---|
| Divider output | `GPIO35` | Analog light reading |
| Pull-down resistor | `GND` | Reference to ground |

Current light calibration used by the dashboard:

- raw `0` = `0/100`
- raw `3800` = `100/100`

## Sender telemetry format

The sender currently transmits compact JSON to keep LoRa packet size under control.

Example payload:

```json
{"n":"Sender","s":3244,"u":3253944,"ba":118,"bt":24.68,"bp":1030.95,"bl":7.87,"dok":true,"dt":24.10,"dh":48.00,"sa":4095,"sd":0,"lv":3082}
```

Field map:

| Key | Meaning |
|---|---|
| `n` | Node name |
| `s` | Sender packet sequence number |
| `u` | Uptime in milliseconds |
| `ba` | BMP280 I2C address |
| `bt` | BMP280 temperature in C |
| `bp` | Pressure in hPa |
| `bl` | Altitude in meters |
| `dok` | DHT11 reading valid flag |
| `dt` | DHT11 temperature in C |
| `dh` | DHT11 humidity percent |
| `sa` | Soil moisture analog raw reading |
| `sd` | Soil moisture digital state |
| `lv` | Light raw reading |

## Gateway dashboard behavior

The dashboard in `src/gateway.cpp` currently includes:

- live conditions cards
- compact packet feed
- temperature vs humidity graph
- soil moisture graph
- light level graph
- pressure vs altitude graph
- wildfire likelihood over time graph
- `Wildfire Conditions` color band:
  - `Blue`
  - `Green`
  - `Yellow`
  - `Orange`
  - `Red`

The gateway stores recent history in RAM only.

Current history size:

- `240` recent packets

This history is cleared on boot.

## Wildfire demo logic

The dashboard computes a demo wildfire likelihood score from recent samples.

It currently considers:

- prolonged low soil moisture
- prolonged low humidity
- elevated temperature
- strong daylight as a secondary supporting factor

Important:

- this is a demo heuristic, not a real wildfire prediction model
- it is useful for showing how environmental conditions can be combined into a warning indicator

## Calibration values currently in use

### Soil moisture

- dry threshold: `3700 -> 0`
- wet threshold: `1800 -> 100`

### Light level

- dark: `0 -> 0`
- bright: `3800 -> 100`

### Altitude

In `src/sender.cpp`, altitude is adjusted with:

- `ALTITUDE_CALIBRATION_OFFSET_M = 154.17f`

This was used to make the local displayed altitude approximately correct at the calibration point used during development.

## Current known-good operating state

- sender firmware builds and uploads on `COM7`
- gateway firmware builds and uploads on `COM9`
- sender transmits every `1 second`
- gateway receives sender packets and acknowledges them
- gateway dashboard serves over Wi-Fi
- packet history, graphs, and wildfire-condition logic update from live data

## Recommended next maintenance points

- if Wi-Fi credentials change, update `src/gateway.cpp`
- if the router gives a different IP, find the gateway again on the local network
- if the altitude drifts from the target demo value, adjust `ALTITUDE_CALIBRATION_OFFSET_M`
- if soil or light sensors are changed, recalibrate the scaling values in `src/gateway.cpp`

## Additional guide

For a step-by-step first-time installation and setup process, see:

- [`FIRST_TIME_SETUP.md`](FIRST_TIME_SETUP.md)
