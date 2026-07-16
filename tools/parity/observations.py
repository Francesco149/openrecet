#!/usr/bin/env python3
"""tools/parity/observations.py — EP-04 observation normalization + adjudication.

Turns the raw artifacts the porting loop already produces (the identity JOIN
`pairs.json`, the Trace Studio v3 `view.json` draw report, a normalized pixel/
draw metrics doc) into the two schema-shaped objects a proof bundle needs
(docs/schemas/parity-proof-v1.schema.json): an `observation` (raw evidence per
pillar) and its adjudicated `pillar_result` (PASS/FAIL/NOT_CAPTURED/INCONCLUSIVE).

The truth-defining core of the roadmap's Wave-0. Three rules it must never break
(roadmap §3, §4.1; EP-04 acceptance):

  * FAIL CLOSED — an ABSENT evidence file is NOT_CAPTURED, never PASS. A required
    paired frame with no measurement is NOT_CAPTURED. Nothing here invents a pass.
  * TRUST THE JOIN, NOT A FILENAME — before comparing, every adapter validates
    that the metrics cover EXACTLY the identity-join's required frames, in order.
    A foreign frame, a reordered/duplicated stream, or a source-container hash that
    doesn't match the join is INCONCLUSIVE (the artifact is a different/stale
    capture — tool exit 2), distinct from a real disproof (FAIL).
  * ONE WORD ONE MEANING — the producer verdict tokens (draw ALIGNED/BATCHING/
    DIVERGENT, join JOIN_COMPLETE/JOIN_PARTIAL) map to a scoped PILLAR verdict per
    docs/reference/parity-vocabulary.md; the raw token is kept as detail.

`observations.py` owns the shared foundation + the `identity` adapter (it reads
pairs.json, which is the source of the required-frame set every other adapter
joins against). The concrete `pixels`/`render_program` adapters live in their own
modules (pixels.py, render_program.py) and import from here.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Callable, NamedTuple, Optional

from .fingerprint import FingerprintError, sha256_file

# Normalized metrics/observation contract major. A metrics doc with a different
# schema_version is an unknown major ⇒ rejected (roadmap §3 rule 7).
OBS_SCHEMA_VERSION = 1

# Pillar verdicts — the frozen §4.1 vocabulary, re-exported so adapters don't
# hard-code strings.
PASS = "PASS"
FAIL = "FAIL"
NOT_CAPTURED = "NOT_CAPTURED"
NOT_REQUIRED = "NOT_REQUIRED"
INCONCLUSIVE = "INCONCLUSIVE"


class ObservationError(Exception):
    """The evidence is PRESENT but cannot be trusted — corrupt JSON, a malformed
    frame key, a reordered/duplicated frame stream, a frame outside the join, or a
    source-container hash that doesn't match the capture the join came from.

    Maps to an INCONCLUSIVE pillar (tool exit 2, roadmap §4.1) — deliberately
    distinct from a simply-absent file (NOT_CAPTURED) and from a real disproof
    (FAIL). Raised inside an adapter and converted to INCONCLUSIVE at its boundary,
    so an adapter is total: it always returns an AdapterResult."""


# ── logical frame identity (roadmap §4.2) ────────────────────────────────────

class LogicalFrame(NamedTuple):
    """The canonical join key `(anchor, occurrence, offset)`. Never an absolute
    present index. Hashable ⇒ usable directly in sets/dicts for the frame-set
    checks below."""

    anchor: str
    occurrence: int
    offset: int

    @staticmethod
    def from_key(key) -> "LogicalFrame":
        """Parse a pairs.json / metrics `key` = [anchor, occ, offset]. Strict:
        bool is not an int here (JSON true/false must not masquerade as a count)."""
        if not isinstance(key, (list, tuple)) or len(key) != 3:
            raise ObservationError(f"malformed frame key (want [anchor,occ,offset]): {key!r}")
        anchor, occ, off = key
        if not isinstance(anchor, str) or not anchor:
            raise ObservationError(f"frame key anchor must be a non-empty string: {key!r}")
        if isinstance(occ, bool) or not isinstance(occ, int) or occ < 1:
            raise ObservationError(f"frame key occurrence must be an int >= 1: {key!r}")
        if isinstance(off, bool) or not isinstance(off, int):
            raise ObservationError(f"frame key offset must be an int: {key!r}")
        return LogicalFrame(anchor, occ, off)

    @staticmethod
    def from_label(label: str) -> "LogicalFrame":
        """Parse a view.json frame `label` = "ANCHOR#occ+offset" (offset may be
        negative, e.g. "PAUSE_OPEN#1+-3"). rsplit on the LAST '#' so an anchor
        name never has to be '#'-free by accident."""
        if not isinstance(label, str):
            raise ObservationError(f"frame label must be a string: {label!r}")
        try:
            anchor, rest = label.rsplit("#", 1)
            occ_s, off_s = rest.split("+", 1)
            return LogicalFrame.from_key([anchor, int(occ_s), int(off_s)])
        except (ValueError, ObservationError) as exc:
            raise ObservationError(
                f"malformed frame label (want ANCHOR#occ+offset): {label!r}") from exc

    def label(self) -> str:
        return f"{self.anchor}#{self.occurrence}+{self.offset}"

    def as_dict(self) -> dict:
        """The proof schema `logical_frame` shape (keys anchor/occurrence/offset)."""
        return {"anchor": self.anchor, "occurrence": self.occurrence, "offset": self.offset}


# ── loading + provenance ─────────────────────────────────────────────────────

def load_json(path) -> Optional[dict]:
    """Read+parse a JSON file, or return None if it is simply ABSENT (the
    fail-closed NOT_CAPTURED path). A present-but-unparseable file is a trust
    failure ⇒ ObservationError (INCONCLUSIVE)."""
    p = Path(path)
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ObservationError(f"cannot parse {p}: {exc}") from exc


def artifact_ref(path, role: str) -> dict:
    """Schema-shaped `artifact_ref` {sha256, role, bytes} for a produced file, so
    the proof bundle references evidence by hash only (never by a non-portable
    path — that lives in envelope.local_paths). Fail closed on an unreadable file."""
    p = Path(path)
    try:
        size = p.stat().st_size
    except OSError as exc:
        raise FingerprintError(f"cannot stat {p}: {exc}") from exc
    return {"sha256": sha256_file(p), "role": role, "bytes": size}


def verify_source_containers(metrics: dict, expected: Optional[dict]) -> None:
    """A metrics doc records the port/retail container hashes it was computed from
    (`source.{port,retail}_container_sha256`). When the caller supplies the join's
    real container hashes, they MUST match — otherwise the metrics came from a
    different/stale capture and cannot be trusted (INCONCLUSIVE).

    Opt-in: `expected=None` skips the check (fixtures without a capture). But once
    opted in, it is STRICT — a metrics doc that omits `source`, or omits a checked
    role, fails closed rather than passing unverified."""
    if not expected:
        return
    src = metrics.get("source")
    if not isinstance(src, dict):
        raise ObservationError("metrics omit 'source' container hashes; cannot verify provenance")
    for role, exp in expected.items():
        if exp is None:
            continue
        got = src.get(role)
        if got is None:
            raise ObservationError(f"metrics omit source.{role}; cannot verify provenance")
        if got != exp:
            raise ObservationError(
                f"metrics source.{role} {got[:12]}… != join container {exp[:12]}… "
                f"(stale/swapped capture)")


def require_metrics_schema(doc: dict, pillar_name: str) -> None:
    """A normalized metrics doc must declare the current major and (if it names a
    pillar) the right one — else it is the wrong/unknown artifact."""
    sv = doc.get("schema_version")
    if sv != OBS_SCHEMA_VERSION:
        raise ObservationError(
            f"metrics schema_version {sv!r} != {OBS_SCHEMA_VERSION} (unknown major)")
    pn = doc.get("pillar")
    if pn is not None and pn != pillar_name:
        raise ObservationError(f"metrics pillar {pn!r} != {pillar_name!r}")


# ── the identity join → required frame set ───────────────────────────────────

def _window_pred(window) -> Callable[[LogicalFrame], bool]:
    """`window` = None (no restriction) or (anchor, occurrence, off_lo, off_hi)."""
    if window is None:
        return lambda lf: True
    anchor, occ, lo, hi = window
    return lambda lf: lf.anchor == anchor and lf.occurrence == occ and lo <= lf.offset <= hi


def paired_frames(pairs_doc: dict) -> list[LogicalFrame]:
    """Ordered, de-duplicated logical frames a pairs.json actually joined. A
    duplicate key means a JOIN paired the same logical moment twice — a broken or
    tampered join ⇒ ObservationError (INCONCLUSIVE)."""
    pairs = pairs_doc.get("pairs")
    if not isinstance(pairs, list):
        raise ObservationError("pairs.json has no 'pairs' array")
    out: list[LogicalFrame] = []
    seen: set = set()
    for row in pairs:
        if not isinstance(row, dict):
            raise ObservationError(f"pairs.json row is not an object: {row!r}")
        lf = LogicalFrame.from_key(row.get("key"))
        if lf in seen:
            raise ObservationError(f"duplicate logical frame in join: {lf.label()}")
        seen.add(lf)
        out.append(lf)
    return out


def load_required(pairs_path, window=None) -> list[LogicalFrame]:
    """The frame set a windowed proof must compare = the in-window paired frames of
    the identity join. Raises ObservationError if pairs.json is absent/corrupt (the
    orchestrator turns that into an identity NOT_CAPTURED/INCONCLUSIVE and every
    downstream pillar into NOT_CAPTURED — there is nothing to compare)."""
    doc = load_json(pairs_path)
    if doc is None:
        raise ObservationError(f"no pairs.json at {pairs_path}")
    pred = _window_pred(window)
    return [lf for lf in paired_frames(doc) if pred(lf)]


def match_frames(metric_frames: list, required: list[LogicalFrame], pillar_name: str):
    """Enforce that a metrics doc covers EXACTLY the required frames, in order, and
    return the ordered [(LogicalFrame, metric_row)] to compare. Classifies a
    deviation per §4.1:

      * a required frame is absent from the metrics  → return (reason, None)   NOT_CAPTURED
      * a metric frame is outside the required set    → raise                   INCONCLUSIVE (foreign)
      * same set but a different order / a duplicate  → raise                   INCONCLUSIVE (reordered)

    Joining by exact ordered-equality (not a positional zip, not a set) is what
    makes "reordered or mismatched frame identities fail" (EP-04 acceptance) hold:
    a stale metrics file whose frames don't line up with THIS join can never be
    silently compared."""
    got = [LogicalFrame.from_key(m.get("key")) for m in metric_frames]
    if got == required:
        return None, list(zip(got, metric_frames))
    req_set, got_set = set(required), set(got)
    foreign = got_set - req_set
    if foreign:
        raise ObservationError(
            f"{pillar_name} metrics reference {len(foreign)} frame(s) outside the identity join, "
            f"e.g. {sorted(foreign)[0].label()}")
    missing = [lf for lf in required if lf not in got_set]
    if missing:
        return (f"{len(missing)} required frame(s) not captured, first {missing[0].label()}", None)
    # same set, different order or a duplicate row.
    raise ObservationError(
        f"{pillar_name} metrics frame order does not match the identity join "
        f"(reordered/duplicated) — the artifact is not this join's capture")


# ── schema-shaped result builders ────────────────────────────────────────────

def observation(*, captured: bool, artifacts: Optional[list] = None,
                note: Optional[str] = None, **detail) -> dict:
    """A proof-schema `observation` (required: `captured`). Carries the obs schema
    version + any pillar-specific detail; artifacts are hash-only refs."""
    obs = {"captured": captured, "obs_schema_version": OBS_SCHEMA_VERSION}
    if artifacts:
        obs["artifacts"] = artifacts
    if note:
        obs["note"] = note
    obs.update(detail)
    return obs


def first_divergence(lf: LogicalFrame, *, kind: str, path: Optional[str] = None,
                     port_value=None, retail_value=None) -> dict:
    """A proof-schema `first_divergence` — the first frame at which a pillar's
    comparison failed, in logical (not absolute) coordinates."""
    fd = {"logical_frame": lf.as_dict(), "kind": kind}
    if path is not None:
        fd["path"] = path
    if port_value is not None:
        fd["port_value"] = port_value
    if retail_value is not None:
        fd["retail_value"] = retail_value
    return fd


def pillar_result(verdict: str, *, first_div: Optional[dict] = None,
                  detail: Optional[str] = None, **extra) -> dict:
    """A proof-schema `pillar_result` (required: `verdict`). `extra` carries the
    optional `join`/`replay` sub-status when relevant."""
    res = {"verdict": verdict}
    if first_div is not None:
        res["first_divergence"] = first_div
    if detail is not None:
        res["detail"] = detail
    res.update(extra)
    return res


class AdapterResult(NamedTuple):
    """What every adapter returns: the raw `observation` + its adjudicated
    `pillar_result`. Both plug straight into the proof bundle's `observations`/
    `pillars` maps (EP-05)."""

    observation: dict
    pillar: dict


def not_captured(reason: str, *, artifacts: Optional[list] = None) -> AdapterResult:
    """Fail-closed absence: evidence not present ⇒ the pillar is NOT_CAPTURED, and
    the observation records that nothing was captured."""
    return AdapterResult(
        observation(captured=False, artifacts=artifacts, note=reason),
        pillar_result(NOT_CAPTURED, detail=reason),
    )


def inconclusive(reason: str, *, artifacts: Optional[list] = None) -> AdapterResult:
    """Evidence present but untrustworthy ⇒ INCONCLUSIVE. The observation is marked
    captured (data existed) but the comparison could not be soundly made."""
    return AdapterResult(
        observation(captured=True, artifacts=artifacts, note=reason),
        pillar_result(INCONCLUSIVE, detail=reason),
    )


def guard_empty_required(required: list[LogicalFrame], pillar_name: str) -> Optional[AdapterResult]:
    """A comparison over zero frames must never read as PASS. If the join∩window
    selected nothing, the pillar is INCONCLUSIVE (data insufficient), returned so a
    caller can `if r := guard_empty_required(...): return r`."""
    if not required:
        return inconclusive(f"{pillar_name}: no required frames in the contract window")
    return None


# ── the identity pillar adapter ──────────────────────────────────────────────

def adapt_identity(pairs_path, *, window=None) -> AdapterResult:
    """`identity` pillar: do the logical frames actually pair (JOIN_COMPLETE, zero
    honest gaps) across the contract window?

    PASS iff there are no in-window gaps (JOIN_COMPLETE). Honest gaps ⇒ FAIL with
    the first gap frame as first_divergence — a JOIN can be structurally honest yet
    still DISPROVE "the frames pair" (roadmap §4.1: JOIN never implies a parity
    pass, and an INCOMPLETE join fails the identity pillar). Absent ⇒ NOT_CAPTURED;
    corrupt/malformed ⇒ INCONCLUSIVE. Carries the `join` sub-status either way."""
    try:
        doc = load_json(pairs_path)
        if doc is None:
            return not_captured(f"no pairs.json at {pairs_path}")
        frames = paired_frames(doc)  # validates well-formed + de-duplicated
        art = [artifact_ref(pairs_path, "pairs")]
        pred = _window_pred(window)
        in_window_pairs = [lf for lf in frames if pred(lf)]

        # In-window honest gaps decide the pillar. Prefer the explicit gap arrays
        # (window-filterable); fall back to the stored join_verdict for a pre-EP-03
        # pairs.json that lacks them.
        gap_arrays_present = ("port_only" in doc) and ("retail_only" in doc)
        if gap_arrays_present:
            gaps = []
            for side in ("port_only", "retail_only"):
                for row in doc.get(side) or []:
                    lf = LogicalFrame.from_key(row.get("key"))
                    if pred(lf):
                        gaps.append(lf)
            complete = not gaps
            first_gap = sorted(gaps)[0] if gaps else None
        else:
            jv = doc.get("join_verdict")
            if jv not in ("JOIN_COMPLETE", "JOIN_PARTIAL"):
                raise ObservationError(
                    "pairs.json lacks both gap arrays and a join_verdict; cannot classify")
            complete = jv == "JOIN_COMPLETE"
            first_gap = None  # cannot localize without the gap arrays

        join_sub = "JOIN_COMPLETE" if complete else "JOIN_PARTIAL"
        obs = observation(captured=True, artifacts=art,
                          note=f"{len(in_window_pairs)} paired frame(s) in window",
                          paired=len(in_window_pairs))
        if complete:
            return AdapterResult(obs, pillar_result(
                PASS, join=join_sub,
                detail=f"{len(in_window_pairs)} frames pair with no honest gaps"))
        fd = first_divergence(first_gap, kind="identity_gap") if first_gap else None
        return AdapterResult(obs, pillar_result(
            FAIL, join=join_sub, first_div=fd,
            detail="identity join has honest gaps in the contract window"))
    except (ObservationError, FingerprintError) as exc:
        return inconclusive(str(exc))
