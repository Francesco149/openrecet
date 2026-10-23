#!/usr/bin/env python3
"""tools/craft_pix_save.py — set one save slot's pix and restamp its checksum.

A recording that should start "rich" (buy anything from the first frame) loads a
save whose slot holds a large gold value.  Gold is bank dword 3
(SAVE_BANK_FIELD_GOLD, src/save_bank.h); the bank's additive checksum
(dword 0xb7f1 = sum of dwords [0, 0xb7f0)) must be restamped or the boot sweep
(FUN_004901c2) resets the tampered bank to the empty pattern.

Usage:
  python3 tools/craft_pix_save.py --base tests/scenarios/_saves/<sha>.sav.gz \
      [--bank N] [--gold 9999999]

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
    ap.add_argument("--gold", type=int, default=9_999_999)
    a = ap.parse_args()

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
    old = struct.unpack_from("<I", buf, base + F_GOLD * 4)[0]
    struct.pack_into("<I", buf, base + F_GOLD * 4, a.gold & 0xffffffff)
    struct.pack_into("<I", buf, base + F_CHECKSUM * 4, checksum(buf, base))

    tmp = STORE / ".craft_pix_save.tmp"
    STORE.mkdir(parents=True, exist_ok=True)
    tmp.write_bytes(bytes(buf))
    try:
        sha, blob = trace_save.store_save(str(tmp), str(STORE))
    finally:
        tmp.unlink(missing_ok=True)
    print(f"bank {bank}: gold {old} -> {a.gold}; checksum restamped")
    print(f"save {sha}  ({blob})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
