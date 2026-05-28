#!/usr/bin/env python3
"""
tools/render_trace_gate.py — Linux-runnable render-path regression gate.

The port side already emits a deterministic D3D call trace (src/d3d_trace.c,
flags --d3d-trace <path> / --d3d-trace-frames i,j,k — see
docs/findings/d3d-trace.md).  This tool turns that into a self-contained
regression *signal*: per scenario it re-runs build/openrecet.exe (via the
Job-Object supervisor, exactly like tools/scenario-test.py), captures a
fresh trace at the scenario's capture_frames, and diffs it byte-for-byte
against a committed golden trace:

    tests/scenarios/<name>/d3d_trace.golden.jsonl

The trace records ONLY D3D call args (render-state codes, primitive counts,
matrices, opaque pointer ids) — NO vendor pixels — so the golden is safe to
commit and gives 35 render files / scene1_render.c automated coverage with
no Windows/Frida/retail dependency: openrecet.exe runs under WSLInterop.

Unlike render_diff.py (which compares retail-vs-port and so also coalesces
redundant state writes to absorb benign cross-target noise), this gate
compares the SAME binary against its own golden, so it does NOT coalesce —
it demands the exact same call sequence.  It DOES opaquify pointers the way
render_diff's --opaque-pointers mode does: texture / vertex-buffer / index-
buffer handle VALUES are heap addresses that legitimately differ between two
processes (allocator / ASLR), so they are rewritten to first-seen synthetic
ids (#0, #1, …) per (op, arg) before comparison.  Everything else — render-
state codes, primitive counts, shaders, matrices, materials, and the
relative ORDER in which pointers are allocated — must match exactly.  Any
divergence there is a real regression in the render path.

Usage:
    nix develop --command tools/render_trace_gate.py            # all scenarios
    nix develop --command tools/render_trace_gate.py boot-idle  # one scenario
    nix develop --command tools/render_trace_gate.py --bless    # (re)generate goldens

Exit code: 0 if every scenario's fresh trace matches its golden, 1 on any
divergence or missing golden, 2 on a launch/structural error.
"""

from __future__ import annotations

import argparse
import datetime as dt
import importlib.util
import subprocess
import sys
from pathlib import Path


ROOT       = Path(__file__).resolve().parent.parent
SCENARIOS  = ROOT / "tests" / "scenarios"
BUILD_EXE  = ROOT / "build" / "openrecet.exe"
ASSET_CWD  = ROOT / "vendor" / "original"
SUPERVISOR_EXE = ROOT / "build" / "openrecet-supervisor.exe"

GOLDEN_NAME = "d3d_trace.golden.jsonl"

# Deterministic, Linux-runnable title-phase scenarios. Each must have a
# scenario.yaml + trace.jsonl. HOUSE/INGAME scenarios are intentionally
# excluded — they need a save-inject and emit 1000+ events/frame.
DEFAULT_SCENARIOS = (
    "boot-idle",
    "title-z-press",
    "title-down-press",
    "title-options",
)


def _load_module(mod_name: str, file_name: str):
    """Import a tools/*.py module by path. Registers it in sys.modules
    BEFORE exec so that @dataclass annotations (which resolve types via
    cls.__module__) work — render_diff.py + scenario-test.py both define
    dataclasses."""
    path = ROOT / "tools" / file_name
    spec = importlib.util.spec_from_file_location(mod_name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    spec.loader.exec_module(mod)
    return mod


def _load_render_diff():
    """Import tools/render_diff.py for its load_trace + diff_frame logic."""
    return _load_module("openrecet_render_diff", "render_diff.py")


def _load_scenario_test():
    """Import tools/scenario-test.py for the Scenario dataclass loader."""
    return _load_module("openrecet_scenario_test", "scenario-test.py")


def wslpath_w(p: Path) -> str:
    r = subprocess.run(["wslpath", "-w", str(p)],
                       capture_output=True, text=True, check=True)
    return r.stdout.strip()


def capture_trace(scen, out_path: Path) -> None:
    """Run the supervised exe through `scen`, emitting a fresh d3d trace to
    `out_path` at the scenario's capture_frames. Mirrors the launch flags of
    tools/scenario-test.py::run_scenario_capture so the run is identical
    to the regression harness (hidden, turbo, silent audio, pinned seed)."""
    if not SUPERVISOR_EXE.exists():
        raise SystemExit(
            f"supervisor missing: {SUPERVISOR_EXE}\n"
            f"  build it with: nix develop --command make -C tools/supervisor")
    if not BUILD_EXE.exists():
        raise SystemExit(
            f"exe missing: {BUILD_EXE}\n  build it with: nix develop --command make -C src")

    trace_path = scen.path / "trace.jsonl"
    if not trace_path.exists():
        raise SystemExit(f"scenario trace missing: {trace_path}")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    if out_path.exists():
        out_path.unlink()

    frames_csv = ",".join(str(f) for f in scen.capture_frames)
    child_args = [
        "--input-trace-replay", wslpath_w(trace_path),
        "--rng-seed",           str(scen.rng_seed),
        "--max-frames",         str(scen.max_frames),
        "--max-duration-ms",    str(scen.duration_ceiling_ms),
        "--d3d-trace",          wslpath_w(out_path),
        "--d3d-trace-frames",   frames_csv,
        "--turbo",
        "--silent-audio",
        "--hidden",
    ]
    sup_timeout_ms = scen.duration_ceiling_ms + 1000
    cmd = [str(SUPERVISOR_EXE), str(int(sup_timeout_ms)),
           wslpath_w(BUILD_EXE), *child_args]

    proc = subprocess.run(
        cmd, cwd=str(ASSET_CWD),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        timeout=scen.duration_ceiling_ms / 1000 + 2,
    )
    if proc.returncode != 0:
        raise SystemExit(
            f"exe exited nonzero ({proc.returncode}) for scenario {scen.name}")
    if not out_path.exists():
        raise SystemExit(
            f"no trace produced at {out_path} for scenario {scen.name}")


def _load_opaqued(rd, path: Path) -> dict:
    """load_trace + per-frame pointer opaquification (no coalesce). Pointer
    handle values are per-process heap addresses; opaquify_pointers rewrites
    them to first-seen synthetic ids so two runs of the same binary compare
    equal as long as objects are allocated in the same relative order."""
    return {f: rd.opaqueify_pointers(evts)
            for f, evts in rd.load_trace(path).items()}


def diff_traces(rd, fresh_path: Path, golden_path: Path) -> list:
    """Per-frame diff: fresh vs golden, pointers opaquified, no coalesce.
    Returns the list of FrameDiff objects that diverged."""
    fresh_by_frame  = _load_opaqued(rd, fresh_path)
    golden_by_frame = _load_opaqued(rd, golden_path)

    frames = sorted(set(fresh_by_frame) | set(golden_by_frame))
    diverged = []
    for f in frames:
        if f not in fresh_by_frame:
            print(f"    frame {f}: present in golden, MISSING in fresh trace")
            fd = rd.FrameDiff(frame=f, n_retail=len(golden_by_frame[f]), n_port=0)
            fd.blocks.append({"tag": "delete", "retail": golden_by_frame[f],
                              "port": [], "i_lo": 0, "p_lo": 0})
            diverged.append(fd)
            continue
        if f not in golden_by_frame:
            print(f"    frame {f}: present in fresh trace, MISSING in golden")
            fd = rd.FrameDiff(frame=f, n_retail=0, n_port=len(fresh_by_frame[f]))
            fd.blocks.append({"tag": "insert", "retail": [],
                              "port": fresh_by_frame[f], "i_lo": 0, "p_lo": 0})
            diverged.append(fd)
            continue
        # diff_frame(frame, retail=golden, port=fresh) — labels are cosmetic.
        fd = rd.diff_frame(f, golden_by_frame[f], fresh_by_frame[f])
        if fd.diverged:
            diverged.append(fd)
    return diverged


def run_gate(names: list[str], bless: bool) -> int:
    rd = _load_render_diff()
    st = _load_scenario_test()

    rid = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_root = ROOT / "runs" / "render-trace-gate" / rid

    total = passed = failed = blessed = 0
    for name in names:
        scen_path = SCENARIOS / name
        if not (scen_path / "scenario.yaml").exists():
            print(f"# {name}: SKIP (no scenario.yaml)")
            continue
        scen = st.Scenario.load(scen_path)
        total += 1
        golden = scen_path / GOLDEN_NAME
        fresh  = run_root / f"{name}.jsonl"

        print(f"\n# {name}  (capture_frames={scen.capture_frames})")
        capture_trace(scen, fresh)
        nlines = sum(1 for _ in fresh.open())
        print(f"  captured {nlines} D3D call(s) → {fresh.relative_to(ROOT)}")

        if bless:
            golden.write_bytes(fresh.read_bytes())
            print(f"  blessed → {golden.relative_to(ROOT)}")
            blessed += 1
            continue

        if not golden.exists():
            print(f"  FAIL: no golden at {golden.relative_to(ROOT)} "
                  f"(run with --bless to create)")
            failed += 1
            continue

        diverged = diff_traces(rd, fresh, golden)
        if not diverged:
            print(f"  PASS: fresh trace matches golden")
            passed += 1
        else:
            print(f"  FAIL: {len(diverged)} frame(s) diverge from golden:")
            rd.print_diffs(
                _load_opaqued(rd, golden), _load_opaqued(rd, fresh),
                diverged, ctx=3, max_blocks=5)
            failed += 1

    print()
    print("=" * 60)
    if bless:
        print(f"blessed {blessed}/{total} scenario golden(s)")
        return 0
    print(f"{passed} passed, {failed} failed (of {total} scenario(s))")
    return 0 if failed == 0 else 1


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", nargs="*",
        help="scenario name(s) under tests/scenarios/; omit for the "
             "default deterministic set "
             f"({', '.join(DEFAULT_SCENARIOS)})")
    ap.add_argument("--bless", action="store_true",
        help="(re)generate the committed golden trace from a fresh run "
             "instead of diffing against it")
    args = ap.parse_args(argv)
    names = args.scenario if args.scenario else list(DEFAULT_SCENARIOS)
    return run_gate(names, args.bless)


if __name__ == "__main__":
    sys.exit(main())
