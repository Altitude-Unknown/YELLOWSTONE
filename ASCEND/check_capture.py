"""Validate real UAS USB CSV output: python3 ASCEND/check_capture.py capture.txt."""
import csv
from pathlib import Path
import sys
import re

lines = Path(sys.argv[1]).read_text().splitlines()
header_line = next((line for line in lines if line.startswith("project,")), None)
if header_line is None:
    # USB may attach after setup prints the header. Use the declared v1 schema.
    source = (Path(__file__).parent / "libraries/ASCENDCore/src/ASCENDCore.h").read_text()
    header_line = re.search(r'const char HEADER\[\]="([^"]+)";', source).group(1)
    print("Startup header not captured; validating against the declared v1 schema")
header = next(csv.reader([header_line]))
rows = [next(csv.reader([line])) for line in lines if line.startswith("ASCEND,UAS,")]
assert len(rows) >= 10, "Need at least ten real samples"
assert len(set(header)) == len(header), "Duplicate headers"
previous = None
for fields in rows:
    assert len(fields) == len(header), (len(fields), len(header))
    row = dict(zip(header, fields))
    sequence, uptime = int(row["sequence"]), int(row["uptime_ms"])
    if previous:
        assert sequence == previous[0] + 1, "USB sample sequence gap"
        assert 1000 <= uptime - previous[1] < 2000, "Unexpected sample interval"
    previous = sequence, uptime
    for flag, columns in {
        "NO_UTC": ["timestamp_utc"],
        "NO_FIX": ["latitude", "longitude", "altitude_msl_m"],
        "NO_PRESSURE": ["pressure_hpa", "barometer_temperature_c"],
        "NO_PM": ["pm1_ugm3", "pm25_ugm3", "pm10_ugm3"],
    }.items():
        if flag in row["qc_flag"].split(";"):
            assert all(row[column] == "" for column in columns), flag
print(f"PASS: {len(rows)} real samples, {len(header)} columns, consecutive sequences, nominal 1 Hz, missing values blank")
