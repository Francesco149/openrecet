#!/usr/bin/env python3
"""tools/craft_pix_save.py — set one save slot's pix and restamp its checksum.

A recording that should start "rich" (buy anything from the first frame) loads a
save whose slot holds a large gold value.  Gold is bank dword 3
(SAVE_BANK_FIELD_GOLD, src/save_bank.h); the bank's additive checksum
(dword 0xb7f1 = sum of dwords [0, 0xb7f0)) must be restamped or the boot sweep
(FUN_004901c2) resets the tampered bank to the empty pattern.

Usage:
  python3 tools/craft_pix_save.py --base tests/scenarios/_saves/<sha>.sav.gz \
      [--bank N] [--gold 9999999] [--set DWORD=VALUE ..] [--add-item ID[+N] ..]

--set patches any bank dword (index and value: decimal or 0x hex; layout in
tools/save_arena_map.json), e.g. `--set 0xb100=9` = merchant level 10 on the HUD
(the 0-based level DAT_0450fb98) — a precondition for an `rd` probe task whose
menu entry the natural saves do not have yet. With --set, gold changes only if
--gold is given. A crafted state is not one the game produced: say so in the
task, and prefer a played save when one exists.

--bank defaults to the header's last-saved slot (dword 7, the one "Load the
newest slot" picks).  The result is stored content-addressed in
tests/scenarios/_saves/ (user data, gitignored); prints its sha for
`rhost.py record NAME --save SHA`.
"""
from __future__ import annotations

import argparse
import gzip
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import trace_save  # noqa: E402

HEADER_BYTES = 0x0b10
STRIDE_BYTES = 0x2dfc8
MAGIC = 0x341944da
F_MAGIC, F_GOLD, F_CHECKSUM = 1, 3, 0xb7f1
H_LAST_SLOT = 7
STORE = Path(__file__).resolve().parent.parent / "tests" / "scenarios" / "_saves"


def checksum(buf: bytes, base: int) -> int:
    return sum(struct.unpack_from(f"<{F_CHECKSUM - 1}I", buf, base)) & 0xffffffff


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="a save.dat or <sha>.sav.gz")
    ap.add_argument("--bank", type=int, help="slot to patch (default: the header's last-saved slot)")
    ap.add_argument("--gold", type=int)
    ap.add_argument("--set", action="append", default=[], metavar="DWORD=VALUE", help="patch a bank dword (repeatable)")
    ap.add_argument("--add-item", action="append", default=[], metavar="ID[+N]",
                    help="put an item into the first empty bag slot (bank dwords 6..20005 hold id<<6 | plus; repeatable)")
    a = ap.parse_args()
    if a.gold is None and not a.set and not a.add_item:
        a.gold = 9_999_999

    raw = Path(a.base).read_bytes()
    buf = bytearray(gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw)
    if len(buf) != trace_save.SAVE_ARENA_BYTES:
        raise SystemExit(f"{a.base}: {len(buf)} bytes, not a save arena")
    bank = a.bank if a.bank is not None else struct.unpack_from("<I", buf, H_LAST_SLOT * 4)[0]
    base = HEADER_BYTES + bank * STRIDE_BYTES
    if struct.unpack_from("<I", buf, base + F_MAGIC * 4)[0] != MAGIC:
        raise SystemExit(f"bank {bank}: no save there (magic)")
    if checksum(buf, base) != struct.unpack_from("<I", buf, base + F_CHECKSUM * 4)[0]:
        raise SystemExit(f"bank {bank}: stale checksum — not a committed slot")
    notes = []
    if a.gold is not None:
        old = struct.unpack_from("<I", buf, base + F_GOLD * 4)[0]
        struct.pack_into("<I", buf, base + F_GOLD * 4, a.gold & 0xffffffff)
        notes.append(f"gold {old} -> {a.gold}")
    for kv in a.set:
        d, v = (int(x, 0) for x in kv.split("="))
        if not 0 <= d < F_CHECKSUM:
            raise SystemExit(f"--set {kv}: dword index outside the bank's content (0..{F_CHECKSUM - 1:#x})")
        old = struct.unpack_from("<I", buf, base + d * 4)[0]
        struct.pack_into("<I", buf, base + d * 4, v & 0xffffffff)
        notes.append(f"dword {d:#x}: {old} -> {v}")
    for it in a.add_item:
        iid, _, plus = it.partition("+")
        val = (int(iid, 0) << 6) | (int(plus or "0", 0) & 0x3f)
        for d in range(6, 6 + 20000):
            if struct.unpack_from("<I", buf, base + d * 4)[0] == 0xffffffff:
                struct.pack_into("<I", buf, base + d * 4, val)
                notes.append(f"item {iid}{'+' + plus if plus else ''} -> bag slot {d - 6}")
                break
        else:
            raise SystemExit("--add-item: the bag is full")
    struct.pack_into("<I", buf, base + F_CHECKSUM * 4, checksum(buf, base))

    tmp = STORE / ".craft_pix_save.tmp"
    STORE.mkdir(parents=True, exist_ok=True)
    tmp.write_bytes(bytes(buf))
    try:
        sha, blob = trace_save.store_save(str(tmp), str(STORE))
    finally:
        tmp.unlink(missing_ok=True)
    print(f"bank {bank}: {'; '.join(notes)}; checksum restamped")
    print(f"save {sha}  ({blob})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
