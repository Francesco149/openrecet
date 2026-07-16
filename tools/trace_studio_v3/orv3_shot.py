#!/usr/bin/env python3
"""Trace Studio v3 — headless frame/draw PROBE.

"What does this side actually render at frame N?" without opening the native
viewer or flagging a note. Wraps replay.exe to render any kept frame (or a
draw-PREFIX of it) of a captured container to a PNG you can Read — plus a frame
SWEEP (a labelled strip across many frames, e.g. to watch a transition build up)
and a DRAW-ISOLATION strip (clear → +draw0 → +draw1 … → full, to see what each
draw paints). This is the default way to answer a retail-rendering question
YOURSELF (see CLAUDE.md: probe with the tools, don't ask).

Caveat — render targets: v3 captures/replays SetRenderTarget and CopyRects, but a
target may have been populated in an EARLIER frame. The native viewer uses history
replay for RT containers; this helper's isolated frame/draw-prefix path may show such
samples empty. Use the full history-enabled viewer plus `orv3_rt.py` for captured-
screen backdrops, radial-blur transitions, and other cross-frame RT effects.

Usage:
  orv3_shot.py <container.bin | scenario:side> --frame N [--upto K] [--out P] [--feed]
  orv3_shot.py <container.bin | scenario:side> --sweep N0,N1,N2,... [--upto K] [--feed]
  orv3_shot.py <container.bin | scenario:side> --frame N --draws [--feed]   # per-draw build-up

`scenario:side` resolves the newest runs/studio-v3-cache/<scenario>-*/<side>/v3cap.bin.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import orv3                                    # noqa: E402
import v3cache                                 # noqa: E402
from orv3_view import read_raw_rgb, _winpath   # noqa: E402

ROOT = HERE.parent.parent
REPLAY_EXE = HERE / "replay" / "replay.exe"
FEED_PY = Path("/opt/src/llm-feed/feed.py")
CACHE = ROOT / "runs" / "studio-v3-cache"


def resolve_container(spec: str) -> Path:
    """A direct path, or `scenario:side` → newest cached container for that side."""
    if ":" in spec and not Path(spec).exists():
        scenario, side = spec.rsplit(":", 1)
        cands = sorted(CACHE.glob(f"{scenario}-*/{side}/v3cap.bin"),
                       key=lambda p: p.stat().st_mtime, reverse=True)
        if not cands:
            raise SystemExit(f"no cached container for {scenario!r} side {side!r} under {CACHE}")
        return cands[0]
    p = Path(spec)
    if not p.exists():
        raise SystemExit(f"container not found: {p}")
    return p


def render(container: Path, idx: int, upto: int = -1):
    """Render kept frame `idx` (issuing its first `upto` draws; -1 = all) → RGB array."""
    scratch = v3cache.localappdata_v3() / "shotscratch.raw"
    if scratch.exists():
        scratch.unlink()
    r = subprocess.run([str(REPLAY_EXE), _winpath(container), "--upto", str(idx), str(upto),
                        _winpath(scratch)],
                       cwd=str(REPLAY_EXE.parent), capture_output=True, text=True)
    if not scratch.exists():
        raise SystemExit(f"replay.exe produced no raw (idx={idx} upto={upto}):\n"
                         f"{r.stdout}\n{r.stderr}")
    return read_raw_rgb(scratch)


def save_png(rgb, path: Path):
    from PIL import Image
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgb).save(path)


def strip(shots, out: Path, scale: float = 0.42):
    """Compose (label, rgb) panels into one labelled horizontal strip PNG."""
    from PIL import Image, ImageDraw
    H, W = shots[0][1].shape[:2]
    tw, th = int(W * scale), int(H * scale)
    im = Image.new("RGB", (tw * len(shots), th + 18), (0, 0, 0))
    d = ImageDraw.Draw(im)
    for i, (name, rgb) in enumerate(shots):
        im.paste(Image.fromarray(rgb).resize((tw, th)), (i * tw, 18))
        d.text((i * tw + 4, 4), name, fill=(255, 255, 0))
    out.parent.mkdir(parents=True, exist_ok=True)
    im.save(out)


def push_feed(path: Path, title: str, note: str = ""):
    subprocess.run([sys.executable, str(FEED_PY), "image", str(path),
                    "--title", title, "--note", note], check=False)


def main() -> int:
    ap = argparse.ArgumentParser(description="render a v3 container frame/draw to PNG (headless probe)")
    ap.add_argument("container", help="container.bin path OR scenario:side (e.g. house-pause:retail)")
    ap.add_argument("--frame", type=int, help="kept-frame index to render")
    ap.add_argument("--sweep", help="comma list of frame indices → one labelled strip")
    ap.add_argument("--upto", type=int, default=-1, help="render only the first K draws (-1 = all)")
    ap.add_argument("--draws", action="store_true",
                    help="with --frame: draw-isolation strip (clear, +draw0, +draw1, …, full)")
    ap.add_argument("--out", type=Path, help="output PNG (default runs/_shot/<auto>.png)")
    ap.add_argument("--feed", action="store_true", help="push the result to the llm-feed")
    args = ap.parse_args()

    cont = resolve_container(args.container)
    outdir = ROOT / "runs" / "_shot"
    tag = f"{cont.parent.parent.name}_{cont.parent.name}"

    if args.sweep:
        idxs = [int(x) for x in args.sweep.split(",") if x.strip() != ""]
        shots = [(f"f{n}", render(cont, n, args.upto)) for n in idxs]
        out = args.out or outdir / f"{tag}_sweep.png"
        strip(shots, out)
        print(f"sweep {idxs} -> {out}")
        for n, (_, rgb) in zip(idxs, shots):
            print(f"  f{n}: mean={rgb.mean():.1f}")
        if args.feed:
            push_feed(out, f"{tag} sweep {idxs}", f"frames {idxs}, upto={args.upto}")
        return 0

    if args.frame is None:
        raise SystemExit("need --frame N or --sweep N0,N1,...")

    if args.draws:
        from orv3_draws import enumerate_draws
        ndraws = len(enumerate_draws(orv3.Container.load(cont), args.frame))
        cuts = [0] + list(range(1, ndraws + 1))
        shots = [("clear" if k == 0 else f"+d{k-1}", render(cont, args.frame, k)) for k in cuts]
        out = args.out or outdir / f"{tag}_f{args.frame}_draws.png"
        strip(shots, out)
        print(f"frame {args.frame}: {ndraws} draws -> {out}")
        for (name, rgb) in shots:
            print(f"  {name}: mean={rgb.mean():.1f}")
        if args.feed:
            push_feed(out, f"{tag} f{args.frame} draw build-up", f"{ndraws} draws")
        return 0

    rgb = render(cont, args.frame, args.upto)
    out = args.out or outdir / f"{tag}_f{args.frame}.png"
    save_png(rgb, out)
    print(f"frame {args.frame} (upto={args.upto}) -> {out}  shape={rgb.shape} mean={rgb.mean():.1f}")
    if args.feed:
        push_feed(out, f"{tag} frame {args.frame}", f"upto={args.upto}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
