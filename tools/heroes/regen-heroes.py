#!/usr/bin/env python3
"""tools/heroes/regen-heroes.py — regenerate the README hero comparison images.

Each hero is one labelled OpenRecet|Retail montage composed from a
deterministic scenario both-run (tools/scenario-test.py --target both). Frames
are picked by *cap index* (the anchor-relative capture order recorded in
run.json's `captured_frames`), which is stable across load-frame jitter — the
same cap index frames the same beat on both targets, so the heroes regen from
saved inputs without tracking down frame numbers by hand.

Recipe: tools/heroes/heroes.yaml. For each shot:
  1. resolve the both-run dir (newest runs/scenarios/<scenario>-both-* unless
     `run_dir` pins one),
  2. read run.json on each side -> captured_frames[cap] is the frame number
     (or use explicit port_frame / retail_frame),
  3. locate that frame file (png or bmp), optionally zoom-crop both identically,
  4. compose_comparison.py -> docs/img/<name>.png.

  # recompose every hero from the latest existing both-runs (fast, host-free)
  regen-heroes.py

  # one shot, and push the result to the live feed for eyeballing
  regen-heroes.py --shot hero-iv1_1-sigh --push

  # drive scenario-test --target both first, then compose (needs Frida host)
  regen-heroes.py --shot house-comparison --rerun

A shot whose `cap` is null (not yet chosen) or whose both-run dir is missing is
skipped with a warning, so a partial recipe still regenerates the ready shots.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
RECIPE = Path(__file__).resolve().parent / "heroes.yaml"
COMPOSE = REPO / "tools" / "compose_comparison.py"
SCENARIO_TEST = REPO / "tools" / "scenario-test.py"
FEED = Path("/opt/src/llm-feed/feed.py")
OUT_DIR = REPO / "docs" / "img"


def newest_both_run(scenario: str) -> Path | None:
    """Newest runs/scenarios/<scenario>-both-* dir (timestamps sort lexically)."""
    matches = sorted((REPO / "runs" / "scenarios").glob(f"{scenario}-both-*"))
    return matches[-1] if matches else None


def frame_file(frames_dir: Path, frame_no: int) -> Path | None:
    """The frame_NNNNN.{png,bmp} file for a frame number, preferring png."""
    stem = f"frame_{frame_no:05d}"
    for ext in (".png", ".bmp"):
        p = frames_dir / f"{stem}{ext}"
        if p.exists():
            return p
    return None


def side_frame(run_dir: Path, side: str, shot: dict) -> Path | None:
    """Resolve the chosen frame file for one side (openrecet|retail) of a shot."""
    # recipe keys are port_frame / retail_frame; the openrecet side maps to port
    explicit = shot.get("port_frame" if side == "openrecet" else "retail_frame")
    frames_dir = run_dir / side / "frames"
    if explicit is not None:
        return frame_file(frames_dir, int(explicit))
    cap = shot.get("cap")
    if cap is None:
        return None
    run_json = run_dir / side / "run.json"
    if not run_json.exists():
        print(f"  ! {side}: no run.json in {run_dir}", file=sys.stderr)
        return None
    caps = json.loads(run_json.read_text()).get("captured_frames", [])
    if cap >= len(caps):
        print(f"  ! {side}: cap {cap} out of range (have {len(caps)})",
              file=sys.stderr)
        return None
    return frame_file(frames_dir, caps[cap])


def zoom_crop(src: Path, zoom: dict, tmpdir: Path) -> Path:
    """Crop src to zoom box and nearest-neighbour upscale by factor (crisp
    pixel-art zoom). Returns a temp PNG path."""
    x, y, w, h = zoom["x"], zoom["y"], zoom["w"], zoom["h"]
    factor = zoom.get("factor", 1)
    im = Image.open(src).convert("RGB").crop((x, y, x + w, y + h))
    if factor != 1:
        im = im.resize((w * factor, h * factor), Image.NEAREST)
    out = tmpdir / f"zoom_{src.stem}.png"
    im.save(out)
    return out


def compose(a: Path, b: Path, labels: str, out: Path) -> bool:
    cmd = ["python3", str(COMPOSE), "--a", str(a), "--b", str(b),
           "--labels", labels, "--out", str(out)]
    r = subprocess.run(cmd, cwd=REPO)
    return r.returncode == 0


def push_feed(img: Path, title: str, note: str) -> None:
    if not FEED.exists():
        print(f"  ! feed.py not found at {FEED}; skipping push", file=sys.stderr)
        return
    subprocess.run(["python3", str(FEED), "image", str(img),
                    "--title", title, "--note", note], cwd=REPO)


def rerun_scenario(scenario: str, frida_remote: str) -> None:
    print(f"  -> scenario-test {scenario} --target both")
    subprocess.run(["python3", str(SCENARIO_TEST), scenario,
                    "--target", "both", "--frida-remote", frida_remote], cwd=REPO)


def do_shot(shot: dict, args: argparse.Namespace, tmpdir: Path) -> bool:
    name = shot["name"]
    scenario = shot["scenario"]
    print(f"[{name}] scenario={scenario} cap={shot.get('cap')}")

    if args.rerun:
        rerun_scenario(scenario, args.frida_remote)

    run_dir = (REPO / shot["run_dir"]) if shot.get("run_dir") else \
        newest_both_run(scenario)
    if run_dir is None or not run_dir.exists():
        print(f"  ! no both-run dir for {scenario} (skipping)", file=sys.stderr)
        return False

    a = side_frame(run_dir, "openrecet", shot)
    b = side_frame(run_dir, "retail", shot)
    if a is None or b is None:
        print(f"  ! frame unresolved (port={a}, retail={b}) — skipping",
              file=sys.stderr)
        return False

    zoom = shot.get("zoom")
    if zoom:
        a = zoom_crop(a, zoom, tmpdir)
        b = zoom_crop(b, zoom, tmpdir)

    out = OUT_DIR / f"{name}.png"
    labels = shot.get("labels", "OpenRecet,Retail")
    print(f"  port={a.name}  retail={b.name}  -> {out.relative_to(REPO)}")
    if not compose(a, b, labels, out):
        print(f"  ! compose failed for {name}", file=sys.stderr)
        return False

    if args.push:
        push_feed(out, f"hero: {name}", shot.get("note", name))
    return True


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shot", action="append", default=None,
                    help="regen only this shot name (repeatable); default all")
    ap.add_argument("--push", action="store_true",
                    help="push each composed hero to the live feed")
    ap.add_argument("--rerun", action="store_true",
                    help="drive scenario-test --target both before composing "
                         "(needs the Frida retail host)")
    ap.add_argument("--frida-remote", default="cutestation.soy:27042",
                    help="frida-server host:port for --rerun (default %(default)s)")
    args = ap.parse_args(argv)

    recipe = yaml.safe_load(RECIPE.read_text())
    shots = recipe["shots"]
    if args.shot:
        wanted = set(args.shot)
        shots = [s for s in shots if s["name"] in wanted]
        missing = wanted - {s["name"] for s in shots}
        if missing:
            print(f"unknown shot(s): {', '.join(sorted(missing))}", file=sys.stderr)
            return 2

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        tmpdir = Path(td)
        results = [do_shot(s, args, tmpdir) for s in shots]

    ok = sum(results)
    print(f"\nregen-heroes: {ok}/{len(results)} shot(s) composed")
    return 0 if ok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
