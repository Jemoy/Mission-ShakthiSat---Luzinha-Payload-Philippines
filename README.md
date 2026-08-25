# payload-logger

Payload data logger and OBC link for a 500 km Sun-synchronous CubeSat payload.

The payload samples six sensors at 1 Hz, writes them to flash as CSV, and pushes
the last 30 seconds of data to the main OBC over UART every 30 seconds. The OBC
treats the arrival of each frame as the payload's heartbeat.

Sensor drivers are currently **dummies** that replay two hours of pre-generated
data from flash, so the whole chain can be tested before any hardware exists.
Each dummy has the same interface as the real driver will, so swapping one in is
a single-file edit.

---

## Mission context

| | |
|---|---|
| Orbit | 500 km Sun-synchronous, i = 97.4°, period 94.6 min |
| Payload link | UART, 115200 8N1, plain text |
| Downlink | UHF 9600 bps, ~370 kB/day payload allocation |
| Sample rate | 1 Hz |
| Heartbeat | one frame every 30 s |

---

## Repository layout

```
SensorLogger/          payload sketch
  SensorLogger.ino
  src/                 six sensor drivers + shared types
    data/              PROGMEM replay tables (~170 kB)
ObcReceiver/           OBC sketch: verifies frames, sends commands
  ObcReceiver.ino
tools/
  obc_receiver.py      PC-side receiver, writes verified files to disk
  gen_pure.py          generates the 2 h dummy dataset
  gen_memory.py        generates the bit-flip counts
  split.py             splits the dataset per sensor
  make_drivers.py      converts CSVs into PROGMEM tables + drivers
data/                  the generated CSVs and the driver truth reference
docs/
  PROTOCOL.md          wire format specification
```

---

## Hardware

Payload: **ESP32-P4** (any board with a spare hardware UART works for testing).
OBC: a second board, or a PC with a USB-TTL adapter.

| Payload | OBC |
|---|---|
| GPIO 17 (TX) → | → RX |
| GPIO 16 (RX) ← | ← TX |
| GND — | — GND |

TX crosses to RX. **Common ground is mandatory** — without a shared reference the
signal has nothing to swing against and you get intermittent garbage that reads
like a software bug. Both sides must be 3.3 V.

Only TX and GND are strictly required for the heartbeat, since the OBC does not
acknowledge. Wire RX to use the commands.

> Check GPIO 16/17 against your P4 module's pinout first. Some P4 variants commit
> those pins to the in-package PSRAM interface. If yours does, `Serial1`
> initialises on unavailable pins, the console still prints `[SEND]` normally,
> and nothing reaches the wire.

---

## Quick start

**1. Offline, no hardware.** Verify the protocol end to end:

```bash
cd tools
python3 obc_receiver.py --file ../data/example_capture.bin --out received/
```

**2. Payload only.** Open `SensorLogger/SensorLogger.ino`, flash it, open the
Serial Monitor at 115200. You should see the banner, then an aligned table of
sensor values, one row per second, with `[SEND]` lines every 30 rows.

**3. Payload + PC.** Connect a USB-TTL adapter's RX to payload GPIO 17 and GND to
GND:

```bash
python3 tools/obc_receiver.py --port /dev/ttyUSB0 --out received/
```

**4. Payload + OBC board.** Flash `ObcReceiver/ObcReceiver.ino` to the second
board and wire as above.

Both sketches are **sketches, not libraries** — unzip into your Arduino
sketchbook and open the `.ino`. `Add .ZIP Library` will reject them.

---

## Wire format

See [docs/PROTOCOL.md](docs/PROTOCOL.md) for the full specification.

```
<FILE 639_000030>
sec,tmp0_C,tmp1_C,bpx65_a_V,...,psram,fram,eeprom
0,-11.1,-8.7,0.0131,-0.0135,0.0058,-0.0106,0,0,2.56,1,1,5
  ... 30 rows ...
<END 639_000030 1707 A3F19C22>
```

The `<END>` line carries the body's **byte count** and **CRC32** (poly
`0xEDB88320`). They sit in the trailer rather than the header so the payload can
stream the body in one pass instead of scanning it twice.

The **filename is the sequence number**: names step by exactly 30, so a jump from
`639_000060` to `639_000120` means one heartbeat was missed and says precisely
which 30 seconds are gone. No separate sequence field is needed.

---

## OBC commands

Optional — the link works one-way without them. Type into the OBC's Serial
Monitor, or send over the RX line:

| Command | Reply |
|---|---|
| `LIST` | `<LIST>` … names and sizes … `<ENDLIST>` |
| `STATUS` | `<STATUS uptime=… files=… frames=… pruned=… free=… ring=…>` |
| `CLOSE` | roll the current file early so it can be retrieved |
| `RESEND 639_003600` | the stored file, in the same framing as a heartbeat |

The open file cannot be resent — its length changes while streaming, so the byte
count and CRC could never match. `CLOSE` rolls it, after which `RESEND` works.
Without it the first retrievable file appears an hour in.

`RESEND` is what makes the flash copy worth keeping. Without it the stored files
are write-only and a corrupt or missed heartbeat is unrecoverable.

---

## Storage

Two independent periods, and confusing them is expensive:

| | Default | Purpose |
|---|---|---|
| `TX_PERIOD_S` | 30 s | heartbeat to the OBC, sent from RAM |
| `FILE_PERIOD_S` | 3600 s | disk file rollover |

They can differ because the transmit path never reads the file back — each row is
appended to both as it is produced.

**Why hourly files.** LittleFS allocates whole 4096-byte blocks. A 2 kB file
still occupies 4 kB, wasting 51%. A 226 kB hourly file wastes 1.5%:

| File period | On disk/hour | Slack | 32 MB lasts |
|---|---|---|---|
| 30 s | 492 kB | 51% | 2.84 d |
| **1 hour** | **228 kB** | **1.5%** | **6.10 d** |

Same data, 2.2× less flash. Content-only the difference is under 6% — the cost is
almost entirely allocation, not bytes.

**Ring buffer.** `MAX_STORED_FILES 0` sizes the ring from the partition,
reserving 20% for metadata and wear levelling. On a 6 MB partition that is 21
files = 21 hours of rolling history. Logging never stops; the oldest file is
pruned as each new one opens.

> Check **Tools → Partition Scheme** before a long run. Minimal/OTA schemes give
> ~190 kB, which holds under an hour. The startup banner prints the volume size
> and whether the configured run will fit.

---

## Configuration

All at the top of `SensorLogger.ino`:

| Define | Default | Notes |
|---|---|---|
| `SAMPLE_PERIOD_MS` | 1000 | 1 Hz |
| `TX_PERIOD_S` | 30 | heartbeat interval |
| `FILE_PERIOD_S` | 3600 | file rollover |
| `RUN_DURATION_S` | 7200 | 0 = run forever |
| `OUTPUT_ENGINEERING` | 1 | 0 writes raw device codes |
| `ADS_FSR_VOLTS` | 4.096 | **must match the PGA setting** |
| `DISPLAY_MODE` | `DISPLAY_TABLE` | `DISPLAY_CSV`, `DISPLAY_OFF` |
| `CLEAN_ON_BOOT` | 1 | wipe old files at startup |
| `MAX_STORED_FILES` | 0 | 0 = auto from partition |
| `STORAGE_BACKEND` | `STORAGE_LITTLEFS` | `STORAGE_SD`, `STORAGE_SERIAL` |

`STORAGE_SERIAL` writes nothing to disk and prints each file to the monitor
between `BEGIN`/`END` markers. Useful for a first run: it proves the drivers,
timing and row format with no filesystem risk.

---

## Data format note

Files hold **engineering units** (°C, volts). Decimal places are chosen to be
lossless rather than arbitrary:

| Field | Decimals | Device step |
|---|---|---|
| TMP117 | 1 | 1/128 °C — 1 dp discards resolution deliberately |
| Photodiodes | 4 | 125 µV — 4 dp is finer, so nothing is lost |
| ADR4525 | 2 | 10 mV step vs a 2 mV signal range |

Set `OUTPUT_ENGINEERING 0` for raw integer codes. Raw is reversible if a
calibration constant is later found wrong; engineering values are not.

**The `adr4525_V` column is a diagnostic, not a measurement.** It monitors the
voltage reference and should read a constant `2.56` forever. It is also the last
field in the row, so a steady value proves every field boundary before it is
landing correctly. If it starts wandering, either the reference is drifting or
the parsing has slipped — and both matter.

---

## Testing

The chain has been verified in simulation over a full 2-hour run:

| Check | Result |
|---|---|
| Rows produced | 7,200 across 2 hourly files |
| Heartbeats | 240, cadence exact |
| Frames decoded at OBC | 240 ok, 0 bad, 0 gaps |
| RESEND of a 224 kB file | byte-identical to the flash copy |
| One byte corrupted | rejected on CRC |
| A file removed | reported as a heartbeat gap |
| Receiver joined mid-stream | resynchronises, no false failure |

Fault injection matters here: a validator that never fails is worthless, so each
check was confirmed to fire on a deliberately broken stream.

---

## Known limits

- **Write failures are not checked.** `logFile.println()` returns a byte count
  nothing looks at. If the volume fills in a way the ring buffer cannot fix, you
  get empty files and healthy-looking heartbeats.
- **Sleep residency is not logged.** It was removed with the ESP32 housekeeping
  fields. Before flight it should return, at least per-orbit — it is the only
  signal that light-sleep silently stopped working.
- **1 Hz exceeds the downlink by ~17×.** At 1 Hz the payload produces ~6.2 MB/day
  of text against a ~370 kB/day allocation. The UART link is fine (0.9% utilised),
  but a reduction step — 30-second statistics, or event-triggered windows — has to
  exist somewhere before flight. It is not implemented here.
- **PSRAM on-die ECC is unconfirmed.** If the ESP32-P4's in-package PSRAM
  transparently corrects single-bit errors, the SEU experiment measures zero for
  the entire mission while looking perfectly healthy.

---

## Regenerating the dummy data

```bash
cd tools
python3 gen_pure.py        # 2 h of sensor data at 1 Hz
python3 gen_memory.py      # bit-flip counts for 3 memory devices
python3 split.py           # split per sensor
python3 make_drivers.py    # -> PROGMEM tables + driver sources
```

The source dataset keeps all four TMP117 columns. How many are compiled into
flash is a build decision: `TMP_CHANNELS` in `make_drivers.py` (currently **2**).
Raising it means regenerating the table and widening `Tmp117Data.raw[]`,
`Tmp117Driver.cpp`, the column header and both row format strings.

`make_drivers.py` also writes `driver_truth.csv`, the exact integers baked into
flash. That is the reference to diff against, not the engineering-unit CSVs —
converting volts back to codes can land ±1 count off.
