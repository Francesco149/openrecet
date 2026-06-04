#!/usr/bin/env python3
"""
tools/recette_anim_probe.py — autonomously flag frames where Recette's RENDERED
walk cell diverges from retail, even though her actor-record anim counters match.

WHY THIS EXISTS (the open lead, found 2026-06-04 by user eyeball)
-----------------------------------------------------------------
`phase_probe house-walk-down-dense` reports the player anim RECORD fields
(ANIM/COUNTER/FRAME/FACING at &DAT_056daae8[0]) as bit-exact 1:1 vs retail on
every frame.  But on ~18% of frames the DRAWN sprite is a different walk cell:
at e.g. db054=60 both sides log aframe=1/anim=1/cnt=10/oct=4 AND the on-screen X
matches (dX≈0), yet Recette's legs are a different walk step.  So the rendered
player cell is NOT a pure function of the record fields phase_probe watches — the
walker draw (shop-walker FUN_004552d0 / chr-sprite walker FUN_00456f56) is reading
some OTHER state for the cell (candidates: the FUN_0048b850 motion/sprite-history
ring DAT_056da3dc, the STATE field 0x56daafc that phase_probe does NOT watch, or a
finer sub-frame timer).  This tool finds the divergent frames so the render-side
source can be tracked down.  It is NOT a phase-counter desync (those are aligned).

HOW IT DETECTS (user's heuristic: "diff mostly white up to the top = diff cell")
-------------------------------------------------------------------------------
Crops a tight box around Recette that EXCLUDES Tear + her wing-glow (she stands to
Recette's right), diffs port vs retail at native res, and measures the fraction of
strongly-different pixels (mean|abs| > 30/255) in the TOP HALF of the crop (head +
torso).  A 1px position jitter only lights thin edges (low %); a different cell
lights large solid regions reaching up into the torso/head.  Calibration on
house-walk-down-dense (2026-06-04): ALIGNED frames score 0.0% top-half, DIVERGENT
frames score 16–19% — clean separation, threshold 8%.

USAGE
-----
  # 1. produce phase+rng-aligned frames on BOTH targets (writes the run dir):
  nix develop --command python3 tools/phase_probe.py house-walk-down-dense
  # 2. flag the divergent frames + build a 4x [port|retail|diff] montage:
  nix develop --command python3 tools/recette_anim_probe.py house-walk-down-dense
  #    add --push to send the montage to the llm-feed, --all to montage every frame.

Outputs runs/recette-anim-probe/<scenario>/ (montage frames) + a verdict table.
The montage is the thing to eyeball at 4x: for each flagged frame, [PORT | RETAIL
| amplified diff] so the differing walk cell is visible (partly obscured by foot
dust — read the upper body).  Cross-refs: docs/findings/scene1-recette-walk-cell.md,
docs/phase-debugging.md, [[reference_phase_probe_tool]].
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pixel_diff import amplified_diff  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
FEED = Path("/opt/src/llm-feed/feed.py")

# Recette-only crop (full-res 1024x768), excludes Tear + wing-glow (she is to
# Recette's right, x>~570).  Recalibrate if the camera/scenario changes.
BOX = (486, 610, 566, 734)          # (l, t, r, b)
ZOOM = 4
STRONG = 30                          # per-pixel mean|abs| threshold (/255)
TOPHALF_PCT_THRESH = 8.0             # top-half strong-diff % above which = diff cell


def load_pairs(run: Path):
    """Return sorted [(db054, port_frame, retail_frame, port_meta_row)] aligned
    by db054 over the post-phasepin window (mirrors phase_probe's alignment)."""
    agent = (run / "retail" / "agent.log").read_text() if (run / "retail" / "agent.log").exists() else ""
    m = re.search(r"phasepin .* at frame (\d+)", agent)
    pin = int(m.group(1)) if m else -1
    port = {}
    for l in (run / "port" / "meta.jsonl").read_text().splitlines():
        if '"db054"' in l:
            r = json.loads(l); port[r["db054"]] = r
    ret = {}
    for l in (run / "retail" / "watch.jsonl").read_text().splitlines():
        o = json.loads(l)
        if o["frame"] <= pin:
            continue
        d = o["vals"].get("db054")
        if d is not None and d not in ret:
            ret[d] = o["frame"]
    return [(d, port[d]["frame"], ret[d], port[d]) for d in sorted(set(port) & set(ret))]


def crop(run: Path, side: str, frame: int) -> Image.Image:
    return Image.open(run / side / "frames" / f"frame_{frame:05d}.png").convert("RGB").crop(BOX)


def metric(pa: Image.Image, ra: Image.Image) -> tuple[float, float]:
    a = np.asarray(pa).astype(int); b = np.asarray(ra).astype(int)
    md = np.abs(a - b).mean(axis=2)
    h = md.shape[0] // 2
    return (md[:h] > STRONG).mean() * 100.0, (md > STRONG).mean() * 100.0


def panel(pa, ra, label) -> Image.Image:
    diff, differ, _ = amplified_diff(np.asarray(pa), np.asarray(ra), 6.0)
    da = Image.fromarray(diff, "RGB")
    up = lambda im: im.resize((im.width * ZOOM, im.height * ZOOM), Image.NEAREST)
    pa, ra, da = up(pa), up(ra), up(da)
    W = pa.width * 3 + 40
    g = Image.new("RGB", (W, pa.height + 22), (18, 18, 22))
    d = ImageDraw.Draw(g)
    for i, (lbl, im) in enumerate([("PORT", pa), ("RETAIL", ra), ("DIFF x6", da)]):
        x = 10 + i * (pa.width + 10)
        g.paste(im, (x, 20)); d.text((x + 2, 4), f"{lbl} {label}", fill=(255, 240, 120))
    return g


def main() -> int:
    ap = argparse.ArgumentParser(description="flag Recette walk-cell render divergences")
    ap.add_argument("scenario", nargs="?", default="house-walk-down-dense")
    ap.add_argument("--run", help="explicit phase-probe run dir (default runs/phase-probe/<scenario>)")
    ap.add_argument("--all", action="store_true", help="montage every aligned frame, not just flagged")
    ap.add_argument("--push", action="store_true", help="push the montage to the llm-feed")
    args = ap.parse_args()

    run = Path(args.run) if args.run else ROOT / "runs" / "phase-probe" / args.scenario
    if not (run / "port" / "meta.jsonl").exists():
        sys.exit(f"recette_anim_probe: no phase-probe run at {run} — run phase_probe first.")

    pairs = load_pairs(run)
    out = ROOT / "runs" / "recette-anim-probe" / args.scenario
    out.mkdir(parents=True, exist_ok=True)
    for f in out.glob("frame_*.png"):
        f.unlink()

    print(f"  {'idx':>3} {'db054':>5} {'aframe':>6} {'cnt':>3} {'top%':>5} {'full%':>5} verdict")
    flagged, montage_idx = [], []
    for i, (d, pf, rf, meta) in enumerate(pairs):
        pa, ra = crop(run, "port", pf), crop(run, "retail", rf)
        top, full = metric(pa, ra)
        diff_cell = top > TOPHALF_PCT_THRESH
        if diff_cell:
            flagged.append(d)
        if diff_cell or args.all:
            montage_idx.append((i, d, pf, rf, meta, top))
        if diff_cell or i % 5 == 0:
            print(f"  {i:>3} {d:>5} {meta['aframe']:>6} {meta['counter']:>3} "
                  f"{top:>4.1f}% {full:>4.1f}% {'❌ DIFF-CELL' if diff_cell else 'aligned'}")

    for k, (i, d, pf, rf, meta, top) in enumerate(montage_idx):
        panel(crop(run, "port", pf), crop(run, "retail", rf),
              f"f={i} db054={d} af={meta['aframe']} cnt={meta['counter']} top={top:.0f}%"
              ).save(out / f"frame_{k:05d}.png")

    print(f"\n  {len(flagged)}/{len(pairs)} frames render a DIFFERENT walk cell "
          f"({100*len(flagged)//max(1,len(pairs))}%): db054 {flagged}")
    print(f"  montage ({len(montage_idx)} frames): {out}")
    print("  NB anim RECORD counters are bit-exact on these — the divergence is the "
          "render cell SOURCE (ring/STATE/sub-frame), not a phase desync.")

    if args.push and montage_idx:
        note = (f"Recette walk-cell render divergence: {len(flagged)}/{len(pairs)} frames "
                f"draw a different walk cell vs retail despite bit-exact anim-record counters "
                f"(db054 {flagged}). Each page [PORT|RETAIL|diff x6], 4x, Recette-only "
                f"(Tear excluded). Read the upper body (feet obscured by dust). Render-side "
                f"cell source, NOT a phase desync.")
        subprocess.run([sys.executable, str(FEED), "montage", "--frames-dir", str(out),
                        "--title", f"Recette walk-cell divergence — {args.scenario} ({len(flagged)} frames)",
                        "--note", note], check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
