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
import datetime as dt
import html
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import yaml


ROOT          = Path(__file__).resolve().parent.parent
SCENARIOS_DIR = ROOT / "tests" / "scenarios"
RUNS_DIR      = ROOT / "runs" / "scenarios"
OUT_DIR       = ROOT / "runs" / "comparisons"
SCENARIO_TEST = ROOT / "tools" / "scenario-test.py"

# scenario-test.py writes run dirs as `<name>-<target>-<YYYYMMDDTHHMMSSZ>`.
# We only care about `--target both` runs here.
RUN_DIR_RE = re.compile(r"^(?P<name>.+)-both-(?P<ts>\d{8}T\d{6}Z)$")

DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "127.0.0.1:27042")


# ─── scenario + run discovery ─────────────────────────────────────────────


def discover_scenarios() -> list[Path]:
    if not SCENARIOS_DIR.is_dir():
        return []
    return sorted(p for p in SCENARIOS_DIR.iterdir()
                  if p.is_dir() and (p / "scenario.yaml").exists())


def latest_both_run(scen_name: str) -> Path | None:
    """Most recent `<name>-both-<ts>` run dir with a sidebyside.png inside,
    or None if no such run has been produced yet."""
    if not RUNS_DIR.is_dir():
        return None
    cands: list[tuple[str, Path]] = []
    for p in RUNS_DIR.iterdir():
        m = RUN_DIR_RE.match(p.name)
        if not m or m.group("name") != scen_name:
            continue
        if (p / "sidebyside.png").exists():
            cands.append((m.group("ts"), p))
    if not cands:
        return None
    cands.sort()
    return cands[-1][1]


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
           "--frida-remote", frida_remote]
    if turbo:
        cmd.append("--turbo")
    if silent_audio:
        cmd.append("--silent-audio")
    r = subprocess.run(cmd, cwd=str(ROOT))
    return r.returncode


# ─── artifact collection ─────────────────────────────────────────────────


def parse_run_ts(run_dir: Path) -> dt.datetime | None:
    m = RUN_DIR_RE.match(run_dir.name)
    if not m:
        return None
    try:
        return (dt.datetime
                .strptime(m.group("ts"), "%Y%m%dT%H%M%SZ")
                .replace(tzinfo=dt.timezone.utc))
    except ValueError:
        return None


def collect_artifacts(scenarios: list[Path]) -> list[dict]:
    """For each scenario, find latest --target both run + copy its
    sidebyside.png to a stable OUT_DIR/<scenario>/sidebyside.png so the
    HTML can use deterministic relative paths."""
    items: list[dict] = []
    for sp in scenarios:
        name = sp.name
        run_dir = latest_both_run(name)

        desc = ""
        try:
            data = yaml.safe_load((sp / "scenario.yaml").read_text()) or {}
            desc = str(data.get("description", ""))
        except Exception as e:
            print(f"  WARN: cannot read scenario.yaml for {name}: {e}",
                  file=sys.stderr)

        item = {
            "name":           name,
            "description":    desc,
            "run_dir":        run_dir,
            "sbs_rel":        None,
            "sbs_zoom_rel":   None,  # zoom companion present iff scenario has zoom_text
            "captured_at":    None,
            "captured_mtime": None,
            "run_dir_rel":    None,
        }

        if run_dir is not None:
            dest_dir = OUT_DIR / name
            dest_dir.mkdir(parents=True, exist_ok=True)
            dest = dest_dir / "sidebyside.png"
            shutil.copyfile(run_dir / "sidebyside.png", dest)

            ts = parse_run_ts(run_dir)
            item["sbs_rel"]        = f"{name}/sidebyside.png"
            item["captured_at"]    = ts.isoformat() if ts else "(unknown)"
            item["captured_mtime"] = int(dest.stat().st_mtime)
            item["run_dir_rel"]    = str(run_dir.relative_to(ROOT))

            # Optional zoomed-text companion — scenario-test.py only
            # writes this when the scenario's YAML carries `zoom_text:`.
            zoom_src = run_dir / "sidebyside-zoom.png"
            if zoom_src.exists():
                zoom_dest = dest_dir / "sidebyside-zoom.png"
                shutil.copyfile(zoom_src, zoom_dest)
                item["sbs_zoom_rel"] = f"{name}/sidebyside-zoom.png"

        items.append(item)
    return items


# ─── HTML rendering ───────────────────────────────────────────────────────


HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>openrecet ↔ retail comparisons</title>
<style>
:root {{
    color-scheme: dark;
    --bg: #15161a;
    --panel: #20222a;
    --text: #d8dae0;
    --muted: #8a8d97;
    --accent: #7aa8ff;
    --stale: #f3884b;
    --very-stale: #ef4444;
}}
* {{ box-sizing: border-box; }}
body {{
    background: var(--bg);
    color: var(--text);
    font-family: system-ui, -apple-system, sans-serif;
    max-width: 1180px;
    margin: 0 auto;
    padding: 16px;
    font-size: 14px;
}}
header {{ border-bottom: 1px solid #2b2d35; padding-bottom: 8px; margin-bottom: 16px; }}
h1 {{ font-size: 18px; margin: 0; }}
.regen {{ color: var(--muted); font-size: 12px; margin-top: 4px; font-family: ui-monospace, monospace; }}
.toc {{ font-size: 12px; margin: 8px 0 16px; }}
.toc a {{ color: var(--accent); text-decoration: none; margin-right: 12px; }}
.toc a:hover {{ text-decoration: underline; }}
.scenario {{
    background: var(--panel);
    border-radius: 6px;
    padding: 12px 14px;
    margin-bottom: 20px;
    scroll-margin-top: 8px;
}}
.scenario h2 {{ font-size: 15px; margin: 0; }}
.scenario h2 a {{ color: var(--text); text-decoration: none; }}
.scenario h2 a:hover {{ color: var(--accent); }}
.meta {{ color: var(--muted); font-size: 11px; margin: 4px 0 6px; font-family: ui-monospace, monospace; }}
.desc {{ color: #b6bac3; font-size: 13px; margin: 6px 0 10px; line-height: 1.4; white-space: pre-wrap; }}
img.sbs {{ max-width: 100%; display: block; border: 1px solid #2b2d35; border-radius: 4px; }}
img.sbs-zoom {{ image-rendering: pixelated; image-rendering: crisp-edges; margin-top: 6px; }}
.zoom-label {{ color: var(--muted); font-size: 11px; margin: 12px 0 4px; font-family: ui-monospace, monospace; }}
.missing {{ color: var(--very-stale); font-size: 12px; }}
.age {{ color: inherit; }}
.age.stale {{ color: var(--stale); }}
.age.very-stale {{ color: var(--very-stale); }}
</style>
</head>
<body>
<header>
<h1>openrecet ↔ retail comparisons</h1>
<div class="regen">regen run: {regen_iso} &middot; manual refresh (Ctrl-R) to pull updated images &middot; <span class="age" data-mtime="{regen_mtime}">just now</span></div>
</header>

<nav class="toc">
Tests: {toc}
</nav>

{cards}

<script>
// Computes "X minutes ago" labels and colors anything stale relative to
// the page-load time. Purely cosmetic; the absolute timestamps are the
// real staleness signal.
(function () {{
    function relAge(now, then) {{
        const dt = (now - then) / 1000;
        if (dt < 60)    return Math.round(dt) + "s ago";
        if (dt < 3600)  return Math.round(dt / 60) + "m ago";
        if (dt < 86400) return (dt / 3600).toFixed(1) + "h ago";
        return (dt / 86400).toFixed(1) + "d ago";
    }}
    const now = Date.now();
    for (const el of document.querySelectorAll(".age[data-mtime]")) {{
        const m = parseInt(el.getAttribute("data-mtime"), 10) * 1000;
        if (!m) continue;
        el.textContent = relAge(now, m);
        const dt = (now - m) / 1000;
        if (dt > 24 * 3600) el.classList.add("very-stale");
        else if (dt > 3600) el.classList.add("stale");
    }}
}})();
</script>
</body>
</html>
"""


def render_html(items: list[dict], out_path: Path) -> None:
    now = dt.datetime.now(dt.timezone.utc)
    regen_iso    = now.strftime("%Y-%m-%dT%H:%M:%SZ")
    regen_mtime  = int(now.timestamp())

    toc_links = " ".join(
        f'<a href="#{html.escape(it["name"])}">{html.escape(it["name"])}</a>'
        for it in items
    )

    cards: list[str] = []
    for it in items:
        name = html.escape(it["name"])
        desc = html.escape(it["description"]) if it["description"] else ""

        if it["sbs_rel"] is None:
            meta_line = ""
            body = (
                f'<div class="missing">no --target both run yet for this scenario. '
                f'Run: <code>tools/regen-comparisons.py</code> '
                f'(or <code>tools/scenario-test.py {name} --target both</code>).</div>'
            )
        else:
            sbs = html.escape(it["sbs_rel"])
            mt  = it["captured_mtime"]
            run_rel = html.escape(it["run_dir_rel"])
            meta_line = (
                f'<div class="meta">'
                f'captured {html.escape(it["captured_at"])} &middot; '
                f'<span class="age" data-mtime="{mt}">just now</span> &middot; '
                f'left=openrecet, right=retail &middot; '
                f'src={run_rel}'
                f'</div>'
            )
            body = (
                f'<img class="sbs" src="{sbs}?v={mt}" '
                f'alt="{name} openrecet|retail side-by-side">'
            )
            if it["sbs_zoom_rel"]:
                zoom = html.escape(it["sbs_zoom_rel"])
                body += (
                    '<div class="zoom-label">↓ zoom-text ×N companion '
                    '(nearest-neighbor; pixel grid preserved for font diffs)</div>'
                    f'<img class="sbs sbs-zoom" src="{zoom}?v={mt}" '
                    f'alt="{name} openrecet|retail zoom-text side-by-side">'
                )

        desc_block = f'<div class="desc">{desc}</div>' if desc else ""
        cards.append(
            f'<section class="scenario" id="{name}">'
            f'<h2><a href="#{name}">{name}</a></h2>'
            f'{meta_line}'
            f'{desc_block}'
            f'{body}'
            f'</section>'
        )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(HTML_TEMPLATE.format(
        regen_iso=regen_iso,
        regen_mtime=regen_mtime,
        toc=toc_links,
        cards="\n".join(cards),
    ))


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

    items = collect_artifacts(scenarios)
    render_html(items, OUT_DIR / "index.html")

    print()
    print(f"index: {OUT_DIR / 'index.html'}")
    print(f"open in browser:")
    print(f"  file://{OUT_DIR / 'index.html'}")

    n_present = sum(1 for it in items if it["sbs_rel"] is not None)
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
