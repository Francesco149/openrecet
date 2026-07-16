# Parity proof compiler — M0 adversarial review (R3, 2026-07-16)

> Wave-0 step 4 of `docs/plans/parity-evidence-roadmap.md` ("R3 adversarial review
> of one proof bundle"). R3 owes this before any new parity gate becomes
> authoritative (roadmap §2). Vocabulary: `docs/reference/parity-vocabulary.md`.

## Scope

Reviewed EP-05 `tools/parity_prove.py` + the EP-04 adapters (`tools/parity/`) by
compiling **one real proof bundle** and then trying to FOOL the gate on real
evidence — the check a code-only read can't give. Subject: **`house-firstcust-arrprobe`
win-0-1500** (the USER-CONFIRMED-1:1 first-customer drive). Authored the **first
real corpus parity contract** on it (`scenario.yaml` `proof:` block, scoped to the
gap-free `HOUSE_FREEROAM#1 [1,80]` window — the join's only gap there is offset 0).

## The bundle

`proof_id 9bc05dd8…` · verdict **FAIL / exit 1**.
`identity PASS` · `render_program FAIL @ HOUSE_FREEROAM#1+1` (tex `d44541872da3b494`,
port 0 / retail 80 tris) · `pixels`+`state`+`save`+`audio_events`+`timing`+`boundary`
**NOT_CAPTURED**. Caveat emitted: capture-tool hashes are current-on-disk, not the
producing versions (EP-08).

**Truthful headline:** our *most-confirmed* scenario is pixel-1:1 by human eyeball
yet **NOT tool-proven parity** — the render program is genuinely DIVERGENT (the
b494 80-triangle strip retail draws every HOUSE_FREEROAM frame and paints 0 px; the
port omits it — FRONT's "tex b494 80tris retail-only"), and pixels have no producer.
The gate refuses to launder human confidence into a PASS. This IS the roadmap
thesis (expose untested behavior; join/eyeball never imply a PILLAR PASS).

## M0 exit conditions — all three demonstrated on REAL evidence

1. **Content-addressed proof produced** → `runs/proofs/sha256/9b/9bc05dd8…/proof.json`
   (validates against `parity-proof-v1`; gitignored; hashes-only, no proprietary bytes).
2. **Deliberate cross-target mismatch FAILS** → render_program FAIL on the *real*
   b494 divergence; and an injected `differ>0` pixel-metrics → `pixels FAIL`,
   first_divergence localized to the frame.
3. **Join-only cannot pass** → `identity PASS` never promotes to parity; `pixels`
   required-but-absent → NOT_CAPTURED → the gate cannot PASS (exit 1/2, never 0).

## Adversarial probe matrix

A review-time 22-probe harness (run against the gitignored v3-cache window, so not a
committed CI test — the durable subset is in `tools/test_parity_{observations,prove}.py`)
— **all OK after the HOLE-1 fix**: determinism (rerun→identical id), id-recompute,
envelope-id-stable, no-path-leak, schema-valid, deliberate-pixel-mismatch→FAIL+localized,
pixels-can-PASS (all differ==0), foreign-frame→INCONCLUSIVE, reordered→INCONCLUSIVE,
missing-frame→NOT_CAPTURED, unknown-major→INCONCLUSIVE, tamper-stored-verdict→proof_id
breaks, unscoped-identity→FAIL (the 105 honest load gaps).

## Holes found

### HOLE-1 — `proof_id` NOT portable (FIXED)

A `NOT_CAPTURED`-by-absence pillar baked its **absolute probe path** into
`observations.<p>.note` **and** `pillars.<p>.detail` — both hashed — via
`not_captured(f"…at {metrics_path}")`. So the *same logical run at a different
checkout dir* (`/opt/src/openrecet` vs `/home/x/recet`) hashed to a **different
proof_id**, violating §4.4 ("proof_id excludes local absolute paths") and the EP-02
acceptance ("same inputs → same ID from different absolute directories"). It bit
**every current bundle** (pixels is always NOT_CAPTURED-by-absence today).

*Why the synthetic tests missed it:* they build at ONE tmp dir and only assert
*relative* determinism (`a==b`), never cross-dir portability.

**Fix:** `observations.portable_reason()` scrubs absolute dirs → basename in the two
builders of hashed reasons (`not_captured` / `inconclusive`); PASS/FAIL details are
author f-strings over `lf.label()`/tex-ids and are already path-free. + regression
test `test_parity_prove.test_proof_id_portable` (same window at two abs dirs →
identical id; assert no abs path in the canonical core). Verified: portable id is
now `9bc05dd8…` (was `3ee83a14…` with the leak).

### HOLE-2 — container-provenance check is DEAD in the CLI (LOGGED — MUST-FIX before a pixels/state producer)

`resolve_observations` calls `adapt_pixels`/`adapt_render_program` **without
`expected_containers`**, so `verify_source_containers` never runs. A stale/**foreign**
`pixel-metrics.json` with matching frame identities (`anchor,occ,offset`) but from a
**different capture** would be trusted (`differ==0` → false PASS). Proven: the CLI
path accepts a doc with bogus `source` containers (PASS); the adapter *with*
`expected_containers` rejects the same doc (INCONCLUSIVE) — the defense exists and is
simply not wired. **Latent today** (no pixel producer; the render doc is regenerated
in-process from *this* window's `view.json`, so its provenance is inherent).

Cannot be soundly wired now: `view.json.{port,retail}_container` are absolute WSL
**paths**, not hashes, and the pairs cache key is only 8-hex. The real fix = a content
hash of the capture container (`v3cap.bin`) keyed by full provenance = **EP-08**, then
thread `source=`/`expected_containers=` from it. **GATE: no pixels/state-producer
package ships a PASS-capable adapter without this.**

### HOLE-3 — environment is operator-supplied, trusted blindly (LOGGED, by-design)

`collect_environment` validates the 8 fields non-empty but never *probes* them, and a
from-cache proof does not bind env to the capture host (this bundle's
`os_build`/`gpu`/`driver` are `operator-attested:…` sentinels). Acceptable for a
review/dev bundle; **CI-05 preservation-release** requires the Windows capture host to
emit the real env and the proof to bind it (RT/BT lanes).

### HOLE-4 — exceptions recorded but NOT gate-enforced (LOGGED, by-design)

`proof.exceptions[]` is stored, not consumed by the gate. The b494 render exception
does **not** flip `render_program`→PASS (it still FAILs — honest). An exception today
is documentation for the EP-07 human-review bridge, **not** auto-tolerance. Correct,
but state it so nobody expects an exception to green a pillar.

**Minor:** no `parity_verify` re-checks a stored bundle's `proof_id` on read (tamper is
recompute-detectable; content-addressing is the guarantee). From-cache capture-tool
hashes are current-on-disk (caveat emitted; EP-08).

## Verdict

The M0 gate is **SOUND and authoritative-ready** for the pillars that have producers
(`identity`, `render_program`) after the HOLE-1 fix; absent producers correctly
fail-closed `NOT_CAPTURED`. **HOLE-2 is the one hard prerequisite** before a
pixels/state producer's PASS may be trusted. EP-06 (ledger migration) may proceed — it
consumes exactly these scoped verdicts.

## New scenario result

`house-firstcust-arrprobe`: **identity PROVEN** (`HF#1[1,80]`, JOIN_COMPLETE);
**render_program DISPROVEN** — a real, minor, known-benign render-program gap (the
b494 80-tri 0-px retail-only strip) that the human pixel-confirm masked. Lead: port
the retail 80-tri strip (or confirm it's a degenerate/off-screen no-op and enforce the
exception once EP-07 lands). **pixels UNPROVEN** (no producer). Recorded here rather
than promoting a global "verified" — the whole point of the program.
