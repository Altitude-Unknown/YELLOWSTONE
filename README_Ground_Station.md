# Yellowstone Ground Station

Cross-platform Python/Tkinter GUI for the Yellowstone ground unit.

For the full Yellowstone airborne, ground, and GUI operating manual, see [MANUAL.md](MANUAL.md).

## Features

- Reads Yellowstone CSV telemetry from the ground board serial port.
- Shows a spreadsheet-style telemetry table.
- Shows live telemetry summary values in metric and imperial units.
- Shows GPS altitude, pressure altitude, corrected pressure altitude, pressure,
  temperature, and vertical speed.
- Computes payload range and bearing from a user-entered launch location.
- Sends a confirmed cutdown command through Ground YELLOWSTONE to Airborne
  YELLOWSTONE, which forwards it to SHERPA over UART for ICARUS.
- Generates a live Leaflet/OpenStreetMap page in `yellowstone_live_site`.
- Can publish the map folder to Netlify using Netlify CLI settings.

## Run From Source

Install Python 3 and then:

```bash
cd "YELLOWSTONE Project"
python3 -m pip install -r requirements.txt
python3 yellowstone_ground_station.py
```

On Windows, use `python` instead of `python3` if needed.

## Serial Data Format

The GUI expects the ground Yellowstone board to output:

```text
lat,lon,gps_alt_m,gps_alt_ft,ground_speed_mps,ground_speed_mph,vertical_speed_mps,vertical_speed_fpm,heading_deg,pressure_pa,pressure_hpa,pressure_inhg,pressure_alt_m,pressure_alt_ft,pressure_temp_c,pressure_temp_f,rssi,packet,date_utc,time_utc,fix_type,sats,gps_valid,pressure_valid
```

Rows with `gps_valid` equal to `0` are shown in the table but are not added to the map trail.

Pressure telemetry may still be valid when `gps_valid` is `0`.

## Cutdown Command

When connected to the ground Yellowstone serial port, the GUI enables **Send
Cutdown**. Pressing it asks for confirmation, then sends:

```text
CMD,CUTDOWN
```

The ground board transmits a LoRa command to Airborne YELLOWSTONE. Airborne
forwards accepted commands to SHERPA over the Yellowstone PB22/PB23 `Serial5`
UART as:

```text
SHERPA,CUTDOWN,<sequence>
```

When ICARUS accepts the command, the acknowledgement returns through SHERPA,
Airborne, and Ground. The ground board prints:

```text
STATUS,ICARUS_ACK,<sequence>,accepted,<count>,rssi,<rssi>
```

The GUI shows that acknowledgement in the Cutdown panel.

## Netlify Publishing

The app writes a static site to:

```text
yellowstone_live_site
```

For Netlify publishing from the GUI, install Node.js so `npx` is available. Then enter:

- Netlify Site ID
- Netlify Auth Token
- Publish interval

The GUI runs:

```bash
npx netlify-cli deploy --prod --dir yellowstone_live_site --site SITE_ID --auth TOKEN
```

Users can change the Site ID and Auth Token to publish to their own Netlify site.
