#!/usr/bin/env python3
"""
tools/bisect_call_trace_vas.py — find the maximum subset of engine VAs
that can be Frida-hooked without crashing the retail engine.

Some addresses in tools/ttd/data/engine_function_vas.json overlap with
MSVC CRT / MFC internals that Frida cannot safely trampoline (allocator
re-entry, exception-unwind paths, TLS slots, ...).  Hooking them all at
once kills the engine on boot.  This script:

  1. Starts from the safe seed set (engine VAs ∩ Ghidra non-thunks,
     entry size ≥ 5).
  2. Tries to boot retail with that set.
  3. On crash, binary-searches to identify one offending VA.
  4. Adds it to the exclusion list, retries.
  5. Loops until a boot succeeds.

Progress is written incrementally to:
    tools/ttd/data/engine_function_vas_frida_safe.json
so a kill mid-run loses at most one bisection's worth of work.

Each test spawns frida_capture.py and waits for the engine to reach
Present > 0 (last_engine_frame >= 1).  ~15-25s per test; ~12 tests per
bad VA identified.  Plan for ~20-30 min total on a few-bad-VA case.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
RUN_BASE = Path("/tmp/openrecet-bisect")
OUT_PATH = REPO / "tools" / "ttd" / "data" / "engine_function_vas_frida_safe.json"
PROGRESS_PATH = RUN_BASE / "progress.json"

DURATION_MS = 8000
SUBPROC_TIMEOUT = 45
MAX_FRAMES = 3


def build_seed() -> list[int]:
    engine_vas = set(json.loads((REPO / "tools" / "ttd" / "data" /
                                 "engine_function_vas.json").read_text())["vas"])
    keep: list[int] = []
    with (REPO / "docs" / "decompiled" / "functions.csv").open() as f:
        for row in csv.DictReader(f):
            try:
                va = int(row["entry"], 16)
                size = int(row["size"])
            except (KeyError, ValueError):
                continue
            if (va in engine_vas
                    and size >= 5
                    and row.get("is_thunk", "").lower() != "true"):
                keep.append(va)
    return sorted(keep)


_RUN_SEQ = [0]
_FRIDA_DEV = [None]


def _frida_device():
    """Cached Frida device handle for cleanup kills.  device.kill(pid)
    works cross-handle on PIDs Frida-server can reach, even if they
    don't appear in enumerate_processes()."""
    if _FRIDA_DEV[0] is None:
        import frida
        dm = frida.get_device_manager()
        try:
            _FRIDA_DEV[0] = dm.add_remote_device("cutestation.soy:27042")
        except frida.InvalidArgumentError:
            _FRIDA_DEV[0] = dm.get_device("cutestation.soy:27042")
    return _FRIDA_DEV[0]


def _list_retail_pids() -> set[int]:
    """PIDs of any running recettear.unpacked.exe on the host."""
    try:
        r = subprocess.run(
            ["/mnt/c/Windows/system32/tasklist.exe",
             "/fi", "imagename eq recettear.unpacked.exe",
             "/fo", "csv", "/nh"],
            capture_output=True, text=True, timeout=10)
    except Exception:
        return set()
    pids: set[int] = set()
    for line in r.stdout.splitlines():
        parts = [p.strip('"') for p in line.split(",")]
        if len(parts) >= 2:
            try:
                pids.add(int(parts[1]))
            except ValueError:
                pass
    return pids


def cleanup_stale_retail(label: str = "") -> int:
    """Kill any retail processes left over from prior runs.  Returns the
    number killed."""
    pids = _list_retail_pids()
    if not pids:
        return 0
    dev = _frida_device()
    n = 0
    for pid in pids:
        try:
            dev.kill(pid)
            n += 1
        except Exception:
            pass
    if n:
        print(f"      cleanup{(' ' + label) if label else ''}: "
              f"killed {n} stale recettear(s) {sorted(pids)}", flush=True)
    return n


def test_subset(vas: list[int]) -> tuple[bool, str]:
    """True iff retail reached Present > 0 with `vas` hooked."""
    # Always start from a clean slate so a previous test's leak doesn't
    # tax the host's memory.
    cleanup_stale_retail("pre")

    _RUN_SEQ[0] += 1
    run_id = _RUN_SEQ[0]
    run_dir = RUN_BASE / f"r{run_id:04d}"
    run_dir.mkdir(parents=True, exist_ok=True)
    vas_file = run_dir / "vas.json"
    vas_file.write_text(json.dumps({"count": len(vas), "vas": vas}))

    cmd = [
        "python3", str(REPO / "tools" / "frida_capture.py"),
        "--run-dir", str(run_dir / "run"),
        "--max-frames", str(MAX_FRAMES),
        "--duration-ms", str(DURATION_MS),
        "--hide-window", "--turbo", "--silent-audio",
        "--call-trace",
        "--call-trace-vas-file", str(vas_file),
        # frame index that the run never reaches — keeps onEnter cheap
        "--call-trace-frames", "99999",
    ]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=SUBPROC_TIMEOUT)
    except subprocess.TimeoutExpired:
        return False, "subprocess_timeout"

    m = re.search(r'"last_engine_frame":\s*(-?\d+)', r.stdout)
    lef = int(m.group(1)) if m else -1
    return lef >= 1, f"last_engine_frame={lef}"


def find_one_bad(vas: list[int]) -> int:
    """Bisect `vas` (which is known to fail) to one offending VA."""
    lo, hi = 0, len(vas)
    while hi - lo > 1:
        mid = (lo + hi) // 2
        # if the lower half ALONE passes, the bad VA is in the upper
        ok, info = test_subset(vas[lo:mid])
        print(f"      bisect[{lo}:{mid}] n={mid-lo}: "
              f"{'PASS' if ok else 'FAIL'} ({info})", flush=True)
        if ok:
            lo = mid
        else:
            hi = mid
    return vas[lo]


def save_progress(excluded: list[int], current_active: int,
                  status: str, elapsed_s: float) -> None:
    PROGRESS_PATH.write_text(json.dumps({
        "status":         status,
        "elapsed_s":      round(elapsed_s, 1),
        "excluded":       excluded,
        "n_excluded":     len(excluded),
        "n_active":       current_active,
    }, indent=2))


def save_result(seed: list[int], excluded: list[int],
                elapsed_min: float) -> None:
    safe = [v for v in seed if v not in set(excluded)]
    OUT_PATH.write_text(json.dumps({
        "description":   ("engine VAs vetted Frida-safe via "
                          "tools/bisect_call_trace_vas.py"),
        "count":         len(safe),
        "n_excluded":    len(excluded),
        "elapsed_min":   round(elapsed_min, 1),
        "vas":           safe,
        "excluded":      sorted(excluded),
    }, indent=2))


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--resume", action="store_true",
        help="resume from progress.json (skip the seed test if a result "
             "already exists)")
    ap.add_argument("--max-bad", type=int, default=20,
        help="give up after identifying this many bad VAs (default 20)")
    args = ap.parse_args(argv)

    RUN_BASE.mkdir(parents=True, exist_ok=True)
    seed = build_seed()
    print(f"seed safe-set: {len(seed)}", flush=True)

    excluded: list[int] = []
    if args.resume and PROGRESS_PATH.exists():
        prev = json.loads(PROGRESS_PATH.read_text())
        excluded = list(prev.get("excluded", []))
        print(f"resume: {len(excluded)} previously-known-bad VAs", flush=True)

    t0 = time.monotonic()

    for round_i in range(args.max_bad + 1):
        active = [v for v in seed if v not in set(excluded)]
        elapsed = time.monotonic() - t0
        save_progress(excluded, len(active), "testing_full", elapsed)
        print(f"\n=== round {round_i}: testing {len(active)} VAs "
              f"(elapsed {elapsed/60:.1f} min) ===", flush=True)

        ok, info = test_subset(active)
        print(f"  full set: {'PASS' if ok else 'FAIL'} ({info})", flush=True)

        if ok:
            elapsed_min = (time.monotonic() - t0) / 60
            save_progress(excluded, len(active), "complete", elapsed)
            save_result(seed, excluded, elapsed_min)
            print(f"\nDONE in {elapsed_min:.1f} min. "
                  f"safe={len(active)} excluded={len(excluded)}", flush=True)
            print(f"wrote {OUT_PATH}", flush=True)
            return 0

        save_progress(excluded, len(active), "bisecting", elapsed)
        bad = find_one_bad(active)
        excluded.append(bad)
        save_progress(excluded, len(active) - 1, "found_bad", elapsed)
        save_result(seed, excluded, (time.monotonic() - t0) / 60)
        print(f"  bad VA isolated: 0x{bad:x} (excluded total: {len(excluded)})",
              flush=True)

    print(f"\nGAVE UP after {args.max_bad} bad VAs identified", flush=True)
    save_result(seed, excluded, (time.monotonic() - t0) / 60)
    return 2


if __name__ == "__main__":
    sys.exit(main())
