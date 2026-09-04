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
import socket
import struct
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
    "text", "text dim", "on accent", "border", "nav", "icon",
]
SCREENS = ["home", "night", "block", "clock", "price", "mempool", "wifi", "settings",
           "odds", "payout"]


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
        # ThreadingHTTPServer, the live mirror and the seed task can all write
        # at once. Keep complete command batches together on the sim's stdin.
        self.input_lock = threading.Lock()
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
        # bytearray avoids copying the entire partial frame on every 64 KiB
        # pipe read. Only the latest complete framebuffer is retained.
        buf = bytearray()
        out = self.proc.stdout
        while True:
            chunk = out.read(65536)
            if not chunk:
                return
            buf.extend(chunk)
            while len(buf) >= FRAME:
                if buf[:4] != b"GTFB":
                    idx = buf.find(b"GTFB", 1)
                    if idx > 0:
                        del buf[:idx]
                    else:
                        # Preserve a possible split magic prefix.
                        del buf[:-3]
                    continue
                frame_no, width, height = struct.unpack_from("<IHH", buf, 4)
                if width != W or height != H:
                    # A coincidental magic string in corrupt output is not a
                    # frame boundary; advance and resume the normal resync.
                    del buf[:4]
                    continue
                frame = bytes(buf[HEADER:FRAME])
                del buf[:FRAME]
                with self.cv:
                    self.frame = frame
                    self.frame_no = frame_no
                    self.cv.notify_all()

    def _read_log(self):
        for raw in iter(self.proc.stderr.readline, b""):
            line = raw.decode("utf-8", "replace").rstrip()
            with self.lock:
                self.log.append(line)
                del self.log[:-300]

    def send(self, line):
        return self.send_many((line,))

    def send_many(self, lines):
        payload = "".join(line + "\n" for line in lines if line)
        if not payload:
            return True
        try:
            with self.input_lock:
                self.proc.stdin.write(payload.encode())
                self.proc.stdin.flush()
            return True
        except (BrokenPipeError, OSError, ValueError):
            return False

    def wait_frame(self, since, timeout=8.0):
        with self.cv:
            self.cv.wait_for(
                lambda: self.frame_no > since or self.proc.poll() is not None,
                timeout)
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
                self.sim.send_many("B " + s for s in sentences)
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
        try:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            if body:
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            # Normal when a tab closes or abandons a superseded long poll.
            pass

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
            lines = [line.strip() for line in body.splitlines() if line.strip()]
            self.server.sim.send_many(lines)
            return self._send(200, b"ok", "text/plain")

        if self.path == "/demo":
            self.server.sim.send_many("B " + s for s in DEMO)
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


class SimServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class SimServer6(SimServer):
    address_family = socket.AF_INET6

    def server_bind(self):
        # Linux defaults bindv6only=0, so a :: socket would also claim IPv4 and
        # collide with the v4 socket we bind alongside it. Pin this one to v6.
        try:
            self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        except (AttributeError, OSError):
            pass
        return super().server_bind()


def serve(bind, port):
    """Return every listening socket the browser might reach us on.

    Windows browsers resolve "localhost" to ::1 before 127.0.0.1, and WSL2's
    port relay treats the two families separately: a v4-only bind refuses ::1,
    and a v6 bind with V6ONLY off still does not answer 127.0.0.1. Binding
    both with IPv4 first silently binds only IPv4, because the later :: socket
    also claims IPv4 and collides. So bind one socket per family, v6 pinned to
    v6, each on its own thread, and fall back to IPv4 alone if v6 is
    unavailable. curl picks IPv4, which is why an IPv4-only bind looks healthy
    from the shell while the browser cannot connect at all.
    """
    servers = []
    if bind in ("", "0.0.0.0", "::"):
        for cls, addr in ((SimServer, ("0.0.0.0", port)), (SimServer6, ("::", port))):
            try:
                servers.append(cls(addr, Handler))
            except OSError as e:
                print("note: could not bind %s: %s" % (addr[0], e), flush=True)
    else:
        servers.append(SimServer((bind, port), Handler))

    if not servers:
        raise SystemExit("could not bind port %d" % port)
    return servers


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8010)
    ap.add_argument("--bind", default="127.0.0.1",
                    help="listen address (default: loopback only; use 0.0.0.0 deliberately for LAN access)")
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
            sim.send_many("B " + s for s in DEMO)
    threading.Thread(target=seed, daemon=True).start()

    servers = serve(args.bind, args.port)
    for srv in servers:
        srv.sim = sim
        srv.mirror = mirror
    for srv in servers[1:]:
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    httpd = servers[0]
    # Flushed: when stdout is a pipe the banner otherwise sits in a buffer,
    # which is exactly what hid a silent bind failure once.
    print("Turbo Touch sim on http://localhost:%d and http://127.0.0.1:%d  (%dx%d), %d listener(s)"
          % (args.port, args.port, W, H, len(servers)), flush=True)
    if args.live:
        print("mirroring %s over BAP" % args.live, flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        sim.send("Q")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
