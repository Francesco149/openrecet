#!/usr/bin/env python3
"""tools/heroes/regen-heroes.py — regenerate the README hero SCREENSHOTS.

ONE OpenRecet screenshot per hero -> docs/img/<name>.png.  NO side-by-side: the
port is 1:1 on what we showcase, so a single faithful OpenRecet frame is the
point.  Two frame SOURCES, chosen per shot in heroes.yaml:

  source: scenario   (default; FAITHFUL — the real exe framebuffer)
      Drive  tools/scenario-test.py <scenario> --target openrecet  (PORT only, no
      Frida) -> real 1024x768 framebuffer frames in runs/scenarios/<s>-openrecet-*/
      frames/.  Frame picked by `cap` (anchor-relative index into run.json's
      captured_frames — stable across load-frame jitter) or an explicit `frame`.
      The scenario must carry discrete {capture} ops (NOT a v3-only {caprange}).
      Use whenever the pixels must be faithful: god-rays, lighting, the HUD, and
      overlay/transparency effects (e.g. the iv1_2 magic-circle) all render here.

  source: v3shot     (fast; reuses the Trace Studio v3 cache)
      Render via  tools/trace_studio_v3/orv3_shot.py <scenario>:<side> --frame N
      — replays the captured d3d stream from runs/studio-v3-cache/.  No re-drive
      when a cache exists; --rerun rebuilds the PORT cache (no Frida) via
      port_capture.py over the scenario's {caprange} (sliced by `window`).
      CAVEAT: the v3 proxy does NOT capture SetRenderTarget / CopyRects yet, so
      RT-based effects (pause/menu captured-screen backdrops, radial-blur
      transitions, post-fx) render EMPTY/black here — don't pick an RT-effect
      frame; use source:scenario for those.  Fine for ordinary scenes (verified
      on the haggle UI: the shop, characters, gauge and dialogue all render).

Recipe: tools/heroes/heroes.yaml.  Optional identical `zoom` crop per shot
(nearest-neighbour upscale = crisp pixel-art zoom).

Usage (host tools need the nix prefix):
  # full rebuild — (re)capture every shot's source, then write all heroes:
  nix develop --command python3 tools/heroes/regen-heroes.py --rerun
  # fast recompose from the existing runs/caches (no capture):
  nix develop --command python3 tools/heroes/regen-heroes.py
  # one shot, pushed to the live feed to eyeball:
  nix develop --command python3 tools/heroes/regen-heroes.py --shot hero-haggle --push
  # add a shot: append it to heroes.yaml (pick a source above) and re-run.

What's persisted so the heroes regen from a clean checkout:
  * the scenarios — tests/scenarios/<scenario>/ (committed: scenario.yaml +
    trace.jsonl), incl. their {capture}/{caprange} ops and phase/RNG pins,
  * this recipe (tools/heroes/heroes.yaml: source + scenario + cap/frame),
  * the deterministic cap index / frame number per shot.
The runs/ + runs/studio-v3-cache/ artefacts are an ephemeral, gitignored
convenience; --rerun recreates them from the committed scenario.
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
SCENARIO_TEST = REPO / "tools" / "scenario-test.py"
ORV3_SHOT = REPO / "tools" / "trace_studio_v3" / "orv3_shot.py"
PORT_CAPTURE = REPO / "tools" / "trace_studio_v3" / "port_capture.py"
FEED = Path("/opt/src/llm-feed/feed.py")
OUT_DIR = REPO / "docs" / "img"
V3_CACHE = REPO / "runs" / "studio-v3-cache"


def run(cmd: list, **kw) -> subprocess.CompletedProcess:
    """Run a host tool under this interpreter (the nix devshell python3)."""
    print(f"    $ {Path(cmd[0]).name} {' '.join(str(c) for c in cmd[1:])}")
    return subprocess.run([sys.executable, *map(str, cmd)], cwd=REPO, **kw)


# ---- source: scenario (real exe framebuffer via scenario-test) --------------

def newest_openrecet_run(scenario: str) -> Path | None:
    """Newest runs/scenarios/<scenario>-openrecet-* dir (timestamps sort lexically)."""
    m = sorted((REPO / "runs" / "scenarios").glob(f"{scenario}-openrecet-*"))
    return m[-1] if m else None


def frame_file(frames_dir: Path, frame_no: int) -> Path | None:
    """The frame_NNNNN.{png,bmp} for a frame number, preferring png."""
    for ext in (".png", ".bmp"):
        p = frames_dir / f"frame_{frame_no:05d}{ext}"
        if p.exists():
            return p
    return None


def resolve_scenario_frame(shot: dict, do_rerun: bool) -> Image.Image | None:
    scen = shot["scenario"]
    if do_rerun:
        # --target openrecet drives the PORT only (no Frida).  Its exit code can be
        # non-zero on a golden-frame MISMATCH (we don't bless heroes), so we ignore
        # it and just resolve the freshly-captured frames.
        run([SCENARIO_TEST, scen, "--target", "openrecet"])
    run_dir = newest_openrecet_run(scen)
    if run_dir is None:
        print(f"  ! no openrecet run for {scen!r} — re-run with --rerun", file=sys.stderr)
        return None
    frames = run_dir / "frames"
    explicit = shot.get("frame")
    if explicit is not None:
        fp = frame_file(frames, int(explicit))
    else:
        cap = shot.get("cap")
        if cap is None:
            print("  ! scenario shot needs `cap` or `frame`", file=sys.stderr)
            return None
        caps = json.loads((run_dir / "run.json").read_text()).get("captured_frames", [])
        if cap >= len(caps):
            print(f"  ! cap {cap} out of range (have {len(caps)} captures) — widen the "
                  f"scenario's {{capture}} ops or pick a lower cap", file=sys.stderr)
            return None
        fp = frame_file(frames, caps[cap])
    if fp is None:
        print("  ! resolved frame file is missing on disk", file=sys.stderr)
        return None
    print(f"    frame {fp.name}  ({run_dir.name})")
    return Image.open(fp).convert("RGB")


# ---- source: v3shot (Trace Studio v3 cache replay via orv3_shot) ------------

def v3_cache_exists(scenario: str, side: str) -> bool:
    return any(V3_CACHE.glob(f"{scenario}-*/{side}/v3cap.bin"))


def resolve_v3shot_frame(shot: dict, do_rerun: bool, tmpdir: Path) -> Image.Image | None:
    scen = shot["scenario"]
    side = shot.get("side", "port")
    frame = shot.get("frame")
    if frame is None:
        print("  ! v3shot shot needs `frame` (a kept-frame index)", file=sys.stderr)
        return None
    if do_rerun or not v3_cache_exists(scen, side):
        if side != "port":
            print(f"  ! v3shot --rerun rebuilds only the PORT cache; the {side!r} cache "
                  f"must already exist (drive it via orv3_window)", file=sys.stderr)
        else:
            # port_capture.py drives the PORT (no Frida) through the scenario's
            # {caprange}, sliced to `window` (OFFSET:COUNT), into the v3 cache.
            cmd = [PORT_CAPTURE, scen, "--no-verify"]
            if shot.get("window"):
                cmd += ["--window", shot["window"]]
            if shot.get("anchor"):
                cmd += ["--anchor", shot["anchor"]]
            run(cmd)
    out = tmpdir / f"v3_{shot['name']}.png"
    run([ORV3_SHOT, f"{scen}:{side}", "--frame", frame, "--out", out])
    if not out.exists():
        print(f"  ! orv3_shot produced no PNG for {scen}:{side} frame {frame} — "
              f"is the cache present? (--rerun to rebuild)", file=sys.stderr)
        return None
    print(f"    {scen}:{side} kept-frame {frame}")
    return Image.open(out).convert("RGB")


# ---- compose (zoom-crop + write + feed push) --------------------------------

def zoom_crop(im: Image.Image, zoom: dict) -> Image.Image:
    """Crop to the zoom box and nearest-neighbour upscale by `factor` (crisp
    pixel-art zoom)."""
    x, y, w, h = zoom["x"], zoom["y"], zoom["w"], zoom["h"]
    factor = zoom.get("factor", 1)
    im = im.crop((x, y, x + w, y + h))
    if factor != 1:
        im = im.resize((w * factor, h * factor), Image.NEAREST)
    return im


def push_feed(img: Path, title: str, note: str) -> None:
    if not FEED.exists():
        print(f"  ! feed.py not found at {FEED}; skipping push", file=sys.stderr)
        return
    run([FEED, "image", str(img), "--title", title, "--note", note])


def do_shot(shot: dict, args: argparse.Namespace, tmpdir: Path) -> bool:
    name = shot["name"]
    source = shot.get("source", "scenario")
    print(f"[{name}] source={source} scenario={shot.get('scenario')}")

    if source == "scenario":
        im = resolve_scenario_frame(shot, args.rerun)
    elif source == "v3shot":
        im = resolve_v3shot_frame(shot, args.rerun, tmpdir)
    else:
        print(f"  ! unknown source {source!r} (want scenario|v3shot)", file=sys.stderr)
        return False
    if im is None:
        return False

    if shot.get("zoom"):
        im = zoom_crop(im, shot["zoom"])

    out = OUT_DIR / f"{name}.png"
    im.save(out)
    print(f"    -> {out.relative_to(REPO)}  ({im.size[0]}x{im.size[1]})")

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
                    help="(re)capture each shot's source before composing "
                         "(scenario: drive the port; v3shot: rebuild the port cache) "
                         "— no Frida needed")
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
    print(f"\nregen-heroes: {ok}/{len(results)} shot(s) written")
    return 0 if ok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
