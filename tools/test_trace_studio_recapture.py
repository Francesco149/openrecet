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

from trace_studio.capture import _cached_retail_base, _resolve_want_retail  # noqa: E402


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

    # ── _resolve_want_retail: a session WITH cached retail always keeps it ──
    # (regression 1: `--target openrecet --only port` used to drop the cached retail
    # from the session — videos/diff/anchors/base lost — because want_retail keyed on
    # --target alone.  regression 2, 2026-06-10 item-display-2: a session whose STORED
    # target had been poisoned to "openrecet" by an earlier port-only iteration made a
    # plain `recapture` (only=both) skip the retail leg with no error and write a
    # port-only manifest — the studio showed "no retail frames" while 1842 cached
    # retail frames sat intact on disk.  Rule now: target=="both" OR cached retail ⇒
    # the session is two-sided; the manifest also stores sidedness (not the transient
    # flag) so the poison cannot recur.)
    assert _resolve_want_retail("both", "both", False) is True, "target=both → retail"
    assert _resolve_want_retail("both", "port", False) is True, "target=both → retail"
    # port-only re-run of a session that has cached retail keeps (reuses) it.
    assert _resolve_want_retail("openrecet", "port", True) is True, \
        "--only port + cached retail → preserve it"
    # a genuinely port-only session (no cached retail) stays port-only.
    assert _resolve_want_retail("openrecet", "port", False) is False, \
        "--only port + no cached retail → still none"
    assert _resolve_want_retail("openrecet", "both", False) is False, \
        "full re-run of a genuinely port-only session → still none"
    # a FULL re-run of a session with cached retail RE-CAPTURES retail even under a
    # stale port-only stored target (the 2026-06-10 fix).  Dropping a session's
    # retail side is now an explicit act (delete retail/), not a flag side-effect.
    assert _resolve_want_retail("openrecet", "both", True) is True, \
        "--only both + cached retail → two-sided (re-capture retail)"

    print("OK: trace_studio recapture (--only port recovers cached retail base from "
          "disk when the manifest lost it; empty → None; want_retail keeps a session "
          "two-sided whenever cached retail exists, whatever the stored target)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
