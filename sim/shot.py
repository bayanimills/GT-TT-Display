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
SCREENS = ["home", "night", "block", "clock", "price", "mempool", "wifi", "settings"]

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
    def __init__(self, binary="./gtsim"):
        env = dict(os.environ, SIM_OFFLINE="1")
        self.p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, env=env)
        self.buf = b""

    def send(self, line):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()

    def latest_frame(self, settle=1.2):
        """Drain whatever frames arrive over `settle` seconds; return the newest.

        The sim only emits on repaint, so reads must never block -- a static
        screen legitimately produces nothing."""
        deadline = time.time() + settle
        last = None
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
                    last = self.buf[12:FRAME]
                    self.buf = self.buf[FRAME:]
        return last

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
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    presets = [args.preset] if args.preset is not None else range(len(PRESETS))

    sim = Sim(args.binary)
    try:
        sim.latest_frame(settle=5.5)          # let the boot screen finish
        for line in WARMUP:
            sim.send("B " + line)
        sim.send("N " + args.screen)
        sim.latest_frame(settle=1.5)

        for i in presets:
            sim.send("P %d" % i)
            frame = sim.latest_frame(settle=1.5)
            if frame is None:
                print("no frame for preset %d" % i, file=sys.stderr)
                continue
            name = PRESETS[i].split()[0].lower()
            path = os.path.join(args.outdir, "%s-%02d-%s.png" % (args.screen, i, name))
            write_png(path, frame)
            print(path)
    finally:
        sim.close()


if __name__ == "__main__":
    main()
