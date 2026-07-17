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

All twelve top-level groups are required (`schema_version, proof_id, subject,
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
the NON-HASHED keys `proof_id`, `envelope`, **and** `human_review` removed, keys
sorted recursively, compact UTF-8 JSON (`separators=(",",":")`), arrays keep
order. Same inputs ⇒ same `proof_id` from any machine. Reference impl:
`tools/parity/canonical.py:canonical_bytes` (promoted by EP-02;
`test_parity_schema.py` imports it).

`envelope` holds local absolute paths, wall-clock timestamps, and freeform display
notes; `human_review` holds structured human attestation (below). Both are stripped
before hashing, so neither perturbs the id nor leaks a non-portable path into
content addressing.

## Required-pillar gate (fail closed, code-only)

Schema validity ≠ pass. A proof passes a contract iff **every**
`contract.proof.required_pillars` entry has `verdict == PASS` in the bundle.
A required pillar left `NOT_CAPTURED`/`FAIL`/`INCONCLUSIVE` ⇒ the scenario is not
proven (exit 1/2). Reference impl: `tools/parity/canonical.py:proof_passes`; the
real consumer is `parity_prove.py` (EP-05).

## Human review (EP-07, additive + non-hashed)

`human_review` is optional human attestation, ADDITIVE and NON-HASHED (in
`canonical.NON_HASHED`), so attaching one never perturbs `proof_id` nor a machine
verdict. `tools/parity/prove.py:attach_human_review(proof, review, *,
required_pillars)` builds the record; `tools/parity_review.py` is the CLI
(`parity_review.py <bundle_dir> --reviewer … --date … --scope … [--verdict
confirmed|rejected|noted]`), writing the review back into the SAME content-addressed
bundle (a non-hashed amendment, like the envelope; `required_pillars` default to the
bundle's own scenario contract, verified against the recorded `contract_sha256`).

The record carries `machine_verdict` (the §4.1 gate at review time). A **confirming
review over a non-PASS gate** is recorded as `confirmed-despite-<MACHINE>` (e.g.
`confirmed-despite-FAIL`) — a human standing behind a known machine gap is explicit
and scoped, **never a silent pass**: the CLI still exits with the machine gate's code
(EP-07 acceptance — a human confirmation cannot override a failed machine-required
pillar). Use it for a scene human-confirmed 1:1 yet not bit-exact (e.g.
`house-firstcust-arrprobe`, visually 1:1 with a sub-perceptual cross-target diff).

Adding `human_review` to `NON_HASHED` was an **R3-approved canonicalization
refinement** for EP-07, not a schema major bump: the frozen schema SHAPE is unchanged
(`human_review` stays a required top-level field), only the canonicalization exclusion
list grew. Pre-EP-07 bundles' `proof_id`s are stale under the new rule (they hashed a
`human_review:null` into the core) and re-address on the next drive — advisory only,
the durable key is `contract_sha256`; no persisted bundle ever carried a real review.

## Observation adapters + normalized metrics (EP-04)

`tools/parity/{observations,pixels,render_program}.py` turn the artifacts the
porting loop already produces into a schema-shaped `observation` + adjudicated
`pillar_result` (the two maps above). Each adapter returns an `AdapterResult`
and is TOTAL — it never raises; a trust failure becomes `INCONCLUSIVE`.

Three fail-closed rules (roadmap §3/§4.1):

- **Absent ⇒ `NOT_CAPTURED`.** A missing evidence file, or a required paired
  frame with no measurement, is never a PASS.
- **The identity join is authority.** Before comparing, an adapter checks the
  metrics cover EXACTLY the required frames (the in-window paired frames of
  `pairs.json`), in order. A frame outside the join, a reordered/duplicated
  stream, or a `source` container hash that mismatches the join is
  `INCONCLUSIVE` (a stale/foreign capture — exit 2), distinct from a real
  disproof (`FAIL`).
- **Scoped tokens.** Producer verdicts map to one PILLAR verdict:
  join `JOIN_COMPLETE`→identity `PASS` / gaps→`FAIL`; draw `ALIGNED`/`BATCHING`
  →`PASS` (BATCHING noted: same materials, pixels expected equal) / `DIVERGENT`
  →`FAIL`; pixels exact-mode `PASS` iff every required frame `differ==0`.

Normalized metrics doc (`schema_version` = observations `OBS_SCHEMA_VERSION`,
one row per identity-joined frame, keyed by the logical `[anchor, occ, offset]`):

```json
{ "schema_version": 1, "pillar": "pixels", "mode": "exact",
  "source": { "port_container_sha256": "<64hex>", "retail_container_sha256": "<64hex>" },
  "frames": [ { "key": ["PAUSE_OPEN", 1, 123], "differ": 0, "total": 786432, "meanabs": 0.0 } ] }
```

`render_program` rows carry `draw_verdict` (+ `port_tris`/`retail_tris`/
`divergent[]`) instead of `differ`; `render_program.from_view_json()` bridges a
real Trace Studio v3 `view.json` (which already bakes `draw_verdict` per paired
frame) into this doc with no new tooling. `parity_prove.py` (EP-05) resolves or
drives these docs and calls the adapters. Gate: `tools/test_parity_observations.py`.

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
