#!/usr/bin/env python3
"""Turbo Touch screen simulator -- browser front end on :8010.

Runs ./gtsim (the real UI compiled for the host), streams its 800x480 RGB565
framebuffer to the browser, and pipes clicks back as touch events.

  python3 server.py                       # sim only
  python3 server.py --live 192.168.1.50  # also mirror a real Bitaxe over BAP

The Bitaxe's HTTP API is polled and translated into the same BAP sentences the
Turbo Touch would receive over UART, so the screen shows real numbers without
anything being flashed to hardware.
"""
import argparse
import json
import os
import select
import subprocess
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

W, H = 800, 480
HEADER = 12
FRAME = HEADER + W * H * 2
HERE = os.path.dirname(os.path.abspath(__file__))

PRESETS = [
    "Bitaxe Red", "Bitcoin Orange", "Matrix Green", "Cyber Cyan", "Deep Violet",
    "Nord", "Gruvbox", "Paper (light)", "Mono",
]
SLOTS = [
    "background", "card", "accent", "red",
    "text", "text dim", "on accent", "border", "nav",
]
SCREENS = ["home", "night", "block", "clock", "price", "mempool", "wifi", "settings"]


# ---------------------------------------------------------------- BAP encoding

def bap(param, value):
    """Build a BAP RES sentence with a valid XOR checksum.

    The checksum covers everything between '$' and '*', matching
    bap_calculate_checksum() in main/bap_protocol.c.
    """
    body = "BAP,RES,%s,%s" % (param, value)
    ck = 0
    for c in body.encode():
        ck ^= c
    return "$%s*%02X" % (body, ck)


def axeos_to_bap(info):
    """Map one AxeOS /api/system/info payload onto BAP sentences."""
    def g(key, default=None):
        return info.get(key, default)

    out = []
    if g("hashRate") is not None:
        out.append(bap("hashrate", "%.1f" % g("hashRate")))
    if g("temp") is not None:
        out.append(bap("chipTemp", "%.1f" % g("temp")))
    if g("power") is not None:
        out.append(bap("power", "%.1f" % g("power")))
    if g("voltage") is not None:
        out.append(bap("voltage", "%.2f" % (g("voltage") / 1000.0)))
    if g("fanrpm") is not None:
        out.append(bap("fan_speed", str(g("fanrpm"))))
    if g("sharesAccepted") is not None:
        out.append(bap("shares", "%s/%s" % (g("sharesAccepted", 0), g("sharesRejected", 0))))
    if g("bestDiff") is not None:
        out.append(bap("best_difficulty", str(g("bestDiff"))))
    if g("blockHeight") is not None:
        out.append(bap("block_height", str(g("blockHeight"))))
    if g("ASICModel"):
        out.append(bap("asicModel", str(g("ASICModel"))))
    if g("boardVersion"):
        out.append(bap("deviceModel", "Gamma Turbo" if str(g("boardVersion")) == "801" else str(g("boardVersion"))))
    if g("stratumURL"):
        out.append(bap("pool", str(g("stratumURL"))))
    if g("stratumPort") is not None:
        out.append(bap("poolPort", str(g("stratumPort"))))
    if g("stratumUser"):
        out.append(bap("poolUser", str(g("stratumUser"))))
    if g("ssid"):
        out.append(bap("wifi_ssid", str(g("ssid"))))
    if g("wifiRSSI") is not None:
        out.append(bap("wifi_rssi", str(g("wifiRSSI"))))
    if g("ipv4"):
        out.append(bap("wifi_ip", str(g("ipv4"))))
    return out


# A believable steady state for offline work.
DEMO = [
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
    bap("wifi_ssid", "bitaxe-lab"),
    bap("wifi_rssi", "-58"),
    bap("wifi_ip", "192.168.1.50"),
]


# ------------------------------------------------------------------- sim driver

class Sim:
    def __init__(self, binary):
        self.binary = binary
        self.frame = None
        self.frame_no = 0
        self.cv = threading.Condition()
        self.log = []
        self.lock = threading.Lock()
        self.proc = None
        self.start()

    def start(self):
        env = dict(os.environ)
        env.setdefault("SIM_OFFLINE", "1")   # screens must not stall on the internet
        self.proc = subprocess.Popen(
            [self.binary], cwd=HERE, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        threading.Thread(target=self._read_frames, daemon=True).start()
        threading.Thread(target=self._read_log, daemon=True).start()

    def _read_frames(self):
        buf = b""
        out = self.proc.stdout
        while True:
            chunk = out.read(65536)
            if not chunk:
                return
            buf += chunk
            while len(buf) >= FRAME:
                if buf[:4] != b"GTFB":
                    idx = buf.find(b"GTFB", 1)
                    buf = buf[idx:] if idx > 0 else b""
                    continue
                with self.cv:
                    self.frame = buf[HEADER:FRAME]
                    self.frame_no += 1
                    self.cv.notify_all()
                buf = buf[FRAME:]

    def _read_log(self):
        for raw in iter(self.proc.stderr.readline, b""):
            line = raw.decode("utf-8", "replace").rstrip()
            with self.lock:
                self.log.append(line)
                del self.log[:-300]

    def send(self, line):
        try:
            self.proc.stdin.write((line + "\n").encode())
            self.proc.stdin.flush()
            return True
        except Exception:
            return False

    def wait_frame(self, since, timeout=8.0):
        with self.cv:
            if self.frame_no <= since:
                self.cv.wait(timeout)
            return self.frame_no, self.frame


# ------------------------------------------------------------------ live mirror

class LiveMirror(threading.Thread):
    """Polls a real Bitaxe and replays it into the sim as BAP traffic."""

    def __init__(self, sim, host, interval=3.0):
        super().__init__(daemon=True)
        self.sim = sim
        self.host = host
        self.interval = interval
        self.enabled = bool(host)
        self.status = "off"

    def run(self):
        while True:
            if not self.enabled or not self.host:
                self.status = "off"
                time.sleep(1.0)
                continue
            try:
                url = "http://%s/api/system/info" % self.host
                with urllib.request.urlopen(url, timeout=6) as r:
                    info = json.loads(r.read().decode("utf-8", "replace"))
                sentences = axeos_to_bap(info)
                for s in sentences:
                    self.sim.send("B " + s)
                self.status = "%s ok (%d fields)" % (self.host, len(sentences))
            except Exception as e:
                self.status = "%s error: %s" % (self.host, e)
            time.sleep(self.interval)


# ----------------------------------------------------------------- http server

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "gtsim"

    def log_message(self, *a):
        pass

    def _send(self, code, body=b"", ctype="application/octet-stream", extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        path, _, query = self.path.partition("?")
        q = dict(p.split("=", 1) for p in query.split("&") if "=" in p)

        if path in ("/", "/index.html"):
            with open(os.path.join(HERE, "web", "index.html"), "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")

        if path == "/frame":
            since = int(q.get("since", "0"))
            no, frame = self.server.sim.wait_frame(since)
            if frame is None or no <= since:
                return self._send(204)
            return self._send(200, frame, "application/octet-stream",
                              {"X-Frame": str(no), "X-Width": str(W), "X-Height": str(H)})

        if path == "/state":
            sim = self.server.sim
            with sim.lock:
                log = sim.log[-60:]
            body = json.dumps({
                "frame": sim.frame_no,
                "presets": PRESETS,
                "slots": SLOTS,
                "screens": SCREENS,
                "live": self.server.mirror.status,
                "liveHost": self.server.mirror.host or "",
                "liveOn": self.server.mirror.enabled,
                "log": log,
            }).encode()
            return self._send(200, body, "application/json")

        return self._send(404, b"not found", "text/plain")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n).decode("utf-8", "replace")

        if self.path == "/cmd":
            for line in body.splitlines():
                line = line.strip()
                if line:
                    self.server.sim.send(line)
            return self._send(200, b"ok", "text/plain")

        if self.path == "/demo":
            for s in DEMO:
                self.server.sim.send("B " + s)
            return self._send(200, b"ok", "text/plain")

        if self.path == "/live":
            cfg = json.loads(body or "{}")
            m = self.server.mirror
            if "host" in cfg:
                m.host = cfg["host"].strip()
            if "on" in cfg:
                m.enabled = bool(cfg["on"])
            return self._send(200, b"ok", "text/plain")

        return self._send(404, b"not found", "text/plain")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8010)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--binary", default=os.path.join(HERE, "gtsim"))
    ap.add_argument("--live", default="", help="Bitaxe host/IP to mirror over BAP")
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        print("missing %s -- run 'make' first" % args.binary)
        return 1

    sim = Sim(args.binary)
    mirror = LiveMirror(sim, args.live)
    mirror.start()

    # Give the boot animation a head start, then seed plausible values so the
    # screens are not all "loading..." when the browser first connects.
    def seed():
        time.sleep(6)
        if not mirror.enabled:
            for s in DEMO:
                sim.send("B " + s)
    threading.Thread(target=seed, daemon=True).start()

    httpd = ThreadingHTTPServer((args.bind, args.port), Handler)
    httpd.sim = sim
    httpd.mirror = mirror
    print("Turbo Touch sim on http://localhost:%d  (%dx%d)" % (args.port, W, H))
    if args.live:
        print("mirroring %s over BAP" % args.live)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        sim.send("Q")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
