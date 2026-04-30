# Document with model maps. Each map is in a table, with first row being columns.

1. ## LORA wiring map table:

LoRa Pin,ESP32 Pin (Freenove Label),Function
3V3,3.3V,Power (Do NOT use 5V!)
GND,GND,Ground
MISO,GPIO 19,SPI Master In / Slave Out
MOSI,GPIO 23,SPI Master Out / Slave In
SCK,GPIO 18,SPI Clock
NSS,GPIO 5,Chip Select (SS)
RST,GPIO 14,Reset
DIO0,GPIO 2,Interrupt

