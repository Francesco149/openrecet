#!/usr/bin/env python3
"""
tools/frame_io.py — shared frame I/O so captures persist as lossless PNG, not
the 3 MB/frame top-down-BGRA BMPs the engine/Frida emit.

The capture WRITERS (src/main.c capture_backbuffer for the port, the Frida agent
for retail) emit BMP because that's what's cheap from C / a raw backbuffer grab.
Everything downstream is Python + PIL (which reads BMP and PNG identically), so we
convert to PNG at the capture boundary and read png-or-bmp everywhere else. Old
BMPs on disk keep working (readers fall back); only NEW captures are PNG.

CLI:  python tools/frame_io.py <dir> [<dir> ...]   # convert frame_*/cap_* BMP→PNG
"""

from __future__ import annotations

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:                       # pragma: no cover — PIL always present in devshell
    Image = None


def write_frame_png(path: Path, w: int, h: int, bgra_topdown: bytes) -> None:
    """Write a top-down BGRA backbuffer buffer as a lossless RGB PNG.

    Mirrors src/main.c capture_backbuffer's layout (32-bit BGRA, top row first)
    so retail (Frida) frames are pixel-identical to what the old BMP path
    produced, just smaller. `path` should end in .png."""
    img = Image.frombuffer("RGBA", (w, h), bytes(bgra_topdown), "raw", "BGRA", 0, 1)
    img.convert("RGB").save(path, "PNG")


def frame_glob(frames_dir: Path, prefix: str = "frame_") -> list[Path]:
    """Sorted unique capture frames in `frames_dir`, preferring .png over a
    same-stem .bmp (so a half-converted dir still yields each frame once)."""
    by_stem: dict[str, Path] = {}
    for ext in (".bmp", ".png"):          # png second → wins on stem collision
        for p in frames_dir.glob(f"{prefix}*{ext}"):
            by_stem[p.stem] = p
    return [by_stem[s] for s in sorted(by_stem)]


def frame_path(frames_dir: Path, stem: str) -> Path | None:
    """The existing capture file for a stem (e.g. 'frame_01234' / 'cap_00'),
    preferring .png. None if neither exists."""
    for ext in (".png", ".bmp"):
        p = frames_dir / f"{stem}{ext}"
        if p.exists():
            return p
    return None


def convert_dir(frames_dir: Path, delete_bmp: bool = True) -> int:
    """Convert every frame_*/cap_* BMP in `frames_dir` to a sibling .png and
    (by default) delete the BMP. Returns the count converted. Idempotent;
    a no-op if PIL is missing or the dir doesn't exist."""
    if Image is None or not frames_dir.is_dir():
        return 0
    n = 0
    for bmp in list(frames_dir.glob("frame_*.bmp")) + list(frames_dir.glob("cap_*.bmp")):
        png = bmp.with_suffix(".png")
        try:
            if not png.exists():
                Image.open(bmp).convert("RGB").save(png, "PNG")
            if delete_bmp:
                bmp.unlink()
            n += 1
        except Exception as e:            # pragma: no cover — best-effort cleanup
            print(f"frame_io: failed on {bmp}: {e}", file=sys.stderr)
    return n


def _main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: frame_io.py <dir> [<dir> ...]", file=sys.stderr)
        return 2
    total = 0
    for d in argv[1:]:
        total += convert_dir(Path(d))
    print(f"frame_io: converted {total} BMP→PNG")
    return 0


if __name__ == "__main__":
    raise SystemExit(_main(sys.argv))
