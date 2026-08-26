# Integration guide — SEED-P payload to Main OBC

For the team writing the flight OBC software. This is what you need to build,
in what order, and the mistakes that have already cost us time so they need not
cost you any.

The formal specification is [`PROTOCOL.md`](PROTOCOL.md). This document is the
practical companion: it tells you how to implement it and how to know when you
have.

---

## 1. What you are integrating

A payload that talks and never listens unless asked.

| | |
|---|---|
| Link | UART, **115200 8N1**, no flow control |
| Direction | Payload transmits continuously; OBC replies only to send commands |
| Encoding | **Plain ASCII text.** No binary, no byte order to agree on |
| Cadence | One frame every **30 s**, ~1.8 kB, taking ~160 ms |
| Duty | **0.5%** of link capacity |

The payload starts transmitting on power-up with no command. It has no clock and
no radio.

---

## 2. Electrical

| Payload | OBC |
|---|---|
| TX → | → RX |
| RX ← | ← TX |
| GND — | — GND |

Both sides 3.3 V. **Common ground is mandatory even when the boards are powered
separately** — a UART signal is a voltage measured against ground, and without a
shared reference the receiver has nothing to compare against. Symptoms of a
missing ground look exactly like a software fault: nothing arrives, or
intermittent garbage.

Connect grounds only. Do not join the supply rails.

You need only **RX and GND** to receive the heartbeat. Wire TX as well if you
want to send commands.

---

## 3. What the payload guarantees

These hold regardless of what your software does:

| | |
|---|---|
| Starts transmitting within 60 s of power-on, no command needed | |
| A frame every 30 s while powered, byte count and CRC correct | |
| Every sample also written to payload flash, most recent ~6 days retained | |
| Any retained file re-sendable intact on request | |
| Keeps transmitting a heartbeat even when a sensor or its storage fails | |
| Unrecognised input produces **no reply at all** | |

That last one matters to you: you cannot provoke a response by sending junk. If
you get no answer to a command, the command was wrong, not lost.

---

## 4. Build it in this order

Each step is testable on its own. Do not skip to step 4.

### Step 1 — Receive bytes

Open the UART and print what arrives. Nothing else. You should see:

```
<BOOT id=639A_ fw=2 rate=1Hz tx=30s file=3600s>
<FRAME 639A_000030>
sec,tmp0_C,tmp1_C,bpx65_a_V,bpx65_b_V,bpw34_a_V,bpw34_b_V,n3163_a,n3163_b,adr4525_V,psram,fram,eeprom
0,-11.1,-8.7,0.0131,-0.0135,0.0058,-0.0106,0,0,2.56,1,1,5
  ... 29 more rows ...
<END 639A_000030 1807 04A9275A>
```

If this works, the wiring and baud rate are correct and everything after is
software.

### Step 2 — Find frame boundaries

Discard bytes until a line begins `<FRAME `. Collect until a line begins
`<END `. Report the name and the number of rows.

**Discarding is not optional.** Your OBC will often start mid-frame, and a
partial frame must not be reported as a failure.

### Step 3 — Verify

Compute the CRC over the body and compare against the `<END>` line, along with
the byte count. Report pass or fail.

At this point you have a working receiver.

### Step 4 — Do something with it

Store it, timestamp it, forward it, reduce it. That is your design, not ours.

---

## 5. The exact things that will catch you out

Every item here cost real time during payload development.

### 5.1 The body boundary is exact

The body is **every byte between the newline that ends the `<FRAME>` line and the
`<` of `<END`** — including the final row's own line terminator.

Off by one byte and every CRC fails. This is the single most common integration
error.

### 5.2 Do not normalise line endings

Heartbeat rows arrive terminated with `\n`. Rows from a `RESEND` arrive with
`\r\n`, because they were written to flash with a function that adds both.

Both are valid. **The CRC covers the bytes that actually arrived**, so if you
strip or convert `\r` before hashing, heartbeats will verify and `RESEND` will
never work.

### 5.3 It is the zlib CRC32, not the Castagnoli one

| | |
|---|---|
| Correct | reflected polynomial **`0xEDB88320`**, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF` |
| Reference | Python `zlib.crc32(body) & 0xFFFFFFFF` |
| **Wrong** | `0x82F63B78` — CRC32C, used by CSP and by SSE hardware instructions |

Both are called "CRC32". They produce entirely different values.

### 5.4 Size your receive buffer for a whole frame

A frame is ~1.8 kB arriving as a single **157 ms burst**. A 256-byte buffer holds
about **22 ms** of that.

Anything your software does during the burst is time not spent draining the
buffer. Printing to a console, writing to storage, a slow interrupt — any of it
can overflow the buffer, and the loss is silent.

**Use at least 4 kB.** On ESP32, `setRxBufferSize()` must be called *before*
`begin()` or it has no effect.

### 5.5 Tell truncation from corruption

Both fail the CRC. The byte count tells you which, and therefore where to look:

| Received | Declared | Meaning | Suspect |
|---|---|---|---|
| 1807 B | 1807 B | Bytes altered in transit | The wire, or the payload |
| **1546 B** | **1812 B** | **Bytes never arrived** | **Your receiver** |

Truncation also leaves whole rows missing from the middle while the rows present
are well-formed. Corruption garbles rows in place.

Log the received byte count on every failure. Without it you cannot tell these
apart, and you will look in the wrong place.

### 5.6 Do not buffer a frame to verify it

A heartbeat is 1.8 kB, but a `RESEND` of an hourly file is **210 kB**. Feed bytes
into a running CRC as they arrive; do not accumulate the body.

There is a subtlety: you cannot know a line is `<END>` until it is complete, but
by then you would already have hashed it — and the terminator is not part of the
body. **Hold each line back one step** and fold it into the CRC only once the
next line proves it was not the terminator.

Both reference implementations do this; see section 7.

### 5.7 Several fields are signed

Temperatures and photodiode voltages go **negative** — the payload is genuinely
below freezing in eclipse, and the photodiodes read converter noise either side
of zero in darkness. About 45% of rows contain a negative value.

Reading a signed 16-bit value as unsigned turns −11.1 °C into roughly **+501 °C**.
That is plausible-looking and completely wrong, which makes it expensive.

### 5.8 Filenames start with a digit

`639A_000030`. Any "is this a name or a number" test based on the first character
will misclassify it. Test for the `_`.

### 5.9 Never reply to input you do not recognise

If your software ever echoes — a loopback adapter, TX shorted to RX, a debug
bridge — then answering unknown lines creates an amplifying feedback loop:

```
<ERR unknown 56,4,3,2>
<ERR unknown <ERR unknown 56,4,3,2>>
<ERR unknown <ERR unknown <ERR unknown 56,4,3,2>>>
```

Traffic doubles every pass until the link saturates. The payload was changed to
stay silent on unrecognised input for exactly this reason. Do the same.

---

## 6. Time

**The payload has no real-time clock and never reports UTC.** The `sec` column is
seconds since payload boot.

Assigning absolute time is your job:

1. On the first frame after a payload boot, record the payload's `sec` against
   your own clock. That pair is the anchor.
2. Every later record is placed by interpolation from that anchor.
3. A payload restart resets `sec` to 0 and emits a fresh `<BOOT>` line. Detect it,
   create a new anchor, and leave data under the old anchor with its original
   times.

This is deliberate. A clock the payload cannot keep accurate across resets and
thermal cycling would produce timestamps that look authoritative and are not.

---

## 7. Reference implementations

Three, increasing in scope. Read whichever matches what you are building.

| | Lines | Does |
|---|---|---|
| `UartBridge/UartBridge.ino` | 70 | Copies bytes both ways. Understands nothing. Use to prove wiring |
| `FrameReader/FrameReader.ino` | 150 | Parses, verifies, prints values. No storage |
| `ObcReceiver/ObcReceiver.ino` | 900 | Verifies, stores, browses, sends commands |
| `tools/obc_receiver.py` | 200 | Same as above, in Python, writes files to disk |

`FrameReader` is the one to read first. It is the smallest complete
implementation of everything in section 5.

---

## 8. Test data

`data/example_capture.bin` is a real capture of the payload's output — 19 frames,
byte for byte as they came off the wire.

```bash
python3 tools/obc_receiver.py --file data/example_capture.bin --out received/
```

Expect 19 frames received, 0 rejected, 0 gaps.

**Develop against this file before connecting hardware.** It is repeatable, it
contains negative values and real CRCs, and it removes the wiring from the set of
things that might be wrong.

To test failure handling, corrupt a byte and confirm your software rejects the
frame rather than accepting it.

---

## 9. Commands, if you want them

Optional. The link works one way without any of this.

One command per line, `\n` terminated.

| Command | Reply |
|---|---|
| `STATUS` | `<STATUS uptime=… files=… frames=… pruned=… free=… ring=… storage=…>` |
| `LIST` | `<LIST>`, then `name bytes` per line, then `<ENDLIST>` |
| `CLOSE` | `<CLOSED name rows>` — rolls the current file so it can be re-sent |
| `RESEND <name>` | A normal frame carrying the stored file |
| anything else | **Nothing** |

`RESEND` is what makes the payload's stored copy useful: a frame that failed its
CRC is recoverable by name for about six days.

It refuses the file currently being written — its length changes mid-stream and
the CRC could never match. Send `CLOSE` first.

A `RESEND` takes ~18 s, during which heartbeats continue on schedule. Your parser
must handle the interleave.

---

## 10. Acceptance checklist

Your software is ready when all of these pass:

| # | Test | Expected |
|---|---|---|
| 1 | Receive 100 consecutive frames | 100 verified, 0 rejected |
| 2 | Start mid-frame | Partial discarded, next frame verifies. No false failure |
| 3 | Corrupt one byte of a captured frame | Rejected, reported as corruption |
| 4 | Delete 200 bytes from a captured frame | Rejected, reported as truncation |
| 5 | Remove one whole frame from a capture | Gap detected from the filename step |
| 6 | Power-cycle the payload | `<BOOT>` seen, new time anchor created |
| 7 | `RESEND` an hourly file (~210 kB) | Verifies, and memory use does not scale with file size |
| 8 | Send junk on the TX line | Payload silent, no loop |

Tests 3, 4 and 5 need deliberately broken copies of the capture file. Build them
once and keep them — **a validator that has never failed has not been tested.**

---

## 11. Questions to settle with us

| | |
|---|---|
| Does the OBC supply payload power, and can it cycle it independently? | Assumed yes |
| Will the OBC supply orbital position per record? | Needed for spatial mapping; the payload has no idea where it is |
| What is the OBC's response to a missed heartbeat? | Proposed: 90 s silence → power cycle, 5 attempts, then hold off and flag |
| Which data product goes to the ground? | The payload produces 5 MB/day; the downlink allocation is far smaller. This is unresolved |

The last one is the important one. The payload will hold ~6 days of full-rate
data on flash, so the decision can be deferred — but not indefinitely.
