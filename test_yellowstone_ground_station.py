import csv
import queue
import unittest
from dataclasses import asdict
from io import StringIO

from yellowstone_ground_station import (
    EXPORT_FIELDS,
    SERIAL_COMMAND_TERMINATOR,
    SerialReader,
    TelemetryPoint,
    table_sash_position,
)


class FakeSerial:
    def __init__(self):
        self.data = bytearray()
        self.flushed = False

    def write(self, data):
        self.data.extend(data)
        return len(data)

    def flush(self):
        self.flushed = True


class GroundStationTests(unittest.TestCase):
    def test_table_layout_keeps_a_visible_table_and_sidebar(self):
        self.assertEqual(table_sash_position(1280), 940)
        self.assertEqual(table_sash_position(980), 640)

    def test_export_fields_accept_complete_export_row(self):
        point = TelemetryPoint(
            46.0, -110.0, 1000, 3280.8, 1.0, 2.24, 0.5, 98.4, 90.0,
            90000, 900.0, 26.58, 1000, 3280.8, 20.0, 68.0, -80, 1,
            "2026-07-15", "19:35:16", 3, 6, 1, 1,
        )
        row = asdict(point)
        row.update({
            "corrected_pressure_alt_m": 1000.0,
            "corrected_pressure_alt_ft": 3280.8,
            "distance_km": 1.0,
            "distance_mi": 0.62,
            "distance_nm": 0.54,
            "bearing_deg_from_launch": 90.0,
            "altimeter_inhg": 29.92,
            "launch_name": "Test launch",
        })
        output = StringIO(newline="")
        writer = csv.DictWriter(output, fieldnames=EXPORT_FIELDS)
        writer.writeheader()
        writer.writerow(row)
        self.assertIn("Test launch", output.getvalue())

    def test_serial_command_uses_cross_platform_crlf_and_reports_bytes(self):
        commands = queue.Queue()
        output = queue.Queue()
        commands.put("CMD,CUTDOWN")
        reader = SerialReader("test", output, commands, None)
        fake = FakeSerial()

        reader.write_pending_commands(fake)

        expected = f"CMD,CUTDOWN{SERIAL_COMMAND_TERMINATOR}".encode("ascii")
        self.assertEqual(bytes(fake.data), expected)
        self.assertTrue(fake.flushed)
        self.assertEqual(output.get_nowait(), ("status", f"Sent command: CMD,CUTDOWN ({len(expected)} bytes)"))

    def test_ping_command_uses_same_serial_transport(self):
        commands = queue.Queue()
        output = queue.Queue()
        commands.put("CMD,PING")
        reader = SerialReader("test", output, commands, None)
        fake = FakeSerial()

        reader.write_pending_commands(fake)

        expected = f"CMD,PING{SERIAL_COMMAND_TERMINATOR}".encode("ascii")
        self.assertEqual(bytes(fake.data), expected)
        self.assertTrue(fake.flushed)


if __name__ == "__main__":
    unittest.main()
