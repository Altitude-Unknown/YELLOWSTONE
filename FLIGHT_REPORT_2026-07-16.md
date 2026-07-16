# HAB Flight Report — 2026-07-16

## Flight Summary

- Launch site: Livingston airport, Montana
- Launch time: approximately 07:00 local time
- Maximum reported altitude: approximately 85,000 ft
- Flight event: balloon burst at maximum altitude
- Ground command unit: separate, known-working Ground YELLOWSTONE PCB
- Cutdown attempts: approximately 20 commands transmitted
- Outcome: cutdown did not fire

## Evidence Available

The flight firmware did not preserve command events separately at each hop, so
the failure could not initially be localized from flight data alone. During
post-flight bench testing, SHERPA reported a received command sequence, 15
successful ESP-NOW transmissions, zero local send failures, and no ICARUS
acknowledgement.

Physical inspection found the ICARUS PCB unplugged. After reconnecting ICARUS,
an end-to-end cutdown test using the unchanged flight firmware succeeded.

## Confirmed Root Cause

The ICARUS PCB was unplugged and therefore could not receive the command or
energize the cutdown outputs. The existing Ground, Airborne, SHERPA, and ICARUS
software path was verified operational after the connection was restored.

## Corrective Firmware Work

The command protocol now uses a common sequence ID and returns acknowledgements
for Airborne, SHERPA, and ICARUS reception. Ground and Airborne write command
events to `GNDEVT.CSV` and `AIREVT.CSV`. A non-firing end-to-end `PING` command
exercises Ground YELLOWSTONE → Airborne YELLOWSTONE → SHERPA → ICARUS and the
complete return path before a cutdown is attempted.

An ICARUS software acknowledgement does not prove cutter current. A future
hardware revision or test should add output-voltage/current feedback if that
electrical distinction is required in flight.

## Follow-Up Status

- New Ground and Airborne firmware compiled successfully.
- New SHERPA and ICARUS firmware images were built successfully.
- The updated macOS Ground Station application built and passed code-signature
  verification; its Python test suite passed.
- Firmware flashing was deferred. ICARUS did not reliably enumerate over USB,
  and an upload attempt failed before erasing or writing flash because the
  ESP32-C6 bootloader returned no serial data.
- Troubleshoot the ICARUS USB data/boot connection before the next flash.
- Add an ICARUS power/connector check and successful end-to-end ping to the
  flight preflight checklist.
