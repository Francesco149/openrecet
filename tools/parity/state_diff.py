#!/usr/bin/env python3
"""tools/parity/state_diff.py — ST-04 first-divergence state report.

Given a Trace Studio v3 `--state` window's per-frame volatile state (the same
identity-keyed `{label: {port, retail}}` the ST-03 producer bridges), localize the
FIRST logical frame whose volatile-state Merkle root diverges port↔retail and emit
a full diagnostic (roadmap §7 ST-04 output):

  * the first divergent LOGICAL FRAME (anchor/occurrence/offset — never absolute);
  * the divergent leaf ROOT PATH (subsystem/field) + its schema TYPE;
  * TYPED VALUES + RAW BITS (the canonical encoder bytes) on each side;
  * the LAST MATCHING FRAME (the newest frame whose roots still matched);
  * the value TRANSITION across that boundary — state-derivable mutation provenance:
    what the field held at the last matching frame vs now, each side, and whether the
    port MISSED a change retail made, made a SPURIOUS one, or applied a WRONG one;
  * every CO-DIVERGENT leaf at that frame (the full extent of the corruption, not
    just the top-priority leaf).

This is the DIAGNOSTIC sibling of the `state` pillar ADAPTER (state.py): the adapter
answers "is the pillar PASS/FAIL for the proof bundle?"; this answers "given a FAIL,
exactly what first went wrong and how." Both consume the ST-03 evidence and reuse the
ST-02 codec+Merkle; neither invents a pass (fail closed — see the verdict map). The
adapter stays AUTHORITATIVE for the proof bundle; this tool is the standalone drill-in.

The roadmap's "nearby mutation/CALL provenance" is provided at the strength derivable
from STATE ALONE: the value transition pins WHAT changed and in which direction. The
CALLSITE/owner of that write is ST-05's domain (mutation capture); the report leaves a
`provenance: null` seam ST-05 fills without touching this contract.

Verdict map (roadmap §4.1) — a standalone diagnostic verdict, matching adapt_state on
the cases both see:
  * no required frames                      → INCONCLUSIVE   (exit 2)
  * has_state false                         → NOT_CAPTURED   (exit 2)
  * a required frame uncovered (no state)   → NOT_CAPTURED   (exit 2)  [+divergence if any before the gap]
  * every required frame's roots identical  → PASS           (exit 0)
  * first non-identical frame localized     → FAIL           (exit 1)
"""
from __future__ import annotations

from pathlib import Path
from typing import Optional

from .observations import (
    FAIL,
    INCONCLUSIVE,
    NOT_CAPTURED,
    PASS,
    LogicalFrame,
    ObservationError,
    load_json,
)
from .state_codec import StateCodecError, StateSchema, build_tree, encode_value
from .state_merkle import LeafDiff, all_divergent_leaves, merkle_root
from .state_producer import paired_state_from_view


def _bits_hex(schema: StateSchema, sub: str, field: str, value, present: bool) -> Optional[str]:
    """The canonical encoder bytes of a leaf value as hex (the RAW BITS the Merkle
    root actually hashed), or None when the field is absent this frame."""
    if not present:
        return None
    typ = schema.field_type(sub, field)
    return encode_value(typ, value).hex()


def _leaf_report(schema: StateSchema, d: LeafDiff) -> dict:
    """One divergent leaf → a typed, bit-level diagnostic row."""
    sub, field = d.path.split("/", 1)
    return {
        "path": d.path,
        "subsystem": sub,
        "field": field,
        "type": schema.field_type(sub, field),
        "port_value": d.a_value,
        "retail_value": d.b_value,
        "port_present": d.a_present,
        "retail_present": d.b_present,
        "port_bits_hex": _bits_hex(schema, sub, field, d.a_value, d.a_present),
        "retail_bits_hex": _bits_hex(schema, sub, field, d.b_value, d.b_present),
    }


def _transition(schema: StateSchema, primary: LeafDiff, lm_frame: LogicalFrame,
                lm_port_tree, lm_retail_tree) -> dict:
    """The value TRANSITION of the primary leaf across the last-matching → divergent
    boundary. The last matching frame had EQUAL roots ⇒ both sides held the same
    value there (Merkle), so `prev` is unambiguous — the report says whether the port
    MISSED retail's change, made a SPURIOUS one, or a WRONG one."""
    sub, field = primary.path.split("/", 1)
    prev = (lm_port_tree.get(sub) or {}).get(field)   # == retail's at a matching frame
    prev_present = prev is not None
    prev_value = prev[1] if prev_present else None
    prev_canon = prev[2] if prev_present else None

    port_now = _bits_hex(schema, sub, field, primary.a_value, primary.a_present)
    retail_now = _bits_hex(schema, sub, field, primary.b_value, primary.b_present)
    prev_hex = prev_canon.hex() if prev_present else None
    port_changed = (primary.a_present != prev_present) or (port_now != prev_hex)
    retail_changed = (primary.b_present != prev_present) or (retail_now != prev_hex)

    if retail_changed and not port_changed:
        interp = f"port did NOT apply retail's change to {primary.path}"
    elif port_changed and not retail_changed:
        interp = f"port made a change to {primary.path} that retail did not"
    elif port_changed and retail_changed:
        interp = f"port and retail both changed {primary.path}, to different values"
    else:  # neither changed yet they differ — impossible after an equal-root frame
        interp = f"{primary.path} differs though neither side changed it since the last match"
    return {
        "last_matching_frame": lm_frame.as_dict(),
        "prev_value": prev_value,
        "prev_present": prev_present,
        "prev_bits_hex": prev_hex,
        "port": {"changed": port_changed, "to": primary.a_value, "present": primary.a_present},
        "retail": {"changed": retail_changed, "to": primary.b_value, "present": primary.b_present},
        "interpretation": interp,
    }


def build_report(paired_by_label: dict, schema: StateSchema,
                 required: list[LogicalFrame], *, has_state: bool = True) -> dict:
    """Walk `required` in order over the paired per-frame state; localize the first
    root divergence into the full ST-04 diagnostic. Pure (no I/O): testable on
    synthetic paired dicts. See the module docstring for the returned shape + verdict.

    Raises ObservationError if a field value can't be canonically encoded (a codec
    fault ⇒ INCONCLUSIVE at the CLI boundary, never a silent pass)."""
    n_required = len(required)
    # coverage: every required frame must have both-sided state to be comparable.
    first_uncovered: Optional[LogicalFrame] = None
    covered = 0
    for lf in required:
        if lf.label() in paired_by_label:
            covered += 1
        elif first_uncovered is None:
            first_uncovered = lf

    report: dict = {
        "pillar": "state",
        "has_state": bool(has_state),
        "state_schema_version": schema.schema_version,
        "n_required": n_required,
        "n_covered": covered,
        "coverage_complete": covered == n_required and n_required > 0,
        "first_uncovered_frame": first_uncovered.as_dict() if first_uncovered else None,
        "n_identical": 0,
        "last_matching_frame": None,
        "first_divergence": None,
    }

    try:
        n_identical = 0
        last_matching: Optional[LogicalFrame] = None
        lm_port_tree = lm_retail_tree = None
        divergence = None
        for lf in required:
            st = paired_by_label.get(lf.label())
            if st is None:
                continue  # uncovered — recorded above; can't compare this frame
            pt = build_tree(st["port"], schema)
            rt = build_tree(st["retail"], schema)
            if merkle_root(pt, schema) == merkle_root(rt, schema):
                n_identical += 1
                last_matching, lm_port_tree, lm_retail_tree = lf, pt, rt
                continue
            # first divergent covered frame — build the rich payload and stop.
            leaves = all_divergent_leaves(pt, rt, schema)
            primary = leaves[0]
            fd = {
                "logical_frame": lf.as_dict(),
                **_leaf_report(schema, primary),
                "n_divergent_leaves": len(leaves),
                "divergent_leaves": [_leaf_report(schema, d) for d in leaves],
                "transition": (
                    _transition(schema, primary, last_matching, lm_port_tree, lm_retail_tree)
                    if last_matching is not None else None),
                "provenance": None,   # ST-05 mutation-capture seam
            }
            divergence = fd
            break
        report["n_identical"] = n_identical
        report["last_matching_frame"] = last_matching.as_dict() if last_matching else None
        report["first_divergence"] = divergence
    except StateCodecError as exc:
        raise ObservationError(str(exc))

    # verdict (fail closed) — a divergence is a real disproof even under a coverage
    # gap, so surface it; but an incomplete/absent capture can never read PASS.
    if n_required == 0:
        verdict = INCONCLUSIVE
    elif not has_state:
        verdict = NOT_CAPTURED
    elif report["first_divergence"] is not None:
        verdict = FAIL
    elif not report["coverage_complete"]:
        verdict = NOT_CAPTURED
    else:
        verdict = PASS
    report["verdict"] = verdict
    return report


def ordered_frames_from_view(view_doc: dict) -> list[LogicalFrame]:
    """The ordered both-sided logical frames a `--state` view holds (view order = the
    join order) — the `required` list an ST-05 provenance attach needs when no contract
    window scopes it."""
    paired, _ = paired_state_from_view(view_doc)
    return [LogicalFrame.from_label(lbl) for lbl in paired]


def report_from_view_json(view_path, *, required=None,
                          schema: Optional[StateSchema] = None) -> dict:
    """Bridge a Trace Studio v3 `--state` `view.json` → the ST-04 report. When
    `required` is None every both-sided frame is walked in view order (a whole-window
    scan); pass the contract's in-window paired frames to scope it."""
    v = load_json(view_path)
    if v is None:
        raise ObservationError(f"no view.json at {view_path}")
    schema = schema or StateSchema.load()
    paired, has_state = paired_state_from_view(v)
    req = required if required is not None else [
        LogicalFrame.from_label(lbl) for lbl in paired]
    return build_report(paired, schema, req, has_state=has_state)


def render_text(report: dict) -> str:
    """A short human summary of the report (the CLI's non-JSON output)."""
    v = report["verdict"]
    lines = [f"state-diff: {v}  "
             f"({report['n_identical']}/{report['n_covered']} covered frames identical; "
             f"{report['n_required']} required)"]
    if not report["coverage_complete"] and report["first_uncovered_frame"]:
        u = report["first_uncovered_frame"]
        lines.append(f"  first uncovered frame: {u['anchor']}#{u['occurrence']}+{u['offset']}")
    fd = report.get("first_divergence")
    if fd:
        lf = fd["logical_frame"]
        lines.append(f"  first divergence @ {lf['anchor']}#{lf['occurrence']}+{lf['offset']}"
                     f"  {fd['path']}  ({fd['type']})")
        lines.append(f"    retail={fd['retail_value']!r}  port={fd['port_value']!r}")
        lines.append(f"    retail_bits={fd['retail_bits_hex']}  port_bits={fd['port_bits_hex']}")
        if fd["n_divergent_leaves"] > 1:
            others = ", ".join(d["path"] for d in fd["divergent_leaves"][1:])
            lines.append(f"    +{fd['n_divergent_leaves'] - 1} co-divergent: {others}")
        tr = fd.get("transition")
        if tr:
            lm = tr["last_matching_frame"]
            lines.append(f"    last match @ {lm['anchor']}#{lm['occurrence']}+{lm['offset']}"
                         f" (was {tr['prev_value']!r}) — {tr['interpretation']}")
        else:
            lines.append("    (no last matching frame — divergence at the window head)")
    return "\n".join(lines)
