# Yellowstone Hardware Notes

Working target board: Adafruit Feather M0 / SAMD21, `adafruit:samd:adafruit_feather_m0`.

## Shared Pinout

| Function | Feather Pin | Notes |
| --- | --- | --- |
| LoRa CS | D8 | RFM95 chip select |
| LoRa DIO0 / IRQ | D3 | RadioHead interrupt pin; required for receive |
| LoRa RST | D9 | RFM95 reset |
| SD CS | D10 | Yellowstone PCB SD card chip select |
| SPI MOSI | Feather MOSI | Shared by LoRa and SD |
| SPI MISO | Feather MISO | Shared by LoRa and SD |
| SPI SCK | Feather SCK | Shared by LoRa and SD |
| I2C SDA | Feather SDA | Shared by GPS / sensors |
| I2C SCL | Feather SCL | Shared by GPS / sensors |
| SHERPA UART TX | PB22 / SAMD21 package pin 37 / Arduino D30 | `Serial5` TX to SHERPA RX-YELLOWSTONE |
| SHERPA UART RX | PB23 / SAMD21 package pin 38 / Arduino D31 | `Serial5` RX from SHERPA TX-YELLOWSTONE |

## SHERPA UART Note

Yellowstone's SHERPA connector uses the Feather M0 core's `Serial5`, not
`Serial1`. The Adafruit Feather M0 variant maps:

```text
Serial5 TX = PB22 / SAMD21 package pin 37 / Arduino D30
Serial5 RX = PB23 / SAMD21 package pin 38 / Arduino D31
```

The default `Serial1` on this core is PA10/PA11 and will not drive the
Yellowstone SHERPA UART connector.

## Expected I2C Devices

Known-good or repaired boards have shown:

| Address | Likely Device | Notes |
| --- | --- | --- |
| `0x42` | u-blox GNSS | Used by airborne firmware |
| `0x76` | Pressure sensor | Present on boards with sensor populated |

If the I2C scanner reports every address from `0x01` through `0x7E`, that is not a real device list. It usually means the bus is electrically broken. In our tests, that matched:

```text
SDA: LOW
SCL: HIGH
```

The first board with this fault was fixed by removing an accel/gyro IC that appeared to be installed backwards and was pulling SDA low.

## LoRa Configuration

Both airborne and ground firmware use:

```text
Frequency: 915.0 MHz
Modem config: RH_RF95::Bw125Cr48Sf4096
Preamble length: 12
Airborne TX power: 23 dBm
```

LoRa can pass `rf95.init()` even if receive still fails. If the receiver initializes but sees no packets while a known-good ground board receives normally, check:

- DIO0 / IRQ continuity to D3
- RFM95 module edge solder joints
- RFM95 power and ground
- antenna / RF connector path
- CS, RST, MISO, MOSI, and SCK solder joints

## SD Notes

Yellowstone SD chip select is D10.

If LoRa initializes but SD fails on D10, the Feather SPI peripheral is not completely dead. Focus on the SD-card-specific path:

- SD socket VCC to 3.3V
- SD socket GND to ground
- SD socket CS to D10
- SD socket MISO / MOSI / SCK continuity
- card seating and socket contacts
- card format: FAT32 / MBR preferred

## Diagnostic Sketches

Temporary board test sketches live in:

- `Yellowstone_Board_Diagnostic`
- `Yellowstone_Ground_Rx_Diagnostic`
- `Yellowstone_UART_Ping_Talker`
- `Yellowstone_UART_Ping_Responder`

The board diagnostic checks:

- USB / MCU heartbeat
- I2C line levels and address scan
- SD init on D10
- LoRa init and frequency set

The ground RX diagnostic disables SD and prints a heartbeat plus LoRa packet counters. It is useful for separating "firmware not running" from "LoRa initialized but no packets are arriving."
