#!/usr/bin/env python3
"""tools/probe.py — thin CLI client for the live-probe daemon (probe_daemon.py).

Sends one line-delimited JSON command to the running daemon (discovered via
runs/probe/daemon.json) and prints the reply. The daemon must already be up:

    nix develop --command python3 tools/probe_daemon.py --view --rng-seed 19937 &

Examples:
    tools/probe.py status
    tools/probe.py state
    tools/probe.py shot                 # → PNG in runs/probe/session/shots/
    tools/probe.py tap a                # confirm/talk
    tools/probe.py hold up 30           # walk up 30 frames
    tools/probe.py esc                  # pause / skip
    tools/probe.py input off            # hand input back to the human
    tools/probe.py read 0x0438b1c0 i32  # scene state
    tools/probe.py anchors --clear
"""
from __future__ import annotations

import json
import socket
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CTRL = ROOT / "runs" / "probe" / "daemon.json"


def send(req, timeout=30.0):
    if not CTRL.exists():
        return {"ok": False, "err": "no daemon.json — is probe_daemon running?"}
    port = int(json.loads(CTRL.read_text())["port"])
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(("127.0.0.1", port))
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf.decode()) if buf else {"ok": False, "err": "no reply"}


def main(argv):
    if not argv:
        print(__doc__)
        return 0
    cmd = argv[0]
    a = argv[1:]
    req: dict = {"cmd": cmd}
    if cmd in ("tap", "hold"):
        req["mask"] = a[0]
        if cmd == "hold" and len(a) > 1:
            req["frames"] = int(a[1])
        if cmd == "tap" and len(a) > 1:
            req["repeat"] = int(a[1])
    elif cmd == "read":
        req["va"] = a[0]
        req["type"] = a[1] if len(a) > 1 else "i32"
    elif cmd == "poke":
        req["va"] = a[0]
        req["type"] = a[1] if len(a) > 2 else "i32"
        req["val"] = int(a[-1], 0)
    elif cmd == "callq":
        req["va"] = a[0]
        req["args"] = [int(x, 0) for x in a[1:]]
    elif cmd in ("input", "turbo", "audio"):
        val = a[0].lower() in ("on", "1", "true", "yes")
        req["active" if cmd == "input" else "on"] = val
    elif cmd == "anchors":
        req["clear"] = "--clear" in a
    elif cmd == "shot" and a:
        req["path"] = a[0]
    elif cmd == "record":
        req["action"] = a[0]
        if a[0] == "start":
            req["path"] = a[1]
    print(json.dumps(send(req), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
