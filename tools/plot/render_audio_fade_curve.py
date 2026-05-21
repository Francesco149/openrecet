#!/usr/bin/env python3
"""
tools/plot/render_audio_fade_curve.py — render the BGM fade-in ramp
to runs/audio-fade-curve.png.

The curve mirrors src/audio_fade.c::audio_fade_compute. We don't link
the C — we re-implement the same formula in Python and assert the
endpoints, so a divergence between the two impls would show up as
a wonky curve shape.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).parent))

from curve import plot_curve  # noqa: E402


SILENCE_CENTIBEL = -10000
MATH_FLOOR = -9600
SCALE = 9600.0
ANGLE_MAX = 2 * math.pi / 5
FRAME_COUNT = 10  # 0..9


def fade_compute(frame: int, target_centibel: int = 0) -> int:
    if frame <= 0:
        return SILENCE_CENTIBEL
    if frame >= FRAME_COUNT - 1:
        return target_centibel
    angle = (FRAME_COUNT - 1 - frame) * ANGLE_MAX / 9.0
    return int(math.cos(angle) * (target_centibel + SCALE) - SCALE)


def main() -> int:
    points = [(f, fade_compute(f, 0)) for f in range(FRAME_COUNT)]
    # Anchor checks — should match the C tests' acceptance.
    assert points[0]  == (0, -10000), points[0]
    assert points[9]  == (9, 0),      points[9]
    # Monotone 1..9 inclusive.
    for i in range(2, 10):
        assert points[i][1] > points[i - 1][1], (i, points)
    out = ROOT / "runs" / "audio-fade-curve.png"
    plot_curve(
        points,
        out,
        title="BGM fade-in (audio_fade_compute, target=0)",
        xlabel="frame counter (0=silence, 9=target)",
        ylabel="centibel",
    )
    print(f"wrote {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
