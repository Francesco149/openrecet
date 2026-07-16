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
import orv3_sync   # noqa: E402
import v3cache     # noqa: E402  (owns LoadedSide/as_side — the parse-once handoff)


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
def merge_anchor_seq(pmeta, rmeta) -> dict[tuple[str, int], int]:
    """(name, occ) → ordering position, merged across both sides' stored anchor
    streams (same logical sequence — frame numbers differ, order doesn't; anything
    one side lacks, e.g. a run that ended early, appends in the other's order)."""
    seq: dict[tuple[str, int], int] = {}
    for meta in (pmeta, rmeta):
        for a in sorted(meta.anchors or [], key=lambda a: (a["frame"], a["name"])):
            seq.setdefault((a["name"], a["occ"]), len(seq))
    return seq


def merge_keys(port_keys: set[tuple], retail_keys: set[tuple],
               seq: dict[tuple[str, int], int]) -> list[dict]:
    """Merge both sides' per-frame identity keys (anchor, occ, delta) into the
    viewer timeline, ordered chronologically by (anchor firing position, delta).
    Each column says which sides are present and, when exactly one is, names the
    HONEST gap. Pure — no filesystem, no images."""
    def order(k):
        return (seq.get((k[0], k[1]), len(seq)), k[2])
    rows = []
    for key in sorted(port_keys | retail_keys, key=order):
        hp, hr = key in port_keys, key in retail_keys
        rows.append({
            "key": key, "offset": key[2],
            "label": f"{key[0]}#{key[1]}+{key[2]}",
            "has_port": hp, "has_retail": hr,
            "gap": None if (hp and hr) else ("retail" if hp else "port"),
        })
    return rows


def _side_index(side):
    """(meta, {key: orv3.Frame}, [w,h], Container) for a cache entry, keyed by the
    per-frame identity key (meta v2: most-recent anchor; legacy: offset arithmetic).
    The Frame carries index/present/draws/calls/res for the state panel; dims come
    from DEV_PARAMS; the Container is kept for the per-draw/material semantic diff
    (orv3_draws). `side` is a parse-once v3cache.LoadedSide OR an entry Path/str."""
    s = v3cache.as_side(side)
    return s.meta, s.index, s.dims, s.cont


def build_view(port_entry: Path, retail_entry: Path, out_dir: Path,
               *, amp: float = 6.0, quiet: bool = False) -> dict:
    """Bake the port|retail|diff PNGs + manifest.json for a cached entry pair into
    `out_dir`. Returns the manifest dict. Pairs are an identity JOIN (orv3_sync);
    the diff metric is the project's `gt8` (pixels with any channel |Δ| > 8) so a
    v3 verdict is directly comparable to a v2 one."""
    from pixel_diff import amplified_diff

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    pside, rside = v3cache.as_side(port_entry), v3cache.as_side(retail_entry)
    pmeta, pidx, _, _ = _side_index(pside)
    rmeta, ridx, _, _ = _side_index(rside)
    join = orv3_sync.sync_entries(pside, rside, quiet=True)   # parse-once: reuse loaded sides

    rows = merge_keys(set(pidx), set(ridx), merge_anchor_seq(pmeta, rmeta))
    frames, worst = [], {"offset": None, "label": None, "gt8": -1}
    dims = None
    n_diff = 0
    for col, row in enumerate(rows):
        key = row["key"]
        entry = {"offset": row["offset"], "label": row["label"],
                 "port": None, "retail": None, "diff": None,
                 "gt8": None, "meanabs": None, "maxd": None, "gap": row["gap"],
                 "port_present": None, "retail_present": None,
                 "port_draws": None, "retail_draws": None,
                 "port_calls": None, "retail_calls": None,
                 "port_res": None, "retail_res": None}
        prgb = rrgb = None
        if row["has_port"]:
            pf = pidx[key]
            prgb = read_raw_rgb(pside.entry / f"v3ref_{pf.index:03d}.raw")
            rel = f"port/c{col:05d}.png"
            save_png(prgb, out_dir / rel)
            entry["port"], entry["port_present"] = rel, pf.present
            entry["port_draws"], entry["port_calls"] = pf.n_draws, pf.n_calls
            entry["port_res"] = len(pf.res_referenced)
        if row["has_retail"]:
            rf = ridx[key]
            rrgb = read_raw_rgb(rside.entry / f"v3ref_{rf.index:03d}.raw")
            rel = f"retail/c{col:05d}.png"
            save_png(rrgb, out_dir / rel)
            entry["retail"], entry["retail_present"] = rel, rf.present
            entry["retail_draws"], entry["retail_calls"] = rf.n_draws, rf.n_calls
            entry["retail_res"] = len(rf.res_referenced)
        if prgb is not None and rrgb is not None and prgb.shape == rrgb.shape:
            # retail = ground truth (A/left); port = B — matches pixel_diff/v2.
            d, _differ, meanabs = amplified_diff(rrgb, prgb, amp)
            gt8 = int((np.abs(rrgb.astype(int) - prgb.astype(int)).max(axis=2) > 8).sum())
            maxd = int(np.abs(rrgb.astype(int) - prgb.astype(int)).max())
            rel = f"diff/c{col:05d}.png"
            save_png(d, out_dir / rel)
            entry.update(diff=rel, gt8=gt8, meanabs=round(meanabs, 4), maxd=maxd)
            n_diff += 1
            if gt8 > worst["gt8"]:
                worst = {"offset": row["offset"], "label": row["label"], "gt8": gt8}
            dims = dims or [int(prgb.shape[1]), int(prgb.shape[0])]
        frames.append(entry)

    manifest = {
        "scenario": pmeta.scenario,
        "anchor": pmeta.anchor,
        "anchor_occ": pmeta.anchor_occ,
        "join_verdict": join["join_verdict"],
        "verdict": join["verdict"],
        "load_stretch": join["load_stretch"],
        "dims": dims,
        "amp": amp,
        "offset0": rows[0]["offset"] if rows else None,
        "count": len(rows),
        "n_diff": n_diff,
        "n_gaps": sum(1 for r in rows if r["gap"]),
        "worst": worst if worst["offset"] is not None else None,
        "port_entry": str(pside.entry),
        "retail_entry": str(rside.entry),
        "frames": frames,
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=1))
    if not quiet:
        print(f"baked {len(frames)} columns ({n_diff} diffs, {manifest['n_gaps']} gaps) "
              f"-> {out_dir}")
        print(f"  join: {manifest['join_verdict']}   dims: {dims}   "
              f"load-stretch: {manifest['load_stretch']:+d}")
        if manifest["worst"]:
            print(f"  worst:   offset {worst['offset']}  gt8={worst['gt8']}px")
    return manifest


def _winpath(p: Path) -> str:
    """WSL path -> Windows path (the native viewer is a Windows process; fopen needs a
    Windows path for the container, like replay.exe gets)."""
    import subprocess
    return subprocess.run(["wslpath", "-w", str(Path(p).resolve())],
                          capture_output=True, text=True, check=True).stdout.strip()


def _sha256_file(p: Path) -> str:
    """Stream the SHA-256 of the whole v3cap.bin container. Baked into view.json so a
    downstream proof (parity_prove) can BIND a pixel/render metrics doc to the EXACT
    container this window's identity join + draw report were built from: a foreign or
    stale metrics doc with matching frame keys but a different source container is then
    rejected (INCONCLUSIVE) instead of silently trusted. This is the container-content
    hash the M0 adversarial review's HOLE-2 close (EP-08) threads as `expected_containers`.
    ~0.3 s for a ~90 MB container, paid only on --view."""
    import hashlib
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write_view_json(port_entry: Path, retail_entry: Path, out_path: Path,
                    join_anchor: str | None = None) -> dict:
    """Emit the NATIVE viewer's lean manifest: the identity-join timeline + the two
    container paths (Windows), NO baked images — the viewer replays frames live from
    the containers (the container is the only artifact). Each column carries both
    sides' kept-frame INDEX (what the viewer renders) or an honest gap, plus the
    per-side draw/call counts + presents for the state panel. `port_entry`/`retail_entry`
    are parse-once v3cache.LoadedSides (threaded from orv3_window) OR entry Paths — either
    way each container parses ONCE here, shared by _side_index, the internal sync, and the
    per-column material bake."""
    import orv3_draws   # local import: the semantic-diff layer (P3 N3)
    import orv3_state   # local import: the engine-state pillar (the v2 game-state panel)

    pside, rside = v3cache.as_side(port_entry), v3cache.as_side(retail_entry)
    pmeta, pidx, pdims, pc = _side_index(pside)
    rmeta, ridx, _, rc = _side_index(rside)
    if join_anchor:   # opt-in re-based join — re-key the columns (and state below) from
        pidx, ridx = pside.reindex(join_anchor), rside.reindex(join_anchor)   # a shared anchor
    join = orv3_sync.sync_entries(pside, rside, quiet=True, join_anchor=join_anchor)
    rows = merge_keys(set(pidx), set(ridx), merge_anchor_seq(pmeta, rmeta))
    # Engine state per identity LABEL (from each side's cached call_trace.jsonl, a
    # --state drive). Empty {} without --state ⇒ the viewer shows the opt-in hint.
    state_rows = orv3_state.build_state_rows(pside, rside, join_anchor)
    # one ResHash per container, reused across every column (a resource is hashed once,
    # not per frame) — the difference between a fast bake and a pathological one at scale.
    preshash, rreshash = orv3_draws.ResHash(pc), orv3_draws.ResHash(rc)
    frames = []
    for row in rows:
        key = row["key"]
        pf, rf = pidx.get(key), ridx.get(key)
        fr = {
            "offset": row["offset"], "label": row["label"], "gap": row["gap"],
            "port_idx": pf.index if pf else None, "retail_idx": rf.index if rf else None,
            "port_present": pf.present if pf else None, "retail_present": rf.present if rf else None,
            "port_draws": pf.n_draws if pf else None, "retail_draws": rf.n_draws if rf else None,
            "port_calls": pf.n_calls if pf else None, "retail_calls": rf.n_calls if rf else None,
        }
        if pf and rf:   # both sides present → the material/draw-program semantic diff
            fr.update(orv3_draws.frame_draw_report(pc, pf.index, rc, rf.index, preshash, rreshash))
        st = state_rows.get(row["label"])   # {"port": {...}, "retail": {...}} or None
        if st:
            fr["state"] = st
        frames.append(fr)
    manifest = {
        "scenario": pmeta.scenario, "anchor": pmeta.anchor, "anchor_occ": pmeta.anchor_occ,
        "join_verdict": join["join_verdict"], "verdict": join["verdict"], "load_stretch": join["load_stretch"], "dims": pdims,
        "port_container": _winpath(pside.entry / "v3cap.bin"),
        "retail_container": _winpath(rside.entry / "v3cap.bin"),
        # Content hashes of the SAME two containers (not just their paths) — the
        # provenance the proof compiler binds a metrics doc to (HOLE-2 / EP-08).
        "port_container_sha256": _sha256_file(pside.entry / "v3cap.bin"),
        "retail_container_sha256": _sha256_file(rside.entry / "v3cap.bin"),
        # Windows-local notes file the viewer reads+writes (UNC paths aren't writable
        # from the Windows viewer); orv3_notes.py reads the same file from WSL.
        "notes_path": _winpath(v3cache.notes_file(pmeta.scenario)),
        "offset0": rows[0]["offset"] if rows else None, "count": len(rows),
        "n_gaps": sum(1 for r in rows if r["gap"]),
        "has_state": bool(state_rows),   # the viewer's game-state panel is populated
        "frames": frames,
    }
    Path(out_path).write_text(json.dumps(manifest, indent=1))
    return manifest


def main() -> int:
    ap = argparse.ArgumentParser(description="v3 view bake (port+retail cache pair -> PNG panels + manifest)")
    ap.add_argument("port_entry", type=Path, help="port cache entry dir (v3cap.bin + v3ref_*.raw + v3meta.json)")
    ap.add_argument("retail_entry", type=Path, help="retail cache entry dir")
    ap.add_argument("--out", type=Path, default=None,
                    help="output view dir (PNG bake), or view.json path with --native")
    ap.add_argument("--amp", type=float, default=6.0, help="diff amplification (default 6)")
    ap.add_argument("--native", action="store_true",
                    help="emit the native viewer's view.json (no PNG bake) instead")
    args = ap.parse_args()
    if args.native:
        out = args.out or (args.port_entry / "view.json")
        m = write_view_json(args.port_entry, args.retail_entry, out)
        print(f"wrote {out} — {m['count']} columns ({m['n_gaps']} gaps), {m['join_verdict']}, "
              f"dims {m['dims']}")
        return 0
    out = args.out or (args.port_entry / "view")
    build_view(args.port_entry, args.retail_entry, out, amp=args.amp)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
