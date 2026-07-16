# Agent workflow — reasoning tiers, delegation, and verification

> **Status:** authoritative orchestration policy  \
> **Last verified:** 2026-07-16  \
> **Reasoning-tier definitions:** shared with
> `plans/parity-evidence-roadmap.md` §2

OpenRecet's exact-parity work benefits from delegation, but only after judgment has
reduced a problem to a bounded contract. This document names roles by required reasoning,
not by provider or model family.

## 1. Roles

| Tier | Role | Suitable work |
|---|---|---|
| **R3 — highest reasoning** | orchestrator, architect, parity adjudicator | choose next work; interpret retail; define schemas, invariants, thresholds, and proof; integrate cross-subsystem results |
| **R2 — implementation reasoning** | bounded implementer | implement an approved tool/chip/API; write focused tests; integrate known structures |
| **R1 — mechanical** | search/fixture/execution worker | enumerate, grep, transcribe approved tables/schemas, run tests/captures, backfill metadata, repair links |

The test is not “can a cheaper model probably do this?” It is:

> Has every decision that changes behavior or proof meaning already been made?

If no, retain the task at R3. If yes, package the remaining work for R2/R1.

## 2. R3-only decisions

Do not delegate these without an explicit R3 checkpoint:

- choosing the next gameplay or tooling target;
- interpreting ambiguous decompile/disassembly or inferred struct layouts;
- deciding whether a divergence is logic, phase, RNG, upstream input, observer effect,
  or acceptable environment variance;
- changing a parity schema, cache key, normalization pin, comparison threshold, or
  definition of done;
- designing cross-subsystem state, coverage, behavior-graph, ABI, graphics-resource,
  audio, or timing semantics;
- accepting any residual from an exact gate;
- replacing code inside retail or adding a potentially perturbing hook;
- deciding that retail behavior is unreachable, irrelevant, or out of scope.

A lower-tier report may present evidence and hypotheses. It may not close one of these
questions.

## 3. Good bounded delegations

### R1

- Search named paths for a specific opcode/API/address pattern and return file:line facts.
- Run an exact command on specified scenarios and return artifacts, hashes, exit codes,
  and first failures.
- Add fixtures from an already-approved schema.
- Backfill explicit scenario metadata without inferring semantics.
- Update links/index rows after the authoritative source has changed.
- Generate a vtable/table enumeration from an approved source.

### R2

- Translate one clearly bounded function with known inputs, outputs, callers, and test.
- Implement one adapter against a frozen schema.
- Add one D3D opcode after recording/replay semantics are approved.
- Build a CLI whose inputs, outputs, error behavior, and negative tests are specified.
- Extend a parser/extractor from a documented format and validate against fixtures.
- Add a retail/port probe for addresses and types supplied by R3.

### Keep at R3

- “Read the whole decompile and find what matters.”
- “Make this multi-function RNG-coupled system exact.”
- “Design the state hash/coverage graph.”
- “Port the next subsystem.”
- “Investigate why these captures differ.”
- “Pick reasonable audio/pixel/timing tolerances.”

R3 may delegate mechanical slices from those tasks after localizing them.

## 4. Required task packet

Cold workers have no reliable implicit context. Every delegation must include:

```text
WORK PACKAGE
Stable plan ID or issue.

GOAL
One binary success statement.

CONTEXT
Only the facts required to execute; cite durable repo paths.

READ SET
Exact files/sections/functions/addresses.

WRITE SET
Exclusive paths the worker owns. State forbidden paths.

LOCKED DECISIONS
Names, schema, types, thresholds, algorithms, compatibility constraints.

PROCEDURE
Ordered steps; distinguish edits from observations.

ACCEPTANCE
Exact commands and expected exit/results.

NEGATIVE TEST
How the new gate is proved capable of catching a mismatch.

STOP CONDITIONS
Ambiguities or failures that must return to R3.

REPORT
Changed paths; tests/results; artifacts; unresolved evidence. Short and factual.
```

For parity-exact work, add:

```text
EXACT OR FAIL
No phase/benign/close-enough conclusion is permitted.
If residual != 0, localize it and report FAIL.
Compare values and order, not only counts.
```

## 5. Orchestrator procedure

1. Read `CLAUDE.md`, `FRONT.md`, relevant plan, and durable findings.
2. Establish the current first divergence or concrete capability gap.
3. Decide invariants, proof bar, and file ownership.
4. Split only independent work with non-overlapping write sets.
5. Give each worker the task packet.
6. Continue a distinct R3 task; never duplicate delegated work.
7. Inspect diffs and reports.
8. Re-run the decisive value-level gate personally.
9. Adjudicate any residual; send a narrower follow-up or take over.
10. Integrate documentation/status through authoritative mechanisms.
11. Commit one logical unit; never push without explicit instruction.

Worker self-assessment is not evidence. Binary commands, artifacts, hashes, and
file:line facts are evidence.

## 6. Exact-parity calibration

The recurrent delegation failure mode is satisficing:

```text
count is nearly equal
    -> residual described as phase
    -> task declared mostly complete
```

That is invalid. Equal RNG call count does not prove equal draw order or value. Similar
pixels do not prove equal draw program. Complete identity join does not prove equal
frames. Presence of a trace macro does not prove runtime execution.

Required safeguards:

- Gate says `EXACT` or returns `FAIL`.
- Residual is localized by segment/frame/call/field/draw.
- Value and ordering checks accompany count checks.
- “Phase” requires a demonstrated constant offset with matching evolution and consumers.
- R3 reproduces the decisive check before updating parity status.
- Lower-tier output is treated as a draft until that check passes.

## 7. Parallelism

Parallelize only when:

- tasks are independent;
- write sets do not overlap;
- each has a deterministic acceptance command;
- merge/integration cost is lower than saved time;
- retail/proxy singleton execution is not contended.

Good fan-out:

- independent static-index exporters and fixtures;
- searches across unrelated subsystems;
- read-only scenario measurements that can run without singleton conflict;
- separate schema-prescribed adapters.

Bad fan-out:

- multiple agents editing the same parser/agent/proxy;
- several agents driving the singleton Windows game concurrently;
- coupled RNG/state/render functions;
- open-ended investigations that require shared evolving hypotheses.

The root orchestrator owns task splitting and integration. Workers do not recursively
delegate unless the active orchestration environment explicitly supports and requests it.

## 8. File and repository discipline

- Preserve unrelated user changes; never sweep with `git add -A`.
- Workers never commit or push. Root commits only the requested logical unit.
- Never modify or commit `vendor/`, local game assets, saves, retail frames/audio, or
  generated licensed artifacts.
- Do not modify `ghidra/projects/`; use the exported decompile or GUI project read-only.
- Use `apply_patch`/structured edits for source and docs.
- Put reusable analysis scripts under `tools/analyze/` or the owning tool package.
- Put durable conclusions under `docs/`; do not depend on private auto-memory names.
- Archive superseded plans/workflows under `docs/archive/` or `docs/plans/archive/`.
- Current truth, derived status, and historical snapshots follow `DOCUMENTATION.md`.

## 9. Build and runtime rules

Host commands run through the Nix dev shell:

```sh
nix develop --command make -C src
nix develop --command make -C tests run
nix develop --command python3 tools/run_python_tests.py
```

- Run game executables only through `tools/run-openrecet.sh`,
  `tools/scenario-test.py`, or the probe/Trace Studio supervisor.
- Prefer `build/openrecet-debug.exe` only when the supervising tool requests console
  output; do not launch it bare.
- Use `vendor/unpacked/` for disassembly and `docs/decompiled/` for static reading.
- Retail capture/probe failures are investigated and cleaned up in `finally`; never
  touch the user's real save.
- Test commands in a task packet must name required local licensed inputs and should
  degrade to an explicit skip/unavailable result when those inputs are absent.

## 10. Findings and quirks

- Retail-ground-truth quirks go in `findings/engine-quirks.md`.
- Investigation-specific evidence goes in the relevant `findings/*.md`.
- Tool limitations and proof scope belong in the owning plan/reference, not engine
  quirks.
- A confirmed-human parity claim remains authoritative within its recorded scope, but a
  new machine contradiction triggers investigation rather than automatic dismissal.
- `PORT-DEBT` marks intentional port shortcuts; tooling/proof debt belongs in the
  parity-evidence roadmap or an explicit tool issue, not a fake engine function.

## 11. Commit attribution

The repository must never rewrite history or trailers to claim a model that did not
author the change. The commit hook accepts an optional exact trailer through:

```sh
OPENRECET_AI_COAUTHOR='Name <email>' git commit ...
```

If unset, it leaves attribution untouched. Human-only and environments without a stable
AI identity require no opt-out. Never amend/rebase or bypass hooks without explicit
authorization.

## 12. Stop and report

Stop at a natural boundary when:

- an R3 decision is required;
- retail evidence contradicts a locked task packet;
- exact acceptance fails after localization;
- a risky/destructive/external action needs new authority;
- a work package or subsystem milestone is complete;
- the next work is a materially new scope.

Report outcome first, then changed paths, verification, remaining risks, and the next
stable work-package ID.
