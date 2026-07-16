#!/usr/bin/env python3
"""Trace Studio v3 — shared bit-exact replay verification (raw refs OR hash refs).

One verifier for every caller (port_capture, retail_capture/house_capture,
orv3_slice, orv3_window): given a capture dir (v3cap.bin + references), prove every
kept frame re-renders bit-exactly.

Two reference shapes, decided by what the proxy wrote:
  • v3refs.txt (refhash=1) — ONE resident replay.exe --verify-hashes run for the
    whole window: render every listed frame, compare fnv1a-64. The
    thousands-of-frames path: no per-frame process spawn, no GBs of raw pixels.
  • v3ref_NNN.raw — the legacy per-frame loop (one replay.exe spawn per frame,
    byte compare). Fine for tens of frames; kept for old captures/caches.
"""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

REPLAY_EXE = Path(__file__).resolve().parent / "replay" / "replay.exe"


def wslpath_w(p: Path) -> str:
    return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True,
                          check=True).stdout.strip()


def _verify_hashes(cap: Path, refs: Path, quiet: bool) -> tuple[int, int, str | None]:
    # replay.exe launches from WSL intermittently fail with a vsock error and NO
    # output (no HASHVERIFY summary).  That is a launch flake, NOT a verify
    # failure — a real mismatch ALWAYS prints "HASHVERIFY pass=.. fail=.." — so
    # retrying on a missing summary can't mask a finding.
    out = ""
    for _ in range(6):
        r = subprocess.run([str(REPLAY_EXE), wslpath_w(cap), "--verify-hashes",
                            wslpath_w(refs)], capture_output=True, text=True)
        out = r.stdout + r.stderr
        m = re.search(r"HASHVERIFY pass=(\d+) fail=(\d+) total=(\d+)", out)
        if m:
            break
    if not m:
        return 0, -1, f"--verify-hashes produced no summary after retries (exit {r.returncode}):\n{out[-2000:]}"
    npass, nfail = int(m.group(1)), int(m.group(2))
    first = None
    fm = re.search(r"first failure: (.+)", out)
    if fm:
        first = fm.group(1).strip()
    if not quiet:
        for ln in out.splitlines():
            if ln.startswith("FAIL "):
                print("  " + ln)
    return npass, nfail, first


def _verify_raws(cap: Path, refs_dir: Path, n: int, quiet: bool) -> tuple[int, int, str | None]:
    cap_w = wslpath_w(cap)
    chk_w = wslpath_w(refs_dir / "v3replay_chk.raw")
    npass = nfail = 0
    first = None
    for i in range(n):
        ref = refs_dir / f"v3ref_{i:03d}.raw"
        # Retry the launch (not the comparison) when replay.exe produces no
        # "differing bytes" line — that's the WSL vsock launch flake, not a real
        # diff (which always prints the line).
        db = None
        for _ in range(6):
            r = subprocess.run([str(REPLAY_EXE), cap_w, wslpath_w(ref), str(i), chk_w],
                               capture_output=True, text=True)
            for ln in (r.stdout + r.stderr).splitlines():
                if "differing bytes" in ln:
                    db = ln.split(":", 1)[1].split("(")[0].strip()
            if db is not None:
                break
        if db == "0":
            npass += 1
        else:
            nfail += 1
            if first is None:
                first = f"frame {i}: differing bytes={db!r} (exit {r.returncode})"
    return npass, nfail, first


def verify_counts(d: Path, n: int | None = None, *, label: str = "",
                  quiet: bool = False) -> tuple[int, int, int]:
    """Verify every kept frame in capture dir `d` re-renders bit-exactly. Returns
    (npass, nfail, total). `n` (kept count) is required for the raw-ref loop; with
    a v3refs.txt it is only cross-checked (the file carries its own frame list)."""
    if not REPLAY_EXE.exists():
        raise SystemExit(f"replayer not built: {REPLAY_EXE} — `nix develop --command make` in replay/")
    cap = d / "v3cap.bin"
    refs = d / "v3refs.txt"
    if refs.exists():
        n_listed = sum(1 for ln in refs.read_text().splitlines() if ln.startswith("REF "))
        if not quiet:
            print(f"[verify] {label or d.name}: hash-verify {n_listed} kept frames (resident, one process) …")
        npass, nfail, first = _verify_hashes(cap, refs, quiet)
        total = n_listed
        if n is not None and n_listed != n:
            print(f"  WARNING: v3refs.txt lists {n_listed} frames, expected {n}")
    else:
        if n is None:
            n = len(list(d.glob("v3ref_*.raw")))
        if not quiet:
            print(f"[verify] {label or d.name}: replaying all {n} kept frames (raw refs) …")
        npass, nfail, first = _verify_raws(cap, d, n, quiet)
        total = n
    if not quiet:
        print("=" * 48)
        print(f"  BIT-EXACT: {npass} / {total}   |   FAILED: {nfail}")
        if first:
            print(f"  first failure: {first}")
        replay = "REPLAY_EXACT" if (nfail == 0 and npass == total and total > 0) else "REPLAY_DIVERGENT"
        print(f"  REPLAY VERDICT: {replay}" + ("  *** GO ***" if replay == "REPLAY_EXACT" else ""))
        print("  (same-side recorder/replayer fidelity — NOT a cross-target parity claim)")
        print("=" * 48)
    return npass, nfail, total


def verify_dir(d: Path, n: int | None = None, *, label: str = "", quiet: bool = False) -> int:
    """verify_counts as an exit code: 0 iff every frame passed (and there was ≥1)."""
    npass, nfail, total = verify_counts(d, n, label=label, quiet=quiet)
    return 0 if (nfail == 0 and npass == total and total > 0) else 1


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="verify a v3 capture dir bit-exact (hash or raw refs).")
    ap.add_argument("dir", type=Path, help="capture dir (v3cap.bin + v3refs.txt or v3ref_*.raw)")
    ap.add_argument("-n", type=int, default=None, help="expected kept-frame count")
    args = ap.parse_args()
    raise SystemExit(verify_dir(args.dir, args.n))
