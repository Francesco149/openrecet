#!/usr/bin/env python3
"""
tools/phase_probe.py — one-command port↔retail PHASE / determinism probe.

THE tool to reach for whenever a free-roam animation / bob / sparkle / spawn
*phase* looks off vs retail, or you suspect a counter/RNG desync.  It answers
ONE question immediately: **is this a logic error, or just a load-dependent
phase offset (counters/RNG out of sync but the LAWS bit-exact)?**

How it works (the process this tool solidifies — see docs/phase-debugging.md):
  1. Take a synced free-roam scenario trace (one with a HOUSE_FREEROAM anchor;
     the canonical one is `house-walk-down-dense`, where Recette's walk is 1:1).
  2. Inject a `{phasepin:N}` op just before the capture window (default
     window_start − 40) so BOTH targets reset the companion's load-dependent
     phase (db054 + anim cycle) to a common origin AFTER the post-anchor load
     freeze settles — see {phasepin} in src/input_segtrace.c / the Frida agent.
     (`--no-pin` skips it to show the RAW offset first.)
  3. Drive the PORT  (tools/export_trace.py → meta.jsonl, the --player-pos-log
     per-frame phase fields) and RETAIL (tools/frida_capture.py --watch of the
     engine phase VAs → watch.jsonl).
  4. Align the two by db054 VALUE (a shared clock once pinned) and, per counter,
     classify the port−retail offset:
        {0}            → ALIGNED (bit-exact)
        {one nonzero}  → CONSTANT OFFSET (pure phase/sync — NOT a logic bug)
        many / growing → DRIFT (a real per-frame LOGIC divergence; the frame it
                         first changes is printed)
  5. Print a verdict table.  RNG is reported separately (pin it with a
     `{rngseed}` op for a clean RNG comparison; unpinned RNG is expected to
     differ and is flagged as such, not as a bug).

Engine phase VAs (companion record from FUN_0048a4d1; db054 from §94):
  db054=0x056db054  cframe=0x056dab50  ccnt=0x056dab4c  ctimer=0x056dab48
  canim=0x056dab40  canimsel=0x056dab54  coct(facing)=0x056dab58  rng=0x006023a0

Usage:
  nix develop --command python3 tools/phase_probe.py house-walk-down-dense
  nix develop --command python3 tools/phase_probe.py house-walk-down-dense --no-pin
  nix develop --command python3 tools/phase_probe.py <trace.jsonl> --window 1540,140

Outputs land in runs/phase-probe/<scenario>/{port,retail}/ and a verdict to stdout.
Companion-focused today; player/NPC actor phase VAs are a documented follow-up
(extend STD_WATCHES + the port pos-log).  Cross-refs: docs/phase-debugging.md,
docs/render-depth-debugging.md (the draw-side `d3d_state_diff.py phase` twin),
docs/findings/scene1-tear-visual-diffs.md, engine-quirks §94.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The standard companion phase-counter watch set (retail Ghidra VAs).  Each entry
# is (name, va, type) and the matching field name in the port's player-pos-log.
STD_WATCHES = [
    ("db054",    0x056db054, "s32"),   # bob/sparkle per-scene counter (§94)
    ("cframe",   0x056dab50, "s32"),   # companion sprite anim FRAME cell
    ("ccnt",     0x056dab4c, "s32"),   # companion anim COUNTER
    ("ctimer",   0x056dab48, "f32"),   # companion anim TIMER (float)
    ("canim",    0x056dab40, "s32"),   # companion anim id
    ("coct",     0x056dab58, "s32"),   # companion facing octant
    ("rng",      0x006023a0, "s32"),   # engine LCG state DAT_006023a0
]
# Counters compared for the verdict (rng handled separately).
PHASE_COUNTERS = ["cframe", "ccnt", "coct", "canim"]


def resolve_trace(arg: str) -> Path:
    p = Path(arg)
    if p.exists():
        return p
    scen = ROOT / "tests" / "scenarios" / arg / "trace.jsonl"
    if scen.exists():
        return scen
    sys.exit(f"phase_probe: no trace at {arg!r} nor scenario tests/scenarios/{arg}/")


def trace_window(trace: Path) -> tuple[int, int]:
    """Derive the capture window [start, count] from the trace's {capture} ops."""
    caps = []
    for ln in trace.read_text().splitlines():
        ln = ln.strip()
        if '"capture"' in ln:
            caps.append(json.loads(ln)["capture"])
    if not caps:
        return (1540, 140)
    return (min(caps), max(caps) - min(caps) + 1)


def inject_pins(trace: Path, pin_at: int, out: Path, seed: int | None) -> None:
    """Copy `trace`, inserting {phasepin:pin_at} (and, unless seed is None,
    {rngseed:[pin_at,seed]}) after the LAST HOUSE_FREEROAM wait, so BOTH targets
    reset phase AND RNG to a common origin at the same anchor-relative frame —
    the captures + diffs are then phase- AND rng-aligned (no sparkle noise)."""
    lines = [l.rstrip("\n") for l in trace.read_text().splitlines()]
    wait_idxs = [i for i, l in enumerate(lines) if l.strip() and '"wait"' in l]
    if not wait_idxs:
        sys.exit("phase_probe: trace has no {wait} anchor — can't anchor a phasepin")
    i = wait_idxs[-1]
    ops = [json.dumps({"phasepin": pin_at})]
    if seed is not None:
        ops.append(json.dumps({"rngseed": [pin_at, seed]}))
    new = lines[: i + 1] + ops + lines[i + 1 :]
    out.write_text("\n".join(new) + "\n")


def run_port(trace: Path, start: int, count: int, run_dir: Path) -> None:
    cmd = [sys.executable, str(ROOT / "tools" / "export_trace.py"), str(trace),
           "--caprange", f"{start},{count}", "--run-dir", str(run_dir),
           "--name", "phase-probe"]
    print(f"  port: export_trace --caprange {start},{count} …", flush=True)
    with (run_dir / "drive.log").open("w") as log:
        subprocess.run(cmd, check=True, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)


def run_retail(trace: Path, run_dir: Path, max_frames: int, remote: str) -> None:
    cmd = [sys.executable, str(ROOT / "tools" / "frida_capture.py"),
           "--remote", remote, "--run-dir", str(run_dir),
           "--input-segtrace", str(trace),
           "--turbo", "--silent-audio", "--force-resolution", "1024x768",
           "--max-frames", str(max_frames), "--hide-window", "--no-montage"]
    for name, va, typ in STD_WATCHES:
        cmd += ["--watch", f"{name}=0x{va:08x}:{typ}"]
    print(f"  retail: frida_capture --input-segtrace … --watch ×{len(STD_WATCHES)} "
          f"--max-frames {max_frames}", flush=True)
    with (run_dir / "drive.log").open("w") as log:
        subprocess.run(cmd, check=True, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)


def load_port(run_dir: Path) -> list[dict]:
    return [json.loads(l) for l in (run_dir / "meta.jsonl").read_text().splitlines()
            if '"db054"' in l]


def load_retail(run_dir: Path) -> tuple[list[dict], int | None]:
    rows = [json.loads(l)["vals"] | {"frame": json.loads(l)["frame"]}
            for l in (run_dir / "watch.jsonl").read_text().splitlines()]
    pin_frame = None
    log = run_dir / "agent.log"
    if log.exists():
        for ln in log.read_text().splitlines():
            m = re.search(r"phasepin .* at frame (\d+)", ln)
            if m:
                pin_frame = int(m.group(1))
    return rows, pin_frame


def classify(offsets: list[int]) -> tuple[str, str]:
    s = sorted(set(offsets))
    if s == [0]:
        return ("ALIGNED", "bit-exact")
    if len(s) == 1:
        return ("CONST-OFFSET", f"+{s[0]} constant (phase/sync, NOT logic)")
    # mod-cycle wrap looks like 2 values whose gap is the anim period — still phase
    if len(s) == 2:
        return ("CONST-OFFSET", f"offsets {s} (phase shift mod anim cycle)")
    return ("DRIFT", f"{len(s)} distinct offsets {s[:6]}… → LOGIC divergence")


def main() -> int:
    ap = argparse.ArgumentParser(description="port↔retail phase / determinism probe")
    ap.add_argument("trace", help="scenario name or trace.jsonl path")
    ap.add_argument("--window", help="START,COUNT capture window (default: from trace)")
    ap.add_argument("--pin-at", type=int, default=None,
                    help="base-relative frame to fire {phasepin} (default window_start-40)")
    ap.add_argument("--no-pin", action="store_true",
                    help="skip the phasepin — show the RAW load-dependent offset")
    ap.add_argument("--seed", type=int, default=99155263,
                    help="RNG seed to pin on BOTH targets at the pin frame "
                         "(phase+rng aligned captures/diffs). 0 disables rng-pin.")
    ap.add_argument("--remote", default="cutestation.soy:27042")
    ap.add_argument("--max-frames", type=int, default=6500,
                    help="retail frame cap (must reach 2nd anchor + window; default 6500)")
    ap.add_argument("--reuse", action="store_true",
                    help="skip the runs, just re-analyze existing run dirs")
    args = ap.parse_args()

    trace = resolve_trace(args.trace)
    name = trace.parent.name if trace.name == "trace.jsonl" else trace.stem
    out = ROOT / "runs" / "phase-probe" / name
    (out / "port").mkdir(parents=True, exist_ok=True)
    (out / "retail").mkdir(parents=True, exist_ok=True)

    if args.window:
        start, count = (int(x) for x in args.window.split(","))
    else:
        start, count = trace_window(trace)
    pin_at = args.pin_at if args.pin_at is not None else max(0, start - 40)

    work = out / "trace.work.jsonl"
    seed = None if (args.no_pin or args.seed == 0) else args.seed
    if args.no_pin:
        work.write_text(trace.read_text())
        print(f"phase_probe: {name}  window={start},{count}  PIN=off (raw offset)")
    else:
        inject_pins(trace, pin_at, work, seed)
        rng = f" + rngseed={seed}" if seed is not None else " (rng UNPINNED)"
        print(f"phase_probe: {name}  window={start},{count}  "
              f"phasepin@base+{pin_at}{rng}")

    if not args.reuse:
        run_port(work, start, count, out / "port")
        run_retail(work, out / "retail", args.max_frames, args.remote)

    port = load_port(out / "port")
    ret_rows, pin_frame = load_retail(out / "retail")

    # Align by db054 value.  When pinned, both reset db054→0 at base+pin_at, so
    # db054 is a shared clock; restrict retail rows to AFTER the pin fired.
    lo, hi = 1, count + start  # db054 values we expect in/after the window
    if not args.no_pin and pin_frame is not None:
        ret_rows = [r for r in ret_rows if r["frame"] > pin_frame]
        lo, hi = 1, count + 60
    retmap = {}
    for r in ret_rows:
        d = r.get("db054")
        if d is not None and lo <= d <= hi:
            retmap.setdefault(d, r)  # first occurrence after the pin
    portmap = {}
    for r in port:
        d = r.get("db054")
        if d is not None and lo <= d <= hi:
            portmap[d] = r

    common = sorted(set(portmap) & set(retmap))
    if not common:
        print("phase_probe: NO common db054 values — alignment failed "
              "(check the runs reached the window; raise --max-frames).")
        return 2

    print(f"\n  aligned {len(common)} frames by db054 "
          f"(range {common[0]}..{common[-1]})\n")
    print(f"  {'counter':<10} {'verdict':<13} detail")
    print(f"  {'-'*10} {'-'*13} {'-'*40}")
    worst = "ALIGNED"
    rank = {"ALIGNED": 0, "CONST-OFFSET": 1, "DRIFT": 2}
    for c in PHASE_COUNTERS:
        offs = [portmap[v][c] - retmap[v][c] for v in common
                if c in portmap[v] and c in retmap[v]]
        if not offs:
            continue
        verdict, detail = classify(offs)
        if rank[verdict] > rank[worst]:
            worst = verdict
        # first frame an offset changes (for DRIFT)
        print(f"  {c:<10} {verdict:<13} {detail}")
    # RNG
    rng_off = [portmap[v]["rng"] - retmap[v]["rng"] for v in common
               if "rng" in portmap[v] and "rng" in retmap[v]]
    rng_pinned = '"rngseed"' in work.read_text()
    if rng_pinned:
        v, d = classify(rng_off)
        print(f"  {'rng':<10} {v:<13} {d}")
        if rank[v] > rank[worst]:
            worst = v
    else:
        match = sum(1 for o in rng_off if o == 0)
        print(f"  {'rng':<10} {'UNPINNED':<13} {match}/{len(rng_off)} match — "
              f"add a {{rngseed}} op for a clean RNG comparison")

    print()
    if worst == "ALIGNED":
        print("  VERDICT: ✅ PHASE-CLEAN — all counters bit-exact vs retail. "
              "Any visual diff is RNG (sparkles) or render-side, not phase.")
    elif worst == "CONST-OFFSET":
        print("  VERDICT: ⏱  PHASE/SYNC OFFSET — laws bit-exact, only the phase "
              "ORIGIN differs (load-dependent). Pin earlier/later or RNG-pin; "
              "NOT a logic bug.")
    else:
        print("  VERDICT: ❌ LOGIC DRIFT — a counter diverges per-frame (offset "
              "grows). This is a real port logic error; see the DRIFT row above.")
    print(f"\n  data: {out}/  (port/meta.jsonl, retail/watch.jsonl)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
