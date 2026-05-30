#!/usr/bin/env python3
"""tools/pixel_diff.py — side-by-side + amplified white-diff comparison.

The canonical OpenRecet render-parity comparison format (per user request,
2026-05-30): given two frames (typically RETAIL and US/openrecet), emit a
single panel:

    [ A | B | amplified pixel-diff ]

where the diff panel is BLACK where the two images are bit-identical and
WHITE (scaled by --amp) where they differ. This makes "how bit-perfect is
it" answerable at a glance — a black diff panel means pixel-identical.

Cross-target captures (retail-via-Frida vs openrecet) are NOT guaranteed
bit-comparable globally, but with a matched resolution + an aligned camera
the static 3D geometry lines up, so the diff panel is meaningful for
texture/filtering/shading parity within a crop. Always pass a --crop that
avoids un-ported overlays (2D HUD, dialog boxes, character billboards) and
any region where the two captures are at different animation/camera state.

Examples:
    # whole-frame compare
    pixel_diff.py --a retail.png --b ours.png --out cmp.png

    # zoom into the front counter book, 4x nearest-neighbour, diff amplified 6x
    pixel_diff.py --a runs/cchr2h-retail/frames/frame_06000.png \
                  --b runs/mipfix-port/frame_03300.png \
                  --crop 440,495,500,548 --scale 4 --amp 6 \
                  --labels "RETAIL,US (openrecet)" --out book_diff.png

`--a`/`--b` accept .bmp or .png. Prints differing-pixel count + mean
abs-diff so the result is also machine-checkable.
"""
import argparse
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("pixel_diff: needs Pillow (run under `nix develop`)")

import numpy as np


def parse_crop(s):
    if not s:
        return None
    parts = [int(v) for v in s.replace(" ", "").split(",")]
    if len(parts) != 4:
        sys.exit("--crop wants x0,y0,x1,y1")
    return tuple(parts)


def main():
    ap = argparse.ArgumentParser(description="side-by-side + amplified white-diff")
    ap.add_argument("--a", required=True, help="left image (convention: RETAIL / ground truth)")
    ap.add_argument("--b", required=True, help="right image (convention: US / openrecet)")
    ap.add_argument("--crop", help="x0,y0,x1,y1 in source-pixel coords (applied to both)")
    ap.add_argument("--scale", type=int, default=3, help="nearest-neighbour upscale of each panel")
    ap.add_argument("--amp", type=float, default=6.0, help="diff amplification (white = differ)")
    ap.add_argument("--labels", default="RETAIL,US,diff (white=differ)",
                    help="comma list; first two label A/B, third labels the diff")
    ap.add_argument("--out", required=True, help="output PNG path")
    args = ap.parse_args()

    a = Image.open(args.a).convert("RGB")
    b = Image.open(args.b).convert("RGB")
    box = parse_crop(args.crop)
    if box:
        a = a.crop(box)
        b = b.crop(box)
    if a.size != b.size:
        sys.exit(f"pixel_diff: size mismatch A{a.size} B{b.size} (crop both to match)")

    na = np.asarray(a).astype(int)
    nb = np.asarray(b).astype(int)
    perpx = np.abs(na - nb).sum(2)              # 0..765 per pixel
    differ = int((perpx > 0).sum())
    total = perpx.size
    meanabs = perpx.mean() / 3.0

    # White-on-black amplified diff: any nonzero -> scaled toward white.
    d = np.clip(perpx.astype(float) * args.amp, 0, 255).astype(np.uint8)
    diff_img = Image.fromarray(np.stack([d, d, d], axis=2), "RGB")

    labels = (args.labels.split(",") + ["", "", ""])[:3]
    panels = [a, b, diff_img]
    cw, ch = a.size
    SC = args.scale
    pad = 22
    gap = 10
    W = cw * SC * 3 + gap * 2
    out = Image.new("RGB", (W, ch * SC + pad), (12, 12, 12))
    drw = ImageDraw.Draw(out)
    for i, im in enumerate(panels):
        x = i * (cw * SC + gap)
        out.paste(im.resize((cw * SC, ch * SC), Image.NEAREST), (x, pad))
        drw.text((x + 3, 6), labels[i], fill=(255, 255, 0))
    out.save(args.out)

    pct = 100.0 * differ / total
    print(f"pixel_diff: {differ}/{total} px differ ({pct:.2f}%)  mean|abs|/ch={meanabs:.2f}  "
          f"{'BIT-IDENTICAL' if differ == 0 else ''}")
    print(f"pixel_diff: wrote {args.out}")


if __name__ == "__main__":
    main()
