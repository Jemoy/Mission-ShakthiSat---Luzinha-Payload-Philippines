#!/usr/bin/env python3
"""
Convert the sensor CSVs into PROGMEM tables and generate seven dummy
sensor drivers.

Each driver mirrors the shape of a real one:

    void   xxx_begin();
    bool   xxx_read(XxxData &out);

so replacing a dummy with a real I2C/SPI driver is a single-file edit.

Time is not owned by the drivers. A shared DummySource holds the current
sample index; the application advances it once per sample. That matches
reality (a real driver does not know what time it is) and keeps all seven
in step without them knowing about each other.

Outputs
    src/drivers/*.h, *.cpp        driver sources
    src/drivers/data/*.h          PROGMEM tables
    driver_truth.csv              the exact integers baked into flash
"""
import csv
import os
import textwrap

SRC = "."
OUT = "CspLite/src/drivers"
DATA = f"{OUT}/data"
ADS_FSR_V = 4.096
DIE_OFFSET = 40

os.makedirs(DATA, exist_ok=True)


def load(name):
    with open(os.path.join(SRC, name)) as f:
        return list(csv.DictReader(f))


hk   = load("esp32_housekeeping.csv")
tmp  = load("tmp117.csv")
bpx  = load("bpx65.csv")
bpw  = load("bpw34.csv")
n31  = load("n3163.csv")
adr  = load("adr4525.csv")
mem  = load("memory_flipbits.csv")

N = len(hk)
assert all(len(x) == N for x in (tmp, bpx, bpw, n31, adr, mem)), "row count mismatch"


def volts_to_code(v):
    """ADS1115 raw code from volts at the configured full-scale range."""
    c = round(float(v) * 32768.0 / ADS_FSR_V)
    return max(-32768, min(32767, int(c)))


def degc_to_code(c):
    """TMP117 raw code: LSB = 1/128 degC, signed 16-bit."""
    r = round(float(c) * 128.0)
    return max(-32768, min(32767, int(r)))


# ------------------------------------------------------------------ encode
hk_rows = []
for r in hk:
    die   = int(r["die_temp_C"]) + DIE_OFFSET
    sleep = int(r["sleep_pct"])
    loads = (int(r["load1"]) & 1) | ((int(r["load2"]) & 1) << 1)
    hk_rows.append((max(0, min(255, die)), max(0, min(127, sleep)), loads))

TMP_CHANNELS = 2      # only two TMP117 devices are fitted
tmp_rows = [[degc_to_code(r[f"tmp117_{k}_C"]) for k in range(TMP_CHANNELS)] for r in tmp]
bpx_rows = [[volts_to_code(r["bpx65_a_V"]), volts_to_code(r["bpx65_b_V"])] for r in bpx]
bpw_rows = [[volts_to_code(r["bpw34_a_V"]), volts_to_code(r["bpw34_b_V"])] for r in bpw]
adr_rows = [volts_to_code(r["adr4525_V"]) for r in adr]
mem_rows = [[int(r["psram_flips"]), int(r["fram_flips"]), int(r["eeprom_flips"])]
            for r in mem]

# 3N163 is a sparse event line: 480 ones out of 14,400 values. Storing the
# indices of the ones is 7x smaller than a byte per sample, and matches how
# a real event-counting driver would work.
n3163_a_events = [i for i, r in enumerate(n31) if int(r["n3163_a"])]
n3163_b_events = [i for i, r in enumerate(n31) if int(r["n3163_b"])]


# ------------------------------------------------------------------ emit
def emit_table(path, guard, decls):
    with open(path, "w") as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <Arduino.h>\n\n")
        for d in decls:
            f.write(d)
            f.write("\n")
        f.write(f"#endif /* {guard} */\n")


def fmt_rows(values, per_line, formatter):
    out, line = [], []
    for i, v in enumerate(values):
        line.append(formatter(v))
        if len(line) == per_line:
            out.append("  " + ", ".join(line) + ",")
            line = []
    if line:
        out.append("  " + ", ".join(line) + ",")
    return "\n".join(out)


emit_table(f"{DATA}/Esp32HkData_table.h", "ESP32HK_TABLE_H", [
    f"#define ESP32HK_SAMPLES {N}\n",
    "/* die_temp_code, sleep_pct, load_bits(b0=load1, b1=load2) */\n"
    "static const uint8_t ESP32HK_TABLE[] PROGMEM = {\n"
    + fmt_rows([v for row in hk_rows for v in row], 15, lambda v: f"{v:3d}")
    + "\n};\n",
])

emit_table(f"{DATA}/Tmp117Data_table.h", "TMP117_TABLE_H", [
    f"#define TMP117_SAMPLES {N}\n",
    "/* 2 channels, signed 16-bit, LSB = 1/128 degC */\n"
    "static const int16_t TMP117_TABLE[] PROGMEM = {\n"
    + fmt_rows([v for row in tmp_rows for v in row], 10, lambda v: f"{v:6d}")
    + "\n};\n",
])

emit_table(f"{DATA}/Bpx65Data_table.h", "BPX65_TABLE_H", [
    f"#define BPX65_SAMPLES {N}\n",
    "static const int16_t BPX65_TABLE[] PROGMEM = {\n"
    + fmt_rows([v for row in bpx_rows for v in row], 10, lambda v: f"{v:6d}")
    + "\n};\n",
])

emit_table(f"{DATA}/Bpw34Data_table.h", "BPW34_TABLE_H", [
    f"#define BPW34_SAMPLES {N}\n",
    "static const int16_t BPW34_TABLE[] PROGMEM = {\n"
    + fmt_rows([v for row in bpw_rows for v in row], 10, lambda v: f"{v:6d}")
    + "\n};\n",
])

emit_table(f"{DATA}/Adr4525Data_table.h", "ADR4525_TABLE_H", [
    f"#define ADR4525_SAMPLES {N}\n",
    "static const int16_t ADR4525_TABLE[] PROGMEM = {\n"
    + fmt_rows(adr_rows, 10, lambda v: f"{v:6d}")
    + "\n};\n",
])

emit_table(f"{DATA}/MemoryScanData_table.h", "MEMSCAN_TABLE_H", [
    f"#define MEMSCAN_SAMPLES {N}\n",
    "/* psram, fram, eeprom flip counts per scan */\n"
    "static const uint8_t MEMSCAN_TABLE[] PROGMEM = {\n"
    + fmt_rows([v for row in mem_rows for v in row], 15, lambda v: f"{v:2d}")
    + "\n};\n",
])

emit_table(f"{DATA}/N3163Data_table.h", "N3163_TABLE_H", [
    f"#define N3163_SAMPLES {N}\n",
    f"#define N3163_A_EVENTS {len(n3163_a_events)}\n",
    f"#define N3163_B_EVENTS {len(n3163_b_events)}\n",
    "/* Sparse: sample indices at which the line reads 1. Sorted ascending.\n"
    " * 480 events against 14400 samples - a list is 7x smaller than a\n"
    " * byte per sample, and matches how an event-counting driver works. */\n"
    "static const uint16_t N3163_A_TABLE[] PROGMEM = {\n"
    + fmt_rows(n3163_a_events, 12, lambda v: f"{v:5d}")
    + "\n};\n",
    "static const uint16_t N3163_B_TABLE[] PROGMEM = {\n"
    + fmt_rows(n3163_b_events, 12, lambda v: f"{v:5d}")
    + "\n};\n",
])

# ------------------------------------------------------------------ truth
with open("driver_truth.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["idx", "die_code", "sleep_pct", "load1", "load2",
                "tmp0", "tmp1",
                "bpx65_a", "bpx65_b", "bpw34_a", "bpw34_b",
                "n3163_a", "n3163_b", "adr4525",
                "psram", "fram", "eeprom"])
    sa, sb = set(n3163_a_events), set(n3163_b_events)
    for i in range(N):
        w.writerow([i, hk_rows[i][0], hk_rows[i][1],
                    hk_rows[i][2] & 1, (hk_rows[i][2] >> 1) & 1,
                    *tmp_rows[i], *bpx_rows[i], *bpw_rows[i],
                    1 if i in sa else 0, 1 if i in sb else 0,
                    adr_rows[i], *mem_rows[i]])

# ------------------------------------------------------------------ report
sizes = {
    "esp32_hk":  N * 3,
    "tmp117":    N * 2 * 2,
    "bpx65":     N * 4,
    "bpw34":     N * 4,
    "adr4525":   N * 2,
    "memory":    N * 3,
    "n3163":     (len(n3163_a_events) + len(n3163_b_events)) * 2,
}
print(f"samples per driver : {N:,}\n")
print(f"{'driver':<12}{'flash':>10}")
print("-" * 22)
for k, v in sizes.items():
    print(f"{k:<12}{v/1024:>8.1f} kB")
print("-" * 22)
print(f"{'total':<12}{sum(sizes.values())/1024:>8.1f} kB\n")
print(f"n3163 events       : A={len(n3163_a_events)}  B={len(n3163_b_events)}")
print(f"tmp117 code range  : {min(min(r) for r in tmp_rows)} to "
      f"{max(max(r) for r in tmp_rows)}")
print(f"bpx65 code range   : {min(min(r) for r in bpx_rows)} to "
      f"{max(max(r) for r in bpx_rows)}")
print(f"adr4525 code range : {min(adr_rows)} to {max(adr_rows)}")
print(f"memory flip range  : {min(min(r) for r in mem_rows)} to "
      f"{max(max(r) for r in mem_rows)}")
print("\ndriver_truth.csv written (exact integers baked into flash)")
