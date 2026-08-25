#!/usr/bin/env python3
"""
Split the combined payload dataset into one CSV per sensor type.

No timestamp column. Files are ROW-ALIGNED: row N of every file is the
same 1 Hz sample. They must be kept in step - sorting or filtering one
file without the others silently breaks the correspondence.
"""
import csv

SRC = "payload_data_2h_1hz.csv"

GROUPS = {
    "esp32_housekeeping.csv": ["die_temp_C", "sleep_pct", "load1", "load2"],
    "tmp117.csv":             ["tmp117_0_C", "tmp117_1_C",
                               "tmp117_2_C", "tmp117_3_C"],
    "bpx65.csv":              ["bpx65_a_V", "bpx65_b_V"],
    "bpw34.csv":              ["bpw34_a_V", "bpw34_b_V"],
    "n3163.csv":              ["n3163_a", "n3163_b"],
    "adr4525.csv":            ["adr4525_V"],
}

rows = list(csv.DictReader(open(SRC)))

# Guard against a typo in GROUPS silently dropping a column.
src_cols = set(rows[0].keys()) - {"uptime_s"}
mapped   = {c for cols in GROUPS.values() for c in cols}
missing  = src_cols - mapped
extra    = mapped - src_cols
if missing:
    raise SystemExit(f"columns not assigned to any file: {sorted(missing)}")
if extra:
    raise SystemExit(f"columns named but not in source: {sorted(extra)}")

for fname, cols in GROUPS.items():
    with open(fname, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for r in rows:
            w.writerow([r[c] for c in cols])
    print(f"{fname:<26} {len(cols)} cols  {len(rows):,} rows")

print(f"\nall {len(src_cols)} data columns accounted for, none duplicated")
