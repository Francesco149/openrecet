"""transport/convert.py — frame format conversion + retail renumber.

- convert_to_png: the exe writes BMP; run-openrecet usually converts to PNG but can
  miss a large capture, leaving the studio (which globs frame_*.png) seeing 0 frames.
  We convert here too (idempotent) so frame counts / diff / encode are format-robust.
- renumber_retail: retail writes frame_<absolute>.png; mirror export_trace's 0-based
  renumber so frame_NNNNN aligns with the port.
"""
from __future__ import annotations

from pathlib import Path


def convert_to_png(frames_dir: Path) -> int:
    """BMP→PNG in place (idempotent). Returns the number converted."""
    from frame_io import convert_dir
    if not Path(frames_dir).is_dir():
        return 0
    return convert_dir(Path(frames_dir))


def renumber_retail(retail_dir: Path) -> int | None:
    """Rebase retail's frame_<absolute> to 0-based. Returns the rebase base, or None."""
    from frame_io import frame_glob
    frames_dir = Path(retail_dir) / "frames"
    frames = frame_glob(frames_dir)
    if not frames:
        return None
    nums: list[tuple[int, Path]] = []
    for p in frames:
        digits = "".join(c for c in p.stem if c.isdigit())
        if digits:
            nums.append((int(digits), p))
    if not nums:
        return None
    nums.sort()
    base = nums[0][0]
    if base == 0:
        return 0
    # rel < abs everywhere, so ascending rename never collides.
    for n, p in nums:
        tgt = p.with_name(f"frame_{n - base:05d}{p.suffix}")
        if tgt != p:
            p.rename(tgt)
    return base
