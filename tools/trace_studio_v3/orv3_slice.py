#!/usr/bin/env python3
"""Trace Studio v3 — slice a cached capture to a sub-window (ZERO re-drive).

The P2 cache win: capture retail's window ONCE (a generous full-extent), then serve
any sub-window as a SLICE of the cached container — no retail re-drive across
re-windowing or port-fix loops. This is v2's `--only port` generalized so a *window
change* no longer forces a retail re-drive (E4: retail execution is the whole cost).

Given a cached entry (v3cap.bin + v3ref_*.raw + v3meta.json) and a sub-window in
IDENTITY-OFFSET space (`--window OFFSET:COUNT`, the same anchor-relative offsets the
join uses), this:
  • maps the offset range to kept-frame indices via the entry's stored offset0;
  • re-emits those frames as a STANDALONE container (orv3.slice_window — pulls
    forward content-hash-dedup'd resources first defined before the slice);
  • copies the matching references, renumbered 0-based for the slice;
  • writes a sub-window v3meta.json (its own offset0 = the requested OFFSET);
  • replay-verifies EVERY sliced frame is bit-exact vs its original reference.

No proxy, no Frida, no retail — pure container surgery + the replayer. A re-window
that cost a multi-minute retail drive in v2 is now instant.

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/orv3_slice.py \
      <cache-entry-dir> --window 130:20 [--out DIR] [--no-verify]
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import asdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import orv3       # noqa: E402
import v3cache    # noqa: E402

REPLAY_EXE = Path(__file__).resolve().parent / "replay" / "replay.exe"


def wslpath_w(p: Path) -> str:
    return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True,
                          check=True).stdout.strip()


def main() -> int:
    ap = argparse.ArgumentParser(description="slice a cached v3 capture to a sub-window (zero re-drive).")
    ap.add_argument("entry", type=Path, help="cache entry dir (v3cap.bin + v3ref_*.raw + v3meta.json)")
    ap.add_argument("--window", metavar="OFFSET:COUNT", required=True,
                    help="sub-window in identity-offset space, e.g. 130:20 = offsets 130..149.")
    ap.add_argument("--out", type=Path, default=None,
                    help="output dir for the slice (default: <entry>/slice-OFFSET-COUNT/).")
    ap.add_argument("--no-verify", action="store_true",
                    help="slice only; skip the per-frame bit-exact replay check.")
    args = ap.parse_args()

    try:
        off_a, n = (int(x) for x in args.window.split(":"))
    except ValueError:
        raise SystemExit(f"--window wants OFFSET:COUNT (got {args.window!r})")

    meta = v3cache.load_meta(args.entry)
    cont = orv3.Container.load(args.entry / "v3cap.bin")
    ext_lo, ext_hi = meta.offset0, meta.offset0 + meta.count          # cached extent [lo, hi)
    if not (ext_lo <= off_a and off_a + n <= ext_hi):
        raise SystemExit(f"sub-window offsets [{off_a},{off_a + n}) outside the cached extent "
                         f"[{ext_lo},{ext_hi}) — capture a wider full-extent or narrow the window")

    a = off_a - meta.offset0                                          # kept-index range
    b = a + n
    out = args.out or (args.entry / f"slice-{off_a}-{n}")
    out.mkdir(parents=True, exist_ok=True)
    for f in [out / "v3cap.bin", *out.glob("v3ref_*.raw"), out / "v3meta.json"]:
        f.unlink(missing_ok=True)

    # re-emit the sub-window as a standalone container + copy its references 0-based
    (out / "v3cap.bin").write_bytes(cont.slice_window(a, b))
    for i in range(n):
        shutil.copy2(args.entry / f"v3ref_{a + i:03d}.raw", out / f"v3ref_{i:03d}.raw")
    sub = v3cache.FrameIdentity(
        side=meta.side, scenario=meta.scenario, anchor=meta.anchor,
        anchor_occ=meta.anchor_occ, anchor_frame=meta.anchor_frame,
        offset0=off_a, count=n, present_first=cont.frames[a].present)
    (out / "v3meta.json").write_text(json.dumps(asdict(sub), indent=1))

    cap_mb = (out / "v3cap.bin").stat().st_size / 1048576
    print(f"[slice] {meta.side} {meta.anchor}#{meta.anchor_occ}  offsets {off_a}..{off_a + n - 1} "
          f"(kept idx {a}..{b - 1} of {meta.count}) → {out}")
    print(f"[slice] {n} frames · container {cap_mb:.1f} MB · ZERO retail re-drive "
          f"(sliced from the cached full-extent)")

    if args.no_verify:
        return 0

    if not REPLAY_EXE.exists():
        raise SystemExit(f"replayer not built: {REPLAY_EXE} — `make` in replay/")
    cap_w = wslpath_w(out / "v3cap.bin")
    chk_w = wslpath_w(out / "v3slice_chk.raw")
    npass = nfail = 0
    first_fail = None
    print(f"[verify] replaying all {n} sliced frames …")
    for i in range(n):
        r = subprocess.run([str(REPLAY_EXE), cap_w, wslpath_w(out / f"v3ref_{i:03d}.raw"),
                            str(i), chk_w], capture_output=True, text=True)
        db = None
        for ln in (r.stdout + r.stderr).splitlines():
            if "differing bytes" in ln:
                db = ln.split(":", 1)[1].split("(")[0].strip()
        if db == "0":
            npass += 1
        else:
            nfail += 1
            if first_fail is None:
                first_fail = f"slice frame {i} (offset {off_a + i}): differing bytes={db!r}"
    print("=" * 48)
    print(f"  SLICE BIT-EXACT: {npass} / {n}   |   FAILED: {nfail}")
    if first_fail:
        print(f"  first failure: {first_fail}")
    print(f"  VERDICT: {'ALL SLICED FRAMES BIT-EXACT — cache slice is sound' if nfail == 0 else 'DIVERGENT'}")
    print("=" * 48)
    return 0 if nfail == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
