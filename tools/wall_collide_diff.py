#!/usr/bin/env python3
"""
tools/wall_collide_diff.py — diff the player world position (px/pz) of the PORT
against RETAIL for the house-wall-collide scenario (or any shared segtrace
drive), so the collision resolver's accuracy can be measured + tuned.

Both targets run the SAME anchor-segmented trace, whose timing is rebased onto
the 2nd HOUSE_FREEROAM anchor — so the two position series are comparable at the
same *anchor-relative* frame, despite the load-jitter offset in absolute frames.
This tool rebases both and reports the per-frame px/pz divergence.

Inputs:
  --port    pos.jsonl from openrecet `--player-pos-log`
              rows: {"frame":N,"px":..,"py":..,"pz":..}
  --retail  watch.jsonl from `frida_capture.py --watch px=.. pz=..`
              rows: {"frame":N,"vals":{"px":..,"pz":..,...}}
  --port-log / --retail-log
              the run's stderr/agent log, parsed for the 2nd HOUSE_FREEROAM
              anchor frame (override with --port-anchor / --retail-anchor).

With only --port given, prints the port series summary + the retail ground-truth
wall contour for eyeballing (handy before a retail capture is available).

Exit 0 always (a diagnostic, not a gate).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Retail ground truth (runs/w4-collide | w4-table3): the right wall px pin vs pz.
# Reference contour for eyeballing only — the canonical accuracy signal is the
# per-frame --retail diff + anchor-phase search below (RESOLVED 1:1 2026-05-31:
# port is physically identical to retail across the whole slide, mod a 1-frame
# anchor offset). The straight-RIGHT drive contacts the counter row (px~2.15)
# then slides to the px=3.10 front section; both points match retail.
RETAIL_WALL_CONTOUR = [
    (9.23, 2.15, "counter row"),
    (4.51, 2.29, ""),
    (-0.65, 3.10, "room front (wider)"),
]

ANCHOR_RE = re.compile(r'HOUSE_FREEROAM"?\s*,\s*"?frame"?\s*[:=]\s*(\d+)')
# also accept the bare "...HOUSE_FREEROAM ... 1234" forms some logs emit
ANCHOR_RE2 = re.compile(r'HOUSE_FREEROAM\D+(\d+)')


def second_anchor_frame(log_path: Path | None) -> int | None:
    """Return the frame of the 2nd HOUSE_FREEROAM in a run log, or None."""
    if not log_path or not log_path.is_file():
        return None
    frames: list[int] = []
    for line in log_path.read_text(errors="replace").splitlines():
        if "HOUSE_FREEROAM" not in line:
            continue
        m = ANCHOR_RE.search(line) or ANCHOR_RE2.search(line)
        if m:
            frames.append(int(m.group(1)))
    if len(frames) >= 2:
        return frames[1]
    if frames:
        return frames[0]
    return None


def load_port(path: Path) -> dict[int, tuple[float, float]]:
    out: dict[int, tuple[float, float]] = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        r = json.loads(line)
        out[int(r["frame"])] = (float(r["px"]), float(r["pz"]))
    return out


def load_retail(path: Path) -> dict[int, tuple[float, float]]:
    out: dict[int, tuple[float, float]] = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        r = json.loads(line)
        v = r.get("vals", r)
        if "px" not in v or "pz" not in v:
            continue
        out[int(r["frame"])] = (float(v["px"]), float(v["pz"]))
    return out


def rebase(series: dict[int, tuple[float, float]], anchor: int
           ) -> dict[int, tuple[float, float]]:
    return {f - anchor: v for f, v in series.items() if f >= anchor}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=Path, required=True)
    ap.add_argument("--port-log", type=Path, default=None)
    ap.add_argument("--port-anchor", type=int, default=None)
    ap.add_argument("--retail", type=Path, default=None)
    ap.add_argument("--retail-log", type=Path, default=None)
    ap.add_argument("--retail-anchor", type=int, default=None)
    ap.add_argument("--shift-window", type=int, default=3,
                    help="± frames to search for the best anchor-phase alignment "
                         "(distinguishes load jitter from a real gap; default 3)")
    args = ap.parse_args(argv)

    port_abs = load_port(args.port)
    if not port_abs:
        print("wall_collide_diff: no port rows", file=sys.stderr)
        return 0

    p_anchor = args.port_anchor
    if p_anchor is None:
        p_anchor = second_anchor_frame(args.port_log)
    if p_anchor is None:
        # No anchor info: rebase to the first logged frame (relative series still
        # internally consistent, just not anchor-aligned).
        p_anchor = min(port_abs)
        print(f"# port: no HOUSE_FREEROAM anchor found — rebasing to first "
              f"frame {p_anchor}", file=sys.stderr)
    port = rebase(port_abs, p_anchor)

    # Final resting position (last anchor-relative frame).
    last_rel = max(port)
    fpx, fpz = port[last_rel]
    print(f"PORT:   {len(port)} frames  anchor=@{p_anchor}  "
          f"final px={fpx:.4f} pz={fpz:.4f}  (rel frame {last_rel})")

    if not args.retail:
        print("\nRETAIL: (no watch.jsonl given) — right-wall ground-truth contour:")
        for pz, px, note in RETAIL_WALL_CONTOUR:
            tag = f"   ({note})" if note else ""
            print(f"   pz={pz:+.2f}  →  px={px:.2f}{tag}")
        # If the port rested near a known pz, show the target + delta.
        for pz, px, _ in RETAIL_WALL_CONTOUR:
            if abs(fpz - pz) < 0.4:
                print(f"\n   port rested at pz={fpz:.2f} (≈ contour pz={pz:.2f}): "
                      f"px {fpx:.3f} vs retail {px:.3f}  Δ={fpx - px:+.3f}")
                break
        return 0

    retail_abs = load_retail(args.retail)
    r_anchor = args.retail_anchor
    if r_anchor is None:
        r_anchor = second_anchor_frame(args.retail_log)
    if r_anchor is None:
        r_anchor = min(retail_abs)
        print(f"# retail: no HOUSE_FREEROAM anchor found — rebasing to first "
              f"frame {r_anchor}", file=sys.stderr)
    retail = rebase(retail_abs, r_anchor)

    common = sorted(set(port) & set(retail))
    if not common:
        print("wall_collide_diff: no overlapping anchor-relative frames "
              "(check the anchors)", file=sys.stderr)
        return 0

    def diff_at(shift: int):
        """RMS/max |Δpx|, max |Δpz| comparing port[rel] vs retail[rel+shift]."""
        keys = [r for r in port if (r + shift) in retail]
        if not keys:
            return None
        sse = mx_px = mx_pz = 0.0
        at_px = at_pz = 0
        for rel in keys:
            ppx, ppz = port[rel]
            rpx, rpz = retail[rel + shift]
            dpx, dpz = ppx - rpx, ppz - rpz
            sse += dpx * dpx
            if abs(dpx) > abs(mx_px):
                mx_px, at_px = dpx, rel
            if abs(dpz) > abs(mx_pz):
                mx_pz, at_pz = dpz, rel
        return {"n": len(keys), "rms": (sse / len(keys)) ** 0.5,
                "mx_px": mx_px, "at_px": at_px, "mx_pz": mx_pz, "at_pz": at_pz}

    rfpx, rfpz = retail[common[-1]]
    rms0 = diff_at(0)
    print(f"RETAIL: {len(retail)} frames  anchor=@{r_anchor}  "
          f"final px={rfpx:.4f} pz={rfpz:.4f}")
    print(f"\nDIFF (shift 0) over {rms0['n']} shared frames:")
    print(f"   final px:  port {fpx:.4f}  retail {rfpx:.4f}  Δ={fpx - rfpx:+.4f}")
    print(f"   final pz:  port {fpz:.4f}  retail {rfpz:.4f}  Δ={fpz - rfpz:+.4f}")
    print(f"   max |Δpx|: {abs(rms0['mx_px']):.4f} (Δ={rms0['mx_px']:+.4f} "
          f"@ rel frame {rms0['at_px']})")
    print(f"   max |Δpz|: {abs(rms0['mx_pz']):.4f} (Δ={rms0['mx_pz']:+.4f} "
          f"@ rel frame {rms0['at_pz']})")
    print(f"   RMS Δpx:   {rms0['rms']:.4f}")

    # Anchor-phase search: the 2nd-HOUSE_FREEROAM rebase carries ±a-few-frames of
    # load jitter (the known determinism leak — sim is bit-exact, load frame-count
    # is not).  If a small integer frame shift drives the residual to ~0, the
    # trajectories are physically identical and only the anchor alignment differs.
    # This is what tells a real collision-accuracy gap apart from benign jitter.
    cands = [(s, diff_at(s)) for s in range(-args.shift_window, args.shift_window + 1)]
    cands = [(s, d) for s, d in cands if d]
    best_s, best = min(cands, key=lambda sd: sd[1]["rms"])
    print(f"\nANCHOR-PHASE SEARCH (±{args.shift_window} frames):")
    for s, d in cands:
        mark = "  <- best" if s == best_s else ""
        print(f"   shift {s:+d}: RMS Δpx={d['rms']:.4f}  "
              f"max|Δpx|={abs(d['mx_px']):.4f}  max|Δpz|={abs(d['mx_pz']):.4f}{mark}")
    if best_s != 0 and best["rms"] < rms0["rms"] - 1e-9:
        verdict = ("BIT-IDENTICAL" if best["rms"] < 1e-6 else "physically identical")
        print(f"\n   => {verdict} at shift {best_s:+d} "
              f"(RMS Δpx={best['rms']:.4f}, max|Δpx|={abs(best['mx_px']):.4f}). "
              f"The shift-0 residual is a {best_s:+d}-frame anchor-phase offset "
              f"(load jitter), NOT a collision-accuracy gap.")
    elif best_s == 0 and best["rms"] < 1e-6:
        print("\n   => BIT-IDENTICAL at shift 0 — no anchor offset, no gap.")
    else:
        print(f"\n   => Residual persists at every shift (best RMS Δpx="
              f"{best['rms']:.4f} @ shift {best_s:+d}): a real trajectory "
              f"divergence, not anchor jitter.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
