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
import v3verify   # noqa: E402

REPLAY_EXE = Path(__file__).resolve().parent / "replay" / "replay.exe"


def wslpath_w(p: Path) -> str:
    return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True,
                          check=True).stdout.strip()


def slice_refs(entry: Path, out: Path, a: int, b: int) -> None:
    """Copy the references for kept-index range [a, b) into `out`, renumbered
    0-based. Handles BOTH shapes: v3refs.txt hash lines (subset + renumber the
    kept field — the n-frames path) and v3ref_NNN.raw files (copy those that
    exist; sparse when the capture ran refhash + refraw_every)."""
    refs = entry / "v3refs.txt"
    if refs.exists():
        kept_lines = []
        for ln in refs.read_text().splitlines():
            parts = ln.split(maxsplit=2)
            if len(parts) == 3 and parts[0] == "REF" and parts[1].isdigit():
                k = int(parts[1])
                if a <= k < b:
                    kept_lines.append(f"REF {k - a} {parts[2]}")
        (out / "v3refs.txt").write_text("".join(ln + "\n" for ln in kept_lines))
    for i in range(a, b):
        src = entry / f"v3ref_{i:03d}.raw"
        if src.exists():
            shutil.copy2(src, out / f"v3ref_{i - a:03d}.raw")


def slice_entry(entry: Path, off_a: int, n: int, out: Path | None = None,
                verify: bool = True, *, quiet: bool = False) -> tuple[Path, int, int]:
    """Re-emit a cached entry's sub-window [off_a, off_a+n) (identity-offset space)
    as a STANDALONE container at `out` (default <entry>/slice-OFFSET-COUNT/), and —
    unless verify=False — replay every sliced frame and byte-compare to its original
    reference. Returns (out_dir, npass, nfail); nfail<0 means "not verified".

    This is the pure mechanism the CLI main() and the orv3_window orchestrator both
    call: a re-window that cost a multi-minute retail drive in v2 is now instant.
    """
    def say(*a):
        if not quiet:
            print(*a)

    meta = v3cache.load_meta(entry)
    cont = orv3.Container.load(entry / "v3cap.bin")
    # extent containment is in ARM space (the drive request): a mid-window
    # suppressed load makes kept < armed without shrinking the covered window.
    ext_lo, ext_hi = meta.eff_arm_offset, meta.eff_arm_offset + meta.eff_arm_count
    if not (ext_lo <= off_a and off_a + n <= ext_hi):
        raise ValueError(f"sub-window offsets [{off_a},{off_a + n}) outside the cached extent "
                         f"[{ext_lo},{ext_hi}) — capture a wider full-extent or narrow the window")

    # arm offsets → kept-index range by PRESENT (kept frames may be non-contiguous
    # in present space once loads are suppressed mid-window; for a contiguous legacy
    # entry this reduces to off_a - offset0 exactly).
    from bisect import bisect_left
    presents = [f.present for f in cont.frames]
    a = bisect_left(presents, meta.anchor_frame + off_a)
    b = bisect_left(presents, meta.anchor_frame + off_a + n)
    if a >= b:
        raise ValueError(f"sub-window [{off_a},{off_a + n}) contains no kept frames "
                         f"(all suppressed/load frames?) — widen the window")
    out = out or (entry / f"slice-{off_a}-{n}")
    out.mkdir(parents=True, exist_ok=True)
    for f in [out / "v3cap.bin", *out.glob("v3ref_*.raw"),
              out / "v3refs.txt", out / "v3meta.json", out / "call_trace.jsonl"]:
        f.unlink(missing_ok=True)

    # re-emit the sub-window as a standalone container + copy its references 0-based
    (out / "v3cap.bin").write_bytes(cont.slice_window(a, b))
    slice_refs(entry, out, a, b)
    kept = b - a
    sub = v3cache.FrameIdentity(
        side=meta.side, scenario=meta.scenario, anchor=meta.anchor,
        anchor_occ=meta.anchor_occ, anchor_frame=meta.anchor_frame,
        offset0=off_a, count=kept, present_first=cont.frames[a].present,
        arm_offset=off_a, arm_count=n, anchors=meta.anchors)
    (out / "v3meta.json").write_text(json.dumps(asdict(sub), indent=1))
    # Carry the engine-state sidecar forward (a --state capture). It's identity-keyed
    # and the view filters by the slice's columns, so the full-extent call_trace.jsonl
    # serves any sub-window unchanged — out-of-window rows simply match no column.
    ct = entry / "call_trace.jsonl"
    if ct.exists():
        shutil.copy2(ct, out / "call_trace.jsonl")

    cap_mb = (out / "v3cap.bin").stat().st_size / 1048576
    say(f"[slice] {meta.side} {meta.anchor}#{meta.anchor_occ}  offsets {off_a}..{off_a + n - 1} "
        f"(kept idx {a}..{b - 1} of {meta.count}) → {out}")
    say(f"[slice] {kept} kept frame(s) · container {cap_mb:.1f} MB · ZERO retail re-drive "
        f"(sliced from the cached full-extent)")

    if not verify:
        return out, -1, -1

    npass, nfail, _total = v3verify.verify_counts(out, kept, label=f"slice {off_a}:{n}",
                                                  quiet=quiet)
    return out, npass, nfail


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

    try:
        _out, _npass, nfail = slice_entry(args.entry, off_a, n, out=args.out,
                                          verify=not args.no_verify)
    except ValueError as e:
        raise SystemExit(str(e))
    return 0 if nfail <= 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
