# SensorLogger — code walkthrough

How the payload firmware is put together, explained in the order the code runs
rather than the order it is written.

For what the payload *does*, see [`docs/SYSTEM_OVERVIEW.md`](../docs/SYSTEM_OVERVIEW.md).
For the wire format, see [`docs/PROTOCOL.md`](../docs/PROTOCOL.md).

---

## 1. File layout

```
SensorLogger/
├── SensorLogger.ino        the application, ~990 lines
└── src/
    ├── SensorTypes.h       six data structs, one per sensor group
    ├── DummySource.h/.cpp  shared sample cursor
    ├── Tmp117Driver.h/.cpp ┐
    ├── Bpx65Driver.h/.cpp  │
    ├── Bpw34Driver.h/.cpp  │  six drivers, identical shape
    ├── N3163Driver.h/.cpp  │
    ├── Adr4525Driver.h/.cpp│
    ├── MemoryScanDriver.h  ┘
    └── data/               PROGMEM replay tables, ~500 kB
```

Arduino compiles everything in the sketch folder and in `src/` recursively. If
`src/` is flattened or `src/data/` goes missing, the build fails on the PROGMEM
tables.

### Why the drivers are separate files

Every driver exposes the same two functions:

```cpp
void tmp117_begin();
bool tmp117_read(Tmp117Data &out);
```

Replacing a dummy with real I²C or SPI code is a **single-file edit**. Nothing
above it changes — not the application, not the other drivers, not the data
format.

### Why the drivers do not own time

A real TMP117 has no idea what sample number it is on; it returns whatever it
currently reads. The dummies behave the same way. They read from a shared cursor
that the application advances once per sample:

```cpp
void     dummy_advance();     // step to the next sample
uint16_t dummy_index();       // where are we now
```

This keeps all six in step without any of them knowing about each other, and
means one can be swapped for real hardware without disturbing the rest.

---

## 2. `setup()` — runs once

**Order matters here, and the order is deliberate.**

```cpp
OBC_UART.begin(...);      // 1. the link, first
sendBootFrame();          // 2. announce, before anything can block

Serial.begin(115200);     // 3. console, bounded wait
while (!Serial && millis() < CONSOLE_WAIT_MS) { }

storageOk = LittleFS.begin(true);   // 4. storage, failure is survivable
```

**(1) and (2) come first** because in flight there is no serial monitor, and the
OBC's recovery procedure watches for the boot frame within a few seconds of
power-on. Sending it before anything that can block means it arrives in
milliseconds regardless of whether a console is attached or the filesystem
mounts.

**(3) The console wait is bounded.** Native-USB boards (ESP32-S3/C3, SAMD,
RP2040) enumerate *after* `begin()` returns, so without a wait the banner is
written into a port that does not exist yet. But a bare `while (!Serial)` hangs
forever on a board flying with no USB host — hence `CONSOLE_WAIT_MS`, which can
be set to 0 for a flight build.

**The code runs with no monitor attached.** The wait simply expires.

**(4) A storage mount failure is not fatal.** An earlier version looped forever
here, which silenced the payload — and a silent payload gets power-cycled five
times and then latched off by the OBC. Losing a payload because a filesystem
would not mount is a bad trade.

Instead `storageOk` is set false and the payload runs without it: samples are
still taken, displayed and transmitted every 30 s. Only the on-board recovery
buffer is lost, and `STATUS` reports `storage=FAILED` so the ground knows.

The rest of setup, in order:

| Step | Purpose |
|---|---|
| `deleteAllDataFiles()` | Filenames restart each run and would collide with the previous run's |
| `resolveMaxFiles()` | Ring size computed once, from the actual partition |
| Open UART to the OBC | |
| `begin()` every driver | |
| `openNextFile()` | First file open before the first sample |

---

## 3. `loop()` — the whole application

```cpp
void loop()
{
  pollCommands();                                   // 1

  if (millis() - lastSampleMs < SAMPLE_PERIOD_MS) return;
  lastSampleMs += SAMPLE_PERIOD_MS;                 // 2

  writeRow();
  uptimeS++;
  dummy_advance();

  if (rowsInFrame >= ROWS_PER_FRAME) sendHeartbeat();   // 3
  if (rowsInFile  >= ROWS_PER_FILE)  { close; open; }   // 4
}
```

**(1) Commands are polled first**, so a `RESEND` is handled between samples. Even
a 224 kB transfer cannot delay a sample by more than one sample period.

**(2) `lastSampleMs += PERIOD`, not `= millis()`.** The second form accumulates
drift: a few milliseconds late each second becomes seconds of error across an
hour. This form holds a fixed cadence indefinitely.

**(3) and (4) are independent.** The heartbeat fires every 30 rows, the file rolls
every 3,600. They do not divide evenly into one another and do not need to,
because the transmit path never reads the file.

---

## 4. `writeRow()` — one sample

Six driver reads into six structs, then the same row goes three places:

| Destination | Purpose | Fails independently |
|---|---|---|
| `logFile.println(row)` | Permanent record on flash | Storage fault |
| `txAppend(row)` | RAM buffer for the next heartbeat | — |
| `printTableRow(...)` | Serial monitor for the operator | — |

**The independence is the point.** A storage fault does not stop the heartbeat,
because the heartbeat is assembled in RAM. A dead UART does not stop logging.

### `logFile.flush()` after every row

Buffering an hour of data means a power cut loses the whole file. Flushing bounds
the loss to a single row, at a small cost in flash wear. On a payload that may
brown out or hit its watchdog, that is the right trade.

### The two format branches

```cpp
#if OUTPUT_ENGINEERING
  snprintf(row, ..., "%.1f,%.1f,%.4f,...", tmp117_toC(...), ads_toVolts(...));
#else
  snprintf(row, ..., "%d,%d,%d,...",       tp.raw[0], bx.raw[0], ...);
#endif
```

Both write into the same `row[]` buffer, so everything downstream — file,
transmit buffer, display — is identical either way.

Decimal places are chosen to be lossless, not arbitrary:

| Field | Decimals | Device step | Result |
|---|---|---|---|
| TMP117 | 1 | 1/128 °C | Deliberately coarse; discards resolution |
| Photodiodes | 4 | 125 µV | Finer than the ADC; **nothing lost** |
| ADR4525 | 2 | 125 µV | Collapses to a constant |

---

## 5. The transmit path

```cpp
static void sendFrame(const char *name, const char *body, uint16_t len)
{
  OBC_UART.println("<FRAME ...>");
  OBC_UART.write((const uint8_t *)body, len);
  uint32_t crc = crc32(body, len);
  OBC_UART.println("<END name bytes crc>");
}
```

### Why the CRC is in the trailer, not the header

With the CRC up front you would have to scan the body twice — once to hash it,
once to send it. In the trailer, the body streams out in a single pass.

That is also what makes `RESEND` of a 230 kB file possible with no RAM buffer.

### After sending

```cpp
txReset();
txAppend(COLUMN_HEADER);   // every frame is self-describing
rowsInFrame = 0;
```

Each frame carries its own column header, so a receiver that joined mid-stream
can still interpret it.

### Fire-and-forget

The OBC does not acknowledge. Arrival of a frame every 30 s *is* the liveness
signal, which has two consequences in the code:

- No retry loop. With nothing coming back there is no signal that would make a
  retry meaningful, and a duplicate frame would inflate the OBC's heartbeat count.
- A frame goes out **even if the buffer overflowed**. The byte count and CRC
  describe what is actually in the frame, so a short frame still verifies.
  Silence would read as "payload dead" rather than "one bad window".

---

## 6. Storage and the ring buffer

```cpp
static void ensureSpace(void)
{
  for (uint8_t guard = 0; guard < 32; guard++) {
    if (countDataFiles() < maxFiles) return;
    if (!deleteOldestDataFile()) { /* nothing left to prune */ return; }
  }
}
```

Called before each file opens.

**`maxFiles` is computed once at boot** from `LittleFS.totalBytes()`, reserving
20% for metadata and wear levelling. On a 6 MB partition that is 21 hourly files.

**The `guard` counter** stops the loop spinning forever if the filesystem cannot
free space. Bounding any loop that might not terminate is a habit worth keeping
on a system with a watchdog.

**`deleteOldestDataFile()`** finds the lowest number in the filename. That works
because names encode the second at which the file closed, so lexical and
chronological order coincide.

### Why files are opened with an explicit remove first

```cpp
if (FS_HANDLE.exists(currentName)) FS_HANDLE.remove(currentName);
logFile = FS_HANDLE.open(currentName, FILE_WRITE);
```

`FILE_WRITE` means **truncate** on the ESP32 FS layer but **append** in the
classic SD library. Without the explicit remove, a second run on SD would
silently double the rows in each file. Removing first gives identical behaviour
on every backend.

### Why hourly files, not 30-second files

LittleFS allocates whole 4096-byte blocks. A file smaller than one block still
occupies a full block:

| File period | Content | On disk | Slack | 30 MB lasts |
|---|---|---|---|---|
| 30 s | 2 kB | 4 kB | **51%** | 2.7 days |
| **1 hour** | **205 kB** | **208 kB** | **1.4%** | **6.15 days** |

Same data, 2.3× the duration. The content barely changed; the allocation did.

---

## 7. Commands

Read a line at a time from the OBC UART, executed between samples.

| Command | Implementation note |
|---|---|
| `LIST` | Walks the directory, prints name and size |
| `STATUS` | Counters and free space in one line |
| `CLOSE` | Rolls the current file so it becomes retrievable |
| `RESEND <name>` | Streams a stored file back |

### `RESEND` streams, it does not buffer

```cpp
uint32_t crc = crc32_init();
uint8_t chunk[128];
while ((n = f.read(chunk, sizeof(chunk))) > 0) {
    OBC_UART.write(chunk, (size_t)n);
    crc = crc32_update(crc, chunk, (uint16_t)n);
    total += (uint32_t)n;
}
snprintf(line, ..., "<END %s %lu %08lX>", name, total, crc32_final(crc));
```

128 bytes at a time with the CRC accumulating as it goes. **A 230 kB file never
sits in RAM.**

### `RESEND` refuses the open file

```cpp
if (strcmp(path, currentName) == 0) {
    OBC_UART.println("<ERR file open ...>");
    return;
}
```

Its length changes while streaming, so the byte count and CRC could never match.
This is why `CLOSE` exists — it rolls the file so it can be retrieved.

---

## 8. The design idea underneath all of it

**Nothing depends on anything it does not have to.**

| Component | Does not know about |
|---|---|
| Drivers | Files, the UART, each other |
| Heartbeat | The stored file — it reads from RAM |
| Stored file | The heartbeat period |
| Time | Any individual driver — it comes from a shared cursor |

That is why the 30-second and hourly periods could be decoupled later without
touching anything else, and why a driver can be swapped for real hardware in one
edit.

Coupling is easy to add and expensive to remove. Most of the structure here
exists to avoid it.

---

## 9. Configuration reference

All at the top of `SensorLogger.ino`.

| Define | Default | Effect |
|---|---|---|
| `SAMPLE_PERIOD_MS` | 1000 | Sample rate |
| `TX_PERIOD_S` | 30 | Heartbeat interval |
| `FILE_PERIOD_S` | 3600 | File rollover |
| `RUN_DURATION_S` | 7200 | 0 = run forever |
| `OUTPUT_ENGINEERING` | 1 | 0 writes raw device codes |
| `ADS_FSR_VOLTS` | 4.096 | **Must match the PGA setting** |
| `DISPLAY_MODE` | `DISPLAY_TABLE` | `DISPLAY_CSV`, `DISPLAY_OFF` |
| `CLEAN_ON_BOOT` | 1 | Wipe old files at startup |
| `MAX_STORED_FILES` | 0 | 0 = size the ring from the partition |
| `STORAGE_BACKEND` | `STORAGE_LITTLEFS` | `STORAGE_SD`, `STORAGE_SERIAL` |
| `ACCEPT_COMMANDS` | 1 | Enables the RX path |

`STORAGE_SERIAL` writes nothing to disk and prints each file to the monitor
between `BEGIN`/`END` markers. Useful for a first run: it exercises the drivers,
timing and row format with no filesystem risk.

---

## 10. Known gaps

| Gap | Consequence |
|---|---|
| Write failures are not checked | `println()` returns a count nobody reads. A full volume gives empty files and healthy-looking heartbeats |
| Memory pattern is not scanned before writing | On real hardware this would erase every upset accumulated while the payload was off — see `docs/DECISION_LOG.md`, BG-08 |
| Sleep residency is not logged | The only signal that light-sleep silently stopped working |
| 3N163 encoding unsettled | 16 bits each, or 1 bit each, depending on whether the channels are digital or thresholded analog |
