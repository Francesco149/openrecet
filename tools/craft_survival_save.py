#!/usr/bin/env python3
"""tools/craft_survival_save.py — craft a save that UNLOCKS the title Survival row.

The title menu shows "Survival" (item code 6) only when FUN_0049a324 returns
``uVar1 == 3`` — i.e. SOME save bank satisfies BOTH:

  bit 1:  bank[2]      (OCCUPIED / play-time) > 0   AND
          bank[0xb759] (GAME_MODE)            == 3
  bit 2:  one of bank[6 + j] for j in [0, bank[0xaec6]=ITEM_COUNT)
          has  (entry >> 6) in [0xd49 .. 0xd50]   (an "adventure-8" item id)

(The decompile labels these "Adventure 2 cleared" / "Adventure 8 item"; in the
port's bank layout — src/save_bank.h — they are the GAME_MODE tag == 3 and an
item-table entry whose id is in [0xd49..0xd50]. See findings/title-survival-RE.md.)

This poke-and-restamp recipe takes an existing populated save, sets those fields
on one bank, recomputes that bank's additive checksum (else save_bank_init_all
RESETS the tampered bank on load), and stores the result as a content-addressed
``<sha256>.sav.gz`` blob ready to embed in a scenario trace via {savefile}.

Usage:
  nix develop --command python3 tools/craft_survival_save.py \
      --base tests/scenarios/guild-buyout/_saves/<hash>.sav.gz \
      --out-dir tests/scenarios/title-survival/_saves \
      [--bank 1]

Prints the {savefile} relpath to embed (relative to the scenario dir).
"""
from __future__ import annotations

import argparse
import gzip
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import trace_save  # noqa: E402  (store_save helper)

# ── Bank layout (mirrors src/save_bank.h) ──
HEADER_BYTES = 0x0b10
STRIDE_BYTES = 0x2dfc8
MAGIC        = 0x341944da
F_MAGIC      = 1
F_OCCUPIED   = 2
F_ITEM_TABLE = 6
F_ITEM_COUNT = 0xaec6
F_GAME_MODE  = 0xb759
F_CHECKSUM   = 0xb7f1

# An "adventure-8" item: any value with (value >> 6) in [0xd49..0xd50].
ADV8_ITEM = 0xd49 << 6   # 0x35240 → (>>6) == 0xd49, the low end of the window


def bank_off(bank_idx: int) -> int:
    return HEADER_BYTES + bank_idx * STRIDE_BYTES


def rd(buf: bytes, byte_off: int) -> int:
    return struct.unpack_from("<I", buf, byte_off)[0]


def wr(buf: bytearray, byte_off: int, val: int) -> None:
    struct.pack_into("<I", buf, byte_off, val & 0xffffffff)


def dword(buf, bank_base: int, dword_idx: int) -> int:
    return rd(buf, bank_base + dword_idx * 4)


def set_dword(buf, bank_base: int, dword_idx: int, val: int) -> None:
    wr(buf, bank_base + dword_idx * 4, val)


def restamp_checksum(buf: bytearray, bank_base: int) -> None:
    s = 0
    for i in range(F_CHECKSUM - 1):       # indices [0, 0xb7f0)
        s = (s + rd(buf, bank_base + i * 4)) & 0xffffffff
    set_dword(buf, bank_base, F_CHECKSUM, s)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True, help="base .sav.gz (a populated save)")
    ap.add_argument("--out-dir", required=True, help="content store dir for the crafted blob")
    ap.add_argument("--bank", type=int, default=None,
                    help="bank to unlock on (default: first with OCCUPIED>0 and ITEM_COUNT>0)")
    args = ap.parse_args()

    base = Path(args.base)
    if base.suffix == ".gz":
        with gzip.open(base, "rb") as f:
            buf = bytearray(f.read())
    else:
        buf = bytearray(base.read_bytes())
    assert len(buf) == trace_save.SAVE_ARENA_BYTES, f"unexpected save size {len(buf)}"

    # Pick a bank.
    candidates = range(args.bank, args.bank + 1) if args.bank is not None else range(100)
    chosen = None
    for b in candidates:
        bb = bank_off(b)
        if dword(buf, bb, F_MAGIC) != MAGIC:
            continue
        if dword(buf, bb, F_OCCUPIED) > 0 and dword(buf, bb, F_ITEM_COUNT) > 0:
            chosen = b
            break
    if chosen is None:
        print("ERROR: no suitable bank (need magic + OCCUPIED>0 + ITEM_COUNT>0)", file=sys.stderr)
        return 1

    bb = bank_off(chosen)
    print(f"crafting on bank {chosen}: occupied={dword(buf, bb, F_OCCUPIED)} "
          f"item_count={dword(buf, bb, F_ITEM_COUNT)}")

    set_dword(buf, bb, F_GAME_MODE, 3)               # bit 1 (GAME_MODE == 3)
    set_dword(buf, bb, F_ITEM_TABLE + 0, ADV8_ITEM)  # bit 2 (adv-8 item in [0,count))
    restamp_checksum(buf, bb)

    # Verify the unlock locally (mirror FUN_0049a324's two-bit test).
    uv = 0
    if dword(buf, bb, F_OCCUPIED) > 0 and dword(buf, bb, F_GAME_MODE) == 3:
        uv |= 1
        for j in range(min(dword(buf, bb, F_ITEM_COUNT), 20000)):
            it = dword(buf, bb, F_ITEM_TABLE + j)
            if 0xd49 <= (it >> 6) <= 0xd50:
                uv |= 2
                break
    assert uv == 3, f"unlock check failed: uVar1={uv}"
    assert restamp_ok(buf, bb), "checksum re-verify failed"
    print(f"unlock OK: uVar1=={uv}, checksum re-verified")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(suffix=".save.bin", delete=False) as tf:
        tf.write(bytes(buf))
        tmp_raw = tf.name
    sha, blob = trace_save.store_save(tmp_raw, out_dir)
    Path(tmp_raw).unlink(missing_ok=True)
    print(f"stored: {blob.name}")
    print(f"embed:  {{\"savefile\": \"_saves/{blob.name}\"}}")
    return 0


def restamp_ok(buf, bank_base) -> bool:
    s = 0
    for i in range(F_CHECKSUM - 1):
        s = (s + rd(buf, bank_base + i * 4)) & 0xffffffff
    return s == dword(buf, bank_base, F_CHECKSUM)


if __name__ == "__main__":
    raise SystemExit(main())
