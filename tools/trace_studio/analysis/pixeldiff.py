"""analysis/pixeldiff.py — per anchor-relative index, the retail-vs-port white-diff.

retail = ground truth (A/left), port = B. Writes frame_NNNNN.png diffs + returns a
summary {n, per_frame:[{frame, differ, meanabs}]} the viewer's diff ribbon reads.
"""
from __future__ import annotations

from pathlib import Path


def load_png_rgb(path: Path):
    from PIL import Image
    import numpy as np
    return np.asarray(Image.open(path).convert("RGB"))


def save_png(arr, path: Path) -> None:
    from PIL import Image
    Image.fromarray(arr).save(path)


def _by_index(frames_dir: Path) -> dict[int, Path]:
    return {int("".join(c for c in p.stem if c.isdigit())): p
            for p in Path(frames_dir).glob("frame_*.png")}


def build_diff(port_dir: Path, retail_dir: Path, diff_dir: Path, amp: float) -> dict:
    from pixel_diff import amplified_diff
    diff_dir = Path(diff_dir)
    diff_dir.mkdir(parents=True, exist_ok=True)
    # Pair by DENSE captured ordinal — the i-th frame on each side, which IS the MP4 / cursor /
    # state.jsonl index — NOT by the abs-based PNG filename number. A load is suppressed for a
    # DIFFERENT number of frames per side (turbo retail's load runs far longer), so the same
    # filename number is a different moment after it, and the common-number set is gappy (each
    # side's load gaps) — both desync the diff ribbon from the dense cursor (the "red area lands
    # ~N frames late, with a blank hole" bug). Sorting by number then taking the i-th gives the
    # dense order the videos are built in.
    pf = [p for _, p in sorted(_by_index(Path(port_dir) / "frames").items())]
    rf = [p for _, p in sorted(_by_index(Path(retail_dir) / "frames").items())]
    per: list[dict] = []
    for i in range(min(len(pf), len(rf))):
        a = load_png_rgb(rf[i])              # retail = ground truth (left/A)
        b = load_png_rgb(pf[i])              # port
        if a.shape != b.shape:
            continue
        d, differ, meanabs = amplified_diff(a, b, amp)
        save_png(d, diff_dir / f"frame_{i:05d}.png")
        per.append({"frame": i, "differ": differ, "meanabs": round(meanabs, 4)})
    return {"n": len(per), "per_frame": per}
