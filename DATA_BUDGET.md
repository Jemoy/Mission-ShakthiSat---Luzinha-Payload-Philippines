# Payload data size budget

**Scope.** This document covers how much data the payload produces, how much
flash it occupies, and how long storage lasts. It is self-contained: nothing here
depends on having read the repository README.

**Context.** The payload samples six sensors at 1 Hz on a 500 km Sun-synchronous
CubeSat, writes them to on-board flash as CSV, and pushes the most recent
30 seconds to the main OBC over UART as a heartbeat. Storage is a ring buffer, so
logging never stops — the oldest file is deleted when the volume approaches full.

Measured from the shipped configuration, not estimated. All figures come from a
full 2-hour simulated run of `SensorLogger.ino` producing 7,200 rows across two
hourly files.

| Configuration | Value |
|---|---|
| Sample rate | 1 Hz |
| Columns | 14 (`sec` + 13 fields) |
| Output format | engineering units (°C, V) |
| Heartbeat period | 30 s |
| File period | 3600 s (hourly) |
| Filesystem | LittleFS, 4096 B block |

---

## 1. Headline numbers

| | Content | On disk |
|---|---|---|
| Row | 58.3 B | — |
| Hourly file | 205 kB | 208 kB |
| Per day | 5.05 MB | **5.11 MB** |
| **Time to 30 MB** | 6.22 d | **6.15 d** (148 h) |
| Time to 32 MB | 6.64 d | 6.56 d (157 h) |

147 hourly files fill 30 MB.

Content and on-disk figures are within 1.2% of each other. That is the point of
hourly files — see section 3.

---

## 2. Where the bytes go

A row is 58.3 bytes on average, ranging 56–59 depending on sign characters and
magnitude. Per-column contribution, measured across 3,600 rows:

| Column | Mean chars | Share |
|---|---|---|
| `bpx65_a_V` | 6.00 | 13.5% |
| `bpx65_b_V` | 6.00 | 13.5% |
| `bpw34_a_V` | 6.00 | 13.5% |
| `bpw34_b_V` | 6.00 | 13.5% |
| `adr4525_V` | 4.00 | 9.0% |
| `tmp1_C` | 3.82 | 8.6% |
| `tmp0_C` | 3.81 | 8.6% |
| `sec` | 3.69 | 8.3% |
| `n3163_a` | 1.00 | 2.3% |
| `n3163_b` | 1.00 | 2.3% |
| `psram` | 1.00 | 2.3% |
| `fram` | 1.00 | 2.3% |
| `eeprom` | 1.00 | 2.3% |
| commas (13) | 12.00 | — |
| CRLF | 2.00 | — |
| Header, once per file | 103 B | — |

**The four photodiode columns are 54% of the row.** Everything else together is
less than half. Any further reduction has to start there.

---

## 3. Why the file period matters more than the row

LittleFS allocates whole 4096-byte blocks. A file smaller than one block still
occupies a full block, and the remainder is unusable by anything else.

| File period | Rows | Content | On disk | Slack | MB/day | 30 MB lasts |
|---|---|---|---|---|---|---|
| 30 s | 30 | 2 kB | 4 kB | **54.8%** | 11.80 | 2.67 d |
| 1 min | 60 | 4 kB | 4 kB | 12.1% | 5.90 | 5.33 d |
| 5 min | 300 | 17 kB | 20 kB | 14.1% | 5.90 | 5.33 d |
| 15 min | 900 | 51 kB | 52 kB | 1.3% | 5.11 | 6.15 d |
| 30 min | 1800 | 103 kB | 104 kB | 1.4% | 5.11 | 6.15 d |
| **1 hour** | **3600** | **205 kB** | **208 kB** | **1.4%** | **5.11** | **6.15 d** |
| 6 hour | 21600 | 1230 kB | 1232 kB | 0.2% | 5.05 | 6.23 d |

Two things worth noting.

**Going from 30-second to 1-minute files more than doubles the duration** — from
2.67 to 5.33 days — with no change to the data at all. That single step recovers
most of the available gain.

**Past 15 minutes the curve flattens.** 15 min, 30 min and 1 hour are within
0.1% of each other; 6 hours buys another 1.3%. One hour was chosen as the point
where slack is negligible while files stay small enough to transfer individually
over the 115200 baud link in about 20 seconds.

The relationship is not monotonic — 5-minute files (14.1% slack) are slightly
worse than 1-minute files (12.1%), because their content lands just past a block
boundary. Landing near a block multiple matters more than being large.

---

## 4. Reduction options, if 6 days is not enough

All at hourly file period:

| Change | Row | MB/day | 30 MB lasts | Cost |
|---|---|---|---|---|
| **Current** | 58.3 B | 5.11 | **6.15 d** | — |
| Photodiodes to 2 dp | 50.3 B | 4.42 | 7.11 d | loses 10 mV resolution on a 125 µV LSB |
| Drop `adr4525_V` | 53.3 B | 4.62 | 6.81 d | loses the bit-alignment diagnostic |
| `OUTPUT_ENGINEERING 0` | 41.0 B | 3.64 | **8.65 d** | raw codes; ground must convert |

Raw integer codes are the strongest single option and the only one that loses no
information — `-1423` carries exactly what `-11.1172` does, in fewer characters,
and remains reprocessable if a calibration constant is later corrected.

Dropping `adr4525_V` saves less than the format change and removes the column
that proves the reference held and that every field boundary parsed correctly.
Not recommended.

---

## 5. Ring buffer

The ring is sized from the partition at boot, reserving 20% for
metadata and wear levelling.

| Partition | Files kept | History | Fills after |
|---|---|---|---|
| 190 kB (minimal/OTA) | 2 | 2 h | — prunes immediately |
| 1.5 MB (default 4 MB flash) | 5 | 5 h | 5 h |
| 3 MB | 11 | 11 h | 11 h |
| 6 MB | 21 | 21 h | 21 h |
| 12 MB | 46 | 46 h | 1.9 d |
| 30 MB | 117 | 117 h | 4.9 d |

Logging never stops. Once the ring is full the oldest file is deleted as each new
one opens, so the payload always holds the most recent N hours.

The payload prints the resolved ring size at startup, along with whether the
configured run will fit the volume:

```
volume     : 6144 kB total, 6144 kB free
file size  : ~228 kB   ring keeps 21 -> 21 h of history
run needs  : 456 kB for 2 files -> FITS
```

---

## 6. UART link, for comparison

The OBC link is nowhere near loaded, and should not be confused with the storage
or downlink budgets:

| | |
|---|---|
| Heartbeat frame | ~1.8 kB (30 rows + header + framing) |
| Transmit time at 115200 8N1 | ~160 ms |
| Window | 30,000 ms |
| **Duty cycle** | **0.5%** |
| Per day over UART | 5.2 MB |

A `RESEND` of an hourly file is ~208 kB and takes about 20 s, during which
heartbeats continue on schedule.

---

## 7. Open item: downlink

This report covers **storage and the UART link**. It does not resolve the
downlink.

| | Per day |
|---|---|
| Payload produces | 5.11 MB |
| UHF allocation at 9600 bps | ~370 kB |
| **Ratio** | **~14× over** |

The UART link and the flash both cope comfortably. The radio does not. Closing
that gap needs a reduction step that does not exist yet — 30-second statistics
instead of 1 Hz rows, event-triggered full-rate windows, or a lower sample rate.

Format changes cannot close it: even raw codes at 3.64 MB/day are 10× over. This
is a sampling-rate and data-product question, not an encoding one.

---

## 8. Summary

| Question | Answer |
|---|---|
| How big is one row? | 58.3 B |
| How big is one hourly file? | 205 kB content, 208 kB on disk |
| How much per day? | 5.11 MB |
| How long to fill 30 MB? | **6.15 days** |
| How long to fill 32 MB? | 6.56 days |
| Is the UART link loaded? | No — 0.5% duty |
| Is the downlink closed? | **No — ~14× over allocation** |

The storage side is solved: hourly files brought slack from 55% to 1.4%, and the
ring buffer means the payload runs indefinitely while always holding the most
recent N hours.

The downlink is not, and cannot be fixed by encoding. It needs a decision about
what data product goes to the ground.

---

*Figures measured from a full 2-hour run producing 7,200 rows across two hourly
files. Regenerate with the tools in the repository if the column set or output
format changes — every number here follows from the row size.*
