#!/usr/bin/env python3
"""Scripted interaction driver for the sim.

shot.py applies taps before drags, so it cannot reach a control that has to be
scrolled into view first. This takes an ordered script instead, one step per
line, and writes a PNG after every step so a sequence can be read back frame
by frame.

  python3 drive.py out/ --skin 1 \
      "screen:settings" "drag:400,360,400,80" "shot:before" \
      "tap:81,381" "shot:after"

Steps:
  screen:<name>          switch screen
  cmd:<raw>              raw sim command, e.g. "cmd:G drawer 1"
  tap:<x>,<y>            press and release
  drag:<x1>,<y1>,<x2>,<y2>
  wait:<seconds>
  shot:<name>            write <out>/<name>.png
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import Sim, write_png, WARMUP


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("steps", nargs="+")
    ap.add_argument("--binary", default="./gtsim")
    ap.add_argument("--online", action="store_true")
    ap.add_argument("--skin", type=int, default=None, help="0 classic, 1 glass")
    ap.add_argument("--preset", type=int, default=0)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    sim = Sim(args.binary, online=args.online)
    try:
        sim.latest_frame(settle=5.5)          # let the boot screen finish
        for line in WARMUP:
            sim.send("B " + line)
        if args.skin is not None:
            sim.send("K %d" % args.skin)
            sim.latest_frame(settle=1.2)
        sim.send("P %d" % args.preset)
        sim.latest_frame(settle=1.0)

        for step in args.steps:
            kind, _, val = step.partition(":")
            if kind == "screen":
                sim.send("N " + val)
                sim.latest_frame(settle=1.5)
            elif kind == "cmd":
                sim.send(val)
                sim.latest_frame(settle=0.8)
            elif kind == "tap":
                x, y = val.split(",")
                sim.send("T %s %s 1" % (x, y))
                sim.latest_frame(settle=0.2)
                sim.send("T %s %s 0" % (x, y))
                sim.latest_frame(settle=0.8)
            elif kind == "drag":
                x1, y1, x2, y2 = [int(v) for v in val.split(",")]
                sim.send("T %d %d 1" % (x1, y1))
                sim.latest_frame(settle=0.15)
                for i in range(1, 13):
                    sim.send("T %d %d 1" % (x1 + (x2 - x1) * i // 12,
                                            y1 + (y2 - y1) * i // 12))
                    sim.latest_frame(settle=0.05)
                sim.send("T %d %d 0" % (x2, y2))
                sim.latest_frame(settle=0.8)
            elif kind == "wait":
                sim.latest_frame(settle=float(val))
            elif kind == "log":
                # Drain stderr so a MEASURE line can be read back.
                sim.latest_frame(settle=float(val) if val else 1.0)
            elif kind == "shot":
                frame = sim.fresh_frame()
                if frame is None:
                    print("  %-24s no frame" % val)
                    continue
                path = os.path.join(args.outdir, val + ".png")
                write_png(path, frame)
                print("  wrote %s" % path)
            else:
                raise SystemExit("unknown step: %s" % step)
    finally:
        sim.close()


if __name__ == "__main__":
    main()
