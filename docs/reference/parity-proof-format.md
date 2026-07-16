# Parity proof + contract format (v1)

> **Status:** FROZEN v1 (roadmap EP-01, adopted 2026-07-16). Schema changes need
> R3 approval + a version bump. Machine vocabulary: [`parity-vocabulary.md`](parity-vocabulary.md).

Two schemas under `docs/schemas/`:

| Schema | Validates | Lives in |
|---|---|---|
| `parity-contract-v1.schema.json` | the parity contract a scenario declares | `tests/scenarios/<id>/scenario.yaml` (opt-in) |
| `parity-proof-v1.schema.json` | a content-addressed proof bundle | `runs/proofs/sha256/<first2>/<proof_id>/proof.json` (local, gitignored) |

Gate: `tools/test_parity_schema.py` (validates both + fixtures + the two code-only
rules below). Fixtures: `docs/schemas/fixtures/`.

## Canonical pillars (frozen)

A **pillar** is one comparison axis with an independent verdict. No pillar is
globally passed; every result is scenario/window-scoped.

| Pillar | Asserts | Owning workstream |
|---|---|---|
| `identity` | logical frames actually pair (join complete, zero honest gaps) | trace-studio v3 |
| `state` | canonical state-tree roots equal cross-target | ST |
| `save` | save-file bytes equal after a scripted sequence | ST |
| `render_program` | same draw program (textures + per-tex tri totals + order) | GX |
| `pixels` | every required paired pixel equal (mode `exact`) | GX |
| `audio_events` | SE/BGM/fade event stream equal (id + order + params) | AU |
| `timing` | cadence/latency contract (real-time lane only) | RT |
| `boundary` | system-boundary event streams equal (input/file/COM/…) | BT |

`identity` is a precondition, not parity: `JOIN_COMPLETE` alone never implies a
scenario passed (roadmap §4.1). Same-side replay fidelity (`REPLAY_EXACT`) is
recorder evidence, not cross-target proof.

## Verdict vocabulary (frozen, roadmap §4.1)

- Pillar result: `PASS | FAIL | NOT_CAPTURED | NOT_REQUIRED | INCONCLUSIVE`.
- Identity join sub-status: `JOIN_COMPLETE | JOIN_PARTIAL`.
- Same-side replay sub-status: `REPLAY_EXACT | REPLAY_DIVERGENT`.
- Tool exit: `0` all required pillars PASS · `1` ≥1 required FAIL · `2` invalid
  input / unavailable tool / corrupt artifact / inconclusive.

**Fail closed:** a required observation absent ⇒ `NOT_CAPTURED`, never PASS. A
missing required fingerprint fails schema validation outright.

## Frame identity (roadmap §4.2)

Join key is logical: `(anchor_name, anchor_occurrence, offset_since_anchor)`.
Never compare absolute present indices. Side-local coords
(`present_index, simulation_tick, capture_index`) diagnose pacing only.

## Proof bundle — required fingerprints

All eleven top-level groups are required (`schema_version, proof_id, subject,
inputs, environment, normalization, tools, observations, pillars, coverage,
exceptions, human_review`). Mandatory hashes:

- `subject.retail.{pe_sha256, reference_id}`; `subject.port.{pe_sha256,
  git_commit, dirty_patch_sha256|null}`.
- `inputs.save` = resolved save SHA-256 or `@fresh`; `inputs.scenario_contract`
  (id + contract hash + version); `inputs.trace_sha256`;
  `inputs.assets_manifest_sha256`/`recet_ini_sha256` (hash or `@none`).
- `environment.{os_build, locale, codepage, d3d_runtime, gpu, driver,
  resolution, display_mode}`.
- `normalization[]` = every pin/hook + params.
- `tools.{frida_agent, d3d_proxy, replayer, comparator, schema}_sha256`
  (comparator/schema always a hash; capture tools may be `@none`).

## proof_id + canonicalization (frozen rule)

`proof_id = SHA-256(canonical_bytes)` where `canonical_bytes` = the bundle with
`proof_id` **and** `envelope` removed, keys sorted recursively, compact UTF-8
JSON (`separators=(",",":")`), arrays keep order. Same inputs ⇒ same `proof_id`
from any machine. Reference impl: `tools/parity/canonical.py:canonical_bytes`
(promoted by EP-02; `test_parity_schema.py` imports it).

`envelope` is the ONLY place local absolute paths, wall-clock timestamps, and
human display notes may appear — it is stripped before hashing, so it never
perturbs the id and never leaks a non-portable path into content addressing.

## Required-pillar gate (fail closed, code-only)

Schema validity ≠ pass. A proof passes a contract iff **every**
`contract.proof.required_pillars` entry has `verdict == PASS` in the bundle.
A required pillar left `NOT_CAPTURED`/`FAIL`/`INCONCLUSIVE` ⇒ the scenario is not
proven (exit 1/2). Reference impl: `tools/parity/canonical.py:proof_passes`; the
real consumer is `parity_prove.py` (EP-05).

## Contract (scenario.yaml opt-in)

Adds `schema_version: 2` + a `proof:` block + optional `coverage_expectations:`
without breaking legacy keys (`description, max_frames, rng_seed, …` stay,
unconstrained). `proof.contract_version: 1` is the contract major (distinct from
the scenario `schema_version`). Empty `coverage_expectations` list = "no declared
obligation yet", NOT "covered". Full example: `fixtures/contract-full.valid.yaml`.

## Schema evolution + redaction

- **Major** bump (`schema_version`) = breaking; a v1 reader rejects any other
  major. **Minor** = additive optional fields; readers preserve unknown fields
  when copying (`additionalProperties: true` on containers; `required` enforces
  the fingerprints).
- **No proprietary bytes** in any committed manifest/fixture — hashes, sizes,
  ids, metrics only. Captured artifacts stay in the local CAS.

## Regenerate fixtures

Fixtures are generated deterministically (valid 64-hex hashes). To change them,
edit the generator in the commit that introduced them
(`docs: EP-01 parity schemas`) or re-emit by hand keeping every hash 64 lowercase
hex. `tools/test_parity_schema.py` is the acceptance gate.
