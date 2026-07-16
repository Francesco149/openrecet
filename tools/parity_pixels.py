#!/usr/bin/env python3
"""tools/parity_pixels.py — produce the `pixels` pillar's metrics for a v3 window.

The `pixels` proof pillar needs a real per-frame differ measurement, not a source
marker. This is the producer CLI: given a scenario + an already-captured Trace Studio
v3 window, it replays the port and retail command streams for every identity-paired
frame in the contract's join window, measures the bit-exact differing-pixel count
(pixel_diff.amplified_diff), and writes <window_dir>/pixel-metrics.json — the doc
tools/parity_prove.py then consumes to give the `pixels` pillar a PASS/FAIL verdict
instead of NOT_CAPTURED.

Two-step flow (the heavy retail drive stays in orv3_window, serialized):

    # 1. capture the window (drives retail once, caches; re-drives port on a rebuild)
    orv3_window.py <scenario> --window OFF:COUNT --view
    # 2. produce the pixels metrics for its contract join window
    parity_pixels.py <scenario> --window OFF:COUNT
    # 3. compile the proof (now the pixels pillar has a real verdict)
    parity_prove.py <scenario> --window OFF:COUNT --env-json <env.json> --json

Exit: 0 wrote metrics (see the differ summary), 2 fatal (no window / render failure).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import parity_prove  # noqa: E402  (contract loading + window resolution)
from parity import load_required  # noqa: E402
from parity.observations import ObservationError  # noqa: E402
from parity.pixel_producer import PixelProducerError, produce_for_window  # noqa: E402


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="produce the pixels pillar metrics for a v3 window")
    ap.add_argument("scenario")
    ap.add_argument("--from-window", type=Path, default=None,
                    help="an existing v3 window dir (pairs.json + containers)")
    ap.add_argument("--window", default=None, metavar="OFF:COUNT",
                    help="locate runs/studio-v3-windows/<scenario>/win-OFF-COUNT")
    ap.add_argument("--contract", type=Path, default=None,
                    help="external contract yaml (default: the scenario.yaml proof block)")
    ap.add_argument("--json", action="store_true", help="print a JSON summary")
    args = ap.parse_args(argv)

    try:
        window_dir = args.from_window
        if window_dir is None and args.window:
            off, _, count = args.window.partition(":")
            window_dir = ROOT / "runs/studio-v3-windows" / args.scenario / f"win-{off}-{count}"
        if window_dir is None:
            raise parity_prove.ProveError("pass --from-window <dir> or --window OFF:COUNT")
        if not window_dir.is_dir():
            raise parity_prove.ProveError(f"window dir not found: {window_dir}")

        contract_doc = parity_prove.load_scenario_contract(args.scenario, contract_path=args.contract)
        contract = contract_doc["proof"]
        window = parity_prove.contract_window(contract)
        mode = (contract.get("pixels") or {}).get("mode", "exact")

        pairs_path = window_dir / "pairs.json"
        required = load_required(pairs_path, window)
        if not required:
            raise PixelProducerError(
                "no required frames in the contract window ∩ identity join "
                "(check proof.join.window vs the captured window)")

        doc, out = produce_for_window(window_dir, required, mode=mode)

    except (parity_prove.ProveError, PixelProducerError, ObservationError) as exc:
        if args.json:
            print(json.dumps({"error": str(exc), "exit_code": 2}, indent=1), file=sys.stderr)
        else:
            print(f"parity_pixels: {exc}", file=sys.stderr)
        return 2

    frames = doc["frames"]
    n_diff = sum(1 for f in frames if f["differ"] > 0)
    worst = max(frames, key=lambda f: f["differ"], default=None)
    summary = {
        "scenario": args.scenario,
        "wrote": str(out),
        "mode": mode,
        "frames": len(frames),
        "frames_differ": n_diff,
        "frames_identical": len(frames) - n_diff,
        "worst": ({"key": worst["key"], "differ": worst["differ"], "total": worst["total"],
                   "meanabs": worst["meanabs"]} if worst else None),
        "source": doc.get("source"),
        "exit_code": 0,
    }
    if args.json:
        print(json.dumps(summary, indent=1))
    else:
        print(f"pixels: wrote {out}")
        print(f"  {len(frames)} frames  ·  {summary['frames_identical']} bit-identical  ·  "
              f"{n_diff} differ  (mode {mode})")
        if worst and worst["differ"] > 0:
            k = worst["key"]
            print(f"  worst: {k[0]}#{k[1]}+{k[2]}  {worst['differ']}/{worst['total']} px  "
                  f"mean|Δ|/ch={worst['meanabs']}")
        elif worst:
            print("  ALL required frames bit-identical")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
