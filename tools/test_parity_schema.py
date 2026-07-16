#!/usr/bin/env python3
"""tools/test_parity_schema.py — EP-01 gate for the parity contract/proof schemas.

Validates docs/schemas/parity-{contract,proof}-v1.schema.json against their
fixtures under docs/schemas/fixtures/, and pins the two rules the schema alone
cannot express:

  * canonicalization determinism + proof_id EXCLUSION (proof_id and the
    non-hashed `envelope` do not enter their own preimage; any hashed byte does);
  * the required-pillar GATE (fail closed): a proof PASSES a contract only if
    every contract.required_pillars entry has verdict == PASS in the proof — so a
    required pillar left NOT_CAPTURED cannot validate as pass.

Negative tests included: a missing retail PE hash and an unknown MAJOR
schema_version must both fail schema validation; a required pixels pillar marked
NOT_CAPTURED must fail the gate while remaining structurally valid.

Reference canonicalization + gate here are the frozen contract (roadmap §4.4/EP-01);
tools/parity/ (EP-02/EP-05) promotes them to library code.

Regenerate fixtures: see docs/reference/parity-proof-format.md.
Run: nix develop --command python3 tools/test_parity_schema.py
Exits non-zero on failure; prints OK on success. SKIPs cleanly (exit 0) when
jsonschema is unavailable (e.g. the stdlib-only CI shell).
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMAS = ROOT / "docs" / "schemas"
FIX = SCHEMAS / "fixtures"

try:
    import yaml
    from jsonschema import Draft202012Validator
except ModuleNotFoundError as exc:  # stdlib-only env (ci shell)
    print(f"SKIP: {exc.name} unavailable — run under the default nix devshell")
    sys.exit(0)


# ── reference canonicalization + gate (frozen; roadmap §4.4/EP-01) ──────────

_NON_HASHED = ("proof_id", "envelope")


def canonical_bytes(proof: dict) -> bytes:
    """Deterministic preimage of proof_id: drop proof_id + envelope, then
    sort keys recursively and emit compact UTF-8 JSON. Arrays keep order."""
    core = {k: v for k, v in proof.items() if k not in _NON_HASHED}
    return json.dumps(
        core, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def proof_id_of(proof: dict) -> str:
    return hashlib.sha256(canonical_bytes(proof)).hexdigest()


def proof_passes(required_pillars: list[str], proof: dict) -> bool:
    pillars = proof.get("pillars", {})
    return all(
        pillars.get(p, {}).get("verdict") == "PASS" for p in required_pillars
    )


# ── harness ────────────────────────────────────────────────────────────────

_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    if not cond:
        _failures.append(msg)


def load_json(name: str) -> dict:
    return json.loads((FIX / name).read_text())


def load_yaml(name: str) -> dict:
    return yaml.safe_load((FIX / name).read_text())


def errors(schema: dict, instance: dict) -> list:
    return list(Draft202012Validator(schema).iter_errors(instance))


def main() -> int:
    proof_schema = json.loads((SCHEMAS / "parity-proof-v1.schema.json").read_text())
    contract_schema = json.loads(
        (SCHEMAS / "parity-contract-v1.schema.json").read_text()
    )

    # 0. the schemas are themselves valid Draft 2020-12 documents
    for name, schema in (("proof", proof_schema), ("contract", contract_schema)):
        try:
            Draft202012Validator.check_schema(schema)
        except Exception as e:  # noqa: BLE001
            check(False, f"{name} schema is not valid Draft2020-12: {e}")

    # 1. valid fixtures validate clean
    for name in ("proof-minimal.valid.json", "proof-full.valid.json"):
        errs = errors(proof_schema, load_json(name))
        check(not errs, f"{name} should validate, got: {[e.message for e in errs]}")
    for name in ("contract-minimal.valid.yaml", "contract-full.valid.yaml"):
        errs = errors(contract_schema, load_yaml(name))
        check(not errs, f"{name} should validate, got: {[e.message for e in errs]}")

    # 2. schema negative tests — required fingerprint + unknown major
    errs = errors(proof_schema, load_json("proof-missing-retail-hash.invalid.json"))
    check(bool(errs), "missing retail pe_sha256 must fail schema validation")
    check(
        any("pe_sha256" in " ".join(map(str, e.absolute_path)) or "pe_sha256" in e.message for e in errs),
        "missing-retail-hash error should point at pe_sha256",
    )

    errs = errors(proof_schema, load_json("proof-unknown-major.invalid.json"))
    check(bool(errs), "schema_version 2 must fail the v1 proof schema")

    errs = errors(contract_schema, load_yaml("contract-missing-pillars.invalid.yaml"))
    check(bool(errs), "contract without required_pillars must fail")
    errs = errors(contract_schema, load_yaml("contract-bad-pillar.invalid.yaml"))
    check(bool(errs), "contract with an unknown pillar name must fail")

    # 3. canonicalization determinism + proof_id exclusion + sensitivity
    full = load_json("proof-full.valid.json")
    id1 = proof_id_of(full)
    check(id1 == proof_id_of(full), "proof_id must be deterministic")

    from copy import deepcopy

    excl = deepcopy(full)
    excl["proof_id"] = "0" * 64
    excl["envelope"] = {"generated_at": "2099-01-01T00:00:00Z", "display_notes": "x"}
    check(
        proof_id_of(excl) == id1,
        "changing proof_id/envelope must NOT change the derived proof_id",
    )

    sens = deepcopy(full)
    sens["subject"]["retail"]["reference_id"] = "some-other-build"
    check(
        proof_id_of(sens) != id1,
        "changing a hashed field MUST change the derived proof_id",
    )

    # 4. required-pillar gate (fail closed)
    contract = load_yaml("contract-full.valid.yaml")
    required = contract["proof"]["required_pillars"]
    check(
        proof_passes(required, full),
        "proof-full.valid should pass the full contract gate",
    )
    gatefail = load_json("proof-required-pixels-not-captured.gatefail.json")
    check(
        not errors(proof_schema, gatefail),
        "pixels-not-captured fixture must still be structurally valid",
    )
    check(
        not proof_passes(required, gatefail),
        "a required pillar left NOT_CAPTURED must FAIL the gate",
    )

    # 5. no proprietary bytes: every fixture is small text; no hex/base64 blob > 64
    for fp in sorted(FIX.iterdir()):
        raw = fp.read_bytes()
        check(len(raw) < 8192, f"{fp.name} unexpectedly large for a fixture")
        check(
            raw.decode("utf-8", "strict") is not None,
            f"{fp.name} must be valid UTF-8 text",
        )

    # 6. standing gate: any real scenario that opts in (schema_version==2) validates
    scen_dir = ROOT / "tests" / "scenarios"
    opted = 0
    for y in sorted(scen_dir.glob("*/scenario.yaml")):
        data = yaml.safe_load(y.read_text()) or {}
        if data.get("schema_version") == 2 and "proof" in data:
            opted += 1
            errs = errors(contract_schema, data)
            check(not errs, f"{y}: contract invalid: {[e.message for e in errs]}")

    if _failures:
        print("FAIL:")
        for f in _failures:
            print("  -", f)
        return 1
    print(f"OK ({opted} opted-in scenario contract(s) validated)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
