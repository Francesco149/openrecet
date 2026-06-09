#!/usr/bin/env python3
"""test_trace_studio_pixeldiff.py — build_diff pairs frames by LABEL.

Guards the 2026-06-10 fix: build_diff paired by dense ordinal ("the i-th file on
each side"), a workaround from the era when retail frames kept raw abs numbers.
After the label unification (both sides renumbered to anchor-relative labels),
ordinal pairing became the bug — with a kept-count mismatch (item-display-2: port
1845 vs retail 1842) every diff after the first seam compared label L vs L−k, so
the diff frames ghosted everything that MOVES (the bg-window NPCs) while the
same-label sides were 1:1 (user-reported at label 1420).

Pins: (1) label-intersection pairing + unmatched-label reporting; (2) the
ordinal fallback for pre-unification sessions (no common labels at all).
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from trace_studio.analysis.pixeldiff import build_diff  # noqa: E402


def _mk_frames(side_dir: Path, labels: dict[int, tuple[int, int, int]]) -> None:
    """Write 2x2 solid-color frame_<label>.png files."""
    from PIL import Image
    (side_dir / "frames").mkdir(parents=True, exist_ok=True)
    for n, rgb in labels.items():
        Image.new("RGB", (2, 2), rgb).save(side_dir / "frames" / f"frame_{n:05d}.png")


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)

        # ── label pairing: gappy kept-sets pair by label, not by index ──
        # port has labels {10, 11, 13, 14}; retail {10, 12, 13}.  Ordinal pairing
        # would compare 11↔12 and 13↔13 by accident of position; label pairing
        # must produce exactly {10, 13} and report the rest unmatched.
        port, retail = d / "p" / "port", d / "p" / "retail"
        _mk_frames(port,   {10: (0, 0, 0), 11: (1, 1, 1), 13: (40, 40, 40), 14: (3, 3, 3)})
        _mk_frames(retail, {10: (0, 0, 0), 12: (2, 2, 2), 13: (10, 10, 10)})
        out = build_diff(port, retail, d / "p" / "diff" / "frames", amp=1.0)
        got = sorted(e["frame"] for e in out["per_frame"])
        assert got == [10, 13], f"label pairing: expected diffs for [10, 13], got {got}"
        assert out["unmatched"]["port"] == [11, 14], out["unmatched"]
        assert out["unmatched"]["retail"] == [12], out["unmatched"]
        assert (d / "p" / "diff" / "frames" / "frame_00013.png").exists()
        assert not (d / "p" / "diff" / "frames" / "frame_00011.png").exists(), \
            "unmatched port label must not get a diff frame"
        # label 10 is identical (0 differing px); label 13 differs on all 4.
        by = {e["frame"]: e for e in out["per_frame"]}
        assert by[10]["differ"] == 0, by[10]
        assert by[13]["differ"] == 4, by[13]
        assert by[13]["gt8"] == 4, "Δ30 > 8 on every px"

        # ── ordinal fallback: pre-unification retail naming (no common labels) ──
        port2, retail2 = d / "q" / "port", d / "q" / "retail"
        _mk_frames(port2,   {0: (0, 0, 0), 1: (5, 5, 5)})
        _mk_frames(retail2, {14567: (0, 0, 0), 14568: (5, 5, 5), 14569: (9, 9, 9)})
        out2 = build_diff(port2, retail2, d / "q" / "diff" / "frames", amp=1.0)
        got2 = sorted(e["frame"] for e in out2["per_frame"])
        assert got2 == [0, 1], f"ordinal fallback names by port label, got {got2}"
        assert out2["unmatched"].get("ordinal_fallback") is True, out2["unmatched"]
        by2 = {e["frame"]: e for e in out2["per_frame"]}
        assert by2[0]["differ"] == 0 and by2[1]["differ"] == 0, \
            "fallback pairs i-th with i-th (identical colors here)"

    print("OK: trace_studio pixeldiff (build_diff pairs by label with unmatched "
          "reporting; pre-unification sessions fall back to ordinal pairing)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
