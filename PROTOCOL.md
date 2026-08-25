# Payload → OBC wire protocol

Version 1. Plain text over UART, 115200 8N1, no flow control.

This document is the interface contract. If anything here changes, both
`SensorLogger.ino` and the OBC parser must change together.

---

## 1. Frame

```
<FILE 639_000030>\r\n
<body>
<END 639_000030 1707 A3F19C22>\r\n
```

| Element | Meaning |
|---|---|
| `<FILE name>` | start marker. `name` never contains `>` or whitespace |
| body | the file contents, verbatim |
| `<END name bytes crc>` | end marker, byte count and CRC32 of the body |

**The body** is every byte between the newline that terminates the `<FILE>` line
and the `<` of `<END`. It includes each row's own line terminator, including the
last one.

Getting that boundary wrong by one byte makes every CRC fail, so it is worth
testing against a known-good capture before trusting a new parser.

---

## 2. Line endings

Arduino's `println()` emits `\r\n`. A parser that matches `"<END ...>"` exactly
will fail on the trailing `\r`.

Two sources produce bodies with different terminators, and both are valid:

| Source | Row terminator | Why |
|---|---|---|
| Heartbeat | `\n` | rows come from a RAM buffer written byte by byte |
| `RESEND` | `\r\n` | rows were written to flash with `println()` |

The CRC covers whatever bytes actually arrived, so both verify correctly provided
the parser does not normalise line endings before hashing.

---

## 3. Integrity

`bytes` is the decimal length of the body.
`crc` is eight uppercase hex digits: **CRC32, reflected polynomial `0xEDB88320`**,
init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. This is the standard zlib/PNG CRC32 —
*not* the Castagnoli variant (`0x82F63B78`) used by CSP.

Reference: Python `zlib.crc32(body) & 0xFFFFFFFF`.

Both must match, and `name` must be identical in the `<FILE>` and `<END>` lines.
Any mismatch means the frame is corrupt and should be discarded.

---

## 4. Naming and sequence

`639_SSSSSS` — the prefix `639_` identifies the payload; `SSSSSS` is the payload
uptime in seconds at the moment the frame closes, zero-padded to six digits.

**The name is the sequence number.** Consecutive heartbeats step by exactly
`TX_PERIOD_S` (30). A larger step means beats were lost, and says which seconds:

```
639_000060 -> 639_000120     one missed, seconds 60..89 are gone
```

No separate sequence field exists, and none is needed.

`RESEND` replies carry the stored file's name, which steps by `FILE_PERIOD_S`
(3600) instead. A parser tracking gaps should only apply the 30 s rule to
unsolicited frames.

---

## 5. Time

**The payload has no real-time clock and never reports UTC.**

The `sec` column is payload uptime in seconds since boot. The OBC owns the
mapping to absolute time: stamp the arrival of the first frame after a payload
boot, then interpolate.

```
utc = anchor_utc + (sec - anchor_sec)
```

A payload restart resets `sec` to 0. Detect it by the sequence going backwards
and establish a new anchor; data under the old anchor keeps its original
timestamps.

---

## 6. Heartbeat semantics

The OBC does not acknowledge. Arrival of a frame every `TX_PERIOD_S` **is** the
liveness signal.

Two consequences:

1. **A frame goes out every window, even a degraded one.** If the payload's
   transmit buffer overflows it sends a short frame rather than staying silent,
   because silence would read as "payload dead" rather than "one bad window".
   The byte count and CRC describe what is actually in the frame, so a short
   frame still verifies.
2. **The CRC is advisory.** The OBC can detect corruption but cannot request a
   resend automatically. Use the `RESEND` command to recover — every file stays on
   flash until pruned.

Suggested watchdog threshold: a frame takes ~175 ms at 115200, so arrivals land
30.0–30.3 s apart. **35–40 s** gives margin without being slow to notice a dead
payload.

---

## 7. Commands (OBC → payload)

One command per line, `\n` terminated. Replies are single lines except `LIST`
and `RESEND`.

| Command | Reply |
|---|---|
| `LIST` | `<LIST>`, then `name bytes` per line, then `<ENDLIST>` |
| `STATUS` | `<STATUS uptime=… files=… frames=… pruned=… free=… ring=…>` |
| `RESEND <name>` | a normal frame, or `<ERR …>` |
| anything else | `<ERR unknown …>` |

Errors:

| Reply | Cause |
|---|---|
| `<ERR not found NAME>` | no such file, or the name is not a data file |
| `<ERR file open NAME>` | that file is currently being written |
| `<ERR cannot open NAME>` | filesystem refused the read |
| `<ERR no filesystem>` | payload is running `STORAGE_SERIAL` |

`RESEND` refuses the file currently being written because its length changes
mid-stream and the CRC could never match.

Commands are handled between sample ticks, so even a 224 kB `RESEND` cannot delay
a sample by more than one sample period. A `RESEND` takes ~20 s at 115200, during
which heartbeats continue.

---

## 8. Parser requirements

1. Discard bytes until a line beginning `<FILE ` — a receiver that joins
   mid-frame must not report the partial as a failure.
2. Do not buffer the body. A heartbeat is ~2 kB but a `RESEND` is ~230 kB. Feed
   bytes into a running CRC instead.
3. Because a line is only known to be `<END` once complete, hold each line back
   one step and fold it into the CRC when the next line proves it was not the
   terminator.
4. Tolerate both `\n` and `\r\n` without normalising before hashing.
5. Track filename gaps to detect missed heartbeats.

Two working implementations are in this repository: `tools/obc_receiver.py` and
`ObcReceiver/ObcReceiver.ino`. Either can serve as a reference.

---

## 9. Row format

```
sec,tmp0_C,tmp1_C,bpx65_a_V,bpx65_b_V,bpw34_a_V,bpw34_b_V,n3163_a,n3163_b,adr4525_V,psram,fram,eeprom
```

| Column | Unit | Range | Notes |
|---|---|---|---|
| `sec` | s | 0.. | payload uptime, not UTC |
| `tmp0_C`, `tmp1_C` | °C | −20..+30 | **negative in eclipse** |
| `bpx65_*`, `bpw34_*` | V | −0.02..2.8 | **negative at night** — ADC noise about zero |
| `n3163_a/b` | 0/1 | | one `1` per channel per 30 s window |
| `adr4525_V` | V | 2.56 | reference monitor, should be constant |
| `psram`, `fram`, `eeprom` | count | 1..5 | bit flips found in that scan |

**Negative values are expected and correct.** Temperatures go below freezing in
eclipse; photodiodes read ADC noise either side of zero in darkness. A parser
treating these as unsigned turns −11.1 °C into roughly +501 °C — plausible-looking
and completely wrong.

The first row of every body is the column header, so each frame is
self-describing.
