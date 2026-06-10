"""analysis/pixeldiff.py — per anchor-relative LABEL, the retail-vs-port white-diff.

retail = ground truth (A/left), port = B. Writes frame_<label>.png diffs + returns a
summary {n, per_frame:[{frame, differ, meanabs}]} the viewer's diff ribbon reads.
`per_frame[].frame` is the LABEL (anchor-relative index, = the port file number) —
the key web/model.mjs diffAt() looks up (labelOf(k) = frames[0] + k*cadence) — so
the ribbon stays correct for caprange.start > 0 and capstride > 1 windows.
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
    # Pair by LABEL (the anchor-relative frame number both sides' PNGs are named
    # by since the 2026-06-09 coordinate unification — BOTH sides go through
    # convert.renumber_to_label, so same name == same moment BY CONSTRUCTION).
    #
    # This used to pair by dense ordinal ("the i-th file on each side"), a
    # workaround from the era when retail frames kept their RAW abs numbers (a
    # load ran a different frame count per side, so equal numbers were different
    # moments and the common-number set was gappy).  Post-unification the ordinal
    # pairing became the bug: with a kept-count mismatch (e.g. item-display-2's
    # port 1845 vs retail 1842 — one extra port frame per load seam) every pair
    # after the first seam compared label L against label L−k, so the diff showed
    # ghost differences on everything that MOVES (the bg-window NPCs) while the
    # same-label sides were 1:1.  Labels with no partner (the seam extras) get no
    # diff frame — honest holes, reported below, instead of a silent global shift.
    port_by = _by_index(Path(port_dir) / "frames")
    retail_by = _by_index(Path(retail_dir) / "frames")
    labels = sorted(set(port_by) & set(retail_by))
    unmatched = {"port": sorted(set(port_by) - set(retail_by)),
                 "retail": sorted(set(retail_by) - set(port_by))}
    if not labels and port_by and retail_by:
        # Pre-unification session (retail still abs-named): no common labels at
        # all.  Fall back to the old ordinal pairing so old sessions keep a diff,
        # named by the port label as before.
        plist = sorted(port_by.items())
        rlist = sorted(retail_by.items())
        labels = [n for n, _ in plist[:min(len(plist), len(rlist))]]
        pairs = [(rlist[i][1], plist[i][1], labels[i]) for i in range(len(labels))]
        unmatched = {"port": [], "retail": [], "ordinal_fallback": True}
    else:
        pairs = [(retail_by[n], port_by[n], n) for n in labels]
    import numpy as np
    per: list[dict] = []
    for rpath, ppath, label in pairs:
        a = load_png_rgb(rpath)              # retail = ground truth (left/A)
        b = load_png_rgb(ppath)              # port
        if a.shape != b.shape:
            continue
        d, differ, meanabs = amplified_diff(a, b, amp)
        save_png(d, diff_dir / f"frame_{label:05d}.png")
        # gt8 = pixels with any channel |Δ| > 8 — the project's bit-clean
        # criterion ("0 px >8/ch"); `differ` counts ANY 1-LSB pixel and is
        # noise-dominated on real captures, so triage thresholds on gt8.
        gt8 = int((np.abs(a.astype(int) - b.astype(int)).max(axis=2) > 8).sum())
        per.append({"frame": label, "differ": differ,
                    "meanabs": round(meanabs, 4), "gt8": gt8})
    return {"n": len(per), "per_frame": per, "unmatched": unmatched}
