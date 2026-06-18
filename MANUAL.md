# Yellowstone Telemetry System Manual

Living manual for the Yellowstone LoRa telemetry airborne unit, ground unit, and desktop GUI.

Last updated: 2026-06-18

## System Overview

The Yellowstone system has three main pieces:

- **Airborne firmware:** `Airborne_YELLOWSTONE_dev/Airborne_YELLOWSTONE_dev.ino`
- **Ground firmware:** `Ground_YELLOWSTONE_dev/Ground_YELLOWSTONE_dev.ino`
- **Desktop ground-station GUI:** `yellowstone_ground_station.py`

The airborne unit reads GNSS data, builds telemetry payloads, transmits them by LoRa, and optionally logs to SD. The ground unit receives LoRa telemetry, prints CSV rows over USB serial, and optionally logs to SD. The desktop GUI reads ground serial CSV rows, displays live telemetry, exports data, and creates a live browser map.

## Hardware Target

Current firmware builds for:

```text
adafruit:samd:adafruit_feather_m0
```

Common upload command:

```bash
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn adafruit:samd:adafruit_feather_m0 "path/to/sketch"
```

Port names vary by computer and OS.

## Shared Yellowstone Pinout

| Function | Feather Pin | Notes |
| --- | --- | --- |
| LoRa CS | D8 | RFM95 chip select |
| LoRa DIO0 / IRQ | D3 | RadioHead interrupt pin |
| LoRa RST | D9 | RFM95 reset |
| SD CS | D10 | Yellowstone SD card chip select |
| SPI MOSI | Feather MOSI | Shared by LoRa and SD |
| SPI MISO | Feather MISO | Shared by LoRa and SD |
| SPI SCK | Feather SCK | Shared by LoRa and SD |
| I2C SDA | Feather SDA | GNSS / sensors |
| I2C SCL | Feather SCL | GNSS / sensors |

Additional hardware detail is kept in:

```text
HARDWARE.md
```

## LoRa Settings

Airborne and ground firmware both use:

```text
Frequency: 915.0 MHz
Modem config: RH_RF95::Bw125Cr48Sf4096
Preamble length: 12
```

The airborne transmitter uses:

```text
Tx power: 23 dBm
```

## Telemetry Payload

The binary LoRa payload includes:

- Magic: `0x5953` (`YS`)
- Version: `2`
- GPS valid flag
- Latitude and longitude in degrees x `10^7`
- Altitude in meters
- Ground speed in cm/s
- Heading in tenths of a degree
- UTC date/time fields
- GPS fix type
- Satellite count

Ground firmware converts valid payloads to CSV.

## Airborne Unit

### Airborne Responsibilities

- Initialize SD logging.
- Initialize I2C GNSS.
- Initialize RFM95 LoRa.
- Read GNSS PVT data.
- Transmit telemetry payload every `2000 ms`.
- Log airborne telemetry to `AIRLOG.CSV` when SD is available.

### Airborne Startup Messages

Possible serial messages:

```text
Airborne SD logging ready
Airborne SD not found; logging disabled
GPS not found
LoRa init failed
LoRa ready: long-range mode
```

### Airborne SD Log

File:

```text
AIRLOG.CSV
```

Columns:

```text
lat,lon,alt_m,speed_mps,speed_mph,heading_deg,packet,date_utc,time_utc,fix_type,sats,gps_valid
```

### Flashing Airborne Firmware

Compile:

```bash
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 "Airborne_YELLOWSTONE_dev"
```

Upload:

```bash
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn adafruit:samd:adafruit_feather_m0 "Airborne_YELLOWSTONE_dev"
```

## Ground Unit

### Ground Responsibilities

- Initialize SD logging.
- Initialize RFM95 LoRa.
- Receive binary telemetry payloads.
- Reject payloads with bad size, magic, or version.
- Print valid telemetry as CSV over USB serial.
- Log valid telemetry to `GNDLOG.CSV` when SD is available.

### Ground Startup Messages

Possible serial messages:

```text
Ground SD logging ready
Ground SD not found; logging disabled
LoRa init failed
Set freq failed
Ground station ready
LoRa ready: long-range mode
```

### Ground Serial CSV Format

The desktop GUI expects:

```text
lat,lon,alt_m,speed_mps,speed_mph,heading_deg,rssi,packet,date_utc,time_utc,fix_type,sats,gps_valid
```

Rows with `gps_valid` set to `0` are shown in the table but are not added to the live map trail.

### Ground SD Log

File:

```text
GNDLOG.CSV
```

The file uses the same CSV fields as the serial output.

### Flashing Ground Firmware

Compile:

```bash
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 "Ground_YELLOWSTONE_dev"
```

Upload:

```bash
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn adafruit:samd:adafruit_feather_m0 "Ground_YELLOWSTONE_dev"
```

## Desktop Ground Station GUI

Main file:

```text
yellowstone_ground_station.py
```

### GUI Features

- Reads Yellowstone CSV telemetry from the ground board serial port.
- Shows a telemetry table.
- Shows live telemetry summary values.
- Writes live map data to `yellowstone_live_site`.
- Opens a browser Leaflet/OpenStreetMap live map.
- Exports CSV.
- Exports KML.
- Can publish the live site folder to Netlify using Netlify CLI.

### Running From Source

Install dependencies:

```bash
cd "YELLOWSTONE"
python3 -m pip install -r requirements.txt
```

Run:

```bash
python3 yellowstone_ground_station.py
```

### GUI Connection Flow

1. Flash ground firmware to the ground PCB.
2. Plug ground PCB into USB.
3. Launch GUI.
4. Refresh serial ports.
5. Select `/dev/cu.usbmodem...` or the matching serial device.
6. Click Connect.
7. Confirm table rows appear when airborne unit is transmitting.

### Live Map

The GUI writes live site files to:

```text
yellowstone_live_site
```

The browser map uses Leaflet and OpenStreetMap tiles, so internet access is needed for map tiles.

### Netlify Publishing

For Netlify publishing, install Node.js so `npx` is available. Then enter:

- Netlify Site ID
- Netlify Auth Token
- Publish interval

The GUI runs:

```bash
npx netlify-cli deploy --prod --dir yellowstone_live_site --site SITE_ID --auth TOKEN
```

## Diagnostic Sketches

These are bench-test sketches, not production firmware.

| Sketch | Purpose |
| --- | --- |
| `Yellowstone_Board_Diagnostic` | USB heartbeat, I2C line levels/scan, SD init, LoRa init |
| `Yellowstone_Ground_Rx_Diagnostic` | Ground LoRa receive heartbeat and packet counters, SD disabled |
| `Yellowstone_LoRa_Polling_Diagnostic` | Poll raw RFM95 IRQ flags over SPI to bypass DIO0/RadioHead available path |
| `Yellowstone_SD_Arduino_Test` | Minimal Arduino SD read/write test using `SD.begin(10)` |

Current lab debug history is in:

```text
DEBUG_NOTES.md
```

## Known Good I2C Results

Expected I2C devices observed on repaired/working boards:

| Address | Likely Device |
| --- | --- |
| `0x42` | u-blox GNSS |
| `0x76` | Pressure sensor |

If the I2C scanner reports every address from `0x01` through `0x7E`, the bus is electrically broken. In our tests this matched:

```text
SDA: LOW
SCL: HIGH
```

One board with this symptom was fixed by removing an incorrectly installed accel/gyro IC that was pulling SDA low.

## SD Troubleshooting

Yellowstone SD chip select is D10.

Minimal SD test:

```text
Yellowstone_SD_Arduino_Test/Yellowstone_SD_Arduino_Test.ino
```

Successful output:

```text
Initializing SD card...initialization done.
Writing to test.txt...done.
test.txt:
testing 1, 2, 3.
```

If SD is intermittent:

- Reseat card.
- Try known-good card.
- Inspect/reflow SD socket pins.
- Check SD socket VCC to 3.3V.
- Check SD socket GND to ground.
- Check SD CS to D10.
- Check MISO/MOSI/SCK continuity.

## LoRa Troubleshooting

If `rf95.init()` passes but no packets are received:

- Confirm known-good ground board receives from the same airborne unit.
- Run `Yellowstone_Ground_Rx_Diagnostic`.
- If counters stay at zero:

```text
heartbeat valid=0 invalid=0 recvFail=0
```

Then the firmware is alive but the RFM95 is not reporting received packets.

Primary suspects:

- RFM95 DIO0 / IRQ to Feather D3
- antenna / RF connector path
- RFM95 power and ground
- RFM95 edge solder joints
- damaged RFM95 module

If continuity and solder inspection look good, run:

```text
Yellowstone_LoRa_Polling_Diagnostic
```

Interpretation:

- Raw `RxDone` appears: RF is arriving; DIO0/interrupt path is suspect.
- Raw `RxDone` never appears: RF path, antenna, module health, or radio configuration is suspect.

## Maintenance Notes

Update this manual whenever:

- Pin mappings change.
- Payload format changes.
- Serial CSV format changes.
- GUI controls or export behavior changes.
- Netlify/live-map flow changes.
- A new diagnostic sketch is added.
- A board issue is fixed and becomes useful troubleshooting knowledge.
