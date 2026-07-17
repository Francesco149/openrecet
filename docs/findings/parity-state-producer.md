# Parity `state` (volatile) pillar — ST-02 Merkle roots + ST-03 producer

> **Status:** LANDED 2026-07-17 (roadmap `plans/parity-evidence-roadmap.md` §7
> ST-02+ST-03; FRONT ★NEXT c). The per-frame VOLATILE-state sibling of the
> (scenario-scoped, persistent) `save` pillar (`parity-save-producer.md`). Built,
> 72-check gate, wired into `parity_prove` (`state` OUT of `UNBUILT_PILLARS`),
> validated on two real captures. Commit `efaf2e7`.
> **★NEXT-d LANDED 2026-07-17** (§"★NEXT-d LANDED" below): the retail `--state` head
> warm-up is CLOSED — `state` PASSES at `[1,19]` (200/200 full window) + is now a
> REQUIRED pillar ⇒ the FIRST three-pillar (identity·save·state) volatile+persistent
> proof. Fix in `frida_capture` (un-gate) + `v3cache.store` (slice by kept presents).

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

- **`--all-frames`: 198/198 → now 200/200 comparable frames Merkle-IDENTICAL** — after
  the ★NEXT-d fix the whole kept d3d window has both-sided state; the port's volatile
  state is bit-1:1 with retail across the ENTIRE save-commit window.
- **contract-scoped `[1,19]`: NOT_CAPTURED → now PASS** — 19/19 required frames'
  volatile state Merkle-identical, 0 divergences. Was 18/19 because retail's state
  stream started at offset +2 (see the ROOT below); the fix restored the head.

## ✅ ★NEXT-d LANDED 2026-07-17 — retail `--state` head warm-up CLOSED (the three-pillar proof)

**ROOT (settled — NOT hook-install latency).** The Frida STATE_VA hooks install
PRE-RESUME (live from frame 0). The head warp was the `{calltrace}` WINDOW GATE: a
`--state` v3 drive kept the scenario's `{calltrace}` op, which arms the emit window
`[base, base+count]` in `segtraceOnSegmentEnter` — run in `input_poll.onLeave`, ONE
cycle AFTER the anchor's Present (it reads `g_segtrace_fired` set by `anchorTick` at
the prior Present). AND each frame's SIM runs BEFORE its Present, so a window keyed to
an anchor detected at Present F can never cover sim F..F+1. Net: the state stream
began at anchor+2 (`7120` vs the d3d `present_first 7118`) and, because `hi = lo+len`
is inclusive, ran 1 PAST the d3d tail (`7318 > 7317`). The d3d proxy has no such warp —
it arms in-process at the anchor Present (`OrV3ArmWindowAt`), zero latency. So the two
arm paths were desynchronized at the head; nothing to do with the frame counter.

**FIX (two parts; `orv3_state`'s ORIGINAL "emit broadly, window the OUTPUT by identity"
design, restored).**
1. **Un-gate** the retail `--state` emission — `frida_capture` now STRIPS `{calltrace}`
   on ANY v3 drive (not just non-state). The 4 once-per-frame VAs then emit EVERY frame
   (live during sim 7118-7119, the anchor frame's own sim), so the window head is
   captured. (4 VAs ⇒ the pre-window per-frame send cost is the accepted `--state`
   tradeoff.)
2. **Slice at cache time** — `v3cache.store()` gains `kept_presents` (the container's
   kept d3d present-counts); `preserve_live` passes `set(c.presents)`. The call-trace
   `frame` IS the present-count the identity join keys on, so keeping only
   `frame ∈ kept_presents` windows the STORED sidecar to EXACTLY the kept d3d frames —
   dropping the pre-window load-stretch AND the tail overrun. Symmetric (port too, when
   next driven). Guard: `test_orv3.test_state_sidecar_slice`.

**VERIFIED** (re-drove `house-pause-save-commit --window 0:200 --anchor
SAVE_PICKER_READY --state --force-retail`): retail state now == the d3d window EXACTLY
(`[3064,3263]` this drive, 200 frames, offset 0 AND 1 covered, no tail, no pre-window).
`parity_state`: `[1,19]` **PASS 19/19**, `--all-frames` **PASS 200/200**. Added `state`
to `house-pause-save-commit`'s `required_pillars` ⇒ `parity_prove` verdict **PASS:
identity · save · state (+render_program bonus), 0 divergences** — the FIRST three-pillar
(volatile + persistent) proof. `contract_sha256` `9c2d2755…` → `c8c9a6a5…` (the
`required_pillars` edit); `parity-proof-index` `FUN_004905a8` binding re-keyed to it +
pillars `[identity,save,state]`. Generalizes: the HOUSE arrprobe's 1-frame head skew
also closes on its next `--state` drive.

## ★ NEXT — ST-04+

State PASSES at `[1,19]` (identity · save · state). Next: ST-04 first-divergence
report (typed values + nearby mutation provenance), ST-05 mutation capture, ST-06
scene-by-scene subsystem expansion (arrprobe already exercises player/companion/
dialogue; the picker is rng-only). Follow-up (non-blocking): the port's `--state`
sidecar re-slices on its next drive (currently un-gated-wide but join-correct).

## Tooling

`tools/parity/state_codec.py` · `state_merkle.py` · `state_producer.py` · `state.py`
· CLI `tools/parity_state.py` · schema `docs/schemas/state-volatile-v1.json` · gate
`tools/test_parity_state.py` (72 checks). Wired: `parity_prove.resolve_observations`
bridges the view inline; `state` out of `UNBUILT_PILLARS`. ★NEXT-d touched the CAPTURE
path: `tools/frida_capture.py` (strip `{calltrace}` on any v3 drive ⇒ un-gated state
emit) + `tools/trace_studio_v3/v3cache.py` (`_store_call_trace` slices the sidecar to
`kept_presents = set(c.presents)`; guard `test_orv3.test_state_sidecar_slice`).
