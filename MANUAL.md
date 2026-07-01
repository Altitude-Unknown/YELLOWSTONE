# Yellowstone Telemetry System Manual

Living manual for the Yellowstone LoRa telemetry airborne unit, ground unit, and desktop GUI.

Last updated: 2026-06-30

## System Overview

The Yellowstone system has three main pieces:

- **Airborne firmware:** `Airborne_YELLOWSTONE_dev/Airborne_YELLOWSTONE_dev.ino`
- **Ground firmware:** `Ground_YELLOWSTONE_dev/Ground_YELLOWSTONE_dev.ino`
- **Desktop ground-station GUI:** `yellowstone_ground_station.py`

The airborne unit reads GNSS data, builds telemetry payloads, transmits them by LoRa, and optionally logs to SD. The ground unit receives LoRa telemetry, prints CSV rows over USB serial, and optionally logs to SD. The desktop GUI reads ground serial CSV rows, displays live telemetry, exports data, creates a live browser map, and can send a guarded cutdown command.

The cutdown command path is:

```text
Ground Station GUI -> Ground YELLOWSTONE -> LoRa -> Airborne YELLOWSTONE -> SHERPA UART -> ICARUS cutdown PCB
```

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
- Version: `3`
- GPS valid flag
- Latitude and longitude in degrees x `10^7`
- GPS altitude in meters MSL
- Ground speed in cm/s
- Heading in tenths of a degree
- Static pressure in pascals
- ISA pressure altitude in meters
- Pressure-altitude vertical speed in cm/s
- Pressure-sensor temperature in centi-degrees C
- UTC date/time fields
- GPS fix type
- Satellite count

The LoRa payload uses compact metric/SI values. Airborne and ground CSV logs
include both metric and US-standard conversions.

## Airborne Unit

### Airborne Responsibilities

- Initialize SD logging.
- Initialize I2C GNSS.
- Initialize the MS5x-compatible pressure sensor path and try I2C addresses
  `0x76` then `0x77`.
- Initialize RFM95 LoRa.
- Read GNSS PVT data.
- Transmit telemetry payload every `2000 ms`.
- Listen for ground command packets between telemetry transmissions.
- Forward accepted cutdown commands to SHERPA over the Feather M0 `Serial5`
  UART at `115200 baud`.
- Log airborne telemetry to `AIRLOG3.CSV` when SD is available.

### Airborne Startup Messages

Possible serial messages:

```text
Airborne SD logging ready
Airborne SD not found; logging disabled
GPS not found
MS5x pressure sensor ready at 0x76
MS5x pressure sensor ready at 0x77
MS5x pressure sensor not found at 0x76 or 0x77; pressure telemetry disabled
LoRa init failed
LoRa ready: long-range mode
SHERPA UART ready
Command forwarded to SHERPA: CUTDOWN seq=12
```

### Airborne to SHERPA UART

Connect Airborne YELLOWSTONE `Serial5` to the SHERPA UART connector. On the
Yellowstone Feather M0 target, this is the PB22/PB23 UART:

```text
PB22 / SAMD21 package pin 37 / Arduino D30 = Serial5 TX
PB23 / SAMD21 package pin 38 / Arduino D31 = Serial5 RX
```

Do not use the Feather M0 core's default `Serial1` for this connector;
`Serial1` is PA10/PA11 and does not reach the Yellowstone SHERPA UART pins.

```text
Airborne TX -> SHERPA RX-YELLOWSTONE
Airborne RX -> SHERPA TX-YELLOWSTONE
Airborne GND -> SHERPA GND
```

When a valid ground command is received, Airborne writes one ASCII line:

```text
SHERPA,CUTDOWN,<sequence>
```

Repeated LoRa command packets with the same sequence number are ignored after
the first forward, so SHERPA should receive a single UART cutdown line per GUI
command.

When SHERPA reports that ICARUS acknowledged a cutdown sequence, Airborne
queues a compact LoRa acknowledgement and repeats it for several seconds so
Ground can receive it after finishing its cutdown transmit burst.

## SHERPA Cutdown Bridge

Firmware:

```text
SHERPA_Cutdown_Bridge/SHERPA_Cutdown_Bridge.ino
```

Target:

```text
esp32:esp32:esp32c3:CDCOnBoot=cdc
```

SHERPA listens to Airborne YELLOWSTONE over UART and forwards accepted cutdown
commands to ICARUS over ESP-NOW on channel `1`.

USB debug messages include:

```text
SHERPA,BOOT
SHERPA,YELLOWSTONE_UART_READY
SHERPA,READY,MAC,80:F1:B2:F0:1B:3C
SHERPA,UART_RX,SHERPA,CUTDOWN,12347
SHERPA,CUTDOWN_FORWARDED,12347,ok_delta,5,fail_delta,0
SHERPA,ICARUS_ACK,seq,12347,accepted,1
SHERPA,HEARTBEAT,ms,12996,last_sequence,12347,espnow_ok,5,espnow_fail,0,last_ack_sequence,12347
```

### Flashing SHERPA

Compile:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc "SHERPA_Cutdown_Bridge"
```

Upload:

```bash
arduino-cli upload -p /dev/cu.usbmodem1201 --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc "SHERPA_Cutdown_Bridge"
```

Port names vary by computer and OS.

## ICARUS Cutdown Receiver

Firmware:

```text
ICARUS_Cutdown_Receiver/ICARUS_Cutdown_Receiver.ino
```

Target:

```text
esp32:esp32:esp32c6:CDCOnBoot=cdc
```

ICARUS listens for SHERPA ESP-NOW packets on channel `1`. When it receives a
valid cutdown packet with a new sequence number, it drives:

```text
GPIO10 -> main cutdown MOSFET
GPIO11 -> backup cutdown MOSFET
```

Both MOSFET outputs are held high for `8000 ms`, then forced low. Duplicate
packets with the same sequence number are counted but ignored, so SHERPA's
repeated ESP-NOW sends do not restart the burn timer.

USB debug messages include:

```text
ICARUS,BOOT
ICARUS,READY,MAC,...
ICARUS,CUTDOWN_RX,seq,12347,from,80:f1:b2:f0:1b:3c
ICARUS,ACK_SENT,seq,12347,accepted,1
ICARUS,CUTDOWN_ACTIVE,seq,12347,duration_ms,8000
ICARUS,CUTDOWN_COMPLETE,seq,12347
ICARUS,HEARTBEAT,ms,12000,active,0,last_sequence,12347,rx,5,accepted,1,duplicates,4,rejected,0
```

### ICARUS Acknowledgement Path

After accepting a new cutdown sequence, ICARUS sends an ESP-NOW acknowledgement
back toward SHERPA. The full return path is:

```text
ICARUS -> ESP-NOW broadcast ack -> SHERPA -> UART -> Airborne
Airborne -> repeated LoRa ack -> Ground -> USB STATUS line -> GUI
```

Ground prints:

```text
STATUS,ICARUS_ACK,<sequence>,accepted,<count>,rssi,<rssi>
```

The GUI displays this in the Cutdown panel as the confirmation that ICARUS
received and accepted the cutdown command.

### Flashing ICARUS

Compile:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc "ICARUS_Cutdown_Receiver"
```

Upload:

```bash
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc "ICARUS_Cutdown_Receiver"
```

### Airborne SD Log

File:

```text
AIRLOG3.CSV
```

Columns:

```text
lat,lon,gps_alt_m,gps_alt_ft,ground_speed_mps,ground_speed_mph,vertical_speed_mps,vertical_speed_fpm,heading_deg,pressure_pa,pressure_hpa,pressure_inhg,pressure_alt_m,pressure_alt_ft,pressure_temp_c,pressure_temp_f,packet,date_utc,time_utc,fix_type,sats,gps_valid,pressure_valid
```

### Flashing Airborne Firmware

Compile:

```bash
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 "YELLOWSTONE Project/Airborne_YELLOWSTONE_dev"
```

Upload:

```bash
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn adafruit:samd:adafruit_feather_m0 "YELLOWSTONE Project/Airborne_YELLOWSTONE_dev"
```

## Ground Unit

### Ground Responsibilities

- Initialize SD logging.
- Initialize RFM95 LoRa.
- Receive binary telemetry payloads.
- Reject payloads with bad size, magic, or version.
- Print valid telemetry as CSV over USB serial.
- Log valid telemetry to `GNDLOG3.CSV` when SD is available.
- Accept `CMD,CUTDOWN` from the desktop GUI over USB serial.
- Transmit a short LoRa command packet to the airborne unit.

### Ground Startup Messages

Possible serial messages:

```text
Ground SD logging ready
Ground SD not found; logging disabled
LoRa init failed
Set freq failed
Ground station ready
LoRa ready: long-range mode
STATUS,CUTDOWN_SENT,12
```

### Ground Serial CSV Format

The desktop GUI expects:

```text
lat,lon,gps_alt_m,gps_alt_ft,ground_speed_mps,ground_speed_mph,vertical_speed_mps,vertical_speed_fpm,heading_deg,pressure_pa,pressure_hpa,pressure_inhg,pressure_alt_m,pressure_alt_ft,pressure_temp_c,pressure_temp_f,rssi,packet,date_utc,time_utc,fix_type,sats,gps_valid,pressure_valid
```

Rows with `gps_valid` set to `0` are shown in the table but are not added to the live map trail.

The current desktop GUI understands the version-3 CSV schema.

### Ground Serial Commands

The GUI sends cutdown requests to the Ground YELLOWSTONE as:

```text
CMD,CUTDOWN
```

The ground board responds with:

```text
STATUS,CUTDOWN_SENT,<sequence>
```

The ground board repeats the LoRa command packet three times for better receive
odds, using the same sequence number each time.

### Pressure Telemetry Verification

Pressure telemetry was verified end to end on `2026-06-24`:

- The integrated airborne firmware reported live pressure, pressure altitude,
  and pressure validity on the USB debug stream.
- The ground firmware received those same values over LoRa and printed them in
  the version-3 CSV stream.
- During this bench test, pressure data was valid even when GPS had no lock,
  so `pressure_valid` can be `1` while `gps_valid` is `0`.

### 2026-06-25 Bench Verification

During follow-up bench work on `2026-06-25`:

- The airborne board transmitted live telemetry with pressure, pressure
  altitude, pressure temperature, packet count, and vertical speed.
- The ground board received those packets and printed valid version-3 CSV rows
  with incrementing packet numbers.
- The desktop GUI successfully connected to the ground board after all other
  serial monitors were closed.
- An immediate GUI disconnect after pressing `Connect` was traced to serial-port
  contention, not a telemetry-format problem.

During this bench test the airborne GPS still had no indoor lock, so the system
was working even though GPS fields remained zero.

### Ground SD Log

File:

```text
GNDLOG3.CSV
```

The file uses the same CSV fields as the serial output.

### Flashing Ground Firmware

Compile:

```bash
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 "YELLOWSTONE Project/Ground_YELLOWSTONE_dev"
```

Upload:

```bash
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn adafruit:samd:adafruit_feather_m0 "YELLOWSTONE Project/Ground_YELLOWSTONE_dev"
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
- Sends a confirmed cutdown command to the ground board.
- Writes live map data to `yellowstone_live_site`.
- Opens a browser Leaflet/OpenStreetMap live map.
- Exports CSV.
- Exports KML.
- Can publish the live site folder to Netlify using Netlify CLI.

### Running From Source

Install dependencies:

```bash
cd "YELLOWSTONE Project"
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

Important:

- If the GUI connects and immediately disconnects, first make sure no Arduino
  serial monitor or other terminal session still has the ground-board port open.
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
