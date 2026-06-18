#!/usr/bin/env python3
import csv
import html
import json
import os
import queue
import subprocess
import sys
import threading
import time
import webbrowser
from dataclasses import asdict, dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    import serial.tools.list_ports as list_ports
except Exception as exc:
    raise SystemExit("This app requires pyserial. Install it with: pip install pyserial\n" + str(exc))


APP_TITLE = "Yellowstone Ground Station"
BAUD = 115200
SETTINGS_FILE = "yellowstone_ground_station_settings.json"
PUBLIC_DIR = "yellowstone_live_site"
DATA_FILE = "data.json"
INDEX_FILE = "index.html"
MAX_TABLE_ROWS = 2000
MAX_TRAIL_POINTS = 2000
MAP_SERVER_PORT = 8765

CSV_FIELDS = [
    "lat",
    "lon",
    "alt_m",
    "speed_mps",
    "speed_mph",
    "heading_deg",
    "rssi",
    "packet",
    "date_utc",
    "time_utc",
    "fix_type",
    "sats",
    "gps_valid",
]


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


def make_map_handler(public_dir: Path):
    class MapRequestHandler(BaseHTTPRequestHandler):
        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path in ("", "/", f"/{INDEX_FILE}"):
                self.send_file(public_dir / INDEX_FILE, "text/html; charset=utf-8")
            elif parsed.path == f"/{DATA_FILE}":
                self.send_file(public_dir / DATA_FILE, "application/json; charset=utf-8")
            else:
                self.send_error(404, "Map file not found")

        def send_file(self, path: Path, content_type: str):
            try:
                body = path.read_bytes()
            except FileNotFoundError:
                self.send_error(404, f"{path.name} not found")
                return
            except Exception as exc:
                self.send_error(500, f"Could not read {path.name}: {exc}")
                return

            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *args):
            return

    return MapRequestHandler


@dataclass
class TelemetryPoint:
    lat: float
    lon: float
    alt_m: int
    speed_mps: float
    speed_mph: float
    heading_deg: float
    rssi: int
    packet: int
    date_utc: str
    time_utc: str
    fix_type: int
    sats: int
    gps_valid: int


def app_dir() -> Path:
    if getattr(sys, "frozen", False):
        if sys.platform == "darwin":
            return Path.home() / "Library" / "Application Support" / APP_TITLE
        if sys.platform == "win32":
            base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA")
            if base:
                return Path(base) / APP_TITLE
            return Path.home() / "AppData" / "Local" / APP_TITLE
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def parse_telemetry_line(line: str) -> TelemetryPoint:
    row = next(csv.reader([line.strip()]))
    if len(row) != len(CSV_FIELDS):
        raise ValueError(f"Expected {len(CSV_FIELDS)} fields, got {len(row)}")

    return TelemetryPoint(
        lat=float(row[0]),
        lon=float(row[1]),
        alt_m=int(float(row[2])),
        speed_mps=float(row[3]),
        speed_mph=float(row[4]),
        heading_deg=float(row[5]),
        rssi=int(float(row[6])),
        packet=int(float(row[7])),
        date_utc=row[8],
        time_utc=row[9],
        fix_type=int(float(row[10])),
        sats=int(float(row[11])),
        gps_valid=int(float(row[12])),
    )


class SerialReader(threading.Thread):
    def __init__(self, port: str, out_queue: queue.Queue, stop_event: threading.Event):
        super().__init__(daemon=True)
        self.port = port
        self.out_queue = out_queue
        self.stop_event = stop_event

    def run(self):
        try:
            with serial.Serial(self.port, BAUD, timeout=0.5) as ser:
                time.sleep(0.25)
                ser.reset_input_buffer()
                self.out_queue.put(("status", f"Connected to {self.port}"))
                while not self.stop_event.is_set():
                    raw = ser.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    try:
                        point = parse_telemetry_line(line)
                    except Exception:
                        self.out_queue.put(("raw", line))
                    else:
                        self.out_queue.put(("telemetry", point))
        except Exception as exc:
            self.out_queue.put(("error", f"Serial error: {exc}"))


class GroundStationApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1280x760")
        self.minsize(980, 620)

        self.base_dir = app_dir()
        self.public_dir = self.base_dir / PUBLIC_DIR
        self.settings_path = self.base_dir / SETTINGS_FILE
        self.settings = self.load_settings()

        self.serial_queue = queue.Queue()
        self.stop_event = threading.Event()
        self.reader = None
        self.points = []
        self.all_points = []
        self.latest_point = None
        self.last_publish = 0.0
        self.auto_publish_job = None
        self.map_server = None
        self.map_server_thread = None
        self.map_server_port = MAP_SERVER_PORT

        self.port_var = tk.StringVar()
        self.status_var = tk.StringVar(value="Not connected")
        self.map_status_var = tk.StringVar(value="No valid GPS point yet")
        self.netlify_site_var = tk.StringVar(value=self.settings.get("netlify_site_id", ""))
        self.netlify_token_var = tk.StringVar(value=self.settings.get("netlify_auth_token", ""))
        self.publish_interval_var = tk.IntVar(value=int(self.settings.get("publish_interval_sec", 60)))
        self.auto_publish_var = tk.BooleanVar(value=bool(self.settings.get("auto_publish", False)))

        self.build_ui()
        self.refresh_ports()
        self.ensure_public_site()
        self.after(100, self.process_serial_queue)

    def load_settings(self):
        try:
            return json.loads(self.settings_path.read_text(encoding="utf-8"))
        except Exception:
            return {}

    def save_settings(self):
        self.settings.update(
            {
                "netlify_site_id": self.netlify_site_var.get().strip(),
                "netlify_auth_token": self.netlify_token_var.get().strip(),
                "publish_interval_sec": int(self.publish_interval_var.get()),
                "auto_publish": bool(self.auto_publish_var.get()),
            }
        )
        self.settings_path.write_text(json.dumps(self.settings, indent=2), encoding="utf-8")
        self.status_var.set("Settings saved")

    def build_ui(self):
        root = ttk.Frame(self, padding=10)
        root.pack(fill="both", expand=True)

        top = ttk.Frame(root)
        top.pack(fill="x")

        ttk.Label(top, text="Serial Port").pack(side="left")
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=36, state="readonly")
        self.port_box.pack(side="left", padx=6)
        ttk.Button(top, text="Refresh", command=self.refresh_ports).pack(side="left")
        self.connect_btn = ttk.Button(top, text="Connect", command=self.toggle_connection)
        self.connect_btn.pack(side="left", padx=6)
        ttk.Button(top, text="Open Map", command=self.open_map).pack(side="left", padx=(18, 6))
        ttk.Button(top, text="Publish Now", command=self.publish_now).pack(side="left")
        ttk.Button(top, text="Export CSV", command=self.export_csv).pack(side="left", padx=(18, 6))
        ttk.Button(top, text="Export KML", command=self.export_kml).pack(side="left")
        ttk.Label(top, textvariable=self.status_var).pack(side="left", padx=18)

        panes = ttk.Panedwindow(root, orient="horizontal")
        panes.pack(fill="both", expand=True, pady=(10, 0))

        left = ttk.Frame(panes)
        right = ttk.Frame(panes)
        panes.add(left, weight=3)
        panes.add(right, weight=1)

        columns = CSV_FIELDS
        self.table = ttk.Treeview(left, columns=columns, show="headings", height=24)
        for col in columns:
            self.table.heading(col, text=col)
            width = 92
            if col in ("date_utc", "time_utc"):
                width = 104
            elif col in ("lat", "lon"):
                width = 112
            elif col == "heading_deg":
                width = 108
            self.table.column(col, width=width, anchor="center", stretch=False)

        yscroll = ttk.Scrollbar(left, orient="vertical", command=self.table.yview)
        xscroll = ttk.Scrollbar(left, orient="horizontal", command=self.table.xview)
        self.table.configure(yscrollcommand=yscroll.set, xscrollcommand=xscroll.set)
        self.table.grid(row=0, column=0, sticky="nsew")
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")
        left.rowconfigure(0, weight=1)
        left.columnconfigure(0, weight=1)

        telemetry = ttk.LabelFrame(right, text="Live Telemetry", padding=10)
        telemetry.pack(fill="x")
        self.live_labels = {}
        for label in ("Lat", "Lon", "Altitude m", "Speed mph", "Heading", "RSSI", "Packet", "UTC", "Fix", "Sats"):
            row = ttk.Frame(telemetry)
            row.pack(fill="x", pady=2)
            ttk.Label(row, text=label, width=12).pack(side="left")
            var = tk.StringVar(value="-")
            ttk.Label(row, textvariable=var).pack(side="left")
            self.live_labels[label] = var

        map_box = ttk.LabelFrame(right, text="Map", padding=10)
        map_box.pack(fill="x", pady=(10, 0))
        ttk.Label(map_box, textvariable=self.map_status_var, wraplength=300).pack(fill="x")
        ttk.Button(map_box, text="Open Live Map In Browser", command=self.open_map).pack(fill="x", pady=(8, 0))
        ttk.Button(map_box, text="Choose Public Folder", command=self.choose_public_dir).pack(fill="x", pady=(6, 0))

        netlify = ttk.LabelFrame(right, text="Netlify", padding=10)
        netlify.pack(fill="x", pady=(10, 0))
        ttk.Label(netlify, text="Site ID").pack(anchor="w")
        ttk.Entry(netlify, textvariable=self.netlify_site_var).pack(fill="x", pady=(0, 6))
        ttk.Label(netlify, text="Auth Token").pack(anchor="w")
        ttk.Entry(netlify, textvariable=self.netlify_token_var, show="*").pack(fill="x", pady=(0, 6))
        interval_row = ttk.Frame(netlify)
        interval_row.pack(fill="x", pady=(2, 6))
        ttk.Label(interval_row, text="Publish every").pack(side="left")
        ttk.Spinbox(interval_row, from_=15, to=600, increment=15, width=7, textvariable=self.publish_interval_var).pack(side="left", padx=6)
        ttk.Label(interval_row, text="sec").pack(side="left")
        ttk.Checkbutton(netlify, text="Auto publish", variable=self.auto_publish_var, command=self.on_auto_publish_changed).pack(anchor="w")
        ttk.Button(netlify, text="Save Settings", command=self.save_settings).pack(fill="x", pady=(6, 0))
        ttk.Button(netlify, text="Publish Now", command=self.publish_now).pack(fill="x", pady=(6, 0))

        help_box = ttk.LabelFrame(right, text="Files", padding=10)
        help_box.pack(fill="both", expand=True, pady=(10, 0))
        self.public_dir_var = tk.StringVar(value=str(self.public_dir))
        ttk.Label(help_box, textvariable=self.public_dir_var, wraplength=300).pack(fill="x")

    def refresh_ports(self):
        ports = [p.device for p in list_ports.comports()]
        self.port_box["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(ports[0])
        self.status_var.set(f"Found {len(ports)} serial port(s)")

    def toggle_connection(self):
        if self.reader:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_var.get().strip()
        if not port:
            messagebox.showwarning(APP_TITLE, "Choose a serial port first.")
            return
        self.stop_event.clear()
        self.reader = SerialReader(port, self.serial_queue, self.stop_event)
        self.reader.start()
        self.connect_btn.configure(text="Disconnect")
        self.status_var.set(f"Opening {port}")

    def disconnect(self):
        self.stop_event.set()
        self.reader = None
        self.connect_btn.configure(text="Connect")
        self.status_var.set("Disconnected")

    def process_serial_queue(self):
        while True:
            try:
                kind, value = self.serial_queue.get_nowait()
            except queue.Empty:
                break

            if kind == "telemetry":
                self.add_telemetry(value)
            elif kind == "status":
                self.status_var.set(value)
            elif kind == "raw":
                self.status_var.set(f"Ignored serial text: {value[:80]}")
            elif kind == "error":
                self.status_var.set(value)
                self.disconnect()

        self.after(100, self.process_serial_queue)

    def add_telemetry(self, point: TelemetryPoint):
        self.latest_point = point
        self.all_points.append(point)
        if point.gps_valid:
            self.points.append(point)
            if len(self.points) > MAX_TRAIL_POINTS:
                self.points = self.points[-MAX_TRAIL_POINTS:]

        values = [getattr(point, field) for field in CSV_FIELDS]
        self.table.insert("", "end", values=values)
        children = self.table.get_children()
        if len(children) > MAX_TABLE_ROWS:
            self.table.delete(children[0])
        self.table.yview_moveto(1.0)
        self.update_live_labels(point)
        self.write_live_data()

        if self.auto_publish_var.get():
            now = time.time()
            interval = max(15, int(self.publish_interval_var.get()))
            if now - self.last_publish >= interval:
                self.last_publish = now
                self.publish_now()

    def update_live_labels(self, point: TelemetryPoint):
        self.live_labels["Lat"].set(f"{point.lat:.7f}")
        self.live_labels["Lon"].set(f"{point.lon:.7f}")
        self.live_labels["Altitude m"].set(str(point.alt_m))
        self.live_labels["Speed mph"].set(f"{point.speed_mph:.2f}")
        self.live_labels["Heading"].set(f"{point.heading_deg:.1f}")
        self.live_labels["RSSI"].set(str(point.rssi))
        self.live_labels["Packet"].set(str(point.packet))
        self.live_labels["UTC"].set(f"{point.date_utc} {point.time_utc}")
        self.live_labels["Fix"].set(str(point.fix_type))
        self.live_labels["Sats"].set(str(point.sats))
        if point.gps_valid:
            self.map_status_var.set(f"Map tracking packet {point.packet} at {point.date_utc} {point.time_utc} UTC")
        else:
            self.map_status_var.set("Waiting for valid 3D GPS fix")

    def ensure_public_site(self):
        self.public_dir.mkdir(parents=True, exist_ok=True)
        index_path = self.public_dir / INDEX_FILE
        index_path.write_text(map_html(), encoding="utf-8")
        self.write_live_data()
        self.public_dir_var.set(str(self.public_dir))

    def choose_public_dir(self):
        chosen = filedialog.askdirectory(initialdir=str(self.public_dir.parent))
        if not chosen:
            return
        self.public_dir = Path(chosen)
        self.ensure_public_site()

    def write_live_data(self):
        self.public_dir.mkdir(parents=True, exist_ok=True)
        latest = asdict(self.latest_point) if self.latest_point else None
        trail = [asdict(point) for point in self.points[-MAX_TRAIL_POINTS:]]
        payload = {
            "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "latest": latest,
            "trail": trail,
        }
        (self.public_dir / DATA_FILE).write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")
        index_path = self.public_dir / INDEX_FILE
        if not index_path.exists():
            index_path.write_text(map_html(), encoding="utf-8")

    def start_map_server(self):
        if self.map_server:
            return

        handler = make_map_handler(self.public_dir)
        try:
            self.map_server = ReusableThreadingHTTPServer(("127.0.0.1", MAP_SERVER_PORT), handler)
        except OSError as exc:
            try:
                self.map_server = ReusableThreadingHTTPServer(("127.0.0.1", 0), handler)
            except OSError as fallback_exc:
                messagebox.showerror(
                    APP_TITLE,
                    f"Could not start local map server on port {MAP_SERVER_PORT}: {exc}\n\nFallback also failed: {fallback_exc}",
                )
                return

        self.map_server_port = self.map_server.server_address[1]
        self.map_server_thread = threading.Thread(target=self.map_server.serve_forever, daemon=True)
        self.map_server_thread.start()
        self.status_var.set(f"Map server running at http://127.0.0.1:{self.map_server_port}/{INDEX_FILE}")

    def open_map(self):
        self.ensure_public_site()
        self.start_map_server()
        if self.map_server:
            url = f"http://127.0.0.1:{self.map_server_port}/{INDEX_FILE}"
            try:
                if sys.platform == "win32":
                    os.startfile(url)
                elif not webbrowser.open(url):
                    raise RuntimeError("No browser accepted the map URL.")
            except Exception as exc:
                self.clipboard_clear()
                self.clipboard_append(url)
                messagebox.showerror(APP_TITLE, f"Could not open the map browser window: {exc}\n\nMap URL copied to clipboard:\n{url}")

    def on_auto_publish_changed(self):
        self.save_settings()
        if self.auto_publish_var.get():
            self.status_var.set("Auto publish enabled")
        else:
            self.status_var.set("Auto publish disabled")

    def publish_now(self):
        self.save_settings()
        self.ensure_public_site()
        site_id = self.netlify_site_var.get().strip()
        token = self.netlify_token_var.get().strip()
        if not site_id or not token:
            self.status_var.set("Netlify site ID and token are required to publish")
            return

        public_dir = str(self.public_dir)
        self.status_var.set("Publishing to Netlify...")
        threading.Thread(target=self.run_netlify_publish, args=(public_dir, site_id, token), daemon=True).start()

    def run_netlify_publish(self, public_dir: str, site_id: str, token: str):
        cmd = [
            "npx",
            "netlify-cli",
            "deploy",
            "--prod",
            "--dir",
            public_dir,
            "--site",
            site_id,
            "--auth",
            token,
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
            if result.returncode != 0:
                detail = (result.stderr or result.stdout or "Netlify publish failed").strip()
                self.serial_queue.put(("status", f"Netlify publish failed: {detail[:160]}"))
            else:
                self.serial_queue.put(("status", "Published to Netlify"))
        except FileNotFoundError:
            self.serial_queue.put(("status", "Netlify publishing needs Node.js/npx installed"))
        except Exception as exc:
            self.serial_queue.put(("status", f"Netlify publish failed: {exc}"))

    def export_csv(self):
        if not self.all_points:
            messagebox.showwarning(APP_TITLE, "No telemetry data to export yet.")
            return

        default_name = self.default_export_name("csv")
        path = filedialog.asksaveasfilename(
            title="Save Flight CSV",
            defaultextension=".csv",
            initialfile=default_name,
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
        )
        if not path:
            return

        try:
            with open(path, "w", newline="", encoding="utf-8") as fh:
                writer = csv.DictWriter(fh, fieldnames=CSV_FIELDS)
                writer.writeheader()
                for point in self.all_points:
                    writer.writerow(asdict(point))
        except Exception as exc:
            messagebox.showerror(APP_TITLE, f"Could not save CSV: {exc}")
            return

        self.status_var.set(f"Saved CSV: {path}")

    def export_kml(self):
        valid_points = [point for point in self.all_points if point.gps_valid]
        if not valid_points:
            messagebox.showwarning(APP_TITLE, "No valid GPS points to export yet.")
            return

        default_name = self.default_export_name("kml")
        path = filedialog.asksaveasfilename(
            title="Save Flight KML",
            defaultextension=".kml",
            initialfile=default_name,
            filetypes=[("KML files", "*.kml"), ("All files", "*.*")],
        )
        if not path:
            return

        try:
            Path(path).write_text(build_kml(valid_points), encoding="utf-8")
        except Exception as exc:
            messagebox.showerror(APP_TITLE, f"Could not save KML: {exc}")
            return

        self.status_var.set(f"Saved KML: {path}")

    def default_export_name(self, extension: str) -> str:
        if self.latest_point:
            stamp = f"{self.latest_point.date_utc}_{self.latest_point.time_utc}".replace(":", "")
        else:
            stamp = time.strftime("%Y-%m-%d_%H%M%S", time.gmtime())
        return f"yellowstone_flight_{stamp}.{extension}"


def map_html() -> str:
    return """<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Yellowstone Live Map</title>
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <style>
    html, body, #map { height: 100%; margin: 0; }
    .hud {
      position: absolute;
      top: 12px;
      right: 12px;
      z-index: 1000;
      background: rgba(255, 255, 255, 0.92);
      border: 1px solid #d5d9df;
      border-radius: 6px;
      padding: 10px 12px;
      min-width: 240px;
      font: 14px/1.35 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      box-shadow: 0 6px 18px rgba(0,0,0,0.12);
    }
    .hud strong { display: block; margin-bottom: 4px; }
  </style>
</head>
<body>
  <div id="map"></div>
  <div class="hud" id="hud">
    <strong>Yellowstone</strong>
    Waiting for telemetry
  </div>
  <script>
    const map = L.map('map').setView([0, 0], 2);
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxZoom: 19,
      attribution: '&copy; OpenStreetMap contributors'
    }).addTo(map);

    let marker = null;
    let trail = L.polyline([], {color: '#0b63ce', weight: 3}).addTo(map);
    let firstFix = true;

    async function refresh() {
      try {
        const response = await fetch('data.json?cache=' + Date.now());
        const data = await response.json();
        const latest = data.latest;
        const points = (data.trail || []).filter(p => Number(p.gps_valid) === 1);
        const latlngs = points.map(p => [Number(p.lat), Number(p.lon)]);
        trail.setLatLngs(latlngs);

        if (latest && Number(latest.gps_valid) === 1) {
          const pos = [Number(latest.lat), Number(latest.lon)];
          if (!marker) marker = L.marker(pos).addTo(map);
          marker.setLatLng(pos);
          marker.bindPopup(
            `Lat: ${latest.lat}<br>Lon: ${latest.lon}<br>` +
            `Alt: ${latest.alt_m} m<br>Speed: ${latest.speed_mph} mph<br>` +
            `UTC: ${latest.date_utc} ${latest.time_utc}`
          );
          if (firstFix) {
            map.setView(pos, 14);
            firstFix = false;
          } else {
            map.panTo(pos, {animate: true});
          }
          document.getElementById('hud').innerHTML =
            `<strong>Yellowstone</strong>` +
            `Packet: ${latest.packet}<br>` +
            `UTC: ${latest.date_utc} ${latest.time_utc}<br>` +
            `Alt: ${latest.alt_m} m<br>` +
            `Speed: ${latest.speed_mph} mph<br>` +
            `Heading: ${latest.heading_deg}&deg;<br>` +
            `RSSI: ${latest.rssi} dBm<br>` +
            `Sats: ${latest.sats}`;
        } else {
          document.getElementById('hud').innerHTML =
            `<strong>Yellowstone</strong>Waiting for valid GPS fix`;
        }
      } catch (err) {
        document.getElementById('hud').innerHTML =
          `<strong>Yellowstone</strong>Waiting for data`;
      }
    }

    refresh();
    setInterval(refresh, 3000);
  </script>
</body>
</html>
"""


def build_kml(points):
    first = points[0]
    last = points[-1]
    name = f"Yellowstone Flight {first.date_utc} {first.time_utc} UTC"
    coordinates = "\n".join(f"{p.lon:.7f},{p.lat:.7f},{p.alt_m}" for p in points)

    placemarks = []
    for index, point in enumerate(points):
        if index not in (0, len(points) - 1) and index % 25 != 0:
            continue
        title = "Start" if index == 0 else "End" if index == len(points) - 1 else f"Point {point.packet}"
        description = (
            f"UTC: {point.date_utc} {point.time_utc}<br>"
            f"Packet: {point.packet}<br>"
            f"Altitude: {point.alt_m} m<br>"
            f"Speed: {point.speed_mph:.2f} mph<br>"
            f"Heading: {point.heading_deg:.1f} deg<br>"
            f"RSSI: {point.rssi} dBm<br>"
            f"Satellites: {point.sats}"
        )
        placemarks.append(
            f"""
    <Placemark>
      <name>{html.escape(title)}</name>
      <description><![CDATA[{description}]]></description>
      <Point>
        <coordinates>{point.lon:.7f},{point.lat:.7f},{point.alt_m}</coordinates>
      </Point>
    </Placemark>"""
        )

    return f"""<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2">
  <Document>
    <name>{html.escape(name)}</name>
    <Style id="flightPath">
      <LineStyle>
        <color>ffce630b</color>
        <width>4</width>
      </LineStyle>
    </Style>
    <Placemark>
      <name>Flight Path</name>
      <description><![CDATA[
        Start: {html.escape(first.date_utc)} {html.escape(first.time_utc)} UTC<br>
        End: {html.escape(last.date_utc)} {html.escape(last.time_utc)} UTC<br>
        Points: {len(points)}
      ]]></description>
      <styleUrl>#flightPath</styleUrl>
      <LineString>
        <tessellate>1</tessellate>
        <altitudeMode>absolute</altitudeMode>
        <coordinates>
{coordinates}
        </coordinates>
      </LineString>
    </Placemark>
    {''.join(placemarks)}
  </Document>
</kml>
"""


if __name__ == "__main__":
    GroundStationApp().mainloop()
