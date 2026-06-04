#!/usr/bin/env python3
"""
tools/render_diff.py — Phase D.6 D3D state-trace differential orchestrator.

Reads two JSONL traces (one per side — retail emitted by `tools/frida_capture
.py --d3d-trace`, port emitted by `tools/run-openrecet.sh --d3d-trace`) and
surfaces the first per-frame divergence with a context window.  Pairs with
the emitters from D.4 (Frida side) and D.5 (port side).  See
`docs/findings/d3d-trace.md` for the schema + `docs/findings/render-diff.md`
for the diff semantics.

Pipeline:
  1. Load each JSONL into per-frame event lists.
  2. (Optional) apply a ret_va scope filter per side (`--retail-scope` /
     `--port-scope` / `--scope` for both).
  3. (Default) collapse redundant state writes via a per-key "last seen
     value" cache — engine D3D drivers coalesce these on the wire, so the
     traces should diff cleanly only after the same collapse on both sides.
  4. Per frame, align retail vs port via `difflib.SequenceMatcher` keyed by
     `(op, canonical_args)`.  Each non-equal opcode block is one
     divergence; print a context window (`±N`, default 5) of events from
     each side, with the offending blocks highlighted.

CLI:
    nix develop --command tools/render_diff.py \\
        --retail runs/retail-boot-idle/d3d_trace.jsonl \\
        --port   runs/port-boot-idle/d3d_trace.jsonl

    # narrow to events inside FUN_00457714 (engine PII.3b walker) and
    # scene1_walker_pass_render_house (port equivalent):
    tools/render_diff.py … \\
        --retail-scope 0x57714:0x58567 \\
        --port-scope   0xNNNN:0xNNNN

    # one frame only, larger context:
    tools/render_diff.py … --frames 90 --context 10

    # turn off the coalesce-redundant-writes pass:
    tools/render_diff.py … --no-coalesce

Exit code: 0 if frames diff bit-clean, 1 on any divergence, 2 on a
structural error (missing frames, unparseable input).
"""

from __future__ import annotations

import argparse
import difflib
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


# ── canonicalisation ──────────────────────────────────────────────────────


def _canon_args(args: Any) -> tuple:
    """Recursively freeze args into a hashable/comparable tuple.
    Lists → tuples, dicts → sorted (key,val) tuple-pairs.  Used as the
    SequenceMatcher key, so two events compare equal iff their op + every
    arg value matches verbatim."""
    if isinstance(args, dict):
        return tuple((k, _canon_args(args[k])) for k in sorted(args))
    if isinstance(args, list):
        return tuple(_canon_args(v) for v in args)
    return args


def _is_pointer_str(v: Any) -> bool:
    """`"0xNN"` hex-string values are how the emitters serialise pointer
    args (texture/VB/IB handles + immediate-mode data pointers).  Plain
    integers are value args (states / handles / counts) — those stay
    raw."""
    return isinstance(v, str) and v.startswith("0x")


def opaqueify_pointers(events: list[dict]) -> list[dict]:
    """Replace each `"0xNN"` arg value with a synthetic `"#0"`, `"#1"`,
    … id, allocated per (event op, arg field) pair in first-seen order.

    Two retail/port events compare equal afterwards if their pointer
    args occupy the same logical position in each side's allocation
    sequence — true whenever both sides load the same mesh/texture set
    in the same order (the typical walker-draw case).  Use the raw
    diff (`--no-opaque-pointers`, the default) to see the underlying
    addresses, or `--opaque-pointers` to suppress address noise."""
    out: list[dict] = []
    # one dictionary per (op, field) so a SetTexture(stage=0) handle
    # and a SetIndices(ib) handle keep separate id spaces — they're
    # never semantically interchangeable.
    seen: dict[tuple, dict[str, str]] = {}
    for evt in events:
        new_args: dict[str, Any] = {}
        for k, v in evt.get("args", {}).items():
            if _is_pointer_str(v):
                bucket = seen.setdefault((evt["op"], k), {})
                if v not in bucket:
                    bucket[v] = f"#{len(bucket)}"
                new_args[k] = bucket[v]
            else:
                new_args[k] = v
        out.append({**evt, "args": new_args})
    return out


# Vertex-content fields are NOT part of a draw's structural identity — two
# draws align on (op, prim args, stride, pointer-id), and `--explain` then
# decodes the bytes to name the first divergent vertex field.  Excluding
# them from the key means a pure-vertex divergence (same command, different
# vertices) aligns as an "equal" block that --explain still inspects.
_NONKEY_ARGS = frozenset((
    "vb_bytes", "vb_nverts", "ib_bytes", "ib_nidx", "vb_over", "ib_over",
))


def _event_key(evt: dict) -> tuple[str, tuple]:
    """SequenceMatcher hash key — drops `ret_va`/`frame` (side-metadata) and
    the vertex-content fields (decoded separately by --explain)."""
    args = {k: v for k, v in evt.get("args", {}).items()
            if k not in _NONKEY_ARGS}
    return (evt["op"], _canon_args(args))


# ── FVF vertex decode (for --explain) ─────────────────────────────────────
#
# The d3d-trace captures each immediate-mode draw's raw vertex bytes
# (`vb_bytes`, lowercase hex) + count (`vb_nverts`) when run with vertex
# capture on (port `--d3d-trace-verts`, Frida `d3d_trace_verts`).  The FVF
# in effect is the most-recent SetVertexShader handle (fixed-function FVF
# codes have the high bits clear), tracked while walking the event stream.
# Given (fvf, stride, bytes) we decode each vertex into named fields so a
# divergence reads as "vertex 2 POSITION.z: retail X vs port Y" instead of
# an opaque hex-string mismatch.

import struct  # noqa: E402  (kept local to the decode section)

# D3DFVF bit flags (d3d8types.h).
_FVF_POSITION_MASK = 0x00E
_FVF_XYZ           = 0x002   # 3 floats
_FVF_XYZRHW        = 0x004   # 4 floats (x,y,z,rhw)
_FVF_XYZB1         = 0x006   # 3 floats + 1 blend weight
_FVF_NORMAL        = 0x010   # 3 floats
_FVF_PSIZE         = 0x020   # 1 float
_FVF_DIFFUSE       = 0x040   # 1 u32 (BGRA)
_FVF_SPECULAR      = 0x080   # 1 u32
_FVF_TEXCOUNT_MASK = 0xF00
_FVF_TEXCOUNT_SHIFT = 8

# Best-effort stride→FVF fallback for draws whose SetVertexShader landed
# before the capture window (so the tracked FVF is unknown).  Keyed on the
# vertex strides this engine actually emits; the tracked FVF always wins
# when present.
_STRIDE_FVF_FALLBACK = {
    24: 0x142,   # XYZ | DIFFUSE | TEX1            (12+4+8) — sparkle/items/dust
    28: 0x144,   # XYZRHW | DIFFUSE | TEX1         (16+4+8)
    32: 0x1c4,   # XYZRHW | DIFFUSE | SPECULAR | TEX1 (16+4+4+8) — 2D/HUD quads
    20: 0x102,   # XYZ | TEX1                      (12+8)
    16: 0x042,   # XYZ | DIFFUSE                   (12+4)
}


def fvf_field_layout(fvf: int) -> list[tuple[str, str, int]]:
    """Return the ordered field layout for an FVF: (name, kind, n_floats).
    kind ∈ {"f","color"}; for "color" n_floats is 1 (a u32)."""
    layout: list[tuple[str, str, int]] = []
    pos = fvf & _FVF_POSITION_MASK
    if pos == _FVF_XYZRHW:
        layout.append(("POSITION", "f", 4))   # x,y,z,rhw
    elif pos == _FVF_XYZ:
        layout.append(("POSITION", "f", 3))
    elif pos >= _FVF_XYZB1:
        # blend-weighted positions: 3 pos floats + (count) blend weights.
        nblend = (pos - _FVF_XYZ) // 2
        layout.append(("POSITION", "f", 3))
        if nblend:
            layout.append(("BLENDWEIGHT", "f", nblend))
    if fvf & _FVF_NORMAL:
        layout.append(("NORMAL", "f", 3))
    if fvf & _FVF_PSIZE:
        layout.append(("PSIZE", "f", 1))
    if fvf & _FVF_DIFFUSE:
        layout.append(("DIFFUSE", "color", 1))
    if fvf & _FVF_SPECULAR:
        layout.append(("SPECULAR", "color", 1))
    ntex = (fvf & _FVF_TEXCOUNT_MASK) >> _FVF_TEXCOUNT_SHIFT
    for t in range(ntex):
        layout.append((f"TEX{t}", "f", 2))   # default 2D texcoords
    return layout


def decode_vertices(vb_hex: str, nverts: int, stride: int,
                    fvf: int) -> list[dict] | None:
    """Decode `nverts` vertices from `vb_hex` (lowercase hex) using `fvf`
    (or the stride fallback).  Returns a list of per-vertex dicts mapping
    field name → value (list[float] for "f", int for "color").  None if the
    bytes can't be decoded (bad length / unknown layout)."""
    try:
        raw = bytes.fromhex(vb_hex)
    except ValueError:
        return None
    if stride <= 0 or nverts <= 0 or len(raw) < nverts * stride:
        return None
    if not fvf:
        fvf = _STRIDE_FVF_FALLBACK.get(stride, 0)
    layout = fvf_field_layout(fvf) if fvf else None
    out: list[dict] = []
    for v in range(nverts):
        base = v * stride
        vert: dict = {}
        if layout is None:
            # Unknown FVF: expose the raw little-endian floats so a diff
            # still localises which 4-byte lane changed.
            nlanes = stride // 4
            vert["RAW_f"] = list(
                struct.unpack_from(f"<{nlanes}f", raw, base))
        else:
            off = base
            ok = True
            for name, kind, n in layout:
                if off + 4 * n > base + stride:
                    ok = False
                    break
                if kind == "color":
                    vert[name] = struct.unpack_from("<I", raw, off)[0]
                    off += 4
                else:
                    vert[name] = list(
                        struct.unpack_from(f"<{n}f", raw, off))
                    off += 4 * n
            if not ok:
                nlanes = stride // 4
                vert = {"RAW_f": list(
                    struct.unpack_from(f"<{nlanes}f", raw, base))}
        out.append(vert)
    return out


# ── state-coalescing collapse ─────────────────────────────────────────────
#
# Engine D3D drivers internally drop "set state X to value V when it is
# already V" writes on the way to the GPU.  Our traces capture every
# API-level call regardless, so a target that bulk-uploads state every
# frame (retail title BG) shows hundreds of writes that are no-ops at the
# driver level.  Coalesce both sides identically: keep the FIRST write of
# any (op, key) → value; drop subsequent writes that don't change the
# value.  Reset on draw calls — any set after a draw is "live" again.

# Which arg fields combine into the "what state is being set" key
# (everything else is the "value").  Draws + UP-draws have no key + no
# coalesce (they're side-effects, never redundant).
_COALESCE_KEYS = {
    "SetRenderState":       ("state",),
    "SetTextureStageState": ("stage", "type"),
    "SetTransform":         ("state",),
    "SetTexture":           ("stage",),
    "SetStreamSource":      ("stream",),
    "SetIndices":           (),
    "SetVertexShader":      (),
    "SetMaterial":          (),
}


def _coalesce_key(evt: dict) -> tuple | None:
    fields = _COALESCE_KEYS.get(evt["op"])
    if fields is None:
        return None
    args = evt.get("args", {})
    return (evt["op"],) + tuple(args.get(f) for f in fields)


def collapse_redundant(events: list[dict]) -> list[dict]:
    """Drop redundant state writes; preserve relative order of survivors."""
    out: list[dict] = []
    seen: dict[tuple, tuple] = {}      # (op,key…) -> _canon_args(args)
    for evt in events:
        if evt["op"].startswith("Draw"):
            out.append(evt)
            seen.clear()               # any post-draw set is "live" again
            continue
        ck = _coalesce_key(evt)
        if ck is None:
            out.append(evt)
            continue
        val = _canon_args(evt.get("args", {}))
        if seen.get(ck) == val:
            continue                   # same key + same value → drop
        seen[ck] = val
        out.append(evt)
    return out


# ── ret_va scope filter ───────────────────────────────────────────────────


def _parse_range(spec: str) -> tuple[int, int]:
    """`--scope` argument parser. `LO:HI` or `LO-HI`, with optional 0x."""
    sep = ":" if ":" in spec else "-"
    if sep not in spec:
        raise argparse.ArgumentTypeError(
            f"scope must be LO:HI (or LO-HI); got {spec!r}")
    lo_s, hi_s = spec.split(sep, 1)
    lo = int(lo_s.strip(), 0)
    hi = int(hi_s.strip(), 0)
    if lo >= hi:
        raise argparse.ArgumentTypeError(
            f"scope LO ({lo:#x}) must be < HI ({hi:#x})")
    return lo, hi


def apply_scope(events: list[dict],
                scope: tuple[int, int] | None) -> list[dict]:
    if scope is None:
        return events
    lo, hi = scope
    return [e for e in events if lo <= e.get("ret_va", 0) < hi]


# ── load + bucket-by-frame ────────────────────────────────────────────────


def load_trace(path: Path) -> dict[int, list[dict]]:
    """Read a JSONL trace.  Returns frame → list of events in emission order.
    Raises on the first malformed row."""
    by_frame: dict[int, list[dict]] = {}
    with path.open() as f:
        for lineno, raw in enumerate(f, 1):
            raw = raw.strip()
            if not raw:
                continue
            try:
                evt = json.loads(raw)
            except json.JSONDecodeError as e:
                raise SystemExit(
                    f"{path}:{lineno}: malformed JSON: {e}")
            if "op" not in evt or "frame" not in evt:
                raise SystemExit(
                    f"{path}:{lineno}: missing op/frame field: {evt!r}")
            by_frame.setdefault(int(evt["frame"]), []).append(evt)
    return by_frame


# ── diff core ─────────────────────────────────────────────────────────────


@dataclass
class FrameDiff:
    frame:    int
    n_retail: int
    n_port:   int
    blocks:   list[dict] = field(default_factory=list)   # diff blocks
    # block: {"tag": "replace"|"delete"|"insert",
    #         "retail": [evt,...], "port": [evt,...],
    #         "i_lo": int, "p_lo": int}

    @property
    def diverged(self) -> bool:
        return bool(self.blocks)


def diff_frame(frame: int,
               retail: list[dict],
               port: list[dict]) -> FrameDiff:
    fd = FrameDiff(frame=frame, n_retail=len(retail), n_port=len(port))
    r_keys = [_event_key(e) for e in retail]
    p_keys = [_event_key(e) for e in port]
    sm = difflib.SequenceMatcher(a=r_keys, b=p_keys, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        fd.blocks.append({
            "tag":    tag,
            "retail": retail[i1:i2],
            "port":   port[j1:j2],
            "i_lo":   i1,
            "p_lo":   j1,
        })
    return fd


# ── --explain: vertex-level divergence ────────────────────────────────────
#
# Aligns the two command streams (same SequenceMatcher key as the structural
# diff), then for each aligned immediate-mode draw pair decodes both sides'
# vertices and names the FIRST divergent (vertex, field).  A pure-vertex
# divergence (identical command, different vertices) shows up here even though
# the structural diff calls the block "equal".


_UP_DRAWS = frozenset(("DrawPrimitiveUP", "DrawIndexedPrimitiveUP"))


def annotate_fvf(events: list[dict]) -> None:
    """Tag each draw event with `_fvf` = the most-recent SetVertexShader
    handle in effect (0 if none seen in-window).  Mutates in place."""
    cur = 0
    for e in events:
        if e["op"] == "SetVertexShader":
            cur = e.get("args", {}).get("handle", 0) or 0
        elif e["op"].startswith("Draw"):
            e["_fvf"] = cur


def _floats_diverge(a: float, b: float, eps: float) -> bool:
    return abs(a - b) > max(eps, eps * max(abs(a), abs(b)))


@dataclass
class Divergence:
    frame:   int
    op:      str
    ret_va:  int
    kind:    str         # "field" | "count" | "decode" | "structural"
    detail:  str
    r_index: int = -1
    p_index: int = -1


def _first_field_divergence(frame: int, ri: int, pi: int,
                            rdraw: dict, pdraw: dict,
                            eps: float) -> Divergence | None:
    """Decode both draws' vertices and return the first divergent field,
    or None if they match (within eps).  Assumes both are UP draws with
    vb_bytes present."""
    rargs, pargs = rdraw.get("args", {}), pdraw.get("args", {})
    rb, pb = rargs.get("vb_bytes"), pargs.get("vb_bytes")
    op = rdraw["op"]
    ret_va = rdraw.get("ret_va", 0)
    if rb is None or pb is None:
        return None                      # capture-verts not on for this side
    stride = rargs.get("vb_stride", 0)
    rn = int(rargs.get("vb_nverts", 0))
    pn = int(pargs.get("vb_nverts", 0))
    if rn != pn:
        return Divergence(frame, op, ret_va, "count",
                          f"vertex count differs: retail={rn} port={pn} "
                          f"(stride={stride})", ri, pi)
    rfvf = rdraw.get("_fvf", 0)
    rverts = decode_vertices(rb, rn, stride, rfvf)
    pverts = decode_vertices(pb, pn, stride, pdraw.get("_fvf", rfvf))
    if rverts is None or pverts is None:
        if rb != pb:
            return Divergence(frame, op, ret_va, "decode",
                              f"raw vb_bytes differ (undecodable; "
                              f"stride={stride}, nverts={rn})", ri, pi)
        return None
    for v, (rv, pv) in enumerate(zip(rverts, pverts)):
        for name in rv:
            a, b = rv[name], pv.get(name)
            if name.startswith("DIFFUSE") or name.startswith("SPECULAR") \
                    or name == "RAW_u":
                if a != b:
                    return Divergence(
                        frame, op, ret_va, "field",
                        f"vertex {v} {name}: retail=0x{a:08x} "
                        f"port=0x{b:08x}", ri, pi)
            else:
                # list of floats
                for k, (fa, fb) in enumerate(zip(a, b or [])):
                    if _floats_diverge(fa, fb, eps):
                        comp = "." + "xyzw"[k] if name == "POSITION" \
                            and k < 4 else f"[{k}]"
                        return Divergence(
                            frame, op, ret_va, "field",
                            f"vertex {v} {name}{comp}: retail={fa:.5g} "
                            f"port={fb:.5g} (Δ{fb - fa:+.5g}, fvf=0x{rfvf:x}"
                            f", stride={stride})", ri, pi)
    return None


def explain_frame(frame: int, retail: list[dict], port: list[dict],
                  eps: float) -> list[Divergence]:
    """Find vertex-level + structural draw divergences for one frame, in
    command-stream order."""
    annotate_fvf(retail)
    annotate_fvf(port)
    r_keys = [_event_key(e) for e in retail]
    p_keys = [_event_key(e) for e in port]
    sm = difflib.SequenceMatcher(a=r_keys, b=p_keys, autojunk=False)
    found: list[Divergence] = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for i, j in zip(range(i1, i2), range(j1, j2)):
                if retail[i]["op"] in _UP_DRAWS:
                    d = _first_field_divergence(frame, i, j,
                                                retail[i], port[j], eps)
                    if d:
                        found.append(d)
        else:
            # structural: a draw present on one side only, or a command
            # mismatch.  Report the draws involved (the upstream cause of a
            # downstream vertex divergence is usually here).
            for i in range(i1, i2):
                if retail[i]["op"].startswith("Draw"):
                    found.append(Divergence(
                        frame, retail[i]["op"], retail[i].get("ret_va", 0),
                        "structural",
                        f"retail-only draw (tag={tag})", r_index=i))
            for j in range(j1, j2):
                if port[j]["op"].startswith("Draw"):
                    found.append(Divergence(
                        frame, port[j]["op"], port[j].get("ret_va", 0),
                        "structural",
                        f"port-only draw (tag={tag})", p_index=j))
    return found


def print_explain(retail_by_frame: dict[int, list[dict]],
                  port_by_frame: dict[int, list[dict]],
                  frames: list[int], eps: float, first_only: bool) -> bool:
    """Print the vertex-level divergence report.  Returns True if any
    divergence was found."""
    any_div = False
    for f in frames:
        divs = explain_frame(f, retail_by_frame.get(f, []),
                             port_by_frame.get(f, []), eps)
        if not divs:
            continue
        any_div = True
        print()
        print("=" * 78)
        print(f"FRAME {f}: {len(divs)} draw divergence(s)")
        shown = divs[:1] if first_only else divs
        for d in shown:
            loc = []
            if d.r_index >= 0:
                loc.append(f"r#{d.r_index}")
            if d.p_index >= 0:
                loc.append(f"p#{d.p_index}")
            print(f"  [{d.kind}] {d.op} @ret_va={d.ret_va:#x} "
                  f"({','.join(loc)})")
            print(f"      {d.detail}")
        if first_only and len(divs) > 1:
            print(f"  … +{len(divs) - 1} more (use --explain-all)")
    if not any_div:
        print("  ✓ no draw/vertex divergence on the compared frame(s)")
    return any_div


# ── pretty printer ────────────────────────────────────────────────────────


_OP_WIDTH = 24


def _fmt_evt(evt: dict) -> str:
    args = evt.get("args", {})
    return (f"{evt['op']:<{_OP_WIDTH}} "
            f"args={json.dumps(args, separators=(',', ':'))}  "
            f"ret_va={evt.get('ret_va',0):#x}")


def _print_context(label: str,
                   evts: list[dict],
                   pivot_lo: int, pivot_hi: int,
                   ctx: int) -> None:
    """Print evts[pivot_lo-ctx : pivot_hi+ctx] with `>` markers on the
    pivot slice and indices to the original list."""
    lo = max(0, pivot_lo - ctx)
    hi = min(len(evts), pivot_hi + ctx)
    print(f"  {label} [{lo}..{hi})  (pivot: [{pivot_lo}..{pivot_hi}))")
    if lo == hi:
        print("    (empty)")
        return
    for i in range(lo, hi):
        marker = ">" if pivot_lo <= i < pivot_hi else " "
        print(f"   {marker} #{i:<5d} {_fmt_evt(evts[i])}")


def print_diffs(retail_by_frame: dict[int, list[dict]],
                port_by_frame:   dict[int, list[dict]],
                frame_diffs:     list[FrameDiff],
                ctx: int, max_blocks: int) -> None:
    """Top-level diff printer.  We keep the full per-frame event lists in
    scope so the context window can index past the divergence block."""
    for fd in frame_diffs:
        print()
        print("=" * 78)
        print(f"FRAME {fd.frame}: retail={fd.n_retail} evts, "
              f"port={fd.n_port} evts, {len(fd.blocks)} diff block(s)")
        if not fd.diverged:
            print("  ✓ identical after coalesce")
            continue
        r_evts = retail_by_frame.get(fd.frame, [])
        p_evts = port_by_frame.get(fd.frame, [])
        for n, blk in enumerate(fd.blocks, 1):
            if n > max_blocks:
                print(f"  … and {len(fd.blocks) - n + 1} more block(s); "
                      f"use --max-divergences to show more")
                break
            tag = blk["tag"]
            i_lo, p_lo = blk["i_lo"], blk["p_lo"]
            i_hi = i_lo + len(blk["retail"])
            p_hi = p_lo + len(blk["port"])
            print()
            print(f"  [block {n}/{len(fd.blocks)}] tag={tag}  "
                  f"retail [{i_lo}..{i_hi})  port [{p_lo}..{p_hi})")
            _print_context("retail", r_evts, i_lo, i_hi, ctx)
            _print_context("port  ", p_evts, p_lo, p_hi, ctx)


# ── summary ───────────────────────────────────────────────────────────────


def print_summary(retail_by_frame: dict[int, list[dict]],
                  port_by_frame: dict[int, list[dict]],
                  frame_diffs: list[FrameDiff]) -> None:
    print()
    print("─" * 78)
    print(f"  retail trace: {sum(len(v) for v in retail_by_frame.values())} "
          f"events across {len(retail_by_frame)} frame(s)")
    print(f"  port   trace: {sum(len(v) for v in port_by_frame.values())} "
          f"events across {len(port_by_frame)} frame(s)")
    diverged = [fd for fd in frame_diffs if fd.diverged]
    if not diverged:
        print(f"  ✓ all {len(frame_diffs)} compared frame(s) identical "
              f"after coalesce.")
    else:
        print(f"  ✗ {len(diverged)}/{len(frame_diffs)} compared frame(s) "
              f"diverge.  First: frame {diverged[0].frame} with "
              f"{len(diverged[0].blocks)} block(s).")


# ── main ──────────────────────────────────────────────────────────────────


def parse_frames(spec: str) -> list[int]:
    out: list[int] = []
    for tok in spec.split(","):
        tok = tok.strip()
        if not tok:
            continue
        out.append(int(tok, 0))
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--retail", required=True, type=Path,
        help="retail-side JSONL (from frida_capture.py --d3d-trace)")
    ap.add_argument("--port", required=True, type=Path,
        help="port-side JSONL (from run-openrecet.sh --d3d-trace)")
    ap.add_argument("--frames", default=None,
        help="comma-separated frame numbers to compare. Default: the "
             "intersection of frames present in both traces.")
    ap.add_argument("--scope", type=_parse_range, default=None,
        help="apply LO:HI ret_va filter to BOTH sides. Use this if the "
             "engine + port functions sit at the same module-relative "
             "VA (rare).")
    ap.add_argument("--retail-scope", type=_parse_range, default=None,
        help="LO:HI ret_va filter, retail side only. Hex accepted.")
    ap.add_argument("--port-scope", type=_parse_range, default=None,
        help="LO:HI ret_va filter, port side only. Hex accepted.")
    ap.add_argument("--context", type=int, default=5,
        help="N events of context around each diff block (default %(default)d)")
    ap.add_argument("--no-coalesce", action="store_true",
        help="disable the redundant-state-write collapse (off by default; "
             "useful for raw byte-for-byte traces).")
    ap.add_argument("--opaque-pointers", action="store_true",
        help="rewrite each pointer-shaped arg (\"0xNN\") to a synthetic "
             "first-seen id (\"#0\",\"#1\",…). Use this to cancel out "
             "allocator-address noise when both sides allocate the same "
             "set of objects in the same order (typical walker-draw "
             "case).")
    ap.add_argument("--explain", action="store_true",
        help="vertex-level mode: decode each aligned immediate-mode draw's "
             "captured vertices (vb_bytes, from --d3d-trace-verts / "
             "d3d_trace_verts) and name the FIRST divergent (vertex, field) "
             "per frame. Implies --opaque-pointers so UP data pointers align.")
    ap.add_argument("--explain-all", action="store_true",
        help="with --explain, show every draw divergence per frame, not "
             "just the first.")
    ap.add_argument("--vertex-eps", type=float, default=1e-4,
        help="float tolerance for --explain field compares "
             "(abs+relative, default %(default)g)")
    ap.add_argument("--max-divergences", type=int, default=20,
        help="max diff blocks to print per frame (default %(default)d)")
    ap.add_argument("--quiet", action="store_true",
        help="suppress per-frame diff body; print only the summary line")
    args = ap.parse_args(argv)

    if not args.retail.exists():
        raise SystemExit(f"retail trace not found: {args.retail}")
    if not args.port.exists():
        raise SystemExit(f"port trace not found: {args.port}")

    retail_raw = load_trace(args.retail)
    port_raw   = load_trace(args.port)

    # scope filter
    r_scope = args.retail_scope or args.scope
    p_scope = args.port_scope   or args.scope
    retail_filt = {f: apply_scope(evts, r_scope)
                   for f, evts in retail_raw.items()}
    port_filt   = {f: apply_scope(evts, p_scope)
                   for f, evts in port_raw.items()}

    # --explain aligns immediate-mode draws whose `vb` is a transient CPU
    # data pointer (always differs port↔retail); opaque-pointers maps those
    # to first-seen ids so the draws line up.
    if args.explain:
        args.opaque_pointers = True

    # opaque-pointers (applied BEFORE coalesce — synthetic ids are
    # what coalesce should look at, otherwise a SetTexture(0,0xa) call
    # followed by SetTexture(0,0xa) coalesces fine without rewriting,
    # but the rewrite makes that explicit for downstream operations).
    if args.opaque_pointers:
        retail_filt = {f: opaqueify_pointers(e) for f, e in retail_filt.items()}
        port_filt   = {f: opaqueify_pointers(e) for f, e in port_filt.items()}

    # coalesce
    if not args.no_coalesce:
        retail_filt = {f: collapse_redundant(e) for f, e in retail_filt.items()}
        port_filt   = {f: collapse_redundant(e) for f, e in port_filt.items()}

    # frame selection
    if args.frames is not None:
        frames = parse_frames(args.frames)
        missing_r = [f for f in frames if f not in retail_filt]
        missing_p = [f for f in frames if f not in port_filt]
        if missing_r:
            raise SystemExit(
                f"frame(s) missing in retail: {missing_r}\n"
                f"  retail has: {sorted(retail_filt)}")
        if missing_p:
            raise SystemExit(
                f"frame(s) missing in port: {missing_p}\n"
                f"  port has: {sorted(port_filt)}")
    else:
        common = sorted(set(retail_filt) & set(port_filt))
        if not common:
            raise SystemExit(
                f"no frames in common.\n"
                f"  retail frames: {sorted(retail_filt)}\n"
                f"  port frames:   {sorted(port_filt)}")
        frames = common

    # --explain: vertex-level report instead of the structural block diff.
    if args.explain:
        diverged = print_explain(retail_filt, port_filt, frames,
                                 args.vertex_eps, not args.explain_all)
        return 1 if diverged else 0

    # diff
    frame_diffs = [diff_frame(f, retail_filt[f], port_filt[f]) for f in frames]

    if not args.quiet:
        print_diffs(retail_filt, port_filt, frame_diffs,
                    args.context, args.max_divergences)
    print_summary(retail_filt, port_filt, frame_diffs)

    return 0 if all(not fd.diverged for fd in frame_diffs) else 1


if __name__ == "__main__":
    sys.exit(main())
