#!/usr/bin/env python3
"""
tools/contact-sheet.py — render a downscaled grid of screenshots.

Two modes:

  1. Single set:  --src DIR  →  one grid of every frame in DIR.
  2. Diff:        --left DIR --right DIR  →  per-frame side-by-side rows.

Optional --zoom LEFT,TOP,WIDTH,HEIGHT extracts the same crop from every
input at full resolution and appends it as a separate detail grid below
the main one. Useful when small differences need inspection without me
having to load full-res images into context.

Output: a single PNG written to --out (default: contact-sheet.png).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageFile

# Our 32-bit BI_RGB BMPs from openrecet are structurally valid but PIL is
# strict about reading them (treats the X-padding byte as alpha and trips
# its bounds check). They are NOT actually truncated. Override.
ImageFile.LOAD_TRUNCATED_IMAGES = True


def list_images(d: Path) -> list[Path]:
    exts = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}
    return sorted(p for p in d.iterdir() if p.suffix.lower() in exts)


def label_strip(text: str, width: int, height: int = 18,
                font_size: int | None = None) -> Image.Image:
    """Strip with the label text. `height` is the strip's pixel height;
    `font_size` (Pillow 10+) controls the rendered text size — pass it
    when the strip is taller than the default 18 so the text scales
    along with the strip instead of staying tiny. None = native default
    (~10px bitmap font), which matches the legacy small-strip look."""
    img = Image.new("RGB", (width, height), (24, 24, 28))
    draw = ImageDraw.Draw(img)
    font = None
    try:
        if font_size:
            font = ImageFont.load_default(size=font_size)
        else:
            font = ImageFont.load_default()
    except Exception:
        font = None
    # Top padding scales with font: 2px for the default 10px bitmap,
    # roughly font_size/8 for the resized vector font.
    pad_y = 2 if not font_size else max(2, font_size // 8)
    draw.text((4, pad_y), text, fill=(220, 220, 220), font=font)
    return img


def thumb(path: Path, tile_w: int, tile_h: int) -> Image.Image:
    img = Image.open(path).convert("RGB")
    img.thumbnail((tile_w, tile_h), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (tile_w, tile_h), (0, 0, 0))
    canvas.paste(img, ((tile_w - img.width) // 2, (tile_h - img.height) // 2))
    return canvas


def crop_full(path: Path, rect: tuple[int, int, int, int]) -> Image.Image:
    img = Image.open(path).convert("RGB")
    l, t, w, h = rect
    return img.crop((l, t, l + w, t + h))


def grid(tiles: list[Image.Image], labels: list[str], cols: int,
         label_h: int = 18, font_size: int | None = None) -> Image.Image:
    """Compose tiles into a `cols`-wide grid with a per-tile label
    strip on top. `label_h` (pixels) sizes the strip; `font_size`
    sizes the rendered label text via PIL.ImageFont.load_default(size).
    Defaults preserve the small-strip look for legacy callers."""
    if not tiles:
        raise SystemExit("no tiles to compose")
    tw, th = tiles[0].size
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new(
        "RGB", (cols * tw, rows * (th + label_h)), (12, 12, 14)
    )
    for i, (tile, lbl) in enumerate(zip(tiles, labels)):
        r, c = divmod(i, cols)
        x, y = c * tw, r * (th + label_h)
        sheet.paste(label_strip(lbl, tw, label_h, font_size=font_size), (x, y))
        sheet.paste(tile, (x, y + label_h))
    return sheet


def side_by_side(
    left: Path, right: Path, tile_w: int, tile_h: int
) -> tuple[Image.Image, list[str]]:
    """Pair up files by sorted filename. Emit a grid with 2 columns:
    L0 R0 / L1 R1 / ... so each row is a direct comparison."""
    li, ri = list_images(left), list_images(right)
    pairs = max(len(li), len(ri))
    tiles: list[Image.Image] = []
    labels: list[str] = []
    placeholder = Image.new("RGB", (tile_w, tile_h), (40, 0, 0))
    for i in range(pairs):
        lp = li[i] if i < len(li) else None
        rp = ri[i] if i < len(ri) else None
        tiles.append(thumb(lp, tile_w, tile_h) if lp else placeholder)
        labels.append(f"L · {lp.name if lp else '—'}")
        tiles.append(thumb(rp, tile_w, tile_h) if rp else placeholder)
        labels.append(f"R · {rp.name if rp else '—'}")
    return grid(tiles, labels, cols=2), labels


def single(src: Path, tile_w: int, tile_h: int, cols: int) -> Image.Image:
    files = list_images(src)
    if not files:
        raise SystemExit(f"no images in {src}")
    tiles = [thumb(p, tile_w, tile_h) for p in files]
    labels = [p.name for p in files]
    return grid(tiles, labels, cols=cols)


def zoom_strip(
    paths: list[Path], rect: tuple[int, int, int, int]
) -> Image.Image:
    crops = [crop_full(p, rect) for p in paths]
    w, h = crops[0].size
    sheet = Image.new("RGB", (len(crops) * w, h + 18), (8, 8, 10))
    for i, (c, p) in enumerate(zip(crops, paths)):
        sheet.paste(label_strip(f"zoom · {p.name}", w, 18), (i * w, 0))
        sheet.paste(c, (i * w, 18))
    return sheet


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--src", type=Path, help="single directory of screenshots")
    g.add_argument("--left", type=Path, help="left side (e.g. original)")
    ap.add_argument("--right", type=Path, help="right side (e.g. openrecet)")
    ap.add_argument(
        "--tile",
        default="320x240",
        help="tile size WxH (default 320x240)",
    )
    ap.add_argument(
        "--cols", type=int, default=4, help="cols in single mode (default 4)"
    )
    ap.add_argument(
        "--zoom",
        help="optional full-res crop strip: LEFT,TOP,WIDTH,HEIGHT",
    )
    ap.add_argument(
        "--out", type=Path, default=Path("contact-sheet.png"),
        help="output png (default contact-sheet.png)",
    )
    args = ap.parse_args()

    tw, th = (int(x) for x in args.tile.lower().split("x"))

    if args.left and not args.right:
        ap.error("--right is required with --left")

    if args.src:
        sheet = single(args.src, tw, th, args.cols)
        zoom_paths = list_images(args.src)
    else:
        sheet, _ = side_by_side(args.left, args.right, tw, th)
        zoom_paths = list_images(args.left) + list_images(args.right)

    if args.zoom:
        rect = tuple(int(x) for x in args.zoom.split(","))
        if len(rect) != 4:
            ap.error("--zoom must be LEFT,TOP,WIDTH,HEIGHT")
        zoom = zoom_strip(zoom_paths, rect)
        combined = Image.new(
            "RGB",
            (max(sheet.width, zoom.width), sheet.height + zoom.height + 4),
            (12, 12, 14),
        )
        combined.paste(sheet, (0, 0))
        combined.paste(zoom, (0, sheet.height + 4))
        sheet = combined

    args.out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out, optimize=True)
    print(f"wrote {args.out} ({sheet.size[0]}×{sheet.size[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
