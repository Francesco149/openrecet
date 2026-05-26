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


def _event_key(evt: dict) -> tuple[str, tuple]:
    """SequenceMatcher hash key — drops `ret_va` and `frame` (those are
    side-metadata, not part of the call's semantic identity)."""
    return (evt["op"], _canon_args(evt.get("args", {})))


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

    # diff
    frame_diffs = [diff_frame(f, retail_filt[f], port_filt[f]) for f in frames]

    if not args.quiet:
        print_diffs(retail_filt, port_filt, frame_diffs,
                    args.context, args.max_divergences)
    print_summary(retail_filt, port_filt, frame_diffs)

    return 0 if all(not fd.diverged for fd in frame_diffs) else 1


if __name__ == "__main__":
    sys.exit(main())
