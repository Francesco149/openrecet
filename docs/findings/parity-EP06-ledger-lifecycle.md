# EP-06 — truthful port ledger: the lifecycle model (R3, 2026-07-16)

> Wave-0 step 5 of `docs/plans/parity-evidence-roadmap.md`. Replaces the single
> overclaiming `verified/ported/stubbed/unported` status with a two-axis
> lifecycle. Vocabulary: `docs/reference/parity-vocabulary.md` (the EP-06 target
> section). Consumes the EP-05 proof bundles' scoped verdicts, never a marker.

## The overclaim being fixed

`gen_port_ledger.py` derived ALL of `verified/ported/stubbed/unported` from
**source scanning only**, yet rendered `verified` as "runtime-diffed vs retail"
and headlined "2.8% runtime-verified". That is INVENTORY (a `src/` marker)
dressed as RUNTIME (a cross-target proof). A `CALL_TRACE_ENTER(0xVA)` macro is a
*probe point*, not evidence the function ran, let alone matched retail. The
501 "ported" conflated a faithful reimplementation with a bare `FUN_<va>`
mention in a comment.

## The model: TWO independent axes, never collapsed

The roadmap lists nine states in a line, but they are semantically **two axes** —
a function can be retail-executed while unimplemented (the CV-06 quadrant), so
one monotonic label would lie. Per roadmap §EP-06 "do not collapse to one
strongest global label", the ledger stores the full **evidence fact-set** plus
two axis-summaries.

### INVENTORY axis (source-derived — NO runtime claim)

| rung | evidence (from `src/` scan) |
|---|---|
| `discovered` | non-thunk in `functions.csv` — the universe floor |
| `source-referenced` | ≥1 bare `FUN_<va>` in `src/` (mention / provenance / "see FUN_x"). **A bare comment reaches ONLY here** — it is not a port claim |
| `implemented` | an intentional attestation: a `PORT-OF(0xVA)` marker OR a `CALL_TRACE_ENTER(_STUB)` probe (you cannot instrument a function you did not port). Author-attested source fidelity; still INVENTORY |
| `instrumented` | a `CALL_TRACE_ENTER(0xVA)` probe wired — runtime-*diffable* but NOT runtime-*proven*. `_STUB` → instrumented + `stub` quality flag |

`inventory_state` = furthest rung with evidence. Monotonic. A stub sits at
`instrumented` carrying `quality_flags:["stub"]` (an inventory sub-state, not a
separate rung) so the old `stubbed` fact is preserved without a body-complete
claim.

### RUNTIME axis (proof-artifact-derived — each needs an artifact)

`retail-executed → port-executed → call-I/O-aligned → scenario-pillar-proven →
matrix-proven`. Source: the git-tracked **`docs/parity-proof-index.json`**
(hashes/scopes only, no proprietary bytes — like `confirmed-parity-ledger.md`).
Each entry binds a VA to a proof_id + scope + declared runtime state. The
generator takes the furthest declared state per VA. **Empty today** → every
function's `runtime_state` is `null` and `runtime_proven` = 0. That is the honest
state: no proof bundle yet declares which VAs it covers (that VA→proof binding is
future CV-coverage work; EP-05 bundles are scenario/pillar-scoped, not
per-function). The committed ledger stays reproducible because it reads only the
git-tracked index, never the local gitignored `runs/proofs/`.

## Why the runtime axis is empty, not backfilled

Migration rule (roadmap §EP-06): "do not fabricate proof for old entries."
We cannot promote the 72 `CALL_TRACE_ENTER` functions to `retail-executed`: the
macro proves a probe *exists in src/*, not that the probe *fired and aligned* in a
retained artifact. Runtime states appear only when a curator adds a
`parity-proof-index.json` entry backed by a bundle. Human confirmations
(`confirmed-parity-ledger.md`) remain a SEPARATE evidence stream (EP-07 bridges
them; a human attestation is not a machine PILLAR PASS).

## Acceptance (roadmap §EP-06) — how each is met

1. **adding `CALL_TRACE_ENTER` changes only `instrumented`** — the probe sets the
   instrumented rung and nothing on the runtime axis. Test:
   `test_probe_only_reaches_instrumented`.
2. **a runtime proof artifact is required for executed/aligned states** — runtime
   rungs come solely from the proof-index; absent index → all `null`. Test:
   `test_runtime_state_needs_proof_index` feeds a synthetic entry and asserts the
   VA reaches `scenario-pillar-proven`, and that removing it drops back to `null`.
3. **a `FUN_` comment cannot claim implementation** — a bare `FUN_<va>` yields
   `source-referenced`, never `implemented`. Test:
   `test_bare_fun_ref_is_source_referenced_not_implemented`.

## The PORT-OF(0xVA) marker (new, opt-in, additive)

A comment-only author attestation: "this src function faithfully ports engine
`FUN_<va>`" — INVENTORY-level (source fidelity, like `objdump-exact`), NOT a
runtime claim. Example:

```c
/* PORT-OF(0x4060ff) — faithfully ports engine FUN_004060ff (16-global UI scratch). */
```

**Zero real markers seeded this session** (attesting faithfulness is human work;
bulk pattern-matching would re-introduce the overclaim). So
`implemented`-not-`instrumented` = 0 today — honest. Backfill is future R1/author
work: add `PORT-OF` as ports are attested. The rung's mechanism is covered by a
synthetic fixture test, not a fabricated count.

## Compatibility (nothing downstream breaks)

- `functions[].status` kept as a DEPRECATED legacy alias
  (`verified/stubbed/ported/unported`) — `mem_watch.py` reads it and filters
  `("unported","stubbed","unmapped")`; byte-stable.
- `counts` keeps every legacy key (`verified/stubbed/ported/unported/touched/
  pct_touched/pct_verified`) alongside the new lifecycle counts.
- `--check` exit-3 staleness contract unchanged; output stays a pure function of
  git-tracked inputs (`functions.csv`, `src/`, `engine_function_vas.json`,
  `parity-proof-index.json`).

## What the numbers become (2026-07-16)

Inventory: **85 instrumented** (72 full + 13 stub) · **0 implemented**-not-
instrumented (no PORT-OF yet) · **501 source-referenced** (was "ported" — a
FUN_ mention, not a port proof) · **1962 discovered** (was "unported"). Runtime:
**0 proven** on every rung. The headline flips from "2.8% runtime-verified" (a
lie) to "0% runtime-proven — 85 instrumented, index pending".
