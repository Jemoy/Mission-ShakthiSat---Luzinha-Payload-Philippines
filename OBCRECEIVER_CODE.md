# ObcReceiver — code walkthrough

How the OBC-side firmware is put together, explained in the order the code runs.

This is the **main OBC's** side of the link. It receives frames from the payload,
verifies them, keeps its own copy, and lets you interrogate what it has.

For the payload side see [`../SensorLogger/CODE_WALKTHROUGH.md`](../SensorLogger/CODE_WALKTHROUGH.md).
For the wire format see [`../docs/PROTOCOL.md`](../docs/PROTOCOL.md).

---

## 1. What it does

| | |
|---|---|
| Listens on | `Serial1`, 115200 8N1 |
| Verifies | Byte count and CRC32 of every frame |
| Stores | Every **verified** frame to its own LittleFS |
| Reports | Frame status, heartbeat gaps, arrival intervals |
| Accepts | Local browse commands, and commands forwarded to the payload |

It is a single `.ino` file with no dependencies beyond `LittleFS.h` from the
ESP32 core.

---

## 2. The constraint that shapes everything

**A heartbeat is ~1.8 kB. A `RESEND` of an hourly file is ~210 kB.**

The second will not fit in RAM on any board you would use for this. So the
receiver **never buffers a frame body**. Bytes are fed into a running CRC as they
arrive, written to storage as they arrive, and discarded.

Everything awkward in this file follows from that one decision.

---

## 3. The parser, and the one-line hold-back

The frame format is:

```
<FRAME 639A_000030>
sec,tmp0_C,...          <- body starts here
0,-11.1,...
<END 639A_000030 1807 04A9275A>
```

The body is every byte between the newline ending the `<FRAME>` line and the `<`
of `<END`.

**The problem:** you cannot know a line is the `<END>` terminator until you have
read all of it. By then you would already have fed those bytes into the CRC — and
the terminator is not part of the body.

**The solution:** hold each line back one step.

```cpp
static char     held[LINE_MAX];
static uint16_t heldLen = 0;
static bool     haveHeld = false;

static void handleLine(void)
{
  if (strncmp(line, "<END ", 5) == 0) {
    foldHeld();          /* the held line WAS the last body row */
    finishBody(line);    /* ...and this line is the terminator  */
    return;
  }

  foldHeld();            /* previous line is now known not to be <END> */
  memcpy(held, line, lineLen);   /* hold this one instead */
  heldLen = lineLen;
  haveHeld = true;
}
```

`foldHeld()` is the only place bytes enter the CRC:

```cpp
static void foldHeld(void)
{
  if (!haveHeld) return;
  bodyCrc = crc32_update(bodyCrc, (const uint8_t *)held, heldLen);
  bodyBytes += heldLen;
  storeWrite(held, heldLen);
  rowCount++;
  haveHeld = false;
}
```

**Total RAM: one line buffer plus one held line.** About 700 bytes, regardless of
whether the frame is 1.8 kB or 210 kB.

### Why line endings are not normalised

The payload sends heartbeat rows terminated with `\n` (assembled in RAM byte by
byte) but `RESEND` rows with `\r\n` (written to flash by `println()`). Both are
valid. The CRC covers **whatever bytes actually arrived**, so the parser must not
strip or convert anything before hashing. Stripping the `\r` would break every
`RESEND`.

### Joining mid-stream

```cpp
if (state == WAIT_FILE) {
  if (strncmp(line, "<FRAME ", 7) == 0) { startBody(name); return; }
  ...
}
```

Anything before the first `<FRAME ` is discarded. If the OBC boots while the
payload is mid-frame, the partial is dropped and the next complete frame is
picked up — no false failure reported.

---

## 4. Verification

```cpp
uint32_t actual = crc32_final(bodyCrc);
bool ok = (fields == 3)
       && (bodyBytes == declared)
       && (actual == declaredCrc)
       && (strcmp(fileName, endName) == 0);
```

Four checks, not two. The name must match in both the `<FRAME>` and `<END>`
lines, and the `<END>` line must have parsed cleanly — otherwise a corrupted
terminator could produce a frame that passes on a byte count it invented.

On failure the reason is printed rather than just a verdict:

```
[BAD] 639A_000030  1807 B  31 lines  crc EC530BE0!=04A9275A
```

---

## 5. Local storage

```cpp
static void storeOpen(const char *name);
static void storeWrite(const char *d, uint16_t n);
static void storeClose(const char *name, bool ok);
```

Opened when `<FRAME>` is seen, written line by line as the body streams, closed
when `<END>` is verified.

### A failed frame is deleted, not kept

```cpp
static void storeClose(const char *name, bool ok)
{
  outFile.close();
  if (ok) { storedFiles++; return; }
  FS_HANDLE.remove(path);
  Serial.println(F("  ** frame failed - local copy discarded"));
}
```

**A corrupt file on disk is worse than no file**, because it looks like data. You
would open it weeks later with nothing to indicate anything was wrong.

Better to leave a visible gap — which the filename numbering makes obvious — and
recover it with `RESEND`.

### Space guard

```cpp
if (freeBytes() < MIN_FREE_BYTES) { storing = false; return; }
```

Below 32 kB free it stops storing rather than failing silently mid-write.
Verification and display continue; only the local copy is skipped.

---

## 6. Display

Three modes, set by `PRINT_ROWS`:

| Mode | Behaviour |
|---|---|
| `PRINT_NONE` | Verdict lines only |
| `PRINT_ALL` | Every row — fine for a 30-row heartbeat, unusable for a 3,600-row `RESEND` |
| `PRINT_HEADTAIL` | First 4 and last 3 rows, with `...` between |

### The tail ring

Showing the last N rows of a frame you are not buffering needs a rolling window:

```cpp
static char     tailBuf[TAIL_ROWS][LINE_MAX];
static uint8_t  tailNext = 0;

static void tailPush(const char *s, uint16_t n)
{
  memcpy(tailBuf[tailNext], s, n);
  tailLen[tailNext] = n;
  tailNext = (tailNext + 1) % TAIL_ROWS;
  tailCount++;
}
```

Three slots, overwritten in a circle. A 3,600-row file costs exactly the same RAM
as a 30-row one.

`tailFlush()` then prints them in the right order, skipping any that were already
shown as part of the head.

---

## 7. Console commands

**Lowercase acts on the OBC. UPPERCASE is forwarded to the payload.** Two
namespaces, no prefix to remember.

| Local | Effect |
|---|---|
| `ls` | Stored files, sizes, disk usage |
| `last` | Print the newest stored file |
| `cat <name>` | Print a stored file |
| `head <name> [n]` / `tail <name> [n]` | First / last n lines |
| `cols <name>` | Column names with indices, for `plot` |
| `stats <name>` | Min, max and mean of every column |
| `plot <name> <col>` | ASCII chart of one column |
| `rm <name>` / `rm all` | Delete |
| `free` | Free space |
| `show off\|head\|all` | Change the live display without reflashing |
| `?` | Receiver statistics |

Anything else is passed to the payload verbatim, so `LIST`, `STATUS`, `CLOSE` and
`RESEND` work without the receiver needing to know about them.

### Name or count?

```cpp
/* A file name contains '_' (639A_000600); a bare count does not. Testing
 * the first character would fail, since names start with a digit.   */
if (a1 && strchr(a1, '_')) { ... }
```

The obvious test — is the first character a digit — is wrong, because filenames
start with `639`. This was a real bug: `stats 639A_000600` was parsed as a count
and silently applied to the newest file instead.

### `stats` is one pass, `plot` is two

`stats` accumulates min, max and sum per column while streaming the file once.

`plot` needs the range before it can scale, so it reads the file twice: once to
find min and max, once to bucket rows into columns. `f.seek(0)` between passes.

```cpp
/* Never more buckets than rows, or half of them come out empty and
 * plot as zero - which looks like a signal dropout that is not there. */
uint8_t width = (rows < PLOT_WIDTH) ? (uint8_t)rows : PLOT_WIDTH;
```

Also a real bug: 30 rows spread across 60 columns left every other bucket empty,
drawn at zero, looking exactly like a dropout.

---

## 8. Liveness

```cpp
if (lastFrameMs && !warnedLate && (millis() - lastFrameMs) > LATE_WARN_MS) {
  Serial.println("  ** no frame for N s - payload may be down");
}
```

Frames arrive 30.0–30.3 s apart, so the 40 s default allows for one late frame
without crying wolf.

### Gap detection comes from the filename

```cpp
uint32_t step = num - lastNumber;
if (step != EXPECTED_STEP_S) { gaps++; ... }
```

Names step by exactly 30, so a jump from `639A_000060` to `639A_000120` means one
heartbeat was lost — and says precisely which 30 seconds are missing. No separate
sequence field is needed.

---

## 9. Configuration reference

| Define | Default | Effect |
|---|---|---|
| `PAYLOAD_BAUD` | 115200 | Must match the payload |
| `PAYLOAD_RX_PIN` / `TX_PIN` | 16 / 17 | ESP32 only |
| `EXPECTED_STEP_S` | 30 | Heartbeat interval, for gap detection |
| `LATE_WARN_MS` | 40000 | Silence before warning |
| `PRINT_ROWS` | `PRINT_HEADTAIL` | Live display mode |
| `HEAD_ROWS` / `TAIL_ROWS` | 4 / 3 | Rows shown at each end |
| `STORE_FILES` | 1 | Keep a local copy of verified frames |
| `MIN_FREE_BYTES` | 32 kB | Stop storing below this |
| `LINE_MAX` | 320 | Longest line expected |

---

## 10. Known gaps

| Gap | Consequence |
|---|---|
| No ring buffer on the OBC side | Uses ~11.8 MB/day. On a 6 MB partition it fills in about half a day, then stops storing. `rm all` between runs, or add pruning |
| No UTC assignment | The receiver records frames but does not anchor payload uptime to real time. That belongs in the flight OBC software |
| No forwarding to a radio | This is a bench and simulation tool. A flight OBC would hand verified frames to the downlink queue |
