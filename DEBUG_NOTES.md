# Yellowstone Debug Notes

## 2026-06-25 Lab + Release Notes

These notes capture the GitHub/release fixes and the final bench state that
worked at the end of today's session.

### GitHub / Release Workflow Fixes

- The Yellowstone GUI release workflow now runs from the dedicated Yellowstone
  repo instead of the wrong repo.
- The current known-good public GUI release is:

```text
yellowstone-v0.1.5
```

- The dedicated repo release page is:

```text
https://github.com/Altitude-Unknown/YELLOWSTONE/releases/latest
```

- Older mistaken/test GUI releases were cleaned up so the public release list
  is no longer cluttered with failed intermediate tags.

### What Broke macOS Signing / Notarization

The main signing problem was not the Python app itself. The blockers were Apple
certificate / trust-chain setup issues plus one GitHub secret mismatch.

What had to be fixed:

- Apple Developer agreement was accepted in the Apple account.
- A valid `Developer ID Application` certificate with private key was imported
  locally and exported as a `.p12`.
- The Apple `Developer ID` intermediate certificate was installed so the local
  identity became trusted and showed up as a valid code-signing identity.
- The base64 GitHub secret had to be regenerated from the current working `.p12`
  after the certificate chain was corrected.
- The `.p12` password secret had to match the actual export password.

Important GitHub Actions secrets used by the Yellowstone release workflow:

- `MACOS_CERTIFICATE_BASE64`
- `MACOS_CERTIFICATE_PASSWORD`
- `MACOS_KEYCHAIN_PASSWORD`
- Apple notarization credentials already configured for the workflow

Important lesson:

- If `security find-identity -v -p codesigning` shows `0 valid identities found`
  locally, the GitHub macOS signer setup is not really correct yet, even if a
  certificate appears in Keychain Access.

### Known-Good Bench State At End Of Session

Ground and airborne firmware were both reflashed with the current Yellowstone
firmware that matches the newest GUI release.

Observed working USB ports during the final test:

- Airborne: `/dev/cu.usbmodem1101`
- Ground: `/dev/cu.usbmodem1201`

Confirmed behavior:

- The airborne board transmitted live LoRa packets.
- Pressure, pressure altitude, pressure temperature, packet count, and vertical
  speed were present in the airborne serial debug output.
- The ground board received live packets and printed valid version-3 CSV rows.
- The desktop GUI connected successfully after all serial monitors were closed.
- The GUI disconnect-on-connect behavior was caused by the serial port still
  being open in a monitor session.

Important bench-test interpretation:

- LoRa link was working end to end.
- Pressure telemetry path was working end to end.
- GPS did not have lock during the indoor bench test, so `gps_valid=0`,
  `fix_type=0`, `sats=0`, and lat/lon stayed zero. This is expected indoors and
  does not mean the LoRa link or pressure telemetry is broken.

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
