#!/usr/bin/env python3
"""tools/parity/state_producer.py — the `state` (volatile) pillar PRODUCER.

The state pillar proves the VOLATILE-deterministic state class (canonical-state.md
class 2): the once-per-frame engine state — rng/rngcalls, player/companion actors,
phase counters, interaction + customer-service machines, dialogue, title menu —
is bit-identical port↔retail at every required logical frame. It is the per-frame
sibling of the (scenario-scoped, persistent) save pillar.

Evidence source, no new capture tooling: a Trace Studio v3 `orv3_window … --state`
drive bakes each side's call_trace.jsonl STATE_VA fields into `view.json` as a
per-frame `state: {port:{…}, retail:{…}}` block, identity-keyed (the same join the
render_program pillar reads). This producer BRIDGES that view into a normalized
`state-metrics.json`: for every required paired frame it builds each side's
canonical volatile-state tree (state_codec), Merkle-hashes it (state_merkle), and
records the root pair + — on a mismatch — the first divergent (subsystem, field)
leaf.

Pure core (`compare_states`) + a view bridge (`from_view_json`), mirroring
render_program so the truth-defining comparison is testable with no capture.

FAIL CLOSED: a view without `--state` (has_state false) yields zero comparable
frames ⇒ the adapter reports NOT_CAPTURED; a required paired frame missing
both-sided state is omitted ⇒ NOT_CAPTURED (never a silent PASS)."""
from __future__ import annotations

from pathlib import Path
from typing import Optional

from .observations import (
    OBS_SCHEMA_VERSION,
    LogicalFrame,
    ObservationError,
    load_json,
)
from .state_codec import StateCodecError, StateSchema, build_tree
from .state_merkle import first_divergent_leaf, merkle_root


class StateProducerError(Exception):
    """A fatal producer condition (a Merkle invariant broke). Exit 2 at the CLI."""


def _compare_frame(port_fields: dict, retail_fields: dict, schema: StateSchema) -> dict:
    """Two captured {field: value} dicts -> one state-metrics frame row (roots +
    identical + first divergence). Retail is the ground truth (b), port the subject
    (a) — the divergence records both raw values."""
    pt = build_tree(port_fields, schema)
    rt = build_tree(retail_fields, schema)
    pr, rr = merkle_root(pt, schema), merkle_root(rt, schema)
    row = {"port_root": pr, "retail_root": rr, "identical": pr == rr}
    if pr != rr:
        diff = first_divergent_leaf(pt, rt, schema)
        if diff is None:   # roots differ ⇒ a leaf MUST differ (Merkle invariant)
            raise StateProducerError(
                "state Merkle roots differ but no divergent leaf found — codec/merkle bug")
        row["divergence"] = {
            "path": diff.path,
            "port_value": diff.a_value, "retail_value": diff.b_value,
            "port_present": diff.a_present, "retail_present": diff.b_present,
        }
    return row


def compare_states(paired_by_label: dict, schema: StateSchema,
                   required: list[LogicalFrame], *,
                   source: Optional[dict] = None, has_state: bool = True) -> dict:
    """Build the normalized `state-metrics.json` doc from the view's per-label
    state blocks, scoped + ordered to `required` (the contract's in-window paired
    frames). A required frame absent from `paired_by_label` (no both-sided state)
    is omitted ⇒ the adapter's coverage check reports NOT_CAPTURED. If `required`
    is None, every both-sided frame is emitted in the map's order."""
    frames = []
    if required is None:
        for label, st in paired_by_label.items():
            lf = LogicalFrame.from_label(label)
            row = {"key": list(lf)}
            row.update(_compare_frame(st["port"], st["retail"], schema))
            frames.append(row)
    else:
        for lf in required:
            st = paired_by_label.get(lf.label())
            if not st:
                continue
            row = {"key": list(lf)}
            row.update(_compare_frame(st["port"], st["retail"], schema))
            frames.append(row)
    doc = {
        "schema_version": OBS_SCHEMA_VERSION,
        "pillar": "state",
        "state_schema_version": schema.schema_version,
        "has_state": bool(has_state),
        "frames": frames,
    }
    if source:
        doc["source"] = source
    return doc


def paired_state_from_view(view_doc: dict) -> tuple[dict, bool]:
    """Extract the identity-keyed both-sided state from a loaded `view.json` →
    `({label: {"port": {…}, "retail": {…}}}, has_state)`. Only frames whose `state`
    block carries BOTH sides are kept; a one-sided/absent block is dropped (⇒ that
    required frame is uncovered ⇒ NOT_CAPTURED downstream, fail closed). Shared by
    the producer (from_view_json) and the ST-04 report (state_diff), so both read the
    view identically. Trust `has_state` (bool(state_rows) at bake): a drive without
    --state captured no engine state — nothing to compare regardless of stray blocks."""
    has_state = bool(view_doc.get("has_state"))
    paired: dict = {}
    if has_state:
        for fr in view_doc.get("frames") or []:
            st = fr.get("state")
            if not isinstance(st, dict):
                continue
            p, r = st.get("port"), st.get("retail")
            if isinstance(p, dict) and isinstance(r, dict):
                paired[fr["label"]] = {"port": p, "retail": r}
    return paired, has_state


def from_view_json(view_path, *, required=None, schema: Optional[StateSchema] = None,
                   source: Optional[dict] = None) -> dict:
    """Bridge a Trace Studio v3 `view.json` (a `--state` drive) into the state
    metrics doc `adapt_state` consumes. Only frames whose `state` block carries
    BOTH sides are comparable; a one-sided/absent block is dropped (⇒ that required
    frame is uncovered ⇒ NOT_CAPTURED, fail closed). `source` (opt-in) records the
    drive's container hashes so the doc binds to the exact view (the state was
    captured in the same drive as the d3d containers)."""
    v = load_json(view_path)
    if v is None:
        raise ObservationError(f"no view.json at {view_path}")
    schema = schema or StateSchema.load()
    paired, has_state = paired_state_from_view(v)
    try:
        return compare_states(paired, schema, required, source=source, has_state=has_state)
    except StateCodecError as exc:            # a value/type the schema can't encode
        raise ObservationError(str(exc))


# ── driver (standalone; parity_prove bridges inline like render_program) ──────

def produce_from_view(view_path, out_path, *, required=None,
                      schema: Optional[StateSchema] = None,
                      source: Optional[dict] = None) -> tuple[dict, Path]:
    """Bridge a view.json → write `out_path` (state-metrics.json). Returns (doc, path)."""
    doc = from_view_json(view_path, required=required, schema=schema, source=source)
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    import json
    out_path.write_text(json.dumps(doc))
    return doc, out_path
