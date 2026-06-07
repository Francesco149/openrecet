"""edits/apply.py — turn viewer marks (edits.jsonl) into trace edits + a Claude
worklist. The automation of the manual phase/RNG-pin loop. (Lifted verbatim from
the former tools/trace_studio_apply.py.)

Marks are keyed by the VIEWER frame index (0-based, window start = 0). The
trace's {phasepin}/{rngseed}/{caprange} ops live in the FINAL segment and are
numbered relative to that segment's anchor, so:

    segment_frame = caprange.start + viewer_frame_index

(export_trace renumbers the captured window to 0-based: viewer frame 0 ==
segment frame `caprange.start`.) state.jsonl is keyed by the viewer index, so a
mark's retail RNG / db054 is a direct lookup.

Mark kinds:
  phasepin   → insert {"phasepin": F}                       (auto-applied)
  rngpin     → insert {"rngseed": [F, value]}               (auto-applied)
  anchor     → worklist note: add a g_anchors[] entry near here (code task)
  feature    → worklist note: implement the missing path in this region
  note       → worklist note: free text
"""
from __future__ import annotations

import json
from pathlib import Path

CANON_SEED = 19937          # the bg-NPC-warmup canonical seed convention


def _load_jsonl(p: Path) -> list[dict]:
    if not p.is_file():
        return []
    out = []
    for ln in p.read_text().splitlines():
        s = ln.strip()
        if not s:
            continue
        try:
            out.append(json.loads(s))
        except json.JSONDecodeError:
            pass
    return out


def _last_wait_idx(lines: list[str]) -> int:
    """Index of the last `{wait}` op line (the final segment opener), or -1."""
    last = -1
    for i, ln in enumerate(lines):
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and "wait" in o:
            last = i
    return last


def apply(sess_dir: Path, trace_override: Path | None = None,
          auto_pin: bool = False, dry_run: bool = False) -> dict:
    manifest = json.loads((sess_dir / "session.json").read_text())
    cr_start = int(manifest.get("caprange", [0, 0])[0])
    trace = trace_override or Path(manifest.get("working_trace")
                                   or manifest["trace"])
    if not trace.exists():
        raise SystemExit(f"trace_studio apply: trace not found: {trace}")
    # NEVER edit a trace outside the session dir (e.g. a committed scenario). Pins
    # belong on the session's own working trace; an old session without one must
    # be re-captured (which builds edit.trace.jsonl) before pins can be applied.
    if not dry_run and not trace.resolve().is_relative_to(sess_dir.resolve()):
        raise SystemExit(
            f"trace_studio apply: refusing to edit {trace} (outside the session). "
            f"Re-capture this session to create its working trace, then apply.")

    edits = _load_jsonl(sess_dir / "edits.jsonl")
    state = {int(r["frame"]): r for r in _load_jsonl(sess_dir / "state.jsonl")}

    # ── collect pin ops (kind phasepin/rngpin) ──────────────────────────────
    pin_ops: list[dict] = []          # the JSON ops to insert
    worklist: list[str] = []

    def retail_rng_at(idx: int):
        r = state.get(idx, {}).get("retail", {})
        return r.get("rng")

    for e in edits:
        kind = e.get("kind")
        idx = int(e.get("frame", 0))
        seg = cr_start + idx
        note = e.get("note", "")
        if kind == "phasepin":
            pin_ops.append({"phasepin": seg})
        elif kind == "rngpin":
            val = e.get("value")
            if val is None:
                val = retail_rng_at(idx)
            if val is None:
                val = CANON_SEED
            pin_ops.append({"rngseed": [seg, int(val)]})
        elif kind in ("anchor", "feature", "note"):
            st = state.get(idx, {})
            ctx = []
            for side in ("retail", "port"):
                s = st.get(side, {})
                bits = {k: s[k] for k in ("db054", "rng", "rngcalls",
                                          "poct", "coct", "anim") if k in s}
                if bits:
                    ctx.append(f"{side}={bits}")
            box = e.get("box")
            worklist.append(
                f"- **{kind}** @ viewer frame {idx} (seg frame {seg})"
                + (f" — {note}" if note else "")
                + (f"\n    crop: `crop id={manifest['session']} "
                   f"box={','.join(map(str, box))} frame=f={idx}`" if box else "")
                + ("\n    state: " + " · ".join(ctx) if ctx else ""))

    # ── auto-pin from the stored verdict ────────────────────────────────────
    if auto_pin:
        vt = (manifest.get("verdict") or {}).get("text", "")
        if "CONST-OFFSET" in vt and not any("phasepin" in p for p in pin_ops):
            pin_ops.append({"phasepin": cr_start})
            worklist.append(f"- **auto-pin** db054 CONST-OFFSET → added "
                            f"{{phasepin: {cr_start}}} (window start)")
        if "DESYNC" in vt and not any("rngseed" in p for p in pin_ops):
            pin_ops.append({"rngseed": [cr_start, CANON_SEED]})
            worklist.append(f"- **auto-pin** rngcalls DESYNC → added "
                            f"{{rngseed: [{cr_start}, {CANON_SEED}]}}")

    # ── insert pins into the trace (final segment, after the last {wait}) ───
    lines = trace.read_text().splitlines()
    existing = set(json.dumps(json.loads(ln)) for ln in lines
                   if ln.strip() and not ln.strip().startswith("#")
                   and _is_json(ln))
    new_lines = [json.dumps(op) for op in pin_ops
                 if json.dumps(op) not in existing]
    n_dupe = len(pin_ops) - len(new_lines)

    if new_lines:
        wi = _last_wait_idx(lines)
        at = wi + 1 if wi >= 0 else 0
        lines = lines[:at] + new_lines + lines[at:]

    # ── report ──────────────────────────────────────────────────────────────
    print(f"trace_studio apply: session={manifest['session']}  trace={trace}")
    print(f"  pins: {len(new_lines)} new"
          + (f", {n_dupe} already present" if n_dupe else ""))
    for nl in new_lines:
        print(f"    + {nl}")
    if not new_lines and not worklist:
        print("  (no edits.jsonl marks — nothing to do)")

    if new_lines and not dry_run:
        trace.write_text("\n".join(lines) + "\n")
        print(f"  wrote {trace}")
        # Clear the pin marks we just applied (they now live in the trace) so the
        # self-service loop doesn't re-stack them; keep anchor/feature/note for Claude.
        kept = [e for e in edits if e.get("kind") not in ("phasepin", "rngpin")]
        (sess_dir / "edits.jsonl").write_text(
            "".join(json.dumps(e) + "\n" for e in kept))
    elif new_lines and dry_run:
        print("  (dry-run: trace NOT written)")

    # ── worklist.md for Claude ──────────────────────────────────────────────
    if worklist:
        body = (f"# Trace-studio worklist — {manifest['session']}\n\n"
                f"Trace: `{trace}`  ·  caprange start {cr_start}\n\n"
                + "\n".join(worklist) + "\n")
        out = sess_dir / "worklist.md"
        if not dry_run:
            out.write_text(body)
        print(f"\n  worklist ({len(worklist)} item(s)) → {out}:")
        print("\n".join("    " + l for l in body.splitlines()))
    return {"ok": True, "pins_added": len(new_lines), "pins_dupe": n_dupe,
            "worklist_items": len(worklist), "trace": str(trace),
            "dry_run": dry_run}


def _is_json(ln: str) -> bool:
    try:
        json.loads(ln.strip())
        return True
    except json.JSONDecodeError:
        return False
