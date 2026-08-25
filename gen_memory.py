#!/usr/bin/env python3
"""
Memory bit-flip detection counts.

Row-aligned with the sensor CSVs: row N is the same 1 Hz scan as row N
of tmp117.csv, bpx65.csv and the rest. No timestamp column.

One column per memory device, holding the number of bit flips found in
that device on that scan. Values 1 to 5.
"""
import csv
import random

ROWS    = 7200
MIN_VAL = 1
MAX_VAL = 5

DEVICES = ["psram_flips", "fram_flips", "eeprom_flips"]

random.seed(20260102)

rows = [[random.randint(MIN_VAL, MAX_VAL) for _ in DEVICES] for _ in range(ROWS)]

with open("memory_flipbits.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(DEVICES)
    w.writerows(rows)

# ------------------------------------------------------------------ checks
print(f"rows        : {len(rows):,}")
print(f"columns     : {len(DEVICES)}  {DEVICES}")
print()
for i, d in enumerate(DEVICES):
    col = [r[i] for r in rows]
    dist = "  ".join(f"{k}:{col.count(k)}" for k in range(MIN_VAL, MAX_VAL + 1))
    print(f"{d:<14} min {min(col)}  max {max(col)}  mean {sum(col)/len(col):.2f}   {dist}")

flat = [v for r in rows for v in r]
print()
print(f"all values in {MIN_VAL}-{MAX_VAL}: "
      f"{'PASS' if min(flat) >= MIN_VAL and max(flat) <= MAX_VAL else 'FAIL'}")
print(f"total cells : {len(flat):,}")
