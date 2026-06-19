# YELLOWSTONE SAMD21 Bootloader

Custom UF2 bootloader for the YELLOWSTONE SAMD21G18A PCB.

Hardware assumptions:

- MCU: ATSAMD21G18A-AU
- Clock: external 32.768 kHz crystal
- USB: native USB D+ / D-
- Boot/status LED: Arduino D13 / `PIN_PA17`

## Files

- `bootloader-altitude_yellowstone_m0-v4.0.0.bin`
  - Use this for first-time bootloader flashing with Atmel-ICE/SWD.
- `update-bootloader-altitude_yellowstone_m0-v4.0.0.uf2`
  - Use this only after a UF2 bootloader is already installed.

## Expected UF2 Drive Name

- `YSTONEBOOT`

## Note

This bootloader currently uses internal lab USB VID/PID placeholders. Before
sharing broadly, assign official project USB IDs.
