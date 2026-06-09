#!/usr/bin/env python3
"""phase_census.py — enumerate ALL load-timing-dependent state at an anchor.

The phase-state census (docs/audits/2026-06-09-methodology-audit.md T3): run the
SAME side (port or retail) twice with deliberately different pre-anchor timing
(variant B's boot-segment inputs are shifted +delta frames), `{memsnap}` both
runs at the SAME anchor-relative frame, and byte-diff the dumps. Every differing
byte is — by construction — load-timing-dependent state: the complete
phase/RNG-origin set, discovered empirically instead of one lost debugging
session at a time.

Two modes:
  discovery  pins STRIPPED from the caprange segment; snapshot at the window
             start. The diff is the full phase+RNG-bearing set (triage it once
             per scene → extend {phasepin} / annotate benign).
  pinned     canonical pins ENSURED (lint.auto_pin_text); snapshot at
             pin+settle (default 64f, past the spring-lerp re-convergence).
             A COMPLETE pin yields an EMPTY engine diff — any surviving range
             is a missing pin or true non-determinism, named by address. This
             is the standing pin-completeness regression gate.

Attribution: port ranges resolve to symbols via `nm` on the exe (sections are
dumped with a JSON index carrying link_base+RVA); retail ranges get DAT_<va>
names + a KNOWN-pin annotation table seeded from segtrace_phasepin_cb / the
agent pin list.

Usage:
  phase_census.py run  --scenario house-loaded-display --side port
                       [--mode discovery|pinned] [--delta 37] [--settle 64]
                       [--out runs/census/<scn>-<side>-<mode>] [--keep-dumps]
  phase_census.py diff <runA_dir> <runB_dir> --side port|retail [--json OUT]

Exit codes (both subcommands): 0 = clean/informational, 1 = pinned-mode found
unexplained ranges, 2 = unusable (missing dumps / size mismatch).
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "analyze"))

from trace_studio.edits.lint import (  # noqa: E402
    auto_pin_text, caprange_segment)

DEFAULT_DELTA = 37          # odd, no common cycle factor (4/8/10/16/40/64)
DEFAULT_SETTLE = 64         # frames past the pin (spring-lerp settles ~48)
MERGE_GAP = 8               # adjacent differing bytes closer than this merge
IMAGE_BASE = 0x400000       # retail link base (agent rva() convention)

# Retail addresses with a KNOWN story (from segtrace_phasepin_cb + the agent's
# pin block + standing findings). (start, size, name, cls):
#   pinned         — {phasepin}/{rngseed} covers it; differs in DISCOVERY only
#   known-unpinned — a documented mole not yet folded into the pin
#   clock          — an absolute frame/tick counter; differs by construction
KNOWN_RETAIL = [
    (0x006023a0, 4,      "LCG state (rng)",                    "pinned"),
    (0x056db054, 4,      "db054 bob/sparkle counter",          "pinned"),
    (0x056dab48, 12,     "companion anim TIMER/COUNTER/FRAME", "pinned"),
    (0x056daaf0, 12,     "player anim TIMER/COUNTER/FRAME",    "pinned"),
    (0x0438b154, 4,      "b154 shared cursor bob",             "pinned"),
    (0x073a6d98, 8,      "rmb screen-shake countdowns",        "pinned"),
    (0x073a7f80, 0x258,  "bg-NPC SoA (6 records)",             "pinned"),
    (0x073a8bb4, 8,      "bg-NPC spawn cursor + warmup latch", "pinned"),
    (0x0438b8cc, 4,      "g_sim_frame_count (sparkle %8)",     "pinned"),
    (0x073a3e0c, 4,      "dialogue advance-arrow blink",       "known-unpinned"),
    (0x09643628, 4,      "world-map entry timer",              "known-unpinned"),
]

# Port symbols that are HARNESS/clock state — expected to differ by exactly the
# input-shift construction; tagged so the report ranks engine state first.
PORT_HARNESS_PAT = re.compile(
    r"^_?(g_tick|g_segtrace|g_capture|g_input|g_anchor|g_d3d_trace|"
    r"g_call_trace|g_paused|g_turbo|g_frame|g_audio_log|g_dlg_log|"
    r"s_anchor|g_memsnap)")


# ─── variant generation (pure) ──────────────────────────────────────────────

def _parse_line(ln: str):
    s = ln.strip()
    if not s or s.startswith("#"):
        return None
    try:
        o = json.loads(s)
        return o if isinstance(o, dict) else None
    except json.JSONDecodeError:
        return None


def make_variants(text: str, mode: str, delta: int, settle: int,
                  src_dir: Path) -> tuple[str, str, int]:
    """(variant_a, variant_b, snap_frame). Pure on the text; savefile refs are
    absolutized against src_dir so the variants run from anywhere."""
    lines = text.splitlines()

    # absolutize {savefile} + strip pre-existing {memsnap}
    out: list[str] = []
    for ln in lines:
        o = _parse_line(ln)
        if o and "memsnap" in o:
            continue
        if o and isinstance(o.get("savefile"), str) \
                and not str(o["savefile"]).startswith("@"):
            ref = Path(o["savefile"])
            if not ref.is_absolute():
                o["savefile"] = str((src_dir / ref).resolve())
            out.append(json.dumps(o))
            continue
        out.append(ln)
    lines = out

    seg = caprange_segment(lines)
    if seg is None:
        raise SystemExit("phase_census: trace has no {caprange} — cannot "
                         "locate the census anchor segment")

    if mode == "discovery":
        # strip the segment's pins so load history is visible
        drop = {i for i, _ in seg["phasepins"]} | {i for i, _, _ in seg["rngseeds"]}
        lines = [ln for i, ln in enumerate(lines) if i not in drop]
        seg = caprange_segment(lines)
        snap = seg["cr"][0]
    else:                                   # pinned
        text2, _ = auto_pin_text("\n".join(lines) + "\n")
        lines = text2.splitlines()
        seg = caprange_segment(lines)
        pin = seg["phasepins"][0][1] if seg["phasepins"] else seg["cr"][0]
        snap = pin + settle

    ins_at = seg["cr_idx"]
    lines[ins_at:ins_at] = [json.dumps({"memsnap": snap})]
    va = "\n".join(lines) + "\n"

    # variant B: shift the FIRST (boot) segment's timing by +delta
    first_wait = next((i for i, ln in enumerate(lines)
                       if (_parse_line(ln) or {}).get("wait")), len(lines))
    blines: list[str] = []
    shifted = 0
    for i, ln in enumerate(lines):
        o = _parse_line(ln)
        if i < first_wait and o is not None:
            if "frame" in o and "buttons" in o:
                o["frame"] = int(o["frame"]) + delta
                blines.append(json.dumps(o)); shifted += 1
                continue
            if "esc" in o:
                o["esc"] = int(o["esc"]) + delta
                blines.append(json.dumps(o)); shifted += 1
                continue
        blines.append(ln)
    if shifted == 0:
        raise SystemExit("phase_census: boot segment has no inputs to shift — "
                         "variant B would be identical (add a pre-anchor input "
                         "to the trace, or census a different scenario)")
    return va, "\n".join(blines) + "\n", snap


# ─── dump discovery + diffing ───────────────────────────────────────────────

def retail_regions() -> list[tuple[int, int]]:
    from pe import PE
    return [(IMAGE_BASE + s.vaddr, s.vsize)
            for s in PE().sections if s.name in (".data", ".data1")]


def find_dumps(run_dir: Path, side: str) -> list[dict]:
    """[{file, va, size, name}] per dumped region, in stable order."""
    frames = Path(run_dir) / "frames"
    if side == "port":
        idx = sorted(frames.glob("memsnap_*.json"))
        if not idx:
            return []
        m = json.loads(idx[0].read_text())
        base = int(m["link_base"])
        return [{"file": frames / s["file"], "va": base + int(s["rva"]),
                 "size": int(s["vsize"]), "name": s["name"]}
                for s in m["sections"]]
    regs = retail_regions()
    out = []
    for i, (va, size) in enumerate(regs):
        hits = sorted(frames.glob(f"memsnap_*_r{i}.bin"))
        if hits:
            out.append({"file": hits[0], "va": va, "size": size,
                        "name": f"region{i}"})
    return out


def diff_ranges(a: bytes, b: bytes, merge_gap: int = MERGE_GAP) -> list[tuple[int, int]]:
    """[(offset, length)] of differing runs, merging gaps < merge_gap."""
    import numpy as np
    na = np.frombuffer(a, dtype=np.uint8)
    nb = np.frombuffer(b, dtype=np.uint8)
    n = min(len(na), len(nb))
    idx = np.nonzero(na[:n] != nb[:n])[0]
    if not len(idx):
        return []
    ranges: list[tuple[int, int]] = []
    start = prev = int(idx[0])
    for off in idx[1:]:
        off = int(off)
        if off - prev >= merge_gap:
            ranges.append((start, prev - start + 1))
            start = off
        prev = off
    ranges.append((start, prev - start + 1))
    return ranges


def load_nm(exe: Path) -> list[tuple[int, str]]:
    """Sorted (addr, name) data/bss symbols from the port exe."""
    nm = shutil.which("i686-w64-mingw32-nm") or shutil.which("nm")
    if not nm:
        return []
    r = subprocess.run([nm, "-n", str(exe)], capture_output=True, text=True)
    syms: list[tuple[int, str]] = []
    for ln in r.stdout.splitlines():
        parts = ln.split()
        if len(parts) == 3 and parts[1] in "dDbBgGsS":
            try:
                syms.append((int(parts[0], 16), parts[2]))
            except ValueError:
                pass
    return syms


def nearest_sym(syms: list[tuple[int, str]], va: int) -> tuple[str, int]:
    import bisect
    if not syms:
        return ("?", 0)
    i = bisect.bisect_right([a for a, _ in syms], va) - 1
    if i < 0:
        return ("?", 0)
    return (syms[i][1], va - syms[i][0])


def known_retail_cls(va: int, length: int) -> tuple[str, str] | None:
    for start, size, name, cls in KNOWN_RETAIL:
        if va < start + size and start < va + length:
            return name, cls
    return None


def u32_at(buf: bytes, off: int) -> int:
    off &= ~3
    if off + 4 > len(buf):
        return 0
    return int.from_bytes(buf[off:off + 4], "little")


def build_report(dumps_a: list[dict], dumps_b: list[dict], side: str,
                 nm_exe: Path | None) -> dict:
    syms = load_nm(nm_exe) if (side == "port" and nm_exe) else []
    regions = []
    total_ranges = 0
    by_cls: dict[str, int] = {}
    for da, db in zip(dumps_a, dumps_b):
        a = Path(da["file"]).read_bytes()
        b = Path(db["file"]).read_bytes()
        if len(a) != len(b):
            return {"error": f"dump size mismatch in {da['name']}: "
                             f"{len(a)} vs {len(b)}"}
        rows = []
        for off, length in diff_ranges(a, b):
            va = da["va"] + off
            row: dict = {"va": f"0x{va:08x}", "off": off, "len": length,
                         "a_u32": u32_at(a, off), "b_u32": u32_at(b, off)}
            if side == "port":
                name, soff = nearest_sym(syms, va)
                row["sym"] = f"{name}+0x{soff:x}" if name != "?" else "?"
                row["cls"] = ("harness" if PORT_HARNESS_PAT.match(name)
                              else "engine")
            else:
                row["sym"] = f"DAT_{va:08x}"
                k = known_retail_cls(va, length)
                if k:
                    row["known"], row["cls"] = k
                else:
                    row["cls"] = "UNKNOWN"
            by_cls[row["cls"]] = by_cls.get(row["cls"], 0) + 1
            rows.append(row)
        total_ranges += len(rows)
        regions.append({"name": da["name"], "va": f"0x{da['va']:08x}",
                        "size": da["size"], "n_ranges": len(rows),
                        "ranges": rows})
    return {"side": side, "n_ranges": total_ranges, "by_cls": by_cls,
            "regions": regions}


def print_report(rep: dict, mode: str, top: int = 40) -> int:
    if "error" in rep:
        print(f"phase_census: {rep['error']}", file=sys.stderr)
        return 2
    print(f"phase_census [{rep['side']}/{mode}]: {rep['n_ranges']} differing "
          f"range(s)  by class: {rep['by_cls']}")
    interesting = []
    for reg in rep["regions"]:
        for r in reg["ranges"]:
            interesting.append((r["cls"] == "harness", -r["len"], reg["name"], r))
    interesting.sort(key=lambda t: (t[0], t[1]))
    for _, _, rname, r in interesting[:top]:
        extra = f"  [{r.get('known')}]" if r.get("known") else ""
        sym = r.get("sym", "")
        print(f"  {r['va']}  len {r['len']:>6}  {r['cls']:<14} {sym}{extra}"
              f"  A=0x{r['a_u32']:08x} B=0x{r['b_u32']:08x}")
    if rep["n_ranges"] > top:
        print(f"  … +{rep['n_ranges'] - top} more (see report.json)")
    if mode == "pinned":
        bad = sum(n for c, n in rep["by_cls"].items()
                  if c not in ("harness", "pinned", "clock"))
        if bad:
            print(f"phase_census: PINNED census NOT clean — {bad} unexplained "
                  f"range(s) = missing pin or true non-determinism")
            return 1
        print("phase_census: PINNED census clean — pin coverage holds")
    return 0


# ─── run orchestration ──────────────────────────────────────────────────────

def drive(side: str, variant: Path, run_dir: Path, cr: tuple[int, int],
          max_frames_port: int, max_frames_retail: int) -> None:
    run_dir.mkdir(parents=True, exist_ok=True)
    if side == "port":
        cmd = [sys.executable, str(ROOT / "tools" / "export_trace.py"),
               str(variant), "--caprange", f"{cr[0]},{cr[1]}",
               "--run-dir", str(run_dir), "--max-frames", str(max_frames_port)]
    else:
        cmd = [sys.executable, str(ROOT / "tools" / "frida_capture.py"),
               "--run-dir", str(run_dir), "--input-segtrace", str(variant),
               "--max-frames", str(max_frames_retail),
               "--duration-ms", "600000", "--turbo", "--silent-audio",
               "--hide-window"]
    print(f"phase_census: drive {side}: {' '.join(cmd[1:])}")
    r = subprocess.run(cmd, cwd=str(ROOT))
    if r.returncode != 0:
        print(f"phase_census: WARNING {side} run rc={r.returncode} "
              f"(continuing — the dump may still exist)", file=sys.stderr)


def cmd_run(args) -> int:
    scn = ROOT / "tests" / "scenarios" / args.scenario / "trace.jsonl"
    src = Path(args.trace) if args.trace else scn
    if not src.exists():
        raise SystemExit(f"phase_census: no trace at {src}")
    out = Path(args.out) if args.out else (
        ROOT / "runs" / "census" / f"{args.scenario or src.stem}-{args.side}-{args.mode}")
    out.mkdir(parents=True, exist_ok=True)

    va, vb, snap = make_variants(src.read_text(), args.mode, args.delta,
                                 args.settle, src.resolve().parent)
    pa, pb = out / "variant_a.trace.jsonl", out / "variant_b.trace.jsonl"
    pa.write_text(va); pb.write_text(vb)
    seg = caprange_segment(va.splitlines())
    cr = seg["cr"]
    print(f"phase_census: {args.mode} census of {src.name} side={args.side} "
          f"delta=+{args.delta} snap@{snap} (anchor-relative)")

    drive(args.side, pa, out / "a", cr, args.port_max_frames, args.retail_max_frames)
    drive(args.side, pb, out / "b", cr, args.port_max_frames, args.retail_max_frames)

    da, db = find_dumps(out / "a", args.side), find_dumps(out / "b", args.side)
    if not da or not db or len(da) != len(db):
        print(f"phase_census: missing dumps (a={len(da)} b={len(db)}) — "
              f"check the run logs under {out}", file=sys.stderr)
        return 2
    rep = build_report(da, db, args.side,
                       ROOT / "build" / "openrecet.exe")
    rep["mode"] = args.mode
    rep["snap_frame"] = snap
    rep["delta"] = args.delta
    rep["scenario"] = args.scenario or src.stem
    (out / "report.json").write_text(json.dumps(rep, indent=2) + "\n")
    rc = print_report(rep, args.mode, top=args.top)
    print(f"phase_census: report → {out / 'report.json'}")
    if not args.keep_dumps and "error" not in rep:
        for d in (out / "a", out / "b"):
            for f in (d / "frames").glob("memsnap_*"):
                f.unlink()
        print("phase_census: dumps pruned (--keep-dumps to retain)")
    return rc


def cmd_diff(args) -> int:
    da = find_dumps(Path(args.a), args.side)
    db = find_dumps(Path(args.b), args.side)
    if not da or not db or len(da) != len(db):
        print(f"phase_census: missing dumps (a={len(da)} b={len(db)})",
              file=sys.stderr)
        return 2
    rep = build_report(da, db, args.side, ROOT / "build" / "openrecet.exe")
    if args.json:
        Path(args.json).write_text(json.dumps(rep, indent=2) + "\n")
    return print_report(rep, args.mode, top=args.top)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="phase_census")
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run", help="two timing-shifted runs + diff (one stop)")
    r.add_argument("--scenario", help="scenario name under tests/scenarios/")
    r.add_argument("--trace", help="explicit trace path (overrides --scenario)")
    r.add_argument("--side", choices=("port", "retail"), required=True)
    r.add_argument("--mode", choices=("discovery", "pinned"), default="discovery")
    r.add_argument("--delta", type=int, default=DEFAULT_DELTA)
    r.add_argument("--settle", type=int, default=DEFAULT_SETTLE,
                   help="pinned mode: snapshot at pin+settle (default 64)")
    r.add_argument("--out")
    r.add_argument("--top", type=int, default=40)
    r.add_argument("--keep-dumps", action="store_true",
                   help="retain the raw section dumps (50-150 MB each)")
    r.add_argument("--port-max-frames", type=int, default=4000)
    r.add_argument("--retail-max-frames", type=int, default=22000)
    r.set_defaults(func=cmd_run)

    d = sub.add_parser("diff", help="diff two existing census run dirs")
    d.add_argument("a"); d.add_argument("b")
    d.add_argument("--side", choices=("port", "retail"), required=True)
    d.add_argument("--mode", choices=("discovery", "pinned"), default="discovery")
    d.add_argument("--json", help="write report JSON here")
    d.add_argument("--top", type=int, default=40)
    d.set_defaults(func=cmd_diff)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
