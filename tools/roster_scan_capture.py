#!/usr/bin/env python3
"""tools/roster_scan_capture.py — golden-reference capture for the customer
roster-scan (FUN_0045edaa) from LIVE retail via the probe daemon.

The scan is NON-idempotent (it mutates per-slot working-arena state: story-flag
latches + the DAT_045109a8 affinity array), so a clean seed sweep must restore
the arena between calls.  ALL of the scan's mutated INPUT lives inside the
per-slot working arena [DAT_044e3798 + slot*0x2dfc8]; the big kyaku-definition
records (DAT_06a5ea90) + item catalog are read-only, so an arena snapshot/restore
is a complete clean-state reset.

Pre: a live retail parked where the non-tutorial scan runs (f404==0 & f406==0 &
DAT_073dddb8==0), e.g. a fresh day-1 shop free-roam (R1).  Emits a JSON fixture
of {seed -> count, eligible[], queue[], rng_draws, final_seed} + the arena hash,
the golden reference the ported scan (customer_service.c) must reproduce.

Each sample: restore the arena → recompute the centroid (FUN_0048439a; the live
game keeps it fresh, a raw callq leaves it stale) → run the scan → record the
EXACT scan-start seed via the daemon's seed_at_call (read on the engine thread the
instant before the call, defeating the RPC→engine rng drift).  The (seed, outputs)
pairs are reproducible by the port harness (roster_golden_replay.c) — the gate.

Usage: nix develop --command python3 tools/roster_scan_capture.py \
           --samples 6 --out runs/probe/roster-golden.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from probe import send  # noqa: E402

# ── VAs (Ghidra, ImageBase 0x400000) ─────────────────────────────────────────
ARENA_VA   = 0x044e3798   # working-arena bank base (slot 0)
ARENA_LEN  = 0x2dfc8      # per-slot stride
RNG_VA     = 0x006023a0   # MSVC LCG state (poke to pin)
SCAN_VA    = 0x0045edaa   # FUN_0045edaa (customer_service_session_init)
SLOT_VA    = 0x0438b1e0   # active slot index
F404_VA    = 0x0450f404   # sell-active (byte)
F406_VA    = 0x0450f406   # tutorial (byte)
DBG_VA     = 0x073dddb8   # buysell-debug override
COUNT_VA   = 0x0730ac98   # queue count
QUEUE_VA   = 0x0730aca0   # queue entries, stride 24 = {kyaku, item_slot, kind, ...}
ELIG_VA    = 0x06a5d450   # eligible list (-2 terminated)

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
        raise SystemExit(f"readmem {va:#x} failed: {r} "
                         "(daemon too old? relaunch to pick up readmem/writemem)")
    return r["hex"]


def restore_arena(clean_bytes):
    """Restore ONLY the dwords the scan mutated, via race-safe single pokes.
    A bulk arena write from the RPC thread races the live sim (crashes); the
    scan's mutation set is tiny on an empty-grid day-1 (a few story-flag
    dwords; the affinity array stays 0 with no stock), so a diff+single-poke
    restore is both safe and cheap.  Returns the list of restored byte offsets."""
    cur = bytes.fromhex(readmem(ARENA_VA, ARENA_LEN))
    restored = []
    for off in range(0, len(clean_bytes) - 3, 4):
        c = clean_bytes[off:off + 4]
        if cur[off:off + 4] != c:
            poke(ARENA_VA + off, int.from_bytes(c, "little"), "u32")
            restored.append(off)
    return restored


def callq(va):
    r = send({"cmd": "callq", "va": va, "args": [], "argt": [], "ret": "void"})
    if not r.get("ok"):
        raise SystemExit(f"callq {va:#x} failed: {r}")
    return r


CENTROID_VA = 0x0048439a  # FUN_0048439a — recompute the shop attribute centroid


def deterministic_scan():
    """Run the scan with a FRESH centroid + capture the EXACT scan-start seed.

    The live game keeps DAT_0438b4b8/bc current on display changes; a raw callq
    leaves it stale (→ wrong bands), so recompute it first.  And the sim ticks
    the LCG between poke and callq, so a poked seed drifts — the daemon's
    seed_at_call (read on the engine thread the instant before the call) is the
    only reliable scan-start seed.  Returns (seed_at_call, final_seed)."""
    callq(CENTROID_VA)                       # fresh centroid
    r = callq(SCAN_VA)                        # scan (hook grabs the exact seed)
    seed = r.get("seed_at_call")
    after = rd(RNG_VA, "u32") & MASK32
    return seed, after


def rng_draws(seed_before, seed_after, cap=1_000_000):
    """Count LCG steps from seed_before to seed_after (seeds are u32 states)."""
    s = seed_before & MASK32
    tgt = seed_after & MASK32
    for n in range(cap + 1):
        if s == tgt:
            return n
        s = (s * LCG_MUL + LCG_ADD) & MASK32
    return None   # not reached within cap


def read_eligible(cap=52):
    out = []
    for i in range(cap):
        v = rd(ELIG_VA + i * 4, "i32")
        if v == -2 or v == -1:
            break
        out.append(v)
    return out


def read_queue(count, cap=30):
    out = []
    for i in range(min(count, cap)):
        base = QUEUE_VA + i * 24
        out.append({"kyaku": rd(base + 0, "i32"),
                    "item_slot": rd(base + 4, "i32"),
                    "kind": rd(base + 8, "i32")})
    return out


def capture_seed(clean_bytes):
    """One deterministic sample: restore → fresh centroid → scan → record the
    EXACT scan-start seed (seed_at_call) + outputs.  The seed is whatever the
    live sim drifted to (not controllable), but the resulting (seed, outputs)
    pair IS reproducible by the port harness (run it at `seed`) — the gate."""
    restored = restore_arena(clean_bytes)  # reset the scan's mutated state
    seed, after = deterministic_scan()
    count = rd(COUNT_VA, "i32")
    return {"seed": seed & MASK32,
            "count": count,
            "eligible": read_eligible(),
            "queue": read_queue(count),
            "final_seed": after,
            "restored_offsets": [hex(ARENA_VA + o) for o in restored]}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=6,
                    help="number of deterministic scan samples (each records the "
                         "exact drifted scan-start seed via the engine-thread hook)")
    ap.add_argument("--out", default="runs/probe/roster-golden.json")
    args = ap.parse_args(argv)

    slot = rd(SLOT_VA, "i32")
    f404, f406, dbg = rd(F404_VA, "u8"), rd(F406_VA, "u8"), rd(DBG_VA, "i32")
    if not (f404 == 0 and f406 == 0 and dbg == 0):
        raise SystemExit(f"NOT in the scan branch: f404={f404} f406={f406} "
                         f"dbg={dbg} — need all 0 (a real non-tutorial shop).")
    if slot != 0:
        print(f"WARN: slot={slot} (ARENA_VA assumes slot 0)", file=sys.stderr)

    clean_bytes = bytes.fromhex(readmem(ARENA_VA, ARENA_LEN))
    arena_hash = hashlib.sha256(clean_bytes).hexdigest()[:16]
    print(f"arena snapshot {ARENA_LEN} bytes, sha16={arena_hash}; "
          f"sampling {args.samples} deterministic scans (fresh centroid + "
          f"seed_at_call hook)")

    results = [capture_seed(clean_bytes) for _ in range(args.samples)]
    restore_arena(clean_bytes)   # leave the live game in the clean state

    out = {"function": "FUN_0045edaa", "slot": slot, "arena_va": ARENA_VA,
           "arena_len": ARENA_LEN, "arena_sha16": arena_hash, "results": results}
    outp = Path(args.out)
    outp.parent.mkdir(parents=True, exist_ok=True)
    outp.write_text(json.dumps(out, indent=2))

    # Persist the raw arena snapshot alongside the JSON so a port-side replay
    # can inject the EXACT input state (the JSON stores only the sha16).  The
    # sidecar is named <out>.arena.bin; the fixture references it by hash.
    arena_bin = outp.with_suffix(outp.suffix + ".arena.bin")
    arena_bin.write_bytes(clean_bytes)
    out["arena_bin"] = arena_bin.name
    outp.write_text(json.dumps(out, indent=2))

    print(json.dumps(results, indent=2))
    print(f"\nwrote {outp} (+ {arena_bin.name}, sha16={arena_hash})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
