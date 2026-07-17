# Parity evidence, coverage, and full-game convergence roadmap

> **Status:** ADOPTED. **Wave-0 EP-00→EP-08 COMPLETE; M0 REACHED** (2026-07-16). M0
> R3 adversarial review — `../findings/parity-M0-adversarial-review.md` (fixed the
> proof_id path-leak, logged HOLE-2/3/4). **EP-06 truthful two-axis ledger** landed
> 2026-07-16 (`../findings/parity-EP06-ledger-lifecycle.md`). **EP-08 — HOLE-2 close +
> cache re-key by full provenance** landed 2026-07-16
> (`../findings/parity-EP08-cache-provenance.md`). **Next: a pixels/state PRODUCER (now
> UNBLOCKED — the gate is closed): the headless `pixel-metrics.json` producer, then
> ST-00/ST-01 (roadmap M1).** No other package started unless it says so  \
> **Adopted:** 2026-07-16  \
> **Owner:** R3/highest-reasoning orchestrator  \
> **Scope:** long-horizon tooling and methodology; `../FRONT.md` remains the live
> gameplay-port front  \
> **Supersedes for live prioritization:** the open T4–T10 list in
> `../audits/2026-06-09-methodology-audit.md`; that audit remains a historical
> decision record

## 0. Outcome

The program is complete when OpenRecet can be played through every supported path and
the project can state, with reproducible evidence, exactly which retail behavior is
equivalent in the port. The system must find the first incorrect state transition, not
only the first visible bad frame, and must expose untested retail behavior rather than
allowing it to hide behind source-annotation counts.

The central deliverable is an **evidence compiler**:

```text
retail + port builds
       + certified start state
       + exact input edge
       + environment/configuration
       + capture tools
                    |
                    v
       normalized observations
       (identity, state, save, render, pixels,
        audio, timing, external effects, coverage)
                    |
                    v
       content-addressed proof bundle
                    |
          +---------+----------+
          |                    |
          v                    v
   truthful ledger       coverage/behavior atlas
                               |
                               v
                     next highest-value experiment
```

This is additive to the established live-probe → deterministic trace → Trace Studio
porting loop. It turns that loop into a full-game convergence system.

## 1. Audit baseline

Snapshot on 2026-07-16:

- Persistent Frida retail probe, semantic anchors, deterministic segtraces, save
  virtualization, flow tracing, Trace Studio v3 command capture/replay, TTD scaffolding,
  and extensive host tests already exist.
- Scenario corpus: 66 `scenario.yaml` files and 71 traces across 68 scenario
  directories. No scenario declares machine-readable feature/coverage obligations.
- `STATUS.md` classifies 2,548 non-thunk functions, but its strongest states are
  inferred from source markers. `CALL_TRACE_ENTER` presence does not itself prove that a
  function executed or aligned.
- Trace Studio's `ALIGNED` join means frame identities paired without gaps. It does not
  mean cross-target pixels, draw programs, state, saves, or audio are equal.
- Same-side v3 replay verification is strong recorder/replayer evidence, not
  retail-versus-port proof.
- v3 state capture is optional and narrow. Save writes are recorded but not consumed by
  a general equality gate.
- Static export is decompiled text plus a function CSV; there is no queryable
  block/edge/xref/global index.
- D3D proxy forwards some render-affecting methods without recording them and snapshots
  unwrapped resource contents late.
- Deterministic input injection starts after DirectInput polling and turbo replaces
  pacing. That is correct for simulation isolation, but cannot prove input/focus or
  real-time behavior.
- Linux CI builds and runs binary guards but does not run the host C or Python suites.

Already-known backlog retained here rather than claimed as new: state checksum/save
equality, executed-but-unported census, structured exploration, JSON output, hybrid
function replacement, audio comparison, multi-seed sweeps, and a parity sweep.

## 2. Reasoning tiers and delegation rule

These labels are capability requirements, not vendor/model names.

| Tier | Use | Authority |
|---|---|---|
| **R3 — highest reasoning** | architecture, fidelity policy, ambiguous RE, schemas that define truth, divergence adjudication, coverage semantics | may change contracts and accept/reject evidence |
| **R2 — implementation reasoning** | bounded subsystem implementation from an approved contract; integration with tests | may choose local code structure; may not change proof meaning |
| **R1 — mechanical** | searches, enumerations, fixtures, schema-prescribed adapters, test execution, doc/link updates, corpus backfills | reports evidence only; never declares parity |

### Mandatory R3 work

Assign the highest-reasoning model to these packages or gates:

- EP-00, EP-01, and EP-06: claim vocabulary, proof schema, ledger semantics.
- ST-00 and ST-05: canonical state model and mutation semantics.
- CV-00, CV-03 design, CV-07 scoring, and CV-08 calibration.
- GX-01 policy and GX-03 resource-version semantics.
- BA-00, BA-04 grammar design, BA-05 scheduling policy, and BA-07 seed solving.
- CC-00, CC-04, and CC-05: ABI/write-set and hybrid execution safety.
- BT-00, BT-02, AU-02, RT-01, and RT-02 adjudication.
- CI-00 and CI-05: test tiers and preservation-release acceptance.
- Any decision to tolerate a difference, call it phase/RNG/benign, or weaken an exact
  comparison.

R1/R2 agents must stop and return evidence when:

- retail semantics are ambiguous;
- a captured value might be a pointer, handle, padding, uninitialized byte, or
  load-dependent origin;
- a schema/key/threshold would need changing;
- an unsupported D3D/API call is observed;
- an exact gate has a residual;
- a hook could perturb retail or mutate user-owned data;
- the task would commit proprietary bytes.

### Delegation packet required for every R1/R2 task

The orchestrator must provide:

```text
WORK PACKAGE ID
GOAL
READ SET              exact files/sections
WRITE SET             exclusive paths
LOCKED DECISIONS      schema, names, thresholds, invariants
PROCEDURE              ordered mechanical steps
ACCEPTANCE COMMANDS   exact commands and expected exit/result
NEGATIVE TEST          how to prove the gate catches a deliberate mismatch
STOP CONDITIONS       what must be returned to R3
REPORT                 changed paths, tests, evidence, unresolved items
```

Lower-tier agents may implement a binary gate but may not self-grade a nonzero residual
as close enough. R3 reruns and interprets every new parity gate once before it becomes
authoritative.

## 3. Non-negotiable design rules

1. **Fail closed.** Missing required evidence is `NOT_CAPTURED` or `INCONCLUSIVE`, never
   pass.
2. **One word, one meaning.** Join completeness, replay fidelity, implementation
   inventory, runtime coverage, and parity are separate verdicts.
3. **Claims are scoped.** No function, scene, or subsystem is globally `verified`.
4. **Inputs determine cache identity.** Hash every executable, data/config input,
   normalization hook, and tool that can affect output.
5. **Artifacts are immutable.** A proof bundle is content-addressed. Re-running creates
   another bundle; it does not rewrite history.
6. **No proprietary payloads in Git.** Manifests may contain hashes, sizes, IDs, and
   metrics; captures remain local.
7. **Schemas are versioned.** Readers reject unknown major versions and preserve
   unknown fields when copying.
8. **Deterministic and real-time lanes stay separate.**
9. **Exact means exact.** Thresholds require a named, R3-approved contract and cannot be
   silently introduced by a comparator.
10. **Measure observer effects.** Frida/proxy hooks are part of the experimental setup,
    not assumed transparent.
11. **Build consumers before platforms.** TTD, coverage, and capture extensions land
    only with a concrete report/gate that consumes them.
12. **No duplicate live truth.** Current counts/fronts live only in generated status and
    `FRONT.md`; plans contain stable work and dated results.

## 4. Shared contracts to freeze first

### 4.1 Verdict vocabulary

All pillar results use:

- `PASS`: all required observations captured and comparison satisfied.
- `FAIL`: observations captured and comparison disproved the contract.
- `NOT_CAPTURED`: required observation absent.
- `NOT_REQUIRED`: contract explicitly excludes this pillar.
- `INCONCLUSIVE`: data exists but cannot support a sound conclusion.

Identity pairing separately uses `JOIN_COMPLETE` or `JOIN_PARTIAL`. Same-side replay
uses `REPLAY_EXACT` or `REPLAY_DIVERGENT`. Neither maps to `PASS` without a parity
contract.

Tool exit codes:

- `0`: all required pillars `PASS`.
- `1`: at least one required pillar `FAIL`.
- `2`: invalid input, unavailable tool, corrupt artifact, or inconclusive execution.

### 4.2 Frame identity

Canonical logical identity:

```text
(anchor_name, anchor_occurrence, offset_since_anchor)
```

Every observation also carries side-local:

```text
present_index, simulation_tick, capture_index
```

Logical identity performs the join. Side-local coordinates diagnose pacing and capture
behavior. Never compare absolute present numbers as logical identity.

### 4.3 Scenario parity contract

Extend `tests/scenarios/<id>/scenario.yaml` without breaking existing fields:

```yaml
schema_version: 2
description: ...
rng_seed: ...
proof:
  contract_version: 1
  start_node: optional-behavior-atlas-node
  join:
    anchor: HOUSE_FREEROAM
    occurrence: 1
    window: [120, 240]
  required_pillars:
    - identity
    - state
    - render_program
    - pixels
  optional_pillars:
    - audio_events
  pixels:
    mode: exact
    environment: preservation-reference
  seeds: [19937]
  configurations: [reference-1024-windowed]
  exceptions: []
coverage_expectations:
  functions: []
  blocks: []
  vm_operations: []
  content_ids: []
  transitions: []
  assets: []
  audio_ids: []
```

Empty coverage lists mean “no declared obligation yet,” not “covered.” Contracts must
not default a missing pillar to pass.

### 4.4 Proof manifest

Planned path: `docs/schemas/parity-proof-v1.schema.json`.

Required top-level groups:

```json
{
  "schema_version": 1,
  "subject": {},
  "inputs": {},
  "environment": {},
  "normalization": [],
  "tools": {},
  "observations": {},
  "pillars": {},
  "coverage": {},
  "exceptions": [],
  "human_review": null
}
```

Required fingerprints:

- retail PE SHA-256 and reference ID;
- port PE SHA-256, Git commit, dirty-tree patch hash when applicable;
- actual resolved save bytes or `@fresh`;
- trace and scenario contract;
- relevant asset/archive manifest and `recet.ini`;
- Frida agent, D3D proxy, replayer/viewer, comparator, and schema;
- OS build, locale/codepage, D3D runtime, GPU, driver, resolution/display mode;
- every pin/hook plus parameters.

`proof_id` is SHA-256 of deterministic canonical JSON excluding `proof_id`, local
absolute paths, wall-clock timestamps, and human display notes. Volatile metadata lives
in a non-hashed envelope. Canonicalization rules and test vectors require R3 approval.

### 4.5 Coverage ontology

Coverage keys are namespaced and versioned:

```text
code.function:<va>
code.block:<va>
code.edge:<src-va>-><dst-va>
code.indirect-target:<callsite-va>-><target-va>
vm.dialogue:<opcode>:<operand-class>
vm.tutorial:<opcode>:<operand-class>
state.transition:<from>-><to>
content.item:<id>
content.customer:<id>
content.dungeon:<id>
content.enemy:<id>
content.boss:<id>
asset:<normalized-name>
audio.se:<id>
audio.bgm:<id>
boundary:<api>:<result-class>
config:<dimension>=<value>
```

“Covered” means observed in retail under a named edge. “Implemented” and “proven” are
separate joins.

## 5. Dependency map and milestones

```text
EP truth/provenance
 ├── ST state/save ───────────────┐
 ├── CV static+dynamic coverage ──┼── BA behavior atlas/explorer ── CI full sweep
 ├── GX graphics hardening ───────┤
 ├── CC call capsules ────────────┤
 └── BT/AU/RT boundaries ─────────┘
```

| Milestone | Exit condition |
|---|---|
| M0 — truthful evidence | One stable scenario produces a content-addressed proof; a deliberate cross-target pixel/state mismatch fails; join-only success cannot pass |
| M1 — first-state localization | A deliberate save/state mutation is reported at the correct anchor, region, object, and field |
| M2 — coverage atlas | Query returns retail-observed/unimplemented and reachable/unobserved code/content with scenario provenance |
| M3 — capture trust | Every observed D3D8 method is recorded/proven irrelevant or capture fails; in-frame resource mutation regression passes |
| M4 — behavior atlas | Shared prefix plus at least one branch traverses as nodes/edges; explorer adds coverage and minimizes a synthetic divergence |
| M5 — function/boundary fidelity | Observed call capsules and input/file/audio/timing lanes feed proof pillars |
| M6 — scheduled convergence | Private Windows sweep emits proof/coverage health without committing licensed artifacts |
| M7 — preservation release | Complete certified behavior graph and configuration matrix satisfy CI-05 |

M0 is the critical path and should land before adding more global “verified” claims.
ST, CV static export, and GX method census may proceed in parallel after EP-01. Retail
drives themselves stay serialized because the game/proxy uses singleton state.

## 6. Workstream EP — evidence compiler and truthful ledger

### EP-00 — Freeze claim vocabulary

- **Reasoning:** R3 only.
- **Depends:** none.
- **Reads:** `tools/trace_studio_v3/orv3_sync.py`,
  `tools/trace_studio_v3/orv3_window.py`, `tools/scenario-test.py`,
  `tools/gen_port_ledger.py`, confirmed parity ledger.
- **Writes:** `docs/reference/parity-vocabulary.md`; decision section in proof schema.
- **Procedure:** enumerate every current use of aligned, verified, ported, exact,
  divergent, batching, phase-clean, and confirmed. Map each to one scoped term; identify
  compatibility output that must remain temporarily.
- **Acceptance:** examples distinguish identity join, same-side replay, cross-target
  equality, source inventory, runtime execution, and human confirmation. No term implies
  a stronger claim than its evidence.
- **Do not delegate:** naming errors would contaminate every downstream artifact.

### EP-01 — Approve contract and proof schemas

- **Reasoning:** R3 only for schema; R1 may transcribe approved JSON Schema and fixtures.
- **Depends:** EP-00.
- **Planned files:** `docs/schemas/parity-contract-v1.schema.json`,
  `docs/schemas/parity-proof-v1.schema.json`,
  `docs/reference/parity-proof-format.md`,
  `tools/test_parity_schema.py`.
- **Procedure:** freeze required fingerprints, pillar result shape, exception scope,
  canonicalization, schema evolution, and path redaction. Create one minimal valid, one
  full valid, and invalid fixtures for every required group.
- **Acceptance:** deterministic round-trip; unknown major version rejected; missing
  required evidence cannot validate as pass; no fixture contains proprietary bytes.
- **Negative test:** remove retail hash or mark required pixels `NOT_CAPTURED`; validation
  must fail the proof.

### EP-02 — Implement provenance fingerprinting

- **Reasoning:** R2; R3 reviews the complete input inventory.
- **Depends:** EP-01.
- **Planned files:** `tools/parity/__init__.py`, `tools/parity/fingerprint.py`,
  `tools/parity/environment.py`, `tools/test_parity_fingerprint.py`.
- **Procedure:** stream SHA-256 files; hash deterministic directory manifests as
  normalized relative path + size + file hash; fingerprint Git HEAD plus staged and
  unstaged patch bytes; collect PE/tool/config/environment metadata; never read outside
  explicitly supplied roots.
- **Acceptance:** same inputs produce same ID from different absolute directories; one
  changed byte changes the ID; dirty-tree changes are represented; permission/missing
  inputs fail closed.
- **R1 delegation:** fixture creation and path-normalization tests after API is fixed.

### EP-03 — Separate join and replay verdicts

- **Reasoning:** R2 with EP-00 names locked.
- **Depends:** EP-00.
- **Touches:** `orv3_sync.py`, `orv3_window.py`, v3 tests, docs.
- **Procedure:** emit `JOIN_COMPLETE`/`JOIN_PARTIAL`; preserve a deprecated
  machine-readable alias only if existing callers require it; make summary text explicit
  that no equality was tested; keep join exit behavior available behind a join-specific
  command.
- **Acceptance:** two identity-matched but pixel-different fixture captures report
  `JOIN_COMPLETE`, never parity pass. Existing pair construction remains byte-for-byte
  stable.

### EP-04 — Normalize observation adapters

- **Reasoning:** R2.
- **Depends:** EP-01, EP-03.
- **Planned files:** `tools/parity/observations.py`,
  `tools/parity/pixels.py`, `tools/parity/render_program.py`,
  `tools/test_parity_observations.py`.
- **Procedure:** read v3 pairs, replay outputs, pixel metrics, material/draw diff,
  call/state JSONL, audio sidecars, and save outputs into schema-versioned observation
  objects. Adapters validate frame identity and artifact hashes before comparing.
- **Acceptance:** adapters never infer pass from absent files; reordered or mismatched
  frame identities fail; exact pixel mode compares every required paired frame.
- **Mechanical split:** one R1/R2 agent per adapter, exclusive files, using shared
  fixtures.

### EP-05 — Build `parity_prove.py`

- **Reasoning:** R2 implementation; R3 owns final acceptance.
- **Depends:** EP-02, EP-04; state/audio/timing adapters may land later as
  `NOT_CAPTURED`.
- **Planned files:** `tools/parity_prove.py`, `tools/parity/prove.py`,
  `tools/test_parity_prove.py`.
- **CLI:** `parity_prove.py <scenario> --window OFFSET:COUNT --json`.
- **Procedure:** load contract; resolve or drive observations; validate fingerprints;
  evaluate each required pillar; write a temporary bundle; canonicalize; derive
  `proof_id`; atomically move to `runs/proofs/sha256/<first2>/<proof_id>/`.
- **Acceptance:** exit 0/1/2 follows §4.1; output includes first divergence and artifact
  paths; rerun with identical inputs returns identical proof ID; no absolute licensed
  paths enter hashed JSON.
- **Negative tests:** mutate one pixel, draw state, state field, save byte, and tool hash
  independently; each changes/fails the expected pillar.

### EP-06 — Redesign implementation/proof ledger

> **✅ LANDED 2026-07-16** — `../findings/parity-EP06-ledger-lifecycle.md`.
> `gen_port_ledger.py` now emits a two-axis lifecycle (INVENTORY
> `discovered→source-referenced→implemented→instrumented` from src markers;
> RUNTIME `retail-executed→…→matrix-proven` from `../parity-proof-index.json`,
> empty ⇒ `runtime_proven=0`). Acceptance met + gated by
> `tools/test_gen_port_ledger.py` (152 checks): a bare `FUN_` → `source-referenced`
> (not `implemented`); a probe → only `instrumented`; runtime rungs need a proof
> artifact. Legacy `status` kept as a DEPRECATED alias; `--check` idempotent. New
> opt-in `PORT-OF(0xVA)` attestation reaches `implemented` (0 seeded — author
> backfill pending). Human-confirmed rows stay separate (EP-07).

- **Reasoning:** R3 for lifecycle semantics; R2 generator implementation; R1 fixture
  backfill.
- **Depends:** EP-00, EP-05.
- **Touches:** `tools/gen_port_ledger.py`, `docs/STATUS.md`,
  `docs/port-ledger.*`.
- **Required states:** discovered, source-referenced, implemented, instrumented,
  retail-executed, port-executed, call-I/O-aligned, scenario-pillar-proven,
  matrix-proven. Store proof IDs and scope; do not collapse to one strongest global
  label.
- **Migration:** preserve old raw marker facts under explicit fields; do not fabricate
  proof for old entries. Human-confirmed rows remain separate evidence.
- **Acceptance:** adding `CALL_TRACE_ENTER` changes only `instrumented`; a runtime proof
  artifact is required for executed/aligned states; a `FUN_` comment cannot claim
  implementation.

### EP-07 — Bridge human confirmations

> **✅ LANDED 2026-07-17** — `../findings/parity-EP07-human-review.md`. R3 decision (of the
> two sanctioned options): **add `human_review` to `canonical.NON_HASHED`** rather than move
> it into the envelope — the smaller blast radius on the FROZEN schema (its SHAPE is unchanged;
> `human_review` stays a required first-class top-level field), confining the change to the
> canonicalization rule §4.4 already places under R3 authority, and making the code CONFORM to
> §4.4's stated intent ("proof_id excludes … human display notes"). R2 slice:
> `prove.py:attach_human_review(proof, review, *, required_pillars)` (additive, non-hashed —
> asserts review-neutrality under the CURRENT rule; a confirming review over a non-PASS gate is
> recorded `confirmed-despite-<MACHINE>`, stamped with `machine_verdict`, NEVER a silent pass) +
> the `tools/parity_review.py` CLI (writes the review back into the SAME content-addressed bundle;
> `required_pillars` auto-resolve from the bundle's scenario contract with a drift check; exit =
> the MACHINE gate). Acceptance met — a human confirmation cannot override a failed machine-required
> pillar (the CLI still exits 1 over a FAIL). VERIFIED end-to-end on the REAL `house-firstcust-arrprobe`
> bundle → `confirmed-despite-FAIL` (its honest sub-perceptual pixel FAIL). +host tests (test_parity_prove
> +18 checks incl. a stale-id regression; test_parity_schema asserts NON_HASHED membership + id neutrality).
> **NB one-time:** pre-EP-07 bundles' `proof_id`s are stale under the new rule (they hashed a
> `human_review:null`) ⇒ re-address on next drive (advisory; the durable key is `contract_sha256`; no
> bundle ever carried a real review). **DEFERRED (opt-in, not started):** the
> confirmed-parity-ledger → structured-review-records migration ("where practical", not a rewrite).

- **Reasoning:** R2; human review remains human-owned.
- **Depends:** EP-05.
- **Planned files:** proof-side review command and confirmed-ledger link convention.
- **Procedure:** attach reviewer, date, scope, notes, and proof ID without mutating the
  proof core; generate Markdown views from structured review records where practical.
- **Acceptance:** human confirmation cannot override a failed machine-required pillar;
  deferred divergences retain explicit failing/exception scope.

### EP-08 — Re-key and validate caches

> **✅ LANDED 2026-07-16** — `../findings/parity-EP08-cache-provenance.md`
> (commits `62ece6e` HOLE-2 close + `5713074` cache re-key). The v3 studio cache dir
> key is now a **128-bit** `sha256(common_provenance)+arm` over
> `{cache_schema, trace (⇒ {savefile} save), proxy, assets_manifest, recet.ini}`;
> per-side `{pe_sha, agent_sha}` live in `v3meta.prov` and are validated on lookup
> (`side_provenance` + `_staleness`), so changing port PE → port only, retail PE/agent
> → retail only, proxy/assets/config/trace/schema → both, and a port fix still never
> invalidates the retail cache. Corrupt (missing/empty container) rejected; every stale
> decision logged. Acceptance met + gated by `test_orv3.test_provenance_keying`. HOLE-2
> closed in tandem (view.json bakes container hashes; `parity_prove` threads
> `source`/`expected_containers`; `test_parity_prove.test_container_provenance`).
> Residual (logged): the proof `tools` group is still current-on-disk — thread
> `v3meta.prov` into `gather_provenance` (EP-02/EP-05 follow-up, disclosed by a caveat).

- **Reasoning:** R2; R3 audits determining inputs.
- **Depends:** EP-02.
- **Touches:** `v3cache.py`, save resolver, relevant tests.
- **Procedure:** key by full provenance manifest; use at least 128 visible hash bits;
  validate cached save bytes against the name/hash; record cache schema/tool versions;
  explain every stale decision.
- **Acceptance:** changing port/retail PE, resolved save, config, proxy, agent, assets,
  capture flags, or environment invalidates the appropriate side. Corrupt cache is
  rejected, not silently reused.

## 7. Workstream ST — hierarchical state and save equivalence

### ST-00 — Define canonical state model

- **Reasoning:** R3 only for field selection/normalization.
- **Depends:** EP-01.
- **Reads:** save-working-arena findings, `save_bank.h`, flow fields, dialogue/tutorial
  VMs, scene globals, entity arrays.
- **Writes:** `docs/reference/canonical-state.md`,
  `docs/schemas/state-map-v1.json`.
- **Procedure:** define named regions, scalar types, array lengths, identities, ignored
  padding, pointer/handle normalization, float bit policy, and scene applicability.
  Separate persistent, volatile deterministic, environmental, and unknown bytes.
- **Acceptance:** every included byte has meaning and ownership; excluded bytes have a
  reason; roots can be expanded to a field-level diagnostic.
- **Stop:** do not let an R1 agent guess struct sizes or normalize an unexplained byte.

### ST-01 — Verify save inputs and compare save outputs

- **Reasoning:** R2.
- **Depends:** EP-02; may begin before ST-00 for whole-file exactness.
- **Touches:** `tools/trace_save.py`, `scenario-test.py` or proof adapter, tests.
- **Procedure:** rehash decompressed content-addressed saves; enumerate sandboxed writes
  with sequence, path class, size, and SHA-256; pair retail/port writes by semantic
  operation; compare bytes and report first offset plus named region when available.
- **Acceptance:** corrupt blob/name mismatch rejected; zero/multiple writes handled;
  missing, extra, reordered, truncated, failed, and byte-divergent writes produce
  distinct diagnostics.
- **Negative test:** flip one byte in a sandboxed output and identify its offset.

### ST-02 — Implement canonical encoder and Merkle roots

- **Reasoning:** R2 with ST-00 locked.
- **Depends:** ST-00.
- **Planned files:** `tools/parity/state_codec.py`,
  `tools/parity/state_merkle.py`, tests and synthetic fixtures.
- **Tree:** root → persistent/volatile → subsystem → collection/object → field/chunk.
- **Procedure:** encode integer bytes explicitly; preserve f32/f64 bit patterns; sort
  keyed records; retain ordered arrays; include schema/version in every hash domain.
- **Acceptance:** same semantic state at different addresses hashes equally; one field
  mutation reports the exact leaf path; pointer/padding fixture changes do not alter a
  root when declared excluded.

### ST-03 — Expand retail and port state capture

- **Reasoning:** R2 plumbing; R3 provides each address/schema mapping.
- **Depends:** ST-00, ST-02.
- **Touches:** Frida agent, port trace emitter, v3 state capture, tests.
- **Procedure:** emit canonical raw values at semantic anchors and optionally every kept
  frame; batch reads by contiguous region; include capture status per field; gate heavy
  arrays by requested schema groups.
- **Acceptance:** both sides emit the same schema/version and logical object IDs; absent
  scene fields are explicit; capture overhead measured; current four-field state panel
  remains compatible through an adapter.
- **R1 delegation:** add pre-specified address/type rows and fixtures only.

### ST-04 — First-divergence state report

> **✅ LANDED 2026-07-17** — `../findings/parity-state-producer.md` §"ST-04 LANDED".
> `tools/parity/state_diff.py` (pure core) + CLI `tools/state_diff.py` +
> `tools/test_state_diff.py` (43 checks). Emits the first divergent logical frame,
> leaf root path + schema type, typed values, raw canonical bits, last matching
> frame, the value TRANSITION (state-derivable provenance: port-MISSED / SPURIOUS /
> WRONG), and every co-divergent leaf; new `all_divergent_leaves` in the Merkle
> layer. Acceptance met: synthetic mutations at leaf / co-divergent / presence /
> head-of-window levels localize; stable JSON + short text; §4.1 exit codes (a test
> proves the verdict agrees with `adapt_state`). The "call provenance" (WRITER
> callsite/owner) is ST-05's domain — a `provenance:null` seam is left for it.
> VERIFIED on real captures (house-pause-save-commit PASS 200/200; arrprobe FAIL @
> companion/cx with raw bits).

- **Reasoning:** R2.
- **Depends:** ST-01 through ST-03.
- **Planned files:** `tools/state_diff.py` or parity subcommand, tests.
- **Output:** first divergent logical frame, root path, typed values, raw bits, last
  matching frame, and nearby mutation/call provenance.
- **Acceptance:** synthetic mutations at every tree level localize correctly; report is
  stable JSON plus short text; exit codes follow §4.1.

### ST-05 — Define and capture semantic mutations

> **✅ CONSUMER + DESIGN LANDED 2026-07-17** — `../reference/state-mutations.md`;
> schema `../schemas/state-mutation-v1.json`; `tools/parity/state_mutation.py` +
> `tools/test_state_mutation.py` (44 checks). Per rule 11 the CONSUMER lands before
> the platform: the R3 mutation model (event shape + semantic/derived/noise class
> gate + the grounded event catalog) + the consumer that RECONSTRUCTS a subtree
> (idempotent/dedup), localizes the FIRST WRONG WRITE (cumulative per-frame value,
> shared-start recovered from a write's `old`), enforces the **first-wrong-write ≤
> first-state-root-divergence** ordering invariant (the ST-04/ST-05 link), and fills
> ST-04's `first_divergence.provenance` seam (`state_diff.py --mutations`, host-tested
> end-to-end). Acceptance met on the consumer axis. **DEFERRED remainder:** the Frida
> post-write / TTD CAPTURE PLATFORM (named writers/hooks emitting `state-mutation.json`)
> — lands when a scenario needs the provenance; owners are `attested-at-capture` (only
> `save_slot_commit → FUN_004905a8` certain today).

- **Reasoning:** R3 designs mutation semantics; R2 implements named writers/hooks.
- **Depends:** ST-00, ST-04.
- **Events:** gold, inventory quantity/slot, shop placement, day/time, flags, customer
  closeness, news, loot, HP/status, VM PC/stack, save-slot commit.
- **Procedure:** prefer post-write observation at known owners; use TTD/memory-watch to
  discover unknown writers; emit old/new typed values and callsite/owner.
- **Acceptance:** mutation streams reconstruct selected state subtrees; duplicate
  observations do not double-apply; first wrong write precedes or equals first state-root
  divergence.
- **R3 gate:** decide whether a change is semantic, derived, or noise.

### ST-06 — Scene-by-scene state-map expansion

- **Reasoning:** R1 capture/enumeration plus R3 mapping review.
- **Depends:** ST-03.
- **Order:** title/config → shop/economy → town → dungeon/combat → scripted events.
- **Procedure:** run two retail instances of the same certified edge, inspect unknown
  changing regions, map owners/readers, propose fields, then obtain R3 approval before
  adding them to canonical state.
- **Acceptance:** coverage atlas records schema groups captured per scenario; no global
  “state pass” if a required scene group is absent.

## 8. Workstream CV — static/dynamic coverage atlas

### CV-00 — Freeze coverage ontology and reachability semantics

- **Reasoning:** R3 only.
- **Depends:** EP-01.
- **Decisions:** block boundaries, thunk/external treatment, indirect edges, exception
  flow, observed vs inferred reachability, VM operand classes, content identity, and
  scenario aggregation.
- **Acceptance:** examples cover direct/indirect calls, switch tables, dead CRT/library
  code, scene-specific VMs, and data-driven content. No “percentage” mixes dimensions.

### CV-01 — Export an offline Ghidra index

- **Reasoning:** R2 once CV-00 schema is fixed.
- **Depends:** CV-00.
- **Touches:** new headless Ghidra script(s), `tools/ghidra-headless.sh`, fixture tests.
- **Output:** `re-index.sqlite` or deterministic JSON tables for functions, blocks,
  flows, calls, xrefs, global reads/writes, strings, switch cases, symbols/types, sizes,
  and byte hashes.
- **Procedure:** extend one-shot export; never start a resident Ghidra service; use
  transactions/batched inserts; record Ghidra/project/executable versions.
- **Acceptance:** known functions and indirect/switch examples query correctly; repeated
  export is deterministic after volatile metadata removal; peak memory stays bounded.
- **Reference:** Ghidra `BasicBlockModel`/flow APIs, not decompiled-text parsing.

### CV-02 — Build coverage database and query API

- **Reasoning:** R2.
- **Depends:** CV-01.
- **Planned files:** `tools/coverage_atlas.py`, `tools/coverage_db.py`, tests.
- **Tables:** static entities/edges, scenarios, runs, observations, implementations,
  proof scopes, content dimensions, and provenance.
- **Queries:** observed-retail/unimplemented-port; implemented/unexecuted; executed/no
  proof; uncovered successors of covered blocks; content/VM/config gaps; scenario
  coverage delta.
- **Acceptance:** import is idempotent; every row traces to an artifact hash; SQL/API
  results stable under path relocation.

### CV-03 — Collect retail dynamic block/edge coverage

- **Reasoning:** R3 designs/calibrates; R2 implements.
- **Depends:** CV-00, CV-02.
- **First candidate:** existing Frida Stalker, window-gated and aggregated to a bitmap.
  WinAFL/DynamoRIO is a fallback only if measured Stalker overhead/coverage loss is
  unacceptable.
- **Procedure:** prove module filtering and address normalization; run
  uninstrumented-versus-covered observer test; collect block starts and transitions;
  include lost-event/overflow counters.
- **Acceptance:** a tiny known path matches static CFG expectations; repeat runs under
  identical pinned input yield identical logical coverage or explained hook-only deltas;
  overhead and missed-event bounds documented.
- **R3 stop:** sampling/transform choices that can erase short blocks or indirect edges.

### CV-04 — Add scenario coverage declarations

- **Reasoning:** R1 after schema/examples are approved.
- **Depends:** CV-00 and scenario schema support.
- **Procedure:** add stable IDs/descriptions/start node; initially import observed
  coverage automatically. Hand-declared expectations are limited to intentional
  behavior/content, not thousands of code blocks.
- **Acceptance:** all scenarios validate; omission remains unknown; no bulk claim is
  inferred from directory names.

### CV-05 — Instrument semantic coverage dimensions

- **Reasoning:** R2 per subsystem; R3 maps ambiguous IDs/opcodes.
- **Depends:** CV-00, ST-03.
- **Targets:** dialogue/tutorial opcode + operand class, scene transitions, table/content
  IDs, asset loads, audio IDs, save operations, boundary outcomes.
- **Procedure:** emit normalized IDs on both sides with frame identity; import into atlas;
  cross-check table bounds against static data.
- **Acceptance:** one representative of every already-reached subsystem appears; invalid
  IDs fail validation; events retain scenario/proof provenance.

### CV-06 — Executed-but-unimplemented and branch-gap reports

- **Reasoning:** R2.
- **Depends:** CV-02, CV-03, EP-06.
- **Output:** ranked JSON/Markdown with VA, owning function, first scenario/frame/caller,
  downstream uncovered edges, implementation/proof state.
- **Acceptance:** known reached stub and known ported-unreached function appear in the
  correct lists; indirect targets are not silently dropped.

### CV-07 — Next-experiment prioritizer

- **Reasoning:** R3 freezes scoring; R2 implements.
- **Depends:** CV-02 through CV-06.
- **Inputs:** new blocks/edges, new semantic content, distance from certified node,
  runtime cost/flakiness, port readiness, and proof deficit.
- **Output:** ranked candidate edge/scenario with an explanation, never an opaque score.
- **Acceptance:** fixture rankings obey policy; scoring weights/version recorded in
  output; active human-selected front can override without falsifying coverage.

### CV-08 — Calibrate coverage truth

- **Reasoning:** R3.
- **Depends:** CV-03, CV-07.
- **Procedure:** compare Stalker against call traces, static CFG, TTD, and repeated runs
  on representative direct/indirect/switch/exception paths. Document blind spots.
- **Acceptance:** coverage claims include collection mode and confidence; no dashboard
  publishes a global percentage until calibration passes.

## 9. Workstream GX — D3D8 capture completeness

### GX-00 — Device/resource method census

> **✅ STATIC + DYNAMIC census LANDED 2026-07-17 + first live verdict** —
> `../findings/gx00-d3d-method-census.md`; census `../schemas/d3d8-method-census-v1.json`;
> `tools/parity/d3d_census.py` + `tools/d3d_census.py` + `tools/test_d3d_census.py` (63
> checks). **STATIC:** all 113 vtable methods (IDirect3D8 16, IDirect3DDevice8 97) classified
> recorded (23) / wrapper (6) / query_only (45) / forwarded_irrelevant (6) /
> **render_affecting_unsupported (33 — the fail-closed RISK set)**; whole roadmap high-risk
> seed list confirmed forwarded-uncaptured; drift guard asserts `proxy_generated.h`'s
> recorded/forwarded split matches the census (acceptance met). **DYNAMIC:** `gen_forwarders.py`
> instruments all 84 `fwd_` thunks with a process-lifetime `InterlockedIncrement` (zero
> hand-edits; recorded methods captured-by-construction); the proxy emits `v3cap.census.json`
> per kept frame (threaded through the v3 cache). **First verdict — `house-firstcust-arrprobe`
> [1,80] → VIOLATION both sides:** `CreateVertexBuffer`+`CreateIndexBuffer` are
> forwarded-uncaptured (retail 130× / port 13× each), the other **31/33 risk methods
> 0-observed** — a SURGICAL resource-creation gap = the GX-03/GX-04 hinge. Content IS
> snapshotted late (`snap_vb`/`snap_ib`); the residual risk is same-frame re-mutation.
> Sharpens arrprobe's M0 honest-FAIL with a concrete mechanism (not the expected SAFE — the
> census earns its keep on our most-confirmed scene).

- **Reasoning:** R2; R1 can generate forwarder tables/tests.
- **Depends:** EP-02 for provenance, otherwise independent.
- **Touches:** `tools/trace_studio_v3/proxy/d3d8_proxy.c`,
  `proxy_generated.h`, `format/orv3_format.h`, `replay/replay_core.c`, and inspector.
- **Procedure:** count every `IDirect3D8`, device, resource, and surface method; classify
  call as recorded, forwarded-proven-irrelevant, query-only, or unsupported.
- **Initial high-risk forwarded list to verify:** `Reset`, `SetGammaRamp`,
  `UpdateTexture`, `MultiplyTransform`, `SetViewport`, `SetClipPlane`, state-block
  begin/end/capture/apply/delete, palette methods, `ProcessVertices`, vertex-shader
  constants, pixel shader creation/bind/constants. This is a seed list, not an assertion
  that retail reaches every method.
- **Acceptance:** all vtable slots represented; scenario capture emits census; wrapper
  and generated-forwarder lists cannot drift unnoticed.

### GX-01 — Approve record-or-fail policy

> **◐ GATE MECHANISM LANDED 2026-07-17 (POST-HOC form)** —
> `../findings/gx00-d3d-method-census.md` §"Dynamic census". `d3d_census.py --dynamic
> <v3cap.census.json>` is the record-or-fail gate: exit 0 SAFE (every risk method
> 0-observed), 1 VIOLATION (a render_affecting_unsupported method fired — names
> method+count+subgroup), 2 INCONCLUSIVE (sidecar incomplete/drift, fail-closed).
> Acceptance met via the negative test (a deliberate `SetViewport` cannot pass as a
> complete capture). **REMAINING:** (a) the STRICTER in-proxy form — hard-terminate capture
> on the FIRST render-affecting forwarded call, with args/frame/caller (needs a proxy build);
> (b) the R3 policy call on wiring the census as a hard PRECONDITION on the pixels/
> render_program pillars in `parity_prove` (blast radius: every 3D scene creates VB/IB ⇒
> would flip them INCONCLUSIVE until GX-04) — deferred with GX-03/GX-04.

- **Reasoning:** R3 for classification; R2 implementation.
- **Depends:** GX-00.
- **Procedure:** define which methods can alter future pixels/resources/device state.
  Unsupported render-affecting calls terminate capture with method, arguments, frame,
  and caller context. Queries may be logged without replay if their returned value is
  proven irrelevant.
- **Acceptance:** deliberate `SetViewport`/state-block/shader-constant fixture cannot
  produce a successful incomplete capture.

### GX-02 — Implement observed missing methods incrementally

- **Reasoning:** R2 per method, R3 if semantics/inheritance are unclear.
- **Depends:** GX-01.
- **Order:** only methods observed by the census. Add container opcode, recorder,
  replayer, inspector, corruption tests, and same-side exact fixture as one unit.
- **Acceptance:** new method fixture replays exact; older container versions remain
  readable or fail with explicit migration message.

### GX-03 — Specify per-draw resource versions

- **Reasoning:** R3 only.
- **Depends:** GX-00.
- **Problem:** unwrapped resources snapshotted at frame end can lose contents used before
  a same-frame `Lock`/`Unlock`, copy, or render-target mutation.
- **Decision:** define resource identity, generation, dirty regions, lock flags, partial
  updates, pointer reuse, render-target writes, and lifetime/release behavior.
- **Acceptance:** a synthetic two-draw frame with one intervening mutation has two
  distinct bound versions and a deterministic container representation.

### GX-04 — Wrap/version resources

- **Reasoning:** R2 under GX-03; R3 reviews COM lifetime correctness.
- **Depends:** GX-03.
- **Procedure:** wrap relevant VB/IB/texture/surface interfaces; increment generations on
  mutation; bind generation IDs at draw/copy; snapshot only referenced dirty generations;
  preserve QueryInterface/AddRef/Release identity.
- **Acceptance:** same-frame mutation fixture, partial lock fixture, pointer reuse, lost
  device/reset, and existing title/HOUSE replay all pass.

### GX-05 — Harden deduplication and corruption detection

- **Reasoning:** R1/R2.
- **Depends:** GX-04.
- **Procedure:** replace hash-only FNV equivalence with SHA-256 or hash-plus-byte-compare;
  include size/type/format in domain; validate offsets/counts/opcodes when reading.
- **Acceptance:** forced hash collision fixture remains distinct; truncated/corrupt
  containers fail safely.

### GX-06 — Graphics capture regression corpus

- **Reasoning:** R1 fixture maintenance; R3 approves coverage set.
- **Depends:** GX-02, GX-04.
- **Corpus:** title 2D, HOUSE static/dynamic geometry, render targets, state inheritance,
  dynamic resources, reset/display changes, late-game effects as discovered.
- **Acceptance:** every observed render-affecting method has at least one fixture and one
  real scenario proof.

## 10. Workstream BA — behavior atlas and guided exploration

### BA-00 — Define behavior node/edge identity

- **Reasoning:** R3 only.
- **Depends:** EP-01, ST-00, CV-00.
- **Node:** semantic anchor + persistent state root + required volatile state root + RNG
  state/draw count + configuration + retail build.
- **Edge:** ordered semantic/raw input segment + completion condition + normalization
  policy.
- **Decisions:** what can be restored safely, when a node is equivalent, how load seams
  and nondeterministic scheduling are represented, and how cycles are stored.
- **Acceptance:** examples cover menu choice, shop sale, day transition, dungeon branch,
  death/retry, save/reload, and a loop without conflating distinct state.

### BA-01 — Implement content-addressed atlas store

- **Reasoning:** R2.
- **Depends:** BA-00, EP-02.
- **Planned files:** `tools/behavior_atlas.py`, `tools/atlas/`, tests.
- **Store:** committed graph metadata contains no licensed payloads; local CAS contains
  saves/captures; nodes/edges reference hashes and proof IDs.
- **Acceptance:** import is idempotent; common prefixes dedupe; graph survives relocation;
  missing local licensed artifact is reported as unavailable, not corrupt.

### BA-02 — Import existing scenarios

- **Reasoning:** R1 with R3 review of node boundaries.
- **Depends:** BA-01, CV-04.
- **Procedure:** map each scenario to start/end anchors, trace hash, save ref, config, and
  known branch intent; do not infer state equality for old traces.
- **Acceptance:** every active scenario is indexed; duplicates/common prefixes reported;
  unclassifiable traces carry `needs_r3_boundary`.

### BA-03 — Build atlas traversal runner

- **Reasoning:** R2.
- **Depends:** BA-01, EP-05.
- **Procedure:** resolve start node, materialize licensed local state, run edge on target,
  wait for completion contract, capture proof, validate end node, and atomically record
  result. Reuse certified prefixes where safe.
- **Acceptance:** two-edge branch fixture runs retail/port; failure identifies first edge;
  crash/timeout leaves no partially certified node.

### BA-04 — Define action grammars

- **Reasoning:** R3 designs per scene; R2 implements; R1 enumerates menu/content values.
- **Depends:** BA-00, CV-05.
- **Grammars:** title/menu navigation, dialogue choice/advance, shop placement/customer
  service, town movement/facility menus, dungeon movement/combat/inventory.
- **Rule:** actions include preconditions and semantic completion, while retaining the
  exact raw mask sequence used.
- **Acceptance:** generated actions cannot press impossible UI options silently; raw
  replay reproduces the semantic edge.

### BA-05 — Coverage-guided scheduler

- **Reasoning:** R3 policy/algorithm; R2 implementation.
- **Depends:** BA-03, BA-04, CV-07.
- **Procedure:** select certified start node and grammar actions; favor new retail
  blocks/edges/content/transitions; run retail first to learn reachability, then port;
  stop at first state/proof divergence; bound time and state explosion.
- **Acceptance:** toy graph reaches known rare branch faster than uniform random; every
  choice and score is logged/reproducible.

### BA-06 — Trace/edge minimizer

- **Reasoning:** R2.
- **Depends:** BA-03, BA-04.
- **Procedure:** hierarchical delta-debug: action chunks → waits → repeated inputs → raw
  frame changes. Preserve start node, completion, and the same first-divergence signature.
- **Acceptance:** injected divergence trace shrinks while proof ID of divergence class
  remains stable; nondeterministic reproduction yields `INCONCLUSIVE`, not a false min.

### BA-07 — RNG callsite map and seed solver

- **Reasoning:** R3 algorithm/branch interpretation; R1 can run bounded sweeps after
  specification.
- **Depends:** CV-03, CV-05, BA-03.
- **Procedure:** associate each retail RNG draw with caller, logical frame, semantic
  consumer, and downstream branch; use the known LCG to invert/advance states; solve or
  enumerate seeds for target predicates; validate in retail.
- **Acceptance:** reproduce several known seed-sensitive decisions and one previously
  uncovered branch; count/order/value all match, not count alone.
- **Stop:** R1 cannot label an unexplained mismatch phase or change predicate semantics.

### BA-08 — Full-atlas health report

- **Reasoning:** R2 report; R3 interprets release risk.
- **Depends:** BA-03 onward.
- **Output:** certified/unproven edges, unreachable claims, flaky nodes, proof age,
  coverage deltas, configuration/seed gaps, and first failure per traversal.
- **Acceptance:** no single “percent complete” hides dimensions; every summary count
  links to node/edge/proof records.

## 11. Workstream CC — observed call capsules and hybrid validation

### CC-00 — Define capsule ABI and memory model

- **Reasoning:** R3 only.
- **Depends:** EP-01, ST-00, CV-01.
- **Schema:** target/caller VA, ABI/registers/stack args, pointed-object identities,
  relevant prestate, return, ordered writes, external calls, poststate, relocation map,
  scenario/frame/proof provenance.
- **Decisions:** aliasing, pointer relocation, bounded object graphs, x87 state,
  callbacks, exceptions, allocator/handle dependencies.
- **Acceptance:** examples cover pure leaf, known globals, in-place struct mutation,
  RNG consumer, and an explicitly unsupported OS-coupled call.

### CC-01 — Capture known-write-set calls

- **Reasoning:** R2.
- **Depends:** CC-00.
- **Extends:** existing `diff_test.py`/Frida RPC pattern and E.4 plan.
- **Procedure:** snapshot declared inputs, call on frozen/suspended retail, capture
  return/declared writes, restore in `finally`; mirror through host adapter.
- **Acceptance:** at least five varied real call capsules replay bit-exact; exception
  path restores retail state; race status explicit.

### CC-02 — Generate host adapters and corpus tests

- **Reasoning:** R2; R1 may add schema-prescribed targets.
- **Depends:** CC-01.
- **Procedure:** generate or template marshaling code, relocation tables, fixture loader,
  and comparison; compile into existing host diff library; mutate boundaries around
  observed vectors.
- **Acceptance:** adding a simple known-write leaf requires only a descriptor plus port
  symbol; deliberate return/write/order mismatches are detected.

### CC-03 — Expand observed capsule corpus

- **Reasoning:** R1 collection; R3 chooses targets/interprets failures.
- **Depends:** CC-02, CV-06.
- **Priority:** retail-executed unproven leaves on the active path, then high fan-in
  helpers and rare branch owners.
- **Acceptance:** each corpus addition records scenario/caller distribution and proof;
  no random synthetic state is called “retail-observed.”

### CC-04 — Unknown write-set capture through TTD

- **Reasoning:** R3 query/semantics; R2 may implement approved export.
- **Depends:** CC-00 and a concrete target Tier 1 cannot express.
- **Procedure:** query call interval, registers, memory writes, threads/events; normalize
  writes into capsule object identities; cap bytes/calls; compare against Frida/manual
  sample.
- **Acceptance:** one stateful function with unknown writes produces a repeatable,
  consumed capsule and exposes a real or seeded divergence. Do not expand TTD before
  this consumer exists.

### CC-05 — Hybrid retail function replacement

- **Reasoning:** R3 only for design and first uses.
- **Depends:** CC-00, CC-02, stable calling convention and rollback.
- **Procedure:** inject a port DLL or bridge; replace exactly one retail target; preserve
  x87/stack/register/exception behavior; log entry/exit; restore original atomically.
- **Acceptance:** identity replacement is behavior-neutral across repeated captures;
  deliberate wrong replacement causes expected first state divergence; crash cleanup
  restores process/environment.

### CC-06 — Feed capsule results into proof/ledger

- **Reasoning:** R2.
- **Depends:** CC-02, EP-06.
- **Acceptance:** a capsule proves only its observed ABI/state domain and callers; ledger
  links all capsule IDs rather than promoting the function globally.

## 12. Workstream BT/AU/RT — system boundary, audio, and real time

### BT-00 — Define boundary-event schema

- **Reasoning:** R3 only.
- **Depends:** EP-01.
- **Events:** DirectInput/COM, Win32 messages/focus, file/INI, save replace/rename,
  DirectMusic/DirectSound, movie playback, mutex/window/device lifecycle.
- **Fields:** logical frame/time, thread, API/method, normalized args/result, relevant
  buffer hash/size, side effects, caller module/VA.
- **Acceptance:** schema distinguishes call-sequence equivalence, result equivalence, and
  external effect equivalence without storing licensed payloads.

### BT-01 — Instrument input, file, INI, Win32, and COM boundaries

- **Reasoning:** R2 per boundary after BT-00; R3 for unexplained semantics.
- **Depends:** BT-00.
- **Procedure:** add retail hooks and matching port logs; normalize handles/pointers/path
  roots; capture failures/retries, not just success; window-gate high-volume calls.
- **Acceptance:** title→shop trace yields paired event streams; current cooperative-mode,
  multi-poll, reacquire, and numeric-INI differences appear explicitly.

### BT-02 — Add external-input conformance lane

- **Reasoning:** R3 designs safety/semantics; R2 implements approved mechanism.
- **Depends:** BT-01.
- **Procedure:** drive OS-visible keyboard/controller input rather than writing the
  decoded mask; exercise acquire/loss, focus, repeats, simultaneous keys, multiple polls,
  alt-tab/minimize/restore. Prefer user-mode mechanisms; virtual HID/kernel work requires
  separate approval.
- **Acceptance:** raw boundary events and final engine masks match for a matrix of cases;
  deterministic internal-mask TAS remains unchanged as the simulation lane.

### BT-03 — Configuration and environment matrix

- **Reasoning:** R1 enumeration/execution; R3 approves dimensions and equivalence.
- **Depends:** BT-01.
- **Matrix:** supported resolutions, window/fullscreen, FPS setting, sound/music levels,
  fresh/missing/corrupt/max saves, locale/codepage, keyboard/controller, focus/device
  loss, movie present/missing.
- **Acceptance:** each cell has explicit supported/unsupported status and proof IDs;
  pairwise reduction may optimize runs but cannot erase named high-risk combinations.

### AU-00 — Integrate audio events with v3 identity

- **Reasoning:** R2.
- **Depends:** EP-04, BT-00.
- **Procedure:** emit SE/BGM/play/stop/volume/pan/loop/fade events with logical frame;
  pair by anchor identity/order; include audio sidecar in proof bundle; do not ignore
  fades by default.
- **Acceptance:** missing/extra/reordered/timing/parameter differences are distinct;
  existing identity/count comparator remains available as a legacy diagnostic only.

### AU-01 — Capture per-process PCM

- **Reasoning:** R2; R3 reviews capture point and alignment.
- **Depends:** AU-00.
- **First approach:** WASAPI process loopback on the Windows host; consider a
  DirectMusic/DirectSound tap if loopback adds nondeterminism.
- **Procedure:** record format/device metadata, align by an emitted sync/event, trim only
  contract-approved silence, store local WAV and proof hash/metrics.
- **Acceptance:** same-side repeated capture has quantified stability; injected sample
  shift/amplitude/channel changes are detected; licensed audio stays out of Git.

### AU-02 — Define audio equality metrics

- **Reasoning:** R3 only.
- **Depends:** AU-00, AU-01 measurements.
- **Decision:** exact samples where backend permits; otherwise event-exact plus approved
  latency/alignment and waveform/spectral metrics. Thresholds are environment-scoped and
  evidence-backed.
- **Acceptance:** known audible and inaudible perturbations calibrate policy; no generic
  perceptual score can hide a wrong event sequence.

### RT-00 — Collect real-time presentation telemetry

- **Reasoning:** R2.
- **Depends:** EP-02, BT-00.
- **First approach:** PresentMon ETW data plus engine anchors and boundary events.
- **Fields:** CPU/GPU/display duration, present mode/status, dropped/late frames,
  simulation ticks, load completion, focus, input/audio event times.
- **Acceptance:** retail/port same-host runs align by semantic events; missing ETW fields
  are explicit; deterministic turbo lane remains unaffected.

### RT-01 — Define cadence/latency contracts

- **Reasoning:** R3 only.
- **Depends:** RT-00 repeated data.
- **Decisions:** sequence vs distribution claims, warmup/load regions, FPS modes,
  acceptable OS noise, input-to-sim/present/audio latency, dropped/catch-up behavior.
- **Acceptance:** deliberately inserted frame delay, extra sim tick, and delayed audio
  each fail the intended metric; normal same-side variance does not produce arbitrary
  exceptions.

### RT-02 — Measure instrumentation bias

- **Reasoning:** R1 runs fixed matrix; R3 adjudicates.
- **Depends:** RT-00, representative ST/AU capture.
- **Matrix:** uninstrumented, Frida only, proxy only, Frida+proxy; repeated retail and
  port trials.
- **Compare:** anchors, state roots, RNG, boundary/audio events, and presentation timing.
- **Acceptance:** every authoritative observation lists observer mode; biased tools are
  restricted to pillars they do not perturb or corrected with an R3-approved method.

### BT-04 — Remaining experience boundaries

- **Reasoning:** R3 scopes behavior; R2 implements.
- **Depends:** BT-01 through RT-01.
- **Targets:** opening movie decode/presentation/audio timing, custom configuration tool
  compatibility, window lifecycle, device reset/loss, suspend/resume, unusual save I/O
  failures.
- **Acceptance:** behavior atlas contains explicit edges and contracts rather than
  silently skipping these as non-gameplay.

## 13. Workstream CI — automation and preservation release

### CI-00 — Freeze test taxonomy and gating policy

- **Reasoning:** R3 only.
- **Depends:** EP-01.
- **Tiers:** pure host; schema/tool unit; synthetic integration; stable licensed parity;
  frontier parity; real-time/config matrix; full atlas.
- **Decisions:** trigger frequency, required environment, artifact retention, timeout,
  flaky policy, and who may bless/change a contract.
- **Acceptance:** no unlicensed CI attempts retail parity; no flaky test is converted to
  pass by retry without retaining failures.

### CI-01 — Run host, Python, and documentation checks

- **Reasoning:** R1.
- **Depends:** CI-00 only for final grouping; can land immediately.
- **Touches:** pre-commit and `.github/workflows/nightly.yml`.
- **Commands:** C host suite, Python unit suite, documentation consistency, proprietary
  byte guard, x87 guard.
- **Acceptance:** deliberate failing C/Python/doc fixture blocks the correct lane; source
  changes cannot bypass tests due to file-filter mistakes.

### CI-02 — Add private/self-hosted Windows parity runner

- **Reasoning:** R2; R3 reviews security/licensed-data boundary.
- **Depends:** EP-05, CI-00.
- **Procedure:** runner owns local licensed assets and exact retail executable; jobs
  receive only source/contracts; serialize retail/proxy execution; upload proof manifests,
  hashes, metrics, and logs but no proprietary payloads.
- **Acceptance:** one stable scenario completes unattended; cleanup kills processes and
  preserves user saves; artifact scan proves no licensed bytes uploaded.

### CI-03 — Schedule tiered sweeps

- **Reasoning:** R2.
- **Depends:** CI-02, BA-08.
- **Cadence:** per-change fast tests; stable proof subset at arc boundaries; nightly
  frontier/coverage; periodic atlas/config/real-time sweep.
- **Acceptance:** changed dependencies invalidate relevant proofs; failures point to
  first edge/pillar; unchanged retail artifacts reuse validated caches.

### CI-04 — Generate proof/coverage health views

- **Reasoning:** R1 implementation after metrics fixed; R3 approves interpretation.
- **Depends:** EP-06, CV-07, BA-08.
- **Views:** proof age, pillar deficits, uncovered retail observations, flaky edges,
  exception/debt inventory, configuration matrix, first failing traversal.
- **Acceptance:** all numbers drill to immutable artifacts; no blended completion
  percentage.

### CI-05 — Define and execute preservation-release gate

- **Reasoning:** R3 only.
- **Depends:** every workstream.
- **Required decision record:** supported reference build/environment; behavior-atlas
  completeness argument; static/dynamic coverage residuals; all exceptions/debts;
  same-host pixel/audio/timing results; save/input/config compatibility; reproducibility
  instructions.
- **Acceptance:** independent fresh environment with legitimate game files rebuilds the
  port and regenerates the selected proof set; every excluded retail behavior has a
  justified classification, not merely lack of observation.

## 14. Recommended execution order

### Wave 0 — make current evidence honest

1. EP-00 and EP-01 with an R3 model.
2. EP-02, EP-03, and schema fixtures in parallel where write sets do not overlap.
3. EP-04 and EP-05.
4. R3 adversarial review of one proof bundle.
5. EP-06 ledger migration and EP-08 cache hardening.

**Status (2026-07-16):** steps 1–4 **DONE**. Steps 1–3 landed EP-00→EP-05; step 4
(this review, `../findings/parity-M0-adversarial-review.md`) compiled the first real
bundle over `house-firstcust-arrprobe` (first corpus contract), demonstrated all
three M0 exit conditions on real evidence, and found+fixed a `proof_id` portability
leak (an absolute path in a hashed pillar note/detail — now scrubbed by
`observations.portable_reason`, guarded by `test_parity_prove.test_proof_id_portable`).
Two gate prerequisites logged: **HOLE-2** — `parity_prove` never threads
`expected_containers`, so a foreign metrics doc with matching frame identities would
be trusted; **no pixels/state producer may ship a PASS-capable adapter until EP-08
provides a real capture-container hash to thread**. Step 5 (EP-06/EP-08) is next.

Do not start BA exploration or publish new global coverage percentages before this
wave. Existing gameplay porting may continue, but new parity claims should retain
scenario-specific evidence.

### Wave 1 — expose invisible divergence and unknown reachability

1. ST-00 → ST-04, with ST-01 early.
2. CV-00 → CV-02 static index.
3. GX-00 → GX-01 method tripwire.
4. In parallel after schemas: CV-03 spike and ST-03 capture expansion.
5. Finish M1/M2/M3 negative tests before corpus-scale runs.

### Wave 2 — turn traces into a convergence graph

1. CV-04 through CV-07.
2. BA-00 with R3; then BA-01/02/03.
3. BA-04/05/06 and BA-07.
4. Begin atlas traversal over already-settled title/shop arcs before dungeon expansion.

### Wave 3 — deepen function and system fidelity

1. CC-00 through CC-03 on atlas-reached hot leaves.
2. BT-00/01 and AU-00.
3. External-input/config, PCM, PresentMon, and observer matrix.
4. Build CC-04/05 only when a concrete active divergence requires them.

### Wave 4 — automate and close the game

1. CI-02/03 private proof sweeps.
2. Grow atlas through town/dungeon/endgame/content variants.
3. Retire proof-scoped debt, calibrate coverage, and close configuration/boundary gaps.
4. CI-05 independent preservation-release audit.

## 15. Per-package completion checklist

Every work-package commit must contain:

- implementation and versioned schema change, if applicable;
- unit/synthetic test plus at least one negative test;
- exact commands and observed results in the plan build log;
- no absolute local paths or licensed bytes;
- compatibility/migration note;
- updated index/status only through its authoritative mechanism;
- R3 review record where this roadmap marks a gate;
- one logical commit; no unrelated working-tree changes.

Recommended verification commands:

```sh
nix develop --command python3 <focused-test-script.py>
nix develop --command python3 tools/run_python_tests.py
nix develop --command python3 tools/ci/check_docs.py
nix develop --command make -C tests run       # C changes
nix develop --command make -C src             # port/proxy-facing changes
```

Retail/proxy validation commands are package-specific and must name the scenario,
window, proof ID, and expected pillar result. “Looks good,” join `ALIGNED`, or same-side
replay exactness alone is never an acceptance criterion.

## 16. Explicit non-priorities

- More viewer UI before proof semantics and state/coverage consumers exist.
- Resident Ghidra/MCP service; use bounded one-shot export.
- Blind random input-mask fuzzing.
- TTD expansion without a capsule/state consumer.
- Universal cross-GPU exact-pixel gates.
- Whole-executable lifting or byte-identical rebuild.
- Wall-clock pinning as a deterministic-simulation fix.
- Generic ML screenshot similarity as parity proof.
- Portability/backend abstraction before preservation release.
- Any dashboard headline based solely on `FUN_` comments or trace macros.

## 17. External implementation references

**Prior-art transfer map:** `../reference/decomp-port-techniques.md` synthesizes
AI-driven (Snowboard Kids 2 / "Nigel", Body Harvest, Macabeus, Agent4Decompile,
DecLLM) and human-driven (objdiff, asm-differ, decomp-permuter, splat, N64Recomp)
matching-decomp projects into ranked transferable techniques, each mapped to a
package here (e.g. objdiff scoring → EP-06 behavioral match%; permuter → a
trace-permuter; L1/L2/L3 routing → the multi-pillar fixer; oracle-guard hooks →
new). Read it before designing an agent porting loop or new parity tooling.

Use primary documentation when building these packages:

- Frida Stalker block/exec events: <https://frida.re/docs/stalker/>
- Ghidra program/block APIs:
  <https://ghidra.re/ghidra_docs/api/ghidra/program/model/block/package-summary.html>
- AFL++ structure-aware mutators and trimming concepts:
  <https://aflplus.plus/docs/custom_mutators/>
- Microsoft TTD object model:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/time-travel-debugging-object-model>
- PresentMon: <https://github.com/GameTechDev/PresentMon>
- WASAPI application loopback sample:
  <https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/>

These are implementation references, not permission to substitute a generic tool's
notion of coverage, timing, or similarity for the contracts defined here.

## 18. Build log

Append terse dated entries only when a package starts or lands:

```text
YYYY-MM-DD  WP-ID  STARTED|LANDED|BLOCKED
commit/proof:
result:
next:
```

```text
2026-07-16  EP-00  LANDED
result:   docs/reference/parity-vocabulary.md — six independent claims
          (JOIN/REPLAY/PILLAR/INVENTORY/RUNTIME/CONFIRMED) frozen; every current
          sloppy term mapped to a scoped meaning; machine-output alias list for
          EP-03/EP-06; ledger `verified`-overclaim flagged for EP-06.
next:     EP-03 emits JOIN_COMPLETE/REPLAY_EXACT behind the alias list.

2026-07-16  EP-01  LANDED
result:   docs/schemas/parity-{contract,proof}-v1.schema.json (Draft2020-12) +
          docs/reference/parity-proof-format.md + tools/test_parity_schema.py +
          9 fixtures. Canonical pillar set + verdict vocab + fail-closed required
          fingerprints frozen. Negative tests: missing retail hash + unknown
          major fail schema; required pixels NOT_CAPTURED fails the gate.
          proof_id canonicalization (excl proof_id+envelope) determinism pinned.
          Added jsonschema to flake devshell. Gate: test_parity_schema.py OK.
next:     EP-02 fingerprinting (promote canonical_bytes → tools/parity/).

2026-07-16  EP-02  LANDED
result:   tools/parity/ package. canonical.py = promoted §4.4 rule
          (canonical_bytes/proof_id_of/proof_passes); test_parity_schema.py now
          IMPORTS it (single source of truth). fingerprint.py = sha256_file,
          dir_manifest (relocation-invariant, refuses EVERY symlink), git_head +
          git_dirty_patch_sha256 (staged|unstaged blob, None=clean), port/retail
          subject builders, @fresh/@none input+tool sentinels — all FAIL CLOSED
          (FingerprintError, never a fabricated hash). environment.py = validated
          8-field group + stdlib host_probe. Gate tools/test_parity_fingerprint.py
          (52 checks): relocation-invariance, 1-byte sensitivity, dirty-tree via a
          hermetic temp git repo, symlink+missing negative tests. Full python
          suite 22/22, check_docs OK.
          R3 input-inventory call: dirty_patch_sha256 = combined staged+unstaged
          diff EXACTLY per the EP-01 frozen field; untracked files are NOT dirty
          by that definition — subject.port.pe_sha256 is the build-identity
          backstop for a build-affecting untracked source file.
next:     EP-03 — split JOIN/REPLAY verdicts (orv3_sync/orv3_window emit
          JOIN_COMPLETE/REPLAY_EXACT behind the EP-00 alias list); ∥ EP-04/EP-05.

2026-07-16  EP-03  LANDED
result:   Separate JOIN + REPLAY from parity (roadmap §4.1) — both Python-side, NO
          native rebuild. orv3_sync.classify_join() → {join_verdict:
          JOIN_COMPLETE|JOIN_PARTIAL, verdict: ALIGNED|PARTIAL alias}; PAIRING-ONLY
          (no pixel/state input) ⇒ a JOIN can never read as a parity pass.
          join_verdict threaded into the sync result + pairs.json + both orv3_view
          manifests; orv3_window banner + exit key on join_verdict (0=JOIN_COMPLETE)
          w/ a "not a parity pass" caveat; web app.mjs relabels "sync / JOIN",
          prefers join_verdict (fallback verdict). REPLAY: v3verify.py prints
          REPLAY_EXACT/REPLAY_DIVERGENT + "same-side fidelity, not parity" (exit
          code + returned counts contract unchanged). The ALIGNED/PARTIAL `verdict`
          alias is RETAINED byte-identical for pre-EP-03 consumers — native
          viewer.exe (rfind "ALIGNED") + app CSS — so NO viewer rebuild; viewer.cpp
          migration to join_verdict deferred (reads the retained alias).
          +test_orv3.test_classify_join (JOIN_COMPLETE for zero-gap regardless of
          pixels = never a parity pass; alias retained). orv3 + full python 22/22,
          check_docs OK, app.mjs node --check OK.
next:     EP-04 normalize observation adapters (draw ALIGNED/BATCHING/DIVERGENT +
          flow tokens → PILLAR PASS/FAIL) → EP-05 parity_prove.py.

2026-07-16  EP-04  LANDED
result:   tools/parity/{observations,pixels,render_program}.py — observation
          normalization + pillar adjudication. observations.py = the foundation:
          LogicalFrame (from pairs key / from view label), OBS_SCHEMA_VERSION-tagged
          metrics contract, match_frames (EXACT ordered-equality join = reorder/
          foreign/dup ⇒ INCONCLUSIVE), verify_source_containers (stale-capture
          guard), schema-shaped observation/pillar_result/first_divergence builders,
          load_required (join∩window), + the `identity` adapter (JOIN_COMPLETE→PASS /
          honest in-window gaps→FAIL, pre-EP-03 join_verdict fallback). pixels.py =
          exact-mode differ==0 gate (PASS/FAIL@first/NOT_CAPTURED). render_program.py
          = draw_verdict gate (ALIGNED/BATCHING→PASS+note / DIVERGENT→FAIL) +
          from_view_json bridge (real view.json → normalized doc, gap rows skipped).
          Every adapter TOTAL + fail-closed: absent→NOT_CAPTURED, untrustworthy→
          INCONCLUSIVE, disproof→FAIL. Gate tools/test_parity_observations.py (60
          checks) — negative tests: mutate differ 0→7 (FAIL@frame), reorder
          (INCONCLUSIVE), drop required frame (NOT_CAPTURED), stale source hash
          (INCONCLUSIVE), flip DIVERGENT (render FAIL), bump schema major
          (INCONCLUSIVE); produced obs/pillar cross-validated against
          parity-proof-v1 $defs. Full python 23/23, check_docs OK.
          R3 note: the pixel PRODUCER (headless per-frame v3-replay → differ doc) is
          not built here — adapters are format-only + fixture-tested (roadmap rule 11
          "build consumers before platforms"); the render producer IS real via
          from_view_json. EP-05 wires the producers + drives when absent.
next:     EP-05 parity_prove.py — contract → resolve/drive observations → validate
          fingerprints (EP-02) → required-pillar gate → content-addressed bundle.

2026-07-16  EP-05  LANDED
result:   tools/parity/prove.py (pure core) + tools/parity_prove.py (CLI) +
          tools/test_parity_prove.py. prove.py = assemble (stamp proof_id AFTER the
          core, §4.4 canonical excl proof_id+envelope) + gate (§4.1: all req PASS→0 /
          any req FAIL→1 / else NOT_CAPTURED|INCONCLUSIVE→2, omitted req = NOT_CAPTURED)
          + write_bundle (atomic dir-rename into runs/proofs/sha256/<first2>/<id>/,
          IMMUTABLE+idempotent, envelope = the ONLY place local paths/wall-clock live,
          asserted NOT to perturb proof_id) + summarize. parity_prove.py = the one
          command: load scenario `proof:` contract (jsonschema-validated) → resolve
          each pillar via the EP-04 adapters over a captured v3 window (identity from
          pairs.json; render_program by bridging the REAL view.json, SCOPED to the
          contract's in-window frames — a multi-anchor window would else read foreign;
          pixels from an optional metrics doc; ST/AU/RT/BT pillars NOT_CAPTURED, no
          producer yet) → gather EP-02 provenance (port/retail subjects, trace+contract
          hashes, comparator=dir_manifest(tools/parity), schema hash; env REQUIRED via
          --env-json, fail-closed) → assemble/gate/store. Gate tools/test_parity_prove.py
          (41 checks): the §4.1 exit matrix, proof_id determinism + per-field
          sensitivity (a pillar-verdict / tool-hash / subject-hash change flips the id),
          envelope path-isolation (a local path is written but excluded from the
          preimage — no leak), immutable CAS idempotency, and the §15 per-pillar
          negatives (mutate a pixel diff → pixels FAIL, a draw verdict → render FAIL,
          absent pixel producer → NOT_CAPTURED, honest join gap → identity FAIL) end-to-
          end over a synthetic window + real provenance (real trace/comparator/git).
          + from_view_json gained a `required=` scoping arg (EP-04 render module).
          DEMONSTRATED end-to-end on a REAL captured window
          (guild-ui-flow win-330-2600, contract LOADING_END#2[0,105]): identity PASS +
          render_program PASS over 106 real frames, exit 0, a real content-addressed
          bundle whose recomputed proof_id matches + carries no local path in its
          preimage (env was a DEMO placeholder ⇒ a pipeline demonstration, NOT a
          certified proof — a real bundle needs the RT-00/BT-03 host-env capture).
          Full python 24/24, check_docs OK.
next:     Wave-0 step 4 — R3 adversarial review of one REAL proof bundle (needs a host
          env capture + a scenario `proof:` block), then EP-06 ledger migration + EP-08
          cache re-keying.
```

Do not copy live gameplay-front history here. Completed package detail remains under its
stable ID; move this plan to archive only after CI-05 lands.
