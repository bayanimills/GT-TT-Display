#!/usr/bin/env python3
"""Headless screenshotter for the Turbo Touch sim.

  python3 shot.py out/                      # every preset, home screen
  python3 shot.py out/ --screen settings    # a specific screen
  python3 shot.py out/ --preset 2           # a single preset

Writes <out>/<screen>-<NN>-<preset>.png. No browser needed -- handy for
eyeballing a palette across screens or diffing a theme change in review.
"""
import argparse
import os
import select
import struct
import subprocess
import sys
import time
import zlib

W, H = 800, 480
FRAME = 12 + W * H * 2

PRESETS = [
    "Bitaxe Red", "Bitcoin Orange", "Matrix Green", "Cyber Cyan", "Deep Violet",
    "Nord", "Gruvbox", "Paper (light)", "Mono",
]
SCREENS = ["home", "night", "block", "clock", "price", "mempool", "wifi", "settings",
           "odds"]

def bap(param, value):
    """BAP RES sentence with a real XOR checksum -- the parser drops bad ones."""
    body = "BAP,RES,%s,%s" % (param, value)
    ck = 0
    for c in body.encode():
        ck ^= c
    return "$%s*%02X" % (body, ck)


# A plausible steady state so screens are not all "loading...".
# Parameter names must match the strcmp list in main/bap_parser.c.
WARMUP = [
    bap("deviceModel", "Gamma Turbo"),
    bap("asicModel", "BM1370"),
    bap("hashrate", "2201.6"),
    bap("chipTemp", "58.5"),
    bap("power", "35.9"),
    bap("voltage", "12.39"),
    bap("fan_speed", "2728"),
    bap("shares", "66/0"),
    bap("best_difficulty", "3.65M"),
    bap("block_height", "965167"),
    bap("pool", "public-pool.io"),
    bap("poolPort", "21496"),
    bap("poolUser", "bc1qexample.worker"),
    bap("wifi_ssid", "bitaxe-lab"),
    bap("wifi_rssi", "-58"),
    bap("wifi_ip", "192.168.1.50"),
]


def write_png(path, rgb565):
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        base = y * W * 2
        for x in range(W):
            v = rgb565[base + x * 2] | (rgb565[base + x * 2 + 1] << 8)
            rows.append(((v >> 11) & 0x1F) << 3)
            rows.append(((v >> 5) & 0x3F) << 2)
            rows.append((v & 0x1F) << 3)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


class Sim:
    def __init__(self, binary="./gtsim", online=False):
        # sim_rt.c treats the mere presence of SIM_OFFLINE as offline.
        env = dict(os.environ, SIM_OFFLINE="1")
        if online:
            env.pop("SIM_OFFLINE", None)
        # SIM_DEBUG=1 lets the firmware's log through to the terminal.
        stderr = None if os.environ.get("SIM_DEBUG") else subprocess.DEVNULL
        self.p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=stderr, env=env)
        self.buf = b""

    def send(self, line):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()

    def latest_frame(self, settle=1.2):
        """Drain whatever frames arrive over `settle` seconds; return the newest.

        The sim only emits on repaint, so reads must never block -- a static
        screen legitimately produces nothing."""
        deadline = time.time() + settle
        # Remember the newest frame across calls: a command that changes
        # nothing on screen produces no new frame, and the caller still wants
        # the last good one rather than None.
        last = getattr(self, "last", None)
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            r, _, _ = select.select([self.p.stdout], [], [], min(remaining, 0.2))
            if r:
                data = os.read(self.p.stdout.fileno(), 1 << 20)
                if not data:
                    break
                self.buf += data
                while len(self.buf) >= FRAME:
                    if self.buf[:4] != b"GTFB":
                        idx = self.buf.find(b"GTFB", 1)
                        self.buf = self.buf[idx:] if idx > 0 else b""
                        continue
                    self.last_no = struct.unpack("<I", self.buf[4:8])[0]
                    last = self.buf[12:FRAME]
                    self.buf = self.buf[FRAME:]
        self.last = last
        return last

    def fresh_frame(self, timeout=4.0):
        """Force a repaint and return the first frame emitted after it.

        The frame header carries a sequence number, so this cannot hand back a
        frame that predates the commands just sent, whatever the pipe timing."""
        seen = getattr(self, "last_no", 0)
        self.send("R")
        deadline = time.time() + timeout
        while time.time() < deadline:
            frame = self.latest_frame(settle=0.3)
            if getattr(self, "last_no", 0) > seen and frame is not None:
                return frame
        return self.last

    def close(self):
        try:
            self.send("Q")
            self.p.wait(timeout=3)
        except Exception:
            self.p.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--screen", default="home", choices=SCREENS)
    ap.add_argument("--preset", type=int, default=None)
    ap.add_argument("--binary", default="./gtsim")
    ap.add_argument("--cmd", action="append", default=[],
                    help="raw sim command sent after the preset (repeatable), e.g. 'K 1' or 'G layout 0'")
    ap.add_argument("--name", default=None,
                    help="output file name (without .png); default <screen>-<NN>-<preset>")
    ap.add_argument("--touch", action="append", default=[],
                    help="tap at 'x,y' after the commands (repeatable); each tap is a press then release")
    ap.add_argument("--online", action="store_true",
                    help="let price/mempool fetch for real through curl instead of SIM_OFFLINE")
    ap.add_argument("--settle", type=float, default=1.0,
                    help="seconds to wait before the final frame (raise for --online)")
    ap.add_argument("--drag", action="append", default=[],
                    help="drag 'x1,y1,x2,y2' after the taps (repeatable); a press, a swept move, a release")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    presets = [args.preset] if args.preset is not None else range(len(PRESETS))

    sim = Sim(args.binary, online=args.online)
    try:
        sim.latest_frame(settle=5.5)          # let the boot screen finish
        for line in WARMUP:
            sim.send("B " + line)
        # Skin and glass commands must land before the screen is built, since
        # the home screen decides which surface to show at construction.
        pre = [c for c in args.cmd if c.startswith("K ")]
        post = [c for c in args.cmd if not c.startswith("K ")]
        for c in pre:
            sim.send(c)
        sim.send("N " + args.screen)
        sim.latest_frame(settle=1.5)

        for i in presets:
            sim.send("P %d" % i)
            sim.latest_frame(settle=1.0)
            for c in post:
                sim.send(c)
                sim.latest_frame(settle=0.6)
            for t in args.touch:
                x, y = t.split(",")
                sim.send("T %s %s 1" % (x, y))
                sim.latest_frame(settle=0.15)
                sim.send("T %s %s 0" % (x, y))
                sim.latest_frame(settle=0.6)
            for d in args.drag:
                x1, y1, x2, y2 = [int(v) for v in d.split(",")]
                steps = 12
                sim.send("T %d %d 1" % (x1, y1))
                sim.latest_frame(settle=0.1)
                for s in range(1, steps + 1):
                    sim.send("T %d %d 1" % (x1 + (x2 - x1) * s // steps, y1 + (y2 - y1) * s // steps))
                    sim.latest_frame(settle=0.04)
                sim.send("T %d %d 0" % (x2, y2))
                sim.latest_frame(settle=0.8)
            sim.latest_frame(settle=args.settle)
            frame = sim.fresh_frame()
            if frame is None:
                print("no frame for preset %d" % i, file=sys.stderr)
                continue
            name = args.name or "%s-%02d-%s" % (args.screen, i, PRESETS[i].split()[0].lower())
            path = os.path.join(args.outdir, name + ".png")
            write_png(path, frame)
            print(path)
    finally:
        sim.close()


if __name__ == "__main__":
    main()
