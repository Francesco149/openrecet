#!/usr/bin/env python3
"""
tools/comparison_page.py — interactive openrecet↔retail comparison gallery.

Shared rendering backend for tools/regen-comparisons.py (batch) and
tools/scenario-test.py (auto-regen after a --target both run). Builds, per
capture, a single ATLAS PNG laid out as:

    row 0:  [ openrecet | retail ]      ← always visible in the page
    row 1:  [ amplified pixel-diff   ]  ← hidden until the card is clicked

and emits a dark-theme static HTML index whose clever trick is: the page
shows only [left | right]; clicking a comparison expands it to reveal the
amplified diff below; right-click → "Copy Image" copies the *full* atlas
bitmap (all three panels) regardless of the CSS clip, so a pasted screenshot
is the complete [left | right | diff] montage while the page stays clean.

The diff math is tools/pixel_diff.amplified_diff (BLACK = bit-identical,
amplified toward WHITE where the two captures differ), so the gallery shows
exactly the canonical render-parity diff the CLI does.

Pairing:
  - segtrace scenarios: the Nth left capture vs the Nth right capture
    (absolute frames jitter port-vs-retail under anchor-relative timing).
  - legacy scenarios: pair by filename (same absolute frame on both sides).

Pure tooling — no engine. Importable (no hyphen in the name).
"""

from __future__ import annotations

import datetime as dt
import html
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# amplified_diff lives in pixel_diff.py (no hyphen → importable).
from pixel_diff import amplified_diff


ROOT = Path(__file__).resolve().parent.parent

# Display geometry. Each row-0 panel is rendered at PANEL_W×PANEL_H (4:3);
# the diff row spans the full atlas width at the capture's native aspect.
PANEL_W = 460
PANEL_H = 345
LABEL_H = 18
AMP_DEFAULT = 6.0


# ─── trace classification (kept in sync with scenario-test._inspect_trace) ──


def inspect_trace(trace_path: Path) -> tuple[bool, int]:
    """(is_segtrace, n_captures) for a scenario trace. A trace is a segtrace
    if any line carries a `wait` or `capture` op; n_captures counts `capture`
    ops. Comments / blanks ignored. Missing file → legacy with 0 captures."""
    if not trace_path.exists():
        return False, 0
    is_seg = False
    n_cap = 0
    for raw in trace_path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            continue
        if "wait" in rec:
            is_seg = True
        if "capture" in rec:
            is_seg = True
            n_cap += 1
    return is_seg, n_cap


# ─── atlas building ─────────────────────────────────────────────────────────


def _font(size: int) -> ImageFont.FreeTypeFont:
    try:
        return ImageFont.truetype("DejaVuSansMono.ttf", size)
    except OSError:
        return ImageFont.load_default()


def _frame_no(p: Path) -> int:
    try:
        return int(p.stem.split("_")[1])
    except (IndexError, ValueError):
        return -1


def _list_frames(frames_dir: Path) -> list[Path]:
    if not frames_dir.is_dir():
        return []
    return sorted(frames_dir.glob("frame_*.bmp"), key=_frame_no)


def _panel(img: Image.Image, label: str) -> Image.Image:
    """A PANEL_W×(PANEL_H+LABEL_H) tile: label strip + LANCZOS-fit photo."""
    canvas = Image.new("RGB", (PANEL_W, PANEL_H + LABEL_H), (24, 24, 28))
    draw = ImageDraw.Draw(canvas)
    draw.text((4, 2), label, fill=(210, 214, 224), font=_font(13))
    fit = img.convert("RGB").resize((PANEL_W, PANEL_H), Image.LANCZOS)
    canvas.paste(fit, (0, LABEL_H))
    return canvas


def build_capture_atlas(left_png: Path, right_png: Path, out_path: Path,
                        label: str, amp: float = AMP_DEFAULT) -> dict | None:
    """Build one capture's atlas (row0 [left|right], row1 amplified diff).

    Returns a dict describing the atlas geometry + diff stats, or None if a
    side is unreadable. Geometry is given as width-relative percentages so the
    page can responsively clip the wrapper to row 0 and expand it to the full
    atlas on click (see render_html).
    """
    try:
        la = Image.open(left_png).convert("RGB")
        ra = Image.open(right_png).convert("RGB")
    except (OSError, ValueError):
        return None

    # Diff at native resolution (crop to the common region if the two
    # captures somehow differ in size — they shouldn't, retail is res-forced).
    cw = min(la.width, ra.width)
    ch = min(la.height, ra.height)
    diff_rgb, differ, meanabs = amplified_diff(
        np.asarray(la.crop((0, 0, cw, ch))),
        np.asarray(ra.crop((0, 0, cw, ch))),
        amp,
    )

    atlas_w = PANEL_W * 2
    row0_h  = PANEL_H + LABEL_H

    # Diff row: full atlas width, native aspect, NEAREST so amplified diff
    # pixels stay crisp. Plus its own label strip.
    diff_native = Image.fromarray(diff_rgb, "RGB")
    diff_disp_h = max(1, round(atlas_w * ch / cw))
    diff_fit = diff_native.resize((atlas_w, diff_disp_h), Image.NEAREST)
    diff_label = (f"diff ×{amp:g}  ·  {differ} px differ  ·  "
                  f"mean|abs|/ch {meanabs:.2f}  ·  black = bit-identical")
    row1_h = diff_disp_h + LABEL_H
    diff_panel = Image.new("RGB", (atlas_w, row1_h), (12, 12, 14))
    ddraw = ImageDraw.Draw(diff_panel)
    ddraw.text((4, 2), diff_label, fill=(255, 240, 120), font=_font(13))
    diff_panel.paste(diff_fit, (0, LABEL_H))

    total_h = row0_h + row1_h
    atlas = Image.new("RGB", (atlas_w, total_h), (12, 12, 14))
    atlas.paste(_panel(la, f"openrecet · {label}"), (0, 0))
    atlas.paste(_panel(ra, f"retail · {label}"),     (PANEL_W, 0))
    atlas.paste(diff_panel, (0, row0_h))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(out_path, optimize=True)

    return {
        "label":     label,
        "rel":       None,    # filled by collect_artifacts (relative to OUT_DIR)
        "differ_px": differ,
        "meanabs":   round(meanabs, 2),
        "row0_pct":  round(100.0 * row0_h / atlas_w, 4),
        "total_pct": round(100.0 * total_h / atlas_w, 4),
    }


def build_scenario_atlases(run_dir: Path, dest_dir: Path, *,
                           is_segtrace: bool,
                           amp: float = AMP_DEFAULT) -> list[dict]:
    """Build every capture atlas for one scenario's latest --target both run.

    `run_dir` is the `<name>-both-<ts>` dir (with openrecet/ + retail/
    subdirs). Atlases are written to `dest_dir` as atlas_NN.png. Returns the
    per-capture dicts (with `rel` set to the dest filename)."""
    left  = _list_frames(run_dir / "openrecet" / "frames")
    right = _list_frames(run_dir / "retail" / "frames")

    if is_segtrace:
        # Pair by capture index (absolute frames differ across targets).
        pairs = [(i,
                  left[i]  if i < len(left)  else None,
                  right[i] if i < len(right) else None,
                  f"cap_{i:02d}")
                 for i in range(max(len(left), len(right)))]
    else:
        # Pair by filename (same absolute frame on both targets).
        lmap = {p.name: p for p in left}
        rmap = {p.name: p for p in right}
        names = sorted(set(lmap) | set(rmap))
        pairs = [(i, lmap.get(n), rmap.get(n), f"frame {_frame_no(Path(n)):05d}")
                 for i, n in enumerate(names)]

    dest_dir.mkdir(parents=True, exist_ok=True)
    out: list[dict] = []
    for idx, lp, rp, label in pairs:
        if lp is None or rp is None:
            # One side missing — record a placeholder so the gap is visible.
            out.append({
                "label": label, "rel": None, "differ_px": None,
                "meanabs": None, "row0_pct": None, "total_pct": None,
                "missing": "openrecet" if lp is None else "retail",
            })
            continue
        atlas_name = f"atlas_{idx:02d}.png"
        meta = build_capture_atlas(lp, rp, dest_dir / atlas_name, label, amp)
        if meta is None:
            continue
        meta["rel"] = atlas_name
        out.append(meta)
    return out


# ─── artifact collection ────────────────────────────────────────────────────


import re

import yaml

RUN_DIR_RE = re.compile(r"^(?P<name>.+)-both-(?P<ts>\d{8}T\d{6}Z)$")


def _parse_run_ts(run_dir: Path):
    m = RUN_DIR_RE.match(run_dir.name)
    if not m:
        return None
    try:
        return (dt.datetime.strptime(m.group("ts"), "%Y%m%dT%H%M%SZ")
                .replace(tzinfo=dt.timezone.utc))
    except ValueError:
        return None


def latest_both_run(scen_name: str, runs_dir: Path) -> Path | None:
    """Most recent `<name>-both-<ts>` run dir that produced a side-by-side
    (the completion marker), or None."""
    if not runs_dir.is_dir():
        return None
    cands: list[tuple[str, Path]] = []
    for p in runs_dir.iterdir():
        m = RUN_DIR_RE.match(p.name)
        if not m or m.group("name") != scen_name:
            continue
        if (p / "sidebyside.png").exists():
            cands.append((m.group("ts"), p))
    if not cands:
        return None
    cands.sort()
    return cands[-1][1]


def collect_artifacts(scenarios: list[Path], runs_dir: Path, out_dir: Path,
                      amp: float = AMP_DEFAULT) -> list[dict]:
    """For each scenario, find its latest --target both run, build per-capture
    atlases under out_dir/<name>/, and return the gallery item dicts."""
    items: list[dict] = []
    for sp in scenarios:
        name = sp.name
        is_seg, _ = inspect_trace(sp / "trace.jsonl")

        desc = ""
        try:
            data = yaml.safe_load((sp / "scenario.yaml").read_text()) or {}
            desc = str(data.get("description", ""))
        except Exception as e:  # pragma: no cover
            print(f"  WARN: cannot read scenario.yaml for {name}: {e}",
                  file=sys.stderr)

        run_dir = latest_both_run(name, runs_dir)
        item = {
            "name": name, "description": desc, "is_segtrace": is_seg,
            "captures": [], "captured_at": None, "captured_mtime": None,
            "run_dir_rel": None,
        }
        if run_dir is not None:
            dest = out_dir / name
            caps = build_scenario_atlases(run_dir, dest, is_segtrace=is_seg, amp=amp)
            # Atlases are written under out_dir/<name>/; the HTML lives at
            # out_dir/index.html, so each img src needs the <name>/ prefix.
            for c in caps:
                if c.get("rel"):
                    c["rel"] = f"{name}/{c['rel']}"
            ts = _parse_run_ts(run_dir)
            item["captures"]       = caps
            item["captured_at"]    = ts.isoformat() if ts else "(unknown)"
            item["captured_mtime"] = int(dt.datetime.now(dt.timezone.utc).timestamp())
            try:
                item["run_dir_rel"] = str(run_dir.relative_to(ROOT))
            except ValueError:
                item["run_dir_rel"] = str(run_dir)
        items.append(item)
    return items


# ─── viewer ─────────────────────────────────────────────────────────────────


def open_in_viewer(path: Path) -> None:
    """Open `path` with the default Windows viewer (WSL); no-op + note
    otherwise. Factored from montage_frames._open_windows."""
    try:
        win = subprocess.run(["wslpath", "-w", str(path.resolve())],
                             capture_output=True, text=True, check=True).stdout.strip()
        subprocess.run(["explorer.exe", win], check=False)
    except (FileNotFoundError, subprocess.CalledProcessError):
        print(f"comparison_page: could not auto-open {path} (not on WSL?)",
              file=sys.stderr)


# ─── HTML ───────────────────────────────────────────────────────────────────


_HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>openrecet ↔ retail comparisons</title>
<style>
:root {{
    color-scheme: dark;
    --bg: #15161a; --panel: #20222a; --text: #d8dae0; --muted: #8a8d97;
    --accent: #7aa8ff; --stale: #f3884b; --very-stale: #ef4444;
}}
* {{ box-sizing: border-box; }}
body {{
    background: var(--bg); color: var(--text);
    font-family: system-ui, -apple-system, sans-serif;
    max-width: 1080px; margin: 0 auto; padding: 16px; font-size: 14px;
}}
header {{ border-bottom: 1px solid #2b2d35; padding-bottom: 8px; margin-bottom: 16px; }}
h1 {{ font-size: 18px; margin: 0; }}
.regen {{ color: var(--muted); font-size: 12px; margin-top: 4px; font-family: ui-monospace, monospace; }}
.hint {{ color: var(--muted); font-size: 12px; margin-top: 4px; }}
.toc {{ font-size: 12px; margin: 8px 0 16px; }}
.toc a {{ color: var(--accent); text-decoration: none; margin-right: 12px; }}
.toc a:hover {{ text-decoration: underline; }}
.scenario {{
    background: var(--panel); border-radius: 6px; padding: 12px 14px;
    margin-bottom: 20px; scroll-margin-top: 8px;
}}
.scenario h2 {{ font-size: 15px; margin: 0; }}
.scenario h2 a {{ color: var(--text); text-decoration: none; }}
.scenario h2 a:hover {{ color: var(--accent); }}
.meta {{ color: var(--muted); font-size: 11px; margin: 4px 0 6px; font-family: ui-monospace, monospace; }}
.desc {{ color: #b6bac3; font-size: 13px; margin: 6px 0 10px; line-height: 1.4; white-space: pre-wrap; }}
.cap {{ margin: 0 0 14px; }}
/* The wrapper clips the atlas to row 0 ([left|right]); .open expands it to
 * the full atlas, revealing the diff row. padding-bottom is a % of WIDTH, so
 * the clip scales responsively with the column width. The <img> is the whole
 * atlas, so browser "Copy Image" yields the 3-up montage regardless. */
.atlas-wrap {{
    position: relative; width: 100%; height: 0; overflow: hidden;
    border: 1px solid #2b2d35; border-radius: 4px; cursor: zoom-in;
    padding-bottom: var(--r0); transition: padding-bottom .18s ease;
}}
.atlas-wrap.open {{ padding-bottom: var(--tot); cursor: zoom-out; }}
.atlas-wrap img {{ position: absolute; top: 0; left: 0; width: 100%; display: block; }}
.capcap {{ color: var(--muted); font-size: 11px; margin: 4px 2px 0; font-family: ui-monospace, monospace; }}
.capcap .ok {{ color: #6fcf8e; }}
.capcap .diff {{ color: var(--stale); }}
.missing {{ color: var(--very-stale); font-size: 12px; margin: 6px 0; }}
.age {{ color: inherit; }}
.age.stale {{ color: var(--stale); }}
.age.very-stale {{ color: var(--very-stale); }}
</style>
</head>
<body>
<header>
<h1>openrecet ↔ retail comparisons</h1>
<div class="regen">regen run: {regen_iso} &middot; manual refresh (Ctrl-R) to pull updated images &middot; <span class="age" data-mtime="{regen_mtime}">just now</span></div>
<div class="hint">Each comparison shows [ openrecet | retail ]. <b>Click</b> to reveal the amplified pixel-diff (black = bit-identical). <b>Right-click → Copy Image</b> copies the full [left | right | diff] montage.</div>
</header>

<nav class="toc">
Tests: {toc}
</nav>

{cards}

<script>
(function () {{
    function relAge(now, then) {{
        const d = (now - then) / 1000;
        if (d < 60)    return Math.round(d) + "s ago";
        if (d < 3600)  return Math.round(d / 60) + "m ago";
        if (d < 86400) return (d / 3600).toFixed(1) + "h ago";
        return (d / 86400).toFixed(1) + "d ago";
    }}
    const now = Date.now();
    for (const el of document.querySelectorAll(".age[data-mtime]")) {{
        const m = parseInt(el.getAttribute("data-mtime"), 10) * 1000;
        if (!m) continue;
        el.textContent = relAge(now, m);
        const d = (now - m) / 1000;
        if (d > 24 * 3600) el.classList.add("very-stale");
        else if (d > 3600) el.classList.add("stale");
    }}
    // Click a comparison to toggle the diff reveal.
    for (const w of document.querySelectorAll(".atlas-wrap")) {{
        w.addEventListener("click", () => w.classList.toggle("open"));
    }}
}})();
</script>
</body>
</html>
"""


def _cap_block(cap: dict, mt: int) -> str:
    if cap.get("rel") is None:
        miss = html.escape(cap.get("missing", "?"))
        return (f'<div class="cap"><div class="missing">'
                f'{html.escape(cap["label"])}: no capture on {miss} side</div></div>')
    rel = html.escape(cap["rel"])
    label = html.escape(cap["label"])
    differ = cap["differ_px"]
    if differ == 0:
        stat = '<span class="ok">bit-identical</span>'
    else:
        stat = (f'<span class="diff">{differ} px differ</span> · '
                f'mean|abs|/ch {cap["meanabs"]}')
    return (
        f'<div class="cap">'
        f'<div class="atlas-wrap" '
        f'style="--r0:{cap["row0_pct"]}%;--tot:{cap["total_pct"]}%">'
        f'<img src="{rel}?v={mt}" alt="{label} atlas" loading="lazy">'
        f'</div>'
        f'<div class="capcap">{label} · {stat} · click to reveal diff · '
        f'right-click → Copy Image for 3-up</div>'
        f'</div>'
    )


def render_html(items: list[dict], out_path: Path) -> None:
    """Render the interactive gallery from collect_artifacts() items."""
    now = dt.datetime.now(dt.timezone.utc)
    regen_iso   = now.strftime("%Y-%m-%dT%H:%M:%SZ")
    regen_mtime = int(now.timestamp())

    toc = " ".join(
        f'<a href="#{html.escape(it["name"])}">{html.escape(it["name"])}</a>'
        for it in items
    )

    cards: list[str] = []
    for it in items:
        name = html.escape(it["name"])
        desc = html.escape(it["description"]) if it["description"] else ""
        desc_block = f'<div class="desc">{desc}</div>' if desc else ""

        if not it.get("captures"):
            meta_line = ""
            body = ('<div class="missing">no --target both run yet for this '
                    'scenario. Run: <code>tools/regen-comparisons.py</code> or '
                    f'<code>tools/scenario-test.py {name} --target both</code>.</div>')
        else:
            mt = it["captured_mtime"]
            run_rel = html.escape(it.get("run_dir_rel") or "")
            mode = "segtrace · cap-index" if it.get("is_segtrace") else "absolute frame"
            meta_line = (
                f'<div class="meta">captured {html.escape(it["captured_at"])} &middot; '
                f'<span class="age" data-mtime="{mt}">just now</span> &middot; '
                f'left=openrecet, right=retail &middot; {mode} &middot; '
                f'src={run_rel}</div>'
            )
            body = "\n".join(_cap_block(c, mt) for c in it["captures"])

        cards.append(
            f'<section class="scenario" id="{name}">'
            f'<h2><a href="#{name}">{name}</a></h2>'
            f'{meta_line}{desc_block}{body}</section>'
        )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(_HTML_TEMPLATE.format(
        regen_iso=regen_iso, regen_mtime=regen_mtime,
        toc=toc, cards="\n".join(cards),
    ))
