"""transport/convert.py — frame format conversion + frame→LABEL renumber.

- convert_to_png: the exe writes BMP; run-openrecet usually converts to PNG but can
  miss a large capture, leaving the studio (which globs frame_*.png) seeing 0 frames.
  We convert here too (idempotent) so frame counts / diff / encode are format-robust.
- renumber_to_label: rebase a side's frame_<n>.png into the LABEL space (frame name ==
  window_start + anchor-relative index) so the same frame_NNNNN is the same captured
  moment on BOTH sides for ANY window — the coordinate contract of
  docs/plans/trace-studio-v2.md / web/model.mjs (label = frames[0] + k*cadence).
  retail writes frame_<absolute>; the port writes frame_<anchor-relative index> (0-based).
  BOTH need +window_start to reach label space, so BOTH go through here. When
  window_start == 0 (the common window) it is a no-op — which is exactly why the PORT
  side rode along unrenumbered and uncaught until a caprange.start > 0 window
  (merchants-guild) made port frame N collide with retail label N+window_start, painting
  the whole diff white over content that was actually 1:1.
"""
from __future__ import annotations

from pathlib import Path


def convert_to_png(frames_dir: Path) -> int:
    """BMP→PNG in place (idempotent). Returns the number converted."""
    from frame_io import convert_dir
    if not Path(frames_dir).is_dir():
        return 0
    return convert_dir(Path(frames_dir))


def renumber_to_label(side_dir: Path, window_start: int = 0) -> int | None:
    """Rebase a side's frame_<n> into LABEL space: the i-th kept frame becomes
    frame_{window_start + (n_i - n_0):05d} (n_0 = the first kept frame's number) so the
    same frame_NNNNN is the same captured moment on BOTH sides for any caprange.start/
    stride (the first kept frame is the window start). Returns n_0 (retail: the absolute
    engine frame; port: 0), or None when there are no frames. Idempotent: a renamed dir
    has min == window_start, so a second pass shifts by 0."""
    from frame_io import frame_glob
    frames_dir = Path(side_dir) / "frames"
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


# Back-compat: the retail leg historically called renumber_retail. Both sides now share
# the side-agnostic core (the port goes through it too — see the module docstring).
renumber_retail = renumber_to_label
