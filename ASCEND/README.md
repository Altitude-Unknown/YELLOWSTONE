# ASCEND UAS firmware — initial bench version

Branch: `ascend/uas-datalogger`, based on `agent/command-diagnostics`.
The original Yellowstone sketches are unchanged. This is a payload datalogger,
not a flight controller. Target: Yellowstone / Feather M0 SAMD21.

- `ASCEND_UAS`: samples GPS, MS5x pressure, and Adafruit PMSA003I at nominal 1 Hz;
  writes CSV to SD and broadcasts each sample over LoRa.
- `ASCEND_Ground`: receives the new ASCEND packets and saves the same CSV rows to
  its own SD card. It never transmits. USB output is available at 115200 baud.
- `libraries/ASCENDCore`: shared versioned packet, CSV, radio and file handling.

The PMSA003I uses I2C address 0x12. It reports atmospheric/environmental PM1.0,
PM2.5 and PM10 mass concentrations in µg/m³, not calculated AQI. Driver reads
validate the sensor checksum. GPS uses 0x42; MS5x probes 0x76 then 0x77. The
optional `readExtraSensor` hook must define its type and units before enabling
`EXTRA_VALID`. No sensor values are simulated.

Adafruit references: [sensor](https://www.adafruit.com/product/4632),
[wiring](https://learn.adafruit.com/pmsa003i/pinouts).
Check whether you have the Adafruit breakout or bare sensor before wiring power;
their supply arrangements differ. Power off before attaching sensors.

## Build and upload

Run from the Yellowstone repository root. Existing dependencies: Adafruit SAMD
core 1.7.10, RadioHead 1.143.1, MS5x 1.0.0, SparkFun u-blox GNSS 2.2.25,
SD-master, Adafruit BusIO 1.14.5. The PMSA003I driver is pinned to 1.1.1 to
match the installed BusIO; driver 2.0.0 requires newer BusIO headers.

```sh
arduino-cli lib install --no-deps 'Adafruit PM25 AQI Sensor@1.1.1'
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 --libraries ASCEND/libraries ASCEND/ASCEND_UAS
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 --libraries ASCEND/libraries ASCEND/ASCEND_Ground
arduino-cli board list
arduino-cli upload -p /dev/cu.usbmodem1201 --fqbn adafruit:samd:adafruit_feather_m0 ASCEND/ASCEND_UAS
```

Use the actual detected port. Upload the ground sketch to a second Yellowstone
when available. Both endpoints need the ASCEND firmware; existing Yellowstone
ground firmware and desktop packet decoders do not understand this protocol.

## Data and radio

Each boot creates a new `U0000.CSV` (UAS) or `G0000.CSV` (ground), advancing to
the next unused number through 9999. Rows are flushed after writing. Never
remove the card while powered. Logging errors disable SD writes and report to
USB; sampling/radio continue. Restart after fixing/inserting the card. No file
is intentionally overwritten; sudden power loss can still damage FAT files.

The CSV follows ASCEND field names for UTC, coordinates, MSL altitude, pressure,
and PM. Missing readings are empty; QC flags explain missing UTC, fix, pressure,
or PM. Barometer temperature has its own column because it is not a validated
ambient temperature measurement. UTC requires a fresh GPS solution with valid
date/time. MSL altitude uses `getAltitudeMSL`, not ellipsoid height. No AGL
altitude is inferred. Uptime and sequence remain available without GPS time.
The ASCEND importer needs UTC and currently limits uploads to 5,000 rows / 2 MiB;
long logs need splitting before import. Team/mission assignment occurs on import.

The 56-byte v1 packet is a packed little-endian SAMD21 struct with IEEE754
float32 fields, defined in `ASCENDCore.h`. Validity bits govern measurements;
zero-filled invalid wire fields must not be treated as observations. RadioHead
supplies packet CRC. Ground checks length, magic and version before logging.
Node ID defaults to 1; assign unique IDs for multiple UAS. Session ID is the UAS
file number, only unique within that card's history; without SD it is zero.

Radio: 915 MHz, SF7, bandwidth 125 kHz, coding rate 4/5, preamble 12, 13 dBm.
This faster preset allows 1 Hz transmissions; it differs from legacy Yellowstone
SF12 settings. It needs range testing. UAS sends asynchronously and bounds a
stuck TX at 8 seconds. A busy/failed radio can skip samples; there is no replay
queue. Sample sequences allow gaps to be identified. One-way reception is a
best-effort backup, not a guaranteed full copy. Ground stores received rows
without deduplication. No cutdown, commands, ACKs or SHERPA UART are implemented.

Missing startup GPS/pressure requires a restart after repair; missing PMSA003I
is retried every 10 seconds. Sensor read failures leave fields blank. The loop
does not require a USB host, but SD/I2C operations can delay nominal sampling.
Electrical bus lockups and long-duration endurance remain untested.

## Bench status — 2026-10-06

Connected board: `/dev/cu.usbmodem1201`.

- Diagnostic upload/flash verification and MCU heartbeat passed.
- I2C SDA and SCL high; GPS responded at 0x42.
- No pressure device responded despite confirmed populated sensor.
- RFM95 initialization and 915 MHz configuration passed; this does not prove RF range.
- Standalone SD test reported `initialization failed!` with a card confirmed present.
- PMSA003I initially disconnected; subsequent connected-sensor check passed
  (see below).
- No second board is available for end-to-end ground backup testing.
- Both final sketches compiled (UAS 80,696 bytes; ground 47,784 bytes).
- Final UAS firmware uploaded and flash verified; 25 captured samples continued
  at 1 Hz with valid GPS UTC, no position fix (zero satellites), and blank
  pressure/PM fields. See `bench-capture.txt`; check with
  `python3 ASCEND/check_capture.py ASCEND/bench-capture.txt`.
- Existing SD-master emits SPI divider/packed-member compiler warnings. Its
  `flush()` does not expose sync status, so write-error reporting cannot prove
  durable storage. SD success and card contents still need verification.

Next bench checks: reseat or try a known-good FAT32/MBR card without erasing the
current card; inspect SD power/contacts/CS D10. Check pressure sensor supply,
solder joints and SDA/SCL continuity. Test both radio endpoints and SD contents
before flight use.

### PMSA003I connected-sensor check

On 2026-10-06, the running UAS firmware produced 26 consecutive samples at 1 Hz
with checksum-validated PMSA003I readings: PM1.0 = 0, PM2.5 = 1–3, and
PM10 = 1–4 µg/m³. `NO_PM` cleared in every captured row. GPS supplied UTC and
reported 3–5 satellites, but no valid position fix. `NO_PRESSURE` persisted.
SD was not retested in this capture; its previous initialization failure remains
unresolved. This verifies sensor communication, not concentration calibration or
ground reception. Raw output: `bench-pmsa003i-capture.txt`.

```sh
python3 ASCEND/check_capture.py ASCEND/bench-pmsa003i-capture.txt
```

### SD format correction

On 2026-10-06, macOS successfully read the 125.7 GB card named `RBSTrain`.
It used exFAT on MBR, which the installed SD-master FAT driver does not support.
All 25 non-system-event files (47,360,082 bytes) were copied to
`../SD-Card-Backups/RBSTrain-2026-10-06` relative to the repository root; the
backup manifest records matching SHA-256 hashes. Live `.fseventsd` metadata was
excluded. With approval, the card was erased and formatted as `ASCEND`,
FAT32/MBR with 32 KiB clusters. Laptop write/read of `SDCHECK.TXT` and
`diskutil verifyVolume` passed. Board initialization and logging must still be
retested after reinserting the card and restarting Yellowstone.

### SD board verification after formatting

The follow-up board test confirmed SD logging: `U0000.CSV` contained 6,262 bytes
with the expected CSV header and real particulate readings through sequence 88.
Readback after firmware replacement verified that these rows were stored on the
card, not merely printed over USB. See `bench-sd-log-readback.txt`.

The legacy standalone SD test still reported initialization failure. A controlled
test using SdFat 2.2.2, CS D10, LoRa CS D8 high, LoRa held in reset on D9, and
400 kHz shared SPI passed initialization and write/read verification. Because
both driver and bus setup differ, this does not isolate the cause of the legacy
test failure. The existing ASCEND UAS firmware itself successfully wrote the CSV;
no production driver change was needed. UAS firmware was restored afterward.

`ASCEND_SD_Verify` preserves the successful diagnostic. It requires SdFat 2.2.2,
waits for USB at 115200, writes/overwrites only its diagnostic `CHECK.TXT`, and
reads the header/tail of `U0000.CSV`. Its saved output shows no GPS UTC/fix or
pressure in that run; PM values are valid. Ground reception remains untested.
