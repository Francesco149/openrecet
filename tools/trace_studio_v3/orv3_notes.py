#!/usr/bin/env python3
"""Trace Studio v3 — read the viewer's NOTES (the user's flagged divergences).

The native viewer lets the user drag a crop box on a panel + type a note to flag a
divergence for Claude (the v2 `edits.jsonl` notes loop, now native). The viewer is a
Windows process and CANNOT fopen-write a `\\wsl.localhost` UNC path, so it writes the
notes to a WINDOWS-LOCAL json under %LOCALAPPDATA% (recorded in view.json as
`notes_path`). THIS tool reads that file back on the WSL side so Claude can see the
flags — and can render the flagged frame+region (port|retail|diff, cropped) so Claude
can SEE exactly what was flagged. One notes file per scenario, keyed inside by the
stable identity label (so a note survives re-windowing).

  orv3_notes.py <scenario>                       list the notes (id · label · side · box · text)
  orv3_notes.py <scenario> --render [--id N]      render the flagged frame(s) port|retail|diff,
       [--view VIEW.json] [--pad 24] [--feed]      crop to the box, compose → PNG (+ push to feed)

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/orv3_notes.py guild-ui-flow
  nix develop --command python3 tools/trace_studio_v3/orv3_notes.py guild-ui-flow --render --feed
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))   # repo tools/ (pixel_diff)
import v3cache        # noqa: E402

ROOT       = Path(__file__).resolve().parent.parent.parent
REPLAY_EXE = ROOT / "tools" / "trace_studio_v3" / "replay" / "replay.exe"
WIN_ROOT   = ROOT / "runs" / "studio-v3-windows"
FEED_PY    = Path("/opt/src/llm-feed/feed.py")


def load_notes(scenario: str) -> list[dict]:
    """The user's flagged divergences for `scenario` (or [] if none yet)."""
    p = v3cache.notes_file(scenario)
    if not p.exists():
        return []
    try:
        data = json.loads(p.read_text())
    except (ValueError, OSError):
        return []
    return data if isinstance(data, list) else []


def _box_str(n: dict) -> str:
    b = n.get("box")
    return f"[{b[0]},{b[1]},{b[2]},{b[3]}]" if b else "(whole frame)"


def print_notes(scenario: str, notes: list[dict]) -> None:
    p = v3cache.notes_file(scenario)
    if not notes:
        print(f"no notes for {scenario!r}  (file: {p})")
        print("  → in the viewer: toggle 'note mode' + drag a box on a panel, or 'note frame'.")
        return
    print(f"{len(notes)} note(s) for {scenario!r}  (file: {p}):")
    for n in notes:
        side = n.get("side") or "frame"
        print(f"  #{n.get('id'):<3} {side:<6} {n.get('label','') or '(col '+str(n.get('col'))+')':<22} "
              f"{_box_str(n):<26} {n.get('text','')}")


# ── rendering a flagged frame+region for Claude to SEE ──
def _winpath(p: Path) -> str:
    return subprocess.run(["wslpath", "-w", str(Path(p).resolve())],
                          capture_output=True, text=True, check=True).stdout.strip()


def find_view(scenario: str, explicit: Path | None) -> Path | None:
    """The view.json to resolve a label→frame-index from. Explicit wins; else the most
    recently written one under runs/studio-v3-windows/<scenario>/*/view.json."""
    if explicit:
        return explicit if explicit.exists() else None
    cands = sorted((WIN_ROOT / scenario).glob("*/view.json"),
                   key=lambda p: p.stat().st_mtime, reverse=True)
    return cands[0] if cands else None


def render_frame_rgb(container_win: str, idx: int):
    """Render kept-frame `idx` of a container (Windows path) to RGB via replay.exe.
    replay.exe --upto idx -1 issues ALL draws (a full frame) → a w,h+BGRA raw written
    to a Windows-local scratch (replay.exe can't write UNC either)."""
    from orv3_view import read_raw_rgb
    scratch_wsl = v3cache.localappdata_v3() / "notescratch.raw"
    r = subprocess.run([str(REPLAY_EXE), container_win, "--upto", str(idx), "-1",
                        _winpath(scratch_wsl)],
                       cwd=str(REPLAY_EXE.parent), capture_output=True, text=True)
    if not scratch_wsl.exists():
        raise SystemExit(f"replay.exe produced no raw for idx {idx}:\n{r.stderr}")
    return read_raw_rgb(scratch_wsl)


def render_note(scenario: str, note: dict, view: Path, pad: int, feed: bool) -> Path:
    """Render the note's frame port|retail|diff, crop to its (padded) box, compose into
    one PNG, and return its path (pushing to the llm-feed if --feed)."""
    import numpy as np
    from PIL import Image, ImageDraw
    from pixel_diff import amplified_diff

    m = json.loads(view.read_text())
    label = note.get("label", "")
    col = next((f for f in m["frames"] if f.get("label") == label), None)
    if col is None and note.get("col") is not None and 0 <= note["col"] < len(m["frames"]):
        col = m["frames"][note["col"]]          # fall back to the stored column index
    if col is None:
        raise SystemExit(f"note #{note.get('id')}: label {label!r} not in {view} "
                         f"(re-window to a range that includes it)")
    pidx, ridx = col.get("port_idx"), col.get("retail_idx")
    parts, labels = [], []
    prgb = render_frame_rgb(m["port_container"], pidx) if pidx is not None else None
    rrgb = render_frame_rgb(m["retail_container"], ridx) if ridx is not None else None
    drgb = None
    if prgb is not None and rrgb is not None and prgb.shape == rrgb.shape:
        drgb, _differ, _meanabs = amplified_diff(rrgb, prgb, 6.0)   # retail=A, port=B (v2 law)

    H, W = (prgb if prgb is not None else rrgb).shape[:2]
    box = note.get("box")
    if box:
        x0 = max(0, int(box[0]) - pad); y0 = max(0, int(box[1]) - pad)
        x1 = min(W, int(box[2]) + pad); y1 = min(H, int(box[3]) + pad)
    else:
        x0, y0, x1, y1 = 0, 0, W, H

    def crop(rgb, name):
        if rgb is None:
            return
        c = rgb[y0:y1, x0:x1].copy()
        if box:                                  # outline the exact flagged region in the crop
            im = Image.fromarray(c); d = ImageDraw.Draw(im)
            d.rectangle([int(box[0]) - x0, int(box[1]) - y0, int(box[2]) - x0, int(box[3]) - y0],
                        outline=(80, 230, 80), width=2)
            c = np.asarray(im)
        parts.append(c); labels.append(name)

    crop(prgb, "port"); crop(rrgb, "retail"); crop(drgb, "diff")
    if not parts:
        raise SystemExit(f"note #{note.get('id')}: neither side present (gap column)")

    ch = max(p.shape[0] for p in parts); gap = 8
    cw = sum(p.shape[1] for p in parts) + gap * (len(parts) - 1)
    canvas = np.zeros((ch + 18, cw, 3), np.uint8)
    x = 0
    for p, name in zip(parts, labels):
        canvas[18:18 + p.shape[0], x:x + p.shape[1]] = p
        x += p.shape[1] + gap
    out = Image.fromarray(canvas)
    ImageDraw.Draw(out).text((2, 4), f"#{note.get('id')} {label} [{note.get('side') or 'frame'}]  "
                             f"{' | '.join(labels)}   {note.get('text','')}", fill=(220, 220, 120))
    out_path = Path("/tmp") / f"orv3_note_{scenario}_{note.get('id')}.png"
    out.save(out_path)
    print(f"  rendered note #{note.get('id')} ({label}) → {out_path}")
    if feed:
        subprocess.run([sys.executable, str(FEED_PY), "image", str(out_path),
                        "--title", f"note #{note.get('id')} · {scenario} · {label}",
                        "--note", note.get("text", "")], check=False)
    return out_path


def main() -> int:
    ap = argparse.ArgumentParser(description="read (and render) the viewer's flagged notes")
    ap.add_argument("scenario", help="scenario name (the notes file is keyed by it)")
    ap.add_argument("--render", action="store_true",
                    help="render the flagged frame(s) port|retail|diff cropped to the box")
    ap.add_argument("--id", type=int, default=None, help="render only this note id (with --render)")
    ap.add_argument("--view", type=Path, default=None,
                    help="view.json to resolve label→frame-index (default: newest for the scenario)")
    ap.add_argument("--pad", type=int, default=24, help="px of context around the box crop (default 24)")
    ap.add_argument("--feed", action="store_true", help="push each rendered crop to the llm-feed")
    args = ap.parse_args()

    notes = load_notes(args.scenario)
    print_notes(args.scenario, notes)
    if not args.render:
        return 0
    if not notes:
        return 0
    if not REPLAY_EXE.exists():
        raise SystemExit(f"replay.exe not built: {REPLAY_EXE}")
    view = find_view(args.scenario, args.view)
    if view is None:
        raise SystemExit(f"no view.json for {args.scenario!r} under {WIN_ROOT}/{args.scenario}/ "
                         f"— run orv3_window.py {args.scenario} --window ... --view first")
    print(f"\nrendering crops (view: {view}):")
    todo = [n for n in notes if args.id is None or n.get("id") == args.id]
    if not todo:
        raise SystemExit(f"no note with id {args.id}")
    for n in todo:
        render_note(args.scenario, n, view, args.pad, args.feed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
