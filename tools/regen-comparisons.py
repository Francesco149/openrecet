#!/usr/bin/env python3
"""
tools/regen-comparisons.py — refresh the cross-target comparison suite.

For each scenario under tests/scenarios/, run scenario-test.py --target
both to produce a side-by-side openrecet|retail capture, then assemble a
static HTML index at runs/comparisons/index.html. Each scenario card
shows its capture timestamp + a relative-age tag so stale data is
visible at a glance. Ctrl-R always fetches the latest images thanks to
?v=<mtime> cache-busting on every <img src>.

The index page is a static file with no auto-refresh:
  - open it with file://runs/comparisons/index.html
  - run this script after each shipped change
  - Ctrl-R in the browser to pull updated images

Usage:
    tools/regen-comparisons.py                            # full regen, all scenarios
    tools/regen-comparisons.py --html-only                # rebuild HTML from latest existing runs
    tools/regen-comparisons.py --frida-remote HOST:PORT   # passed through to scenario-test.py
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

import comparison_page


ROOT          = Path(__file__).resolve().parent.parent
SCENARIOS_DIR = ROOT / "tests" / "scenarios"
RUNS_DIR      = ROOT / "runs" / "scenarios"
OUT_DIR       = ROOT / "runs" / "comparisons"
SCENARIO_TEST = ROOT / "tools" / "scenario-test.py"

DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")


# ─── scenario + run discovery ─────────────────────────────────────────────


def discover_scenarios() -> list[Path]:
    if not SCENARIOS_DIR.is_dir():
        return []
    return sorted(p for p in SCENARIOS_DIR.iterdir()
                  if p.is_dir() and (p / "scenario.yaml").exists())


# ─── regen ─────────────────────────────────────────────────────────────────


def run_one_scenario(name: str, frida_remote: str, *,
                     turbo: bool = True,
                     silent_audio: bool = True) -> int:
    """Drive scenario-test.py --target both for `name`. Returns its exit code.
    The diff-against-golden side-effects are unchanged (and will print FAIL
    lines if a scenario has no golden-retail/ yet — harmless: sidebyside.png
    is still produced).

    Defaults to `--turbo --silent-audio` since the whole point of this
    script is to crank through every scenario in sequence — turbo halves
    each scenario's wall time and silent_audio keeps the host quiet."""
    cmd = [sys.executable, str(SCENARIO_TEST), name, "--target", "both",
           "--frida-remote", frida_remote,
           # We rebuild the gallery once after the whole batch, so suppress
           # scenario-test's own per-run regen + viewer-open.
           "--no-regen"]
    if turbo:
        cmd.append("--turbo")
    if silent_audio:
        cmd.append("--silent-audio")
    r = subprocess.run(cmd, cwd=str(ROOT))
    return r.returncode


# ─── cli ──────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--html-only", action="store_true",
                    help="skip running scenarios; just rebuild HTML from the "
                         "latest existing --target both run dirs")
    ap.add_argument("--frida-remote", default=DEFAULT_REMOTE,
                    help="passed through to scenario-test.py "
                         "(default %(default)s; env $OPENRECET_FRIDA_REMOTE)")
    ap.add_argument("--no-turbo", action="store_true",
                    help="run each scenario at real-time 60 FPS instead of "
                         "the default frame-limiter-bypass turbo mode "
                         "(audible BGM + slower wall-clock per scenario)")
    ap.add_argument("--no-silent-audio", action="store_true",
                    help="leave audio output audible. Default silences it "
                         "since this script is typically a background batch "
                         "and BGM looping over and over is annoying.")
    ap.add_argument("--open", action="store_true",
                    help="open the rebuilt index.html in the default Windows "
                         "viewer when done (WSL)")
    args = ap.parse_args(argv)

    scenarios = discover_scenarios()
    if not scenarios:
        print("no scenarios under tests/scenarios/")
        return 0
    print(f"discovered {len(scenarios)} scenario(s): "
          + ", ".join(p.name for p in scenarios))

    rc = 0
    if not args.html_only:
        for sp in scenarios:
            print(f"\n=== regenerating {sp.name} ===")
            sub_rc = run_one_scenario(
                sp.name, args.frida_remote,
                turbo=not args.no_turbo,
                silent_audio=not args.no_silent_audio)
            if sub_rc != 0:
                print(f"  WARN: {sp.name} exited {sub_rc}", file=sys.stderr)
                rc = max(rc, sub_rc)

    items = comparison_page.collect_artifacts(scenarios, RUNS_DIR, OUT_DIR)
    comparison_page.render_html(items, OUT_DIR / "index.html")

    print()
    print(f"index: {OUT_DIR / 'index.html'}")
    print(f"open in browser:")
    print(f"  file://{OUT_DIR / 'index.html'}")
    if args.open:
        comparison_page.open_in_viewer(OUT_DIR / "index.html")

    n_present = sum(1 for it in items if it["captures"])
    n_missing = len(items) - n_present
    if n_missing:
        print(f"\n  {n_missing} scenario(s) have no --target both run yet")

    # We don't propagate the per-scenario diff exit code as our own; the
    # HTML is what the user cares about. Surface it with a hint instead.
    if rc != 0:
        print(f"\nNote: at least one scenario exited {rc} during regen "
              f"(usually means a per-target golden diff failed). The HTML "
              f"still reflects the latest captures.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
