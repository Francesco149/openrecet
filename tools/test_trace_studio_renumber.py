#!/usr/bin/env python3
"""test_trace_studio_renumber.py — BOTH sides renumber into label space.

Guards the 2026-06-10 fix: only retail went through the frame→label renumber, so the
port kept its 0-based frame_<k> names.  When window_start == 0 (the common window) a
0-based name already IS the label, so the gap rode along uncaught — until a
caprange.start > 0 window (merchants-guild, window_start=330) made the label-keyed
build_diff compare port frame N against retail's window_start-later frame N, painting
the ENTIRE diff white over content that was actually 1:1 (the world map).

Pins: (1) renumber_to_label shifts a 0-based PORT dir to window_start and is
idempotent; (2) it still rebases an abs-based RETAIL dir; (3) window_start==0 is a
no-op; (4) the end-to-end repro — port(0-based) + retail(abs) renumbered to the SAME
window_start then build_diff'd pair the SAME moment (identical frames → zero diff),
which BEFORE the fix mispaired and reported max diff.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from trace_studio.transport.convert import renumber_to_label, renumber_retail  # noqa: E402
from trace_studio.analysis.pixeldiff import build_diff  # noqa: E402


def _mk(side_dir: Path, nums, rgb=(0, 0, 0)) -> None:
    from PIL import Image
    (side_dir / "frames").mkdir(parents=True, exist_ok=True)
    colors = rgb if isinstance(rgb, dict) else {n: rgb for n in nums}
    for n in nums:
        Image.new("RGB", (2, 2), colors[n]).save(side_dir / "frames" / f"frame_{n:05d}.png")


def _labels(side_dir: Path):
    return sorted(int("".join(c for c in p.stem if c.isdigit()))
                  for p in (side_dir / "frames").glob("frame_*.png"))


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)

        # ── (1) PORT: 0-based → label space, idempotent ──
        port = d / "a" / "port"
        _mk(port, [0, 1, 2, 3])
        ret = renumber_to_label(port, window_start=330)
        assert _labels(port) == [330, 331, 332, 333], _labels(port)
        assert ret == 0, f"port first-frame number is 0, got {ret}"
        renumber_to_label(port, window_start=330)               # second pass: no-op
        assert _labels(port) == [330, 331, 332, 333], "renumber must be idempotent"

        # ── (2) RETAIL: abs-based → same label space (the original behavior) ──
        retail = d / "b" / "retail"
        _mk(retail, [14340, 14341, 14342, 14343])
        base = renumber_retail(retail, window_start=330)        # alias still works
        assert _labels(retail) == [330, 331, 332, 333], _labels(retail)
        assert base == 14340, f"retail returns the abs base, got {base}"

        # ── (3) window_start == 0 is a no-op (why the port gap stayed hidden) ──
        p0 = d / "c" / "port"
        _mk(p0, [0, 1, 2])
        renumber_to_label(p0, window_start=0)
        assert _labels(p0) == [0, 1, 2], "window_start=0 must not move 0-based frames"

        # ── (4) end-to-end repro: window_start=330, port 0-based + retail abs ──
        # Same content at each moment (matching colors); after BOTH are renumbered to
        # window_start they must pair label-for-label → zero diff.  BEFORE the fix the
        # port stayed 0-based, so the {330..332} intersection paired port's LATER frames
        # against retail's first three → nonzero/max diff over identical content.
        moment_rgb = [(10, 20, 30), (40, 50, 60), (70, 80, 90), (100, 110, 120)]
        ep, er = d / "e" / "port", d / "e" / "retail"
        _mk(ep, [0, 1, 2, 3], {i: moment_rgb[i] for i in range(4)})           # 0-based
        _mk(er, [14340, 14341, 14342, 14343],
            {14340 + i: moment_rgb[i] for i in range(4)})                     # abs
        renumber_to_label(ep, window_start=330)
        renumber_to_label(er, window_start=330)
        assert _labels(ep) == _labels(er) == [330, 331, 332, 333]
        out = build_diff(ep, er, d / "e" / "diff" / "frames", amp=1.0)
        got = sorted(e["frame"] for e in out["per_frame"])
        assert got == [330, 331, 332, 333], f"all four labels pair, got {got}"
        assert not out["unmatched"]["port"] and not out["unmatched"]["retail"], \
            out["unmatched"]
        assert all(e["differ"] == 0 for e in out["per_frame"]), \
            f"same moment == same content == zero diff, got {out['per_frame']}"

        # ── (5) the BROKEN case the fix removes: port LEFT 0-based, retail at ws ──
        # window_start=2; 5 distinct moments per side.  Leaving the port 0-based gives
        # overlapping-but-shifted label sets ({0..4} vs {2..6}) whose non-empty
        # intersection {2,3,4} pairs port-moment-k against retail-moment-(k-2): max diff
        # over identical content — exactly merchants-guild's full-white world map.
        mom = [(11, 0, 0), (0, 22, 0), (0, 0, 33), (44, 44, 0), (0, 55, 55)]
        bp, br = d / "f" / "port", d / "f" / "retail"
        _mk(bp, [0, 1, 2, 3, 4], {i: mom[i] for i in range(5)})              # 0-based
        _mk(br, [9002, 9003, 9004, 9005, 9006],
            {9002 + i: mom[i] for i in range(5)})                            # abs
        renumber_to_label(br, window_start=2)                               # ONLY retail
        broken = build_diff(bp, br, d / "f" / "diff" / "frames", amp=1.0)
        bbad = {e["frame"]: e for e in broken["per_frame"]}
        assert sorted(bbad) == [2, 3, 4], f"mispaired intersection, got {sorted(bbad)}"
        assert all(bbad[L]["differ"] > 0 for L in (2, 3, 4)), \
            "the bug: shifted labels diff identical content as fully different"
        # …and renumbering the port too collapses it back to zero (the fix).
        renumber_to_label(bp, window_start=2)
        fixed = build_diff(bp, br, d / "f" / "diff" / "frames", amp=1.0)
        assert all(e["differ"] == 0 for e in fixed["per_frame"]) and \
            sorted(e["frame"] for e in fixed["per_frame"]) == [2, 3, 4, 5, 6], fixed

    print("OK: trace_studio renumber (port 0-based + retail abs BOTH rebase to label "
          "space for window_start>0; idempotent; window_start=0 no-op; end-to-end "
          "label-true diff over the merchants-guild caprange.start>0 repro)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
