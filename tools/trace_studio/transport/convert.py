"""transport/convert.py — frame format conversion + retail renumber.

- convert_to_png: the exe writes BMP; run-openrecet usually converts to PNG but can
  miss a large capture, leaving the studio (which globs frame_*.png) seeing 0 frames.
  We convert here too (idempotent) so frame counts / diff / encode are format-robust.
- renumber_retail: retail writes frame_<absolute>.png; rebase into the LABEL space
  (anchor-relative index) the port's export_trace renumber already uses, so the same
  frame_NNNNN name is the same captured moment on BOTH sides for ANY window
  (caprange.start > 0, capstride > 1) — the coordinate contract of
  docs/plans/trace-studio-v2.md / web/model.mjs (label = frames[0] + k*cadence).
"""
from __future__ import annotations

from pathlib import Path


def convert_to_png(frames_dir: Path) -> int:
    """BMP→PNG in place (idempotent). Returns the number converted."""
    from frame_io import convert_dir
    if not Path(frames_dir).is_dir():
        return 0
    return convert_dir(Path(frames_dir))


def renumber_retail(retail_dir: Path, window_start: int = 0) -> int | None:
    """Rebase retail's frame_<absolute> into LABEL space: the i-th kept frame becomes
    frame_{window_start + (abs_i - abs_0):05d} — matching the port's anchor-relative
    naming for any caprange.start/stride (the first kept frame is the window start).
    Returns abs_0 (the absolute engine frame of the first kept frame), or None when
    there are no frames. Idempotent: a renamed dir has min == window_start, so a
    second pass shifts by 0."""
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
    shift = base - window_start
    if shift == 0:
        return base
    # new = n - shift < n everywhere (shift > 0: abs ≥ window labels), so ascending
    # rename never collides. (shift < 0 can only happen on an already-renumbered dir
    # being re-shifted to a LARGER window_start; descend to stay collision-free.)
    for n, p in (nums if shift > 0 else reversed(nums)):
        tgt = p.with_name(f"frame_{n - shift:05d}{p.suffix}")
        if tgt != p:
            p.rename(tgt)
    return base
