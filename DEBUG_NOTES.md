# Yellowstone Debug Notes

## Current Status

These notes capture where we left off before pausing lab work.

### Board Under Test

Problem Yellowstone ground PCB.

Known-good comparison:

- A known-good Yellowstone ground PCB receives telemetry from the airborne unit normally.
- The problem PCB programs over USB and runs sketches.

### Fixed / Improved

- I2C bus was fixed earlier by removing/reworking the backwards accel/gyro issue.
- SD card path became intermittent, then passed the minimal Arduino SD read/write test on `CS 10`.

Successful SD output:

```text
Initializing SD card...initialization done.
Writing to test.txt...done.
test.txt:
testing 1, 2, 3.
```

### Remaining Issue

LoRa receive still does not work on the problem ground PCB.

Verbose RX diagnostic result:

```text
heartbeat valid=0 invalid=0 recvFail=0
```

Interpretation:

- MCU is alive.
- Sketch is running.
- RFM95 initializes enough for RadioHead setup to continue.
- No packets are seen at all: `rf95.available()` never becomes true.
- No valid packets, invalid packets, or receive failures are reported.

Hardware work already performed:

- RFM95 pins resoldered.
- RFM95-to-Feather M0 pins rang out successfully.
- PCB washed/dried several times.
- Microscope inspection found no obvious broken traces or solder bridges.

## Next Diagnostic To Run

A new diagnostic sketch has been added:

```text
Yellowstone_LoRa_Polling_Diagnostic/
```

Purpose:

- Ignore the normal `rf95.available()` / DIO0 interrupt path.
- Poll raw RFM95 IRQ flags over SPI.
- Check whether the radio ever sets `RxDone`.

Expected interpretation:

- If raw polling shows `RxDone` or valid packets, the RFM95 is receiving RF and the issue is likely DIO0/IRQ mapping/path to Feather `D3`.
- If raw polling never shows `RxDone`, the problem is more likely RF path, antenna/connector, module health, frequency/config mismatch, or front-end damage.

When back in the lab:

1. Connect the problem Yellowstone PCB.
2. Compile/upload `Yellowstone_LoRa_Polling_Diagnostic`.
3. Monitor at `115200`.
4. Compare output against known-good ground PCB if useful.

Important: the upload was intentionally interrupted before this diagnostic was flashed, because the PCB was no longer available.
