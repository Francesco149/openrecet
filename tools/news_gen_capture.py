#!/usr/bin/env python3
"""tools/news_gen_capture.py — golden-reference capture for the daily-news
generator (FUN_00436623) from LIVE retail via the probe daemon; the
roster_scan_capture.py protocol applied to the news chip.

The generator's whole mutation set lives inside the per-slot working arena
(news list, headline buffers, scroll offsets, pair TTLs), so an arena
snapshot/diff-poke restore is a complete clean-state reset.  Each sample:
restore the arena (toward the VARIANT template) → callq FUN_00436623 (the
daemon's seed_at_call hook records the EXACT engine-thread seed the instant
before the call, defeating RPC→engine rng drift) → read back the list,
headline bytes, offsets, pairs, final seed.

Three arena VARIANTS exercise every generator phase (the port replays the
same patched arena.bin, so patches need no port-side plumbing):
  natural  the arena as captured (empty list ⇒ the new-news pick path)
  expiry   day=10 + two active entries (dur 1: trend 5 + trend 'd')
           ⇒ dedup vs actives + both expiry-headline branches + day-range
  boom     day=10, rank=9, 8 sold-pairs of one item ⇒ the boom-news path
           (threshold/variant/duration rolls + pair clearing)

Pre: ANY live retail (the generator has no scene/branch gate; it reads day/
rank from the arena).  Usage:
    nix develop --command python3 tools/news_gen_capture.py \\
        --samples 4 --out-dir runs/probe/news-golden
Then the port side, per variant:
    OPENRECET_NEWS_GOLDEN=<variant>.arena.bin \\
    OPENRECET_NEWS_SEEDS=<seeds from the golden> \\
    OPENRECET_NEWS_OUT=port.json  tools/run-openrecet.sh
and diff results[] (tools/news_gen_diff.py).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from probe import send  # noqa: E402

# ── VAs (Ghidra, ImageBase 0x400000) ─────────────────────────────────────────
ARENA_VA   = 0x044e3798   # working-arena bank base (slot 0)
ARENA_LEN  = 0x2dfc8      # per-slot stride
RNG_VA     = 0x006023a0   # MSVC LCG state
GEN_VA     = 0x00436623   # FUN_00436623 (news_daily_update)
SLOT_VA    = 0x0438b1e0   # active slot index

LIST_OFF   = 0x275c8      # arena offset: news entries (20 × 0xc)
HL_CNT_OFF = 0x2a6c0      # headline count (i32)
HL_TXT_OFF = 0x2a6c4      # headline rows (0x100 each)
HL_OFF_OFF = 0x2bac4      # per-row scroll offsets (i32)
HL_TOT_OFF = 0x2bb14      # offsets total (i32)
PAIRS_OFF  = 0x2dde4      # sold-pairs (20 × {i16 id, i16 ttl})
DAY_OFF    = 0x2c3ec      # SHOP_DAY dword
RANK_OFF   = 0x2c400      # SHOP_RANK dword

LCG_MUL = 0x343fd
LCG_ADD = 0x269ec3
MASK32  = 0xffffffff


def rd(va, ty="i32"):
    r = send({"cmd": "read", "va": va, "type": ty})
    if not r.get("ok"):
        raise SystemExit(f"read {va:#x} failed: {r}")
    return r["val"]


def poke(va, val, ty="u32"):
    r = send({"cmd": "poke", "va": va, "type": ty, "val": val})
    if not r.get("ok"):
        raise SystemExit(f"poke {va:#x} failed: {r}")


def readmem(va, ln):
    r = send({"cmd": "readmem", "va": va, "len": ln}, timeout=60.0)
    if not r.get("ok"):
        raise SystemExit(f"readmem {va:#x} failed: {r}")
    return r["hex"]


def restore_arena(target_bytes):
    """Diff-poke the live arena toward `target_bytes` (race-safe single pokes,
    same rationale as roster_scan_capture.restore_arena)."""
    cur = bytes.fromhex(readmem(ARENA_VA, ARENA_LEN))
    restored = []
    for off in range(0, len(target_bytes) - 3, 4):
        c = target_bytes[off:off + 4]
        if cur[off:off + 4] != c:
            poke(ARENA_VA + off, int.from_bytes(c, "little"), "u32")
            restored.append(off)
    return restored


def callq(va):
    r = send({"cmd": "callq", "va": va, "args": [], "argt": [], "ret": "void"})
    if not r.get("ok"):
        raise SystemExit(f"callq {va:#x} failed: {r}")
    return r


def rng_draws(seed_before, seed_after, cap=1_000_000):
    s = seed_before & MASK32
    tgt = seed_after & MASK32
    for n in range(cap + 1):
        if s == tgt:
            return n
        s = (s * LCG_MUL + LCG_ADD) & MASK32
    return None


def read_outputs():
    arena = bytes.fromhex(readmem(ARENA_VA, ARENA_LEN))
    entries = []
    for i in range(20):
        e = arena[LIST_OFF + i * 0xc: LIST_OFF + (i + 1) * 0xc]
        tgt, nid = struct.unpack_from("<ii", e, 0)
        trend = struct.unpack_from("<b", e, 8)[0]
        dur = struct.unpack_from("<b", e, 9)[0]
        entries.append({"target": tgt, "id": nid, "trend": trend, "dur": dur})
    hl = struct.unpack_from("<i", arena, HL_CNT_OFF)[0]
    heads, offs = [], []
    for i in range(max(0, min(hl, 24))):
        row = arena[HL_TXT_OFF + i * 0x100: HL_TXT_OFF + (i + 1) * 0x100]
        heads.append(row.split(b"\0")[0].hex())
        offs.append(struct.unpack_from("<i", arena, HL_OFF_OFF + i * 4)[0])
    total = struct.unpack_from("<i", arena, HL_TOT_OFF)[0]
    pairs = list(struct.unpack_from("<40h", arena, PAIRS_OFF))
    return {"list": entries, "hl_count": hl, "headlines": heads,
            "offsets": offs, "offsets_total": total, "pairs": pairs}


def capture_sample(template):
    restore_arena(template)
    r = callq(GEN_VA)
    seed = r.get("seed_at_call")
    # seed_after_call is read on the ENGINE thread the instant the call
    # returns — a client-side rd(RNG_VA) here would race the resumed sim
    # and over-count draws (the first gate run showed exactly that: outputs
    # matched on every seed, final_seed only on the samples where no sim
    # draw landed in the race window).
    after = r.get("seed_after_call")
    if after is None:
        raise SystemExit("daemon has no seed_after_call — relaunch to load "
                         "the updated agent")
    out = read_outputs()
    out["seed"] = seed & MASK32
    out["final_seed"] = after & MASK32
    out["rng_draws"] = rng_draws(seed, after)
    return out


def patch(template: bytes, off: int, data: bytes) -> bytes:
    return template[:off] + data + template[off + len(data):]


def build_variants(clean: bytes) -> dict[str, bytes]:
    v = {"natural": clean}

    # expiry: day 10; entry0 {target -1, id 1, trend 5, dur 1};
    #         entry1 {target -1, id 2, trend 'd', dur 1}
    t = patch(clean, DAY_OFF, struct.pack("<i", 10))
    t = patch(t, LIST_OFF + 0x0, struct.pack("<iiBB", -1, 1, 5, 1))
    t = patch(t, LIST_OFF + 0xc, struct.pack("<iiBB", -1, 2, 0x64, 1))
    v["expiry"] = t

    # boom: day 10, rank 9, 8 pairs of item id 12 (ttl 3)
    t = patch(clean, DAY_OFF, struct.pack("<i", 10))
    t = patch(t, RANK_OFF, struct.pack("<i", 9))
    t = patch(t, PAIRS_OFF, struct.pack("<16h", *([12, 3] * 8)))
    v["boom"] = t
    return v


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=4,
                    help="samples per variant (each records the exact drifted "
                         "seed via the engine-thread seed_at_call hook)")
    ap.add_argument("--out-dir", default="runs/probe/news-golden")
    ap.add_argument("--variants", default="natural,expiry,boom")
    args = ap.parse_args(argv)

    slot = rd(SLOT_VA, "i32")
    if slot != 0:
        print(f"WARN: slot={slot} (ARENA_VA assumes slot 0)", file=sys.stderr)

    clean = bytes.fromhex(readmem(ARENA_VA, ARENA_LEN))
    outdir = Path(args.out_dir)
    outdir.mkdir(parents=True, exist_ok=True)

    for name, template in build_variants(clean).items():
        if name not in args.variants.split(","):
            continue
        sha16 = hashlib.sha256(template).hexdigest()[:16]
        print(f"[{name}] arena sha16={sha16}; sampling {args.samples}")
        results = [capture_sample(template) for _ in range(args.samples)]
        bin_path = outdir / f"{name}.arena.bin"
        bin_path.write_bytes(template)
        fixture = {"function": "FUN_00436623", "variant": name, "slot": slot,
                   "arena_sha16": sha16, "arena_bin": bin_path.name,
                   "results": results}
        (outdir / f"{name}.json").write_text(json.dumps(fixture, indent=2))
        print(f"[{name}] seeds: {[r['seed'] for r in results]} "
              f"draws: {[r['rng_draws'] for r in results]}")

    restore_arena(clean)   # leave the live game clean
    print(f"wrote {outdir}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
