#!/usr/bin/env python3
"""tools/parity_save.py — produce the `save` pillar's metrics for a scenario.

The `save` proof pillar needs a real byte-for-byte comparison of the save file
each side writes, not a source marker. This is the producer CLI: given a
save-committing scenario, it reads the two save.dat files a `scenario-test
--target both` drive left in its per-side `saveout/` sandboxes, compares the
~18 MB arenas, localizes the first divergence to a canonical-state region (ST-00),
and writes save-metrics.json — the doc tools/parity_prove.py consumes to give the
`save` pillar a PASS/FAIL verdict instead of NOT_CAPTURED.

The save is scenario-scoped (one committed artifact, not a per-frame join), so its
evidence comes from a `--target both` drive, NOT the Trace Studio v3 window. It is
deposited INTO the v3 window dir so parity_prove --window finds it beside pairs/
view/pixel-metrics.

Flow:

    # 1. drive both sides of a save-committing scenario (writes the two save.dat)
    scenario-test.py <scenario> --target both        # (or pass --drive here)
    # 2. produce the save metrics, deposited in the v3 window dir
    parity_save.py <scenario> --window OFF:COUNT      # auto-locates the newest both-run
    # 3. compile the proof (now the save pillar has a real verdict)
    parity_prove.py <scenario> --window OFF:COUNT --env-json <env.json> --json

Exit: 0 wrote metrics (see the diff summary), 2 fatal (no save.dat / bad size).
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import parity_prove  # noqa: E402  (contract/window helpers, ProveError)
from parity.save_producer import (  # noqa: E402
    SaveProducerError,
    produce_from_run_dir,
    saveout_pair,
)

SCEN_RUNS = ROOT / "runs/scenarios"


def _latest_both_run(scenario: str) -> Path:
    """Newest runs/scenarios/<scenario>-both-<ts> dir (timestamps sort lexically)."""
    cands = sorted(SCEN_RUNS.glob(f"{scenario}-both-*"))
    if not cands:
        raise SaveProducerError(
            f"no --target both run under {SCEN_RUNS}/{scenario}-both-* — drive it "
            f"first (scenario-test.py {scenario} --target both) or pass --drive")
    return cands[-1]


def _drive_both(scenario: str) -> Path:
    """Run scenario-test --target both, then return the run dir it produced."""
    before = set(SCEN_RUNS.glob(f"{scenario}-both-*"))
    r = subprocess.run(
        [sys.executable, str(ROOT / "tools/scenario-test.py"), scenario, "--target", "both"],
        cwd=str(ROOT))
    if r.returncode != 0:
        raise SaveProducerError(f"scenario-test --target both exited {r.returncode}")
    after = set(SCEN_RUNS.glob(f"{scenario}-both-*"))
    new = sorted(after - before)
    return new[-1] if new else _latest_both_run(scenario)


def _deposit_dir(args, scenario: str) -> Path:
    """Where save-metrics.json is written so parity_prove finds it (the v3 window
    dir), or --into, or (fallback) the run dir itself."""
    if args.into:
        return args.into
    if args.from_window:
        return args.from_window
    if args.window:
        off, _, count = args.window.partition(":")
        return ROOT / "runs/studio-v3-windows" / scenario / f"win-{off}-{count}"
    return None  # caller falls back to run_dir


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="produce the save pillar metrics for a scenario")
    ap.add_argument("scenario")
    ap.add_argument("--run-dir", type=Path, default=None,
                    help="a scenario-test --target both run dir (default: newest for the scenario)")
    ap.add_argument("--drive", action="store_true",
                    help="run scenario-test --target both first, then use that run")
    ap.add_argument("--window", default=None, metavar="OFF:COUNT",
                    help="deposit save-metrics.json in runs/studio-v3-windows/<scen>/win-OFF-COUNT")
    ap.add_argument("--from-window", type=Path, default=None,
                    help="deposit save-metrics.json in this v3 window dir")
    ap.add_argument("--into", type=Path, default=None,
                    help="deposit save-metrics.json in this dir (overrides --window/--from-window)")
    ap.add_argument("--json", action="store_true", help="print a JSON summary")
    args = ap.parse_args(argv)

    try:
        if args.drive:
            run_dir = _drive_both(args.scenario)
        elif args.run_dir:
            run_dir = args.run_dir
        else:
            run_dir = _latest_both_run(args.scenario)
        if not run_dir.is_dir():
            raise SaveProducerError(f"run dir not found: {run_dir}")

        out_dir = _deposit_dir(args, args.scenario) or run_dir
        if not out_dir.is_dir():
            raise SaveProducerError(
                f"deposit dir not found: {out_dir} — capture the v3 window first "
                f"(orv3_window {args.scenario} --window … --view)")

        # fail closed early with a clear message if the two save.dat aren't there
        saveout_pair(run_dir)
        doc, out = produce_from_run_dir(run_dir, out_dir / "save-metrics.json")

    except (parity_prove.ProveError, SaveProducerError) as exc:
        if args.json:
            print(json.dumps({"error": str(exc), "exit_code": 2}, indent=1), file=sys.stderr)
        else:
            print(f"parity_save: {exc}", file=sys.stderr)
        return 2

    fd = doc.get("first_divergence")
    summary = {
        "scenario": args.scenario,
        "run_dir": str(run_dir),
        "wrote": str(out),
        "arena_bytes": doc["arena_bytes"],
        "identical": doc["identical"],
        "ndiff": doc["ndiff"],
        "first_divergence": ({"byte_off": fd["byte_off"], "path": fd["path"],
                              "region": fd["region"], "class": fd["class"],
                              "port_byte": fd["port_byte"], "retail_byte": fd["retail_byte"]}
                             if fd else None),
        "regions_differ": len(doc.get("region_summary") or []),
        "source": doc.get("source"),
        "exit_code": 0,
    }
    if args.json:
        print(json.dumps(summary, indent=1))
    else:
        print(f"save: wrote {out}")
        if doc["identical"]:
            print(f"  save arenas BYTE-IDENTICAL ({doc['arena_bytes']} bytes)  → PASS")
        else:
            print(f"  {doc['ndiff']} bytes differ across {summary['regions_differ']} region(s)  → FAIL")
            print(f"  first divergence @ {fd['path']}  (byte {fd['byte_off']}, "
                  f"port 0x{fd['port_byte']:02x} vs retail 0x{fd['retail_byte']:02x})")
            if fd.get("note"):
                print(f"    {fd['region']}: {fd['note']}")
            for r in (doc.get("region_summary") or [])[:10]:
                if r["scope"] == "header" or r.get("n_banks", 0) <= 1:
                    where = r["region"] if r["scope"] == "header" else f"{r['region']} @bank{r.get('bank_min')}"
                else:
                    where = f"{r['region']} @banks{r['bank_min']}-{r['bank_max']}"
                print(f"    {where:<40} {r['ndiff']:>7} bytes  (class {r['class']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
