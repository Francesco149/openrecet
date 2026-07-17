#!/usr/bin/env python3
"""tools/parity/state_mutation.py — ST-05 semantic-mutation CONSUMER.

A mutation is a single named WRITE to a canonical-state field (schema
docs/schemas/state-mutation-v1.json). Where the state pillar (state-volatile-v1.json)
compares the once-per-frame state VALUE and the save pillar the on-disk bytes, a
mutation STREAM is the causal layer beneath both: it answers "which WRITE first
diverged, and WHO wrote it?" — the provenance ST-04's first-divergence report leaves
`null`. This module is the CONSUMER; per roadmap rule 11 (build consumers before
platforms) it lands BEFORE the Frida post-write / TTD capture platform, defining the
event shape that platform must emit. R3 design + rationale: reference/state-mutations.md.

Three consumer duties (roadmap §7 ST-05 acceptance):

  * RECONSTRUCT a selected subtree — replay the compared classes in join order,
    last-write-wins per path ⇒ the subtree's value at a frame; verified against the
    captured per-frame state (a completeness check on the stream).
  * DEDUP — a post-write hook that fires twice (or a batched + per-write observation
    of one store) must not double-apply; the dedup key is (logical_frame, seq, path).
  * FIRST WRONG WRITE ≤ FIRST STATE-ROOT DIVERGENCE — the ordering invariant that
    LINKS ST-05 to ST-04: if the stream is complete for a field, the first write whose
    CUMULATIVE value diverges port↔retail precedes-or-equals the field's state-root
    divergence frame. A wrong write found AFTER the state already diverged means the
    stream missed the causal write ⇒ INCONCLUSIVE, never a pass.

Fail closed: an unknown class/type, a non-encodable value, a foreign frame, or a
conflicting double-observation RAISES (ObservationError ⇒ INCONCLUSIVE at a boundary).
"""
from __future__ import annotations

from typing import NamedTuple, Optional

from .observations import (
    LogicalFrame,
    ObservationError,
)
from .state_codec import StateCodecError, encode_value

MUTATION_SCHEMA_VERSION = 1

# the R3 class gate (state-mutation-v1.json `classes`).
SEMANTIC, DERIVED, NOISE = "semantic", "derived", "noise"
_CLASSES = {SEMANTIC, DERIVED, NOISE}
COMPARED = frozenset({SEMANTIC, DERIVED})   # noise is excluded from every comparison
_TYPES = {"i32", "u32", "hex", "f32"}


class Mutation(NamedTuple):
    """One named write. `canon` = the canonical bytes of `new` (state_codec) — the
    same encoding the state/save roots hash, so a mutation and a state divergence
    compare in one vocabulary. `old_canon` is None when `old` was unobserved."""

    frame: LogicalFrame
    seq: int
    path: str
    cls: str
    type: str
    old: object
    new: object
    owner_va: Optional[str]
    callsite_va: Optional[str]
    canon: bytes
    old_canon: Optional[bytes]

    def as_write(self) -> dict:
        """The compact write record embedded in a first-wrong-write / provenance report."""
        return {"old": self.old, "new": self.new, "seq": self.seq,
                "owner_va": self.owner_va, "callsite_va": self.callsite_va}


def parse_mutation(ev: dict) -> Mutation:
    """One raw event dict → a validated Mutation. Fail closed on shape/class/type."""
    if not isinstance(ev, dict):
        raise ObservationError(f"mutation is not an object: {ev!r}")
    frame = LogicalFrame.from_key(ev.get("logical_frame"))
    seq = ev.get("seq", 0)
    if isinstance(seq, bool) or not isinstance(seq, int) or seq < 0:
        raise ObservationError(f"mutation seq must be an int >= 0: {ev!r}")
    path = ev.get("path")
    if not isinstance(path, str) or "/" not in path:
        raise ObservationError(f"mutation path must be a canonical-state 'scope/field': {path!r}")
    cls = ev.get("class")
    if cls not in _CLASSES:
        raise ObservationError(f"mutation class must be one of {sorted(_CLASSES)}: {cls!r}")
    typ = ev.get("type")
    if typ not in _TYPES:
        raise ObservationError(f"mutation type must be one of {sorted(_TYPES)}: {typ!r}")
    if "new" not in ev:
        raise ObservationError(f"mutation missing 'new': {ev!r}")
    try:
        canon = encode_value(typ, ev["new"])
        old = ev.get("old")
        old_canon = encode_value(typ, old) if old is not None else None
    except StateCodecError as exc:
        raise ObservationError(str(exc))
    return Mutation(frame, seq, path, cls, typ, old, ev["new"],
                    ev.get("owner_va"), ev.get("callsite_va"), canon, old_canon)


def load_stream(doc: dict) -> tuple[str, list[Mutation]]:
    """A `state-mutation.json` stream doc → (side, [Mutation]). Validates the doc
    major + each event. `side` is 'port'|'retail' when declared (advisory)."""
    if not isinstance(doc, dict):
        raise ObservationError("mutation stream is not an object")
    sv = doc.get("schema_version")
    if sv != MUTATION_SCHEMA_VERSION:
        raise ObservationError(
            f"mutation stream schema_version {sv!r} != {MUTATION_SCHEMA_VERSION}")
    side = doc.get("side")
    if side is not None and side not in ("port", "retail"):
        raise ObservationError(f"mutation stream side must be port|retail|absent: {side!r}")
    muts = [parse_mutation(ev) for ev in (doc.get("mutations") or [])]
    return side, dedup(muts)


# ── ordering + dedup ─────────────────────────────────────────────────────────

def _order_index(required: list[LogicalFrame]) -> dict:
    return {lf: i for i, lf in enumerate(required)}


def dedup(muts: list[Mutation]) -> list[Mutation]:
    """Collapse identical re-observations of one write (a hook firing twice) by
    (frame, seq, path). A CONFLICTING double-observation — same key, different `new`
    — is a capture fault ⇒ ObservationError (never silently pick one)."""
    seen: dict = {}
    out: list[Mutation] = []
    for m in muts:
        key = (m.frame, m.seq, m.path)
        prev = seen.get(key)
        if prev is None:
            seen[key] = m
            out.append(m)
        elif prev.canon != m.canon:
            raise ObservationError(
                f"conflicting double-observation of {m.path} @ {m.frame.label()} "
                f"seq {m.seq}: {prev.new!r} vs {m.new!r}")
        # identical re-observation ⇒ drop (idempotent)
    return out


def _sorted(muts: list[Mutation], idx: dict) -> list[Mutation]:
    """Mutations in JOIN order then intra-frame seq. A frame outside the join is a
    foreign event ⇒ ObservationError (the stream isn't this window's capture)."""
    for m in muts:
        if m.frame not in idx:
            raise ObservationError(
                f"mutation references a frame outside the join: {m.frame.label()}")
    return sorted(muts, key=lambda m: (idx[m.frame], m.seq))


# ── reconstruction ────────────────────────────────────────────────────────────

def reconstruct_subtree(muts: list[Mutation], required: list[LogicalFrame], prefix: str,
                        *, up_to: Optional[LogicalFrame] = None,
                        classes=COMPARED) -> dict:
    """Replay the compared-class writes under `prefix` in join order (up to and
    including `up_to`, or the whole window) ⇒ `{path: Mutation}` = the subtree's
    value at that frame (last-write-wins ⇒ idempotent). Only paths WRITTEN in-window
    appear (a never-written field holds its window-start value, unknowable from the
    stream alone — correctly absent)."""
    idx = _order_index(required)
    cutoff = idx[up_to] if up_to is not None else len(required)
    cur: dict = {}
    for m in _sorted(muts, idx):
        if idx[m.frame] > cutoff or m.cls not in classes:
            continue
        if m.path.startswith(prefix):
            cur[m.path] = m
    return cur


def verify_reconstruction(reconstructed: dict, captured_fields: dict, schema, prefix: str):
    """Cross-check a reconstructed VOLATILE subtree against the state pillar's captured
    per-frame fields: every reconstructed path's `new` bytes must equal the captured
    value's bytes (the stream is complete for that field). Returns (ok, mismatches).
    `prefix` is 'subsystem/' — `captured_fields` is the flat {field: value} dict the
    state producer consumes; the field name is the path tail."""
    mismatches = []
    for path, m in reconstructed.items():
        sub, _, field = path.partition("/")
        typ = schema.field_type(sub, field)
        if typ is None or field not in captured_fields:
            mismatches.append({"path": path, "reason": "field absent from captured state"})
            continue
        try:
            cap_canon = encode_value(typ, captured_fields[field])
        except StateCodecError as exc:
            raise ObservationError(str(exc))
        if cap_canon != m.canon:
            mismatches.append({"path": path, "reconstructed": m.new,
                               "captured": captured_fields[field]})
    return (not mismatches), mismatches


# ── first wrong write (the ST-04/ST-05 link) ────────────────────────────────────

def _resolved(pv: Optional[Mutation], rv: Optional[Mutation]):
    """A path's canonical value on each side at a frame, given each side's LAST write
    to it (or None). An unwritten side holds the WINDOW-START value — shared across
    sides when the window opens from a matched state — recovered from the other side's
    write `old`. Returns (p_canon, r_canon) or None entries when undeterminable."""
    def side(mine: Optional[Mutation], other: Optional[Mutation]):
        if mine is not None:
            return mine.canon
        if other is not None and other.old_canon is not None:
            return other.old_canon   # unwritten here ⇒ == the shared start == other.old
        return None
    return side(pv, rv), side(rv, pv)


def first_wrong_write(port_muts: list[Mutation], retail_muts: list[Mutation],
                      required: list[LogicalFrame], *, classes=COMPARED) -> Optional[dict]:
    """The first (logical_frame, path) at which the port's CUMULATIVE value of a
    canonical-state field diverges from retail's, walking frames in join order. Because
    unwritten paths hold the shared window-start (recovered via a write's `old`), a
    one-sided write IS a real divergence (that side left the shared start). Returns the
    wrong-write report, or None if no compared write ever diverges."""
    idx = _order_index(required)
    ps = _sorted(port_muts, idx)
    rs = _sorted(retail_muts, idx)
    # per-frame-index write maps (last seq wins), compared classes only.
    def by_frame(ms):
        d: dict = {}
        for m in ms:
            if m.cls in classes:
                d.setdefault(idx[m.frame], {})[m.path] = m
        return d
    pf, rf = by_frame(ps), by_frame(rs)

    pcum: dict = {}   # path -> last port write
    rcum: dict = {}   # path -> last retail write
    for fi, lf in enumerate(required):
        touched = set()
        for path, m in pf.get(fi, {}).items():
            pcum[path] = m; touched.add(path)
        for path, m in rf.get(fi, {}).items():
            rcum[path] = m; touched.add(path)
        # a divergence can only APPEAR at a path written THIS frame (values are
        # cumulative; anything else was already equal or already flagged earlier).
        for path in sorted(touched):
            pv, rv = pcum.get(path), rcum.get(path)
            p_canon, r_canon = _resolved(pv, rv)
            if p_canon is None or r_canon is None:
                continue   # undeterminable (a write lacked `old`) — not a false positive
            if p_canon != r_canon:
                kind = "value" if (pv and rv) else ("port_extra" if pv else "port_missing")
                return {
                    "logical_frame": lf.as_dict(),
                    "path": path,
                    "kind": kind,
                    "port": pv.as_write() if pv else None,
                    "retail": rv.as_write() if rv else None,
                    "port_value": pv.new if pv else (rv.old if rv else None),
                    "retail_value": rv.new if rv else (pv.old if pv else None),
                }
    return None


def _lf(frame) -> Optional[LogicalFrame]:
    """Coerce a LogicalFrame or an as_dict()/from_key shape to a LogicalFrame."""
    if frame is None or isinstance(frame, LogicalFrame):
        return frame
    if isinstance(frame, dict):
        return LogicalFrame(frame["anchor"], frame["occurrence"], frame["offset"])
    return LogicalFrame.from_key(frame)


def check_ordering(first_wrong: Optional[dict],
                   root_divergence_frame, required: list[LogicalFrame]) -> dict:
    """The ST-04/ST-05 ordering invariant: the first wrong WRITE must precede-or-equal
    the first state-ROOT divergence (ST-04). Returns {ok, detail, ...}. NOT ok (⇒ the
    caller reports INCONCLUSIVE) when the state diverged but no wrong write was found
    (the stream MISSED the causal write) or the wrong write is AFTER the divergence."""
    idx = _order_index(required)
    root_lf = _lf(root_divergence_frame)
    if root_lf is None:
        # state never diverged: a wrong write would contradict a complete stream.
        if first_wrong is None:
            return {"ok": True, "detail": "no state divergence and no wrong write"}
        return {"ok": False, "detail": "a wrong write exists but the state root never "
                "diverged — stream over-reports (INCONCLUSIVE)"}
    di = idx.get(root_lf)
    if first_wrong is None:
        return {"ok": False, "divergence_index": di,
                "detail": "state diverged but no causal write captured — stream "
                          "incomplete (INCONCLUSIVE)"}
    wi = idx.get(_lf(first_wrong["logical_frame"]))
    ok = wi is not None and di is not None and wi <= di
    return {"ok": ok, "wrong_write_index": wi, "divergence_index": di,
            "detail": ("first wrong write precedes-or-equals the state divergence"
                       if ok else "first wrong write is AFTER the state divergence — "
                       "stream missed the causal write (INCONCLUSIVE)")}


# ── ST-04 provenance seam-fill ──────────────────────────────────────────────────

def attach_provenance(st04_report: dict, port_muts: list[Mutation],
                      retail_muts: list[Mutation], required: list[LogicalFrame]) -> dict:
    """Fill ST-04's `first_divergence.provenance` (the WRITER behind the divergent
    leaf) from the mutation streams, and attach the ordering-invariant check. Mutates
    and returns the report. A no-op if the report has no first_divergence."""
    fd = st04_report.get("first_divergence")
    fw = first_wrong_write(port_muts, retail_muts, required)
    root_lf = LogicalFrame(**fd["logical_frame"]) if fd else None
    st04_report["mutation_ordering"] = check_ordering(fw, root_lf, required)
    if not fd:
        return st04_report
    # prefer the wrong write to the SAME path as the divergent leaf; else the first
    # wrong write at/before the divergence frame.
    prov = None
    if fw is not None:
        writer = fw["port"] or fw["retail"]
        if fw["path"] == fd["path"] or fw["port"] or fw["retail"]:
            prov = {"owner_va": (writer or {}).get("owner_va"),
                    "callsite_va": (writer or {}).get("callsite_va"),
                    "path": fw["path"], "kind": fw["kind"],
                    "old": (writer or {}).get("old"), "new": (writer or {}).get("new"),
                    "seq": (writer or {}).get("seq"),
                    "same_leaf": fw["path"] == fd["path"]}
    fd["provenance"] = prov
    return st04_report
