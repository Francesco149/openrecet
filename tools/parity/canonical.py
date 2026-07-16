#!/usr/bin/env python3
"""tools/parity/canonical.py — the frozen proof_id canonicalization rule (§4.4).

Promoted verbatim from tools/test_parity_schema.py by EP-02 so there is ONE
implementation of the content-addressing rule, shared by the EP-01 schema gate,
tools/test_parity_fingerprint.py, and parity_prove.py (EP-05). Any change to these
functions changes proof_id semantics and needs an R3 sign-off + schema note
(docs/reference/parity-proof-format.md, roadmap §4.4).

stdlib-only on purpose: it must import even in the stdlib-only CI shell where
jsonschema/yaml are absent.
"""
from __future__ import annotations

import hashlib
import json

# The two keys excluded from a proof's own preimage: proof_id (self-referential)
# and envelope (NON-HASHED volatile metadata — local paths, wall-clock, notes).
NON_HASHED = ("proof_id", "envelope")


def canonical_bytes(proof: dict) -> bytes:
    """Deterministic preimage of proof_id: drop proof_id + envelope, then sort keys
    recursively and emit compact UTF-8 JSON. Arrays keep order."""
    core = {k: v for k, v in proof.items() if k not in NON_HASHED}
    return json.dumps(
        core, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def proof_id_of(proof: dict) -> str:
    """SHA-256 hex of canonical_bytes(proof). Same hashed bytes ⇒ same id anywhere."""
    return hashlib.sha256(canonical_bytes(proof)).hexdigest()


def proof_passes(required_pillars: list[str], proof: dict) -> bool:
    """Fail-closed required-pillar gate: PASS iff EVERY required pillar has
    verdict == PASS in the bundle. A required pillar left NOT_CAPTURED/absent
    ⇒ not proven (roadmap §4.1, EP-01)."""
    pillars = proof.get("pillars", {})
    return all(
        pillars.get(p, {}).get("verdict") == "PASS" for p in required_pillars
    )
