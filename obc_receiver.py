#!/usr/bin/env python3
"""
obc_receiver.py  -  stand-in for the Main OBC.

Listens on a serial port for the payload's plain-text file frames,
verifies the byte count and CRC32, writes each file to disk, and tracks
the heartbeat cadence.

  pip install pyserial

  python3 obc_receiver.py --port COM11
  python3 obc_receiver.py --port /dev/ttyUSB0 --baud 115200 --out received/
  python3 obc_receiver.py --file captured.bin        # replay a capture

Frame format:
    <FILE 639_000030>
    sec,tmp0_C,...            <- header line
    0,-11.1,...               <- data rows
    <END 639_000030 1994 A3F19C22>
                   |    |
                   |    CRC32 of the body, poly 0xEDB88320
                   byte count of the body

The body is everything between the newline after <FILE ...> and the
'<' of <END ...>, inclusive of the final newline.
"""
import argparse
import os
import re
import sys
import time
import zlib

FILE_RE = re.compile(rb"<FILE ([^>]+)>\r?\n")
END_RE = re.compile(rb"<END (\S+) (\d+) ([0-9A-Fa-f]{8})>")

# Heartbeat: files are named for the second at which they close, so the
# expected step between consecutive names is the file period.
EXPECTED_STEP_S = 30
LATE_WARN_S = 40


class Receiver:
    def __init__(self, outdir, quiet=False):
        self.buf = bytearray()
        self.outdir = outdir
        self.quiet = quiet
        self.ok = 0
        self.bad = 0
        self.gaps = 0
        self.last_name = None
        self.last_time = None
        if outdir:
            os.makedirs(outdir, exist_ok=True)

    def feed(self, data):
        self.buf += data
        while self._extract():
            pass

    def _extract(self):
        m = FILE_RE.search(self.buf)
        if not m:
            # Nothing to do yet. Drop anything before a possible start so
            # the buffer cannot grow without bound on line noise.
            if len(self.buf) > 200000:
                keep = self.buf.rfind(b"<FILE")
                self.buf = self.buf[keep:] if keep > 0 else bytearray()
            return False

        e = END_RE.search(self.buf, m.end())
        if not e:
            return False                       # frame still arriving

        name = m.group(1).decode(errors="replace")
        body = bytes(self.buf[m.end():e.start()])
        end_name = e.group(1).decode(errors="replace")
        declared = int(e.group(2))
        declared_crc = int(e.group(3), 16)

        actual_crc = zlib.crc32(body) & 0xFFFFFFFF
        size_ok = len(body) == declared
        crc_ok = actual_crc == declared_crc
        name_ok = name == end_name
        good = size_ok and crc_ok and name_ok

        now = time.time()
        gap = ""
        if self.last_time is not None:
            dt = now - self.last_time
            gap = f"  +{dt:.1f}s"
            if dt > LATE_WARN_S:
                gap += "  LATE"
        self.last_time = now

        # Filenames carry the sequence: a step of more than one period
        # means a heartbeat was missed and which seconds are gone.
        if self.last_name:
            try:
                prev = int(self.last_name.split("_")[1])
                cur = int(name.split("_")[1])
                if cur - prev != EXPECTED_STEP_S:
                    self.gaps += 1
                    print(f"  ** gap: {self.last_name} -> {name} "
                          f"({(cur - prev) // EXPECTED_STEP_S - 1} missed)")
            except (IndexError, ValueError):
                pass
        self.last_name = name

        status = "OK" if good else "BAD"
        detail = ""
        if not name_ok:
            detail += f" name mismatch ({name} vs {end_name})"
        if not size_ok:
            detail += f" size {len(body)} != {declared}"
        if not crc_ok:
            detail += f" crc {actual_crc:08X} != {declared_crc:08X}"

        if not self.quiet or not good:
            print(f"[{status}] {name}  {len(body):>6} B{gap}{detail}")

        if good:
            self.ok += 1
            if self.outdir:
                with open(os.path.join(self.outdir, name), "wb") as f:
                    f.write(body)
        else:
            self.bad += 1

        self.buf = self.buf[e.end():]
        return True

    def summary(self):
        print()
        print(f"files received : {self.ok}")
        print(f"files rejected : {self.bad}")
        print(f"heartbeat gaps : {self.gaps}")
        if self.outdir and self.ok:
            print(f"written to     : {self.outdir}/")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="serial device, e.g. COM11 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="received",
                    help="directory to write files into ('' to skip)")
    ap.add_argument("--file", help="replay a captured byte stream instead")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    rx = Receiver(a.out or None, a.quiet)

    if a.file:
        with open(a.file, "rb") as f:
            rx.feed(f.read())
        rx.summary()
        return

    if not a.port:
        ap.error("give --port, or --file to replay a capture")

    import serial
    ser = serial.Serial(a.port, a.baud, timeout=0.2)
    print(f"listening on {a.port} @ {a.baud}  (ctrl-c to stop)")
    try:
        while True:
            data = ser.read(4096)
            if data:
                rx.feed(data)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        rx.summary()


if __name__ == "__main__":
    main()
