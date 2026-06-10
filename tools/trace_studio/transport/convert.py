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


def _read_anchor_abs(path: Path) -> dict[str, list[int]]:
    """{anchor_name: [abs_frame, …]} (file order) from a side's anchors.jsonl. The
    frames are ABSOLUTE engine frames (both sides emit the same stream)."""
    import json
    out: dict[str, list[int]] = {}
    p = Path(path)
    if not p.exists():
        return out
    for ln in p.read_text().splitlines():
        s = ln.strip()
        if not s:
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if "anchor" in o and "frame" in o:
            out.setdefault(o["anchor"], []).append(int(o["frame"]))
    return out


def rebase_retail_to_port_anchor(port_dir: Path, retail_dir: Path,
                                 window_start: int, retail_base: int | None) -> int:
    """Re-align the RETAIL frames to the PORT at the LATEST anchor both sides emit
    as a CAPTURED frame, so content AFTER that anchor diffs synced across a
    kept-count LOAD seam.

    renumber_to_label labels each side window_start + kept-index, which assumes
    kept-frame i is the same MOMENT on both sides.  That's false across a
    non-deterministic load (e.g. the guild scene loads ~8 frames on the port vs
    ~88 on retail): the side that suppressed fewer load frames ends up label-ahead,
    so a menu rendered after the cutscene wouldn't line up.  Anchors fire at the
    same MOMENT on both sides (engine events), so re-shifting retail to make a
    shared anchor's label match the port's re-syncs everything downstream of it.

    Call AFTER both sides are renumbered to label space.  Picks the anchor with the
    greatest retail abs whose PORT firing was actually captured (so the rebase point
    is real geometry on both sides).  Returns the applied label shift (0 = already
    aligned, or no shared captured anchor found — a no-op)."""
    import json
    from frame_io import frame_glob
    if retail_base is None:
        return 0
    p_anch = _read_anchor_abs(Path(port_dir) / "anchors.jsonl")
    r_anch = _read_anchor_abs(Path(retail_dir) / "anchors.jsonl")
    if not p_anch or not r_anch:
        return 0
    # port absolute frame → 0-based kept index (label = window_start + index)
    abs_to_k: dict[int, int] = {}
    mp = Path(port_dir) / "meta.jsonl"
    if mp.exists():
        for ln in mp.read_text().splitlines():
            if ln.strip():
                o = json.loads(ln)
                abs_to_k[int(o["frame_abs"])] = int(o["frame"])
    if not abs_to_k:
        return 0
    best: tuple[int, int] | None = None       # (retail_abs, port_label)
    for name in set(p_anch) & set(r_anch):
        for pa, ra in zip(p_anch[name], r_anch[name]):   # matched by occurrence order
            if pa not in abs_to_k:            # the port's firing wasn't a captured frame
                continue
            if ra < retail_base:              # retail firing is before the window
                continue
            if best is None or ra > best[0]:
                best = (ra, window_start + abs_to_k[pa])
    if best is None:
        return 0
    retail_abs, port_label = best
    retail_label = window_start + (retail_abs - retail_base)
    shift = retail_label - port_label
    if shift == 0:
        return 0
    pairs: list[tuple[int, Path]] = []
    for f in frame_glob(Path(retail_dir) / "frames"):
        digits = "".join(c for c in f.stem if c.isdigit())
        if digits:
            pairs.append((int(digits), f))
    # label L → L - shift; rename in the collision-free direction.
    for n, f in (sorted(pairs) if shift > 0 else sorted(pairs, reverse=True)):
        tgt = f.with_name(f"frame_{n - shift:05d}{f.suffix}")
        if tgt != f:
            f.rename(tgt)
    return shift
