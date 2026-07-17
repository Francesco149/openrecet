# Parity `state` (volatile) pillar — ST-02 Merkle roots + ST-03 producer

> **Status:** LANDED 2026-07-17 (roadmap `plans/parity-evidence-roadmap.md` §7
> ST-02+ST-03; FRONT ★NEXT c). The per-frame VOLATILE-state sibling of the
> (scenario-scoped, persistent) `save` pillar (`parity-save-producer.md`). Built,
> 72-check gate, wired into `parity_prove` (`state` OUT of `UNBUILT_PILLARS`),
> validated on two real captures. Commit `efaf2e7`.

## What it proves

The `state` pillar proves the **volatile-deterministic** state class
(`reference/canonical-state.md` class 2) — the once-per-frame engine state (rng,
player/companion actors, phase counters, interaction + customer-service machines,
dialogue, title menu) — is bit-identical port↔retail at every required logical
frame. It is the runtime-axis complement to the save pillar: save proves what hits
disk, state proves what the sim holds each frame. It is the pillar that finds the
**first incorrect state transition, not only the first visible bad frame**
(roadmap §0) — and it is MORE sensitive than pixels (see arrprobe below).

## Design — ST-02 encoder+Merkle as the ST-03 producer's mechanism

Two authoritative inputs, no duplication (build consumers before platforms — the
Merkle layer lands WITH its consumer):

- **`docs/schemas/state-volatile-v1.json`** — the R3 SUBSYSTEM TREE grouping the 4
  STATE_VA fields `retail_fields.json` declares (the fields the v3 game-state panel
  captures) into named subsystems: `rng / phase / player / companion / shop_npc /
  interaction / customer_service / camera / dust_fx / dialogue_house / title_menu /
  dialogue_intro`. Each field's va+TYPE is resolved from `retail_fields.json` (the
  single source of truth) — a grouping that names a field absent there is
  fail-closed, so the map can never silently drift from the capture spec.
- **`tools/parity/state_codec.py`** — the canonical value encoder: i32/u32/hex →
  `pack('<I', v & 0xFFFFFFFF)` (the 32-bit value, so a signed-vs-unsigned repr
  mismatch across the two capture paths can't read as a divergence); f32 →
  `pack('<f', v)` (the IEEE-754 bit pattern; the value arrives already collapsed to
  its f32 by `orv3_state._norm_f32`). x87-invariant, no epsilon. `build_tree` emits
  `{subsystem: {field: (type, value, canon_bytes)}}` in schema order, only for
  present, non-benign fields.
- **`tools/parity/state_merkle.py`** — domain-separated Merkle root
  (`leaf 0x00 / subsystem 0x01 / root 0x02`, schema version bound into every hash
  domain) + `first_divergent_leaf`. ST-02 acceptance, all gated: same values at a
  different capture/dict order → SAME root (order is the schema's fixed canonical
  total order, so the root depends only on values); one field mutation → the EXACT
  leaf path; a benign field change can't move a root (it's absent from the tree); a
  present/absent asymmetry is localized.
- **`tools/parity/state_producer.py`** — `from_view_json` bridges a Trace Studio v3
  `--state` window's `view.json` (which bakes each side's `call_trace.jsonl`
  STATE_VA fields per identity-joined frame as `state: {port, retail}`) into a
  normalized `state-metrics.json`: per required paired frame, build each side's tree,
  Merkle-hash, record the root pair + (on mismatch) the first divergent leaf. Pure
  core + view bridge, mirroring `render_program` — the truth-defining comparison is
  testable with no capture.
- **`tools/parity/state.py`** — `adapt_state`: PASS iff every required frame's roots
  match; FAIL localizes the first divergent leaf; absent / `has_state` false /
  partial coverage → NOT_CAPTURED; bad schema / foreign / reordered / stale-source →
  INCONCLUSIVE. Coverage is gated by the shared `match_frames` (a required paired
  frame with no both-sided state is NOT_CAPTURED — never a silent PASS on the subset).
- **`tools/parity_state.py`** — the producer CLI (standalone; `parity_prove` bridges
  the same view inline like `render_program`).

## R3 finding — `rngcalls` is BENIGN-EXCLUDED (capture-origin, not a game global)

Surfaced by running the producer on the confirmed-1:1 `house-firstcust-arrprobe`
`--state` view: `rngcalls` diverged on every frame (retail read 0/low, port
1902→7210) while the raw `rng` LCG state matched **1498/1498**. Root cause:
`rngcalls` (`retail_fields.json` `src:rngcalls`) is the Frida agent's cumulative
`g_rng_count_total`, whose ORIGIN differs per side — the port counts from process
start, the retail counter from Frida hook-install. Its absolute value is therefore
**capture-origin-dependent (class-3 environmental), not a game global**; comparing
it cross-side compares origins, not logic. The deterministic RNG value is the raw
state `rng` (`DAT_006023a0`) — and matching it frame-over-frame is a STRONGER proof
than a cumulative counter (equal state at every frame boundary ⇒ identical
consumption between boundaries). `rngcalls` is dropped from the tree with that reason;
per-frame consumption COUNT stays `flow_diff --rng-drill`'s domain (a future
per-frame-DELTA leaf could add it).

## Validation — two real captures

### `house-firstcust-arrprobe` (HOUSE free-roam — full subsystem coverage)

The user-confirmed-1:1 win-0-1500 `--state` view. With `rngcalls` excluded: raw
`rng` matches 1498/1498; the remaining divergences are exactly the FRONT's
**KNOWN-OPEN** residuals — `companion/cx` (a ~3-ULP facing-blip, FRONT residual "(C)
companion coct/cx facing blips @389") and `companion/ccnt` (RE §21.28 "+20 pose-era
tick offset"), plus early `dialogue_intro` skip-box counters. **The state pillar is
MORE sensitive than pixels** — it flags these sub-perceptual state divergences that
the pixel pillar rounds away (the trace is pixel-1:1 with "2-3 scattered 1-px
speckles"). It found DOCUMENTED residuals, not false positives — the pillar working
as designed.

### `house-pause-save-commit` (save-picker — rng determinism)

The save-PASS trace (`parity-save-producer.md`). Re-drove its SAVE_PICKER_READY
window WITH `--state`: `orv3_window house-pause-save-commit --window 0:200 --anchor
SAVE_PICKER_READY --state --view` → 19 gap-free pairs. Only the `sched`(rng) VA
fires in the picker scene (house_update / title_sim / dialogue_tick don't run there),
so this window proves RNG determinism. Result:

- **`--all-frames`: 198/198 comparable frames Merkle-IDENTICAL** — the port's
  volatile state is bit-1:1 with retail across the entire save-commit window.
- **contract-scoped `[1,19]`: NOT_CAPTURED** — 18/18 compared frames identical, but
  **1 required frame (offset +1) is uncoverable**: retail's `call_trace` STATE_VA
  events span frames **7120-7318** while the kept d3d window is **7118-7317**, so
  offsets 0-1 have no retail state. A capture TOOL gap, NOT a port divergence
  (`parity_prove` correctly reports NOT_CAPTURED, fail closed; the bundle stays
  identity PASS · save PASS · render_program PASS, `required_pillars` unchanged).
  **Sharper diagnosis for ★NEXT (d):** this is a **frame-numbering SKEW at the window
  head**, not mere hook-install latency — the state stream is offset +2 AND runs 1
  frame PAST the d3d tail (7318 > 7317). That violates `retail_fields.json`'s stated
  invariant *"the call-trace `frame` is the engine/agent frame == the present-count
  the anchor stream + d3d frames use"* right at the arm boundary. Only the `sched`
  (rng) VA fires here (picker scene), so this is the tick-scheduler emit vs the Present
  count. On the HOUSE arrprobe the skew was only 1 frame (offset 0), so it is
  boundary-position-dependent, not a fixed constant. Chase: how the retail `--state`
  call_trace `frame` counter is stamped relative to the d3d present count at an anchor
  arm (frida_capture `call_trace_vas` install path + the agent's per-frame counter).

## ★ NEXT — close the retail `--state` head warm-up ⇒ state PASSES at `[1,19]`

The state at offsets 0-1 IS 1:1 (the port has it; 198/198 match everywhere the
retail capture reaches it) — only the retail Frida `--state` hook-install latency
withholds the evidence. Closing it (install the STATE_VA hooks before the kept window
begins, or lead-in the drive so the warm-up is absorbed pre-window) flips the
contract-scoped `state` pillar to PASS and lets it join `required_pillars` — the
first three-pillar (identity · save · state) volatile+persistent proof. Then: ST-04
first-divergence report (typed values + nearby mutation provenance), ST-05 mutation
capture, ST-06 scene-by-scene subsystem expansion (arrprobe already exercises
player/companion/dialogue; the picker is rng-only).

## Tooling

`tools/parity/state_codec.py` · `state_merkle.py` · `state_producer.py` · `state.py`
· CLI `tools/parity_state.py` · schema `docs/schemas/state-volatile-v1.json` · gate
`tools/test_parity_state.py` (72 checks). Wired: `parity_prove.resolve_observations`
bridges the view inline; `state` out of `UNBUILT_PILLARS`.
