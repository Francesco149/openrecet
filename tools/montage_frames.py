#!/usr/bin/env python3
"""montage_frames.py — tile captured frames into 3x3 montages for quick inspection.

The Frida/port capture harnesses drop per-frame screenshots into
``<run-dir>/frames/`` (``frame_NNNNN.bmp``). Eyeballing a dozen of them one at a
time is slow; this groups every 9 (sorted by engine frame) into a labelled 3x3
montage PNG and — on WSL — opens each with the default Windows image viewer.

Usage::

    python3 tools/montage_frames.py --run-dir runs/house-zspam2
    python3 tools/montage_frames.py --run-dir runs/foo --no-open   # just write

The grid reads left-to-right, top-to-bottom in frame order; each cell is
captioned with its engine frame number so a montage doubles as a timeline.
Designed to be called automatically at the end of a capture run (see
``frida_capture.py --montage``) as well as standalone.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - PIL is in the dev shell
    print("montage_frames: Pillow not available (run inside `nix develop`)",
          file=sys.stderr)
    sys.exit(1)

_FRAME_RE = re.compile(r"frame_(\d+)\.(?:bmp|png)$")
GRID = 3                       # 3x3
PER = GRID * GRID              # 9 frames per montage
PAD = 4                        # px between cells
LABEL_H = 16                   # px caption strip per cell


def _frame_no(p: Path) -> int:
    m = _FRAME_RE.search(p.name)
    return int(m.group(1)) if m else -1


def collect_frames(frames_dir: Path) -> list[Path]:
    """Sorted unique frame images (prefer .png, fall back to .bmp)."""
    by_no: dict[int, Path] = {}
    for p in frames_dir.iterdir():
        m = _FRAME_RE.search(p.name)
        if not m:
            continue
        n = int(m.group(1))
        # Prefer png over bmp when both exist for the same frame.
        if n not in by_no or p.suffix == ".png":
            by_no[n] = p
    return [by_no[n] for n in sorted(by_no)]


def _open_windows(path: Path) -> None:
    """Open with the default Windows viewer (WSL); no-op + note otherwise."""
    try:
        win = subprocess.run(["wslpath", "-w", str(path.resolve())],
                             capture_output=True, text=True, check=True).stdout.strip()
        # `explorer.exe <file>` launches the file's default handler. It returns
        # nonzero even on success, so don't check=True here.
        subprocess.run(["explorer.exe", win], check=False)
    except (FileNotFoundError, subprocess.CalledProcessError):
        print(f"montage_frames: could not auto-open {path} (not on WSL?)",
              file=sys.stderr)


def build_montages(run_dir: Path, do_open: bool = True) -> list[Path]:
    frames_dir = run_dir / "frames"
    if not frames_dir.is_dir():
        print(f"montage_frames: no frames dir at {frames_dir}", file=sys.stderr)
        return []
    frames = collect_frames(frames_dir)
    if not frames:
        print(f"montage_frames: no frame_*.bmp/png in {frames_dir}", file=sys.stderr)
        return []

    try:
        font = ImageFont.truetype("DejaVuSansMono.ttf", 12)
    except OSError:
        font = ImageFont.load_default()

    out_paths: list[Path] = []
    for gi in range((len(frames) + PER - 1) // PER):
        chunk = frames[gi * PER:(gi + 1) * PER]
        thumbs = [Image.open(p).convert("RGB") for p in chunk]
        cw = max(t.width for t in thumbs)
        ch = max(t.height for t in thumbs)
        cell_h = ch + LABEL_H
        W = GRID * cw + (GRID + 1) * PAD
        H = GRID * cell_h + (GRID + 1) * PAD
        canvas = Image.new("RGB", (W, H), (24, 24, 24))
        draw = ImageDraw.Draw(canvas)
        for i, (p, t) in enumerate(zip(chunk, thumbs)):
            r, c = divmod(i, GRID)
            x = PAD + c * (cw + PAD)
            y = PAD + r * (cell_h + PAD)
            draw.text((x + 2, y), f"f={_frame_no(p)}", fill=(255, 230, 120), font=font)
            canvas.paste(t, (x, y + LABEL_H))
        out = run_dir / (f"montage_{gi:02d}.png" if len(frames) > PER
                         else "montage.png")
        canvas.save(out)
        out_paths.append(out)
        print(f"montage_frames: wrote {out} ({len(chunk)} frames)")

    if do_open:
        for out in out_paths:
            _open_windows(out)
    return out_paths


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-dir", type=Path, required=True,
                    help="capture run dir containing frames/")
    ap.add_argument("--no-open", action="store_true",
                    help="write montage PNGs but don't launch the viewer")
    args = ap.parse_args(argv)
    paths = build_montages(args.run_dir, do_open=not args.no_open)
    return 0 if paths else 1


if __name__ == "__main__":
    raise SystemExit(main())
