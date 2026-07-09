#!/usr/bin/env python3
"""tools/haggle_probe.py — live monitor/poker for the customer-service haggle.

Reads the haggle state VAs (from src/customer_service.c) each poll and prints a
compact row, so we can watch how customer behavior evolves across haggle rounds
while driving/poking the live game via the probe daemon. Usage:

    tools/haggle_probe.py watch [N]        # print state N times (default 1)
    tools/haggle_probe.py loop [secs]      # stream until cc08 leaves 4
    tools/haggle_probe.py set <name> <v>   # poke a named haggle var
    tools/haggle_probe.py dump             # one full JSON dump

Names → retail VAs (customer_service.c). Extend as the RE deepens.
"""
from __future__ import annotations
import json, socket, sys, time
from pathlib import Path

CTRL = Path(__file__).resolve().parent.parent / "runs" / "probe" / "daemon.json"

# name -> (va, type). From src/customer_service.c static map + scalars.
VARS = {
    "cc08":       (0x0438cc08, "i32"),  # 4 = customer-service/haggle
    "patience":   (0x0730b590, "i32"),  # s_b590
    "offer":      (0x0730b574, "i32"),  # s_b574 customer's current offer
    "work_price": (0x0730b57c, "i32"),  # s_b57c working price (seed = base)
    "round":      (0x0730b584, "i32"),  # s_b584 haggle round (0 = first)
    "accept_ref": (0x0730b588, "i32"),  # s_b588 accept-test reference
    "item_hnd":   (0x0730b5a4, "i32"),  # s_b5a4 offered-item handle (id = >>6)
    "in_delay":   (0x0730b58c, "i32"),  # s_b58c per-round input delay
    "kind_path":  (0x0730b5a8, "i32"),  # b5a8 (2 = sell path, 3 = buy menu)
    "cust_idx":   (0x0730b56c, "i32"),  # b56c active customer / kyaku index
    "prog_ctr":   (0x0730b604, "i32"),  # s_b604 script program counter
    "leave_ph":   (0x0730b520, "i32"),  # s_b520 leave/dissolve phase
    "ask":        (0x005c6bb8, "i32"),  # s_price_ask player's asking price
    "base":       (0x005c6bc0, "i32"),  # s_price_base base/reference price
    "count":      (0x005c6bc4, "i32"),  # s_price_bc4 item count
    "prev_ask":   (0x005c6bb4, "i32"),  # s_price_bb4 committed/prev ask (-1 none)
    "rng":        (0x006023a0, "u32"),
}
COLS = ["cc08", "cust_idx", "kind_path", "round", "patience", "base",
        "work_price", "ask", "offer", "accept_ref", "prog_ctr", "leave_ph", "rng"]


def send(req, timeout=30.0):
    port = int(json.loads(CTRL.read_text())["port"])
    s = socket.socket(); s.settimeout(timeout); s.connect(("127.0.0.1", port))
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        c = s.recv(65536)
        if not c: break
        buf += c
    return json.loads(buf.decode()) if buf else {}


def readall():
    specs = [{"name": n, "va": va, "type": t} for n, (va, t) in VARS.items()]
    return send({"cmd": "reads", "specs": specs})["vals"]


def row(v):
    return " ".join(f"{c}={v.get(c)}" for c in COLS)


def main(argv):
    cmd = argv[0] if argv else "watch"
    if cmd == "watch":
        n = int(argv[1]) if len(argv) > 1 else 1
        for _ in range(n):
            print(row(readall())); time.sleep(0.3)
    elif cmd == "loop":
        secs = float(argv[1]) if len(argv) > 1 else 30
        t = time.time(); prev = None
        while time.time() - t < secs:
            v = readall()
            key = tuple(v.get(c) for c in COLS)
            if key != prev:
                print(f"[{time.time()-t:5.1f}] " + row(v)); prev = key
            if v.get("cc08") != 4:
                print("cc08 left 4 — haggle ended"); break
            time.sleep(0.1)
    elif cmd == "set":
        name, val = argv[1], int(argv[2], 0)
        va, ty = VARS[name]
        print(send({"cmd": "poke", "va": va, "type": ty, "val": val}))
    elif cmd == "dump":
        print(json.dumps(readall(), indent=2))
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
