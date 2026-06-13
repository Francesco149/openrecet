#!/usr/bin/env python3
"""Trace Studio v3 — the engine-state pillar (the v2 game-state panel, identity-keyed).

v3 captures the RENDER program (the d3d command stream), not engine state. The v2 web
studio had a per-frame STATE panel — the once-per-frame flow-trace fields (rng,
player/companion px/py/anim, title menu, dialogue box) read from `call_trace.jsonl`,
port-vs-retail, diff-highlighted. This module is its v3 form:

  - `state_call_trace_config()` — the CAPTURE side: the 4 lightweight once-per-frame
    STATE_VAS + their declared fields, handed to a --state drive so it hooks EXACTLY
    those (not the heavy full call-graph).
  - `build_state_rows()` — the VIEW side: load each side's `call_trace.jsonl` and key
    every event by the SAME stored identity the d3d frames + the join use
    (`meta.key_of_present(frame)`), so a state row slots onto the identity timeline
    with ZERO new sync logic. It composes onto its column no matter how far the load
    stretched the two sides apart (E3) — exactly like the d3d frames and pairs.

Capture overhead (why --state is OPT-IN, off by default — user call 2026-06-13): the
probes are only 4 hooks, but a v3 segtrace drive has no per-frame whitelist, so they
emit through the whole pre-window load-stretch (the per-frame send cost). Enable
--state when chasing an engine-state divergence; the d3d capture stays lean by default.
The state is keyed by IDENTITY, so it can equally be captured in a separate pass and
still join onto an already-captured d3d session by `(anchor, offset)`. The OUTPUT is
always correctly windowed regardless of capture cost — only kept-frame identities
become columns, so load-stretch events are dropped downstream.
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

# The standard once-per-frame flow-trace anchors whose declared fields the state panel
# surfaces (docs/flow-trace-cheatsheet.md "Standard once-per-frame anchors"). These
# emit once per frame in their own engine state, so one record/frame/VA — the lightweight
# subset of the full bisect-vetted call-graph.
STATE_VAS = {
    0x47be92: "sched",   # tick_scheduler   — rng (LCG state) / rngcalls (consumption)
    0x48670f: "house",   # house_update     — player+companion poct/pang/coct/px/py/pz/...
    0x49a59e: "title",   # scene_title_sim  — menu state machine (10 fields)
    0x46c320: "dlg",     # dialogue_tick    — opening/standee + box-anim state
}

FIELDS_JSON = ROOT / "tools" / "flow" / "retail_fields.json"


def _reg_entry(reg: dict, va: int):
    """retail_fields.json keys VAs as hex strings ('0x48670f'); accept a few forms."""
    return reg.get(hex(va)) or reg.get(str(va)) or reg.get(f"0x{va:x}")


def state_call_trace_config() -> tuple[list[int], dict]:
    """(vas, fields) for a STATE-ONLY call-trace: the 4 STATE_VAS + their declared
    fields from retail_fields.json. Handed to run_capture(call_trace=True,
    call_trace_vas=vas, call_trace_fields=fields) so the agent hooks EXACTLY these —
    a non-None field spec skips frida_capture's heavy full-registry auto-load."""
    reg = json.loads(FIELDS_JSON.read_text()).get("fields", {})
    fields: dict[str, list] = {}
    for va in STATE_VAS:
        e = _reg_entry(reg, va)
        if e and "fields" in e:
            fields[str(va)] = e["fields"]
    return list(STATE_VAS), fields


def label_of(key) -> str:
    """The identity LABEL the view columns are keyed by ('ANCHOR#occ+delta')."""
    return f"{key[0]}#{key[1]}+{key[2]}"


def slim_state_trace(src: Path, dst: Path) -> int:
    """Copy only the STATE_VA lines of a call_trace.jsonl to `dst`. The PORT emits its
    whole compiled CALL_TRACE_BEGIN set (~80 VAs / MBs) under the {calltrace} op, but
    the panel only needs the 4 state probes — slim before caching so the sidecar stays
    tiny (≈100 KB, not tens of MB). Returns the kept line count. (Retail --state already
    hooks ONLY the 4 VAs, so its trace needs no slimming.)"""
    src, dst = Path(src), Path(dst)
    kept = 0
    with src.open() as f, dst.open("w") as o:
        for ln in f:
            s = ln.strip()
            if not s.startswith("{"):
                continue
            try:
                if json.loads(s).get("va") in STATE_VAS:
                    o.write(ln if ln.endswith("\n") else ln + "\n")
                    kept += 1
            except ValueError:
                continue
    return kept


def collect(call_trace_path: Path, meta) -> dict:
    """{identity_key_tuple: {field: value}} for one side's call_trace.jsonl, keyed by
    STORED identity. The call-trace `frame` is the engine/agent frame == the present-
    count the anchor stream + d3d frames use, so meta.key_of_present(frame) gives the
    SAME key the d3d frame at that present resolves to. Merges all STATE_VAS events at
    a frame into one field dict (mirrors v2 tools/trace_studio/analysis/state.py)."""
    path = Path(call_trace_path)
    if not path.exists() or not meta.anchors:   # need the stored anchor stream (meta v2)
        return {}
    sys.path.insert(0, str(ROOT / "tools"))
    from flow_diff import load_trace   # the canonical call_trace.jsonl reader
    by_frame = load_trace(path, va_filter=set(STATE_VAS))
    out: dict = {}
    for fr, evts in by_frame.items():
        merged: dict = {}
        for e in evts:
            f = e.get("f")
            if isinstance(f, dict):
                merged.update({k: _norm_f32(v) for k, v in f.items()})
        if merged:
            out[meta.key_of_present(int(fr))] = merged
    return out


def _norm_f32(v):
    """Canonicalise a float field to its f32 value, so the panel's port-vs-retail diff
    highlight reflects REAL divergence, not serialization noise: a global like px is an
    f32, but retail (Frida) widens it to a full-precision double (-0.30000001192092896)
    while the port (C printf) emits a rounded repr (-0.300000012) — the SAME f32. Round-
    tripping both through f32 collapses them to one double, so an exact compare is right.
    Ints (rng/rngcalls/db054/...) pass through, so a genuine count divergence still flags."""
    if isinstance(v, float):
        return struct.unpack("f", struct.pack("f", v))[0]
    return v


def build_state_rows(port_side, retail_side) -> dict:
    """{label: {"port": {...fields...}, "retail": {...}}} for both sides' state, keyed
    by the identity LABEL the view columns use. `port_side`/`retail_side` are parse-once
    v3cache.LoadedSides (threaded from orv3_window) OR entry Paths. Empty {} when neither
    side carries a call_trace.jsonl (a drive without --state) — the viewer then shows the
    '(re-drive with --state)' hint, exactly like v2's '(capture with --call-trace)'."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import v3cache
    pside, rside = v3cache.as_side(port_side), v3cache.as_side(retail_side)
    rows: dict = {}
    for key, f in collect(pside.entry / "call_trace.jsonl", pside.meta).items():
        rows.setdefault(label_of(key), {})["port"] = f
    for key, f in collect(rside.entry / "call_trace.jsonl", rside.meta).items():
        rows.setdefault(label_of(key), {})["retail"] = f
    return rows


def main() -> int:
    """CLI: print the per-label state rows for a cached port+retail entry pair (debug)."""
    import argparse
    ap = argparse.ArgumentParser(description="v3 engine-state rows (identity-keyed).")
    ap.add_argument("port_entry", type=Path)
    ap.add_argument("retail_entry", type=Path)
    ap.add_argument("--field", default=None, help="show only this field across labels")
    args = ap.parse_args()
    rows = build_state_rows(args.port_entry, args.retail_entry)
    if not rows:
        print("(no state — neither entry has a call_trace.jsonl; re-drive with --state)")
        return 0
    n_diff = 0
    for label in sorted(rows, key=lambda s: (s.split("#")[0], int(s.split("+")[-1]))):
        pr, rt = rows[label].get("port", {}), rows[label].get("retail", {})
        keys = sorted(set(pr) | set(rt)) if not args.field else [args.field]
        diffs = [k for k in keys if k in pr and k in rt and pr[k] != rt[k]]
        if diffs:
            n_diff += 1
            print(f"{label}: " + "  ".join(f"{k}: r={rt.get(k)} p={pr.get(k)}" for k in diffs))
    print(f"\n{len(rows)} labelled state rows, {n_diff} with a port≠retail field"
          + (f" (field {args.field})" if args.field else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
