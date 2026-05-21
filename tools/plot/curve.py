#!/usr/bin/env python3
"""
tools/plot/curve.py — tiny PIL-based 1D-curve plotter.

Used to render small visual sanity-checks of pure-math curves (the BGM
fade-in ramp, etc.) without pulling in matplotlib. Inputs are a list
of (x, y) points; outputs are a fixed-size PNG with axes, x/y range
labels, and the curve drawn as connected line segments.

Usage:
    from curve import plot_curve
    plot_curve([(0, -10000), (1, -5391), ...], out_path,
               title="BGM fade-in",
               xlabel="frame counter", ylabel="centibel")
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def _font():
    try:
        return ImageFont.load_default()
    except Exception:
        return None


def plot_curve(points, out_path: Path | str, *,
               title: str = "", xlabel: str = "", ylabel: str = "",
               size=(640, 360)) -> Path:
    """Render `points = [(x, y), ...]` as a connected line plot.

    Linear axes, autoscaled to the data range. Tiny labels in the
    corners — no ticks, no legend. Just enough to confirm visually
    that a curve has the shape you expect.
    """
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)

    w, h = size
    pad_l, pad_r, pad_t, pad_b = 60, 16, 28, 36
    plot_w = w - pad_l - pad_r
    plot_h = h - pad_t - pad_b

    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    if x1 == x0:
        x1 = x0 + 1
    if y1 == y0:
        y1 = y0 + 1

    img = Image.new("RGB", (w, h), (16, 16, 20))
    draw = ImageDraw.Draw(img)
    font = _font()

    # Plot frame.
    draw.rectangle(
        (pad_l, pad_t, w - pad_r, h - pad_b),
        outline=(80, 80, 90), width=1,
    )

    # Zero-crossing grid line if y range straddles zero.
    if y0 < 0 < y1:
        zy = pad_t + plot_h - int((0 - y0) / (y1 - y0) * plot_h)
        draw.line((pad_l, zy, w - pad_r, zy), fill=(60, 60, 64), width=1)

    def to_px(x, y):
        px = pad_l + int((x - x0) / (x1 - x0) * plot_w)
        py = pad_t + plot_h - int((y - y0) / (y1 - y0) * plot_h)
        return px, py

    # Curve.
    coords = [to_px(x, y) for (x, y) in points]
    if len(coords) >= 2:
        draw.line(coords, fill=(220, 200, 60), width=2)
    # Mark each sample as a small dot so individual frames are visible.
    for (px, py) in coords:
        draw.ellipse((px - 3, py - 3, px + 3, py + 3),
                     fill=(255, 240, 120))

    # Labels.
    if title:
        draw.text((pad_l, 6), title, fill=(220, 220, 230), font=font)
    if xlabel:
        draw.text((pad_l, h - pad_b + 18), xlabel,
                  fill=(180, 180, 190), font=font)
    if ylabel:
        draw.text((4, pad_t - 4), ylabel,
                  fill=(180, 180, 190), font=font)
    # Range hints.
    draw.text((pad_l, h - pad_b + 4), f"{x0}",
              fill=(160, 160, 170), font=font)
    draw.text((w - pad_r - 30, h - pad_b + 4), f"{x1}",
              fill=(160, 160, 170), font=font)
    draw.text((4, pad_t + plot_h - 12), f"{y0}",
              fill=(160, 160, 170), font=font)
    draw.text((4, pad_t + 2), f"{y1}",
              fill=(160, 160, 170), font=font)

    img.save(out, optimize=True)
    return out
