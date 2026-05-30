#!/usr/bin/env python3
"""tools/compose_comparison.py — labelled side-by-side image composer.

Pastes two captures (BMP or PNG) side by side under a label strip and writes
a PNG.  Used for the README HOUSE hero (port | retail) and any other
two-panel comparison.  Distinct from tools/pixel_diff.py, which adds an
amplified white-diff panel for bit-level parity work — this one is the clean
presentation montage.

    compose_comparison.py --a port.bmp --b retail.bmp \
        --labels "OpenRecet,Retail" --out docs/img/house-comparison.png

The label font is a real bold TrueType resolved at run time (fc-match, then a
nix-store glob); we never fall back to PIL's tiny bitmap default silently —
if no TTF is found the script errors so a regen can't quietly ship
microscopic labels (the 2026-05-30 README bug).  Default --font-size is 44
(big on purpose — the labels must read in a shrunk README thumbnail).
"""

from __future__ import annotations

import argparse
import glob
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def resolve_bold_font(size: int) -> ImageFont.FreeTypeFont:
    """Return a bold TrueType font at `size`, or raise.

    Tries, in order: `fc-match` for a bold sans family, then a glob of the
    nix store / system font dirs for any *Bold*.ttf.  Deliberately does NOT
    fall back to ImageFont.load_default() — that bitmap font ignores `size`
    and silently produced unreadable labels in the first README regen.
    """
    cands: list[str] = []

    # fontconfig knows the system's preferred bold sans — best first guess.
    for family in ("DejaVu Sans:bold", "Sans:bold", "sans-serif:bold"):
        try:
            r = subprocess.run(["fc-match", "-f", "%{file}", family],
                               capture_output=True, text=True, timeout=5)
            if r.returncode == 0 and r.stdout.strip().endswith(
                    (".ttf", ".otf")):
                cands.append(r.stdout.strip())
        except (OSError, subprocess.SubprocessError):
            pass

    # Glob fallbacks (nix profiles + system X11 fonts). DejaVu/Liberation
    # first (clean sans), then any bold TTF.
    patterns = [
        "/nix/store/*/share/fonts/**/DejaVuSans-Bold.ttf",
        "/nix/store/*/share/fonts/**/LiberationSans-Bold.ttf",
        "/run/current-system/sw/share/X11/fonts/*Bold*.ttf",
        "/nix/store/*/share/fonts/**/*Bold*.ttf",
        "/usr/share/fonts/**/*Bold*.ttf",
    ]
    for pat in patterns:
        cands.extend(sorted(glob.glob(pat, recursive=True)))

    for path in cands:
        try:
            return ImageFont.truetype(path, size)
        except (OSError, ValueError):
            continue

    raise SystemExit(
        "compose_comparison: no bold TrueType font found (tried fc-match + "
        "nix-store/system globs). Install one or pass --font <path.ttf>.")


def parse_color(s: str) -> tuple[int, int, int]:
    """`R-G-B` (e.g. 120-230-120) → (R,G,B)."""
    parts = s.split("-")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(f"colour wants R-G-B, got {s!r}")
    return tuple(int(p) for p in parts)  # type: ignore[return-value]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="labelled side-by-side comparison composer",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", required=True, help="left panel (BMP/PNG)")
    ap.add_argument("--b", required=True, help="right panel (BMP/PNG)")
    ap.add_argument("--out", required=True, help="output PNG path")
    ap.add_argument("--labels", default="OpenRecet,Retail",
                    help="comma list: left,right label text "
                         "(default %(default)s)")
    ap.add_argument("--label-colors", default="120-230-120,235-235-235",
                    help="comma list of R-G-B per label "
                         "(default green,white)")
    ap.add_argument("--font-size", type=int, default=44,
                    help="label point size (default %(default)s — big so the "
                         "labels survive README downscaling)")
    ap.add_argument("--font", default=None,
                    help="explicit TTF path (skips auto-resolution)")
    ap.add_argument("--gap", type=int, default=6,
                    help="px between the two panels (default %(default)s)")
    ap.add_argument("--width", type=int, default=1200,
                    help="final montage width in px; 0 = no downscale "
                         "(default %(default)s)")
    ap.add_argument("--bg", type=parse_color, default=(12, 12, 16),
                    help="background/strip colour R-G-B (default 12-12-16)")
    args = ap.parse_args(argv)

    a = Image.open(args.a).convert("RGB")
    b = Image.open(args.b).convert("RGB")
    if a.size != b.size:
        # Match heights; panels of differing size still compose, but warn.
        print(f"compose_comparison: panel sizes differ a={a.size} b={b.size}",
              file=sys.stderr)
    w, h = a.size
    bw, bh = b.size

    if args.font:
        font = ImageFont.truetype(args.font, args.font_size)
    else:
        font = resolve_bold_font(args.font_size)

    labels = (args.labels.split(",") + ["", ""])[:2]
    colors = [parse_color(c) for c in args.label_colors.split(",")]
    colors = (colors + [(235, 235, 235), (235, 235, 235)])[:2]

    # Label strip tall enough for the chosen font + a little padding.
    pad = max(6, args.font_size // 4)
    lab = args.font_size + 2 * pad
    panel_h = max(h, bh)
    sheet = Image.new("RGB", (w + args.gap + bw, panel_h + lab), args.bg)
    sheet.paste(a, (0, lab))
    sheet.paste(b, (w + args.gap, lab))

    d = ImageDraw.Draw(sheet)
    d.text((pad, pad), labels[0], fill=colors[0], font=font)
    d.text((w + args.gap + pad, pad), labels[1], fill=colors[1], font=font)

    out = sheet
    if args.width and args.width > 0 and sheet.width != args.width:
        scale = args.width / sheet.width
        out = sheet.resize(
            (args.width, round(sheet.height * scale)), Image.LANCZOS)

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    out.save(args.out)
    print(f"compose_comparison: wrote {args.out} {out.size} "
          f"(font {Path(getattr(font, 'path', '?')).name}@{args.font_size})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
