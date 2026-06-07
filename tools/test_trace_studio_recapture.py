#!/usr/bin/env python3
"""test_trace_studio_recapture.py — `--only port` reuses cached retail robustly.

Guards the fix for "after a port-only re-capture, trace_studio reported retail had 0
frames" even though the cached retail frames + call-trace were on disk. Root cause:
`have_retail_frames` was gated on `retail_base`, which under `--only port` came solely
from `old_manifest["retail"]["base_abs"]` — a field a prior interrupted/partial
capture can leave None. So a perfectly good cached retail was discarded (no diff/
verdict, spurious "retail captured 0 frames").

The fix decouples "do we have retail" from the manifest base (it is now frame
existence) and, when the manifest lost the base, recovers it from the cached frames
on disk via `_cached_retail_base`. This pins that recovery.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from trace_studio.capture import _cached_retail_base  # noqa: E402


def _mk_frames(retail_dir: Path, nums: list[int]) -> None:
    (retail_dir / "frames").mkdir(parents=True, exist_ok=True)
    for n in nums:
        (retail_dir / "frames" / f"frame_{n:05d}.png").write_bytes(b"")


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)

        # 1. an already-renumbered (0-based) cache → base is 0 (correct for those
        #    on-disk frames; the ABS base is genuinely gone from the filenames, but
        #    the diff/verdict pair on the 0-based frames so 0 is right).
        r0 = d / "renumbered"
        _mk_frames(r0, [0, 1, 2, 3])
        assert _cached_retail_base(r0) == 0, "0-based cache → base 0"

        # 2. an un-renumbered (absolute) cache → its min absolute frame number (the
        #    real rebase base, the same value renumber_retail would return).
        rA = d / "absolute"
        _mk_frames(rA, [14188, 14189, 14190])
        assert _cached_retail_base(rA) == 14188, "absolute cache → min abs base"

        # 3. genuinely-empty retail dir → None (the ONLY case that should still read
        #    as "no cached retail to reuse" on a port-only re-capture).
        rE = d / "empty"
        (rE / "frames").mkdir(parents=True)
        assert _cached_retail_base(rE) is None, "no frames → None"

        # 4. no frames subdir at all → None (don't crash).
        rN = d / "nodir"
        rN.mkdir()
        assert _cached_retail_base(rN) is None, "no frames dir → None"

    print("OK: trace_studio recapture (--only port recovers cached retail base from "
          "disk when the manifest lost it; empty → None)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
