# ASCEND Logger v11 bench diagnostic

This smaller SAMD21 logger has GPS, a barometer and SD, without a LoRa radio.
Optional humidity and PM sensors are not required by this diagnostic.

Connected board on 2026-10-26: `/dev/cu.usbmodem1201`, detected as
`AltitudeUnknown:samd:altitude_yellowstone_m0`. This package uses the
`feather_m0_express` variant. Schematic matches SDA PA22, SCL PA23,
SD CS D10/PA18, MOSI PB10, SCK PB11, MISO PA12.

```sh
arduino-cli compile --fqbn AltitudeUnknown:samd:altitude_yellowstone_m0 ASCEND/ASCEND_Logger_Diagnostic
arduino-cli upload -p /dev/cu.usbmodem1201 --fqbn AltitudeUnknown:samd:altitude_yellowstone_m0 ASCEND/ASCEND_Logger_Diagnostic
```

Open USB serial at 115200 to start. The diagnostic scans I2C, checks the
barometer PROM CRC and conversion data, then polls fresh GPS NAV-PVT messages
and barometer readings every two seconds. It does not change persistent GPS
configuration, transmit radio packets or write SD files.

## Initial results — October 26, 2026

- USB upload and flash verification passed for both sketches. MCU heartbeat
  continues during GPS/barometer testing.
- Standalone `Yellowstone_SD_Arduino_Test` passed: initialization, appending
  `testing 1, 2, 3.` to `test.txt`, closing, reopening and reading back.
  User confirmed an SD card is inserted. No formatting was performed.
- I2C SDA/SCL HIGH; only addresses 0x42 and 0x76 found.
- GPS 0x42: UBX communication and repeated fresh NAV-PVT messages passed.
  Initial capture: fix_type=0, fix_ok=0, satellites=0, date_valid=0,
  time_valid=0. Satellite reception/navigation fix not yet verified.
- Barometer 0x76: PROM `00BC BC29 C5A5 6E1A 6A4D 7C34 6653 48E9`;
  stored CRC=9, calculated CRC=9. Repeated nonzero ADC conversions passed.
- User identified the fitted part as MS5607, but this requires rechecking:
  D1=7999760, D2=8530462 gives approximately 1700.47 hPa / 32.20 C with
  MS5607 compensation. Pressure is implausible for ambient conditions.
  MS5611 compensation would give approximately 850.24 hPa. This is evidence
  of a possible model mismatch, not proof of component identity or accuracy.
  Do not pass absolute pressure measurement or silently change the model.
- No humidity/PM devices were detected or tested. No LoRa is fitted/tested.

The sketch is bench firmware, not a deployment-ready standalone logger.

## Component identification resolved

User corrected the fitted barometer to **MS5611** after reviewing the pressure
discrepancy. Diagnostic now uses MS5611 compensation. Schematic U1 value still
mentions MS5607; update that BOM/value in the CAD source to avoid future confusion.
User moved the powered board to a clear sky view for the reception test.

## Latest result after correction and outdoor test

- MS5611: steady pressure approximately 849.5–849.9 hPa, with die temperature
  falling from approximately 28 C indoors to 9.4 C outside. Basic measurement
  function passes; no calibrated reference comparison was performed.
- User confirmed W3011 antenna fitted and subsequently moved fully outdoors.
- GPS, GPS L1 C/A, Galileo and GLONASS configuration keys all read enabled (1).
  These were read only; no GNSS settings were written.
- Outdoor NAV-PVT still has no fix, zero satellites used and invalid UTC.
  NAV-SAT has no positive-C/N0 signals; occasional listed satellites do not
  establish reception. Latest captures list zero satellites.
- MON-RF block 0: noise=94, AGC=5616, jamming-state=0, indicator=25–30.
  State 0 means unknown/disabled, not proof of no interference. These values
  do not identify a particular fault.
- GPS digital communication passes; reception/navigation has NOT passed.
  Next inspect antenna keepout/placement, feed trace, C10, antenna/module
  solder joints and RF ground/layout. Need PCB layout or photos for review.
- Final installed sketch: ASCEND_Logger_Diagnostic, 54396 bytes compiled;
  upload and flash verification passed. Serial 115200. SD is deselected;
  standalone SD test already passed. This is not production logging firmware.

W3011 manufacturer layout reference:
https://productfinder.pulseeng.com/doc_type/WEB301/doc_num/W3011/doc_part/W3011.pdf
Ground clearance and application-dependent matching cannot be checked from
the schematic alone.

## Assembly photo and layout screenshot review

User supplied `PXL_20261026_201813004.MACRO_FOCUS.jpg` and a Fusion layout
screenshot. W3011 is fitted. Its manufacturer drawing states it is symmetrical:
either terminal can be feed/ground, so a 180-degree rotation is not itself an
assembly error. The layout screenshot shows a clearance region, but does not
provide dimensions or independently verify every copper layer. No solder
bridge/open can be conclusively diagnosed from the photo. RF_IN pad 11 and
adjacent grounds 10/12 need close physical inspection; verify continuity of
the RF feed and actual fitted C10 value (schematic 2.2 pF). Asked user to
confirm C10. Do not infer capacitor value from its unmarked appearance.

Repeat serial check after image submission still no fix or positive-C/N0
signals. GPS communication and barometer continue working (~850.2 hPa).

## GPS reception recovered — later October 26

User reported signal and requested another check. Read-only capture on
`/dev/cu.usbmodem1201` at 115200 now shows repeated changing latitude,
longitude and altitude outputs, with SIV 4–6 and altitude about 1490–1493 m.
Exact coordinates omitted from these notes. GPS satellite reception and
position output are now demonstrated. This different serial format indicates
the running firmware changed outside the assistant's last upload; do not
claim ASCEND_Logger_Diagnostic remains installed. This capture does not expose
fix_type/fix_ok/UTC validity, so those flags were not independently verified.
No firmware was flashed in this recheck. Hardware modifications (including
V_BCKP jumper) were not confirmed; do not attribute recovery to any correction.
Earlier no-reception observations are historical, not the current status.
