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


def _convert_one(args: tuple[str, str]) -> int:
    """Worker: read one BMP (typically off a slow /mnt/c drvfs mount) and write a
    sibling PNG into `dest`. Returns 1 on success, 0 on skip/failure."""
    src_s, dest_s = args
    if Image is None:
        return 0
    src = Path(src_s)
    png = Path(dest_s) / (src.stem + ".png")
    try:
        if not png.exists():
            Image.open(src).convert("RGB").save(png, "PNG")
        return 1
    except Exception as e:                    # pragma: no cover — best-effort
        print(f"frame_io: copyback failed on {src}: {e}", file=sys.stderr)
        return 0


def copyback_convert(src_dir: Path, dest_dir: Path, *,
                     delete_src: bool = True, jobs: int | None = None) -> int:
    """D2 (Trace Studio v2): bulk-convert frame_*/cap_* BMPs from a fast
    Windows-LOCAL staging dir (`src_dir`, read here over /mnt/c drvfs) into PNGs
    in `dest_dir` (the WSL run dir). The exe/agent writes BMP to local NTFS in
    sub-ms (vs ~0.4 s/frame synchronous over the 9p \\\\wsl.localhost mount, which
    also stalls the sim loop); this reads them back ONCE, in parallel, and emits
    ~3x-smaller PNGs. Returns the count converted. Parallel across CPUs (the
    /mnt/c read is the bottleneck). Best-effort; no-op if PIL missing / no src."""
    if Image is None or not src_dir.is_dir():
        return 0
    dest_dir.mkdir(parents=True, exist_ok=True)
    bmps = sorted(src_dir.glob("frame_*.bmp")) + sorted(src_dir.glob("cap_*.bmp"))
    if not bmps:
        return 0
    work = [(str(b), str(dest_dir)) for b in bmps]
    if jobs is None:
        import os
        jobs = max(1, min(8, (os.cpu_count() or 2)))
    if jobs > 1 and len(work) > 4:
        import multiprocessing as mp
        try:
            with mp.Pool(jobs) as pool:
                n = sum(pool.map(_convert_one, work))
        except Exception:                     # pragma: no cover — fall back serial
            n = sum(_convert_one(w) for w in work)
    else:
        n = sum(_convert_one(w) for w in work)
    if delete_src:
        import shutil
        shutil.rmtree(src_dir, ignore_errors=True)
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
