#!/usr/bin/env python3
"""tools/parity/prove.py — EP-05 proof-bundle assembly, gate, and content store.

Pure: no capture, no Frida, no Windows, no wall-clock in the hashed core. Given
the EP-02 provenance groups + a resolved {pillar: AdapterResult} map (EP-04), it

  1. assembles the schema-shaped proof bundle (parity-proof-v1),
  2. derives the content-addressed `proof_id` (§4.4 — canonical JSON excluding
     `proof_id` + the non-hashed `envelope`),
  3. applies the FAIL-CLOSED required-pillar gate (§4.1), and
  4. atomically stores the immutable bundle in the local CAS.

`tools/parity_prove.py` is the CLI that gathers real provenance + resolves
observations from a v3 window and calls in here. Splitting the pure core out is
what makes the EP-05 acceptance (determinism, the gate, no-leaked-paths, the
per-pillar negative tests) testable with fixtures — no Windows required.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

from .canonical import proof_id_of

# Canonical pillar order (docs/reference/parity-proof-format.md). Assembly emits
# every declared pillar; a pillar with no producer is NOT_CAPTURED, never dropped.
PILLAR_NAMES = (
    "identity", "state", "save", "render_program",
    "pixels", "audio_events", "timing", "boundary",
)


def assemble(*, subject: dict, inputs: dict, environment: dict, tools: dict,
             normalization: Optional[list], observations: dict, pillars: dict,
             coverage: Optional[dict] = None, exceptions: Optional[list] = None,
             human_review=None, envelope: Optional[dict] = None) -> dict:
    """Build the proof bundle and stamp its `proof_id`. The id is derived AFTER the
    core is complete and is a pure function of the hashed content (envelope +
    proof_id excluded), so identical inputs ⇒ identical id from any machine."""
    proof = {
        "schema_version": 1,
        "subject": subject,
        "inputs": inputs,
        "environment": environment,
        "normalization": list(normalization or []),
        "tools": tools,
        "observations": observations,
        "pillars": pillars,
        "coverage": coverage if coverage is not None else {"captured": False},
        "exceptions": list(exceptions or []),
        "human_review": human_review,
    }
    if envelope is not None:
        proof["envelope"] = envelope
    proof["proof_id"] = proof_id_of(proof)
    return proof


def gate(required_pillars, proof: dict) -> tuple[str, int, dict]:
    """The fail-closed required-pillar gate (§4.1). Returns
    (summary_verdict, exit_code, per_required_verdict):

      every required pillar PASS         → ("PASS", 0)
      any required pillar FAIL           → ("FAIL", 1)
      else (a required pillar is         → ("INCONCLUSIVE", 2)
        NOT_CAPTURED / INCONCLUSIVE)

    A required pillar that is absent from the bundle counts as NOT_CAPTURED — a
    contract can never pass by simply omitting a pillar it required."""
    pillars = proof.get("pillars", {})
    verdicts = {p: pillars.get(p, {}).get("verdict", "NOT_CAPTURED") for p in required_pillars}
    if all(v == "PASS" for v in verdicts.values()):
        return "PASS", 0, verdicts
    if any(v == "FAIL" for v in verdicts.values()):
        return "FAIL", 1, verdicts
    return "INCONCLUSIVE", 2, verdicts


# ── EP-07: human review (additive, non-hashed, verdict-preserving) ────────────

# A human review's own verdict vocabulary — human attestation, NOT a machine pillar
# result. Kept distinct so a reader never conflates the two (§3 rule 2).
HUMAN_VERDICTS = ("confirmed", "rejected", "noted")


def attach_human_review(proof: dict, review: dict, *, required_pillars) -> dict:
    """Attach a human confirmation and return a NEW proof with `human_review` set —
    WITHOUT changing `proof_id` or any machine verdict (EP-07 acceptance: a human
    confirmation cannot override a failed machine-required pillar; deferred
    divergences retain explicit failing/exception scope).

    `human_review` is non-hashed (canonical.NON_HASHED), so this is ADDITIVE: the
    content-addressed id is unchanged (asserted below), and the machine gate (§4.1,
    read from `pillars`) is untouched — `gate()`/exit codes never move for a review.

    The review is stamped with the machine gate verdict at review time
    (`machine_verdict`). A CONFIRMING verdict over a non-PASS machine gate is recorded
    as `confirmed-despite-<MACHINE>` (e.g. `confirmed-despite-FAIL`), so a human
    standing behind a known machine gap is EXPLICIT + scoped, never a silent pass.

    Raises ValueError on a malformed review (missing reviewer/date/scope or an unknown
    human verdict); AssertionError if attaching perturbs proof_id (canonicalization
    broken)."""
    for f in ("reviewer", "date", "scope"):
        if not review.get(f):
            raise ValueError(f"human review requires a non-empty {f!r}")
    human_verdict = review.get("verdict", "noted")
    if human_verdict not in HUMAN_VERDICTS:
        raise ValueError(
            f"unknown human review verdict {human_verdict!r} "
            f"(want one of {HUMAN_VERDICTS})")

    machine_verdict, _, _ = gate(required_pillars, proof)
    recorded = human_verdict
    if human_verdict == "confirmed" and machine_verdict != "PASS":
        recorded = f"confirmed-despite-{machine_verdict}"

    record = {
        "reviewer": review["reviewer"],
        "date": review["date"],
        "scope": review["scope"],
        "verdict": recorded,
        "machine_verdict": machine_verdict,
    }
    if review.get("notes"):
        record["notes"] = review["notes"]
    if review.get("confirmed_pillars") is not None:
        record["confirmed_pillars"] = list(review["confirmed_pillars"])

    reviewed = {**proof, "human_review": record}
    # EP-07 neutrality: a review must not move the content-address. Compare under the
    # CURRENT canonicalization rule (the review-free baseline, recomputed) — robust to a
    # bundle whose STORED proof_id predates this rule (a pre-EP07 bundle's stored id is
    # stale, but that is a separate re-drive concern, not caused by the review).
    if proof_id_of(reviewed) != proof_id_of(proof):
        raise AssertionError(
            "human_review changed proof_id — canonicalization is broken (EP-07)")
    return reviewed


def first_divergences(proof: dict) -> list[dict]:
    """Every FAILed pillar's first_divergence (logical coords), for the summary."""
    out = []
    for name, res in proof.get("pillars", {}).items():
        if res.get("verdict") == "FAIL" and "first_divergence" in res:
            out.append({"pillar": name, **res["first_divergence"]})
    return out


def store_path(root, proof_id: str) -> Path:
    """CAS location: runs/proofs/sha256/<first2>/<proof_id>/."""
    return Path(root) / "sha256" / proof_id[:2] / proof_id


def write_bundle(proof: dict, root, *, local_paths: Optional[dict] = None,
                 generated_at: Optional[str] = None) -> tuple[Path, bool]:
    """Atomically store the bundle under `root`. Returns (bundle_dir, created).

    The `envelope` (machine-local paths + wall-clock) is written into proof.json
    but EXCLUDED from `proof_id`, so no non-portable path ever enters content
    addressing (§4.4, §3 rule 6). Immutable + idempotent (§3 rule 5): if the id
    directory already exists, it is left untouched and `created` is False — a
    re-run with identical inputs never rewrites history."""
    pid = proof["proof_id"]
    if local_paths or generated_at:
        env = dict(proof.get("envelope") or {})
        if generated_at:
            env["generated_at"] = generated_at
        if local_paths:
            env["local_paths"] = local_paths
        proof = {**proof, "envelope": env}
        # The envelope must NOT perturb the id (it is stripped before hashing).
        if proof_id_of(proof) != pid:
            raise AssertionError("envelope changed proof_id — canonicalization is broken")

    dest = store_path(root, pid)
    if dest.exists():
        return dest, False
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.parent / f".{pid}.tmp"
    if tmp.exists():
        for p in tmp.iterdir():
            p.unlink()
        tmp.rmdir()
    tmp.mkdir(parents=True)
    (tmp / "proof.json").write_text(json.dumps(proof, indent=1, sort_keys=True))
    tmp.rename(dest)  # atomic on the same filesystem
    return dest, True


def summarize(proof: dict, required_pillars, *, bundle_dir: Optional[Path] = None) -> dict:
    """A compact JSON summary for `--json`: the gate verdict/exit, per-pillar
    verdicts, first divergences, the proof id, and the bundle path."""
    verdict, code, verdicts = gate(required_pillars, proof)
    return {
        "proof_id": proof["proof_id"],
        "verdict": verdict,
        "exit_code": code,
        "required_pillars": list(required_pillars),
        "pillar_verdicts": verdicts,
        "all_pillar_verdicts": {k: v.get("verdict") for k, v in proof.get("pillars", {}).items()},
        "first_divergences": first_divergences(proof),
        "human_review": proof.get("human_review"),
        "bundle_dir": str(bundle_dir) if bundle_dir else None,
    }
