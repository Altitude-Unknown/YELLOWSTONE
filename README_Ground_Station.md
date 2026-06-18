# Yellowstone Ground Station

Cross-platform Python/Tkinter GUI for the Yellowstone ground unit.

For the full Yellowstone airborne, ground, and GUI operating manual, see [MANUAL.md](MANUAL.md).

## Features

- Reads Yellowstone CSV telemetry from the ground board serial port.
- Shows a spreadsheet-style telemetry table.
- Shows live telemetry summary values.
- Generates a live Leaflet/OpenStreetMap page in `yellowstone_live_site`.
- Can publish the map folder to Netlify using Netlify CLI settings.

## Run From Source

Install Python 3 and then:

```bash
cd "YELLOWSTONE"
python3 -m pip install -r requirements.txt
python3 yellowstone_ground_station.py
```

On Windows, use `python` instead of `python3` if needed.

## Serial Data Format

The GUI expects the ground Yellowstone board to output:

```text
lat,lon,alt_m,speed_mps,speed_mph,heading_deg,rssi,packet,date_utc,time_utc,fix_type,sats,gps_valid
```

Rows with `gps_valid` equal to `0` are shown in the table but are not added to the map trail.

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
