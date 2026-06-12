#!/usr/bin/env python3
"""Trace Studio v3 — view bake: a port+retail cache pair → PNG panels + diff + manifest.

The P3 viewer's data layer. Given a port and a retail cache entry (each a
container + its `v3ref_*.raw` bit-exact frames + `v3meta.json` identity) and their
identity JOIN (orv3_sync), bake the three panels the viewer scrubs — port | retail
| diff — plus a `manifest.json` describing the identity-keyed timeline.

Why refs, not live replay, for the base panels: the proxy's `v3ref_*.raw` ARE the
bit-exact backbuffer readbacks the replayer reproduces 0-byte (proven at capture,
48/48), so converting them is lossless AND instant — no Windows/Frida/d3d. Replay
is reserved for the on-demand zoom / draw-isolation / semantic layer (P3d), where
re-rendering at a new zoom or with a draw isolated is the whole point.

The timeline is keyed by IDENTITY OFFSET (anchor-relative frame index), the v3
universal clock (E3): every distinct offset across both sides is a column. A column
with both sides gets a diff; a column with only one side is an HONEST, named gap —
never a silent mispair (the v2 sync bug class). Same offset == same logical moment
no matter how far a load stretched the absolute present-count.

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/orv3_view.py \
      <port-entry-dir> <retail-entry-dir> [--out DIR] [--amp 6]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))  # repo tools/ (pixel_diff)
import orv3        # noqa: E402
import orv3_sync   # noqa: E402
import v3cache     # noqa: E402


# ── raw frame I/O ──
def read_raw_rgb(path: Path) -> np.ndarray:
    """Read a v3ref_*.raw (8-byte [w,h] u32 header + tightly-packed BGRA rows) into
    an (H, W, 3) uint8 RGB array (the replayer/proxy write BGRA; PIL wants RGB)."""
    b = path.read_bytes()
    w, h = int.from_bytes(b[0:4], "little"), int.from_bytes(b[4:8], "little")
    bgra = np.frombuffer(b, dtype=np.uint8, count=w * h * 4, offset=8).reshape(h, w, 4)
    return bgra[:, :, [2, 1, 0]].copy()   # BGRA -> RGB


def save_png(rgb: np.ndarray, path: Path) -> None:
    from PIL import Image
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgb).save(path)


# ── the identity-keyed timeline (pure; unit-tested) ──
def merge_offsets(port_offsets: set[int], retail_offsets: set[int]) -> list[dict]:
    """Merge both sides' identity offsets into the viewer timeline, ordered by the
    offset (the universal clock). Each column says which sides are present and, when
    exactly one is, names the HONEST gap. Pure — no filesystem, no images."""
    rows = []
    for off in sorted(port_offsets | retail_offsets):
        hp, hr = off in port_offsets, off in retail_offsets
        rows.append({
            "offset": off, "has_port": hp, "has_retail": hr,
            "gap": None if (hp and hr) else ("retail" if hp else "port"),
        })
    return rows


def _side_index(entry: Path):
    """offset -> orv3.Frame for a cache entry, keyed by identity offset (meta +
    container). The Frame carries index/present/draws/calls/res for the state panel."""
    meta = v3cache.load_meta(entry)
    c = orv3.Container.load(entry / "v3cap.bin")
    return meta, {meta.offset_of(f.index): f for f in c.frames}


def build_view(port_entry: Path, retail_entry: Path, out_dir: Path,
               *, amp: float = 6.0, quiet: bool = False) -> dict:
    """Bake the port|retail|diff PNGs + manifest.json for a cached entry pair into
    `out_dir`. Returns the manifest dict. Pairs are an identity JOIN (orv3_sync);
    the diff metric is the project's `gt8` (pixels with any channel |Δ| > 8) so a
    v3 verdict is directly comparable to a v2 one."""
    from pixel_diff import amplified_diff

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    pmeta, pidx = _side_index(port_entry)
    rmeta, ridx = _side_index(retail_entry)
    join = orv3_sync.sync_entries(port_entry, retail_entry, quiet=True)

    rows = merge_offsets(set(pidx), set(ridx))
    frames, worst = [], {"offset": None, "gt8": -1}
    dims = None
    n_diff = 0
    for row in rows:
        off = row["offset"]
        entry = {"offset": off, "port": None, "retail": None, "diff": None,
                 "gt8": None, "meanabs": None, "maxd": None, "gap": row["gap"],
                 "port_present": None, "retail_present": None,
                 "port_draws": None, "retail_draws": None,
                 "port_calls": None, "retail_calls": None,
                 "port_res": None, "retail_res": None}
        prgb = rrgb = None
        if row["has_port"]:
            pf = pidx[off]
            prgb = read_raw_rgb(port_entry / f"v3ref_{pf.index:03d}.raw")
            rel = f"port/o{off:04d}.png"
            save_png(prgb, out_dir / rel)
            entry["port"], entry["port_present"] = rel, pf.present
            entry["port_draws"], entry["port_calls"] = pf.n_draws, pf.n_calls
            entry["port_res"] = len(pf.res_referenced)
        if row["has_retail"]:
            rf = ridx[off]
            rrgb = read_raw_rgb(retail_entry / f"v3ref_{rf.index:03d}.raw")
            rel = f"retail/o{off:04d}.png"
            save_png(rrgb, out_dir / rel)
            entry["retail"], entry["retail_present"] = rel, rf.present
            entry["retail_draws"], entry["retail_calls"] = rf.n_draws, rf.n_calls
            entry["retail_res"] = len(rf.res_referenced)
        if prgb is not None and rrgb is not None and prgb.shape == rrgb.shape:
            # retail = ground truth (A/left); port = B — matches pixel_diff/v2.
            d, _differ, meanabs = amplified_diff(rrgb, prgb, amp)
            gt8 = int((np.abs(rrgb.astype(int) - prgb.astype(int)).max(axis=2) > 8).sum())
            maxd = int(np.abs(rrgb.astype(int) - prgb.astype(int)).max())
            rel = f"diff/o{off:04d}.png"
            save_png(d, out_dir / rel)
            entry.update(diff=rel, gt8=gt8, meanabs=round(meanabs, 4), maxd=maxd)
            n_diff += 1
            if gt8 > worst["gt8"]:
                worst = {"offset": off, "gt8": gt8}
            dims = dims or [int(prgb.shape[1]), int(prgb.shape[0])]
        frames.append(entry)

    manifest = {
        "scenario": pmeta.scenario,
        "anchor": pmeta.anchor,
        "anchor_occ": pmeta.anchor_occ,
        "verdict": join["verdict"],
        "load_stretch": join["load_stretch"],
        "dims": dims,
        "amp": amp,
        "offset0": rows[0]["offset"] if rows else None,
        "count": len(rows),
        "n_diff": n_diff,
        "n_gaps": sum(1 for r in rows if r["gap"]),
        "worst": worst if worst["offset"] is not None else None,
        "port_entry": str(port_entry),
        "retail_entry": str(retail_entry),
        "frames": frames,
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=1))
    if not quiet:
        print(f"baked {len(frames)} columns ({n_diff} diffs, {manifest['n_gaps']} gaps) "
              f"-> {out_dir}")
        print(f"  verdict: {manifest['verdict']}   dims: {dims}   "
              f"load-stretch: {manifest['load_stretch']:+d}")
        if manifest["worst"]:
            print(f"  worst:   offset {worst['offset']}  gt8={worst['gt8']}px")
    return manifest


def main() -> int:
    ap = argparse.ArgumentParser(description="v3 view bake (port+retail cache pair -> PNG panels + manifest)")
    ap.add_argument("port_entry", type=Path, help="port cache entry dir (v3cap.bin + v3ref_*.raw + v3meta.json)")
    ap.add_argument("retail_entry", type=Path, help="retail cache entry dir")
    ap.add_argument("--out", type=Path, default=None,
                    help="output view dir (default: <port_entry>/view)")
    ap.add_argument("--amp", type=float, default=6.0, help="diff amplification (default 6)")
    args = ap.parse_args()
    out = args.out or (args.port_entry / "view")
    build_view(args.port_entry, args.retail_entry, out, amp=args.amp)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
