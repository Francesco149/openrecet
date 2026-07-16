# Parity claim vocabulary (frozen)

> **Status:** FROZEN (roadmap EP-00, adopted 2026-07-16). R3-owned; a rename needs
> R3 sign-off. Proof/contract shapes: [`parity-proof-format.md`](parity-proof-format.md).

## Why this exists

Today one token means many things. `ALIGNED` is emitted by **four** comparators
(identity join, draw/material, flow-field offset, audio counts) for four
different successes. `verified` names both a source marker (ledger) and a
human/runtime proof (prose). "differs" is spelled `DIVERGENT`/`DIVERGE`/`DRIFT`/
`DESYNC`. `bit-exact` is same-side; `1:1` is cross-target human. A reader cannot
tell what evidence backs a claim from the word alone. This doc assigns **one
scoped meaning per claim** and records the machine outputs that need a
compatibility alias when renamed.

## The six independent claims (never conflate)

| Claim | Means | Verdict tokens | Backing evidence |
|---|---|---|---|
| **JOIN** | logical frames pair (identity), no honest gaps | `JOIN_COMPLETE` / `JOIN_PARTIAL` | pairing only — NOT equality |
| **REPLAY** | a side reproduces its OWN captured bytes | `REPLAY_EXACT` / `REPLAY_DIVERGENT` | recorder/replayer fidelity |
| **PILLAR** | one cross-target axis equal (state/render/pixels/…) | `PASS`/`FAIL`/`NOT_CAPTURED`/`NOT_REQUIRED`/`INCONCLUSIVE` | a captured comparison |
| **INVENTORY** | the port SOURCE references a function | ledger `implemented`/`instrumented`/… | a `src/` marker (no runtime) |
| **RUNTIME** | the function actually executed / its I/O aligned | ledger `retail-executed`/`call-I/O-aligned` | a runtime proof artifact |
| **CONFIRMED** | a human attested parity on a trace | `confirmed-parity-ledger.md` rows | human eyeball, scoped |

JOIN and REPLAY **never** imply a PILLAR PASS (roadmap §4.1). INVENTORY **never**
implies RUNTIME. Only PILLAR results + the required-pillar gate constitute
"scenario parity-proven".

## Current term → scoped meaning → action

| Current (emitter) | What it really asserts | Scoped claim | Action |
|---|---|---|---|
| join `ALIGNED`/`PARTIAL` (`orv3_sync.py:139`, `pairs/view/manifest.json`) | frames paired | **JOIN** | EP-03: emit `JOIN_COMPLETE`/`JOIN_PARTIAL`; keep `ALIGNED`/`PARTIAL` as a deprecated alias field for existing consumers |
| draw `ALIGNED`/`BATCHING`/`DIVERGENT` (`orv3_draws.py:565`) | draw program identity | **PILLAR** `render_program` | EP-04: map to `PASS`(ALIGNED)/`PASS`+note(BATCHING = pixels-equal, program differs)/`FAIL`(DIVERGENT); keep tokens as detail |
| flow `ALIGNED`/`CONST-OFFSET`/`DRIFT`/`DESYNC`/`PHASE-CLEAN` (`flow_diff.py`) | field-offset class / rng consumption | **PILLAR** `state` inputs | keep tokens (see alias list — `apply.py` parses them); prove-layer maps DRIFT/DESYNC→`FAIL`, ALIGNED→`PASS`, CONST-OFFSET→`FAIL` unless an R3 exception |
| audio `ALIGNED`/`DIVERGE` (`audio_diff.py:300`) | SE/BGM count match | **PILLAR** `audio_events` | AU-00: upgrade to id+order+params; map to PASS/FAIL |
| replay `ALL FRAMES BIT-EXACT`/`DIVERGENT` (`replay.c:83`,`v3verify.py`) | same-side reproduction | **REPLAY** | EP-03: emit `REPLAY_EXACT`/`REPLAY_DIVERGENT` |
| `bit-exact` (`scenario-test.py:37`) | same-side golden regression | **REPLAY** (intra-target) | prose; do not read as cross-target |
| ledger `verified` (`gen_port_ledger.py:133`) | a `CALL_TRACE_ENTER` macro exists | **INVENTORY** `instrumented` (NOT runtime) | EP-06: rename; the label currently OVERCLAIMS ("runtime-diffed vs retail") |
| ledger `ported` (`:137`) | a `FUN_<va>` ref exists in `src/` | **INVENTORY** `implemented`/`source-referenced` | EP-06: split marker senses |
| ledger `stubbed`/`unported` | stub macro / no marker | **INVENTORY** | EP-06: preserve as inventory sub-states |
| `objdump-exact`/`disasm-exact` (FRONT prose) | asm-level source fidelity to the decompile | **INVENTORY** quality note | prose only; not a runtime claim |
| `1:1`/`CONFIRMED 1:1`/`USER-CONFIRMED` | human cross-target attestation | **CONFIRMED** | EP-07: bridge to a review record + proof id; stays human-owned |
| `phase`/`phase-clean` (prose + flow summary) | constant-offset counter origin, laws bit-exact | PILLAR detail (accepted only via R3 exception) | on an ACTIVELY-WORKED trace, never "accept"; it is a FAIL to close (CLAUDE.md) |

## Machine outputs that require an alias (do NOT rename bare)

External callers depend on these exact strings/exit codes; a rename must keep a
deprecated alias until every consumer migrates (roadmap EP-03):

1. join `ALIGNED`/`PARTIAL` → `pairs.json`/`view.json`/`manifest.json` `verdict`;
   `orv3_window.py:341` exit code; web viewer CSS (`app.mjs:221`).
2. draw `ALIGNED`/`BATCHING`/`DIVERGENT` → `draw_verdict`; tests
   `test_orv3.py:483+` assert exact strings.
3. flow_diff `--verdict` tokens + exit 0/1 → **`apply.py:136,140` substring-parses
   `CONST-OFFSET`/`DESYNC` to auto-generate pins**; `verdict.py`, `triage.py`.
4. audio_diff `ALIGNED`/`DIVERGE` → `triage.py:181,250`.
5. ledger `status` enum + `counts` + `--check` exit 3 → pre-commit hook, STATUS.
6. scenario-test exit 0/1; replay printed verdict strings.

Prose-only (reword freely, no machine consumer): `confirmed-parity-ledger.md`,
STATUS/FRONT/PROGRESS/findings narrative, viewer `CheatSheet.mjs`.

## The ledger overclaim (EP-06 target)

`gen_port_ledger.py` derives all of `verified/ported/stubbed/unported` from
**source scanning only** — yet renders `verified` as "runtime-diffed vs retail".
That is INVENTORY dressed as RUNTIME. EP-06 replaces the single label with the
lifecycle `discovered → source-referenced → implemented → instrumented →
retail-executed → port-executed → call-I/O-aligned → scenario-pillar-proven →
matrix-proven`, where every state past `instrumented` requires a proof artifact,
not a marker. Until then, treat ledger `verified` as `instrumented`.

## Rule for authors

State the CLAIM, not a vibe. In durable text use a scoped term: JOIN / REPLAY /
PILLAR(`<name>`) `PASS|FAIL|NOT_CAPTURED` / INVENTORY(`<state>`) / CONFIRMED. If
you write "aligned"/"verified"/"1:1", a reader must be able to tell which of the
six claims you mean — link this file or use the token.
