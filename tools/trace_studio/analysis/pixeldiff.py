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
    pf = _by_index(Path(port_dir) / "frames")
    rf = _by_index(Path(retail_dir) / "frames")
    common = sorted(set(pf) & set(rf))
    per: list[dict] = []
    for n in common:
        a = load_png_rgb(rf[n])              # retail = ground truth (left/A)
        b = load_png_rgb(pf[n])              # port
        if a.shape != b.shape:
            continue
        d, differ, meanabs = amplified_diff(a, b, amp)
        save_png(d, diff_dir / f"frame_{n:05d}.png")
        per.append({"frame": n, "differ": differ, "meanabs": round(meanabs, 4)})
    return {"n": len(per), "per_frame": per}
