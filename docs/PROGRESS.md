# OpenRecet — Progress Log

Reverse-chronological log of meaningful changes. Auto-generation TBD once
the test harness has coverage metrics worth reporting.

> **Live status now lives in `STATUS.md`** (derived headline) and
> `port-ledger.{json,md}` (per-function port status). This log is the dated
> narrative; don't hand-track per-subsystem "done/not-done" status here.

## 2026-07-17 — GX-06 — graphics-capture regression corpus (the GX arc is COMPLETE)

Roadmap §9 GX-06. Finding: `docs/findings/gx06-graphics-corpus.md`. Commits `b1ae8de`
(fixtures) + `82591ad` (gate) + `f6d47b2` (GX-05 residual) + this (docs).

- **The capstone:** GX-00→05 proved the D3D8 capture COMPLETE; GX-06 proves the record→REPLAY
  path for every recorded opcode is itself correct + regression-guarded. Coverage unit = the
  container OPCODE (`orv3.OPNAME`, tied to the census recorded method(s), drift-guarded), two
  axes: a FIXTURE (synthetic controlled capture → bit-exact replay) + a REAL PROOF (real cached
  scenario containing it → `v3verify` bit-exact).
- **Sweep (0/134 cached containers):** DrawPrimitive / DrawIndexedPrimitiveUP / CopyRects are
  UNOBSERVED ⇒ fixture-only, recorded honestly (engine draws via DrawIndexedPrimitive +
  DrawPrimitiveUP; pause backdrop is a SetRenderTarget re-render, not a CopyRects screen-capture).
- **Corpus:** 2 new fixtures — `gx06_sink` (all 22 non-RT opcodes, lit+textured+transformed) +
  `gx06_rt` (RES_RT_TEX/SetRenderTarget/CopyRects + 4 SURFREF kinds via render-to-tex→composite→
  CopyRects), both 0-diff — plus gx04/05 (VB mutation), and 3 real proofs (title 2D 120/120,
  arrprobe HOUSE 3D 1500/1500, pause RT 240/240, all REPLAY_EXACT).
- **Gate `tools/gx_corpus.py`:** FAST (manifest+census coverage math + drift, no caches/replay ⇒
  host-suite) + `--verify` (drive-capable re-parse/v3verify/fixture-run + re-STAMP, VALIDATED e2e).
  Verdict COMPLETE — 25 opcodes, 22 observed proven, 4 SURFREF kinds; acceptance MET.
- **GX-05 residual CLOSED:** diagnostic reader corruption-safety — `orv3.checked_reader` into the
  orv3_xform/orv3_rt raw re-walks (root: they only walk `Container.load`-validated bytes, so the
  corrupt-input path was already closed — this is defense-in-depth for a walk desync); orv3_state
  isn't a container reader.
- New: `orv3.Container.opcode_counts/surfref_counts/checked_reader`, `docs/parity-graphics-corpus.json`,
  `tools/parity/gx_corpus.py`, 6-check gate test + 2 fixture tests + test_orv3. Python suite 37/37.

## 2026-07-17 — GX-05 — dedup byte-compare + reader corruption-safety

Roadmap §9 GX-05 ("harden deduplication and corruption detection"). Finding:
`docs/findings/gx03-resource-versions.md` §"GX-05 LANDED". Commit pending.

- **Decision: hash-plus-byte-compare, NOT SHA-256** — collision-PROOF (not merely -resistant;
  the GX arc's ethos), no crypto in 3 languages, + the only option that makes the
  forced-collision acceptance constructible/decisive.
- **Dedup (`d3d8_proxy.c`):** `dedup_or_write` byte-compares the retained body (+type+len) on a
  FNV-64 hash hit ⇒ a collision → NEW id, never a false dedup; size/type/format in the domain;
  distinct bodies retained in RAM (dedup-bounded, process-lifetime). Per-drive `dedup` block in
  the census sidecar (`collisions` invariant 0). Env test-seam forces collisions.
- **C reader (`replay_core.c`, viewer + pixel producer):** `cspan`/`cspan_n` (32-bit-overflow-safe)
  bound every variable-length span + fit checks ⇒ no OOB; cursor poisons to EOF on overflow.
- **Python parsers (`orv3.py`, `orv3_draws.py`):** bounds-checked reads → explicit ValueError.
- **Acceptance MET + DECISIVE** (fail on pre-GX-05 code): `gx05_fixture` (A,B,A,C forced-collision
  → 3 distinct RES_VB/[0,1,0,2], census collisions:2), `corrupt_fuzz` (40000 fuzz, cursor never
  escapes), `test_orv3.test_corrupt`. Transparent on valid data (`title` verify 120/120 bit-exact,
  GX-04 unregressed). 3 GX tests wired into `run_python_tests.py`.

## 2026-07-17 — GX-01-full — census wired as a HARD pixels/render_program precondition

The R3 policy call (now that GX-04 emptied the VB/IB risk ⇒ arrprobe census SAFE): the D3D8
capture-completeness census is a **fail-closed record-or-fail PRECONDITION** on the two
D3D-stream-replay pillars in `parity_prove`. Finding: `docs/findings/gx00-d3d-method-census.md`
§"GX-01-full LANDED". Commit pending.

- **Policy (`capture_completeness`, `parity/d3d_census.py`):** bilateral. `pixels`+`render_program`
  reconstruct the frame from the captured D3D8 command stream ⇒ SOUND only if the capture was
  COMPLETE (every render-affecting forwarded method 0-observed) on BOTH sides. not-SAFE (either
  side VIOLATION / INCONCLUSIVE / ABSENT) ⇒ BOTH pillars OVERRIDE to INCONCLUSIVE — never a false
  PASS/FAIL (an incomplete capture makes a FAIL as untrustworthy as a PASS). `identity`/`state`/
  `save` don't read the D3D stream ⇒ NOT gated (correctly scoped).
- **ABSENT is fail-closed too:** a view predating the census bake ⇒ INCONCLUSIVE (a caveat can't
  stop `gate()` returning PASS, so absence must move the VERDICT); a re-drive/re-bake closes it.
- **Plumbing:** census is process-lifetime (1 sidecar/side). `orv3_slice.slice_entry` carries
  `v3cap.census.json` forward verbatim (a slice inherits its drive's census, like `call_trace.jsonl`);
  `orv3_view` bakes the RAW per-side sidecar (`port/retail_census`) into view.json (single source of
  truth = the committed census; `parity_prove` recomputes the verdict against it); `resolve_observations`
  gates render/pixels + adds `census_schema_sha256` to the proof `tools` group.
- **Validated:** `test_d3d_census` +28 → 91 (the completeness policy: both-SAFE→sound, VIOLATION/
  ABSENT/INCONCLUSIVE/malformed→not-sound, the shared `dynamic_from_doc`); `test_parity_prove` census
  gate (SAFE no-op, `SetViewport` VIOLATION→INCONCLUSIVE = the GX-01 acceptance negative, ABSENT→
  INCONCLUSIVE, VIOLATION overrides an intrinsic pixel FAIL, identity ungated). **End-to-end** on
  `house-firstcust-arrprobe [1,80]` (pure cache re-slice, no drive): SAFE both sides (31/31 risk
  0-observed) ⇒ **identity PASS · render_program FAIL · pixels FAIL** unchanged, each render pillar
  stamped `census[SAFE/SAFE]` — the b494 render FAIL + sub-perceptual pixel FAIL now PROVEN over a
  complete capture (not forwarded-uncaptured artifacts). arrprobe's honest FAIL is census-SOUND.

## 2026-07-17 — GX-00 dynamic D3D8 census + GX-01 gate — first live verdict = VIOLATION

The static census (5120a25) named the 33 forwarded, render-affecting, UNCAPTURED D3D8
methods (the RISK surface). This adds the DYNAMIC census — which of them a scene ACTUALLY
calls — + the GX-01 record-or-fail gate, and RAN it on the M0 scene. Finding:
`docs/findings/gx00-d3d-method-census.md`. Commits `cac0840` (mechanism+tests) + this.

- **Proxy counter, zero hand-edits:** the 84 `fwd_` thunks are generated, so
  `gen_forwarders.py` emits a stable `FWD_` enum + `volatile LONG g_fwd_calls[]` +
  `g_fwd_names[]` and an `InterlockedIncrement` per thunk. Process-lifetime + unconditional
  (device state persists ⇒ a risk call ANYWHERE up to the compared window desyncs the
  replay). Recorded (`my_`) methods are captured-by-construction, no counter. `d3d8_proxy.c`
  rewrites `v3cap.census.json` at each KEPT frame (mirrors the container `fflush`);
  `v3cache.store()` threads it into the cache alongside `v3cap.bin`.
- **Consumer + gate** (`d3d_census.py --dynamic`): SAFE (all risk 0-observed, exit 0) /
  VIOLATION (a risk method fired, exit 1) / INCONCLUSIVE (sidecar incomplete/drift, exit 2,
  fail-closed). query_only forwards (GetRenderState…) shown in the profile but never trip the
  gate. +33 host checks (63/0) incl. the roadmap negative test (a deliberate `SetViewport`
  can't pass as complete).
- **★ First verdict — `house-firstcust-arrprobe` [1,80] → VIOLATION both sides** (the new
  proxy hash re-keyed the EP-08 cache ⇒ both re-drove, 80/80 bit-exact). Only
  `CreateVertexBuffer`+`CreateIndexBuffer` fire (retail 130×/port 13× each);
  **31/33 risk methods 0-observed** (no Reset/SetViewport/state-blocks/shaders/cursor). A
  SURGICAL resource-creation gap = the **GX-03/GX-04 hinge**: creation is forwarded-unwrapped,
  but bound VB/IB *content* IS snapshotted late (`snap_vb`/`snap_ib`) ⇒ the residual risk is
  same-frame re-mutation only. Sharpens arrprobe's M0 honest-FAIL with a concrete mechanism —
  NOT the expected SAFE; the census earns its keep on our most human-confirmed scene. (Count
  magnitude 130 vs 13 = process-lifetime scope / retail's longer load, NOT a parity signal.)
- **Next:** GX-03/GX-04 (per-draw resource versions + wrap/version VB/IB); then the R3 call on
  wiring the census as a hard pixels/render precondition in `parity_prove` (GX-01-full).

## 2026-07-17 — EP-07: human-review bridge (additive, non-hashed, verdict-preserving)

A proof bundle carried an unused `human_review: null` stub in the HASHED core, so
attaching a review would have CHANGED `proof_id` — the blocker the prior session recorded
(f29f553). Landed the human-attestation layer. Finding:
`docs/findings/parity-EP07-human-review.md`.

- **R3 decision:** add `human_review` to `canonical.NON_HASHED` (not move it to the
  envelope). Smallest blast radius on the FROZEN schema — its SHAPE is unchanged
  (`human_review` stays a required first-class top-level field); the change is confined to
  the canonicalization rule §4.4 already places under R3, and makes the code CONFORM to
  §4.4 ("proof_id excludes … human display notes"). Not a schema major bump.
- **`prove.py:attach_human_review(proof, review, *, required_pillars)`** — additive,
  asserts review-neutrality under the CURRENT rule; a confirming review over a non-PASS
  gate is recorded `confirmed-despite-<MACHINE>` + stamped with `machine_verdict`, NEVER a
  silent pass; `gate()`/exit stays machine-driven.
- **`tools/parity_review.py`** CLI — writes the review back into the SAME content-addressed
  bundle (non-hashed amendment); `required_pillars` auto-resolve from the bundle's scenario
  contract with a drift check; exit = the MACHINE gate.
- **VERIFIED** e2e on the REAL `house-firstcust-arrprobe` bundle → `confirmed-despite-FAIL`
  (its honest sub-perceptual pixel FAIL): a human-1:1-confirmed-but-not-bit-exact scene can
  now carry a scoped attestation that can't flip the machine FAIL. +18 host checks
  (test_parity_prove 72/0, incl. a stale-id regression) + schema NON_HASHED/neutrality checks.
- **NB:** pre-EP-07 bundles' `proof_id`s are stale under the new rule (they hashed a
  `human_review:null`) ⇒ re-address on next drive (advisory; durable key `contract_sha256`).
  DEFERRED (opt-in): the confirmed-parity-ledger → structured-review-records migration.

## 2026-07-17 — GX-00: D3D8 method census (capture completeness)

A `pixels`/`render_program` pillar is only sound if EVERY render-affecting D3D8 call the game makes was RECORDED — a
replay reconstructs the frame from the captured command stream, so a render-affecting call the proxy silently
FORWARDS would make the replay diverge and a pixel PASS over that scene unsound. GX-00 censuses the v3 proxy's every
vtable slot. `docs/findings/gx00-d3d-method-census.md` + `docs/schemas/d3d8-method-census-v1.json` (R3 classification)
+ `tools/parity/d3d_census.py` + `tools/d3d_census.py` + `tools/test_d3d_census.py` (30 checks).

- **The split:** IDirect3D8 16 methods (4 recorded / 12 forwarded), IDirect3DDevice8 97 (25 / 72). Classified (113):
  23 recorded · 6 wrapper_lifetime · 45 query_only · 6 forwarded_irrelevant · **33 render_affecting_unsupported**.
- **The RISK set (33, fail-closed):** forwarded, render-affecting, UNCAPTURED — Reset, SetViewport, SetClipPlane,
  MultiplyTransform, SetGammaRamp, UpdateTexture, resource-creation (Create{Vertex,Index}Buffer / RenderTarget / …),
  state-blocks (Begin/End/Apply/Capture/Create), ProcessVertices, shaders (Create/SetVertexShaderConstant,
  Create/Set/SetPixelShaderConstant), palettes, cursor, higher-order patches. The whole roadmap high-risk seed list
  confirmed forwarded-uncaptured.
- **NEW lead:** `SetPixelShader` is FORWARDED while `SetVertexShader` is RECORDED — a capture-completeness asymmetry
  (the proxy catches a VS/FVF change but would miss a PS bind).
- **Drift guard:** `test_d3d_census.py` asserts `proxy_generated.h`'s actual recorded/forwarded split matches the
  census exactly — a method flipping recorded↔forwarded (e.g. GX-02 recording one), or a d3d8.h/gen_forwarders
  add/remove, fails until the census follows. "Lists cannot drift unnoticed" (GX-00 acceptance).
- **NOT a regression of existing PASSes** — this is the static risk SURFACE, not a realized fault; a 2007
  fixed-function DX8 title likely never calls most. The DYNAMIC census (proxy call-counter per scenario; 0 observed ⇒
  safe) resolves each — the GX-00 follow-up (needs a drive), then GX-01 record-or-fail.

## 2026-07-17 — ST-05: semantic-mutation CONSUMER (the causal layer under the state pillars)

The layer BENEATH ST-04: a mutation is a named WRITE to a canonical-state field; a stream answers "which write first
diverged, and WHO wrote it?" — the provenance ST-04 leaves null. Per roadmap rule 11 (build consumers before
platforms) the CONSUMER + R3 design land BEFORE the Frida/TTD capture platform. `docs/reference/state-mutations.md`
(R3 design) + `docs/schemas/state-mutation-v1.json` (frozen event shape + the semantic/derived/noise class gate +
the grounded event catalog) + `tools/parity/state_mutation.py` + `tools/test_state_mutation.py` (44 checks).

- **Class gate (R3):** semantic (always compared) / derived (compared, points upstream) / noise (excluded, only with
  a reason). An unknown-class mutation is a STOP for R3, never silently dropped.
- **Reconstruct** a subtree (replay compared classes, last-write-wins; idempotent — a hook firing twice or a
  batched+per-write observation doesn't double-apply; a conflicting double-observation raises). `verify_reconstruction`
  cross-checks it against the state pillar's captured per-frame fields.
- **First wrong write** by CUMULATIVE per-frame value: an unwritten path holds the shared window-start (recovered from
  a write's `old`), so a one-sided write IS a real divergence (value / port-missing / port-extra). Noise never triggers.
- **Ordering invariant (the ST-04/ST-05 link):** first-wrong-write ≤ first-state-root-divergence; a wrong write AFTER
  the state diverged ⇒ the stream missed the causal write ⇒ INCONCLUSIVE, never a pass.
- **Fills ST-04's provenance seam:** `attach_provenance` sets `first_divergence.provenance = {owner_va, callsite_va,
  old, new, …}` + the ordering check; reachable via `state_diff.py --mutations`, host-tested end-to-end (a diverging-
  gold window + matching streams → the writer VA on the leaf).

DEFERRED (rule 11): the Frida post-write / TTD capture PLATFORM (named writers emitting `state-mutation.json`) — lands
when a scenario needs the provenance; owners are `attested-at-capture` (only `save_slot_commit → FUN_004905a8` certain).

## 2026-07-17 — ST-04: first-divergence state report (the `state` pillar's drill-in)

The DIAGNOSTIC sibling of the `state` ADAPTER (`state.py`): the adapter grades the pillar PASS/FAIL for the proof
bundle; ST-04 explains a FAIL. `tools/parity/state_diff.py` (pure core) + CLI `tools/state_diff.py` +
`tools/test_state_diff.py` (43 checks). Reuses the ST-02 codec+Merkle — no new truth. Report (roadmap §7 ST-04
output): first divergent LOGICAL frame → leaf ROOT PATH + schema TYPE + TYPED VALUES + RAW BITS (the canonical
encoder bytes each side hashed) + LAST MATCHING frame + value TRANSITION + every CO-DIVERGENT leaf.

- New `all_divergent_leaves` (`state_merkle.py`, shared `_walk_divergent` generator; first == `first_divergent_leaf`)
  — names the FULL extent of a frame's corruption, not just the top-priority leaf. New `paired_state_from_view`
  (`state_producer.py`) — the view→paired extraction, shared by the producer + ST-04.
- **TRANSITION = state-derivable mutation provenance.** The last matching frame had EQUAL roots ⇒ both sides held the
  same value (Merkle) ⇒ `prev` unambiguous; classifies the primary leaf **port-MISSED** / **port-SPURIOUS** /
  **port-WRONG**. The callsite/owner is ST-05's job — `provenance:null` seam left for it.
- Fail closed, §4.1 exit codes (PASS 0 / FAIL 1 / NOT_CAPTURED · INCONCLUSIVE 2); a test proves the diagnostic verdict
  AGREES with `adapt_state` (authoritative) on shared cases. Stable JSON + short text.

VERIFIED on real captures: `house-pause-save-commit` win-0-200 **PASS 200/200**; `house-firstcust-arrprobe` win-0-1500
**FAIL @ `LOADING_START#1+0 companion/cx (f32)` retail_bits `2709b5bc` port `2309b5bc`** (the FRONT's documented
~3-ULP facing residual — 1 mantissa bit) + 23 co-divergent leaves, head-of-window ⇒ transition None. Whole parity
Python suite green (state_diff 43, parity_state 72, prove/observations/schema/pixels/save/fingerprint unchanged).

## 2026-07-17 — ★NEXT(d): retail `--state` head warm-up CLOSED ⇒ first three-pillar (identity·save·state) proof

The `house-pause-save-commit` `state` pillar was NOT_CAPTURED at the contract window `[1,19]` — the retail `--state`
stream began at anchor+2 (offset 0-1 uncoverable). **ROOT (settled, NOT hook-install latency; the STATE_VA hooks
install pre-resume):** the `{calltrace}` WINDOW GATE. A `--state` v3 drive KEPT the scenario's `{calltrace}` op, which
arms the emit window in `segtraceOnSegmentEnter` — run in `input_poll.onLeave`, ONE cycle AFTER the anchor's Present
(it reads `g_segtrace_fired` set by `anchorTick` at the prior Present) — and each frame's SIM runs BEFORE its Present,
so a window keyed to an anchor detected at Present F can NEVER cover sim F..F+1. Net: state stream `7120..7318` vs the
d3d window `7118..7317` (+2 head, and `hi=lo+len` inclusive ⇒ +1 tail). The d3d proxy has no warp — it arms in-process
at the anchor Present (`OrV3ArmWindowAt`), zero latency; the two arm paths were desynchronized at the head.

**FIX** (restores `orv3_state`'s ORIGINAL "emit broadly, window the OUTPUT by identity" design): (1) **un-gate** —
`tools/frida_capture.py` now strips `{calltrace}` on ANY v3 drive (not just non-state) ⇒ the 4 once-per-frame VAs emit
EVERY frame, live during the head sim; (2) **slice at cache** — `tools/trace_studio_v3/v3cache.py` `store()` gains
`kept_presents` and `_store_call_trace` windows the sidecar to `set(c.presents)` (the call-trace `frame` IS the
present-count the join keys on) ⇒ drops the pre-window load-stretch AND the tail. Symmetric (port re-slices next drive).

**VERIFIED** (`orv3_window house-pause-save-commit --window 0:200 --anchor SAVE_PICKER_READY --state --force-retail`):
retail state == the d3d window EXACTLY (offset 0 AND 1 covered, no tail, no pre-window). `parity_state`: `[1,19]`
**PASS 19/19**, `--all-frames` **PASS 200/200** (was 198/198). Added `state` to `required_pillars` ⇒ `parity_prove`
verdict **PASS: identity·save·state (+render_program bonus), 0 divergences** — the FIRST three-pillar (volatile +
persistent) proof. `contract_sha256 9c2d2755…`→`c8c9a6a5…`; `parity-proof-index` `FUN_004905a8` binding re-keyed +
pillars `[identity,save,state]`; ledger regen (runtime: 1 proven). New guard `test_orv3.test_state_sidecar_slice`;
`test_parity_state` (72), `test_parity_schema`, `test_gen_port_ledger`, `test_orv3` all green. Full story +
ROOT/loop-order derivation: `findings/parity-state-producer.md` §"★NEXT-d LANDED".

## 2026-07-17 — the volatile `state` pillar: ST-02 Merkle roots + ST-03 producer (★NEXT c)

The `state` proof pillar — the per-frame VOLATILE-state sibling of the persistent `save` pillar — proves the
once-per-frame engine state (rng/phase/player/companion/interaction/customer-service/dialogue/title) bit-identical
port↔retail. Mirrors the save pillar: a canonical encoder + domain-separated **Merkle roots** (ST-02,
`tools/parity/state_codec.py`+`state_merkle.py`; R3 subsystem tree `docs/schemas/state-volatile-v1.json` over the 4
STATE_VA fields, types resolved from `retail_fields.json` so a grouping can't silently drift) as the comparison
mechanism of a **view.json producer** (ST-03, `state_producer.from_view_json` → `state-metrics.json`) + `adapt_state`
(coverage-gated via `match_frames`) + CLI `parity_state.py`, wired into `parity_prove` (`state` OUT of
`UNBUILT_PILLARS`). +72-check gate `test_parity_state.py`. Full story: `findings/parity-state-producer.md`.

**R3 finding — `rngcalls` is BENIGN-EXCLUDED.** Running the producer on the confirmed-1:1 arrprobe `--state` view
showed `rngcalls` diverging everywhere (retail 0, port 1902+) while raw `rng` matched 1498/1498. Cause: `rngcalls`
(`src:rngcalls`) is the Frida agent's cumulative `g_rng_count_total`, whose ORIGIN differs per side (port from process
start, retail from hook-install) ⇒ capture-origin-dependent (class-3 environmental), not a game global. The
deterministic RNG value is the raw `rng` state — matching it frame-over-frame is STRONGER than a counter.

**Validated on two real captures.** (1) `house-firstcust-arrprobe` (HOUSE free-roam): raw `rng` **1498/1498**, and the
pillar is MORE sensitive than pixels — it FAILs on the KNOWN-OPEN companion residuals (`companion/cx` ~3-ULP facing
blip, `companion/ccnt` +20 pose-era tick offset) pixels round away (documented residuals, not false positives). (2)
`house-pause-save-commit` (save-picker, rng-only scene): re-drove its SAVE_PICKER_READY window `--state` →
**full-window 198/198 Merkle-IDENTICAL**; contract-scoped `[1,19]` is NOT_CAPTURED on ONE frame (offset +1) because
retail's `--state` capture warms up 2 frames (Frida STATE_VA hooks install late — events 7120-7318 vs kept 7118-7317;
the port's compiled CALL_TRACE has none) — a capture TOOL gap, not a port divergence (bundle stays
identity·save·render_program PASS). ★ NEXT: close the retail `--state` head warm-up ⇒ the `[1,19]` state pillar flips
to PASS and joins `required_pillars` (the first three-pillar identity·save·state proof). Commit `efaf2e7`.

## 2026-07-17 — the FIRST runtime-axis proof binding: STATUS off "0% runtime-proven" (★NEXT b′)

`docs/parity-proof-index.json` now binds one VA — the save-commit `FUN_004905a8` (`save_io_commit_slot`) →
**`scenario-pillar-proven`** — off the `house-pause-save-commit` save-PASS bundle. STATUS headline flips
**"0% runtime-proven — index empty" → "1 function runtime-proven"** (0.0% of 2548 non-thunk; honestly tiny, but
the first non-empty runtime rung: INVENTORY≠PARITY made real, no longer a source-marker inventory).

Convention settled (precedent for every future entry): the durable ledger key is **`contract_sha256`**, never
the volatile `proof_id`. `parity_prove.contract_sha256` is the stable hash of the scenario's `proof` block —
drive- and commit-independent (reproduces from the committed `scenario.yaml`; `9c2d27556b6f…` recomputes
exactly), so it self-cites inside its own commit, defeating the `git_commit`-binding volatility that had
deferred (b′). `proof_id` is now OPTIONAL + ADVISORY (a recording-time locator; `runs/proofs/` is gitignored).

`gen_port_ledger.load_proof_index` requires a 64-hex `contract_sha256` (fail-closed) and treats `proof_id` as
optional; `classify` emits `contract_sha256` per proof ref; the runtime-proven table keys on it. Tests:
`test_gen_port_ledger.py` 155 checks (fail-closed on missing/malformed contract_sha256 + malformed proof_id +
the proof_id-optional path; the live-tree test pins the shipped binding); `--check` idempotent. Finding:
`findings/parity-save-producer.md` §"★NEXT(b′) LANDED".

## 2026-07-17 — `house-pause-save-commit` → save PASS: the FIRST fully-passing multi-pillar bundle (★NEXT b)

`parity_prove` verdict **PASS — identity PASS · save PASS · 0 divergences** (render_program PASS bonus;
`contract_sha256 9c2d2755…`). Two real port bugs the save pillar caught, both fixed byte-exact (invisible to
every frame pillar): **(1)** `house_cam_flag` 0xb37d — the continue-resume `scene_post_fade_init` didn't clear
the from-world-map camera flag retail zeroes (`FUN_0049a59e` all.c:100642), so a `{savefile}` taken after a
map→house return re-committed a stale 1 (commit `d686739`; engine-quirk #134). **(2)** `occupied_playtime`, the
drive-variable playtime accumulator: EXPLAINED as the port counting the two completion-based async-load brackets
(house + pause menu) into playtime — a wall-clock CreateThread race under turbo (house-load Δ2387 + pause-load
Δ1641 = the observed Δ4028; retail's are the deterministic intro-video cadence Δ48/Δ8; engine-quirk #135), then
normalized by a NEW bilateral **`{playtimepin}`** (mirrors `{gsimpin}`; forces the active slot's playtime to a
canonical origin at SAVE_PICKER_READY, past both variable loads; both port+agent fire pre-sim → both land on
V+K=29687, no off-by-one; forwarded to the agent because retail's natural swings 29643/29683/29830 run-to-run).
VERIFIED `--target both` ×2: save.dat byte-identical (ndiff 0), drive-stable. +2 host tests (3434/0). Dropped
the contract's save exception. Commits `d686739`+`64d2e3f`; finding `findings/parity-save-producer.md`
§"★NEXT(b) LANDED".

## 2026-07-16 — Save pillar in a content-addressed bundle: the full `parity_prove` proof (★NEXT a) + a stale-test fix

The save-pillar arc's end-to-end packaging — the ST-01 save pillar now lands inside a content-addressed proof
bundle (the M0 pattern applied to persistent state). Commit pending; finding `findings/parity-save-producer.md`
§"Full proof bundle LANDED".

- **`house-pause-save-commit` proof:** added a `proof:` block (schema_v2, join `SAVE_PICKER_READY#1 [1,19]`,
  `required_pillars:[identity,save]`, an R3 save exception for the phase-origin residual). `parity_save.py …
  --window 0:200` deposits save-metrics.json → `parity_prove.py … --env-json docs/reference/parity-host-environment.json`
  → **identity PASS · save FAIL (exit 1)** @ `bank0/occupied_playtime` (this drive: port 0x41ee/16878 vs retail
  0x73f3/29683). Stable identity = `contract_sha256 77e8e3f4…` + this verdict; regenerate via `parity_prove …
  --window 0:200 --env-json docs/reference/parity-host-environment.json`. The **proof_id is not a durable
  constant** — it binds subject.port.git_commit + PE + the drive's save hashes, so it advances every commit AND
  drive (the port occupied_playtime is drive-variable — see the lead); don't hard-cite it. PORTABLE (abs paths
  only in the non-hashed `envelope.local_paths`; hashed content refs artifacts by sha256). render_program a
  non-required PASS (aligned draw programs across the picker window). The 6-byte save residual = phase-origin
  near-PASS (occupied_playtime + its checksum echo + 1 unmapped byte, all bank-0 non-logic) — the save-pillar
  analogue of arrprobe's honest FAIL.
- **★ GOTCHA (baked into the scenario comment):** arm the v3 window at `--anchor SAVE_PICKER_READY`, NOT the
  default `HOUSE_FREEROAM`. The scenario re-anchors the commit on SAVE_PICKER_READY (its `{caprange}` follows
  `{wait SAVE_PICKER_READY}`); the HOUSE_FREEROAM default DESYNCS under load-stretch (fast port at the picker,
  slow retail still at PAUSE_READY) ⇒ 0 pairs. Arming right ⇒ **19 gap-free pairs** (port#0==retail#1 at +1443
  absolute, load-stretch-immune). Fixed the Jun-14 `win-0-200` (0 pairs) via a `--force` re-drive.
- **Committed canonical env-json** `docs/reference/parity-host-environment.json` (8 operator-attested
  EP-02/HOLE-3 fields, values match arrprobe's M0 bundle).
- **Proof_id reproducibility (finding §Tooling):** the proof_id binds `subject.port.git_commit` (by design) so it
  advances every commit — a specific id can't be self-cited in the commit that mints it; cite `contract_sha256`
  (stable) + the verdict, regenerate locally. En route, **fixed a real bug (commit `30243d3`):** `comparator_sha256`
  = `dir_manifest_sha256(tools/parity)` hashed `__pycache__/*.pyc` (12 of 24 manifest entries) — bytecode is
  interpreter/mtime-dependent, so the proof_id drifted on any import/recompile even at a fixed commit.
  `dir_manifest_entries` now prunes `__pycache__` + skips `.pyc/.pyo` (source-only; +2 fingerprint checks).
  Affects every bundle (arrprobe's too).
- **Stale-test fix (parity suite RED since `6c9c85d`):** the catch-fix renamed `ranking_records`→
  `encyclopedia_discovery` (dword 40566) but left `test_parity_save.py` asserting the old name ⇒
  `test_summary_collapse` IndexError. The pre-commit hook runs C host tests, NOT these Python suites, so it
  slipped through. Renamed the 2 test refs + a stale `save_producer.py` comment. Parity suite green (save 41 ·
  prove 51 · pixels 24 · observations 60 · fingerprint 52; `test_parity_schema` now validates 2 opted-in contracts).
- **Lead (drive-variance, ★NEXT b):** `occupied_playtime` is DRIVE-VARIABLE — across two both-runs the PORT swung
  20906→16878 (Δ4028f ≈ 67s) while retail held 29643→29683 (Δ40). So the save residual is not a clean constant
  phase offset: (b) must first explain the port's large per-drive playtime variance (`sim.c:310` live-scene-frame
  tick; suspect CreateThread-race load-bracket drift) before a `{phasepin}` can flip save to PASS + make the
  proof_id drive-stable.
- **Lead (commit-anim identity):** the commit-anim frames (offset 20+) don't identity-join — the port stays on
  SAVE_PICKER_READY while retail re-anchors to PAUSE_OPEN during the disk write (180 retail-only PAUSE_OPEN
  frames). The save is scenario-scoped so it proves the persistent OUTCOME regardless; the anchor divergence is a
  separate arc. No `parity-proof-index.json` entry (FAIL bundle — that RUNTIME-axis index advances a VA only on a PASS).

## 2026-07-16 — Save-pillar catch FIXED: the encyclopedia-store re-init (the `DAT_095d3728` verify-sweep gate)

The `save` pillar's first real FAIL attributed a REAL port bug (`ranking_records` banks 1–99); now CLOSED,
byte-exact vs retail. Commit pending; finding `findings/parity-save-producer.md` §Leads.

- **Region was a MISNOMER:** dword `0x9e76` ("ranking_records") is the **encyclopedia (図鑑) discovery store**
  (`encyclopedia.c` ENC_DISC_BYTE 0x279d8; `FUN_0049f012`=`encyclopedia_setup`, mislabeled "RANKING" by the port
  author per `scene_title.c:740`). Record `{category_key@+0, catalog_count@+1, discovered_flags@byte8+}`. State-map
  renamed `ranking_records`→`encyclopedia_discovery`.
- **Root cause (3-way seed/port/retail bytes):** the seed's banks 1–99 are never-committed slots — valid magic but
  an **unstamped checksum (stored `0x0` ≠ computed `0x345e7bcf`)** + a key+count a prior title-図鑑 open populated.
  Retail's `FUN_004901c2` **gates the per-bank verify sweep on `DAT_095d3728`** (set on save-load, `FUN_004902fe`)
  ⇒ loaded banks preserved verbatim (**`retail==seed`, 0 diffs**). The port's `save_bank_init_all` IGNORED the gate
  ⇒ always swept ⇒ the stale-checksum banks failed `save_bank_checksum_ok` ⇒ `save_bank_init_one` re-inited them,
  zeroing the key+count + re-stamping the checksum (66 dwords/bank). Invisible to every pixel/frame pillar.
- **Fix:** modeled the gate — `g_save_bank_skip_verify` (save_bank.c/h; = engine `DAT_095d3728`), gate the sweep
  (part 2 of `save_bank_init_all`) on it, set it in `save_io_try_load`'s 3 load buckets (before `init_all`), reset by
  `save_bank_arena_clear` (host-test "process restart" / BSS-zero on real boot). +2 host tests
  (`save_bank_skip_verify_preserves_stale_bank`, `save_io_load_preserves_stale_checksum_nonactive_bank`); host 3432/0.
- **VERIFIED `--target openrecet` re-drive:** banks 1–99 `port^seed=0`, `port^retail=0` (all 99 byte-identical);
  **save diff `6836 → 6` bytes** — the 6435 encyclopedia + 394 banks-1–99 checksum diffs GONE. Only bank-0
  `occupied_playtime` (2, phase-origin frame count port `0x4095`/retail `0x73cb`) + its 3-byte checksum echo + 1
  `(unmapped)` byte remain — all bank-0-only, all non-logic. The save pillar is now a clean near-PASS.

## 2026-07-16 — Parity evidence: ST-00/ST-01 — canonical state model + `save` pillar PRODUCER (M1)

The `save` proof pillar (the persistent-state axis, invisible to every frame pillar) gets a REAL PASS/FAIL,
not `NOT_CAPTURED`. Commits `fe4101f` (ST-00) + `33f706b` (ST-01); finding `findings/parity-save-producer.md`.

- **Survey first:** capture needs NO new engine/agent work — a `scenario-test --target both` drive already
  writes two byte-comparable 18.8 MB `save.dat` (port `--save-write-dir` → `run/openrecet/saveout`; retail
  CreateFileW/A Frida hook → `run/retail/saveout`; both seeded from the same `{savefile}`). Missing = only the
  comparator.
- **ST-00 canonical state model:** `schemas/state-map-v1.json` (named region layout of the save/working arena
  from `save_bank.h`; RE correction: header dword 7 = last_slot_used, dword 6 = hidden_char_unlocked) +
  `reference/canonical-state.md` (4 state classes + float-bit / no-pointer / unknown-included / checksum-derived
  policy) + `tools/parity/state_map.py` (offset→`bankN/region[elem]` localizer, fail-SAFE `(unmapped)`).
- **ST-01 save pillar producer:** `tools/parity/save_producer.py` (`compare_saves` diffs the two arenas +
  localizes first div + buckets diffs by region; FAIL-CLOSED on missing/wrong-size) + `save.py` adapter +
  `parity_save.py` CLI + `parity_prove` wiring (`save` off `UNBUILT_PILLARS`) + `test_parity_save.py` 41 checks
  incl. the M1 one-byte-mutation negative test. Parity suite `41+24+51+60+52` green; pre-commit 27/27.
- **First verdict `house-pause-save-commit`: save FAIL**, 6836/18,838,832 bytes across 4 regions — first div
  `bank0/occupied_playtime` (phase-origin frame count 20906 vs 29643), a derived `checksum` echo (banks 0–99),
  and a REAL `ranking_records` divergence across banks 1–99 (port fresh-bank init / checksum-gate ≠ retail for
  UNUSED banks — a genuine catch no frame pillar sees). Roadmap M1 (first-state localization).

## 2026-07-16 — Parity evidence roadmap: pixels pillar PRODUCER (M0's last required pillar now real)

Built the headless `pixel-metrics.json` producer the `pixels` adapter shipped waiting on (roadmap rule 11:
consumer before platform). The `pixels` pillar now gets a REAL PASS/FAIL from a bit-exact per-frame `differ`,
not `NOT_CAPTURED`. Commit `8514b9d`; finding `findings/parity-pixels-producer.md`.

- **`replay.exe --render-dump <wanted.txt> <outdir>`** — resident RGB dump (mirrors `--verify-hashes`,
  writes pixels instead of hashing). RT-correct: `has_rt` ⇒ render `0..max(wanted)` in order on the resident
  device (cross-frame render targets accumulate), dumping only the wanted; RT-free ⇒ render wanted directly.
  One process, no per-frame spawn, no GB raw dumps. (Per-frame `--upto` spawns render on a FRESH device ⇒ RT
  reads black ⇒ wrong pixels — the trap the producer avoids.)
- **`tools/parity/pixel_producer.py`** — pure core (`build_pixel_metrics`/`wanted_and_map`, injected renderers
  ⇒ unit-tested) + the replay.exe driver. `differ = pixel_diff.amplified_diff` (retail=A, port=B — the one
  canonical metric); stamps `source` container SHA-256s (== what `orv3_view` bakes ⇒ EP-08 provenance PASSES on
  a re-driven window). FAIL-CLOSED: no render / dim mismatch / unpaired required frame → raise.
- **`tools/parity_pixels.py`** CLI (two-step: `orv3_window --view` → `parity_pixels` → `parity_prove`).
  **`tools/test_parity_pixels.py`** 24 checks. Parity suite `24+52+60` green; pre-commit 26/26.

**Verified `house-firstcust-arrprobe` [1,80]:** pixels FAIL (first div `HOUSE_FREEROAM#1+1`) ⇒ proof is
`identity PASS · render_program FAIL · pixels FAIL`. TRUTHFUL: our most human-confirmed-1:1 scene is visually
1:1 (`gt8` 3–5 px/frame — the FRONT "2–3 px accepted" figure) but NOT bit-exact (±1 sub-perceptual cross-target
noise, `meanabs`≪1; near-black off=2 fade frame dominates `differ` 517046/786432 while invisible). `mode:
exact` strict bit-equality is the honest gate and legitimately FAILs cross-target; a "visually 1:1" contract
would need an R3-approved threshold mode (schema extension), not a silent comparator tolerance. **Next per the
roadmap: ST-00/ST-01** (canonical state model + save equality, M1).

## 2026-07-16 — Parity evidence roadmap: EP-08 — HOLE-2 close + cache re-key by full provenance (Wave-0 COMPLETE)

Closed the M0 adversarial-review **HOLE-2** (`findings/parity-M0-adversarial-review.md`): the
container-provenance defense (`verify_source_containers`) was built + adapter-tested but **dead in the
CLI** — `resolve_observations` passed `expected_containers=None`, so a foreign/stale metrics doc
(matching frame keys `(anchor,occ,offset)`, DIFFERENT source capture) would be trusted → false PASS. This
was the named hard gate before any pixels/state producer's PASS could be trusted. Two commits, both
gated by host tests. Full story: `findings/parity-EP08-cache-provenance.md`.

**(B) Bind content pillars to the window's container** (`62ece6e`): `orv3_view.write_view_json` bakes
`port/retail_container_sha256` = `sha256(v3cap.bin)` (the content hash, not just the WSL path it already
had) into view.json — the EXACT container this window's identity join + draw report came from.
`parity_prove.resolve_observations` reads them → threads as `source` (the in-process render bridge's
provenance claim) **and** `expected_containers` into `adapt_render_program`/`adapt_pixels`. A pre-EP08
view without the hashes ⇒ the check is skipped + a caveat is emitted (never a silent trust); the function
now returns caveats (4-tuple). Regression `test_parity_prove.test_container_provenance` (+8, 52/0):
foreign source → INCONCLUSIVE (the HOLE-2 attack), omitted-source-under-bound-view → INCONCLUSIVE,
matching → PASS + `render-metrics.json` stamped, legacy → skip+caveat.

**(A) v3 studio cache re-keyed by full provenance** (`5713074`, roadmap §6 EP-08): the key was
`sha256(trace+arm)[:8]` (32 bits, trace+arm only) — a rebuilt d3d proxy or edited frida agent **never**
invalidated the cached container, so B's hash was only trustworthy once the container itself is
provenance-bound. Now: **128-bit SHARED dir key** = `sha256(common_provenance)+arm` over
`{cache_schema, trace (⇒ {savefile} save), proxy, assets_manifest, recet.ini}` (a proxy/assets/trace/
schema change re-drives BOTH sides); **per-side** `{pe_sha, agent_sha}` in `v3meta.prov` validated on
lookup (`side_provenance`/`_staleness`) so a rebuilt exe (port or retail) or edited agent re-drives ONLY
that side — a port fix STILL never invalidates the retail cache (the load-bearing invariant). Corrupt
(missing/empty `v3cap.bin`) rejected; every stale decision logged with its reason (pre-EP08 /
shared-drift / per-side-drift / corrupt). `test_orv3.test_provenance_keying` (+1) monkeypatches the
provenance paths to temp files and flips one byte at a time to prove each input invalidates the
appropriate side, 128-bit width, corrupt + pre-EP08 rejection, and prov round-trip through v3meta.

**One-time consequence:** every pre-EP08 entry (8-hex key, `prov=None`) is now STALE ⇒ the next
`orv3_window` re-drives it once (retail = the serialized load-stretch, minutes); old dirs orphan under
`runs/studio-v3-cache/` (gitignored, regenerable). **Residual (logged):** the proof `tools` group is still
current-on-disk — thread `v3meta.prov` into `gather_provenance` (a caveat discloses it); HOLE-3 (env
attested) + HOLE-4 (exceptions not gate-enforced) remain by-design (CI-05 / EP-07). **Wave-0 EP-00→EP-08
is COMPLETE.** Next: a pixels/state PRODUCER, now UNBLOCKED — the headless `pixel-metrics.json` producer
(so `house-firstcust-arrprobe`'s last UNPROVEN pillar `pixels` gets a real verdict), then ST-00/ST-01.

## 2026-07-16 — Parity evidence roadmap: EP-06 truthful two-axis port ledger (Wave-0 step 5)

Killed the ledger's runtime-verified overclaim. `gen_port_ledger.py` derived a single `status`
from source scanning yet rendered `verified` as "runtime-diffed vs retail" (headline "2.8%
runtime-verified") — INVENTORY dressed as RUNTIME. Rewrote it as **two independent axes** (roadmap
EP-06; "do not collapse to one strongest label"):

- **INVENTORY** (src markers): `discovered → source-referenced → implemented → instrumented`. A bare
  `FUN_<va>` reaches ONLY `source-referenced` (a mention, not a port claim); `implemented` needs a new
  opt-in `PORT-OF(0xVA)` attestation or a probe; `CALL_TRACE_ENTER(_STUB)` = `instrumented` (+ a `stub`
  quality flag).
- **RUNTIME** (proof artifacts): `retail-executed → port-executed → call-I/O-aligned →
  scenario-pillar-proven → matrix-proven`, each requiring a `docs/parity-proof-index.json` bundle
  (git-tracked, hashes only; the generator never reads the gitignored `runs/proofs/`).

Index empty today ⇒ **`runtime_proven = 0`**. STATUS flips "2.8% runtime-verified" (a lie) → "0%
runtime-proven — 85 instrumented, index empty"; the 501 "ported" are now honestly `source-referenced`.
The two axes correctly diverge for the executed-but-unimplemented quadrant (retail-executed yet
`inventory=discovered`). Compat: per-function `status` kept as a DEPRECATED alias (mem_watch
byte-stable), `counts` keeps every legacy key, `--check` exit-3 idempotence unchanged; no proof
fabricated for old entries (human confirmations stay separate, EP-07). Gate:
`tools/test_gen_port_ledger.py` (152 checks) — all three EP-06 acceptance criteria + proof-index
fail-closed + live-tree consistency; full Python suite 25/25. Commit `2f9bad7`. Design:
`findings/parity-EP06-ledger-lifecycle.md`. **Next: EP-08** (close HOLE-2 — the container-provenance
hard gate before any pixels/state producer PASS).

## 2026-07-16 — Parity evidence roadmap: M0 reached (Wave-0 step 4, R3 adversarial review of one proof bundle)

Continued the parity-evidence-roadmap arc (EP-00→EP-05 landed last session). Wave-0 step 4 =
"R3 adversarial review of one proof bundle": compiled the **first real proof bundle** and tried
to fool the gate on real evidence. Full writeup: `findings/parity-M0-adversarial-review.md`.

- **First real corpus contract** authored on `house-firstcust-arrprobe` (the USER-CONFIRMED-1:1
  first-customer drive): a `scenario.yaml` `proof:` block scoped to the gap-free `HOUSE_FREEROAM#1
  [1,80]` window (the join's only gap there is offset 0). `test_parity_schema` now validates it.
- **Bundle** `9bc05dd8…` = truthful **FAIL/exit 1**: `identity PASS` · `render_program FAIL @
  HOUSE_FREEROAM#1+1` (tex `d445…b494`, port 0 / retail 80 tris — the known b494 80-tri 0-px
  retail-only warm-up strip) · `pixels`+5 later-package pillars `NOT_CAPTURED`. So our *most-
  confirmed* scene is pixel-1:1 by eyeball yet **not tool-proven parity** — the gate refuses to
  launder human confidence into a PASS. All three M0 exit conditions demonstrated on real data.
- **HOLE-1 (FIXED) — `proof_id` was NOT portable.** A `NOT_CAPTURED`-by-absence pillar baked its
  **absolute probe path** into `observations.<p>.note` + `pillars.<p>.detail` (both hashed), so the
  same logical run at a different checkout dir hashed to a different id — violating §4.4 + the EP-02
  portability acceptance, and biting *every* current bundle. Fix: `observations.portable_reason()`
  scrubs abs dirs → basename in `not_captured`/`inconclusive` (the only builders of hashed reasons);
  + regression test `test_proof_id_portable` (same window at 2 abs dirs → identical id). The
  synthetic tests missed it — they build at one tmp dir and only check *relative* determinism.
- **Logged (not review-fixable):** HOLE-2 — `parity_prove` never threads `expected_containers`, so
  a foreign metrics doc with matching frame identities would be trusted (the container ids in
  `view.json` are paths, not hashes → real fix is EP-08); **hard gate before any pixels/state
  producer**. HOLE-3 — env is operator-supplied/unverified (CI-05 needs the capture host to bind
  it). HOLE-4 — `exceptions[]` are recorded but not gate-enforced (descriptive until EP-07; the
  b494 exception does NOT green render_program — honest).
- **Verdict:** the M0 gate is sound + authoritative-ready for producer-backed pillars (identity,
  render_program); absent producers correctly fail-closed. EP-06 (ledger migration) may proceed.
- Tests: `test_parity_{schema,fingerprint,observations,prove}` all green (44 prove checks; +3 new).

## 2026-07-10 — Daily-NEWS subsystem ported: generator FUN_00436623 + picker FUN_004363c6 + trend classifier FUN_004361b2 live (FRONT target #2)

Closes the "news-list population" target: WHO writes the 20-entry featured-news list `DAT_0450ad68`
each day is **`FUN_00436623`**, now ported objdump-exact as `src/news_daily.c` (+16 host tests, 3424/0).
RE: `findings/news-daily-RE.md`.  The generator: (1) picks ONE new news.txt row per day (day-9 scripted
id 1; period-window pool, dedup vs active entries — same id / one generic / same attr-or-category —
特殊 rows rerolled, ≤100 tries), writes trend char (rate byte, 0→'d'), lifetime (dur_base+rng%dur_range+1,
min 2), and an rng-picked target item from the price window with the '<'+2-byte marker spliced to the
item's plural; (2) rolls the player-driven BOOM news off the 20-slot sold-pairs tracker (0x2dde4, writer
already ported): threshold rng%100 by multiplicity {4:10..8+:100} (drawn even at p=0 — load-bearing),
rank≥9 gate, id 0x24/0x25 variant + duration rolls, clears the hot pairs; (3) decrements pair TTLs +
entry lifetimes, composing "The %s boom has ended."/"price of %s has normalized." expiry headlines
(quirk #132: the trend-0 branch uses the RAW target as a SLOT); (4) day≥10 rng-picks a day-range story
row (category -100); (5) computes the ticker scroll offsets (strlen+4).  En route fixed `news_record_t`'s
misnamed fields (+0x94/+0x98 = LIFETIME base/range, +0xac/+0xb0 = target-item PRICE window — proven by
the generator asm; parser+tests renamed).

**★ VERIFIED 1:1 via the live golden gate (same session): 18/18 samples (3 arena variants × 6 seeds)
BIT-EXACT vs retail FUN_00436623 — list entries, headline BYTES, offsets, pairs, rng-draw count AND
`final_seed`.**  Harness `src/news_golden_replay.{c,h}` (OPENRECET_NEWS_GOLDEN) + `tools/news_gen_capture.py`
(arena diff-poke restore; variants natural/expiry/boom exercise every phase).  Methodology unlock: the agent
callq now snapshots DAT_006023a0 on the engine thread before AND after the call (`seed_at_call`/
`seed_after_call`) — round 1 read the final seed via a separate RPC racing the resumed sim and "diverged"
on 7/12 samples (deltas 2..37 = stray bg-sim draws); the atomic window closed all of them.  Also landed the
user hard rule: probe kills target OUR daemon.json pid only (daemon start-reap + kill_retail.py --pid/--all;
an OpenLords2 probe was live on the host mid-session).

**All 3 call sites wired, all gated `SHOP_DAY > 8`** ⇒ zero rng/pixel change on every existing trace
(verified: fresh arrprobe port drive vs the pre-change v3 cache — rngcalls bit-identical 1723/1723
frames, seed 1722/1723 with the single diff at the known wandering load seam, count-neutral):
customer-leave restore (rng%3 mid-day news break, customer_service.c), the iv2 morning-beat start
(f488 day-9 arm + npc_schedule_apply(0) + generator, scene1_tutorial_dispatch.c), the b92c ticker pump +
news-jingle SE 0x2bd @0x1e (scene1_player_ctrl.c W1).  **The FUN_004361b2 price-trend classifier is now
LIVE** (was PORT-DEBT(cs-price-trend) stub 0): `cs_news_price_trend` binding (head gate b1c0/maptype/
cc08==4/f404) feeds the haggle round-0 tilt + the price-panel tint/label (High/Base/Low, asm 5-level
colour table ff0000/ff4d4d/7f7f7f/4d4dff/0000ff) + the merchant-HUD name colour.  New PORT-DEBT:
news-ticker-render (FUN_00436f97 draw + b92c consumer), news-clock-advance (the unported timed shoptime
mechanic's call site), day9-morning-arm (f488's b924==0x276 consumer).

## 2026-07-04 — DAY2 day-transition RENDER gaps: actor re-placement (#4) + companion ease (#4b) — pixel-1:1

After the tutloadpin arc bit-frame-aligned the whole cutscene (Δ0 ~15000f), a DAY2 pixel confirm surfaced
5 pre-existing DAY2 render gaps the anchor-Δ0 had MASKED (anchors track sim events, not overlays/positions).
Prior sessions landed #2 (HUD live-read "Day 2", `c63ee20`) + #1 ("Day 2" title card, `a77c46b`).  This
session closed the two big actor gaps, both RNG-neutral (position-only) and pixel-confirmed:

**#4 actor re-placement (`e66e475`).**  The port left Recette + Tear at the stale customer-service COUNTER
positions (px 0.796 / cx -0.694, mirror-SWAPPED) into the day2 broom; retail re-seats them at the
house-standing pose (px -0.30 / cx 0.6) at the day-advance via the scene-entry re-place `FUN_0048526d →
FUN_00436f97`, which the port's dialogue-load iv2 model skips.  **The prior session's "RENDER BLOCKER" was
REFUTED** — body sprites read `g_scene1_actor_pos[i]` directly (`scene1_shop_walker.c:779`, no snapshot) and
`g_scene1_player_pos` aliases `actor_pos[0]`, so a sim re-place IS the render source; cached-trace ground
truth showed cc08/panim/canim already matched — ONLY positions diverged.  Fix: `scene1_postload_day2_actor_
replace()` (positions-only) re-seats the actors, armed one-shot at the iv2_5 beat + consumed at the
default-arm top → fires @15470 (retail's day2); the pose driver re-derives the face-each-other octants.
VERIFIED: px -0.30/poct 6, cx 0.6/coct 2, RNG 0-diff, host 3394/0; pixel-confirmed before(swapped)→after(retail).

**#4b companion ease (`64bd404`).**  Tear eases cx 0.6→1.0 across the beat in retail via `FUN_0048a833`'s
ELSE-branch (all.c:89434-73, `b928==1 && b924<200`): a FIXED ±1.3 X-offset spring on the player's side at
factor 0.1, no CO_THRESHOLD/vel-clamp.  The port modeled only the free-roam branch (`FUN_0048a4d1`, threshold
1.5 / 0.15) so cx held 0.6.  Ported as a beat-gated branch in `scene1_companion_ctrl_tick` (CO_INTRO_SPRING
0.1, target player_x±1.3).  VERIFIED **MAX|Δcx|=0.0 over the full 190-frame beat** (settles 1.0, retail phase
exact), RNG 0-diff over 25000f, host 3394/0; pixel-confirmed (Tear moved from hugging Recette to spaced right).

Full story: `findings/cutscene-replay-anchor-drift.md` §2026-07-04.  **OPEN DAY2 residuals** (all fresh arcs):
(4c) day2-dialogue portrait ~40f ahead of the port's box-open @15878; #3 "Now Loading…" disc (iv2 load never
calls `nowloading_set_active`); #5 wing-sparkle (minor); #1 b924 fade-seam (cosmetic).

## 2026-07-02 — day-end cutscene: served customer now DESPAWNS at leave — FUN_0046f892 ported (notes #24/#25; RE §21.33)

Viewer notes #24/#25 flagged a chibi customer still roaming the shop floor (and bleeding through Tear's
hair) through the whole day-end CONV_POSE cutscene — retail has none.  `orv3_draws --material`: port drew
one EXTRA body (tex 747d) + shadow (tex 16d2); per-frame count traced the customer as 2/8 through the sale
then retail→1/7 exactly at CONV_POSE_START#2 while the port stayed 2/8.  Root cause: the customer-leave
restore (FUN_00462403 @60337) calls **FUN_0046f892** (cs-NPC array reset → cap=0, all slots ACTIVE=-1),
which the port had DEFERRED in PORT-DEBT(cs-leave-restore) for RNG-neutrality — correct that it draws no
LCG, but it is NOT render-neutral (both cs-NPC renders gate on cap/ACTIVE), so the served customer never
despawned.  PORTED `scene1_customer_npc_reset()` into the leave block (retail order, after b7b0=0, before
the §21.32 shoptime++).  Verified: port re-drive hash-verify **2887/2887 BIT-EXACT**; 747d/16d2 now
**1/7 == retail on every cutscene frame (0 diffs)**, sale region untouched (2/8); notes #24/#25 diffs
**BLACK**; raw rng still bit-exact at both note frames (reset perturbs nothing); +0 host-test regressions.
Narrows PORT-DEBT(cs-leave-restore) to FUN_0048439a/FUN_00473332/FUN_0045e028/octant.  Detail: RE §21.33.

## 2026-07-02 — day-end leads cracked: the "+261-rng day-end consumer" = the SALE-COMMIT coin shower; the post-sale story chain iv1_8→iv2_6 ported — the day2 trace now replays END-TO-END

Both FRONT day-end leads resolved in one arc (RE §21.31/§21.31.1):

- **(a) +261-rng frame REDIAGNOSED** — not next-day regen but the sale-commit burst: the f404==0 accept
  block (gold+=ask, SE 0x14d, FUN_00460d52 → stats dword +0x2c3e0 += ftol((ask/base−1)·100)+signed
  ftol(sqrt(|ask−base|)), Table-A alloc FUN_004132c1(304,128,entry 100,1.0,−1,4), SE 0x17b/0x156).  Parent
  entry 100 = 5 sub-records (templates 173/170/171/172/176, all age_match 0) → 69 particles (28+8+8+8+17),
  207 float + 54 int rng = the +261.  f404 IS 0 on the tutorial sale (old "inert for tutorial" claim wrong).
  PORTED: `cs_sale_commit_stats_fx` + gold + SEs in customer_service.c.  Probe chain: {rngcs}, custom
  call-trace VAs (spawn+wrappers, then allocators), per-segment {memsnap} (NB memsnap ops DROP when their
  segment's {wait} passes — arm in the segment containing the target frame).
- **Template sets 1-3 were never loaded** — pfo_load_one_file loaded only effect1.dat's secondary chunk;
  engine freads ALL FOUR at DAT_00733820+file_idx·0x4330 (one 400-template table).  TEMPLATE_COUNT 256→400,
  `scene1_overlay_templates_load_chunk_at(set,…)`; burst now fires (260@commit+1, mirroring retail's
  SE→spike+1 attribution 14846→14847).
- **FUN_00406584's money rolling-counter wired in INGAME** (sim.c) — DAT_0438b918 eases toward bank gold,
  ONE LCG draw per rolling frame (retail's 0x40688d draw at the spike = the last +1 to 261); rng-load-bearing
  after any sale.
- **(b) there is NO separate day-end load path** — the post-sale flow is the FUN_0044bd0d story chain
  iv1_8 → iv2_1 → iv2_2 → iv2_3 (DAY ADVANCE: fb84++, fb88=0, f400 clear…) → iv2_5 → iv2_6, ported into
  scene1_tutorial_dispatch.c on the existing start_single load bracket.  **The day2 trace's stalling
  {wait: LOADING_START} releases and the port replays the ENTIRE trace: 94/94 non-blink anchors, 0 name
  mismatches vs the retail capture; day-end LOADING_START@2273/CONV_POSE@2274 == retail's aligned frames;
  EXTRA_SPRITE tail cadence (41/239/81/119) frame-exact.**  Residuals: one natural-vs-pinned load duration
  (37 vs 7) + a +1 at that seam; PORT-DEBT(blackout-tut-dispatch) unwired on iv2 entries;
  PORT-DEBT(tut-dispatch-iv2-fx) = iv2_5's FUN_004852fb + b928/b924.
- **VERIFIED (drive-3):** burst **261@commit+1 == retail 261@14847**; sale-segment raw rng **bit-exact
  141 frames** (commit + whole shower spawn + money-roll count-up); post-load day-end segments at the
  known +1 seam (s37 shift+1 → 120/121).
- **✅ FANFARE PHYSICS CLOSED (drive 074133Z, RE §21.31.3):** two more port bugs fixed — the Table-A tick
  passed slot MODE instead of PARAM8 as shape_mode (asm `[esi+0x10]`; coins never got their SHAPE_MODE=4
  aim/landing physics), and the PFO.4 terminal gate `factor == 1.2f` NEVER passed under GCC x87 -O2 excess
  precision (**gotcha #19**: MSVC fstp-spills to float32 before the fcomp; port the equality as a
  bit-pattern compare; SSE host tests hid it — smoke FP-compare chips with `-mfpmath=387 -O2`).  Plus the
  FUN_0040656e landing pulse (kill default: shake timer=4 + SE 0x29d) and the FUN_00406584 shake-jitter arm
  (4 LCG draws/frame) in scene1_top_hud + sim.c.  VERDICT: 24/24 coins land (SEs 1985-2012, start ==
  retail's aligned 14897), gold count-up frame-exact, and the whole 428-frame sale segment is raw-rng
  **428/428 BIT-EXACT** vs retail.
- **OPEN — the "sale fanfare" arc residue (RE §21.31.2/.3):** (1) RENDER: the 69 particles spawn rng-exact but draw
  NOTHING (tpl 170-176, tex 20-30, shape 0, layer 0, MODE 1 projected; the tex-19 mode-0 sparkle draws
  fine) — v3 draw-program diff at the burst frame is the probe; (2) the Table-B coin-LANDING branch
  (slot kill + FUN_0040656e 4f-shake pulse + SE 0x29d per landing coin, all.c:12732) + FUN_00406584's
  jitter arm (4 rng/frame while DAT_00648280>0) = the +141 rng residual (port 1986/retail 14898);
  (3) the TOTAL-EXP popup chain FUN_004606fc → FUN_00485861 → FUN_00406159 (@(412,112), SE 0x174/0x172,
  the 0x648258→0xb4 fanfare timer).

## 2026-07-02 — ★★★ USER-CONFIRMED: "this whole trace is 1:1 now" (house-firstcust-arrprobe win-0-1500)

The first-customer trace — initial load, walk-in, free-roam, pause, tutorial cutscene, first customer, haggle —
is user-confirmed 1:1 end-to-end in the studio.  Covers notes #23/#24 (below) and the pending #8 choice-box
flash.  Ledger updated.  Residual: 2-3 scattered 1-px sprite-edge speckles/frame (accepted); open on this trace:
the (C) coct/cx facing blips + the v3 port replay hash-verify 5/2895 tool lead.

## 2026-07-02 — note #24 recette start-phase FIXED: pose_house_standing snapshot seed → fresh reset (RE §21.30)

Residual (A) closed.  The intro walk-in is bit-aligned through the load; at freeroam entry retail does a fresh
set-anim reset (0/0/0) while the port re-applied the pose_house_standing seed — counter 25/frame 2/timer 5.0f,
a runs/cchr2b STEADY-STATE leaf snapshot (HOUSE frame 17544), not the entry state ⇒ a constant 15-tick
idle-phase offset for ~45f (port wrapped at +16, retail at +41; the first pause realigned).  Seed → 0/0/0.
Whole-window player panim/pframe/pcnt divergences 45→0, companion 0, rng bit-exact; note #24 crop BLACK;
freeroam+39 full-frame 2973→3 px.  Both 2026-07-02 residuals (A)+(B) now closed pending user studio re-confirm.

## 2026-07-02 — note #23 vase shadow FIXED: fade.c ALPHAREF↔ALPHATESTENABLE mistranscription (RE §21.29)

Residual (B) closed.  Root NOT the shadow pass: fade.c (FUN_00453e8f) ported the engine's
`SetRenderState(0x18,0)` = ALPHAREF=0 as ALPHATESTENABLE=FALSE ⇒ every pause/fade frame left alpha-test OFF
device-wide; the next frame's mesh pass inherited it, the flower item's semi-transparent fringe texels z-wrote
(no alpha-test kill) and clipped the display-stand shadow decal's upper arcs (the note-#23 crescent).  Probe
chain: single-frame slice + orv3_shot draw-bisect → decal quads bit-identical, pre-decal color bit-identical ⇒
z-buffer → full-state diff at the draw → RS 15 retail=1/port=0 → ATE timeline → the only FALSE-setter.  Also:
fade's L16-18 fog+MAG/MIN-filter writes are pre-gate UNCONDITIONAL (were skipped at counter==0, + were
MIP+MAG instead of MAG+MIN), and 2 walker tail ALPHAOP `TSS(0,4,4)`=MODULATE were ported
BLENDDIFFUSEALPHA(12) — the value-vs-name gotcha (pixel-neutral, program-parity).  Verified on the re-driven
win-0-1500: note crop BLACK, pause frames 125→2 px (2 scattered 1-px speckles), freeroam+39 residual = ONLY
the Recette sprite (= residual A).  3381 host pass.

## 2026-07-02 — anim seed-origin arc USER-CONFIRMED "basically fully 1:1"; +chip (a) leave-frame fix (RE §21.28.1)

Closed the last two roots on house-firstcust-arrprobe: root 5 (the cs-LEAVE frame @631 ran the free-roam
companion law instead of retail's tick-only cc08==4 arm = the +20 pose-era wing-cycle offset; new
left_4_this_frame marker cleared frame-top in scene1_ingame_tick so it lives exactly one frame in BOTH arms —
the first attempt cleared it in the player tick, which skips event frames, and it latched through the whole
632-824 dialogue era freezing the spring; the verify drive caught it).  Whole-window result:
cframe/ccnt/ctimer/canim ✓ aligned [224,1722] (930 divergent cframe → 0), customer n0* aligned, raw rng
bit-exact 225→1722, all 3 viewer note crops diff BLACK.  USER: "the trace is basically fully 1:1."  Commits
2038905 + ae44071.  Remaining residuals (small, next): (A) recette phase at the very start = player pframe/pcnt
window-start load region (@224); (B) a tiny vase-shadow diff on the counter (object-shadow bug lead, to probe
after /clear).

## 2026-07-02 — the ★★★ anim seed-origin arc CRACKED: 4 tick-cadence roots fixed, notes #20/#21/#22 (RE §21.28)

The FRONT "chr_anim seed ORIGIN" diagnosis resolved into four probe-proven roots (new declarative probe
fields: companion ccnt/ctimer + cs-walker slot0 n0anim/n0frm/n0cnt, port CALL_TRACE + retail_fields.json):
(1) the conv-pose latch-release ignored FUN_0048407f's `cc08 != 4` gate — at the f406 entry retail leaves the
STALE talk anim on Tear through the d3e load (idle law rewrites+resets at 851); the port's forced release at
826 reseeded the wing cycle 25f early = the permanent cframe offset (note #21).  (2) cc08==4 frames order
anim-SET before ONE unconditional frame-tail tick (transition frames end counter=1; free-roam cc08==1 is the
opposite order and keeps the §81 skip rule — probe frames 273/286 vs 850/332).  (3) the cc08 1→4 ENTRY frame
ticks nothing (`goto LAB_004893ff` past FUN_004897c6; new player_ctrl_cc08_entered_this_frame + the §21.18
hold skips its anim ticks).  (4) the cs-walker set-anim (FUN_00482a51 ×3 in FUN_0046fbee, Ghidra-dropped
args, objdump-ground-truthed: walk=1 wstates 0/1, dwell=0 wstate 2) was never ported — the browsing chibi
SLID in the idle pose while retail walks (notes #20/#22 were never a phase slip).  Verified vs fresh retail:
cframe 930→26 divergent frames (residual = the pose-era carry [825,850], separate chip), canim/panim/n0*
fully aligned [224,1722], raw rng bit-exact 225→1722, 3381 host pass.  Commits 6f0993b + 2537904.

## 2026-07-02 — ESC-modal arc USER-CONFIRMED; choice-box commit FLASH ported bit-exact (note #8, RE §21.27)

User confirmed the §21.26 arc ("can confirm everything matches"): box-arm timing, dialogue-under-box,
and the #7/#19 double-composite are 1:1 (ledger entry).  Follow-up flag (note #8): retail flashes the
chosen "Yes" on confirm.  Root: the deferred FUN_0043537e commit anim — while close-counter ac14 < 4 the
CHOSEN label draws at `0x7f − ftol(sin(ac14·π_f/4)·(−128.0))` → 217/254/217 brighten under ADDSIGNED
(af30 picks Yes/No; cancel flashes neither).  Engine-quirk #128: the peak is 254 NOT 255 — the
float-rounded π/2 through the CRT double sin truncates 128·sin to 127; `sinf` would be 1 LSB off.
Ported in choice_box.c (double sin off the float-rounded argument).  Verified bit-exact on BOTH trace
instances: pause "go to bed" confirm (Yes-region max px diff 0 through the whole pulse) + ESC-skip
confirm (sub-LSB).  3381 host pass.

## 2026-07-02 — gap (ii) dialogue-under-ESC-modal RESOLVED (already fixed by §21.15; stale-window flag); #7/#19 REDIAGNOSED (not RT — a doubled skip-prompt draw pass)

Gap (ii): retail `FUN_0046c090` draws the dialogue UNDER the ESC box unconditionally — no hide/clear
exists; the "no dialogue behind retail's box" was arm TIMING (retail's re-post driver arms at line+1,
pre-reveal).  Note #7 was flagged on the Jun-27 window, one day BEFORE 98cbf08 (§21.15) landed the
port's mirror driver.  **Fresh v3 re-drive (win-0-1000, full current pin set): both sides open the box
at TEXT_ANIM_START#1+1, same absolute present (760→761), no dialogue under either box — bit-parity
on the timing; no port change needed.**  Bonus: with the full pin set BOTH sides now reach
HOUSE_FREEROAM#1 at present 224 (identity-join +0 absolute).  #7/#19 rediagnosis: `orv3_rt.py` on the
retail capture shows **0 SetRenderTarget/0 CopyRects** — the RT-composite theory is REFUTED (fix NOT
blocked on tooling); retail draws the skip-prompt block (strip+glyphs+label, 32 draws) TWICE — the
port once (modal draw Δ: port +32 vs retail +63; baseline Δ27 pre-existing).  Root (ret_va probe
runs/probe-skipbox-callers — the pause-block theory was ALSO refuted, FUN_004820ba fires zero):
**FUN_0040a765, the 2D-HUD aggregator, calls FUN_0043537e UNGATED in its tail (all.c:7046) + the
FUN_0046c090 tail = the two passes.**  PORTED: scene1_hud.c mirrors 7046/7499; choice_box.c's inline
cursor removed (retail sites are explicit FUN_0043537e+FUN_00435747 PAIRS — the inline call
double-drew); the customer_service_render.c "Cancelling tutorial?" compensation draw removed (no
retail CS-family site; the HUD-tail mirror covers it).  v3-VERIFIED: modal box-UI region 81==81
draws, per-texture draws+prims all equal (bit-1:1 draw program), box pixels ≤2 LSB; frame Δ27 ==
the pre-existing baseline; 3381 host pass.  RE §21.26.  New tool lead: the fresh PORT capture fails
replay hash-verify on 5/2895 frames (presents 239/542/661/2184/2303) — v3 replayer gap to
investigate.

## 2026-07-02 — day2 scenario gets the full determinism pin set; day-end rng fork pinned down

`house-firstcust-cutscene-day2/trace.jsonl` now carries arrprobe's full pin header ({csloadpin:24} +
{primaryloadpin:16} + {tutloadpin:8} + {bgnpcseed 3502407629/1/dead-slot}; same savefile ⇒ same
naturals).  **VERIFIED `--target both`:** raw rng bit-exact frames 225→1934 = the whole
first-customer region (arrprobe's confirmed span: 225→1722); identical 37-anchor sequence; the
initial Continue-load 1505→223.  The 1935+ fork = the UNPORTED day-end: retail draws +261 rng in
one frame at the day-end Z (PAUSE_CLOSE#3+89) and the port's day-end transition emits no
LOADING_START anchor (music swaps at 2274 == retail's load 2273) — both leads recorded in FRONT
under the day-2 arc.  Method note: the whole-capture `flow_diff --verdict` DESYNC signature
(bgx "DRIFT @81", rngcalls "@3") appears even on the user-confirmed-1:1 arrprobe capture — it is
the accepted pre-pin region + probe print-precision; judge day-region parity by the aligned span.

## 2026-07-01 — first-customer trace rng/NPC/db054 FULLY ALIGNED — USER-CONFIRMED ("npcs aligned, customer aligned")

The 2026-07-01 FRONT cleanup condensed the 06-19→07-01 arcs into the entries below; full
narrative: `archive/FRONT-2026-07-01-full.md`, RE §21.x in `findings/customer-service-haggle-RE.md`,
verdicts in `findings/confirmed-parity-ledger.md`.

- **§21.21/§21.22 `{bgnpcseed}`** — bilateral bg_npc warmup-ORIGIN pin (LCG seed 3502407629 + spawn
  cursor 1 + the dead-slot raw record, consumer-latched inside `scene1_bg_npc_tick()`); dead-slot
  SHADOW fix (shadow pass checks only `visible==-1`, drew the leftover x/y/z).  bgx0..5 bit-exact
  retail [224,825]; stray contact shadow gone (note #25).
- **§21.23/§21.24 db054 +1** — diag probe pinpointed frame 632; arm-selector hypothesis REFUTED.
  Root: db054 rides the `FUN_0048b850` move tail; retail's `FUN_0048670f` if/else on FRAME-START cc08
  skips it on BOTH cc08==4 edge frames; the port re-read the LIVE cc08.  Fix: `cc08_at_dispatch`
  snapshot + gate on snapshot AND live (`scene1_sim.c`).  db054 aligned [224,1722].
- **§21.25 frame-1016 rng** — the `{bgnpcpin}` SoA inject lands 1f late on the port; redundant under
  `{bgnpcseed}` ⇒ both sides skip the inject when both pins present (`main.c` + `frida_capture.py`);
  raw rng cumΔ=0 past 1016; offer 119 / variant 1 bit-identical.
- **§21.18 CONV_POSE_END −1** — hold the conv-pose STATE 1f at the f406 entry
  (`player_ctrl_cc08_f406_pending`), `_posing()` untouched ⇒ every win-0-1500 anchor +0.
- **§21.19 `{gsimpin}` REMOVED** (`9e1db6f`, stale post-§21.18 calibration forced gsim 1 behind;
  do NOT re-add) ⇒ gsim%8 0/200.

Remaining (→ FRONT): the anim SEED-ORIGIN phase class (tear wing-flap cframe + customer walk-cycle).

## 2026-06-30 — free-roam region frame-aligned +0 (racy d3e load + D_TUT_DONE settle); chibi FACING ported

- **§21.16** (`2bae088`): the cc08 d3e CreateThread completion is RACY (28f vs retail's 24f);
  `{csloadpin}` was min-only ⇒ now forces worker completion at frame N (spin on the completion FLAG)
  ⇒ deterministic 24f; the whole pre-wrap-up region (arrival→tutorial→offer decision) +0.
- **§21.17** (`b95b498`): the ESC-skip teardown bypassed the 1-frame D_TUT_DONE settle the natural
  completion includes ⇒ whole region 1f ahead.  Skip → D_TUT_DONE.  HOUSE_FREEROAM +0; bg_npc window
  positions bit-identical.
- **Chibi FACING**: both engine recomputes (idle `FUN_0046fbee` FACE_DIR cardinal; velocity
  `FUN_0047019f` atan2 tail — both x87 octant conversions Ghidra-DROPPED, objdump-recovered) reuse
  `player_ctrl_facing_octant`.  Notes #13/#15/#16 diff BLACK; USER-CONFIRMED.

## 2026-06-27/28 — cc08==4 arrival + offer + hand cursor + the determinism foundation (RE §21.10–§21.15)

- **§21.10.1** (`a93413a`): master tick gated on frame-start `b1cc_pre`; arrival/ground_y/anim
  UNGATED ⇒ arrival bit-identical THROUGH the d3e load (retail's background load doesn't raise the
  load screen); first −1 drift + studio note #1 were one root.
- **§21.10.2** (`dd98991`): companion ctrl inert during the cc08==4 load (idle anim only), walk-in
  after b1cc clears — canim/cx/octant bit-identical.  Arrival region USER-CONFIRMED "nicely synced".
- **§21.11.3 the OFFER**: the port cleared the ESC-skip b150 INLINE at b534 1→2; retail clears it
  the frame AFTER ⇒ PAUSE_CLOSE + the L90 `{rngseed}` re-pin 1f early ⇒ offer 120.  Deferred the
  clear 1f ⇒ offer 119 / poseR 3 == retail, 699 offsets zero rngΔ.  USER-CONFIRMED.
- **§21.12** (`9f6c19e`): the haggle-prompt HAND CURSOR (`FUN_00435747`) at the house-aggregator
  tail + 8 driver sites (scripted/poll/live).  USER-CONFIRMED.
- **§21.13/§21.14 determinism**: the reaction-variant "divergence" was a config-mismatch artifact
  (stale L90-DROPPED retail cache); TWO fresh retail drives = 700/700 rng values bit-identical ⇒
  retail IS run-to-run deterministic and the port matches a FRESH retail bit-for-bit (offer 119,
  variant 1 "Capitalism, ho!").
- **§21.15** (`67c8564`/`98cbf08`/`46f837b`): BARGAIN-banner fade + skip-event box + `{tutloadpin:8}`
  (the iv1_7 D_TUT load bracket; standee position #8 + load-end fade #9).  **#12** (`9380a92`):
  choice-box alpha ramp `(cb_active/4)·255`.  **#7/#19 root-caused**: the engine double-renders the
  whole frame (focus/blur RT composite) — port blocked on a v3 RT-capture extension (→ FRONT).
- **WALL-CLOCK pin REFUTED** (time-source sweep; QPC feeds only frame-pacing) — settled verdict.

## 2026-06-23–25 — determinism pillars: `{csloadpin}`, the RE §21 survey foundation, wrap-up softlock

- `{csloadpin}` load-bracket pin (`4eeb88a`/`9b92b0a`, RE §20) + the v3-harness re-arm RACE fix
  (`9c455f3`, §20.1 — the "offer 117" artifact).
- RE §21.2–§21.5 (`b2ba55f`/`2207c1a`/`d9abe4e`): `{gsimpin}` + the `{bgnpcpin}` full-SoA pin landed,
  rng-drill unblocked, off-30-34 cluster analyzed; §21.2 first wall-clock refutation.
- **§21.6**: the wrap-up "softlock" = a driver ESC-spam artifact, not a port bug.

## 2026-06-21/22 — post-tutorial flow P1/P2 + live-haggle fixes (RE §11–§20)

- **P1** (`e42921a`, RE §11): the "port 3 rounds vs retail 5" misdiagnosis = 3 scripted + 2 LIVE
  practice rounds; the multi-round nav gap closed.
- **P2** (RE §12.1): the post-sale wrap-up dialogue iv1_7 ported; USER-CONFIRMED.
- **§17** (`3d8f6ce`): post-fade camera = the missing f406 cc08-entry consumption.  **§18.1**
  companion position (f404 gate); **§18.3** wrap-up camera leave-reset; **§18.4** companion height
  (ground_y) + the player contact-shadow frozen-floor fix.
- **§19** cs-walker pump gating + ghost-slot reset (`tools/cs_walker_drill.py`); the b5a4 base-price
  f406-branch fix (Walnut Bread).

## 2026-06-19/20 — cc08==4 HAGGLE RENDER arc USER-CONFIRMED 1:1 + the softlock chain (RE §8.4–§9.8)

- **Chips 3a–3e**: dialogue box + typewriter; Recette/Tear character art (the `grp:` per-stage
  parser); arrival anim/pos BIT-EXACT; the cinematic counter camera (§8.7.3); companion at-counter
  pose (§8.7.4).  Nameplate slot-1 (`feb2254`); the haggle UI §2–§4 (`12d668e`); the ADDSIGNED
  COLOROP brackets (rule: GREY 0x7f diffuse ⇒ ADDSIGNED, WHITE/coloured ⇒ MODULATE); prompt
  ellipsis (`df58859`).  USER-CONFIRMED "everything else looks 1:1".
- Note #1 sell-counter "!" emote; camera-hint overlap (b4e8 gate).
- **Softlock chain**: TUTO_PARSER_STRIDE 50→200 (`29e167a`, RE §9.8, quirk §22 — Ghidra rendered
  `imul 0xe740` as `*0x32`); the b534==0xc scripted close (`0c0331c`); **L1a** the live sell machine
  `FUN_004658ab` un-softlocked (`7dfc611`) — offer b574=3870 == retail.

## 2026-06-22 — L1c: per-kyaku dialogue buffer PORTED (the live haggle "..." placeholder → real lines)

The user-flagged "live first-customer haggle dialogue is `...` PLACEHOLDER" (the rounds-4/5 LIVE
practice-sale lines).  Root: `cs_pick_line` (`FUN_00460a1a`) drew the variant rng but stubbed
`s_b270="..."` — the per-kyaku dialogue tail of the engine record was unmodelled.  Ported (RE §13):

- **`customer_dialogue.{c,h}`** — the `kyaku_dialogue_t` slot grid (flat `s = variant + type*0x14`,
  30 types × 20 variants × 0x100-byte text; text/sprite/voice/count = the engine record's
  +0x6e70/0x51d8/0x5b38/0x6df8) + the pure `kyaku_dialogue_parse` (the fixed-width `msgNN:SS:Vvv:text`
  parse, the msg half of `FUN_00475270` all.c:74646-74707) + a per-record heap store.
- **`tables.c::load_kyaku_dialogue`** — after `load_kyaku_txt`, reads each customer's `kyaku/<name>.txt`
  via storage into its buffer.  On the user's real data: **18 scripts / 1229 lines**, no errors.
- **`customer_service.c`** — `cs_pick_line(rec,type,slot)` reads the real text/sprite (slot-0 = record 0
  Recette, slot-1 = customer b56c) and runs the factored `cs_split_line` `<C>` split; all 9 call sites
  pass `(rec,type,slot)` per the by-address comments.  RNG step unchanged (one draw when !f404) ⇒ the
  verified-1:1 LCG holds; only the now-used variant VALUE picks the real line.

Sanity: the reaction `cs_pick_line(0,9,0)` = recette msg09 = **"How much should I?..." / "Capitalism, ho!"**
(count 2, `rand%2`) — the iconic line the `...` hid.  +3 host tests (`kyaku_dialogue_parse_fields/_caps/
_store`), 3345 pass.  Retires PORT-DEBT(cs-kyaku-dialogue); adds cs-dlg-override (the DAT_073dddb8 buysell
variant table) + cs-voice (playback = audio).  **v3-verified + USER-CONFIRMED**: the live greeting renders
"Tear / I would like this, please." bit-identical to retail (was `...`).

**Follow-up (user note #2) — the `<I>`/`<Y>` dialogue TEXT MACROS** (RE §14).  The post-sale close line
"Yay! I sold `<I>` for `<Y>`!" (recette msg08) showed raw/mangled markers; retail substitutes item name + pix.
`font_draw_text_box` (FUN_00465db4) pass-1 macro expansion was stubbed AND leaked the trailing `>` — ported
to new `dialogue_macros.{c,h}` (`dlg_macro_expand`, the 6 tag buffers).  The live close branch sets `<I>`
(`cs_set_item_macro` = FUN_004607f3(b5a4) → `g_item.singular`) + `<Y>` (`snprintf("%dpix", s_price_ask)`),
per all.c:60616-60626.  +4 host tests (3349 pass).  **v3-verified + USER-CONFIRMED**: the close line renders
**"Steel Sword / for 3600pix"** BIT-IDENTICAL to retail.  Retires PORT-DEBT(box-text-macros); adds
cs-item-macro-kinds.

**Two more user notes (RE §15).**  (#3) the LIVE price-input prompt "How much should I?..." was missing — the
render gated (312,250) on `b51c!=0` (scripted); objdump (FUN_00466b7b 0x467629) shows the `b51c==0` live arm
draws the dialogue line `b270` (recette msg09) there, b5a8-coloured.  Fixed in `customer_service_render.c`
(retires PORT-DEBT(cs-haggle-prompt-live)).  (#4) the counter "!" emote lingered through cc08==4 idle — the
Z-entry clears `db000=0` (all.c:87696); `player_ctrl_cc08_sell_counter_enter` now does too.  3349 host pass.

**Two more (notes #5/#6, RE §16).**  The post-practice scripted conclusion "Expertly done. If you ever wish to
practice again, simply ask me<C>any time we are in the shop." (tuto1 id -4) showed "○○○"/ended early — the
master tick's b534==0x14 queue-advance stubbed it (`s_b270="..."`).  Ported the engine's negative-id scan
(all.c:60540-60549): find the g_tuto[fileidx] record with `id==(b528==2)-4`, load its text via
`cs_dialogue_line_setup` (the `<C>` page advance is the shared pre-dispatch check).  The long real line reveals
at retail's rate, so the 0x14 duration + close timing track retail (no early-exit).  Retires
PORT-DEBT(cs-queue-line).  3349 host pass.

## 2026-06-18 — customer-tutorial TRACE-REPLAY blocker ROOT-CAUSED + FIXED (segtrace timeout ate the walk)

The `house-customer-tutorial` port drive stayed `cc08==1` the whole window — the player frozen at
the post-load pose-init, never reaching the sell counter, so the `cc08==4` haggle entry never
fired. RE §8's first pass blamed the **cc08 / LOADING_END timing** ("the port emits LOADING_END at
raw load-complete, free-roam starts 156f later, the walk fires in the dead gap"). **Probing proved
that wrong** and found a TAS-replay (tooling) bug instead.

- **Probe:** a throwaway `_probe-cust-load` scenario (the Continue-load + walk+Z segment) with an
  early `{calltrace}` over the walk window, reading the always-on `0x452cde` (worker-spawn) /
  `0x4850ec` (cc08-set) / `0x48670f` (free-roam) VAs. Found: the port's Continue-load fires
  `LOADING_END`+`HOUSE_FREEROAM`@~f476 with `cc08=1` set ONE frame before (`pose_house_standing`
  runs in the primary-worker body) — so **LOADING_END IS the free-roam boundary**, no dialogue gap,
  no late cc08. Driving the walk segment ALONE (truncated trace), the player walks px −0.30→−1.50 to
  the counter and Z@rel156 flips **cc08→4**, 3/3 runs, load-stretch-immune (LOADING_END
  f476/f483/f491 all reach + enter).
- **Real cause — `input_segtrace.c` `{wait,timeout}`:** a `{wait}` CLOSES a segment; the walk
  segment `[rel0, rel66=walk, rel75=release, rel156=Z]` is terminated by
  `{wait LOADING_START, timeout 60}` (the d3e haggle-asset load the Z spawns). The replayer measured
  the timeout from segment ENTRY (`base_arm` = LOADING_END frame), firing at **rel60 — BEFORE the
  segment's own walk@rel66 / Z@rel156** — so it skipped the segment entirely. The walk never
  applied ⇒ frozen ⇒ no counter ⇒ no cc08==4. (The truncated probe worked only because it had no
  trailing `{wait}`; bumping the timeout 60→220 (> the rel156 span) also restored everything.) This
  is a refinement of `47cdd8c`'s cross-target optional-wait feature.
- **FIX (`input_segtrace.c`):** measure the optional-wait timeout from the segment's LAST entry
  (`base + entries[n-1].frame + wait_timeout`), not segment entry — a segment's recorded inputs must
  ALL apply before its terminating wait can time out (the timeout's intent is "hold the last input N
  frames waiting for the optional anchor"). Entries are ascending and `base==base_arm`, so this only
  ever DELAYS a timeout — a no-op when the last entry is at rel0, which is every OTHER committed
  timeout-wait (audited), so the blast radius is exactly this scenario. +1 host regression test
  (`test_segtrace_wait_timeout_after_last_entry`); 3331 host pass.
- **Validated end-to-end:** the extended probe with the committed **timeout 60** now reaches the d3e
  `LOADING_START` + **cc08==4**, 2/2 across load-stretch. The FULL committed `house-customer-tutorial`
  drive now fires the 2nd `LOADING_START/END` (the entry's d3e load — it stalled at 1 before) and the
  caprange call-trace shows **cc08 {1: 157, 4: 1034}, first cc08==4 @f630** (haggle active) where the
  pre-fix run was `{1: 2351}` (stuck). NEXT: verify the caprange haggle window vs the retail v3 cache
  + port Chip 3 (the BARGAIN!! UI). RE: `findings/customer-service-haggle-RE.md` §8.

## 2026-06-17 night — CUSTOMER-SERVICE / HAGGLE tutorial: harness unblock + haggle math (autonomous)

Started the SHOP CUSTOMER-SERVICE / price-haggle tutorial arc from the user recording
`rec-20260617-051426` (LOAD cad868 — a save with the display items already set up to fire the 2
display-tutorial dialogues that unlock haggling → walk to the sell counter → the customer-service
tutorial that alternates Tear's dialogue with the BARGAIN!! price-haggle UI → first real customer).

- **RE map** (`docs/findings/customer-service-haggle-RE.md`, `e35a3fe`): the entire cc08==4
  subsystem is unported. Entry (`FUN_0048670f` bVar3 + `FUN_0045edaa` → cc08=4), master tick
  `FUN_00462403`, sell machine `FUN_00463cfb`, the haggle math, the `FUN_0046602e`/`00466b7b`
  render, the iv1_7/iv1_8 cutscene gates. Scenario `house-customer-tutorial`; retail captured
  BIT-EXACT, the BARGAIN!! haggle (base price 1600, name-a-price, Tear's "...the base price serves
  as your default") confirmed as the target.
- **HARNESS UNBLOCK — `{wait,timeout}` (`47cdd8c`):** the recording is a retail playthrough and the
  port collapses retail's 3-load prologue into 1 (the accepted "port loads faster" phase pillar), so
  the port's segtrace stalled forever on a `{wait LOADING_START}` for a load it never reproduces →
  **0 frames captured**. Added an optional `{wait:NAME, timeout:N}`: skip the wait after N frames
  WITHOUT adopting a new base (next segment stays relative to the last RESOLVED anchor). Port-only —
  the Frida retail agent ignores it and follows every anchor, so the SAME trace drives both and the
  time-scale mismatch never bites. Port now drives the full caprange **1200/1200 BIT-EXACT** and the
  v3 join is occurrence-aware (**port HOUSE_FREEROAM#1 ≡ retail #3**, +2736 load-stretch; 440/1200
  paired, the rest honest load-seam gaps). +2 host tests. **This unblocks the whole arc.**
- **HAGGLE MATH (`d0ac215`)** — `src/customer_haggle.{c,h}`: budget (`FUN_0045ecc0`), accept/reject
  (`FUN_00460672`), offer up (`FUN_00460161`) + down (`FUN_004603cf`), **transcribed 1:1 from the
  unpacked DISASM** — the Ghidra decompile (and the first-pass RE) were WRONG: the floor `b580` and
  accept-ref `b588` are **rng-driven** (`(u+2.0)·P`, `(u·0.1+1.0)·P`), not deterministic, and the
  trend tilt is `(u·0.5+2.0)·P` etc. Every FP const decoded from `.rdata`; the LCG-draw ORDER is
  replicated (load-bearing for RNG parity). +9 host tests (3323 pass) on kyaku 13 (the tutorial
  customer: init 120/random 3/gull 20/rise 10/budget 3000-300000) + base 1600. Tested building block,
  **NOT yet wired** into the (unported) state machine; x87(port)≡x87(retail) by construction, a Frida
  pure-function-diff is the recommended follow-up. RE §4 corrected with the disasm-accurate formulas.
- **NEXT (next session, with the user for the visual 1:1 check):** entry (cc08 1→4, the f406
  forced-sale auto-arrival) → master tick + sell machine (wire the §4 math) → render (BARGAIN!! panel,
  verify via v3 content-match) → Tear's tutorial dialogue. Plan: `customer-service-haggle-RE.md` §7.

## 2026-06-17 PM — INTRO v3 parity VERIFIED 1:1 + `orv3_draws --material` + stale gaps closed

Closing pass over the opening prologue (user reframe: "not whether visuals are missing — whether
things are out of phase / not rendered faithfully / a logic approximation that should be closed").

- **`orv3_draws --material` (`164eae5`)** — the CLI printed only the per-draw alignment, whose
  ALIGNED/DIVERGENT is swamped by the benign HOUSE-3D batching (iv1_2 overlays the live 3D shop ⇒
  port 88 / retail 115 draws). Exposed the batching-robust MATERIAL verdict already in the module
  (per-texture triangle totals → ALIGNED / BATCHING / DIVERGENT) — every run now prints it first,
  `--material` prints only it. "Fix the known diff so we can see real divergences."
- **Render verified FAITHFUL both cutscenes:** iv1_1 ALIGNED (0 divergent, pure 2D); iv1_2 = 4 benign
  batched + 1 inert `b494` (0 px, HOUSE-wide) — NO retail-only effect texture ⇒ the **anger-marks /
  radial-lines "gap" is NOT a render gap**.
- **Phase verified IN-PHASE (visible window)** from the cached anchor streams: dialogue +121/+156 on
  both, `CONV_POSE_BLINK` 21/85/149 on both. The only diff is invisible (retail poses the chibis
  ~41f before HF#2 under the load overlay = the conv-pose producer PORT-DEBT, 0 px).
- **Stale ledger rows CLOSED:** text-fade-on-dismiss = already ported (user); **iv1_1→iv1_2 is a
  plain fade-to-black-and-back, NOT a shatter/melt grid** (user ground-truth — the RE'd `FUN_0045281c`
  shatter is some other transition; corrected in `opening-prologue.md`).
- ⇒ **opening prologue is verified 1:1** modulo the DEFERRED next-line book-arrow phase (user-OK).
- **NEXT (queued):** the SHOP CUSTOMER SELLING LOOP — user recorded the tutorial trace
  `runs/recordings/rec-20260617-051426.raw.jsonl` (counter tooltip → Z → customer-service tutorial,
  dialogue ↔ haggle → first real customer). Convert to a v3 scenario + RE the haggle subsystem.

## 2026-06-17 — v3 base-anchor auto-detect (`ddeb421`) + iv1_2 mis-armed-retail CORRECTION

`v3cache.preserve_live` resolved a window's BASE anchor by occurrence #1; new
`resolve_base_anchor()` auto-detects it as the most-recent firing ≤ `present_first`, so a side that
captures multiple firings of the base anchor re-bases correctly via `_window_occ`. Re-verified a
NO-OP across the whole v3 cache (fixes a latent `guild-ui-flow` window; every confirmed scenario
unchanged) + `test_base_anchor_auto_detect`. **KEEP — a valid tooling fix.**

**CORRECTION (same day): my first read of this — "iv1_2 join 0/299 → 152/299 honest, gap #4 fade
quantified" — was WRONG; recorded so it isn't re-trusted.** The two cached `intro-iv2-v3` sides are
DIFFERENT cutscenes: `orv3_shot intro-iv2-v3:port --frame 0` = the SHOP (iv1_2, 3D HOUSE, gold HUD)
but `…:retail --frame 299` = the BEDROOM (iv1_1, 2D bg — Tear, bed, mushroom). The trace waits for
the 2nd HOUSE_FREEROAM (HF#2 = iv1_2); the PORT captures HF#2 (shop ✓) but the **retail v3 arm is
occurrence-BLIND** (`house_capture` "arms the first time the anchor fires") so retail armed HF#1 =
iv1_1 (bedroom ✗). The "152 pairs" were coincidental CONV_POSE_BLINK matches across different
cutscenes; the "240f fade" was iv1_1's load-transition (3 quads `417a`/`748c`/`5d80` over ~240f,
0/300 RT frames), not iv1_2's — and `fadeinb`/`fadeoutb` ARE genuine compiler no-ops (decompile
confirms; the port mirrors them).

**RESOLVED same day (`869375f` + re-drive):** made the retail v3 arm OCCURRENCE-AWARE — the agent's
`v3ArmOnAnchor` counts firings + arms at the occ-th; `house_capture --arm-occ` defaults to AUTO =
`wait_occ` = the count of `{wait:<anchor>}` ops in the trace (⇒ 2 for iv1_2, 1 for every unique-anchor
scenario = no-op; not in the cache key). Re-drove retail (`--force-retail --max-frames 40000`): retail
now arms at HF#2 present 5329 = the SHOP (Recette+Tear, "Sorry Tear, I kept you waiting"), matching the
port. **Join 279/299** (genuine iv1_2-vs-iv1_2), **paired frames PIXEL-1:1** (settled 0.2–0.5% mean 0;
opening ~1.1% converging = load-origin phase, port ~1f ahead). port self-verify 299/299 bit-exact. ⇒
**the iv1_2 opening IS 1:1** — the "240f fade / gap #4" was a phantom of the mis-armed iv1_1 capture
(`fadeinb` is a genuine no-op). The occurrence-aware arm generalizes to any cutscene-sequence window.
RE: `findings/opening-prologue.md`. Lesson (`feedback_verify_1to1_before_done`): eyeball both cached
sides are the same scene before trusting a join verdict.

## 2026-06-17 — shop-door "GO!" tooltip (the free-roam emote bubble, FUN_0040a765) — pixel-1:1

When the player stands at the shop door, retail shows a **"GO!" speech bubble** over their head
(the user-flagged "tooltip at the door"). It is the free-roam interaction-affordance **emote
bubble** — the unported inline block of `FUN_0040a765` (decomp L6900-6932), driven by the
`db000`/`db004` door-zone gauge in `house_update` (L87591-87596). Both the driver and the draw
were gaps; the trigger predicate (`bVar17`) was already RE'd + ported (the T1 door exit).

- **driver** (`scene1_player_ctrl.c`): the stub `player_ctrl_cc08_proximity_detect` now runs the
  bVar17 door-zone ramp — at the door (`player_ctrl_at_shop_door`) set `db004=7` + ramp `db000`
  up to 10; off it ramp it down to 0. Factored the ramp into the pure host-testable
  `player_ctrl_emote_ramp_step` (mirrors `player_ctrl_pulse_counters`) + `player_ctrl_emote_level/type`
  accessors. The bVar3 NPC-approach prompt path (db004 0/1) stays a faithful no-op (no live customers).
- **render** (`scene1_hud.c`): `scene1_hud_emote_bubble` draws the `db004` cell of `hpmp_base.tga`
  (`((db004%4)·48+320, (db004/4)·48)` → cell (464,48) = the baked GO!-bubble sprite) at the
  projected player head (`b778·0.1+4.0+py`, via the new `scene1_camera_class_off_z` accessor),
  scaled in by the `db000` sin slide-in (overshoot then settle to 32×32), under COLOROP=MODULATE.
  Called after `scene1_merchant_hud_render` (engine order `FUN_00409925` → bubble).
- **verified** on the new **`house-door`** scenario (Continue the guild save → free-roam → walk to
  the door + HOLD, no Z; caprange LOADING_END+140..260, pinned) vs the retail v3 cache (join
  120/120 ALIGNED, +2543 stretch): the GO! bubble region is **pixel-1:1 (meanabs 0.084/px, 1 px>40
  at the settled offset 259)** and the ramp-in matches retail. **USER-CONFIRMED 1:1.** +4 host
  tests (3312). `2fb6085`. Closes the `town-map-RE` door-tooltip follow-up. `PORT-DEBT(door-proximity)`
  (the FUN_005031e4 sqrt<1.8 radius) unchanged — the X>2.895 subset reproduces the deliberate approach.

## 2026-06-16 — title RECORDS / high-score screen (code 8, submenu_state 4) — pixel-bit-exact

The title menu's **"Survival Score"** row (code 8 — the port author's `HIDDEN_CHAR` name is a
misnomer, same as `RANKING` was for the encyclopedia; the in-game menu tile literally reads
"SURVIVAL SCORE") opens a display-only personal-best Records panel (`FUN_0049c439`), sliding in
like the settings/encyclopedia submenus and closing on A/B.

- **scene_title.c**: `scene_title_records_render` (= `FUN_0049c439`) — a dungeonbord board (the
  same sheet the settings panel uses, src 0,0,320,360) + 4 centered label/value rows under the
  ADDSIGNED→MODULATE2X COLOROP dance (grey-0x7f, scale 0.8): Record End-game Score `%d pt` / Record
  End-game Money `%d pix` / Survival Hell Record `Day %d` / Normal Survival Record `Day %d` (zero ⇒
  `--`). Plus the code-8 dispatch (`submenu_state=4`, cursor 0, NO hand cursor) + the state-4 close
  (A|B → SE 0x143, fold out) in `scene_title_sim`, and the state-4 render arm + item_win/fuki code-8
  header chrome in `scene_title_render`.
- **The record values are persistent SAVE-HEADER fields**, not runtime globals: `FUN_004905a8`
  writes the whole arena from `&DAT_056e5770`, so `DAT_056e60f0/f4/f8/fc` are at arena offsets
  0x980/0x984/0x988/0x98c (inside the 0xb10 header) — they round-trip through save.dat and are
  already in the port's `g_arena` at the title; the render reads them straight from
  `save_arena_base()` (no separate loader, no PORT-DEBT on the populated path).
- **TITLE_RECORDS_READY** anchor (port `anchor_trace.c`/`scene_title.c` + frida agent; scene 0 /
  submenu_state 4 / cursor_anim 10 — no async load ⇒ +0-stretch v3 join).
- **Verification** (`tests/scenarios/title-records`, crafted save: `hidden_char` + four distinct
  record values `123456 pt / 654321 pix / Day 88 / Day 33`): vs the retail v3 cache, the records
  screen is **PIXEL-BIT-EXACT — 0/786432 px differ** across the window (join 119/119 @ +0, 0
  draw-divergent, both sides self-verify bit-exact). +2 host tests (3303). RE:
  `findings/title-records-RE.md`. PORT-DEBT: the end-of-game record producers
  (`FUN_0049d8a4`/`FUN_0049db8a`) stay unported (game-completion arc).

## 2026-06-16 — title all-banks ENCYCLOPEDIA (図鑑, submenu_state 3) — bit-exact (empty + populated)

The title menu's code-7 row (the port author's "RANKING" is a misnomer — the dispatch
`FUN_0049a59e` L101130 runs `FUN_0049f012(1)`, the all-banks encyclopedia setup, and the
render is `FUN_0049f8b8`) opens the 図鑑. Mostly integration over the pause encyclopedia
(verified bit-exact 2026-06-15), with the all-banks scan (param 1) aggregating discoveries
across every save bank instead of the current one.

- **scene_title.c**: code-7 dispatch (`encyclopedia_setup(1)` + submenu_state 3 + slide-in +
  show cursor); per-frame `encyclopedia_update` at cursor_anim==10 (B-close layers the
  menu-back 0x143 over the nav's own 0x13d, folds out, hides the cursor); render dispatch
  `encyclopedia_render(640-cursor_anim*64, 0, board)` gated on cursor_anim>0 && state==3.
- **TITLE_ENCYCLOPEDIA_READY anchor** (anchor_trace + frida; scene 0 / submenu_state 3 /
  cursor_anim 10 — no async load ⇒ +0-stretch v3 join). +1 host test.
- **Two real gaps the trace loop caught (the @fresh case hid both):**
  (1) `encyclopedia_render` bound `g_scene_pause_pause` (pause.tga) for the completion board +
  slot frames — that instance is FREED at the title, so the draws vanished. Parameterized the
  board per-scene (title passes `SCENE_TITLE_TEX_PAUSE`); same split the picker plaque uses.
  (2) The discovery scan reads the WORKING arena (g_work) via `enc_disc_rec`, but at the title
  the port only loads save.dat into g_arena (the picker's source) — g_work is empty, so a save
  with discoveries rendered an EMPTY grid (33 draws vs retail's 464). Fixed with
  `save_work_sync_from_save()` before the all-banks setup (the engine keeps its single
  DAT_044e3798 populated at the title).
- **Verified vs the retail v3 cache, two scenarios:** `title-encyclopedia` (@fresh empty grid)
  and `title-encyclopedia-max` (maxed save, full populated Swords grid + 100% completion +
  description) — **both PIXEL-BIT-EXACT** (0/786432 px differ at every offset, material verdict
  0-divergent, join 119/119 @ +0). The populated frame's per-draw `--list` shows 287 sub-LSB
  geometry-hash pairs (icon carousel floats differ sub-pixel, rasterize identically) — accepted,
  same class as the picker's 54 sub-LSB px. PORT-DEBT: the code-8 HIDDEN submenu (state 4).

## 2026-06-15 — title LOAD-confirm flow verified 1:1 (A on picker → fade → load)

The picker A-confirm was already fully wired (`scene_title.c`: `title_continue_picker_step` →
`save_work_load_slot` + `continue_mode`/`fade_counter`; the fade ramp → `fade_phase1_start` →
`scene_post_fade_init` → house), so this was a flow VERIFICATION — no code change. Converted
`title-load-confirm` to a v3 window at `TITLE_PICKER_READY` over the 2nd-A confirm → card-pulse →
fade-to-black. Verified vs the retail v3 cache (`title-load-confirm-f00eae67`, +0 stretch): settled
54px / **confirm+card-pulse gt8 0.0000% maxdiff 1** / mid-fade 60px / near-black 1px — all meanabs
0.00 (the accepted breathe/seam envelope); draw program 193=193 (the same 3 off-screen wing-portrait
residuals as the picker). The selected card "lights up" (the `fade_counter` `phase` param the picker
unification carries) + the fade-out match retail frame-for-frame. The window crosses the title→INGAME
transition (NEW_GAME/LOADING_END gaps after the fade; the HOUSE arrival is verified separately,
bit-clean). ⇒ all title main-menu render gaps + the load flow are closed.

## 2026-06-15 — title Options/settings panel adopted onto `settings_panel_render` (bit-identical)

The title's `scene_title_settings_render_panel` was a 2nd copy of FUN_0049c050 (the config
panel). The pause Options arc had produced the verified shared `settings_panel_render`; the
engine shares the ONE FUN_0049c050 between the title (6 rows, adds "Clear Save Data") and the
pause (5 rows), keyed by `g_scene_state`. Made the title a thin wrapper calling the shared
render — same single-render structure the engine has.

- **`scene_title_settings_render_panel`** now calls `settings_panel_render(dev,
  &g_tex[SCENE_TITLE_TEX_DUNGEON], NULL, ox, oy, cursor_row, saving_flag)`: the title passes its
  own dungeonbord instance; savewindow is NULL (the title doesn't load savewindow.tga ⇒ the
  dirty-exit "Saving" overlay stays PORT-DEBT, inert unless a setting is changed + exited). This
  restores fidelity the old copy dropped: the engine's ADDSIGNED→MODULATE2X back-to-back COLOROP
  writes and the saving overlay.
- **`TITLE_SETTINGS_READY` anchor** (`anchor_trace.{c,h}`, `main.c`, the frida agent) — rising edge
  of (scene 0 / submenu_state 2 / cursor_anim 10). Like the picker, no async load ⇒ a clean +0
  join. The v2-era `title-options` scenario was converted to a v3 `{caprange}` window. +1 host
  test (3300).
- **Verified vs the retail v3 cache** (`title-options-522438b9`, 39/39 paired @ +0 stretch): the
  draw program is **73=73 ALIGNED (0 divergent)** and pixels are **0/786432 differ — BIT-IDENTICAL**
  across all settled pairs (the 6-row title variant incl. "Clear Save Data"). The settings panel is
  fully static, so unlike the picker there's no breathe seam — perfectly bit-identical. (The port
  TAS ran 40 frames vs retail's 120 — a post-wait TAS-length quirk on `@fresh`; the static panel is
  fully verified on the 39 aligned frames.)

## 2026-06-15 — title Continue/load PICKER unified onto `save_picker_render` (bit-exact)

The title-screen Continue/LOAD-GAME slot picker (`FUN_0049c644` → `FUN_0049b556`) had its
OWN copy of the card-grid render (`scene_title_continue_render_panel`), a 2nd FUN_0049b556
port that diverged from the verified shared `save_picker_render` (the pause Save submenu's).
Unified the title onto the shared render — the single-render structure the engine has
(both `FUN_0049c644` and `FUN_004812e4` call the ONE `FUN_0049b556`), and the lesson of
`save-picker-shared-globals`.

- **`save_picker_render` gained a `plaque` param** (`save_picker.{c,h}`): the "Merchant Level"
  banner texture (pause.tga) differs by scene — the pause submenu passes `g_scene_pause_pause`,
  the title passes its own `SCENE_TITLE_TEX_PAUSE` (because `g_scene_pause_pause` is unloaded at
  the title; the pause scene owns + frees it). Everything else `save_picker_render` binds is a
  sysasset (item_win, font) valid in both scenes. The pause call site (`scene_pause.c`) passes
  `&g_scene_pause_pause` — behavior-identical.
- **`scene_title_continue_render_panel`** is now a thin wrapper that copies the title picker
  state (`g_title_continue_picker`) into the shared FUN_0049b556 globals
  (perm/count/restricted/wing-anim) and calls `save_picker_render` (−218 lines of duplicate).
  This **closes `PORT-DEBT(modetag)`** (the bottom-right Endless/New Game+/Survival label the
  old copy never drew), the off-screen wing rows the old copy skipped (an extra `slot < 0`
  guard the engine lacks), and the >999h TIME clamp.
- **`TITLE_PICKER_READY` anchor** (`anchor_trace.{c,h}`, `main.c`, the frida agent) — rising
  edge of (scene 0 / submenu_state 1 / cursor_anim 10). The title picker has NO async asset
  load (unlike the pause submenus), so it's a clean v3 join anchor (+0 load stretch). +1 host
  test (3299). The v2-era `title-load-picker` scenario was converted to a v3 `{caprange}` window.
- **Verified vs the retail v3 cache** (`title-load-picker-60516ab3`, join 119/119 @ +0 stretch):
  the unification took the draw program **162 → 193 draws = retail's 193** (matched 160 → 190;
  the wings now draw like retail) AND tightened pixels **946 → 54 differing px**, gt8
  **0.0000% BIT-EXACT** (maxdiff 2 — the 54 sub-LSB px are the rotated-portrait rasterization +
  the 1-step breathe seam, the same accepted class as the pause submenus). The residual 3
  "replace" draws are the off-screen wing-page portraits' OOB-perm garbage (invisible; UB on
  both sides). **PORT-DEBT(title-picker-overwrite):** the code-4/6 new-game "choose a file"
  overwrite-dim + per-slot `g_save_picker_avail` stay unported (inert for a normal Continue).

## 2026-06-14 — pause menu M4c: the SAVE-picker A-confirm + COMMIT (pixel-1:1)

Ported the in-game pause Save submenu's A-commit — `FUN_0047f5bc`'s A-branch +
the `FUN_004905a8` disk write + the `FUN_00434def` "Overwriting file." dialog +
the `FUN_004812e4` save-progress bar.

- **`save_io_commit_slot`** (`save_io.c`) = `FUN_004905a8(slot)`: copy the live working
  bank (`save_work`, active slot) → save bank `slot`, re-stamp its checksum
  (`save_bank_stamp_checksum`), write save.dat/_save.dat (the merge the merge-less
  `save_io_write_arena` = `FUN_004905a8(-1)` had omitted). The `{savefile}` sandbox keeps
  replays off the real save.
- **`pause_save_submenu_update`** (`scene_pause.c`): the A-branch — SE 0x143, then an EMPTY
  slot commits at once (`g_pause_save_phase`=1) while an OCCUPIED slot pops the choice box
  (`choice_box_open("Overwriting file. Are you sure?", 1, 0)`, the exact single-row string)
  whose Yes/No drives commit/cancel (`choice_box_poll`); the phase>=1 commit sequence
  (card-field snapshot + the streamed save jingle + `save_io_commit_slot` + the 1→0x3c
  counter); the dungeon "Saving here…" warning is inert in the house (PORT-DEBT).
- **The save-progress bar** (`pause_save_picker_render`): two item_win.tga quads over the
  selected card under COLOROP=ADDSIGNED — an empty-bar frame + a fill quad growing with
  c89c/30, grey pulsing with the −128·sin the card uses, alpha fading past c89c>0x34
  (geometry + the Ghidra-dropped sin amplitude from objdump 0x481358-0x481408).

Verified vs the retail v3 cache on the new **`house-pause-save-commit`** trace
(ESC→3×down→Z→{wait SAVE_PICKER_READY}→A on slot 0→Yes→commit; `orv3_shot` per-frame):
**port#N vs retail#N+1 (the 1-frame async-pause seam) ~0.12% / meanabs ≤0.10 across the whole
window** — the dialog (0.07%), the commit ramp, the progress bar, and the post-commit card
all within M4's accepted breathe/seam phase envelope; only the dialog-close transition frame
is elevated (1.86%, the box text 1 frame off at the seam). +6 host tests (3270 pass).
PORT-DEBT: `save-commit-dungeon` (the dungeon warning + town-state swap),
`save-card-type-modes` (the mode-6/0xb card-snapshot type — pixel-invisible).

## 2026-06-13 — pause menu M2c: calendar / merchant-rank XP bar / numbers (pixel-bit-exact)

Ported FUN_004820ba's [4-9] resting-menu block in `scene_pause.c` — the calendar board +
today/period-end day markers, the merchant-rank XP progress bar, and the gold/quota/level
number glyphs — plus the three value helpers `pause_day_index` (FUN_00482033),
`pause_period_end` (FUN_00482059, with the +0x2c3e8 period-end cache write) and
`pause_weekly_quota` (FUN_0048d997). All read the working save bank directly
(`save_work_dwords_at(save_work_active_slot())`, dword-indexed), mirroring the engine's
`puVar3 = &DAT_044e3798 + slot*0x2dfc8` arena reads.

- **RE correction (load-bearing):** the plan had framed the blocker as "[4] needs the
  CURRENT-DAY `_DAT_0438b91c`, stubbed to 0". That was a mislabel — `_DAT_0438b91c` is the
  **animated merchant-rank XP** (the value the bottom-left HUD eases toward the rank target;
  `scene1_merchant_hud.c` had it labelled right). The [5]/[6] `+0x2c3f8`/`+0x2c3fc` fields are
  the XP **level-start / next-level thresholds** (save_bank.h's "DAY_INDEX"/"RANK_THRESHOLD"
  names), NOT a calendar period; the real calendar day is CARD_DAY (+0x2c3ec). The stubbed
  `g_dat_0438b91c` (`stage_post_load.c:562`) is **dead** (no consumer) and was left untouched;
  at rest (no XP animating in the house) the XP-current bank field (+0x2c3f4) equals
  `_DAT_0438b91c`, so the direct bank read is bit-identical to retail.
- **Verified (re-drove the port over `house-pause` HOUSE_FREEROAM+120:240 vs the retail v3
  cache):** resting-menu **draw program ALIGNED — 10/10 draws matched by content hash, 0
  divergent** ([4] panel ×12prim=6q / [5] today ×2=1q / [6] period-end ×6=3q / [7] quota
  ×12=6q / [8] gold ×8=4q / [9] level ×2=1q; every tex/colorop/tri/geometry-hash = retail),
  and **history-replay pixels BIT-EXACT (meanabs=0.000, gt8=0.000%) across every resting pair**
  (offsets 100/126/152/177). The pause menu is fully static at rest ⇒ zero phase residue — the
  missing-board entanglement that swamped M2b's pixel diff is resolved. Port self-verify still
  **240/240 BIT-EXACT** (M3 backdrop determinism preserved). Host suite 3248 pass / 0 fail.
- **PORT-DEBT(pause-xp-anim):** the XP-display animator (FUN_00406xxx, shared with the
  merchant HUD's own never-called `set_xp`) is unported — only matters mid-rank-up.
- **PENDING:** the user's visual 1:1 confirm (feed "M2c pause calendar+numbers — port|retail|diff").
- RE/empirics: `plans/pause-menu.md` M2c.

## 2026-06-13 — scene-guild: drop the conversation bg+keeper double-draw (v3 render-program drill, `2a2d84d`)

First parity fix found purely through the **v3 draw-program panel** — a render-PROGRAM
divergence on pixel-bit-exact frames (invisible to v2's pixel diff). Drilling
`guild-ui-flow` with `orv3_draws.py` showed the port drawing bg_guild (tex `2780`)
**twice** on every one of the **1076** guild conversation frames (Talk topics /
first-visit cutscene): once from `scene_guild_render`'s slot0, once from the
conversation renderer's own `draw_background` (FUN_0046c9a2) — plus a fully-overdrawn
guildmaster keeper. Retail draws it **once** (full-window scan `{0:114, 1:2486}`, never
2): its render root `FUN_004547ab` **skips the whole mode-6 scene block** when a
full-screen-bg conversation covers the screen — gate `DAT_0438b1c8 && FUN_0046c869()`,
where `FUN_0046c869` returns the active script's n_bg — so a full conversation renders
ONLY itself (its own bg + the guildmaster as a STANDEE + the box).

- **Fix:** gate `scene_guild_render`'s bg + keeper on the port's *existing*
  `scene1_intro_dialogue_covers_screen()` — the exact `FUN_0046c869` gate, already used
  for the INGAME HUD at `main.c:2925` — the same way the guild menu is gated. The iv1_9
  try-leave reminder (n_bg=0, overlay) keeps it false, so the bg + keeper stay the menu
  backdrop (the `aa773d0` fix is preserved). +19/−2 in one file.
- **Verified (cached guild-ui-flow v3 window):** bg draws/frame `{0:106,1:567,2:1076}` →
  `{0:106,1:1722}` (zero double-draws); the post-fix port is **pixel-bit-identical to the
  pre-fix port at all 1749 shared identities** (a per-frame fnv64 pixel-hash join keyed by
  `(anchor,offset)` — the fix changed ZERO pixels); a conversation frame's cross-side draw
  diff goes port-only **2 → 0** (only the retail-only `9fd8` clear-to-black base remains).
- **Deferred lead (invisible):** retail lays a full-screen opaque-black quad (tex `9fd8`,
  128×128 stretched, `0xff000000`, ZENABLE off) as the conversation's FIRST draw on all
  1076 frames — covered by the opaque bg (0 px), source inside `FUN_0046c9a2` (likely the
  `polybg`/`FUN_00455191` layer block). Engine-quirk §122; port deferred until a scene
  exercises it visibly. The HOUSE `ea99` (98-vs-125 draws, 3D batching) is a separate,
  larger divergence. RE: `findings/merchant-guild-RE.md` "Render-program drill".
- **Same class FIXED in the INGAME path (`7d119af`):** retail's `FUN_004547ab` skips the
  WHOLE scene block (3D scene `FUN_0045bbf9` + HUD + overlay + the cc04 tail) under a covering
  cutscene; the port only gated the HUD (correct when the 3D walkers were stubs, stale now the
  3D render is live), drawing the 3D scene + overlay under the iv1_1 opening bg. Gated
  `scene1_render_camera_setup` + `scene1_render_overlay` + `display_menu_render` on the same
  `covers_screen()` the HUD uses (one shared `covers` local; dialogue draw stays unconditional).
  Pixel-safe by construction (covers_screen ⟺ a full-screen bg ⟹ the scene is covered) and
  verified **byte-identical** via `scenario-test intro-dialogue-lines --target openrecet` (16
  pass / 30 fail SAME magnitudes pre/post — iv1_1 caps 0-15 pass both, the 16-45 fails are the
  pre-existing deferred iv1_2 freeroam-anim gaps where covers=0 ⇒ no-op). Formalises retail's
  scene-block gate + prevents the over-draw for any INGAME cutscene over a loaded 3D scene; the
  *measured* over-draw removal is the guild's (the traced covering cutscenes here render no 3D
  scene, so pixel-identical).

## 2026-06-13 — studio-v3: parse-once container handoff + material-aggregate bake (cached re-window loop perf)

The biggest perf remainder in the v3 **cached re-window loop**: each phase (`orv3_sync`,
`orv3_view`, and the view's per-column draws bake) re-parsed the same 91 MB (retail) + 58 MB
(port) containers in pure Python, and the material-diff bake built full `Draw` lists for every
column — hashing geometry the verdict never reads. Both fixed, **behavior-preserving** (the
2600-column guild pair's `view.json` and `pairs.json` are **byte-identical** before/after).

- **Parse-once container handoff.** New `v3cache.LoadedSide` + `load_side` parse a cache entry's
  meta + container + per-frame identity index ONCE; `as_side` accepts a LoadedSide (pass-through,
  idempotent) OR a Path (parse now). `orv3_window` now `load_side`s each window side once and
  threads the SAME object through `sync_entries` → `write_view_json` (which itself re-calls sync) —
  so the containers parse once per loop, not ~3× per side per phase. `identities` / `sync_entries`
  / `_side_index` / `write_view_json` all take a LoadedSide-or-Path.
- **Material aggregate bake.** `orv3_draws.material_agg` walks a kept frame straight to
  `{tex_hash: [triangles, draws]}` — the only thing `material_diff` reads — with **no Draw
  objects, no geo_hash** (a pure-Python fnv1a byte-loop over every UP draw's inline vertices, the
  actual hot cost since the 2D UI is all `DrawPrimitiveUP`), and **no rs/tss copies**. `material_diff`
  and the fast bake now share one `_material_report(pt, rt)`, so reports are byte-identical;
  `enumerate_draws` (full per-draw view/pick/CLI layer) is unchanged.
- **Numbers (2600-col guild pair):** per-column bake **6.71 s → 0.36 s (~18×)**; sync+view
  **compute 8.98 s → 1.40 s (~6.4×)** (sync 0.74→0.07, view 8.24→0.62, + one 0.71 s parse);
  end-to-end loop 10 s → 7 s (the rest is fixed nix-devshell + python + numpy/PIL import startup).
  The plan's "~5 min" was a stale pre-ResHash figure — the loop was already ~10 s.
- **Guards:** `test_material_agg` (fast bake == `enumerate_draws`+`material_diff` over all frame
  pairs, including UP draws + distinct-content textures, exercising ALIGNED + DIVERGENT) and
  `test_load_side` (parse-once meta/container/index; `as_side` idempotent). The slice path benefits
  too (the sync+view re-parse is gone + the 18× bake); its residual cost is the replay verify.

A baked-draws cache was the sanctioned alternative lever — unneeded now the bake is 0.36 s.
The sibling drive-time follow-up (skip the v2 PNG/montage bake on a v3 retail drive) already
landed in `4f7cfed`, so the only remaining v3 perf item is the lazy viewer metric precompute
(~15 s at open). Plan: `plans/trace-studio-v3.md` P3 follow-ups.

## 2026-06-13 — studio-v3: viewer NOTES + crop regions → v2 RETIRED as the working tool

The native viewer gains the v2 `edits.jsonl` notes loop — **the last v2-parity gap**, so v2 is
now retired as the parity-loop tool (user call: "retire all v2 studio stuff … archived unless we
hit blockers with v3"). In the viewer: **note mode (m)** → drag a crop box on any panel (or **note
frame**) → type a note to flag a divergence for Claude; existing notes overlay as green boxes
pinned to their column by **identity label** (stable across re-windows) + a seek/del list.

- **Persistence dodges the UNC-write limit.** The viewer is a Windows process and can't
  fopen-write a `\\wsl.localhost` UNC path, so notes go to a WINDOWS-LOCAL json under
  `%LOCALAPPDATA%\openrecet\v3\notes\<scenario>.json` (one per scenario, keyed inside by the stable
  identity label). `orv3_view` writes its Windows form into view.json (`notes_path`, pre-creating
  the dir); `v3cache.notes_file` is the shared resolver.
- **`orv3_notes.py` (NEW)** reads the flags back on WSL: `list` prints them; `--render [--id N]
  [--view V] [--feed]` replays the flagged frame port|retail|diff via `replay.exe --upto`, crops to
  the (padded) box, outlines the exact region, composes a PNG, and optionally pushes it to the
  llm-feed — so Claude SEES what was flagged. Verified end-to-end on a HOUSE window.
- **Cursor/font fix (`f20b5ea`).** The interactive viewer created its d3d9 backbuffer at the
  1400x920 WINDOW size while ImGui reads the mouse + lays out in CLIENT-area coords (~40px shorter);
  Windows Present-scaled the taller buffer into the client area, a non-integer 920→881 squish that
  caused BOTH a low cursor (content rendered above the mouse) and a non-pixel-perfect / top-clipped
  font. Fixed by sizing the device to `GetClientRect` (1:1) + a `WM_SIZE` reset on resize.
- **Verified:** live drag→save→read round-trip user-confirmed (a real note round-tripped); cursor +
  font fix user-confirmed. **Parity loop is now** `orv3_window <scen> --window OFF:COUNT --launch` →
  drag notes → `orv3_notes.py <scen> --render`. Commits `db28c34` (notes) + `f20b5ea` (fix).
  **Next arc: the perf follow-ups** (skip the v2 bake on v3 retail drives; parse-once container
  handoff; lazy viewer metric precompute), then the formal P4 parity check as v2's send-off.

## 2026-06-12 — studio-v3: HOUSE-drive retail full-extent capture (P1 COMPLETE, 48/48 bit-exact)

The Trace Studio v3 P1 (capture-at-scale) is now fully proven on both sides through a real
post-load 3D scene. `house_capture.py` drives the REAL retail exe through the fa7c82 save-load
to the HOUSE (save-virtualized + input-segtrace) and arms the capture proxy at
`HOUSE_FREEROAM+120` for a 48-frame free-roam present-window. **HOUSE_FREEROAM fired at retail
present 13912** (the ~13k-frame load-stretch E3 predicted, vs the port's 824); the agent armed
`[14032,14080)` in-process 120 frames ahead; **all 48 frames replay 0 px / 0 byte — 48/48
BIT-EXACT**, 29.3 MB container. This combined two already-proven paths (the port's 3D multi-frame
window `da5f601` + R2's retail single-frame `fe3722a`) into one. Three gated pieces (`b034849`):

- **`frida_capture` `v3_arm` plumbing** — a `v3_arm` field on `CaptureConfig` + `run_capture`,
  emitted into `init_cfg` (implies `anchor_trace`). None default ⇒ a silent no-op for every v2
  capture. Lets the FULL scenario machinery (save-virt, segtrace, turbo, resolution/RNG pins)
  carry the anchor-relative proxy arm the agent already supported (`config.v3_arm` →
  `OrV3ArmWindowAt(anchor_frame+offset, count)` on its anchor).
- **proxy `armwait=1` cfg key** — through the long pre-anchor load `g_capframe` is unset, so the
  GetBackBuffer MULTI keep-trigger would mis-keep a stray readback as a bogus load frame. `armwait`
  suppresses that trigger entirely ⇒ the proxy keeps NOTHING until the in-process arm sets the
  present-window. Port MULTI path unaffected (gated on `!armwait`; `port_capture` re-ran 48/48
  bit-exact). The segtrace's own `{caprange}` v2 readbacks become harmless non-keeping reads.
- **`house_capture.py` driver** — load the segtrace scenario → resolve `{savefile}` → stage proxy
  + armwait → `run_capture(... v3_arm ...)` → pull from `%LOCALAPPDATA%` + replay every frame +
  assert bit-exact. Reuses the `retail_capture` helpers. HOUSE_FREEROAM is the robust anchor: it
  fires ONCE, on the same frame as the final LOADING_END, so `HOUSE_FREEROAM+120` == the port's
  `LOADING_END+120..168` window.

Next → P2: the content-addressed retail slice cache (capture retail once, slice sub-windows with
zero re-drives) + window-aware early-exit (kill the post-window over-run the house drive currently
pays) → sync-by-identity (the E3 `(anchor,offset)` join as the real alignment authority). Plan:
`docs/plans/trace-studio-v3.md` (P2 section).

## 2026-06-11 — trace-studio: per-panel video seek (fix the diff diffing a different frame)

The studio's three video panels (port|retail|diff) were seeked to the SAME ordinal
`(k+0.5)/fps` (`VideoStage.mjs`), but across a kept-count LOAD seam the three sides have
**different label↔ordinal maps** (different holes + ranges: e.g. the merchants-guild buy
session is port 2562 / retail 2404 / diff 2307 frames). So the same scrub position landed
on a *different label per panel* — the user's symptom: "port vs retail show a 1:1 frame but
the diff is diffing against a completely different frame." (Measured on the buy session at
ordinal 2053: port→label 2400, retail→2400, **diff→2497**.)

The diff *data* was already correct (label-keyed; the cutscene is gt8=0). The bug was purely
the video **seeking**. Fix: each panel now seeks to ITS OWN frame for the cursor's label.
- `align.mjs` `videoFrameOfLabel(labels, target)` — pure binary search: a side's video frame
  index for a target label (holes hold the previous frame). Tested (`align.test.mjs`).
- `manifest.frame_labels = {port, retail, diff}` — each side's ascending captured-label list
  (`capture.py`). The scrub ordinal still indexes the port (n_frames / state stay port-keyed);
  `labelOf(k) = port_labels[k]` (no longer `frames[0]+k`, which a port hole made wrong).
- `model.mjs` `videoTimeFor(panel, k)` + `VideoStage.mjs` seeks each panel by it. Falls back
  to the shared ordinal for pre-`frame_labels` sessions (no behavior change for the common
  seam-free case — dense labels make `videoFrameOfLabel` the identity).

Now all three panels show the same moment at every cursor (verified: buy session ordinal
2053 → all three label 2400). The diff-magnitude ribbon (`diffAt`) is also label-true now.
Existing sessions get the alignment on next recapture (or a manifest `frame_labels` patch).
49 JS tests + the trace_studio python suite pass.

## 2026-06-11 — guild BUY FLOW step 2 (the milestone): Buy opens the populated item window

Pressing Z on **Buy** in the merchant's-guild menu now slides the main panel out and slides
the shared item window IN, populated with the guild's buy list and rendered 1:1. This is the
"milestone" step of the buy-flow plan (`merchant-guild-RE.md` "BUY FLOW") — the A-dispatch +
slide-in + population + render had to land **together** (the documented coupling: shipping the
dispatch alone blanks the menu).

What landed:
- **`scene_guild_sim`** — the mode-1 A-press dispatch (Buy/Sell → `c24=1,c20=1`, reset item
  cursor/scroll, SE 0x143) + the slide-in ramp (`c20/c24` count to 0x19; at 0xf arm+populate
  `display_menu_open(7=buy/5=sell,1)` + price-mult 0.7/0.3; at 0x19 → mode 0 = item list).
- **`display_menu_open` mode-7 branch** — port of `FUN_0049196f`, the guild buy-stock scan:
  walks the **item DB** (not the inventory) emitting `id<<6` for every item the guild sells the
  store level has unlocked (filter: valid·price>0·guild-stock-byte>0·two id-window episode
  gates·`tier[gi]≤store_level`). Tier tables `DAT_005cfabc` (membership) + `DAT_005c6ef0`
  (qty-cap) extracted from the unpacked `.data`. Shop modes get **no leading "-1 Nothing"
  entry** (the engine's `uVar4==0` predicate); per-item qty-cap → the "N Left" number.
- **Render** — the buy-row format `"%s - %d Left"` (only when the cap is in (0,100); the common
  100 / a 0 show just the name), the "Purchase Price-"/"Sell Price-" description label (the
  guild scene-6 path; the house keeps "Base Price-"), and the description price × the buy/sell
  multiplier. `display_menu_render` wired into `scene_guild_render` (it self-gates on the slide
  counter, so it's a no-op at rest).

**Verified 1:1 at the fresh open** on the studio session `merchants-guild-ui-flow-20260611-052747`:
the port (frozen at the open — item-nav/qty are steps 3-4) shows the identical Swords list to
retail's pre-overlay frame — items (Worn Sword, Longsword), order, icons, daily caps (3 Left,
1 Left), description, price (140), Number possessed (0). The only render fix needed was the
price label. Build clean, host 3230.

PORT-DEBT carried forward: the buy-price **daily-market trend factor** (`FUN_004361b2`,
unported — neutral here so prices match) + the `Out Of Stock`/`Not For Sale`/`Adventurer's
Possession` post-buy status texts. Next: step 3 (item-list nav) → step 4 (qty overlay +
purchase).

## 2026-06-10 — audio: the 2 "missing worldmap sounds" = the first-shop-door-exit SE

`audio_diff` had flagged 2 missing sounds on the merchants-guild trace
(`se_019_id0150` + `bin/se/00re/system/00re_sys09.bin`), pre-cutscene. They were
mislabelled "worldmap sounds". Added a `ret_va` (immediate caller, module-relative) to
the retail audio se_play hooks — the file-SE caller resolved to `FUN_0048670f` (the
HOUSE/shop free-roam update), NOT the world-map sim. They're the **first-shop-door-exit**
SE (the tutorial trip out to the Guild): asm 0x488a95 gates on the first-exit flag
(`save[0x2bc5f]==0`), starts the dissolve `FUN_004526f5(0,0x11)`, sets the first-exit flags
(0x2bc5f/61/62), then plays `00re_sys09.bin` (file, string@0x5cefb8 — Ghidra dropped both
call args, confirmed via objdump) + `0x150` (id).

The port already ported that arm (`player_ctrl_worldmap_exit_arm`: fade + flags) but
stubbed the two sounds (standing `PORT-DEBT(door-SE)`). Un-stubbed (file then id, matching
the asm order). Both fixed (no RNG variant) ⇒ RNG-neutral like the existing `0x143` plays —
no LCG draw to mirror, debt resolved. `audio_diff` merchants-guild: **missing 2→0, whole
track ALIGNED (9 sounds)**; cutscene verdict still PHASE-CLEAN, host 3229. Noted a separate
`PORT-DEBT(door-exit-reset)` for the `DAT_056db000` zero the asm also does (untested,
world-map render already 1:1). `172ecc9`. Finding: `findings/audio-trace-diff.md` (the
`ret_va` caller-naming) + `findings/merchant-guild-RE.md` "Audio".

## 2026-06-10 — guild-cutscene CENSUS: frame-by-frame 1:1 + cutscene-capable verdict (`--align-anchor`)

The "Merchant's Guild cutscene db054-verdict BLOCKED — run the retail census" item turned
out to be a **misframing**, dispelled by analysing the already-captured traces. The cutscene
is richly probed on BOTH sides via `dialogue_tick` (`FUN_0046c320`: box_open/reveal/line_row/
st5_*) — 774 retail / 895 port frames. db054 is the **wrong clock** for a cutscene (its only
source `house_update` `FUN_0048670f` fires 0× retail / 2× port — a HOUSE free-roam counter
that doesn't advance in mode 6), so `--align-field db054` correctly reports "no shared values".
No probe-set extension was needed.

Census result (single −14100 anchor-rebased offset): **frame-by-frame 1:1, three ways** —
75/75 cutscene anchors frame-exact; all 8 dialogue fields × 774 common frames ZERO mismatches
(text reveal + line progression + standee tween bit-identical); rngcalls 0 per-frame desyncs;
raw rng 714/714 bit-exact over [15115..15828]. Residue = the load-suppression seam only (port
loads faster, renders ~121 early cutscene frames in retail's load bracket; phase pillar, accept).
Machine-confirmed; awaiting a user eyeball to upgrade to CONFIRMED-1:1 (parity ledger).

Tooling (`flow_diff.py`): new **`--align-anchor ANCHOR`** (align by a constant frame offset
from a shared anchor — the cutscene/mode-6 complement to `--align-field` when db054 is absent)
+ **`--frame-from/--frame-to`** (clip a pre/post-cutscene load seam out of a verdict). Fixed a
verdict false-positive that hit ALL scenes: per-draw geometry VAs (render_quad_add/flush) were
classified by the per-frame i-th-occurrence pairing → spurious DRIFT under any vcount-batching
difference (item-display-2's house verdict showed it too); now deferred to `render_diff.py`,
with the draw-VA detection computed on RAW frames (`_max_occ_any`) so an `--align-field` rekey
can't misclassify a once-per-frame state VA. `trace_studio` triage auto-falls-back to
`--align-anchor TEXT_ANIM_START` when db054 yields no shared values → `merchants-guild` verdict
is now **exit 0 / ✅ PHASE-CLEAN** (session.json refreshed). `+2` flow_diff tests (10 total);
test harness `run_main` now mirrors CPython string-`SystemExit`. Recipe:
`flow-trace-cheatsheet.md` "Cutscene verdict"; full breakdown: `findings/merchant-guild-RE.md`
"CENSUS DONE".

## 2026-06-10 — trace-studio: fix the caprange.start>0 full-white diff (port label renumber)

A freshly re-captured `merchants-guild-20260608-151902` (caprange `[330,1098]`,
`window_start=330`) showed a **permanently full-white diff over a world map that was
clearly 1:1** in the side-by-side scrub. Root cause: trace-studio renumbers only the
**retail** frames into label space (`convert.renumber_retail`); the **port** kept its
0-based `frame_<k>` names. When `window_start==0` (the common window) a 0-based name
already IS the label, so the gap rode along uncaught — but at `window_start=330` the
label-keyed `build_diff` paired port frame N against retail's 330-later frame N (port
`frame_00400` = world map; the diff compared it to retail `frame_00400` = a frame 330
ticks later). The ordinal-keyed video scrub was unaffected (both sides sorted), which
is why the scrub looked fine while the diff was white. Fix: `renumber_retail` →
side-agnostic `renumber_to_label`; `capture.py` renumbers the port too (`run_port`,
idempotent, return discarded — `port_base` stays global.json's `frame_base_abs`);
`frame_range` now reports the label range so the viewer's `labelOf`/`diffAt` land on
real data. New `test_trace_studio_renumber.py` (port 0-based + retail abs both rebase;
idempotent; `window_start=0` no-op; explicit broken→fixed contrast). Verified on the
session: diff @ label 400 (world map) is now bit-black; first real divergence is now
**label 580** — the guild first-visit cutscene the port doesn't have. `7a7e280`.

## 2026-06-10 — RE: merchant's guild = engine mode 6 (Market), cutscene = iv1_3.ivt

Scoped the next arc. The world-map "Merchant's Guild" (dest 3) is internally the
**Market scene, mode 6** — fully stubbed in the port (no `sim.c` update, `main.c`
`default:` render, no worker-load ⇒ the cyan/blank). The first-visit cutscene is fired
by the per-location event tick `FUN_004922c0` → `FUN_0044ba2c(1,3,1)` → spawn (NOT the
iv1_5/iv1_6 dispatcher), gated by the per-location first-visit flag `DAT_0450f3f4`
(working-arena byte `0x2bc5c`). Dialogue script (group 1, index 3) = `iv/iv1_3.ivt`,
runnable as-is via `scene1_intro_dialogue_start_single(1,3)`. Render chain to port:
`FUN_00490e35`→`FUN_00494a73` (561 B 2D bg blit, reuses the ported `FUN_0046b00a`).
Full RE + incremental port plan: `findings/merchant-guild-RE.md`.

## 2026-06-10 — dialogue: mute voice/SE lines while fast-forwarding (engine-quirk §120)

User-flagged long-standing divergence: retail mutes the dialogue's spoken lines
when you hold X to skip faster; the port played them all (the 3 EXTRA voice
grunts the menu-SFX recapture surfaced — `re_wakata_b`/`tea_sodesu`/`re_un_a`).
RE'd the exact gate from the `se:` opcode handler (asm 0x46d885): `cmpl
$0x1,DAT_005c78ec; jne <skip>` — the voice fires ONLY when the per-frame internal
step count == 1 (normal speed). Holding X (0x20 → 2 steps) or button-3 turbo
(0x40 → 0x50 steps) raises the count, so the play is skipped — keeping a fast
skip from garbling overlapping clips. Ported as `(held & IVE_BTN_FF)==0` on
`IVE_OP_SE` (≡ steps==1 in the port's model; tutorial scenes permit FF so
local_104≡1). Recapture+audio_diff on item-display-2: **VERDICT ALIGNED** — 0
missing, 0 extra, all 9 sounds matched (`tea_chot`, spoken at normal speed, still
fires both sides). Host suite 3229✓. Engine-quirk §120; closes the FRONT #7
audio residual — the whole session's audio is now 1:1.

## 2026-06-10 — display-menu + footstep SEs: un-stub the silent menuing (FRONT #7)

The audio-trace diff's debut find — the item-display interaction is silent in the
port (14 missing triggers over 6 sounds, user-confirmed by ear) — is fixed. The
cc04 interaction (`scene1_player_ctrl.c`) already mirrored the SE-variant *RNG
draws* for stream alignment but stubbed every actual play. Un-stubbed, sourcing
exact paths/ids from the decompile + the retail audio capture (Ghidra dropped the
SE-id args, so the footstep gate came from objdump 0x48c824):

- **open SE** (all.c:87705-87708): `rand()%3` → `00re_sys04a/b/c.bin`.
- **confirm clip** (all.c:87938): `rand()&1` → `00re_sys05b/05a.bin` (05b at
  index 0 — recapture showed the exercised placements all draw 0 and retail plays
  05b; my first guess `[05a,05b]` was flipped, caught by the audio_diff "extra
  05a / missing 05b" and swapped).
- **confirm/pickup/cancel beep** (LAB_0048917a): `FUN_00499519(0x143)` on the
  Z-edge arm (r==3) + cancel (r==2).
- **walk footstep** (asm 0x48c824, `FUN_00499519(0x166)`): a sibling of the
  foot-dust emit — fires every walk frame where `STATE==1 && (COUNTER&0xf)==0xa`,
  *independent* of the `(db054&0xf)==0` dust cadence. `player_ctrl_b850_foot_dust`'s
  early-return was restructured into a nested gate so the step still runs on the
  off-cadence frames. (id0166 is NOT the menu cursor — the 5 ticks all fire in
  free-roam *before* each open; it's the walk footstep tied to the dust.)

All plays are RNG-neutral (the variant-select draws were already mirrored), so no
stream shift: triage worst-frame/px/py/dust unchanged, host suite 3229✓. Verified
on `item-display-2` (recapture `--only port` + `audio_diff`): **missing 14→0, all
6 sounds matched** (open ×3, confirm-beep ×5, confirm-clip ×3, footstep ×5).
**Remaining audio delta:** 3 EXTRA dialogue voice grunts (`re_wakata_b`/
`tea_sodesu`/`re_un_a`) the port plays that retail doesn't — pre-existing,
separate from the menu SFX, a dialogue voice-selection follow-up.

## 2026-06-10 — display-menu: slide the bottom description panel with the menu (`d4899bc`)

The `item-display-2` session's WORST frame (label 181, gt8≈185441, mean 17.7)
was mis-RE'd in `shop-display-menu-RE.md` #8b as the `pressed&0x40` Item-Details
sub-view. It is not. The trace settles it: `cc04` goes 1→0 at frames 176→178 on
BOTH sides (the menu is *closing*, db054 unfreezes 121→123), and the frame
sequence shows retail's bottom **description panel sliding out to the right**
while the port drew it FIXED at x=0 — the "narrow-right detail panel" was just
the 640-wide parchment quad translated off-screen.

- **Root cause = Ghidra arg-drop.** `FUN_0046b00a`'s tail prints the call as
  `FUN_00469b3a()` (no args — the FPU/stack floats were dropped), but the panel's
  internal `local_30 = param_1` is the slide x-offset `fVar1 = 640 −
  (DAT_0734b98c<<7)`, the same offset the main panel rides. At settled-open
  slide==5 ⇒ fVar1==0 ⇒ x=0, which is exactly why the fixed-x port matched the
  open/close ENDPOINTS but diverged across every slide frame (worst = mid-close).
- **Fix:** thread x0 (already computed in `display_menu_render`) into
  `display_menu_description_render` and offset the bg + all four text lines by it
  (retail's `param_1+80` / `param_1+304`).
- **Verified** (recapture `--only port`, studio's own triage): label 181
  185441→near-black; the CLOSE slide is now bit-exact (`R[f]=0.00` every frame).
  Worst frame moved to label 125 gt8≈29182 = the menu-OPEN slide-in, a *separate*
  asymmetric ramp-phase residual (open ~4 frames off, close perfect — FRONT #6;
  the whole menu rows+desc are ~9 mean off, best-aligned same-frame so NOT a clean
  1-frame offset). Host suite 3229✓.
- The actual Item-Details overlay `FUN_0046a336` is **never exercised by this
  bench** (no Button-3 press; no triage frame shows it) → still unported, deferred.

## 2026-06-10 — audio-trace diff: detect sound divergences from traces (no booting the port)

New parity pillar alongside d3d-trace: compare the port's vs retail's SOUND
TRIGGERS from the captured traces. Built the three layers (mirroring d3d-trace):

- **Port** (`src/audio.c`): the `--audio-trace` JSONL already logged `bgm_swap`/
  `se_play`/`fade_start`, but only with `t_ms`. Added a `frame` field stamped
  per-tick by `audio_trace_set_frame(g_tick.frame_count)` from the main loop
  (next to `d3d_trace_begin_frame`) — the same counter d3d/call-trace + the
  retail capture key on. (`2bf5efb`)
- **Retail** (`tools/frida/openrecet-agent.js`): the audio hooks now also hook
  `FUN_0049933c` (filename/voice SE — previously invisible), reconstruct the
  port's `se_NNN_idXXXX` label from `DAT_005d1584[slot]`, and dedup `bgm_swap`
  against `DAT_005d1960` so retail emits only on an actual track change like the
  port. `frida_capture` threads the name through. (`4c7846e`)
- **`tools/audio_diff.py`**: aligns by sound IDENTITY + trigger COUNT, not frame
  — a trace spanning a load has no constant port↔retail frame offset (retail
  plays an intro/load the port skips), so count-matching is the robust signal.
  Reports MISSING-IN-PORT / EXTRA / matched, `--session` resolver, exit code,
  `--summary-json`. (`573b…`, refined `52bbec3`)
- **Wired into the loop**: `export_trace` always passes `--audio-trace` so studio
  sessions carry `port/audio.jsonl` (`run-openrecet.sh` path-rewrites it like
  `--d3d-trace`); `trace_studio triage` runs the audio diff and surfaces it in
  the first-stop report + exit code (`fb8b224`, `0228fa2`). Doc:
  `findings/audio-trace-diff.md`.

**First catch, USER-CONFIRMED:** the item-display interaction is silent in the
port — `audio_diff --session item-display-2` pinpoints 14 missing triggers over
6 sounds: the cursor tick `se_039_id0166` ×5 (all), confirm `se_007_id0143` ×3
(in-house), and 6 `00re_sys*` system menu SEs — while the dialogue VOICE lines
play correctly (`tea_chot.bin` matched). The "3 extra" are tail-window voices
(port replays a few frames past where retail's capture stopped), not real extras.

## 2026-06-10 — iv1_5-tail pose-release slip CLOSED: tutorial re-arm deferred 1 frame (the last cross-seam residual)

The d=−1 the `{tutloadpin}` landing left behind. After tutloadpin equalized the
load **bracket** to 8f/8f, the only remaining cross-seam gap was the port firing
iv1_6's `LOADING_START` **one frame early** after iv1_5 completed (last
`CONV_POSE_BLINK`→`CONV_POSE_END` = 8f port vs 9f retail) — a constant d=−1 on
every iv1_6-internal anchor, d=−2 after iv1_6's own tail. **Root cause (retail
ground truth from the captured call-trace, NOT theory):** retail's dialogue gate
`DAT_0438b1c8` clears 1→0 in `FUN_004536cb`'s outer-loop tail, *after* that
frame's `FUN_0044bd0d` dispatch already ran and saw it still busy — so the
scheduler arms the next tutorial only the **following** frame (item-display-2
retail trace: iv1_5 `FUN_0046c320`-done @f15933, iv1_6 `FUN_00452d07` load-spawn
@f15934 — a 1-frame gap). The port's `scene1_tutorial_dispatch_tick` runs *after*
`scene1_intro_dialogue_tick` in the sim, but the port cleared its gate-equivalent
(`D_TUT`→`D_IDLE`) the **same** frame the script completed, so the dispatch saw
not-busy and armed iv1_6 same-frame. **Fix** (`c8a40df`): a one-frame `D_TUT_DONE`
settle state — a naturally-completing tutorial goes `D_TUT`→`D_TUT_DONE` (not
`D_IDLE`); `_busy()` auto-covers it (dispatch skips, like retail's stale gate),
`_posing()` keeps the pose on (retail's blip-off lands at the next
`LOADING_START`, not at completion), and the next tick drops to `D_IDLE` →
dispatch arms iv1_6 (+9 from the blink). The same latch on iv1_6's own completion
closes the iv1_6-tail d=−2. A skip (`skip_to_end`) still drops straight to
`D_IDLE` (retail's skip teardown clears the gate same-frame, unlike a natural
end); prologue `D_SCRIPT1`/`D_SCRIPT2` untouched. **Verified (recapture #7,
`--only port`):** iv1_5-tail interval 8f→9f, every iv1_6 anchor bit-aligned to
retail (+733/+734/+1166 from iv1_5 `CONV_POSE_START`), triage `problems: []` with
1848/1848, **over-threshold 861→529** (the whole iv1_6 seam — ~332 frames —
collapsed), rngcalls desync +26→+12 (the seam's in-bracket consumption), `panim`
DRIFT→CONST-OFFSET. (Boot save-load is wall-time like §119, so absolute frames
shift run-to-run by the boot delta — alignment is anchor-relative, unaffected.)
**Pending human visual confirm** in the studio; the d=−1/−2 is gone at the data
level. Remaining on this session: the Item-Details sub-view (now the worst frame,
label 181) + the companion micro-DRIFT (FRONT #5/#6). RE: `findings/shop-display-
menu-RE.md` #8 + `findings/conversation-pose-driver.md`.

## 2026-06-10 — {tutloadpin}: the tutorial load bracket pinned on BOTH sides (queue #1, user-asked)

The 4-label post-seam axis shift (quirk #119: the bracket is worker-thread
wall-time — 2f/5f on one capture) is now normalized by mechanism. New trace-global
op `{"tutloadpin": 8}`: the port overrides `IVE_TUT_LOAD_FRAMES`; retail BLOCKS the
`LAB_00452aab` worker at its tail via a CModule until 8 frames past the
`DAT_0438b1c8==2` arm. Key ground truth (hand-decoded — the LAB isn't in the
decompile): the worker tail performs the WHOLE bracket-end handoff itself
(CloseHandle → zero `DAT_06a49950/5c/60` → `b1c8=1`) mid-frame on its own thread,
so v1's per-frame gate re-write lost the race systematically; blocking the worker
IS a slow disk. Three more landings to get there: TinyCC rejects `__stdcall` (use
0-arg kernel32 fns — stdcall==cdecl at zero args; regression test compiles the
CModule via local frida), hooks must install post-`ensureBase()`, and the release
must run before the Present hook's suppress_loads check (else the bracket-END
frame drops retail-only). Plus a latent harness bug exposed twice: `session.
detach()` hangs forever on CModule-hooked captures and the old teardown killed the
target only AFTER detach → wedged pipeline + 3 leaked retails; teardown steps are
now daemon-bounded with spawn-kill FIRST. **Verified on item-display-2 (recapture
#6):** brackets 8f/8f on BOTH sides (anchor-identical at window-relative +647→+655
and +1380/+1381→+1388/+1389), kept counts EQUAL 1848/1848 (triage `problems: []` —
the kept_count_mismatch is dead), iv1_5 + dialogue-1 anchors aligned offset-exact,
over-threshold 916→861, worst seam frame 329.8k→246k px, and the rngcalls delta is
a FLAT 580 with six attributable step points (2 pause-boundary wing emits + the ±1
seams; span 26). **Remaining cross-seam residual = the pre-existing 1-frame
iv1_5-tail pose-release slip** (port's last BLINK→CONV_POSE_END = 8f vs retail 9f),
now exposed as a constant d=−1 on every iv1_6-internal anchor (and d=−2 after
iv1_6's own tail) — promoted to its own FRONT item. Lint INFO
`loads-without-tutloadpin` flags unpinned crossings from the session's previous
anchors. Recorder save-clobber fixed on the way (`<name>.save.bin` now never
overwritten by different content).

## 2026-06-10 — Dialogue typewriter fade fixed for real: per-CHAR law (gap #4)

The reveal gradient the user kept flagging as missing. The 06-09 port (a278101)
faded per ROW — a misread of FUN_0047d464: 0x47d4d4 only INITIALIZES the budget
local; the factor is recomputed per glyph (fildl 0x47d528) and the budget
decremented per logical char (decl 0x47d60e). Real law: char i at
alpha·clamp((budget−i)·0.2, ≤1.0) — the trailing ~5 chars ramp 0.2..1.0 riding
the reveal head (retail's ghost trailing char). The FRONT's ALPHA-pipeline
suspect was disproven by the port's own row fade visibly dimming (vertex alpha
flows). New `font_draw_text_fade` (shared walk, font_draw_text = budget −1
wrapper); dialogue_draw_row passes max_chars. Recapture: text-strip gt8=0 on
every sampled reveal frame, session over-threshold 1212→916. Gotcha #18 added
(loop-counter init misread). RE-doc follow-up #3 corrected.

## 2026-06-10 — Standee offset + portrait outline + NPC note: ONE cause, the iv1_6 load bracket (diagnosed, no code change)

Queue #3 (standees "wider apart", worst frame 1792), queue #7 (Tear portrait
whole-outline diff ~1453-64), user notes @1448 + @1844 all collapse into the
inter-dialogue LOAD-BRACKET length. Template-matched standee trajectories: both
sides slide the identical path at the identical speed (entry 8px/f@640 =
`chr:0:speed:8`; exit 16px/f = 2 internal steps/frame, retail too), and relative
to each side's own LOADING_END the schedule matches ±1 frame — the visible 4-label
lead is retail's iv1_6 bracket (5f) vs the port's `IVE_TUT_LOAD_FRAMES` (2f,
calibrated on iv1_5's 2f bracket) + a 1-frame iv1_5-tail slip. Retail's bracket is
`FUN_00452d07` → CreateThread worker = wall-time (quirk #119) — 2f and 5f within
the SAME capture — so the port constant is NOT tuned to 5. Also corrected the
kept-count-mismatch localization (labels 1379-84 = the load bracket itself; label
= port_abs − 445, the old "mid-dialogue-1" reading used a wrong mapping) and
dissolved #7 entirely (settled frames gt8≈2 ⇒ no filtering residue). Docs:
RE-doc follow-up #8, engine-quirks §119, FRONT.

## 2026-06-10 — Hands-up carry pose (db048==0xc) ported

`4bc8a0b`. The placement-confirm carry: the r==3 pick-up arm sets the player-ctrl
state to 0xc + anim 4 (latch-gated, all.c:87916-87929); the free-roam arm then runs
FUN_0048cdcc's 0xc branch — re-assert anim 4, hold 26 frames, release. While held,
d-pad interactions and the whole walk-impulse region (facing/moving decode included)
are gated off, exactly the engine's 87617/87524 gates; the b850 move/damp keeps
running. Pose-only: retail draws NO overhead item sprite during the carry (the old
"held item red-vs-gold" note was sparkle-phase residue). Carry-window frames drop
from ~2,290 px>8 each to 2-65. Attribution note: the label-441 whole-frame diff
(~160k px) was proven PRE-existing via stash→rebuild→recapture (162.6k without the
chip) — filed as the menu-close camera pan-out residual under FRONT #6.

## 2026-06-10 — Placement-prompt speech bubble (gap B) ported

`36a8ab2`. The "What will you place?" bubble over the placement menu — and the hunt
killed two wrong theories from the RE doc: it is not a localized *string* (no encoding
of it exists in the exe or any data file) and not world-projected. All three prompts
are **baked, Carpe-Fulgur-localized item_win.tga sprites** drawn by FUN_0046b00a
itself in screen space at dst(menu_x−128, 48, 191, 63), so they ride the panel's
slide-in (the motion the session notes pinned at labels 589/592). Variant select:
window flag 1 → "What will you place?" src(832,560,1023,623); 2 → "Exchange with
what?" src(256,768,447,831); Vender-category highlight (live 4-char category-name
compare, FUN_0049ef78) or any other flag → "Place Vending Machine"
src(256,704,447,767). The flag (DAT_0734b990 / FUN_004681ec, single call site
0x488dac) is set by the cc04 arm right after menu open: faced grid cell occupied → 2,
empty → 1. Found by diffing per-frame **quad-emission ret_va sets** from the session's
existing call trace (menu frame vs roam frame) — no new capture needed. Verified on
the item-display-2 recapture: 0-1px at the settled frame and across the slide.
Bonus residual surfaced: retail draws the menu hand cursor 1 frame earlier at open
(label 587) — filed under the menu-boundary cluster.

## 2026-06-08 — Trace-studio editor: the captured-frame-index timeline

The trace editor's port/retail timeline, redesigned around the right primitive. Semantics:
`docs/findings/trace-editor-segment-alignment.md`.

The insight (the user's): the capture is phase-synced + RNG-pinned and runs **1:1** on both
sides, so alignment should not be *reconstructed* from the trace's `{wait}` segments + anchor
offsets — it is already inherent. The x-axis is just the **dense captured-frame index** (one
tick per real frame on that side); place each side's anchors + inputs at their captured index
and a 1:1 capture aligns with **zero forcing**. Where the traces diverge (different per-side
frame counts) the two rows drift apart — that *is* the divergence; you iterate edits until
they reconverge. Loads are suppressed (0 captured frames) so the index re-syncs at every load
boundary regardless of how stretched a load is.

(Two earlier same-day iterations — a piecewise port-axis re-base, then a sequential-band
layout — both tried to reconstruct alignment from segments and broke on real traces: segment-
relative positions diverge across a load, and a mid-capture divergence put corresponding 1:1
frames at different x. They are replaced wholesale.)

Pure core in `align.mjs` (mirrored in `model/segments.py`, pinned in the JS↔Python golden):
`loadSpans` (LOADING_START→END pairs), `capIndexOfAbs(abs, base_abs, loads)` = `(abs −
base_abs) − suppressed-load-frames-before-abs` (the dense captured index), and `absOfCapIndex`
(the inverse, for click-to-edit). The band functions (`resolveSide`/`editorLayout`/`bandAt`/
`absToBand`) are gone. `TraceEditor.mjs` runs entirely on `capIndexOfAbs`: anchors at their
captured index per side, inputs via `segBase + frame → capIndexOfAbs`, cursor at `cur`, a
`tl-loadmark` tick at each suppressed-load boundary. Verified on `merchants-guild`: the 1:1
region (frames 0–161, guild entry + world-map menuing) has port `g` = retail `g` exactly;
at frame 162 the port diverges to cyan (no more anchors) and retail's dialogue (162–933) shows
alone. Swept clean over all 14 sessions; `align.test.mjs` 41/41.

The retail-no-frames symptom (Frida `connection-terminated` after a long load) remains a
separate capture-harness issue; the editor aligns fine regardless (it maps from `base_abs` +
the anchor/trace files, never captured frames).


## 2026-06-05 — LOAD GAME slot picker rendered 1:1 (FUN_0049b556)

User-directed next front (the title→load-menu→in-game arc): the continue/load
slot picker was a functional placeholder (a left-aligned text list on a
`dungeonbord` panel). Ported the faithful **`FUN_0049b556`** (2810 B) so the
LOAD GAME screen renders pixel-1:1 with retail.

- **What it draws** — a horizontally-paged grid of save cards from `item_win.tga`
  (= `DAT_073d8748` = `g_sysassets.item_win_tga`; the placeholder used the wrong
  `dungeonbord` texture): the centre page on-screen + the L/R neighbours off-screen
  for the page-slide, five vertical rows each (one above + three visible + one below
  for the row-slide). Occupied card = parchment bg + the HUD gold clock-frame detail
  panel (src 480,0-768,128) + a rotated day/time hand (`render_quad_draw_rotated_rect`)
  + big day# and gold digit rows (reused `scene1_top_hud_draw_number`) + a money-banner
  tile from `pause.tga` (= `DAT_073d86a8`) + the merchant-level badge (reused
  `scene1_merchant_hud_draw_level`) + right-justified SCORE/LOOP columns + a
  TIME H:MM:SS line; empty cards show `NO-DATA`. Up/down scroll arrows off the ends.
- **Slot summary fields** mapped to `save_bank` dword offsets (bank-0 base =
  arena+0x0b10): PLAYTIME(2, frames@60fps, doubles as the empty test), GOLD(3),
  SCORE(0xb0f7), LOOP(0xb0f9), CARD_DAY(0xb0fb,+1), PORTRAIT_ROT(0xb0fc),
  CHAR_LEVEL(0xb100), GAME_MODE(0xb759).
- **objdump-recovered FPU constants** (Ghidra dropped the scaled-`sin`→`ftol`
  loads, the §97/§99 class): selected-card bg brightness =
  `sin(anim·0.1)·32 + 159` (+ confirm-flash `sin(pulse·π/30)·128`), others flat
  `0x5f`, all under `D3DTOP_ADDSIGNED`; arrows under MODULATE. Card stat text
  (SCORE/LOOP/TIME) draws at **scale 0.8** (`0x519470`) while the slot# + NO-DATA
  use 1.0 (`fld1`) — the user spotted the 0.8 text-size gap on the feed.
- **New leaf helpers**: `font_draw_text_right` (FUN_0047d2db, right-justified
  text — measure-then-draw-at-`x−w`); exported the previously-static
  `scene1_top_hud_draw_number` (FUN_00406a60) + `scene1_merchant_hud_draw_level`
  (FUN_00481ec3) for reuse (both draw from the same `item_win.tga`).
- **Pixel parity** (`title-load-picker --target both`, fa7c82 save): frame 60
  (slide-in) **0/786432** diff px; settled frames 120/180/260 **1 px** each
  (max Δ20 — a sub-pixel rounding on the clock hand / one glyph edge). Goldens
  blessed (port + retail). Scenario reads the save-roundtrip `fa7c82` save (the
  beginning of the end-to-end load→move-sword→save→reload reference trace).
- **PORT-DEBT(modetag)**: the bottom-right game-mode tag (Endless/New Game+/
  Survival) + the new-game-overwrite grey-out flag (`DAT_096432f4`) are not drawn
  (the load path never sets them).

## 2026-06-05 — Render-parity diff engine (Phase 1): stable texture identity

Closed the last remaining Phase-1 item. The d3d-trace serialised a bound texture
as its raw COM pointer — allocation-dependent, so `render_diff` could only match
texture identity *positionally* (opaque-pointer mode: Nth distinct pointer → `#N`),
which mis-matches the moment the two sides bind in a different order and can never
NAME the texture. Now `SetTexture` carries a load-stable **`tex_name`** (source
asset path) on both targets:

- **Port** (`src/d3d_tex_names.{c,h}`, pure-C, host-tested): a `texture* → name`
  hash registry populated at the load chokepoint `sprite.c:sprite_load_impl`
  (covers 2D UI, chr sheets, dialogue, mesh textures via `sprite_load_mipped`) and
  cleared in `sprite_destroy`; `d3d_trace_SetTexture` looks the bound pointer up.
- **Retail** (`tools/frida/openrecet-agent.js` `installTexNameHooks`): hooks the
  two loaders `FUN_0047193c` (UI, ppTexture=arg1 slot, path=arg2) and `FUN_00471b24`
  (mesh, ppTexture=arg0 slot, path=arg1) — both write the created texture pointer to
  the first dword of their output slot (the `FUN_004cd30e`/D3DXCreateTexture…
  ppTexture arg); onLeave reads `*slot` and maps `ptr → name`.
- **render_diff** (`tools/render_diff.py` `_event_key`): drops the raw `texture`
  pointer from the alignment key whenever `tex_name` is present, so binds align on
  the asset NAME (order-/pointer-independent) and a real texture swap surfaces as a
  diff block with both names; pointer fallback for nameless binds.

Validated cross-side on `boot-idle` (`--target both`): the four shared title
textures (`title01.tga`/`title_bg2.bmp`/`title_fuki.tga`/`title_waku.tga`) emit
identical names on both sides and align under render_diff across disjoint pointer
values; retail's extra `nowloading.tga` bind now surfaces *by name* (a real
structural lead for the Phase-2 sweep) instead of hiding in pointer noise. Tests:
7 new host tests for the registry (suite 3177 pass) + 4 new render_diff tests
(19/19). Also fixed a `scenario-test` footgun — `wslpath_w` left a relative
`--run-dir-root` output path relative, so `d3d_trace.jsonl`/`audio.jsonl` silently
landed in the exe's cwd; now resolved to absolute.

## 2026-06-05 — `boot-idle` title frame is structurally 1:1 (Phase 2)

Walked the flow-trace down the `boot-idle` title chain to a clean verdict:
`flow_diff --mapped-only` now reports **✓ chain + data aligned (40 vs 40 calls)** on
frames 30 AND 60 — the port's instrumented title call chain and the data through it
match retail. Three pieces:

- **`input_poll` (0x47b73c) → `chain_benign`.** Under the TAS harness the port
  substitutes synthetic input (`replay_input_poll`/`segtrace_input_poll` write
  `g_input_state[].buttons` from the trace), so the engine's real DirectInput poll
  never runs on the port while retail still calls it. It was shadowing every
  downstream divergence at seq 2. Same class as the 0x47be2f clock read.
- **SIM-leg fields for `scene_title_sim` (0x49a59e).** Added `CALL_TRACE_BEGIN_STUB`
  (field-bearing event + `"stub":true`) and declared the 10 persisted menu-state
  fields on both sides (port reads `g_scene_title_anim` at entry; retail reads the
  `DAT_096435xx` globals at onEnter — mapping in `scene_title.h`). Verified bit-1:1
  (frame_counter==pulse_phase==frame index, menu_folding_out=1, rest 0) — the title
  menu state machine tracks retail exactly given the same input.
- **Render BATCHING fixed.** The port flushed per-quad; retail (FUN_0049c644) batches
  same-texture groups — menu items → one `vcount=24` flush (ADDSIGNED), decoration
  tiles → one `vcount=18` (MODULATE), standalone bg images per-quad. Split
  `title_quad`→`title_quad_add`(no flush)+`title_quad`; port flush vcounts are now
  `[6,6,6,6,24,18]`, bit-identical to retail. Pixel-benign (engine-quirks §98).

Remaining title deltas are all benign/coverage: the FPS overlay (896px, environment
artifact) and un-probed retail-internal funcs (CRT/MCI/audio). Next: extend the same
structural-1:1 check to the title scenarios WITH input (`title-down-press`/`-z-press`/
`-options`) — the menu-state fields now make cursor/select branches diffable.

## 2026-06-05 — Execution + dataflow trace: the divergence drill-in

User-directed next layer: d3d `--explain` names the wrong *draw* but not the *logic
cascade* that produced the wrong state. The existing call-trace was data-blind,
order-blind, and reported an unordered set diff (gap analysis in
`plans/execution-flow-trace.md`). Built the per-frame, both-sides execution+dataflow
trace that matches the call chain AND the data moved, naming the first call whose
inputs matched but output/state diverged. Data capture = declared payloads per fn
(user's choice), joined by (va, field-name). Three increments:

- **Port `CALL_TRACE_BEGIN/FIELD/END` + per-frame `seq`** (`call_trace.c/.h`): a
  field-bearing event assembled into a static buffer, fwritten atomically at END;
  `seq` stamped on every event for chain alignment. Seed: `fade_tick` declares
  phase/counter/duration/mode. `run-openrecet.sh` path-rewrites `--call-trace`.
- **Retail field spec + Frida reader** (`tools/flow/retail_fields.json` + `agent.js`):
  `flowReadField` (global/arg/argderef via `rva`) + `ctNextSeq`; onEnter attaches
  `f:{}` when the va has a spec. Validated live: retail `fade_tick` reads
  `{phase:0,counter:0,duration:170,mode:0}` from its globals.
- **`flow_diff.py`**: seq-ordered chain align (difflib over the va sequence) + per-field
  compare (float eps, int/hex exact, benign = presence-only) → first `[chain]`/`[data]`
  divergence. `--mapped-only` for sparse port coverage. Validated synthetic + real
  (names `duration: retail=170 port=0`). Supersedes the Counter-based `call_trace_diff`
  for drill-in.

Coverage grows with the Phase-2 sweep (each touched function declares fields on both
sides). Commit policy also changed this day: commit in logical units as you go.

## 2026-06-05 — Render-parity diff engine (Phase 1): per-draw vertex capture + `--explain`

Foundation for the frame-by-frame 1:1 sweep. The d3d-trace command-stream diff
saw only `prim_type`/`count`/`stride` for an immediate-mode draw — not the
vertices. Closed that gap end-to-end:

- **FVF decoder + `render_diff.py --explain`.** Decodes each aligned `Draw*UP`
  draw's captured vertices (FVF from the in-effect `SetVertexShader`, stride
  fallback otherwise) and names the **first divergent (vertex, field)** —
  `vertex 2 POSITION.z: retail=-7.2 port=-6.5 (Δ+0.7, fvf=0x142)`. Vertex bytes
  are excluded from the alignment key, so a pure-vertex divergence (same
  command, different verts) still surfaces. Float compares use `--vertex-eps`
  (the engine isn't byte-identical); DIFFUSE/SPECULAR exact; one-sided draws →
  `[structural]`. Synthetic-trace tested across field/count/structural/color/
  fallback paths.
- **Port capture (`src/d3d_trace.c` + `--d3d-trace-verts`).** `d3d_prim_vcount`
  + hex emit append `vb_nverts`/`vb_bytes` (+ `ib_*` for indexed-UP) to each UP
  draw, 64 KiB/draw cap. Wired through `main.c` and `export_trace.py`. Build
  clean, 3169 host tests pass.
- **Retail capture (Frida agent + frida_capture `--d3d-trace-verts`).** Mirror
  `primVcount` + `readByteArray`→hex, same field names + cap. Validated on a
  live retail title capture: port and retail **decode the same `(1024,768,0,1)`
  screen corner**, FVF `0x1c4` (XYZRHW|DIFFUSE|SPECULAR|TEX1, 2D/HUD) and `0x142`
  (XYZ|DIFFUSE|TEX1, 3D billboards). On an unsynced title pair `--explain`
  correctly reports structural draw differences, not fabricated field diffs.
- Only UP draws captured; VB-backed mesh draws + stable texture-identity are the
  remaining Phase-1 items. Schema: `findings/d3d-trace.md`; usage:
  `findings/render-diff.md §--explain`.

## 2026-06-04 (evening) — Load-a-save arc: working arena + continue picker + post-fade branch

New front: the title-screen **Continue / load a save** flow. Landed the data
path that was entirely missing — the port had no live game-state arena at all
(it faked the per-stage record with 4 hardcoded selector globals).

- **W1 — working arena (`save_work.{c,h}`).** Ported the engine's SECOND save
  arena: the live/working arena (`DAT_044e2c88` header / `DAT_044e3798` banks)
  that gameplay actually reads, distinct from the disk-mirror save arena
  (`save_bank.c`). `save_work_load_slot` (FUN_00490259) copies a chosen save
  bank → active working slot + recomputes the live inventory count (first empty
  item slot @dword 0xaec6). `save_work_sync_from_save` (FUN_004902aa).
  8 host tests. Architecture: `findings/save-working-arena.md`.
- **M1 — continue slot-picker (`title_continue_picker.{c,h}`).** FUN_0049b537 +
  the FUN_0049a59e `DAT_09643524==1` body: a 3-row grid cursor over the 100
  save slots with the engine's column-scroll slide animation; A on an occupied
  slot (bank dword 2 != 0) loads it via save_work_load_slot, B cancels.
  11 host tests. + save-header last-used-slot accessors (DAT_056e578c, dword 7).
- **W2 — title integration + post-fade branch.** Menu dispatch now matches the
  engine switch (codes {0,5}→NEW, {1,4}→picker; was {0,4,5}→fade).
  `scene_post_fade_init` splits on `continue_mode` (DAT_0438bed4): NEW resets
  bank 0 + seeds working slot 0 + arms the opening prologue; CONTINUE preserves
  the picker-loaded working slot, skips the reset AND the prologue. +2 tests.

- **M2 — picker render + END-TO-END VERIFIED (human, with the user's save).**
  `scene_title_render` draws the picker when submenu_state==1 (functional
  vertical-list stand-in; faithful 3-col FUN_0049b556 grid is PORT-DEBT). Booted
  the port with the user's real save (`--save-override` the fa7c8 blob; cut trace
  `tests/traces/save-roundtrip/trace-to-picker.jsonl`): title **LOAD GAME** →
  picker shows **slot 1 occupied = 440 G** (others empty) → load → **HOUSE
  free-roam with NO opening prologue** (continue path correct). Decor now sourced
  from the loaded save (stage selector chip). **Confirmed the items-on-display
  gap**: the back-table swords the save has out (`[O O _ _]`) do NOT render — the
  shop-display renderer is unported (task D). Feed has the picker + loaded-shop
  montages.
- **stage selectors from save** (commit b23191d): `stage_init_house` reads the
  four wall/floor/carpet/table selectors from working-bank dwords 0xb379..0xb37c
  (first gameplay subsystem to read the working arena).

- **Items-on-display RE (task D) — UNMAPPED, do not port blind.** A fan-out map
  was wrong twice: `FUN_00456f56` is the dormant CHARACTER walker (not items),
  and bank `0x9e76` (100×18 records) is the per-bank RANKING summary consumed by
  `FUN_0049f012` (ranking screen) — NOT the shop-floor display. Corrected
  `save_bank.h`'s misleading "item-grid scratch" label. The real shop-display
  renderer + its (likely runtime, not saved) source array need a call-graph
  diff on a retail HOUSE frame with merchandise out. Plan:
  `findings/shop-item-display-RE-status.md`.

## 2026-06-04 (pm) — Free-roam RNG-consumption gap CLOSED: invisible dev coord-overlay (§95)

`phase_probe house-walk-down-dense` now reports **`rng` AND `rngcalls` ALIGNED**
(bit-exact LCG state at every db054) — the steady free-roam RNG desync is gone.

- **Root cause was NOT the hypothesized missing every-16 emitter.** The per-frame
  `rngcalls` numeric diff showed a flat **−1 read/frame on every frame** (not a
  periodic +N step). The every-16 foot-dust (`FUN_0048b850`→`scene1_spawn` type
  0xe) and every-4 wing-sparkle were already bit-exact. Drilled the real consumer
  with a `--call-trace` hook on `scene1_spawn` (0x447f4f) + the numeric diff.
- **The culprit:** `FUN_00442cef`'s tail runs an **invisible developer coordinate
  overlay** every frame — one raw LCG step (`thunk_FUN_005041f6`, 442cef.c L421)
  formatted as `"%d"`/`"X:%f"`/`"Y/Z"` into the unrendered debug text grid
  (`DAT_06a47aac` via `FUN_00451874`). The overlay draws nothing in the Steam
  build but still advances the shared LCG once/frame and is the tick's LAST
  consumer → omitting it desynced the whole downstream stream by 1 step/frame
  (first visible db054≈37). **engine-quirks §95.**
- **Fix:** `src/scene1_sim.c` consumes `(void)rng_next15()` at the default-arm
  tail (faithful: consume, render nothing). Regression test
  `test_scene1_ingame_default_arm_consumes_debug_overlay_rng` (default arm burns
  exactly 1 more LCG step than the transition arm). Aligned diff at db054=64 =
  **1.4% px** (residual = Lead B player-anim phase + benign FPS overlay §90).
- **Lead B still open:** player (Recette) anim cycle is not phase-normalized by
  `{phasepin}` (companion-only today). See `findings/freeroam-rng-consumption.md`.

## 2026-06-04 — Phase/determinism tooling + Tear anim-phase root-caused; two leads open

Built the standing **phase/determinism toolchain** and used it to crack the Tear
anim-phase question and surface two concrete next leads.

- **Tear anim-phase (#3/#4) = deterministic phase-ORIGIN offset, NOT a logic bug.**
  Per-frame counter diff on the synced `house-walk-down-dense` trace: port `db054`
  is **+1518 constant, zero drift**; companion FRAME/COUNTER/facing bit-exact once
  phase-aligned. Root: retail freezes `db054` through the `recet_op.wmv` intro
  video (=43 at HOUSE_FREEROAM); the port skips the video so its counter
  accumulates the skipped frames. **engine-quirks §94**; verdict in
  `findings/scene1-tear-visual-diffs.md`.
- **`{phasepin}` segtrace op** (port `input_segtrace.c` + Frida agent +
  frida_capture passthrough): zeros the companion's load-dependent phase (db054 +
  anim cycle) on BOTH targets at an anchor-relative frame so trace comparisons are
  phase-clean. Fire it AFTER the ~47-frame post-anchor load tail. Result: companion
  cframe/ccnt/facing **0/139** vs retail.
- **`tools/phase_probe.py`** — one-command port↔retail phase/determinism verdict
  (ALIGNED / CONST-OFFSET / DRIFT), auto phase+RNG pinning, `--drill` RNG
  call-site capture. Playbook **`docs/phase-debugging.md`**. The draw-side twin is
  `d3d_state_diff.py phase`.
- **RNG-consumption tracking**: port `g_rng_call_count` (rng.c) + a retail LCG hook
  → `rngcalls` counter both sides; `--drill` rolls retail's LCG callers up by
  function. Surfaced that the port under-consumes ~40 LCG calls/window.
- **Two leads left open** (`findings/freeroam-rng-consumption.md`): (A) the RNG
  desync = a missing **every-16-frame ambient emitter** through `scene1_spawn`
  (next: drill `FUN_00447f4f`'s caller, then port or dummy-stub-as-PORT-DEBT);
  (B) **Recette (player) anim** isn't phase-normalized (extend `{phasepin}` to all
  actors + add the player to the probe).
- Discipline set: **re-check determinism (`phase_probe`) after every RNG-touching
  change**; captures/diffs must be phase **and** RNG aligned.

## 2026-06-02 (PM) — Background-window NPC sprites ("motes" → bg_npc)

The shop's back-window townsfolk now draw their **bright character sprites**, not
just the dark contact shadows. The subsystem long misnamed **"ambient floor
motes"** (`scene1_motes`) is actually the **background-NPC system** — 6 NPCs that
drift past the window. The sim + dark shadow were already ported; the **bright
sprite render `FUN_0046f737` was a hidden stub** (the `scene1_shop_walker` L457
between-pass sweep, yet marked ✓ in the ledger).

- Diagnosed via a port d3d-trace at a free-roam frame on a user-recorded trace
  (`openrecet-trace-25120`): every quad was accounted for, none were NPC sprites.
  Root-caused to the stubbed `FUN_0046f737`.
- Renamed `scene1_motes.{c,h}` → `scene1_bg_npc.{c,h}` (+ test, symbols, includers);
  the dark pass is now `scene1_bg_npc_shadow_render`, the new bright pass is
  `scene1_bg_npc_sprite_render`. Restructured the record so its leading 11 dwords
  are the chr-actor sprite-state header the shared leaf consumes, and added the
  sprite-anim stepping (`FUN_00482a51` set + `chr_anim_tick` advance).
- Ported `FUN_0046f737`: per NPC, sheet `DAT_073a9b18[DAT_005c7ce0[type*2]]`
  (the 6 types → `chr{10,35,36,37,38,39}.bmp`), billboard × Scale(0.03), drawn
  via `scene1_chr_sprite_render` at `0xff7f7f7f`. Wired at the engine's L457 slot.
- **User-verified** rendering (screenshots + in-game, feed 2026-06-02). Deferred:
  exact anim-phase/identity vs retail. Note: there is **no** separate genuine
  ambient-mote effect today; a faint real ambient particle (the "tiny dots" in
  diffs) is expected to re-emerge later — see `docs/findings/scene1-bg-npc.md`.

## 2026-06-02 (PM) — Audio: dialogue voice lines, music-past-the-title, main-menu SEs

Wired the three audio gaps in the ported slice (title → new game → prologue →
HOUSE). Backend was already built (engine-quirks §87, `findings/audio-backend.md`);
this is the trigger/selector wiring. Audibility pending the user's listen-check
(scenario runs default `--silent-audio`); all paths verified to load + play
without errors.

- **Voice lines during dialogue** (`audio_play_se_file` = port of `FUN_0049933c`).
  The `.ivt` `se:<bin>` command (was the interpreter's no-op default) now plays
  the named loose RIFF/WAVE clip via a single-slot DirectMusic `LoadObjectFromFile`
  → `PlaySegmentEx(QUEUE)` on **SE path B** (the filename path, distinct from the
  resource SE table on path A — §87 corrects the "path B is dead" note). Reached
  from the pure-C interpreter via a new `g_ive_se_play_fn` bridge (mirrors
  `g_music_swap_fn`). Verified: the `intro-dialogue-lines` port run loads + plays
  all 12 opening voice/SE clips (`tea_mataku` ×2, `re_fue`, `piko`, …), 0 failures.
- **Music changes past the title** (`music_stage_track` + live `music_step_default`).
  `music_step_default` had `scene_state` pinned to 0, so the port played the title
  BGM forever. It now feeds the live `g_scene_state` + HOUSE stage inputs to the
  selector, which returns the per-stage track (HOUSE type 0 → close 9 / open 8 /
  fever 18). The swap is gated on `worker_load_busy() ||
  scene1_intro_dialogue_in_progress()` (a new gapless NEW_GAME→D_DONE bracket) so
  the title theme holds through the whole prologue, then the close theme
  (`bgm/close.wav`) lands at free-roam. Retail ground truth: `runs/bgm-probe`
  (track 0 holds frames 72–11983, → 9 at 11984). Port: 0→9 at the frame after
  `CONV_POSE_END` (prologue end); absolute frame differs only by the synthetic-load
  timing (PORT-DEBT §85).
- **Main-menu SEs** (`scene_title.c`): the title cursor-move (0x146) + select
  (0x143) SEs were stubbed — now wired on held-move / A-press (same pattern as the
  already-shipping settings submenu; user-confirmed the retail cursor auto-repeats
  while held). Confirm SE verified firing on the new-game A-press. The held
  auto-repeat *rate* (shared with the settings menu) is a noted input-fidelity
  follow-up.

+8 host tests (3125 pass). Commits: filename-SE loader + dialogue `se:`; music
selector wiring; main-menu SEs; ground-truth + landed docs.

## 2026-06-02 (PM) — Opening-prologue animation layer: tween, char-reveal, per-script skip, choice-box render, FX/line anchors

Closed the user-reported faithfulness gaps in the opening prologue. Mechanics in
`engine-quirks.md` §84-85; remaining deltas in `findings/opening-prologue.md`
§"Remaining real deltas" + the confirmed-parity ledger.

- **Standee tween** (`scene1_dialogue_run.c` `ive_run_tween`): `moveto` =
  target-only + slide by `speed` (×1000 fixed-point /1000); `col` = current
  colour, `colto` = per-frame delta + countdown. Drives Tear's −390→−100 @5px/f
  slide-in, the kuro fade-from-black, and the sigh/zzz effect fades — all
  user-confirmed bit-identical (retail-probe + `intro-opening`/`intro-sigh`/
  `intro-fade`). Was SNAPped (deferred PORT-DEBT), now faithful.
- **Char-based text reveal** (`ive_completion` + `ive_row_count`): END/book-icon
  latches at `(reveal-4)·32/32` chars (was a nominal-px metric → never
  auto-completed). A line now typewriters in ~its char-length; book icon appears
  on its own; ONE advance press moves on.
- **Per-script ESC skip** (`scene1_intro_dialogue_skip_to_end`): ends only the
  CURRENT script — iv1_1 → iv1_2 (the 2nd dialogue over free-roam), iv1_2 → free
  control (was: straight to free-roam).
- **Player gated during the dialogue** (`scene1_player_ctrl.c`): the free-roam
  walk arm is suppressed while the prologue dialogue is active/loading (engine
  gates FUN_00442cef on `b1c8==0`) — fixes being able to walk over iv1_2. (A
  first attempt gating the whole in-game arm broke the load — reverted; the
  surgical walk-only gate keeps the mote/RNG pump running.)
- **Choice box (ESC Yes/No)** (`choice_box.c`): MODULATE2X banner (was dim),
  un-dimmed unselected option (selection = cursor only), `|sin|` cursor bob (was
  too slow + overshot right), one-row prompt text y=192 (was 8px high). Verified
  vs `runs/skip-golden/arm485` via the new `intro-skip-prompt` scenario
  (`OPENRECET_FORCE_SKIP_AT=1`).
- **Anchors**: catch-all `EXTRA_SPRITE_{START,FADED_IN,FADEOUT,END}` (effect-
  sprite fade lifecycle over `fx_alpha`) + `DLG_LINE_CLEAR/SHOW` (between-lines
  box-gone gap), on both the port (`anchor_trace.c`) and the Frida agent. New
  `scenario-test` zoom pairs port|retail by capture order (was filename → retail
  "missing").
- **Remaining deltas (deferred, tracked)**: box-edge halo; FPS overlay; absolute
  prologue timing (§85); and the iv1_2-opening freeroam anims the port doesn't do
  — Recette look-up + blink, Tear angry pose + radial-lines, text fade-to-
  transparent on dismiss — all captured by the `intro-iv2-gap` reference scenario.

## 2026-06-02 — Context-sensitive ESC dispatch (Phase A); skip-event prompt RE'd

ESC was a skeleton in `main.c` WM_KEYDOWN that always `PostMessage(WM_CLOSE)` →
the "quit the game?" box popped in every context (incl. mid-dialogue). Restored
the engine's per-context routing (WndProc `FUN_0047b2e7` ESC arm): new
`src/esc_dispatch.{c,h}` with `esc_pressed()` — title → quit; any in-game
sub-mode (free-roam, dialogue) → swallow; `g_esc_disabled` (mirror of
`DAT_06a49954`) swallows everywhere. `main.c` calls it and only quits on
`ESC_RESULT_QUIT`. Pure C, unit-tested (`test_esc_dispatch.c`, 3 cases); host
suite 3085 green, Win32 build clean.

This is **Phase A** of the plan to make ESC functional across contexts (plan
`~/.claude/plans/hidden-wiggling-snail.md`). It fixes the reported wrong-quit bug.
RE of the full subsystem — the skip-event state machine (`FUN_00453384`), the
yes/no prompt render (`FUN_00454191`, render-to-texture scene-snapshot fade), and
the pause-menu gate (`FUN_0049a585`/`FUN_0049a59e`) — is written up in
`findings/esc-skip-event.md`. Notable: the prompt also arms via DInput button bit
`0x100`, so retail goldens are capturable through the input-segtrace harness.
**Pending:** Phase B (skip-event state machine + `scene1_intro_dialogue_skip_to_
end`, the functional prologue skip) and Phase C (faithful prompt render, gated on
retail goldens + a live Frida trace of the confirm counters). PORT-DEBT: the
quit-gate overlay-suppress (`DAT_09643520/544`) is title-only until the pause menu
lands; `g_esc_disabled` has no producer yet.

## 2026-06-02 — Opening-prologue dialogue Layer 4a: the "ESC Key: Event Skip" tip

Ported the draw tail of `FUN_0046c9a2` (lines 67831-67843) — the fixed
bottom-right "ESC Key: Event Skip" tip — as `draw_skip_tip` in
`scene1_dialogue_draw.c`. It's a sub-rect of the boot-time `data_win.tga` atlas
(`DAT_073d8678`, already loaded as `g_sysassets.data_win_tga`): src
288,384..488,416 → dst 440,440 200×32. Gated `DAT_073a3e18 > 1 ∧
DAT_073a6db0 == 0`; the second flag is dead (only ever written 0 — no setter in
the corpus). Wired the gate by incrementing the runtime's `skip_prompt`
(`DAT_073a3e18`, the free-running per-tick counter `FUN_0046c320` bumps at the
top of each frame) once per `ive_runtime_step`.

**User-verified 1:1** (feed montage `Layer 4a — ESC-skip tip`): the tip text
pixel-matches retail in the `cap_00` zoom and renders correctly over both iv1_1
(bedroom) and iv1_2 (live-HOUSE) lines; the diff is clean in the tip region
(residual white = pre-existing benign — teapot filtering, FPS overlay, iv1_2
standee tween phase). The user also flagged a **separate** open follow-up: the
dialogue **box edge** is slightly off vs retail (text + tip perfect) — suspected
texture-filter mismatch on the `ive_window.tga` quad; logged in
`findings/opening-prologue.md`, not yet drilled into. Build green, host suite
3082 pass. RE writeup:
`findings/opening-prologue.md` §"ESC Key: Event Skip tip". Remaining Layer 4: the
`rmb` screen-shake RNG reads (`DAT_073a6d98/9c`; closes the foot-dust RNG-phase
front in `scene1-rng-stream-parity.md`) and the choice/menu fade overlay
(`DAT_073a6da4`; no choices in the prologue).

## 2026-06-02 — Opening-prologue dialogue RENDER ported (FUN_0046c9a2), user-verified 1:1

The deferred visual side of the prologue dialogue now renders — `FUN_0046c9a2`
ported as `src/scene1_dialogue_draw.c`, hooked into main.c's INGAME render after
the 3D scene. Built in verifiable layers (plan
`~/.claude/plans/vectorized-scribbling-backus.md`); each landed as its own commit
gated on a feed-pushed port-vs-retail pixel diff:

- **L0 scaffold** (50cafa4): `scene1_dialogue_draw` stub + render hook;
  `render_quad_add_mirrored` (FUN_00404e61); `struct ive_standee[200]` +
  `ive_scene_state_reset` (FUN_0046c0ae) on the runtime; state borrow.
- **L1 background** (99160b4): FUN_0046bf38 bg load + bgset/bgscroll; the painted
  2D bg (iv1_1 bedroom; iv1_2 = live 3D HOUSE). Bedroom matches retail 1:1.
- **L2 standees** (213c12b): chr:N:* settled-state handlers + the standee draw
  loop. Tear/Recette match retail position/pose/mirror. Fixed two latent parser
  bugs: chr:disp emitted a2=0 (→1, the active flag); chrname kept the trailing
  " W,H" (→ truncate at space, else sprite_load fails).
- **L3 box+nameplate+text** (2d69c16/9de8fac/d8dcd4e): FUN_0046c86f box wobble +
  the 4 box-position modes (prologue = mode 1) + windowpos/windowset; the speaker
  nameplate (chrname.tga grid) + next-line arrow; the glyph reveal loop
  (FUN_00405a52 truncation → reused font_draw_text, since the dialogue glyph
  scale 0.65·0.76 equals font_draw_text's built-in with the default font-size 76).
- **Fix** (85004c1): the mirrored (mode-1) box branch was missing
  `render_quad_flush` → the box quad drew with the nameplate texture bound,
  splattering the whole chrname name-sheet flipped over the scene. One-line fix.

**User-verified 1:1** vs the retail goldens (cap_00/10/20) — bg, standees, box,
nameplate, and dialogue text all match. RE writeup: `findings/opening-prologue.md`
§"the DRAW pass". Remaining (Layer 4): the "ESC Key Event Skip" tip
(`DAT_073d8678`), the next-line bubble/arrow, bg/choice fades, and the `rmb`
screen-shake LCG reads — the last of which closes the foot-dust RNG-phase front
(`findings/scene1-rng-stream-parity.md`). PORT-DEBT: animated chr tweens +
font-size/text-speed settings deferred. Host suite 3082 green.

## 2026-06-01 — Inter-script load bracket ported; `scene1_intro_events` stub retired

Closed gap #16 (the iv1_1→iv1_2 transition) structurally and retired the fake
double-load stub. RE in `findings/opening-prologue.md` §"the script-load / gate
/ transition subsystem" — the engine's `DAT_0438b1c8` gate is a 0/2/1 machine
(idle/loading/running): `FUN_0044ba2c` arms a script (gate=2 + `FUN_00452d07`
threaded load), the load thread flips 2→1, and on `end:` the pump drops to 0 +
`FUN_0044baad` arms the queued next script. Retail (Frida
`…retail-20260601T193256Z`) fires **2 LOADING brackets + 2 HOUSE_FREEROAM**:
#1 = the new-game HOUSE scene load (71→3011), #2 = the iv1_1→iv1_2 inter-script
load (4581→4649, 68 frames). iv1_1 runs under #1 with no bracket of its own.

- **`scene1_intro_dialogue.c`** gains a `D_LOAD` state between the two scripts:
  it raises `scene1_intro_dialogue_loading()` for the 68-frame bracket, which
  `main.c` ORs into `anchor_world.loading_active`. This produces LOADING_START/
  END #2 + HOUSE_FREEROAM #2 at the **faithful position** (after iv1_1's last
  line) — where the retired stub fired them ~10 frames after HF#1 (wrong place).
  PORT-DEBT: the 68 is synthetic (iv1_2's assets aren't loaded/rendered yet); the
  residual ~35 frames of gap #16 (retail 389 vs port ~354) is the deferred
  shatter/melt transition (`FUN_0045281c`/`004526f5`), which is render.
- **Retired `scene1_intro_events.{c,h}`** (the stub) — removed its arm/tick from
  `scene.c`/`sim.c`, the build entry, and rewrote `test_anchor_dialogue_double_
  house_freeroam` to drive the anchor module with the dialogue-shaped snapshots.
- **Validated** (port, `scenario-test intro-dialogue-lines --target openrecet`):
  the anchor stream is now NEW_GAME → LOADING/HF #1 → 16 iv1_1 lines → LOADING_
  START #2 @ 4089 → LOADING_END/HF #2 @ 4157 (68 f) → 30 iv1_2 lines — exactly
  retail's 2/2/46 shape. `house-movement` still reaches all 3 captures (no
  stall); its pixel diffs are pre-existing (stale local golden vs the recent
  wing-flap/motes animations — a stub build fails them identically), so the
  house-* port goldens want a re-bless, independent of this change. Host suite
  3075 green.

## 2026-06-01 — HOUSE ambient motes (`FUN_0046f621`/`FUN_0046f2a3`/`FUN_0046f648`) ported

Ported the free-roam ambient-mote subsystem as `src/scene1_motes.c`
(+ `scene1_motes.h`), retiring the `player_ctrl_prologue_churn` no-op stub.
Engine RE in `findings/engine-quirks.md` §83.

- **Sim** (`FUN_0046f2a3` + the 180× warmup `FUN_0046f621`): 6 motes
  (`DAT_005c7dd4`), each a one-axis floor drifter `x += dir·speed·0.05` that
  bounces at the room bounds (x>25 / x<−15) re-rolling depth/threshold/mode.
  Ported from objdump so the shared-LCG consumption is bit-faithful in **count
  and order**: spawn = 7 rolls (8 if the first mode roll misses), bounce = 4 (5)
  — speed + prob are not re-rolled. This is the sporadic free-roam RNG consumer
  in `findings/scene1-rng-stream-parity.md`; its position in the call order
  (controller prologue, before `FUN_0048b850`'s dust emit) is what the foot-dust
  stream depends on. The per-tick pause/counter path is **provably dead** (the
  `ecx`/threshold conditions are mutually exclusive in both drift directions) →
  reproduced verbatim so the counter stays 0 the way retail's does.
- **Render** (`FUN_0046f648`): a dark `0xff202020` shade.bmp blob per mote at
  `Scaling(−0.0046,…)·Translation(x, y+0.08, z)`, drawn inside the ground-shadow
  pass's envelope (the `FUN_00470385` @ `FUN_0045aa36` L122 slot,
  `scene1_chr_shadow.c`). The motes sit at the back-wall window line (z≈−14);
  this is the dark CONTACT pass, subtle by design — the visible bright sparkle
  is a separate unported sprite pass (the per-record anim header is a documented
  stub). Verified on-screen + correctly placed via a bright-red/10× debug build.
- **Tests:** 5 host tests (`test_scene1_motes.c`) lock the spawn/bounce RNG
  step counts, the type table, the warmup spawning all 6, single-tick drift, and
  the dead-pause invariant. Full suite 3062 green.
- **Not yet closed:** the steady per-frame dust consumer `FUN_0046c9a2` is still
  unported, so the foot-dust *phase* won't fully phase-match retail until that
  lands too (both are required — see the rng-stream doc). Retail position/stream
  cross-check via Frida is the next validation step.

## 2026-06-01 — Csh.1: HOUSE character ground shadow (`FUN_0045aa36`) — player + companion, validated vs retail

Ported the scene-1 shadow pass `FUN_0045aa36` (4493 B, `FUN_00459dfd` L205) as
`src/scene1_chr_shadow.c` (+ `scene1_chr_shadow.h`): the render-state envelope
(verbatim) + **Block A**, the player + companion (Tear) ground shadow — the only
one of the function's seven shadow tables that's live in HOUSE free-roam.
Engine RE in `findings/engine-quirks.md` §82.

- **Recipe:** the static ±256 XZ quad `DAT_0064bd88` (UVs sample the 64×64
  `shade.bmp` blob), projected onto the actor's floor plane by a
  **D3DXMatrixShadow**, grey-keyed, drawn as a 2-prim `TRIANGLESTRIP` under a
  multiplicative-darken blend (`SRCBLEND=ZERO, DESTBLEND=SRCCOLOR`).
- **Two new D3DX PSGP helpers** identified from the `FUN_004cdd9f` dispatch table
  and ported into `math3d.c`: `plane_from_point_normal` (slot 12, @0x4a4f65) and
  `mat4_shadow` (slot 27, @0x4a5c86, normalises the plane internally).
- **Geometry/colour** (objdump-recovered): `height = pos.y - floor_y`;
  `alpha = clamp((int)(height·5), 0..255)`; `size = clamp(0.038 - height·0.0015,
  .025..038)·0.14`; companion (i==2) gets `size×0.9`, `alpha+0x40`. Floor height
  + normal come from `collision_query_ground` (W4.2 port of the same
  `FUN_00432e50` the engine queries).
- **Floor-probe fix:** a grounded player (`y == floor == 0`) made the port's
  `collision_query_ground` miss (it needs clearance above the floor; the engine's
  `FUN_00432e50` hits at `y` via an internal +1.5), so the player got no shadow.
  Query the floor at `y + 1.5` (the resolver's `CR_HEAD_HEIGHT` probe); height
  still uses the true `pos.y`. Frida confirms retail draws the player shadow
  (`DAT_056daf94 = 0` hit, `py = 0`).
- **Validated:** Frida retail capture of `house-walk-tables` cap_06 — the player
  shadow under Recette matches retail closely (feed: "Csh.1 shadow VALIDATED").
  Companion shadow draws too; residual diff there is the known wing-anim phase
  jitter, not the shadow. 5 host unit tests added (`test_scene1_chr_shadow.c`),
  full suite 3057 green.
- **Dormant follow-up:** the six other shadow blocks (customers / objects /
  combat / spawn-flash) stay documented stubs. `FUN_00470385` object/furniture
  shadows (the missing table contact-shadow, `scene1-house-render-gaps.md` §4)
  need the object table modelled — the natural next shadow chip.

## 2026-06-01 — controller un-MVP Chip 4: real `cc08` dispatch + faithful `cc08==1` arm, last `FUN_0048670f` simplified debt retired

Retired the final `PORT-DEBT(simplified, FUN_0048670f)` — the cc08 dispatch
SHELL Chip 3 left open (`plans/house-controller-unmvp.md` Chip 4,
`engine-quirks.md` §78). Structural only; the controllable walk math is
unchanged (already bit-exact, §69).

- **Real dispatch:** `cc08` (`DAT_0438cc08`) is now a live module global set to
  1 (free-roam) at HOUSE entry by the ported `FUN_004850ec`
  (`player_ctrl_cc08_enter_freeroam`); the tick dispatches on it (`==1` →
  free-roam arm, else → the inert unported event/menu/dialogue arm). The setter
  omits the engine's `DAT_074b2ec4` latch reset (unported scene-exit subsystem)
  → `CALL_TRACE_ENTER_STUB`.
- **Faithful `cc08==1` arm** (`all.c:919-1225`): wraps the unchanged walk in the
  engine's guard chain — customer-approach escalation → `cc04==0` gate
  (`DAT_0438cc04`) → proximity detection → d-pad interaction → walk. All four
  guards are inert in steady free-roam (no live customer / item / interaction
  target), so control reaches the walk identically to Chip 3.
- **Not lipstick:** `test_player_ctrl_dispatch_gates_on_cc08` forces `cc08=0xf`
  and shows a held d-pad produces no movement, then `cc08←1` resumes it — the
  dispatch genuinely gates on the state. `cc08` stays 1 only because the
  transitions that would leave free-roam (customer approach → 4, counter →
  0x32, talk → cc04) are unported features (honest stubbed states), not a
  simplified body.
- **Verification:** bit-exact via a stash-and-rebuild baseline at HEAD —
  `house-walk-tables` 22/22 + `house-table-corner` 9/9 port-side goldens
  byte-identical before and after; 3051 host tests (+1); both PE exes
  warning-free. Debt 5→4 (simplified 3→2). No visual change (no `regen-
  comparisons` needed). **Next: Chip 5+** — flesh out gameplay cc08 states
  (shop counter, dialogue, menus) as each becomes reachable.

## 2026-06-01 — controller un-MVP Chip 3: the faithful `FUN_0048670f` skeleton shell

Reshaped the hand-rolled `scene1_player_ctrl_tick` into the engine's
`FUN_0048670f` outer structure (`plans/house-controller-unmvp.md` Chip 3,
`engine-quirks.md` §77). **Neutral and bit-exact** — no behaviour change, retires
no debt, adds only `stubbed` ledger entries (379→381 touched, stubbed 15→17). The
scaffold sets up Chip 4 (the real cc08 dispatch + cc08==1 arm) on a skeleton that
matches the engine instead of a flat hand-rolled tick.

- **Shape:** prologue guard `FUN_00434d6a` (reused the already-ported
  `title_save_dialog_gate_tick`; `==-1 → return`) → prologue stubs
  (`player_ctrl_prologue_churn` = `FUN_0046f621`; `player_ctrl_scene_transition_tick`
  = the `DAT_0450f470/485/488/495` fade arms + customer-spawn refresh, returns 0)
  → **cc08 dispatch shell** (`player_ctrl_cc08_freeroam_arm` = the extracted
  free-roam body, routed unconditionally — no live cc08 writer yet) → tail
  `LAB_004893ff` (room-clamp `FUN_00486435` **moved here from inside the arm** +
  `player_ctrl_tail_rumble` = `FUN_00485861`).
- **Why it's inert / bit-exact (§77):** the save-gate is BSS-zero in HOUSE (only
  the title opens it; its closing-ramp keeps it 0), `FUN_00485861` is BSS-gated
  off, and the room-clamp touches only position while the damp/anim touch
  velocity/sprite — three disjoint state sets, so relocating the clamp to the
  tail is order-independent.
- **Gotcha fixed:** the new save-gate guard exposed a host-test isolation leak —
  prior title tests leave `DAT_0438b148` set, which made the tick early-return
  (player stops walking). The 4 tick tests now `title_save_dialog_reset()` in
  setup.
- **Verification:** port-vs-port differential (bless a pre-Chip-3 golden, diff
  the post-Chip-3 build) = **house-walk-tables 22/22 byte-identical**;
  house-table-corner **9/9** vs the retail-derived golden; **3050 host tests**
  pass; both PE exes warning-free. The `FUN_0048670f` `PORT-DEBT(simplified)`
  stays open (the dispatch is still a shell) — retired in Chip 4.

## 2026-05-31 — un-MVP Step 3.4: retire the hardcoded HOUSE furniture table → live placement

Landed plan Step 3.4 (`plans/un-mvp-structural-parity.md`): `collision_house.c`
no longer hardcodes the 5 new-game-tier-0 furniture objects. `collision_house_build`
now iterates the **live ported placement** (`g_scene1_walker_phase1/phase2_pos_*`,
counts, mesh indices) that `scene1_postload_walker_phase2_init` already writes
from the real save-record furniture template — so the collision object set is
sourced from the same data the render path uses, with no duplicated table.

- **The key finding (engine-quirks §74):** the per-object collision origin arrays
  (`DAT_0438c058/0a8/0f8`, rot `DAT_0438c008`) are **the same memory** as the
  render walker's placement columns (`DAT_0438c06c/0bc/10c`, rot `DAT_0438c01c`),
  offset by exactly 5 dwords — the phase-1/phase-2 slot split. One engine write,
  two aliased readers. So "the origins equal the render placement" (§67) is
  literal, and porting the render placement *is* porting the furniture writer.
- **Mapping:** phase-1 mesh_index → stage `map[]` path (same source as
  `scene_map_meshes`); phase-2 mesh_type → `scene1_walker_draw_b_mesh_index` →
  `scene_table_filename` (`xfile/table/shop_table0N.x`).
- **Validation:** built objects are byte-identical to the old literals for HOUSE
  tier 0 (room@0, carpet@−2,0,−1, table01@−2,0,0, table02@−4,0,−8, table02@
  −10,0,−2 rot π/2 — confirmed live in a `house-movement` drive), so wall
  collision stays bit-exact (§66); 3048 host tests pass; both PE binaries build
  clean. Now generalises past tier 0 (any tier/scene_type the writer handles).
- **Debt:** retires `PORT-DEBT(synthetic-data, FUN_0044c88f)` (7→6); the
  attribution to `FUN_0044c88f` was wrong (it writes *actor* spawn positions
  `DAT_056da1dc`, not furniture origins — cf. the `FUN_0044376a` misattribution).
  The `scene1_postload.c` `FUN_00436f97` debt comment is corrected to note block-21
  (the placement writer) is ported + now consumed by collision.

## 2026-05-31 — un-MVP Step 1: PORT-DEBT registry (make the hidden debt visible)

Landed Step 1 of `plans/un-mvp-structural-parity.md`: the *other* kind of debt
the port-ledger can't see — MVP shortcuts, synthetic-data tables, simplified
state machines, and `--force-*` injections living INSIDE code the ledger already
calls "ported". They silently cap structural parity, so they now have one
canonical grep-able marker + a derived registry that can't go stale.

- **Tag convention:** `PORT-DEBT(<kind>, <engine-fn-or-NONE>): <one-line + retire
  cond>`, `<kind>` ∈ `stub | synthetic-data | simplified | hardcode | scaffold |
  force-flag`. The middle field names the engine fn that retires it (the
  migration unit — porting one fn may clear several tags).
- **Registry:** `tools/gen_port_debt.py` scans `src/` → `docs/port-debt.{md,json}`
  (grouped by kind + retiring fn), with a `--check` mode (exit 3 if stale).
  Wired into `tools/git-hooks/pre-commit` next to the port-ledger regen.
  `gen_port_ledger.py` independently re-counts the markers for a one-line STATUS
  headline (own scan — no inter-tool dependency; both are pure functions of src/).
- **First migration pass — 7 load-bearing debts tagged** (3 simplified, 2
  synthetic-data, 1 stub, 1 hardcode) across 6 retiring engine fns: the
  hand-rolled controller (`FUN_0048670f`), hardcoded HOUSE furniture origins
  (`FUN_0044c88f`) + the postload subset that should write them (`FUN_00436f97`),
  the Pass-F-only render (`FUN_004161c7`), the fx-overlay outer-gate stub
  (`FUN_00454191`), the chr-walker single-slot inject vs the Cpop populator
  (`FUN_0048b850`), and the bank-0 save-slot hardcode.

**Scope decision (deliberate, documented):** the ~50 `--force-*` mentions in
`main.c` and the unwired-allocator `--force-b-*` smoke types in
`scene1_records_b_tick.c` were **NOT** tagged. Per the user: the `--force-*`
flags are *old MVPs* — production HOUSE-with-collisions runs with **no** `--force`
anything, so they're dormant test tooling, not active parity caps. (Deleting the
now-unused force-flag scaffolding is its own future cleanup, not a structural
parity gap.) The 124 `deferred`/platform-note comments are likewise mostly
narrative, not synthetic stubs. The registry tracks what *production* fakes; it
grows as more such shortcuts are found. **3048 host tests pass** (comments +
tooling only).

## 2026-05-31 — HOUSE companion wing-glow sparkle emit ported (faithful, render-gated)

Closed §71's deferred "fairy's glowing-wing sparkle (FUN_00447f4f emit)"
PORT-DEBT — the **emit side** (engine-quirks §73). `FUN_0048a833`'s tail
(`LAB_0048b2a0`) drops one particle just off the companion every 4th frame, along
her facing — Tear's ambient wing glow.

Two spawn args Ghidra dropped were **recovered from the asm** (`objdump @
0x48b38e`): the call's `add $0x1c,%esp` = 28-byte / 7-dword cleanup vs the decomp's
4 visible args means the compiler **reused two values already pushed for the
prior `cos` helper** — `push $0x1f` → **type = 0x1f** (scene-counter-wave
particle), and the `0.1` const push → **scale = 0.1** (`param_6`). Same
optimizer-reuse pattern as §61/§69. The `.rdata` constants (0.6 offset, +1.1 Y,
2π/8 angle) were read from the PE to confirm. Position: `comp ± dir(facing·2π/8 −
camera_yaw)·0.6`, `+1.1` Y. Gate's only frame-varying term is `db054 % 4 == 0`
(the §71-validated bob counter); the other four terms (fade-done, render-scale>0,
`easydisp==0`, per-frame-override off) are all true in HOUSE free-roam.

Port: `scene1_companion_ctrl.c` `co_emit_wing_sparkle()` at the tick tail.
**Faithful but invisible today** — the spawned type-0x1f particle is ticked +
killed by `scene1_particles_tick` (`decay_drift_grav_pre`, kill age 0x20 → no slot
leak) but the table-A glow-billboard **renderer** `FUN_004176ff` (30 KB) is
unported (only `pass_f`/type-0x92 draws). It becomes visible for free once that
renderer lands; the renderer is a separate large front. **3048 host tests pass**
(+2: `companion_wing_sparkle_emit` validates type/scale/slot/position exactly;
`_period` validates the every-4th-frame rate). User chose this faithful-emit scope
over pursuing the 30 KB renderer for visible pixels.

**Also fixed a latent Pass F state-leak the emit exposed.** Populating records_a
in HOUSE flips `g_scene1_records_a_count` 0→1 (via the per-frame
`scene1_records_counter_scan`), which woke the MVP `scene1_pass_f_render` (draws
only type 0x92): it wrote its state preamble (`LIGHTING=FALSE`, texture-stage,
CULLMODE) then drew nothing → scene-wide lighting regression (`house-table-corner`
0/9, ~58k px / 7.5%, highlights shifted). Caught by an A/B against the parent
build + white-diff before relying on it. Fix: Pass F scans for a drawable 0x92
slot and returns **before any device-state write** when there is none — a
live-wired MVP must no-op cleanly. `house-table-corner` back to **9/9** with the
emit live (engine-quirks §73 "Gotcha").

## 2026-05-31 — un-MVP HOUSE chr-sheet cache → boot FUN_00472f5d party load (the roster pointer was a static-read error)

Closed §71's deferred "un-MVP the chr-sheet cache → roster loader FUN_00431a80"
PORT-DEBT — by first discovering the pointer itself was the same kind of
static-read error §71 kept hitting (engine-quirks §72).

Tracing the **only three writers** of the engine's chr-sheet table
`DAT_073a9b18[100]` settles where the HOUSE player+companion sheets actually load:
**`FUN_00472f5d`** — the boot **"read systemtex"** init (its caller logs
`read_systemtex_ok` immediately after) — runs a fixed 3-iteration loop loading
`chr00/01/02.bmp` into slots 0/1/2, the resident main party (player 0,
companion/Tear 1, guest 2). The 21-entry HOUSE customer table (`FUN_00474a9a`)
**excludes** 0/1/2, and the roster `FUN_00431a80` is **dungeon-only** — its sole
caller `FUN_00473c15` early-returns when `*DAT_068dd2f0==0` (HOUSE), so it never
runs there and cannot feed HOUSE sheets. So the player+companion sheets come from
the **boot** load, not any roster.

Port: replaced the 8-slot char-keyed LRU placeholder with the engine's 100-slot
**sheet-id-keyed** table; added `scene1_preload_chr_party_sheets()` (the
`FUN_00472f5d` slice) driven from `scene1_preload_init` (boot, device live); dropped
the `post_house_hook` hardcoded `{player,1}` loads (now boot-loaded), pose seeding
kept. chr sheets are `sprite_t` outside the mesh-tex cache HOUSE entry resets, so
the boot load survives to the first HOUSE draw (boot log: `chr00/01/02 → loaded
512x1024`).

Validation: **pixel-exact** — all 9 `house-table-corner` frames are bit-identical
between this build and the prior user-confirmed MVP build (stash A/B, compared by
capture order since the absolute frame number jitters ~300 with the load moved to
boot — anchor-relative content unchanged). So player(0)+companion(1) render
unchanged; only the load structure/timing moves to the engine's real point. **3046
host tests pass.** Local `house-table-corner` openrecet golden re-blessed (it had
gone stale across the controller/anim/companion commits; goldens are gitignored
dev artifacts, not a CI gate). Deferred PORT-DEBT: the rest of `FUN_00472f5d`
(UI/effect textures) + the 21-entry customer billboards + the dungeon roster
`FUN_00431a80` (for dungeon party/enemy sheets) remain unported, separate fronts.

## 2026-05-31 — HOUSE companion (Tear) renders + spring-follows through the real controller

Closed §70's biggest remaining HOUSE character-parity gap: the **untouched Tear
companion**. Now rendered and driven through the real engine path (engine-quirks
§71), ground-truth-first the whole way (`runs/companion-truth/`, a 25-global
retail Frida capture over the new-game→HOUSE tour).

Static reads kept being wrong; the capture inverted each: (1) the live companion
is **actor 2 / char 1** (a bobbing fairy), not actor 1 (char 3, disabled at
free-roam by FUN_00436f97); (2) its position `da1f0/f4/f8` **aliases the particle
spawn_origin** (modeled as one contiguous `g_scene1_actor_pos[3][3]`); (3) the
visible follow is the **spring helper FUN_0048a4d1** (stay 1.5 from the player,
0.15 gain, 0.35 clamp, sin Y-bob) — NOT the fixed-±1.3 hover block first
hypothesised (which fit 20× worse); (4) facing **copies the player when moving**,
**side-rule when idle**; (5) the engine draw default is **3 actors** (`local_14 =
float-bits-of-int-3`), not the port's player-only MVP of 1.

Validation: replaying retail's own player trajectory through the ported law
reproduces the companion XZ to **one-step mean 0.0036** (facing 621/621, bob band
2.806–3.197); the port's live drive obeys it to mean 0.0024 (facing 341/341, bob
2.806–3.194). User-confirmed visually (the silver-haired fairy beside Recette,
facing left toward her). **3046 host tests** (+5). New: `scene1_companion_ctrl.{c,h}`,
contiguous actor-pos array, multi-char chr-sheet cache, draw-default fix.
**Deferred:** the fairy's glowing-wing sparkle (FUN_00447f4f emit) + un-MVP the
chr-sheet cache → roster loader FUN_00431a80; plus auto-shift frame-alignment in
scenario-test for the residual ±1 load-jitter (next task).

## 2026-05-31 — W3b: HOUSE walk-cycle anim ran 1 tick ahead of retail, fixed bit-exact

Follow-up to W4.7: the `house-table-corner` cap_08 visual residual (character
looked slightly off at rel 1851 while the player **world position was bit-exact**)
turned out to be the player's **walk-animation-cycle phase**, drifting cumulatively
over the slide (char-region pixel diff grew 20.8%→25.4%→32.0% across rel
1829/1841/1851). Closed it against retail ground truth (engine-quirks §70).

`runs/w3b-anim-watch` (retail per-frame `anim/counter/frame`) gives the cycle law:
4 frames × 9 ticks, counter-driven, wrapping counter 36→1. The port's idle→walk
transition seeded `counter=0` then ran `chr_anim_tick` unconditionally — its
end-of-call `counter++` left the **seed frame at counter 1**, where retail observes
**0**. That single +1 offset persisted through every wrap, so the whole walk cycle
ran exactly **1 tick ahead** — invisible early, visibly out-of-phase by the end of
a long walk.

Fix (`scene1_player_ctrl.c`): on an idle↔walk transition, seed the new anim and
**skip `chr_anim_tick` that frame** (on a seed frame it can't advance or wrap, so
this suppresses only the unwanted `++`; internal wraps still `++` to 1). Driving
the port through the exact `w3b-anim-watch` trace, actor `counter` + cycle-`frame`
now match retail **bit-for-bit over 11097 frames, 0 mismatches** (idle + walk).
Corner cap_08 char-region diff dropped **32.0%→25.2%**, now flat with cap_06 (drift
gone); player position stays bit-exact (max |Δpos| 0.000008). The remaining ~25% is
the **untouched Tear companion sprite** + octant sprite content — separate fronts.
**3042 host tests pass** (+1 `player_ctrl_walk_anim_starts_at_counter_zero`). The
`--player-pos-log` JSONL now also carries `anim/counter/aframe/oct`.

## 2026-05-31 — W4.7 closed: HOUSE table-corner divergence was an opposing-pair d-pad, fixed bit-exact

Closed the last open HOUSE free-roam divergence (engine-quirks §69): holding a
steer through the central round table's front-left corner, the port left the
corner while retail slid around it. The standing hypothesis was "port the engine's
stored `db05c` facing slewed by an 8-way/sticky law" — **disproved**.

Ground-truth-first, **no new retail capture needed**: reconstructed retail's
per-frame impulse heading from the existing `golden-retail/watch.jsonl` velocities.
The HOUSE velocity recurrence `V_n = 0.82·clamp₀.₁₇₅(V_{n-1} + 0.1·dir(d_n))` has
clamp+damp both direction-preserving, so the heading `d_n` is recoverable by
solving `atan2(V_{n-1}+0.1·dir(d)) = θ(V_n)` per frame
(`tools/facing_reconstruct.py`, new). Retail's heading equals raw `atan2(dpad)` on
**every frame but one**:

- The entire divergence is **one frame (rel 1822)**, where the recorded input is
  `0x0b` = **LEFT+RIGHT+DOWN** — the human held both L and R for a frame while
  rolling from down-left to down-right. The port's `atan2` cancels L+R → straight
  DOWN and **snaps** facing; retail's velocity at 1822 is **byte-identical to
  1821** (`0.14350 @ −45°`) — it discarded the conflicting frame and kept walking
  the held heading. The visible multi-frame drift downstream is just the momentum
  accumulator rotating after that single bad frame.
- **Root cause: opposing-pair rejection.** `db05c` is plain `atan2(dpad)` (written
  through the player-struct pointer, like the velocity in §61 — no literal write
  exists to grep), with one correction: when L&R or U&D are both held the engine
  ignores the frame's d-pad and **repeats the previous facing + moving state**.
  Dormant for cardinals/valid-diagonals, so the W3 walks + wall slide can't regress.

Fix: `player_ctrl_dpad_intent()` (`scene1_player_ctrl.c`), driven from the tick.
Port↔retail now **bit-exact** across the whole corner (rel 1805–1851: max |Δθ|
0.0°, max |Δpos| 0.00000); `house-table-corner` port golden blessed as a permanent
guard. **3041 host tests pass** (+1). The `--player-pos-log` JSONL now also carries
`vx/vz/facing/sticky/buttons` for future facing analysis. Remaining post-corner
`house-walk-tables` drift is the **unported cc08 event-gate** (§60), a separate
known limitation, not a controller-physics bug.

## 2026-05-31 — W4.3 closed: wall collision proven physically identical (per-frame)

Validation pass on the W4.3 follow-up ("standoff tuning" — the scenario.yaml +
ledger still said the resolver pinned px~1.55, ~0.6 short at the counter row).
That figure was **pre-fix** (old radial-push resolver); the committed `pos.jsonl`
that fed it predated commit ee37235 by 18 min. Regenerated a fresh port drive
against the current build and diffed per-frame vs the retail watch ground truth
(`runs/wall-retail`):

- **No gap remains.** Endpoint is bit-exact (port (3.1034, 0.6837) == retail).
  More importantly the *whole trajectory* matches: `tools/wall_collide_diff.py`
  now does a ±3-frame **anchor-phase search**, and at **shift +1** the residual
  is **RMS Δpx = 0.0000, max|Δpx| = 0.0000, max|Δpz| = 0.0000** over all 2547
  shared frames — `port[rel] == retail[rel+1]` exactly. The shift-0 "max|Δpx|=0.175"
  is purely a **one-frame anchor-phase offset** (load-frame-count jitter, the known
  determinism leak — sim is bit-exact), not a collision error.
- The counter-row contour point (px≈2.15 @ pz≈9.27) **is** hit transiently during
  the slide and matches retail — the counter jut is reproduced 1:1, not just the
  px=3.10 front section. The whole "right wall is a contour, port stops short"
  premise is resolved.
- **Tool hardening:** `wall_collide_diff.py` gained the `--shift-window` search +
  a verdict line ("physically identical at shift +1 … NOT a collision-accuracy
  gap" vs "residual persists at every shift → real divergence"), so anchor jitter
  can't masquerade as a gap in future runs. Stale "0.6 short / open follow-up"
  language corrected in scenario.yaml, the contour comment, and engine-quirks §66.

## 2026-05-31 — W4.3 1:1: HOUSE wall collision bit-exact vs retail

Took the room-wall collision from "blocks but ~0.6 short" to **1:1 with retail**,
ground-truth-first (retail Frida call-graph + watch, not eyeballing). Two
Ghidra-dropped details in `FUN_00483170`'s radial push, both confirmed against
`runs/wall-retail` (engine-quirks §66):

- **20 rays, not 8.** Ray count is 8, or **20 when `*DAT_068dd2f0` (stage-palette
  mode) == 0 AND pz > 0.7** — HOUSE at the back of the room. Extra 12 rays at
  stacked heights + a 1.03 cos scale. Confirmed by the retail call graph:
  `FUN_00433674` fires **exactly 20×/frame** per resolver call (call-trace probe
  over `traces/house_wall_probe.jsonl`).
- **Penetration-scaled push.** Ghidra showed `px -= sin·1.0`; the asm at 0x483bc3
  is `px -= (1 − frac)·dir` (frac = raycast hit fraction). A full-vector push
  bounces the player ~1 unit off the wall every frame (oscillates 2.2↔3.1); the
  `(1−frac)` push lands it **exactly against the wall** — Δpx cancels the
  into-wall velocity to the digit. Found by disassembling the push site after the
  decompile + a per-ray host probe (`tests/probe_wall_rays.c`).

Result: holding RIGHT from the back-right corner the player slides −z down the
wall at a constant **px=3.1019** (retail: 3.1019 dead constant), settling at
(3.103, 0.684) — bit-identical px frame-for-frame, pz within one slide-frame
(intro-onset phase). `collision_resolve_player` now takes the palette mode;
3040 host tests pass. Validated via `tools/wall_collide_diff.py` + the
`house-wall-collide --target both` amplified port|retail comparison (user
confirmed the only visible diff is the Tear companion sprite layered over
Recette — unrelated to collision). Scenario runs now default to `--turbo
--silent-audio`.

## 2026-05-31 — W4.3 LIVE: room collision wired into the player walk (walls block)

Fixed the reported bug — **holding RIGHT walked the player straight through the
shop's right wall** (and the left wall was open below pz=7, with a teleport-snap
in the pz>7 corner). Root cause: the live walk only ran the crude 2-line
`player_ctrl_house_room_clamp` (left wall above pz=7 + pz≤9.5); the real mesh
resolver was host-test-only, never wired in.

- **Room collision mesh built live.** `src/collision_house.{c,h}` parses
  `shop_1st.x` at HOUSE entry (`scene1_preload`, right after the map-mesh load)
  → `collision_object_build(COLLISION_PAD_SMALL)` → 1909 tris (matches the W4.1
  self-validation exactly; world x[−43,45] z[−40.4,10.6]). Needed a new
  `mesh_load_parse_xfile` (mesh_load.c) because the render `mesh_t` drops the raw
  geometry + material names collision needs — so collision re-parses the same
  `.x` bytes through the same storage path.
- **Resolver wired into the tick.** `scene1_player_ctrl_tick` now runs
  `collision_resolve_player` (FUN_00483170 radial push) when the mesh is built,
  replacing the clamp (kept as a no-mesh fallback). **Result: px 41.5 → 1.55**
  (blocked, no passthrough, no counter-climb). 3040 host tests pass (+5).
- **Tried the faithful floor-edge try-move** (`collision_resolve_player_floor`,
  FUN_004830f1 = ground query at the destination, §64). It reproduces the engine
  model but **let the player climb onto the counter top** (a valid floor
  triangle: py 0→2.21, px→4.1) — retail's ground query gates step-height, which
  isn't ported. So the radial push is the right model for now; the floor fn is
  kept (documented) for the combined try-move + step-gate the tuning pass needs.
- **Open accuracy gap:** radial-push standoff pins px≈1.55 vs retail **2.15** at
  the counter row (Δ≈−0.6; the wall is a contour, not a plane: 2.15@pz9.23,
  2.29@pz4.51, 3.10@pz−0.65). Blocks correctly but stops short. Tuning to
  perfection vs retail is the tracked follow-up.

**Tooling landed alongside (so the tuning is measurable + visible):**
- `--player-pos-log <file>` — port-side per-frame px/py/pz JSONL (the port's
  `--watch`). `traces/house_wall_repro.jsonl` is the repro drive.
- `tests/scenarios/house-wall-collide/` — deterministic walk-into-wall scenario
  (blessed port golden) + `tools/wall_collide_diff.py` (rebases port pos.jsonl vs
  retail watch.jsonl on the HOUSE_FREEROAM anchor; reports px/pz Δ). Retail
  capture recipe is in the scenario.yaml.
- **Feed auto-push:** any `--capture-to` run now montages+pushes to the feed
  (PostToolUse hook → `tools/feed_push_run.py`); `scenario-test --target both`
  auto-pushes the amplified port|retail `comparison` (`tools/push_comparison.py`).
  Idempotent + best-effort. See memory `feedback_capture_autopush`.

## 2026-05-31 — W4.3 (WIP): slide-resolver raycast + radial push; BLOCKED on furniture placement

Ported the player slice of the slide-resolver as host-tested scaffolding
(`5f0f084`). `src/collision_resolve.{c,h}`: `collision_raycast` (ray-vs-mesh,
`FUN_00433674`) + `collision_resolve_player` (integrate + the 8-ray radial push
`FUN_00483170` L207-247 + ground snap). HOUSE has no type-1/2 wall triangles, so
the engine's atan2 wall-slide never fires — blocking is the radial push's
~1-unit standoff (rays hit vertical type-0 faces and push the player out).
Validated: synthetic raycast (exact) + the real `shop_1st.x` room wall pins the
player (px≈2.5; retail 2.29, standoff tuning pending).

**BLOCKED (engine-quirks §65): furniture world-placement is unported.** Driving
the resolver against the `runs/w4-table3` table-hit revealed the furniture
meshes (`shop_table01/02.x`, `shop_jihanki*.x`) parse to geometry at their
**local origin** (AABB ±2.5) — they do NOT self-place like the room mesh
(`shop_1st.x`, whose `.x` frames carry the world translations). Their world
positions live in the engine's per-object origin table `DAT_0438c058`, populated
by `FUN_00436f97` (block 21) from a `stage_positions` source that is still
unported (the render path fakes it via `--force-walker-phase2`, main.c:285). So
the round-table block can't be positioned yet (that test is skipped), and the
resolver is **not wired into the live player tick** — wiring it now would block
walls but let the player walk through the table, a visible partial state.

**Next chip:** port the furniture world-placement (`FUN_00436f97` stage_positions
→ `DAT_0438c058`) — unblocks BOTH the faked furniture render positions AND W4.3
furniture collision. Then wire the resolver into `scene1_player_ctrl_tick`
(replacing the `player_ctrl_house_room_clamp` seam at scene1_player_ctrl.c:476)
and tune the standoff to the w4-table3 / w4-collide trajectories (px/pz to ~1e-3).

## 2026-05-31 — W4.1+W4.2: HOUSE collision mesh ingestion + ground query ported

Built the faithful foundation of the W4 collision subsystem (the geometry +
query halves; the slide-resolver W4.3 is next). Ground-truth-first throughout.

- **W4.1 — collision mesh ingestion (`047d1bc`).** `src/collision_mesh.{c,h}`
  builds the per-object triangle collision mesh the engine queries. Key RE
  (engine-quirks §63): collision geometry is parsed from the **render `.x`**
  (no `_s.x` ships for HOUSE; `FUN_00472836` falls back to the base file), each
  face's collision **type comes from its referenced material name**
  (`FUN_00471d45` keyword chain, decoded from the unpacked exe: `Plane`→2,
  `kabe`→7, `nohit`→4-and-dropped, … the shop is all type 0/4). The vertex pool
  is ×0.2-scaled; the per-triangle record (`FUN_00432ac6`: plane eq, padded
  AABB, edges) **negates X** into the player/world coordinate space. We reuse
  the oracle-validated `xfile` parser and replicate only the transform/scale/
  classify/build — the 2777-byte `.x` text state machine is not re-ported.
  Self-validation: building the real `shop_1st.x` yields 1909 triangles spanning
  world `z[−40.4,10.6]` with the counter/back-wall edge at z≈10.6 (matches
  retail §62) and floor at y≈0.
- **W4.2 — ground query (`19bea6c`).** `src/collision_query.{c,h}` ports
  `FUN_00432e50`: for a world point, find the highest floor triangle under it
  (above-plane gate + XZ point-in-triangle + ground-height solve, within 5u
  below), returning height + surface normal. Type exclusion {7,8..16}; worldmap
  grid/tiling + dynamic props skipped (HOUSE-gated off). Key model
  (engine-quirks §64): **HOUSE walls are implicit** — the floor mesh ends at
  them, so an off-floor probe is the block signal; there are no participating
  vertical wall triangles. Vendor self-check: query the real shop floor at the
  room origin → `hit, height=0.000, normal=(0,1,0)`; off-room → miss.
- **Tests:** 16 new host tests (exact synthetic plane/AABB/query math + vendor
  self-consistency on `shop_1st.x`). 3035 pass; both exes build clean.
- **W4.3 (next) is the slide-resolver `FUN_00483170`.** Replaying the captured
  `runs/w4-table3` drive pins the retail ground truth: right-wall slide pins
  `px=2.2935` (X blocked, Z free), round-table head-on freezes `px=0.7286,
  pz=0.1067` while vx keeps pushing. Re-reading the resolver against this:
  HOUSE has no type-1/2 triangles so the atan2 wall-slide loop never fires —
  blocking is **try-move probe** (off-floor) + **radial furniture push**
  (`FUN_00433674`, the round-table stop). W4.3 must port both (player/actor-0
  slice only; enemy + companion loops stubbed) and resolve the Ghidra-dropped
  `FUN_004830f1` args, validated by replaying the w4 traces to match px/pz.

## 2026-05-31 — W4 scoping: HOUSE collision is a full mesh subsystem + `{wait_until}` TAS op

Scoped W4 (collision + companion) ground-truth-first before committing to a
port. Two findings reshaped the front:

- **Companion is moot at this game stage.** Retail renders only Recette at first
  HOUSE free-roam (verified from the `w3-walk-watch` montage); the actor-1/2
  slots aren't drawn yet, so there's no companion to port.
- **Collision is a full triangle-mesh subsystem, not a few AABBs**
  (engine-quirks §62). Drove retail into the counter / walls / central round
  table and `--watch`'d `px/pz/vx/vz`: furniture blocks the player everywhere
  (counter at `pz=8.941`), the room is large (`pz` 9.35…−7.27), and the response
  is position-block + **velocity slide** along the surface — walking LEFT into
  the round table slides the player *around its circular edge* (`px 0.69→−0.67`
  as `pz` climbs), which only a real mesh produces. The port walks through all
  of it (the ~3 MB/level `DAT_007ca434` collision mesh isn't loaded; `FUN_00432e50`
  query + `FUN_00483170` slide-resolve are unported). So real parity needs the
  subsystem (mesh loader → query → slide-resolve), not a per-room approximation.

- **New TAS primitive `{wait_until}`** (`5b6a755`, tas-framework P3b) — a
  segtrace segment can break on a live-global predicate instead of an anchor
  (`{"wait_until":{"va":"0x056da1e0","op":"<=","val":2.0}}` = hold UP until
  `pz≤2.0`), removing frame-count guessing for movement drives. Authored the
  canonical collision ground-truth traces with it: `traces/house_collide.jsonl`
  (multi-dir sweep) + `traces/house_table_collide.jsonl` (clean head-on
  round-table hit). agent.js + frida_capture.py.

## 2026-05-31 — W3: HOUSE free-roam walk movement ported (ground-truth-validated)

Made Recette walk in the HOUSE shop. Followed the **ground-truth-first**
approach: drove retail past the two new-game intro events via the
anchor-segmented TAS segtrace and `--watch`'d 15 movement globals while holding
LEFT (`runs/w3-walk-watch`), then decoded the per-frame physics and ported it.

- **Movement model (engine-quirks §61).** Per controllable frame: velocity
  impulse `v += (sin,cos)(db05c)·0.1` from the d-pad facing angle, clamp
  `|v| ≤ 0.175` (b850 `local_8`), integrate the player position, room-bounds
  clamp (`FUN_00486435`: px≥−1.5 / pz≤9.5), damp `v *= 0.82`. The facing octant
  is the objdump-decoded b850 ftol `(int)((db05c+camyaw+π/8)·8/2π+8)&7` with the
  HOUSE camera yaw `DAT_073de39c = −π`. The arithmetic reproduces the retail
  per-frame `px`/`vx` watch to 1e-4 (incl. the wall clamp + the 0.82 release tail).
- **Key RE correction.** The walk velocity is written **through the player
  struct** (`*(float*)(player+0x904)`), so it never appears as `DAT_056daabc =`
  in the decomp — which is why §60's "`FUN_0048b850` sets the velocity" was
  imprecise (b850's only sin/cos accumulate is the `da1bc`-gated stun/hop path,
  speed 0.3, not the 0.1 walk). Lesson logged: grep the struct offset, not the
  `DAT_` alias, for actor state.
- **Port (`src/scene1_player_ctrl.c`).** `scene1_player_ctrl_tick` is no longer a
  stub: pure leaves `player_ctrl_dpad_angle` / `_facing_octant` /
  `_house_room_clamp` + the impulse→clamp→integrate→damp tick (engine order),
  driving `g_scene1_player_pos` + the actor record (anim id + facing octant;
  walk cycle via the already-ported `chr_anim_tick`). 6 new host tests incl. the
  LEFT-trajectory replay vs retail. 3024 host tests pass; both exes build clean.
- **Deferred:** W3b (walk-cycle *frame* timing vs a retail record capture) and
  W4 (furniture/mesh collision `FUN_00483170`, companion, real `cc08` gate).
- **Camera follow (4fd5ef7).** With the player now moving, the HOUSE camera
  panned wrong: `scene1_camera_pose_compute` read the target bias from a static
  seed (the old `apply_house_groundtruth` stand-in) instead of the live player
  pos. The engine reads `DAT_056da1d8/e0` = `g_scene1_player_pos[0]/[2]` each
  frame (clamped to the room), so the camera follows. Switched to the live read
  + removed the stand-in; new `scene1_camera_follows_walking_player` test.
  User-verified 1:1 vs retail.

Also removed the capture/comparison tools' Windows image-viewer auto-open
(`explorer.exe`) now that visuals go to the llm-feed push server (`cf5f5af`).

## 2026-05-30 — TAS scenarios on segtrace + interactive diff gallery + port double-HF stub

Refactored the scenario harness off absolute-frame replay onto the
**anchor-segmented** TAS primitives this branch built, added a HOUSE-movement
scenario, and made the comparison gallery interactive (planning doc since
superseded — see docs/trace-workflow.md). Validated end-to-end **port + retail**:
both sides capture 3/3 anchor-relative frames in actual HOUSE free-roam.

- **Port double-`HOUSE_FREEROAM` stub** (`src/scene1_intro_events.c`) — retail's
  new-game intro runs two scripted events each with its own load, so
  `HOUSE_FREEROAM` fires *twice* before the player can move (engine-quirks §55);
  the port reached HOUSE in one load and fired it once, stalling any segtrace's
  second `wait HOUSE_FREEROAM`. A 4-state frame counter armed from
  `scene_post_fade_init` injects a second load-gate cycle (raise/hold/drop via
  `worker_load_begin/end`), so the port fires it twice too. Live:
  `HF#1@1570 → LS#2@1577 → HF#2@1581`. New host test.
- **Scenarios on segtrace** (`tools/scenario-test.py`) — auto-detects a segtrace
  trace, drives the port with `--input-segtrace` (captures come from the
  trace's `{capture}` ops), keys goldens by **capture order** (`cap_NN.bmp`).
  Retail path threads `input_segtrace_path` through `frida_capture.run_capture`.
  `cap_NN` are **bit-exact across runs** despite load-frame jitter. Fixed two
  capture bugs surfaced by the first real both-run: the port grabbed a spurious
  frame-0 via the wall-clock sampler before its first scheduled capture
  (main.c), and the retail agent inherited the `[0,30,60]` `capture_frames`
  default (segtrace scenarios now force it empty).
- **Interactive gallery** (`tools/comparison_page.py`, factored from
  `regen-comparisons.py` + `pixel_diff.amplified_diff`) — one atlas PNG per
  capture: row 0 `[openrecet | retail]` always visible, row 1 the amplified
  diff (black = bit-identical) revealed on click; right-click → Copy Image
  yields the 3-up. `scenario-test --target both` auto-rebuilds + opens it.
- **`tests/scenarios/house-movement/`** — new-game → HOUSE → walk-left segtrace
  (cloned from the validated `traces/house_walk.jsonl`): A-spam clears the
  intro + tutorial, then captures idle / walking-left / walking off the 2nd
  `HOUSE_FREEROAM` (+1540/1640/1900, i.e. *after* the tutorial dialog —
  engine-quirks §55). Port player frozen (controller unported) → the walking
  caps are the intended port-vs-retail parity baseline until `FUN_0048b850`.

3018 host tests pass; both exes build clean.

## 2026-05-30 — W1: player-controller tick wired live (FUN_0048670f entry, stub body)

First step of the **movement-first** plan (docs/plans, approved this session):
get the HOUSE player to walk + animate by porting `FUN_0048670f` (the 11.5 KB
input→movement→anim driver), with the done Cpop leaves of `FUN_0048b850` (its
camera/effects sub-controller) wiring in afterward.

Investigation reframed the controller architecture: `FUN_0048b850` (Cpop, leaves
done) is the *effects* sub-controller — wiring it produces no visible change.
The actual walking/animation lives in its caller `FUN_0048670f`, which the
engine runs FIRST in the default sim arm (`FUN_00442cef` L40595-40598, before
the records-B tick).

- **`scene1_player_ctrl_tick()`** (`src/scene1_player_ctrl.c`) — the
  `FUN_0048670f` entry, wired into `scene1_ingame_default_arm_tick()`
  (`src/scene1_sim.c`) **before** `scene1_records_b_tick()` to match the engine
  order. Stub body (`CALL_TRACE_ENTER_STUB(0x48670f)`) so call-count parity
  surfaces it as incomplete; the actor pose stays as seeded by
  `player_ctrl_pose_house_standing()` until W2 (free-roam movement) / W3 (walk
  animation) land.
- **1 baseline host test** (`test_player_ctrl_tick_is_pose_preserving_stub`)
  locks in "the stub doesn't disturb the seeded pose" — flips to a real
  movement assertion in W2. 3010 total pass; both exes build warning-free.

## 2026-05-30 — Cpop.8: FUN_0048b6ad HP/SP gauge tween — first controller callee

With the after-image banks done (Cpop.6/7), `FUN_0048b850`'s pure-leaf vein is
mined out, so this chip takes its **first callee** — `FUN_0048b6ad` (407 B),
run at the controller's top — which is itself a clean leaf. It's the on-screen
HP/SP bar follower: each frame it eases two displayed gauges
(`DAT_056db0c4`/`db0c8`) toward their true values (`db0bc` = player HP /
`db0c0` = SP) at a per-character rate, so the bar slides a few frames after a
hit instead of snapping.

- **Port (`src/scene1_player_ctrl.c`).** `player_ctrl_gauge_track` (the two
  asymmetric channels) + `player_ctrl_gauge_rate` (the `(i16+i16)*0.01`
  derivation, `.rdata 0x5193a4`). The HP channel tracks a run-length counter
  (`db0cc`) and direction flag (`db0d0`: 1 = heal, 0 = damage); the SP channel
  is clamp-only. objdump-verified `0x48b6ad-0x48b843` — the `jae`/`jbe` branch
  order makes the equal frame a *third* outcome that resets the counter and
  leaves the direction untouched (engine-quirks §59).
- **Semantics resolved without Frida.** `db0bc` was already named player HP
  (records-b-state-machine.md Q2) and `stage_post_load.h` step 4 already
  documents the `db0bc/db0c0 → db0c4/db0c8` target→follower seed — so the
  gauge-tween reading fell straight out of the existing finding corpus.
- **5 host tests** (`test_player_gauge_*`); 3009 total pass; both exe targets
  build warning-free. char-sprite-render finding + engine-quirks §59 updated;
  the remaining `FUN_0048b850` pieces (intro spawn SM, proximity grid, dust
  spawns) need the live-record callees first — noted in the finding.

## 2026-05-30 — TAS P2 (retail side): anchor-relative capture; README hero regenerated anchor-aligned; HOUSE_FREEROAM double-fire found

Built the retail half of TAS P2 (`docs/plans/tas-framework.md`): the Frida
driver can now capture **relative to a named anchor** instead of a fragile
absolute frame, symmetric with the port's `--capture-at-anchor` — so one
anchor spec captures both targets at the same semantic instant despite the
load jitter.

- **Agent (`tools/frida/openrecet-agent.js`).** New `anchorCaptureSchedule()`
  runs after each anchor emit in `anchorTick(frame, devicePtr)`: resolves
  every matching `{name, offset}` request to `frame + offset` — offset 0
  captures the anchor frame *immediately* (we're in Present.onEnter pre-flip,
  the same sample point as the normal path), future offsets queue into the
  existing `g_capture_pending` set, past offsets drop. A 1:1 port of
  `src/main.c::anchor_capture_schedule()`. The agent self-shuts-down via a new
  `capture_at_anchor_done` once every *distinct* requested anchor has fired
  **and** every resolved target has been captured (waiting on the unfired-name
  set, not just pending, so offset-0 / multi-anchor specs settle correctly).
- **Driver (`tools/frida_capture.py`).** `--capture-at-anchor NAME[+k]`
  (repeatable; `parse_anchor_spec()` splits on the first +/- like the port),
  `CaptureConfig.capture_at_anchor`, init plumb (forces the anchor poll on),
  a `capture_at_anchor_done` handler → `done.set()`, and the `anchors.jsonl`
  sink now also opens for capture-at-anchor runs.
- **Dogfood — README hero regenerated anchor-aligned.** Drove retail
  (`--auto-z-spam --capture-at-anchor HOUSE_FREEROAM+…`, 1024×768, ~1 s under
  turbo) and the port (`run-openrecet.sh --auto-z-spam --capture-at-anchor
  HOUSE_FREEROAM+300`) to the HOUSE shop and rebuilt
  `docs/img/house-comparison.png` as an anchor-aligned port|retail montage —
  replacing the old hand-picked absolute-frame-3300 pairing. New reusable
  composer `tools/compose_comparison.py` (real bold TTF resolved via fc-match,
  **errors instead of silently shipping PIL's tiny bitmap default**; big
  `--font-size 44` by default).
- **Finding — `HOUSE_FREEROAM` fires twice on retail (engine-quirks §55).**
  The sweep showed retail's `INGAME && !loading` edge rising at the *intro
  bedroom event* (~3041), then again (~4588) after the intro→shop transition
  runs **its own loading screen**; the playable top-down shop is the 2nd
  firing + ~1500 frames (A-spam clearing tutorial dialog). The **port has no
  intro** so it fires **once** (~1533) straight into the shop. Same anchor
  name, genuinely different sub-state — the design-doc's "anchor as a
  correctness signal" case. A future precise shop anchor (records-B
  `count_b>0`) would name the playable instant directly on both sides.

**Next (P2 cont.):** declarative `scenario.yaml` `anchors:`/`capture:`
section so scenarios express captures by anchor instead of plumbing the flag
by hand (threads through `scenario-test.py` for both targets).

## 2026-05-30 — TAS P1 (retail side): Frida anchor emitter; one trace, two targets, names align + load divergence localised

Built the retail half of TAS P1 (`docs/plans/tas-framework.md`): the
Frida agent now emits the **same named anchors** as the port's
`src/anchor_trace.c`, so one input trace drives both targets and the
harness aligns them by event instead of by absolute frame.

- **Agent (`tools/frida/openrecet-agent.js`).** New `anchorTick(frame)`
  is a 1:1 mirror of `anchor_trace.c`'s edge logic — samples the engine
  scene-state (`DAT_0438b1c0`) and the two loading gates
  (`DAT_06a49958 || DAT_06a49960`, the OR the engine itself tests at
  all.c L50058) once per Present and emits `{kind:"anchor",anchor:NAME,
  frame:N}` on rising edges. Same names, same wire shape, same causal
  table order (BOOT seeded on first tick; then NEW_GAME / LOADING_START /
  LOADING_END / HOUSE_FREEROAM). Read-only poll, rides the existing
  Present hook; gated on `config.anchor_trace`. Frame is the agent's
  manual per-Present counter (retail's canonical frame numbering).
- **Driver (`tools/frida_capture.py`).** `--anchor-trace` flag +
  `CaptureConfig.anchor_trace`; the `kind:"anchor"` handler appends to
  `<run_dir>/anchors.jsonl` (mirrors trace.jsonl/audio.jsonl). Pairs
  with `--auto-z-spam` to drive a fresh new-game→HOUSE unattended.
- **Validation — one trace, both targets.** Drove retail
  (`--turbo --silent-audio --hide-window --auto-z-spam --anchor-trace`,
  riding `--dump-records-b` for a bounded auto-shutdown at HOUSE). The
  recorded A-spam `trace.jsonl` was then replayed on the **port**
  (`run-openrecet.sh --input-trace-replay … --anchor-trace-record …`).
  Result:

  | anchor          | retail | port  | gap   |
  |-----------------|-------:|------:|------:|
  | BOOT            |      0 |     0 |     0 |
  | NEW_GAME        |     59 |    59 | **0** |
  | LOADING_START   |     59 |    59 | **0** |
  | LOADING_END     |   3018 |  1748 | +1270 |
  | HOUSE_FREEROAM  |   3018 |  1748 | +1270 |

  The deterministic title→new-game prefix aligns **exactly** (frame 59
  on both — the same trace produces the same commit frame, confirming
  the sim advances one step/frame identically on each side), and the
  divergence is localised **precisely to the load** (+1270 frames) —
  exactly the design-principle prediction: anchor-frame gap = the
  non-deterministic loading duration, which absolute framing can't
  absorb but anchors do. Retail genuinely reached free-roam (records-B
  dump: `player_pos [-0.30,0,9.35]` = the canonical standing pose, 8
  live entity records).
- **Anchor precision note.** Retail's *first 3D draw* (frame 2988)
  precedes *HOUSE_FREEROAM* (3018) by 30 frames — 3D draws fire behind
  the still-raised loading overlay. So the load-free `HOUSE_FREEROAM`
  anchor is a strictly more precise "playable HOUSE" marker than the
  ad-hoc `auto_3d_trace` "first DrawIndexedPrimitive" trigger it's
  slated to subsume.

**Next (P2 retail):** resolve `anchor+offset → frame` from the Frida
anchor stream in `frida_capture.py` (retail-side `--capture-at-anchor`,
mirroring the port flag) + a declarative `scenario.yaml`
`anchors:`/`capture:` section, then re-run a render-parity case
cross-target at `HOUSE_FREEROAM + k` and confirm the room aligns.

## 2026-05-30 — TAS P1 (port side): anchor emission + anchor-relative capture; loading-screen non-determinism characterised

Built the port half of TAS P1 (`docs/plans/tas-framework.md`):
deterministic *event* anchors that let the harness align port↔retail (and
run↔run) when absolute frame numbers can't.

- **Determinism, characterised.** The port *sim* is already bit-exact
  given the same inputs — under `--input-trace-replay` the clock is a pure
  virtual counter (one tick/loop, no QPC/Sleep) and `--rng-seed` pins the
  LCG. Verified: a trace double-replayed on the port is **byte-identical**
  (sha256-equal BMPs) at both title and HOUSE frames, with **no new
  pinning work**. The RNG seed is currently a no-op on *visible* output
  for title + HOUSE-freeroam (pure functions of input); it matters for
  later RNG-driven content (wiring already pinnable).
- **The real leak: the loading screen's frame count.** The new-game→HOUSE
  asset load is worker-thread gated (`src/worker_load.c` `CreateThread`);
  the main loop spins `nowloading` frames until `worker_load_busy()`
  drops, and under the turbo virtual clock the few ms of thread
  spawn/teardown maps to a *variable* frame count. Measured `LOADING_END`
  across identical replays: **1489 / 1519 / 1566 / 1594 / 1613 / 1752** —
  ~250-frame jitter. So absolute frames can't align two runs; this is
  precisely why anchors exist. (A threaded load with variable duration is
  normal engine behaviour — not an engine-quirk; the parity *implication*
  is what matters, captured here + in the plan.)
- **`src/anchor_trace.{c,h}`** (pure, 6 unit tests, ASan-clean): per-frame
  world snapshot (`scene_state`, `loading_active`) → rising-edge anchors
  `{"anchor":NAME,"frame":N}` JSONL. Anchors: `BOOT`, `NEW_GAME`
  (TITLE→INGAME), `LOADING_START`/`LOADING_END` (nowloading edges),
  `HOUSE_FREEROAM`. Wired into `main.c` `render_dispatch`; `stderr` echo +
  optional `--anchor-trace-record <file>` (run-openrecet path-translated).
  Keyed on `g_tick.frame_count` == the `--capture-frames` index.
- **`--capture-at-anchor NAME[+k]`** — anchor-relative capture resolved
  live when the anchor fires, immune to the jitter. **Proof:** two
  identical replays, `HOUSE_FREEROAM` at frame 1613 vs 1752 (+139 jitter),
  yet `HOUSE_FREEROAM+0` and `+30` captures were **bit-identical** content
  across both — absolute framing would have caught two different states.
  Also fixed a latent bug: anchor-only capture (count 0 until the anchor
  fires) used to fall into the legacy `--capture-every-ms` sampler and
  grab a spurious `frame_00000.bmp`; listed-mode now triggers on pending
  anchor captures too. **2981/2981** host tests pass.
- **Design targets recorded** (from this session's direction): the
  *anchor-segmented trace timeline* (frames counted per-segment, `wait
  ANCHOR` gaps span non-deterministic loads with zero counted frames) and
  *one harness retiring the ad-hoc capture flags* (`--capture-frames`,
  `--capture-every-ms`, `--auto-z-spam`, the Frida `auto_3d_trace`/`dump_b`
  modes all fold into named anchors). See tas-framework.md.

**Next (P1/P2 retail side):** Frida agent `kind:"anchor"` emitting the
same names off the retail globals + clock/RNG pins, then anchor-relative
capture in `frida_capture.py` and a `scenario.yaml anchors:`/`capture:`
section.

## 2026-05-30 — TAS P0 papercuts: input-trace table un-capped + run-script path translation

Cleared both P0 papercuts in `docs/plans/tas-framework.md` — the two things
that bit the texture-filtering session and block scaling the input-trace
replay toward a full-game TAS run.

- **Un-capped the replay table (`src/input_trace.{c,h}`).** The table was a
  fixed `entries[4096]` array, so any longer trace silently failed the
  *whole* load (`input_trace_parse_buf` → 0 → main.c "replay disabled"); an
  8256-entry trace was the original bite. Now `struct input_trace` carries a
  heap `entries` pointer grown by doubling `realloc` (seeded 256), released
  by a new `input_trace_free`. `input_trace_load` also slurps the file into
  a growing heap buffer — removing the *second* silent cap (the old static
  1 MiB read buffer). `INPUT_TRACE_MAX_ENTRIES` is now only a 16 M-entry
  **sanity ceiling** that fails loudly on `stderr`, far past a full-game run
  (~1 M transitions). main.c frees `g_replay_trace` at shutdown + on the
  load-failure path.
- **Tests.** Rewrote the two lookup tests that poked the (now-heap) array
  directly to build via `parse_buf`; added `input_trace_free` to every
  allocating test (ASan-clean); new regression
  `input_trace_parse_grows_past_old_fixed_cap` builds a 9000-entry trace
  (> the old 4096) and round-trips it. **2975/2975** host tests pass.
- **`tools/run-openrecet.sh` path translation.** Generalised the
  `--capture-to` path-rewrite into a `rewrite_path` helper covering three
  path kinds and now also `--input-trace-replay` (file-in: warn if missing)
  and `--input-trace-record` (file-out: `mkdir -p` parent). All `wslpath
  -w`'d so a repo-relative/Unix path Just Works — no more hand-copying the
  trace into the game dir to dodge the UNC `fopen` failure.
- **Verified end-to-end** (debug exe): record to `runs/.../rec.jsonl`
  (repo-relative) wrote `\\wsl.localhost\…\rec.jsonl`; replay of a synthetic
  9000-entry trace logged `input trace replaying ← … (9000 entries)` — the
  exact case that used to print "replay disabled". Missing-file replay emits
  the new warning.

Also documented the verified one-shot capture recipe (replay + capture in a
single repo-relative `run-openrecet.sh` invocation → `frame_NNNNN.bmp`) in
`tas-framework.md`, closing P0 item 3. **All three P0 papercuts done**; P1
(determinism pinning) is the next TAS milestone.

## 2026-05-30 — Mipmap fix: 3D mesh textures now match retail's filtering (+ pixel_diff tool)

Closed the queued HOUSE texture-filtering parity gap
([[project_texture_filtering_parity]] item 2). The port created **every**
texture with `CreateTexture(Levels=1)` — no mip chain — so minified 3D
meshes (back-room shelf trim, the green star book, the window blinds)
sampled the sharp base level and read *crisper* than retail, even though
the sampler filter STATE already matched (trilinear `2,2,2`).

- **Root cause (objdump ground truth, engine-quirks §54).** Retail has
  **two** d3dx8 texture loaders. `FUN_0047193c` (the one `texture-loader.md`
  documented) loads **2D UI** assets with `MipLevels=1` — no mips, correct
  for 1:1 blits. But **3D mesh** textures load via a *separate* loader
  **`FUN_00471b24`** (mesh-cache miss path `FUN_00472836`, keyed on
  `DAT_073cb108`) which passes **`MipLevels=0`** → a full box-filtered mip
  chain (`MipFilter=D3DX_DEFAULT` = `D3DX_FILTER_BOX`). The decompiled
  "MipLevels 1" note was right for the UI loader but wrongly assumed global.
- **Fix.** `src/sprite.c`: `sprite_create_impl` + `box_downsample` generate
  a `Levels=0` chain by straight 2×2 averaging (matches D3DX box filter);
  new `sprite_load_mipped` is used **only** by the mesh loader
  (`mesh_load_finalize_win32`). UI sprites keep `Levels=1`, mirroring retail.
- **Verified.** Matched-1024×768 port-vs-retail diff of the static
  back-wall shelf trim: OLD differed on **14862 px**, NEW on **282 px
  (mean 0.00/ch)** — bit-identical where the camera aligns. Full-frame
  (dialog-masked) OLD 86.2 → NEW 83.5; many minified-mesh tiles dropped to
  exact 0. Build clean, **2974/2974** host tests pass.
- **New tool:** `tools/pixel_diff.py` — the canonical render-parity format
  `[A | B | amplified white-diff]` (white = differs); prints differing-px
  count + mean abs-diff. Reusable for all future comparisons.

**Deferred (determinism wall):** clean *book* and *back-blind* diffs vs
retail are blocked — every retail capture I have is mid-intro, with the
dialog box occluding the front book and the un-ported 2D HUD (1,000-pix
banner, Day wheel) over the back blinds, while the port is in free-roam.
A clean diff needs port + retail at the same dialog-free state with an
aligned/frozen camera — the first real use-case for the planned TAS
framework (`docs/plans/tas-framework.md`).

## 2026-05-30 — Cchr.2f/2g: solid textured Recette in HOUSE + the real player-draw path (doc-drift audit)

Session began as a doc-drift audit (user: "what do you mean by visible house
pixels — we're already rendering the 3D scene"). They were right:

- **Doc drift fixed** (`a504506`).  `STATUS.md`'s top blocker ("Cf.* writer
  blocks visible HOUSE pixels") was stale since 2026-05-29 — the Cf.* writer
  landed and HOUSE furniture/background render by default.  Root cause: a
  hardcoded `CURRENT_BLOCKER` in `tools/gen_port_ledger.py`.  Corrected +
  regenerated; the real front is **character billboards**.

- **Cchr.2f** (`83b4dc1`): wired the chr sprite-sheet texture.  The leaf
  (`FUN_0045a56f`) binds nothing — the caller does `SetTexture(0,
  DAT_073a9b18[char])`.  Added a sheet loader+getter
  (`scene1_preload_{load_,}chr_sheet`); the asset is `"bmp/chr/chr%02d.bmp"`
  (NO underscore — objdump; the old code's `chr_%02d.bmp` silently failed).
  **Key pixel-diff finding** (existing `runs/cchr2b` leaf capture, HOUSE frame
  17544): the visible standing player (char 0) + companion (char 1) are drawn
  **only** by `FUN_004552d0` (the **shop-walker**) at `0xff808080`, reading the
  `DAT_056daae8` position-history ring (`FUN_0048b850` fills it) — **NOT** the
  `FUN_00456f56` chr-walker, whose blue `0x7f7fff` player path is situational.
  Corrects the Cchr.1 premise.

- **Cchr.2g** (`bf4efaa`): ported the shop-walker's player draw (the port had
  it as a mislabeled, stubbed "light pass" `sw_pass_light`).  Behind an MVP
  inject (`scene1_shop_walker_set_player_inject`, `--force-chr-walker`
  re-targeted), **solid opaque Recette now renders** in HOUSE.

- **bmp colorkey fringe** (`7378abe`): a green halo around the billboard traced
  to `bmp.c` keeping the key RGB at alpha 0 → bilinear bled green.  D3DX's
  colorkey writes transparent BLACK; zero RGB too.  Fixes every keyed sprite.

Remaining for the full (un-MVP'd) player: the companion (char 1 sheet), the
colour base/pulse (`4552d0.c:394-435`), and replacing the inject with the real
`DAT_056daae8` ring (`FUN_0048b850` / Cpop) + `DAT_056da1cc/1d8/dae18` globals.
**Retail screenshot pixel-diff deferred to after the full path lands** (per
user); leaf-level ground truth already validates geometry + colour.

## 2026-05-30 — Cpop.6: FUN_0048b850 dash-trail / after-image record advance

Ported the per-frame advance of the 5 after-image records (`DAT_056dabac`,
0x44-byte stride, ending at `DAT_056dad00`; decomp L90300-90334, objdump
`0x48c991-0x48ca9d`) as `player_ctrl_trail_advance` — the **live consumer**
of the Cpop.2 `player_ctrl_trail_orbit_pos` geometry leaf, and the loop the
dormant chr-sprite walker reads to draw the trail behind a moving player.

Per active record (life counter signed `> 0`): an optional alloc-spawn
(`FUN_0044376a(…,3,i)`, gated on the `DAT_056dae14` decay edge, fired
*before* the copy) → snapshot the live sprite-state ring head
(`DAT_056daae8`, 11 dwords) into the record → recompute the orbit position
via the leaf (`angle = 2·table[idx] + stored`, `r = idx+3`) → a birth-spawn
(`FUN_0041331d(0,x,y,z,4,0.7,0xffffffff)`) the frame life is exactly `600`
→ decrement life.  The two engine calls are reported through an out-param
`pc_trail_events` (NULL-able) so the advance stays pure and host-testable;
the eventual `_WIN32` controller body fires them.

**New quirk — engine-quirks §57 (§53/§56 from the store side):** the
record's `x/y/z` are **floats** (`fstp DWORD` at `0x48ca06`/`0x48ca47`), but
Ghidra walks the record through an `int *` and so prints the writes as
`piVar9[-5] = (int)(…)`.  Trusting the cast would quantise every after-image
to integer world coords and stair-step the trail; the fields are stored
into the int32 record via `memcpy` of the real float bits.  `+3.0`
(`0x519438`), threshold `600`, and the `0.7` spawn arg (`0x519748`) are all
objdump-verified.

4 host tests (2997 → 3001): dead-record skip, sprite-copy + geometry +
decrement, birth-spawn at exactly 600, and decay alloc-event ordering.
Module still unwired → HOUSE behavior + goldens bit-identical, no regen.
Remaining for the chip: the `DAT_056daae0` after-image ring shift
(L90269-90296) and the footstep/proximity block (L90336-90370), then wiring
the whole controller behind the `FUN_0048670f` caller chain.

## 2026-05-30 — Cpop.5: FUN_0048b850 shake-target accumulation — camera-shake-magnitude subsystem complete

Ported the per-frame shake-*target* magnitude accumulation (`local_8`;
decomp L89957-90008, objdump `0x48be33-0x48bfa0`) as
`player_ctrl_shake_target` — the scalar the already-ported
`player_ctrl_camera_shake_clamp` limits the shake vector to.  With this the
**whole camera-shake-magnitude subsystem is ported**: zoom decay (Cpop.1),
magnitude clamp (Cpop.1), per-frame damp factor (Cpop.4), and now the
target accumulation that drives the clamp.

The accumulation: `base` (0.175, or a per-state amplitude-table value the
caller resolves) → `+0.02`/`+0.08` held boosts → `×1.3` action boost →
`+= db074` (when `DAT_0438b8b0 == -1`) → `+0.06`/`+0.03` rumble (gated on
`DAT_056dae9c`, `DAT_056daeac` bits) → state overrides (`db048==1` → 0.5;
`db048∈{4,5}` → char `0x29` ? 1.0 : 0.5) → a proximity-ease override
`0.3 − clamp01(daedc − da1dc)·0.1`.  All constants objdump-verified
(the two held boosts are 64-bit `faddl` doubles in the binary).

**New quirk — engine-quirks §56 (the inverse of §53):** the proximity-ease
gate `DAT_056daed8 == 1` is an **integer** compare (`cmp %edi,…` with
`edi==1`), but Ghidra renders it as the float literal `1.4013e-45` (the
denormal whose bits are `0x00000001`).  The same `edi==1`/`ebx==0` register
convention in this function also flips the rendered polarity of
`DAT_056db034 == 1` and `DAT_056db048 == 1` — always check the `cmp`
operand register before trusting a decomp `== 0`/`== 1`.

6 host tests, one per branch (2991 → 2997).  Module still unwired → HOUSE
behavior + goldens bit-identical, no regen.  Remaining for the chip: the
`0x56dab6c` per-record trail spawn/expiry loop (the visible after-image
fill around the already-ported orbit geometry leaf), then wiring the whole
controller behind the `FUN_0048670f` caller chain.

## 2026-05-30 — Cpop.4: FUN_0048b850 camera-shake damping-factor selector

Ported the per-frame shake-vector decay selector (decomp L90160-90198,
objdump `0x48c538-0x48c6a0`) as `player_ctrl_shake_damp_factor` — using the
input-query args resolved in Cpop.3.  Each frame the shake vector
(`DAT_056daabc`, `DAT_056daac4`) is multiplied by one of six factors picked
by a short decision tree; the leaf returns that factor and the caller
applies it (the zoom bias `DAT_056daac0 *= 0.95` is the unconditional
companion at `LAB_0048c6a6`, left to the controller body).  All six `.rdata`
constants objdump-verified bit-exact: `0.97` (mode≠0), `0.99` (airborne),
`0.95` (grounded + held-gate), `0.998` (idle settle), `0.98`/`0.82` (the
`DAT_056db048` state block).  Kept the in-engine-dead `0.98` arm faithfully
(it's only reachable when *not* grounded, but the block is gated on grounded
upstream — a real decompiled branch, preserved not optimized away).
6 host tests, one per branch (2985 → 2991).

Module still unwired → HOUSE behavior + goldens bit-identical, no regen.
Remaining: the `local_8` zoom-shake *target* accumulation (the other half
of the shake magnitude, feeding the already-ported clamp) and the
`0x56dab6c` trail spawn/expiry loop.

## 2026-05-30 — Cpop.3: FUN_0048b850 motion-history ring shift + resolved the §53 dropped-arg input queries

Continued the `FUN_0048b850` player-controller port with its next pure
leaf and cleared the groundwork the Cpop.2 note flagged as remaining.

- **`player_ctrl_history_shift`** (`src/scene1_player_ctrl.{c,h}`; decomp
  L90243-90269, objdump `0x48c8a9-0x48c90f`): the per-frame motion-history
  ring shift the after-image / trail draw samples.  The engine keeps **two
  parallel 40-slot rings** laid out back-to-back — a 3-float position
  history (`DAT_056da1fc`, 40·0xc B) and an 11-dword sprite-state record
  history (`DAT_056da3dc`, 40·0x2c B), the record ring ending exactly at the
  shake globals `DAT_056daabc` (the memory adjacency confirms the slot
  count).  Each frame it memmoves every slot one place toward "older"
  (`slot[i] = slot[i-1]`, walked high→low via two pointer loops + a
  `rep movsl` for the 11-dword record) then writes slot 0 = the live player
  pos / live `DAT_056daae8` record.  Pure: no engine globals or callees, so
  it ports as a leaf over caller-owned arrays (the chr_walker pattern).
  4 host tests (2981 → 2985).
- **Resolved the §53 dropped-arg input queries** (objdump, no code yet):
  every `FUN_004856d7` / `FUN_0043647f` call in `FUN_0048b850` is shown
  argless by Ghidra but both are `cdecl(int key)` — `004856d7` = "is
  binding `key` **held**?", `0043647f` = "is `key` in this frame's
  **edge/event** list?".  Recovered the literal key ids at every call site:
  the **shake-damp selector** uses `004856d7(0x96b)`×2 + `0043647f(0x9)`;
  the **`local_8` zoom-shake target chain** uses `004856d7(0x968)` (+0.02),
  `004856d7(0x969)` (+0.08), `0043647f(0xb)`/`0043647f(0xc)` (×1.3).  This
  corrects the earlier "0x96b / 9" note (which conflated the two blocks) and
  unblocks porting both — engine-quirks §53 updated.

Module still unwired (caller chain `FUN_00442cef → FUN_0048670f`
unported) → HOUSE behavior + all goldens bit-identical; no regen needed.
Remaining for the chip: the `local_8` target + shake-damp blocks (now with
their args resolved) and the `0x56dab6c` per-record trail spawn/expiry loop
around the already-ported orbit geometry leaf.

## 2026-05-30 — Cpop.2: two more FUN_0048b850 pure leaves (emote-pulse counters + trail-orbit geometry)

Continued the `FUN_0048b850` player-controller port begun in Cpop.1, adding
the next two genuinely-pure leaves (both objdump-ground-truthed before
writing), in `src/scene1_player_ctrl.{c,h}`:

- **`player_ctrl_pulse_counters`** (decomp L89799-89817 / objdump
  `0x48b8c9-0x48b917`): the emote-bubble pulse triple — `db00c` down-counter,
  `db008` 0..0x3c phase timer, `db000` 0..10 intensity that ramps up while
  `phase < 0x1e` and back down after, wrapping at `> 0x3c`.  Confirmed both
  `phase` comparisons use the *post-increment* value (`inc eax` precedes both
  `cmp`s).  `db000` feeds the `sin(level·π/8)` bubble-scale draw at all.c
  L6901+.
- **`player_ctrl_trail_orbit_pos`** (decomp L89906-89933 / objdump
  `0x48c9c6-0x48ca47`): the geometric core of the `0x56dab6c` dash-trail /
  after-image fill — the array the walker reads in sweep-0.  Per record,
  `angle = 2·table[anim_idx] + stored`, `r = idx + 3.0`, then
  `x = sin(angle)·r + px`, `y = py`, `z = cos(angle)·r + pz`.

Two objdump catches vs the Ghidra decomp, both logged as **engine-quirks
§53**: (1) the trail record's `+0x3c` angle field is a **float**
(`fadd DWORD [ebx-4]`), which Ghidra mistypes as `(float)int` — porting it
as the conversion would corrupt every record; (2) the adjacent
velocity-damping block calls `FUN_004856d7(0x96b)` / `FUN_0043647f(9)` with
args Ghidra drops (shown argless) — flagged for whoever ports that block next.

8 host tests (2962 → 2970).  Module still unwired (caller chain
`FUN_00442cef → FUN_0048670f` unported) → HOUSE behavior + all goldens
bit-identical; no regen needed.  Remaining for the chip: the stateful body
(the `local_8` zoom-target accumulation with its dropped-arg queries, the
shake/velocity damping-factor selection, and the per-record trail spawn +
expiry around the geometry leaf).

## 2026-05-30 — Cpop re-scope (trace-confirmed) + Cpop.1 player-controller leaf math

Picked up the parked Cpop attempt (see the deleted `docs/HANDOFF-cpop1.md`).
The whole Cpop premise was misattributed twice over; corrected with evidence,
then runtime-confirmed the real chip before writing any port code:

- **`FUN_0044376a` is NOT the "8538 B actor→render-slot copier."** It is the
  records_b entity-effect spawn allocator (`(owner,type,flag)`; loops
  `idx*0x124` over `@0x069324b0`) — already fully ported as
  `scene1_record_b_spawn_entity()` (C8j.5–C8j.9a). Fixed the POPULATOR SURVEY
  banner + memory notes (commit `5a8f14b`).
- **Objdump ground truth** (`21042e7`): `0x56dacc0` (the party render array) is
  referenced exactly once in `.text` — the walker read @`0x45722a`. The
  survey's two "writer" referents (`0x4375ff`, `0x48c961`) both touch
  `0x56dacf8` (= base+0x38 age) and are *clears*, not fills. The real
  per-frame fill in `FUN_0048b850`'s tail writes the `0x56dab6c`
  trail/after-image array (walker sweep-0 reads it as `esi-0x154`).
- **Runtime confirmation** (`bf2cffe`; Frida call-trace, 60k-frame auto-z-spam
  drive into the playable shop, `runs/calltrace-shop-probe`): `FUN_0048b850`
  fires 40,558× from frame 4583 on — the live playable-HOUSE player controller.
  Its caller is **`FUN_0048670f`** (not `FUN_0048b3f6`, which fires 0×: the
  dispatcher gate @all.c:40595 takes the `0048670f` branch because the HOUSE
  scene-state stays in `[0,4]`). The first-3D-draw scene is instead the
  `FUN_004427d3 → FUN_0048407f` cutscene/event arm (frames ~3046–5958) — a
  separate chip. So `--auto-3d-trace` captures the *wrong* scene for b850.

**Cpop.1** (`076bcab`) starts the faithful `FUN_0048b850` port with its three
genuinely-pure leaf computations (`src/scene1_player_ctrl.{c,h}`), host-tested
per the chr_walker pattern: the 8→4 facing-octant snap with its sticky
horizontal-bias bit (`DAT_056dae3c`), the `DAT_056daac0` camera-zoom decay
(−0.03, floor −2.0), and the camera-shake magnitude clamp. 10 host tests
(2952 → 2962). Module unwired (caller chain `FUN_00442cef → FUN_0048670f`
unported) → HOUSE behavior + all goldens bit-identical; no regen needed.
Remaining for the chip: the stateful body (head timer state-machines, the
`local_8` zoom-target chain, the `0x56dab6c` trail fill) in later sub-chips.

## 2026-05-29 — Cchr.2e: the records / people sprite pre-pass

Ported `FUN_0045672a` (1317 B) — the render-side sibling of the Cchr.2d
walker, dispatched from `scene1_render_meshes` at L246 (the slot the old
`scene1_walk_alpha_pre_TODO` stub held, right after the alpha-pre
`MIPFILTER=NONE` set) — as `src/scene1_chr_prepass.{c,h}`. Three record-draw
sections, objdump-verified @ 0x45672a:

- **Section A** — `g_scene1_records_b` slots (the 0x49-dword table, count
  `DAT_0076b964`) whose `TYPE==0x61` are drawn as world-space 3D meshes via
  the ported `scene1_emit_record` (engine `FUN_00455191(&DAT_073a9658)`).
  World = `Scaling × Translation`, **no** billboard base matrix; two scale
  modes gated on the slot's AGE field (`<0x46` → fixed `(-0.14,0.04,0.14)`;
  else size-field-driven `(-0.5,1,0.5)·(field·0.2)`).
- **Section B** — `g_scene1_records_a` slots (the 0x25-dword table, count
  `DAT_0076b960`) whose `TYPE==0x97` (and `!=-1`), same mesh-draw path with
  a `RotationY(ROT_X)` added: `rotY × Scaling(-s,s,s) × Translation`.
- **Section C** — the 128-entry people billboard table (engine
  `DAT_0076b970`, stride `0xba4`, **unported**): a depth co-sort (engine
  `FUN_0045526a`) on the `+0x450` key, then each active, non-`0xff`-alpha
  entry drawn camera-facing through the validated 2b leaf
  `scene1_chr_sprite_render` (`base × Scaling(desc[+0x44]·0.05) ×
  Translation`, per-entry alpha = `alpha_byte·mult`, color `| 0x7f7f7f`).

Sections A/B share a one-time D3D envelope (engine `FUN_00456c4f`, applied
lazily on the first drawn slot of either); Section C has its own. The
Ghidra branch presentation (the `*piVar5 < 0x46` scale pick) was confirmed
against objdump; the 8 float constants decoded from `.rdata`
(`0.2 / 0.5 / -0.5 / 0.14 / 0.04 / -0.14 / 255.0 / 0.05`); the engine's
redundant `Scaling(1,1,1)` multiplies are kept verbatim (no-ops) and
commented.

**DORMANT in HOUSE.** Sections A/B are wired to the **real** record globals
(empty today — counts 0 — so they fire automatically once those populators
land). Section C's people table has no port-side storage yet
(`chr_prepass_people_base()` returns NULL → whole section skipped), same as
the walker's NPC pass. No visible change on HOUSE entry from this chip.

- **Host-tested** the one non-trivial helper: the index co-sort
  `chr_prepass_sort` (engine `FUN_0045526a`, a stable strict-`<` bubble
  sort). **5 tests** (basic, pre-sorted, signed keys, equal-key stability,
  n≤1 no-op); **2952 total pass**. Both exe targets build warning-free.

**Texture-filtering data point** (for the 1:1-retail follow-up): the A/B
envelope sets `MAG/MINFILTER=LINEAR`; the C (people) envelope sets
`MAG/MINFILTER=POINT`. The alpha pass that dispatches this pre-pass also sets
`MIPFILTER=NONE` just before. Decoded @ 0x456a3c / 0x456c4f.

The Cchr.2 ladder now has 2a–2e landed. Remaining for **visible HOUSE
characters** is the actor populator chain (`FUN_0048b3f6` → `FUN_0048b850`
→ `FUN_0044376a`, ~14 KB — the corrected attribution; *not* `FUN_00436f97`,
which is the already-landed furniture writer). Findings in
`docs/findings/scene1-char-sprite-render.md` "Cchr.2e".

## 2026-05-29 — Cchr.2d: the HOUSE character-sprite walker

Ported `FUN_00456f56` (1982 B) — the per-frame driver that builds the world
matrix + diffuse color for every actor billboard and hands each to the
validated 2b leaf — as `src/scene1_chr_walker.{c,h}`. Wired into
`scene1_render_meshes` (L248-L251, second WIDE-frustum slot), replacing the
`scene1_walk_wide_b_TODO` stub.

Four passes behind a live D3D state envelope: companion (char 2),
player+party (2-sweep loop over the 0x44-stride actor array with spawn-pop
ease + draw-order alpha), NPC billboards (the people record table, off-
screen fade ramp, char 0x43), and an NPC sub-render pass (`FUN_00456d48`,
a no-op stub as in shop Pass F). The intricate Ghidra float-as-int
confusion (`2.8026e-45`=loop bound 2, `1.4013e-45`=1, etc.) was resolved
against objdump @ 0x456f56; all .rdata float constants decoded; the NPC
fade's `FUN_00503954` is `__ftol`.

- **Pure, host-tested math** (where the constants live): `chr_walker_fadein`,
  `chr_walker_spawn_ease`, `chr_walker_actor_alpha`, `chr_walker_npc_alpha`.
  **12 host tests**; 2947 total pass.
- **DORMANT in HOUSE** until the actor/people tables populate. Their
  populator is `FUN_00436f97` (4788 B) — the unported "Cf.* writer chunk"
  STATUS.md lists as the top HOUSE-pixel blocker. The D3D envelope is live;
  the pass bodies reach data through accessors that return NULL/count 0
  today (the established `scene1_shop_walker` dormant-walker pattern). This
  is correct render code that draws nothing until the populator lands —
  **don't expect visible HOUSE characters from this chip alone.**

Both exe targets build warning-free. The Cchr.2 ladder now has 2a/2b/2c/2d
landed; remaining is 2e (`FUN_0045672a` records pre-pass) and the populator
`FUN_00436f97`. Findings in `docs/findings/scene1-char-sprite-render.md`
"Cchr.2d".

## 2026-05-29 — Cchr.2c: the actor animation frame tick

Ported `FUN_00482a71` (118 B) — the per-tick sprite-animation advance —
as `chr_anim_tick()` in `src/scene1_chr_sprite.{c,h}`. Given an actor
sprite-state struct, its char id, and a dt, it walks the per-char frame
LUT (the `chr_meta_lut` accessor Cchr.2a already exposes): when the frame
timer reaches the current frame's duration (LUT field 5) it steps to the
next frame, honoring the two markers on the *next* frame's field-0 cell —
`0x3ff` (HALT → hold on the current frame) and `0xffffffff` (animation end
→ wrap frame + counter to 0).

- **Decompiler correction.** Ghidra typed the state struct as `int *` and
  emitted `param_1[2] = (int)(param_3 + (float)param_1[2])`. objdump @
  0x482a71 shows the timer slot `[2]` is a **float** accumulator (x87
  `flds`/`fadds`/`fstps`/`fldz`/`fcomps`); only `[3]` (frame counter) and
  `[4]` (frame idx) are ints. The port stores/loads `[2]` via `memcpy`
  into the int32 slot, and a dedicated test pins float accumulation.
- All engine call sites pass `dt = 1.0` (`0x3f800000`), so durations are
  integer frame-counts; the float matters for fidelity, not for any
  current caller.

**6 host tests** (`test_chr_anim_tick_*`): below-duration accumulate,
advance-at-duration, HALT hold, animation-end wrap, the float-timer pin,
NULL-safe. **2935 total, all pass**; both exe targets build warning-free.
This is the pure-leaf half of Cchr.2c — the struct *populate* half lands
with the walker (Cchr.2d, `FUN_00456f56`), which will call this tick + the
validated 2b leaf per frame. Findings in
`docs/findings/scene1-char-sprite-render.md` "Cchr.2c".

## 2026-05-29 — Cchr.2b: the HOUSE character sprite leaf renderer

Ported `FUN_0045a56f` (1223 B) — the leaf that draws Recette / Tear / NPC
billboards in the shop — into `src/scene1_chr_sprite.{c,h}`. This is the
renderer Cchr.1 ground-truthed and Cchr.2a built the data layer for.

- **`chr_sprite_build_quads()`** (pure, host-tested): the per-cell quad
  geometry, faithful to objdump @ 0x45a56f. Resolved the two dropped
  pieces the spec flagged: the 8-entry facing→bank/flip tables
  `DAT_005c5a54`/`DAT_005c5a74` (8 dirs → 5 sprite banks + horizontal
  mirror), and the dropped `sin` argument — the spawn shimmer is
  `sin(age·π/2/20)·sheet_w·0.2`, dormant for a standing actor (age 0).
  Per cell it reads the formdata frame entry (big-endian `base`@`char*4`,
  `ncells`@+0x400 / `start`@+0x600 / sheet-pos@+0x800), maps sheet
  position → object XY and the linear atlas walk (`start+i`) →
  half-texel-inset UVs, and emits a 6-vertex TRILIST quad in the engine's
  exact order V0,V1,V2,V3,V0,V2 with the `[7]/[8]/[9]` color/alpha gate.
- **`scene1_chr_sprite_render()`** (Win32): SetTransform(WORLD) → build →
  the flag-gated DrawPrimitiveUP tail (FVF 0x142, stride 0x18). The
  `COLOROP=7/8` bracket on the special-flag branch is verbatim from
  objdump and **pending Frida A/B** for its visual intent.

All engine float constants decoded from the binary (1.0/100/0.5/32/0.2/
π÷2/20). **9 host tests** (`test_scene1_chr_sprite.c`); 2928 total, all
pass. Both exe targets build warning-free. Findings + resolved open
questions in `docs/findings/scene1-char-sprite-render.md`.

**Followup (same day) — strategy-B steps 4–5 scaffolded end-to-end:**

- **Frida leaf-capture** (`frida_capture.py --chr-leaf`): hooks the leaf
  at ENTER + its DrawPrimitiveUP, riding the `--dump-records-b` HOUSE
  free-roam drive. Writes `chr_leaf.jsonl` with `leaf_in` (the 5 inputs
  + the descriptor/formdata-derived fields, so it's self-contained) and
  `leaf_out` (the vertex buffer retail built). Agent + Python both
  syntax-clean.
- **`--force-player-sprite <inject>`** (main.c): wires the Cchr.2a loaders
  at boot (under the flag), reads a flat inject file, and overlays the
  ported leaf's player billboard on the HOUSE scene for visual A/B.
- **`tools/chr_leaf_to_inject.py`**: turns a capture into the inject file
  (picks the player's call by char-id + nearest-to-player matrix), and
  `--emit-expected` dumps retail's `leaf_out` for comparison. Verified
  end-to-end on a synthetic capture — the picked call's expected verts
  match the port's host-tested build_quads output exactly.

**VALIDATED — bit-exact A/B vs retail (same day).** Ran the capture
(`runs/cchr2b`, free-roam HOUSE frame 17544, Recette at (-0.30,0,9.35)):
the leaf fired 8× that frame; the player call (char 0) had `sheet_w=128,
scale=100, y_origin=114, facing 6/bank 4, anim 0 frame 2 → cell 10,
formdata ncells=6 start=60 pos=[5,6,9,10,13,14], color 0xff808080,
tex 512×1024`. **`chr_sprite_build_quads` reproduces retail's full
36-vertex DrawPrimitiveUP buffer bit-for-bit** — locked in as
`test_chr_sprite_retail_recette_house` (2929 pass). The standing player's
flags are 0/0/0 → single-draw tail (COLOROP=7/8 bracket is a different,
unexercised flag state). A retail backbuffer screenshot of the matched
frame confirms the character billboards render correctly.

Both exe targets build warning-free. Remaining: the actor-walker port
(Cchr.2d) that builds param_1 per frame to feed this validated leaf;
then the --force-player-sprite inject is replaced by the real walk path.
Full runbook in `docs/findings/scene1-char-sprite-render.md`.

## 2026-05-29 — Cchr.2a: character sprite-metadata loaders (chr/formdata + .idx)

First *code* chip of the Cchr.2 character-sprite port. Cchr.1 named the
leaf renderer `FUN_0045a56f`; surveying its data dependencies showed it
reads a character-sprite-animation subsystem the port had not built —
so Cchr.2 is a chip ladder (2a–2e), not 3 functions. This chip lands the
**static data layer** both the leaf and the frame-tick depend on:

- **`chr/formdata.bin` blob** (engine `DAT_0438abe0`): loaded raw by the
  tail of `FUN_004341fe`; our `storage.c` port stops before that tail, so
  it was unloaded. Ported as `chr_formdata_load()`.
- **Per-character descriptor array** (engine `DAT_0438cea8`, 68 chars,
  stride 0x5058): built by `FUN_00479f78` by parsing one `.idx` text file
  per character. Ported faithfully as `chr_meta_parse_idx()` — the `.idx`
  grammar was fully resolved (all sscanf formats are `"%s"`; the hold
  marker keyword is `"HALT"` @ 0x5cb994; frames pack 6 dwords each,
  `0x3ff`×6 = HALT, `0xffffffff` = end-of-animation, animations 0x100
  dwords apart).

New `src/chr_sprite_meta.{c,h}` (asset-independent data layer:
alloc/parser/accessors — linked into the host suite) + `src/chr_sprite_meta_load.c`
(storage-backed loaders — real build only, so the host suite needs no
`storage.c`). **9 host tests** (`test_chr_sprite_meta.c`); 2919 total,
all pass. Both exe targets build warning-free.

Not yet wired into boot (awaits the 68-entry idx-filename PTR list at
0x5c80c4 + a decision on descriptor-populate timing). Full dependency
map + the Cchr.2b–e ladder + two MVP strategies (faithful-loaders-first
vs Frida-inject-MVP) + open questions are in
`docs/findings/scene1-char-sprite-render.md`. No retail-side validation
yet — the parser is host-tested on synthetic `.idx` only; a Frida
descriptor dump for bit-exactness is queued.

**Followups (same day):** (1) the two open questions blocking the leaf
port were resolved by objdump of `FUN_0045a56f` — the "frame-LUT stride
mismatch" was an arithmetic slip (`0x359*6 = 0x1416` exactly = block
stride; the facing table is a within-block bank offset), and
`FUN_005038d0` is `__alloca_probe`, not a dropped FPU arg. (2) The
68-entry idx-filename PTR list @ 0x5c80c4 was transcribed, so
`chr_meta_load()` is now operational (recette/tear/.../prime; many slots
share a sheet). (3) A complete, objdump-verified, turnkey port spec for
the leaf renderer (`FUN_0045a56f`) + the chosen Frida-inject MVP step
list are recorded in the findings doc. The leaf C transcription itself
(Cchr.2b) is the next chip — its validation is a Frida player-call
capture + visual A/B.

## 2026-05-29 — Cchr.1: RESOLVED — the HOUSE player/companion sprite path

Followed up Cchr.0 (which falsified "characters live in records_b /
people table") by naming the **actual** renderer, ground-truthed against
retail. Extended the dump driver with a `--quad-hist` trace
(`tools/frida_capture.py` + agent `installQuadHistHooks`): on each
free-roam dump-offset frame it records every 2D quad-add (`FUN_00404efc`)
**plus** every `DrawPrimitive(UP)` / `SetTexture` / `SetTransform(WORLD)`,
so a sprite drawn as a world billboard (not a screen quad) surfaces with
its bound texture and its world-matrix translation.

**Result (run `runs/cchr1-xform`, free-roam frame 18018, `g_player_pos =
(-0.30, 0, 9.35)`):** pairing each `DrawPrimitiveUP` with its preceding
`SetTransform(0x100)` translation and matching to `g_player_pos` pins the
whole scene:

- **Player (Recette)** = `FUN_0045a56f` 12-prim sprite at exactly
  (-0.30, 0.00, 9.35). **Companion (Tear)** = same renderer at
  (0.60, 2.95, 9.35). Shop **object sprites** = same renderer, scattered.
- `FUN_0045a56f` is a sprite-sheet → multi-quad billboard → `DrawPrimitiveUP`
  (stride-24 FVF, `&DAT_0438cdf8` billboard base matrix) renderer, driven by
  the scene-1 actor walkers **`FUN_00456f56` / `FUN_0045672a`** — which are
  two of the 14 walker stubs inside the already-ported `scene1_render_meshes`
  (`FUN_00459dfd`).
- The walkers read the **actor table at `DAT_056da1b8`** (stride `0x44`;
  the player's pos field IS `g_player_pos` = `DAT_056da1d8` = base+0x20).
  This table is live on HOUSE entry — so the minimal "character in HOUSE"
  path needs neither the 25.7 KB `FUN_0043ae20` integrator nor the 30 KB
  `FUN_004176ff` walker (the latter's 6 billboards that frame were the
  ambient `0x1f` particles, re-confirming Cchr.0).
- Player **shadow** = `FUN_0045aa36` (binds the shade tex); object shadow
  blobs = `FUN_0046f648` (dark `0xff202020` quads). Separate, lower-pri pass.

Updated `findings/scene1-char-sprite-trace.md` (Cchr.1 RESULT + Cchr.2
proposed port path), corrected `scene1-chr-walker.md` + INDEX. Tooling
only (Frida JS + Python driver); no C changed, no host-test impact.
Added `nodejs` to the dev-shell flake for `node --check` on the agent JS.

## 2026-05-29 — Cchr.0: retail HOUSE character-render trace — FUN_004176ff is NOT the player renderer

Ground-truthed the C7m premise with a new Frida dump mode
(`tools/frida_capture.py --dump-records-b` + agent `dump_records_b*`).
Drives retail to HOUSE unattended (auto-z-spam + turbo), anchors on the
first frame either record table populates, and dumps the live contents of
all three character-candidate tables — records_a (DAT_069b2f80), records_b
(DAT_069324b0), the people table (DAT_0076bd54) — plus per-pass counts,
g_player_pos, a per-frame DrawIndexedPrimitive heartbeat, and a backbuffer
screenshot per dump frame.

**Result (3 captures, all visually confirmed free-roam HOUSE with Recette +
Tear on screen):** records_b = **0**, people table = **0 alive**,
records_a = **6** — and all 6 are a single ambient particle emitter
(type 0x1f, scale 0.1, staggered recycling ages, clustered at ~1.2,4.0,9.4
= the sparkle by Tear). Per-frame DrawIndexedPrimitive holds at ~82 (static
room + furniture); the character sprites add nothing to it.

**Verdict:** the visible characters are **2D billboards on a dedicated
sprite path**, not records walked by FUN_004176ff. This **falsifies the C7m
conclusion** ("port FUN_0043ae20 25.7 KB integrator + FUN_004176ff 30 KB
walker to get characters") — that pair renders particles/entities, of which
a fresh HOUSE has none-but-ambient. New `findings/scene1-char-sprite-trace.md`
records the trace + the Cchr.1 next chip: hook `render_quad_add` (0x404efc)
in free-roam HOUSE and bucket by caller-VA to name the 2D player/companion
sprite renderer and the player struct it reads. Corrected
`scene1-chr-walker.md` + INDEX. Commits: 0070e31 (tooling), 44320a3 (docs).
No C changed; no host-test impact.

## 2026-05-29 — C7m re-scope: chr-render survey (FUN_004176ff) — dormant without the table-B integrator

After C7j, picked the character sprites (Recette/Tear in the shop) as
the next visible target. Two corrections fell out of the survey:

1. **Pass 7 of the HUD aggregator is NOT chr render.** Body reads of
   FUN_0046b00a (Vendors/market-stocking menu, gated DAT_0734b98c) and
   FUN_00466b7b (sub-panel slide/scale transition, gated DAT_0438b7b0)
   show both are dormant **shop menus** binding the terminal atlas, not
   character renderers — same mislabel the survey flags for FUN_00459847.
   Corrected `findings/scene1-walker.md`.

2. **The real character renderer is `FUN_004176ff`** (30,395 B,
   `scene1_walk_chr_TODO` in the 3D mesh-walker chain,
   `src/scene1_render.c:854`) — surveyed in new
   `findings/scene1-chr-walker.md`. It's the unified per-record
   entity/particle/character 3D render walker over the records_a/b
   tables (371 draw sub-calls, mostly D3DX matrix thunks + mesh leaves).

   **Verdict (data-liveness sweep): porting it renders ZERO characters
   on a fresh new-game HOUSE.** It is data-driven off `g_scene1_records_b`,
   which is BSS-zero on HOUSE entry — the populator, the 25.7 KB
   integrator `FUN_0043ae20` (INGAME arm `FUN_00442cef`), is unported/
   stubbed. So "characters in the shop" is gated behind the two largest
   functions in the scene, not a single chip.

   Documented Route A (HOUSE-minimal `FUN_0043ae20` player-alloc subset
   + incremental walker port) vs Route B (`--force-b-entity-type` smoke
   harness to validate each render sub-pass without the integrator), and
   recommends a **Frida retail trace FIRST** to find which sub-pass +
   record fields actually draw the player, scoping a player-only path
   instead of all ~5,308 lines.

No code landed (survey-only); docs + INDEX updated.

## 2026-05-29 — C7j: scene-1 2D HUD aggregator begun (FUN_0040a765 shell + Passes 1-3)

First chip of the **C7i ladder** — the last HOUSE-visible HUD blocker.
FUN_0040a765 (0x40a765, 7558 B) is the 2D HUD/overlay aggregator the
engine runs between the 3D walker and the overlay dispatcher
(`FUN_004547ab` L71: `camera_setup → FUN_0040a765 → overlay → fx_tail`).
The survey (`findings/scene1-walker.md`) groups it into nine passes;
this chip lands the entry shell + the first three:

- **Pass 1** — entry guards + 2D state preset (`render_quad_state_setup`)
  + stamina/HP backdrop. Backdrop gated `*DAT_068dd2f0 > 0` (DUNGEON
  only) → **dormant in HOUSE**.
- **Pass 2** — letterbox / cinema bars keyed off `DAT_0438b1dc` with the
  engine's ±0.1 dead-zone clamp → BSS-zero → **dormant**.
- **Pass 3** — status-screen takeover (`DAT_073dddb4`): render status
  screen + early-return. BSS-zero outside the Q-menu → **dormant**.

New module `src/scene1_hud.{c,h}`; `scene1_hud_render()` wired into
`main.c` between `scene1_render_camera_setup` and `scene1_render_overlay`
(engine order). Every pass past the state preset short-circuits on a
BSS-zero gate in HOUSE, so the chip is **visually inert today** — it
lands the structure + the 2D render-state preset the player-facing
passes inherit (C7k+: item tooltip, HOUSE/DUNGEON sub-walkers, speech
bubbles, shop terminal, chr render, sub-menu panels, day-counter flash).

Globals mapped: scene mode → `g_scene_state`; stage type → stage record
field 0 (`maptype`); HUD textures already loaded in `g_sysassets`
(`shade_bmp` = DAT_073cc8f0, `system_bmp` = DAT_073aa188). Deferred
sub-calls stubbed (faithful call-count): FUN_0049065b (2D-overlay-camera
feed off a BSS-zero source block), FUN_004141c0 (status screen),
FUN_0043647f (DUNGEON predicate).

**Cr.2 correction:** `scene1_render_overlay` is already wired (main.c)
and the COLORARG2 leak (PHC #18) was resolved 2026-05-26 — so the
"Cr.2 overlay re-enable" item tracked in the HOUSE-visible memos was
already done. C7i is the genuine last HOUSE-HUD blocker, now begun.

Pure helpers (letterbox dead-zone, backdrop colour packing, pass-active
predicate, status flag) host-tested: **+8 (2902 → 2910)**. Both exes
build clean. Port-side HOUSE capture (`runs/c7j-house-check`,
`--auto-z-spam --force-walker-phase2 0`) confirms the shop interior +
furniture render unchanged — no regression from the inserted call.

## 2026-05-29 — E.4 Tier 1: first STATEFUL diff targets (faithful-parity oracle)

Extended the pure-function differential oracle (`tools/diff_test.py` +
`tests/build/libengine_diff.so` + Frida `runRetail*` RPCs) from
arg-less/RNG-state functions to its first two **stateful leaves**, per
`docs/plans/e4-per-call-io-capture.md` Tier 1. These were the gap E.4
exists to close: both leaves landed in the frame-59 burst work on
**call-count parity only** and had never been body-verified — count
parity can't catch a leaf that's called the right number of times but
computes the wrong answer.

- **`stage_gate_boss_id_allowed` (FUN_00431990)** — pure `cdecl(int)→int`
  boss-id range predicate. First target that injects an **arg** (rides
  in on `[esp+4]`, marshalled via Frida `['int']`) rather than a global.
- **`stage_gate_floor_is_checkpoint` (FUN_0043195d)** — reads two globals
  (`DAT_0438b4c8` dungeon id + `DAT_0438b4cc` next floor), returns 0/1.
  First **global-injection** target: the retail RPC snapshots, writes,
  calls, reads back, restores both in a `finally`. Vectors include
  negative `next_floor` because the engine's `next % 5` is a signed
  `idiv` matching C's `%` (verified bit-exact).

Both pass **300/300 vs retail** (cutestation.soy, seeds 0xdeadbeef +
0x1234); full default set (rng_next15 + audio_fade + both new) green, no
regression. 2902 host tests still pass; both exes build.

**Plan correction worth recording:** the E.4 plan listed Tier 2
(engine-tick freeze + race-retry) as a prerequisite for stateful retail
RPCs. It isn't — `diff_test.py` spawns retail `CREATE_SUSPENDED` and
never resumes, so the engine main thread is frozen for the whole run and
there's no race on the injected globals. **Tier 1 stands alone on the
existing infrastructure;** Tier 2 is only needed for a *live* retail
(mid-scenario per-call I/O capture). Docs updated:
`findings/pure-function-diff.md`, `plans/e4-per-call-io-capture.md`,
`harness-roadmap.md` §E.4.

Touched: `src/diff_entry.{c,h}`, `tests/diff_stubs.c` (BSS-zero
`g_scene1_combat_stage_id` + `g_enemylist` so `stage_gate.c` links into
the host .so without dragging in scene1_combat_sm's heavy include web),
`tests/Makefile`, `tools/frida/openrecet-agent.js`, `tools/diff_test.py`.

## 2026-05-29 — Public-release detour: repo goes public-ready (all 5 tasks)

Executed `docs/plans/public-release-detour.md` end-to-end so the repo can
go public with a guarantee of **zero proprietary bytes** in the binary.

- **Task 2 (the gating one) — stop embedding SE; extract at runtime
  (`f4b597d`).** The shipped `openrecet.exe` no longer links any game
  audio. New `src/se_pack.c` + `src/sha256.c`: `audio_init` calls
  `se_pack_acquire()`, which locates the retail `recettear.exe`
  (`OPENRECET_RETAIL_EXE` env, else `./recettear.exe`), hashes it, and
  either loads a matching `%LOCALAPPDATA%\openrecet\se.pack` or extracts
  the 110 `WAVE` resources via `LoadLibraryEx(...AS_DATAFILE)` +
  `FindResource` and writes the cache (keyed on the exe sha256, so a game
  update re-extracts). SteamStub leaves `.rsrc` unencrypted, so this reads
  the *packed* exe directly — no Steamless at runtime. Dropped
  SE_RC/SE_RES_O from `src/Makefile`. Format: `docs/formats/se-pack.md`.
  **Verified:** built exe has 0 RIFF magic (only WAVE strings are Win32/
  DirectMusic type+GUID names); first run logs `extracted 109/110`, second
  run logs `loaded cache`; both preload 109/110 SE segments — identical to
  the old embedded behaviour. 2902 host tests (+8: sha256 vectors,
  se.pack round-trip/reject).
- **Task 5 — pin the reference exe (`6dd26a7`).** `docs/reference/
  vendor-exe.md`: packed+unpacked sha256/size/PE-ts, Steamless v3.1.0.5,
  per-section encryption map (only `.text` differs). App ID **70400**
  confirmed against the local appmanifest.
- **Task 3 — CI nightly (`6643ba9`).** `.github/workflows/nightly.yml`
  (daily cron + manual dispatch) cross-compiles via a new lean
  `devShells.ci` (mingw + make + python3 only — not the full RE closure),
  gated by `tools/ci/no_proprietary_bytes.py` (hard-fails on any RIFF),
  publishing to a rolling `nightly` pre-release (asset clobber → no
  watcher spam). Build is now asset-free, so CI needs no game files.
- **Tasks 1+4 — public README (`726254e`).** Hero = labeled OpenRecet-vs-
  retail HOUSE side-by-side (`docs/img/house-comparison.png`). Framed as
  early-stage / not-playable, detail deferred to STATUS.md + port-ledger;
  ko-fi (`ko-fi.com/lolisamurai`) + AI-driven-RE transparency.

User decisions captured in the plan doc: cache → LOCALAPPDATA; cadence →
daily cron; README → labeled side-by-side, broad status framing.

## 2026-05-29 — HOUSE render fidelity via D3D-trace A/B: brightness + blinds (5 fixes)

Used the D3D state-trace pipeline (D.4 Frida + D.5 port + per-draw compare)
to pin HOUSE render divergences against a fresh retail capture
(`runs/retail-d3d-house`, frame 14000, `frida_capture.py --d3d-trace
--auto-z-spam --turbo`). Each fix is trace-verified.

- **`8d4e376` HOUSE ~2× brightness (RESOLVED, user-confirmed 1:1).**
  `FUN_00454f03` was ported setting `D3DTSS_COLORARG2` not `D3DTSS_COLOROP`
  (the value table {2,4,5,7,8,10,11} are D3DTEXTUREOP codes), and
  `scene1_palette_combiner_mode()` was stubbed to 0 instead of
  `rec->drawcode` (HOUSE `drawcode:2` → mode%7==2 → MODULATE2X). Base room
  pass was MODULATE (1×) vs retail MODULATE2X (2×). Floor-centre
  (90,70,28)→(227,176,79) ≈ retail (207,162,70).
- **`463a810` + `960e4ee` blinds blend/arg leaks.** Two Ghidra
  type-confusions in the HOUSE-dormant `scene1_wide_followup` (it draws
  nothing but its state preamble RUNS and leaks): mis-ported engine
  MIN/MAGFILTER(0x11/0x10) as COLORARG2/1=TEXTURE (→ texture² rainbow),
  and SetRenderState(SRC/DESTBLEND, 0x13/0x14) as
  SetTextureStageState(MAG/MINFILTER) (→ leaked DESTBLEND=INVSRCCOLOR
  instead of INVSRCALPHA). Port HOUSE draw-state now matches retail
  exactly. **Lesson: audit dormant walkers' state writes.**
- **`4070c3f` hikari texture (PARTIAL).** The hikari pass binds the
  engine's animated `DAT_073aa198[frame]` (loaded from `mood_para<NN>.bmp`
  by FUN_00474a9a), not the submesh's embedded sprite. Port had loaded
  `mood_para` but left the hook NULL → cyan fallback. Wired it → curtains
  cyan→green. **Still diverges** (zoom diff `runs/window_zoomdiff.png`):
  likely wrong/missing animation frame + the frustum/middle-glow/table-
  shadow hunt items remain. Deferred to next session — see
  `findings/scene1-house-render-gaps.md` hunt-list.
- **`f1c7b2f`** ruled out `FUN_00459847` (combat additive-fx renderer,
  dormant in HOUSE) as the brightness source; corrected the stale
  "PHC #26 writerless" survey claim.

## 2026-05-29 — PII.3d: per-stage maplight builder ported (FUN_00458f67)

Ported the scene-1 per-stage FFP map-light builder — the `FUN_00458f67`
sub-pass that was stubbed as `scene1_walk_pre_dispatch_TODO` ("purpose
unknown"). New module `src/scene1_maplight.{c,h}`:
`scene1_build_maplight` (L199 build) + `scene1_maplight_rebind`
(L220-230 re-apply), wired into `scene1_render_meshes`. Constructs the
`D3DLIGHT8` at engine `DAT_06a49a40` and does
`SetLight(0)+LightEnable(0,TRUE)+D3DRS_LIGHTING` per the stage's
`maplight` mode.

**Erratum corrected (the key unblock):** the "HOUSE stage palette is
all-zero → lighting off" assumption was wrong. The engine parses
`stage.idx` straight into the `DAT_068dd2f8` table that `DAT_068dd2f0`
indexes, so the live palette IS the parsed record. HOUSE (`stage:0-1`)
is `maplight:3` (time-of-day town light), `fog:20:500`,
`fogcolor:230:240:255`, `hikaridrawcode:2`, `hikarialpha:96`,
`hikariadd:1`. Added `scene1_current_stage_record()` to bridge the
renderer accessors to `g_stage.records[HOUSE]`; the lighting + fog
(start/end/color) accessors in `scene1_render.c` now read the real
record instead of returning 0. The `SetLight` args were never actually
Ghidra-dropped — `FUN_00458f67` builds the struct field-by-field.

The 4 `maplight` modes (0 sun / 1 animated pulse / 2 static / 3 town
time-of-day) are all ported; mode-3 `MAPLIGHT3_PRESET` rows verified
against decomp `local_98[0..0x1a]`. Day/night clock (`DAT_0438b1e0` /
`DAT_0450fb88` / `DAT_0438b7d4`) unported → mode 3 uses the daytime
row 0 (fresh-entry default). 8 new host tests (2886→2894), both exes
build clean.

This closes the lighting prerequisite for the two tracked HOUSE render
diffs. **Still open:** gap #1 hikari god-rays (texture source + additive
blend, `s_hook_animated_tex` still NULL) and visual re-verification of
gap #2 (blinds on lit tris) — see `scene1-house-render-gaps.md`.

## 2026-05-29 — PII.3c: HOUSE shop interior BACKGROUND renders (draw loop A, user-verified)

The shop room now paints behind the furniture — walls, back-wall shelves,
wooden floor, the corner counter, and the carpet. User-verified live ("that
seems to render the room correctly") on `--auto-z-spam` (runs/house-bg-on/
frame_03300.png). Before this the 3 furniture meshes floated on a blank navy
clear; now it's the actual Recettear shop.

**The shop background is 3D, not a 2D layer** (corrected a stale survey
premise). It's FUN_00457714 *draw loop A*: phase-1 instances drawn out of the
per-stage `map:` mesh pool (engine DAT_068dcca0). Retail ground truth (new
`tools/dump_phase1_groundtruth.py` → runs/phase1-groundtruth.json): HOUSE
loads **11** map meshes; draw loop A draws **2** phase-1 instances —
mesh idx 0 = `xfile/shop/shop_1st.x` (the room, 48 submeshes) at the origin,
mesh idx 1 = `xfile/jutan/shop_jutan.x` (carpet) at (-2,0,-1).

Four parts (commits d2d1753 + 6d17aff):
1. **Map-mesh loader** (`src/scene_map_meshes.{c,h}`, FUN_00474681) — the
   pool was never populated; the port loaded wall/floor/jutan/table *textures*
   but not the `.x` *meshes*. Wired into `scene1_preload_house` right after
   `mesh_tex_cache_reset()`; loading all 11 also repopulates the texture cache
   draw loop A's classify→SetTexture path needs.
2. **Phase-1 writer** (block-21 else-branch in `scene1_postload_walker_phase2_init`):
   count dispatch (DAT_0438bfb0), mesh-index array (DAT_0438bfb8), transform
   constant block.
3. **Phase-1 matrix builder** (`scene1_walker_phase1_compute`): S(-0.2,0.2,0.2)
   × RotY × T — phase 2's chain minus the mesh_type==4 flip.
4. **Draw loop A** (`scene1_walker_pass_render_house`): per cache slot, draw
   each phase-1 instance's map mesh; cull skipped (HOUSE threshold 1000 >> the
   shop's ~25-unit camera distance).

The subtle part was the **column→axis remap**: objdump of the phase-1 setup's
`D3DXMatrixTranslation(pOut,x,y,z)` push order (call 0x4a34b0, esi=0x438c0a8 =
shared-column index 15) gives x←rot_y-col, y←pos_x-col, z←pos_y-col,
rot←mesh_type-col, read at index (15+i). Index 15 is never written → instance 0
(room) at the origin; the constant block fills idx 16-19 → instance 1 (carpet).

8 new host tests (2878→2886). NOT ported (not render-critical for HOUSE):
per-mesh FUN_00471d45 (collision/bounds aux) + the DAT_068dcf98 single-mesh
path (gated off for HOUSE).

**Follow-up same day (commit 15db713): floor/walls/rug textures fixed.** The
room's kabe/yuka/jutan surfaces rendered untextured grey because draw loop A's
per-stage SetTexture hooks (`s_hook_{kabe,yuka,jutan}_tex`) were NULL in
production — only ever set in tests. `scene1_preload_house` now installs
`house_{kabe,yuka,jutan}_texture` adapters (return `g_scene_X[selector].tex`)
after the foreground loads. User-verified: wooden floor, textured walls, red
patterned rug all render (runs/house-bg-tex/frame_03300.png).

**Tracked render diffs vs retail** (`docs/findings/scene1-house-render-gaps.md`):
two remaining diffs both trace to the unported scene-1 lighting path — (#1) a
"frustum" solid mesh where retail has god rays (hikari, `param_1==3`, bound via
the still-NULL `animated_texture_hook` + no additive blend); (#2) blinds
texture goes solid-color on lit tris, scaling with god-ray intensity (missing
per-vertex maplight, `D3DLIGHT8` at `DAT_06a49a40`, `SetLight` args Ghidra-
dropped — see scene1_shop_walker.c:509). **Suggested next chip: scene-1
lighting + hikari god-ray overlay** (closes both). Remaining beyond that: the
2D HUD overlay (FUN_0040a765, C7i) + Cr.2 overlay re-enable.

## 2026-05-29 — HOUSE inputs DE-MVP'd: furniture renders with NO flag (user-verified)

The `--force-walker-phase2 0` MVP (which injected retail-captured ground
truth for the 8 HOUSE render inputs) is retired for 7 of 8 inputs — a real
new-game HOUSE now renders shop-table furniture, correctly framed, with **no
flag**. User-verified live ("I saw the furniture and it seemed to render
correctly") on `--auto-z-spam` with no `--force-walker-phase2`.

**Two retail Frida captures** (new `tools/dump_demvp_groundtruth.py`) resolved
the open questions: furniture positions live at save-record **+0x2ce10** (not
+0x2ce20; reader was right), char_mode at +0x2ce0c = 0, scene_type = 0, and
the cam-adds = 14/21/-1.8 at every in-scene frame.

The 8 inputs, by resolution (commits after 4b049b7):
| input | de-MVP source | commit |
|---|---|---|
| ivar8 | engine constant 3 (FUN_00436f97 L178) — not a runtime input | 1 |
| yaw=π | ported into the Cf block (`walker_phase2_init`, FUN_00436f97 L589) | 1 |
| radius/eye.y/lookat.y adds (14/21/-1.8) | scene-1 camera CONSTANTS (no writer in 2620 fns; set in `scene1_camera_init`) | 2 |
| char_mode (0) | save record +0x2ce0c | 3 |
| scene_type (0) | stage selector (HOUSE=0; PHC for the stage-table string loader) | 3 |
| stage_positions | save record +0x2ce10, seeded from template DAT_005cf864 via ported FUN_0048ffd9 | 3 |
| **bias_x/z (-0.3/9.35)** | **STILL a HOUSE stand-in** — output of the FUN_00432e50 placement search (2084 B, unported) | — |

`scene1_postload_load_house_phase2_inputs()` is the new production HOUSE-entry
loader (wired into `scene1_preload_house`); `--force-walker-phase2 N` is now
only a test override for synthetic scene_type tiers 1..4. 2873 tests pass
(new `..._load_house_inputs_from_save_record` proves the loader reproduces the
3 live furniture meshes the retail-groundtruth setter test gets, with no
injection).

**Open PHCs** (faithful-port follow-ups): (a) bias placement search
FUN_00432e50 + FUN_00436f97 block 228-276; (b) the cam-adds' rdata→BSS copy
site (value confirmed, writer unidentified — pre-scene read crashes on the
unmapped page); (c) scene_type from the ported DAT_068dd3fc stage-table
string loader; (d) the port's `save_bank_init_all` leaves record +0x2ce0c at
a -1 artifact vs retail's 0 (the loader establishes the new-game HOUSE fields
explicitly until FUN_0049d36d lands).

## 2026-05-29 — HOUSE furniture orientation/scale RESOLVED: camera pose-input fix (user-verified)

The Cf.minimal landing earlier today made HOUSE shop_table furniture visible
but with wrong orientation + scale (furniture flung to screen edges, tilted on
its side, underlit). **Now fixed — user-verified "finally correct furniture."**

**Root cause: camera pose, not the per-mesh transform.** The view-build
mechanism (`scene1_camera_build_view_matrix` = LookAtRH × RotZ) already matches
the engine (FUN_0040120c), and the per-mesh WORLD chain is asm-verified — so
the divergence was entirely in `scene1_camera_pose_compute`'s *inputs*, several
of which the port had hard-coded to BSS-zero / placeholder values.

**Method.** New `tools/dump_camera_groundtruth.py` (models on
dump_phase2_groundtruth.py) drives retail to HOUSE via `--auto-z-spam` and reads
the engine's camera globals at the furniture frame. Retail HOUSE:
eye=(-1, 22.2, 15), lookat=(-1, 1.2, 1), yaw=π, fov=45° (already matched).
Diffing the pose inputs against the port surfaced five wrong values:

| global | port had | retail HOUSE | source |
|---|---|---|---|
| `_DAT_0695ef70` radius add | 0 (assumed BSS) | **14.0** | per-stage cam-param loader (unported) |
| `_DAT_044e2c70` eye.y add   | 0 | **21.0** | "" |
| `_DAT_069b2f78` lookat.y add| 0 | **-1.8** | "" |
| `g_scene1_camera_yaw`       | 0 | **π** | FUN_00436f97 L589 (Cf block) |
| `char_mode` (uVar2)         | 2 (hardcoded) | **0** | `*(int*)(&DAT_045105a4+slot*0x2dfc8)` |

The yaw=0→π flip put the camera on the opposite side (180°); radius 4→14 was the
2-3× scale; eye.y add 0→21 put the eye below the floor looking up at undersides.
All five reconcile to retail's eye/lookat exactly (block-by-block).

**Fix.** Made the three assumed-zero compose globals + the two bias sources
settable in `scene1_camera.c`; added `scene1_camera_apply_house_groundtruth()`
(sets the retail-captured values), wired behind the same `--force-walker-phase2
0` path in main.c, applied *after* `scene1_camera_init()` (which now clears the
overrides to boot-faithful 0 for test isolation + non-HOUSE scenes). Defaults
unchanged → title-phase canaries unaffected. New host test
`scene1_camera_house_groundtruth_matches_retail` asserts the port reproduces
retail eye/lookat (2872 tests pass). Visual A/B: runs/house-cam-fix vs
runs/house-phase2-on — three upright shop counters, correctly framed/scaled/lit.

**Faithful-port follow-ups (PHC):** this is an MVP injection (Cf.minimal
pattern). yaw=π is a direct write in the Cf block we already partially port;
char_mode is a per-save-slot field; the three adds come from a per-stage
camera-param loader (sentinel-terminated arrays ending at &DAT_044e2c70 /
&DAT_069b2f78, see all.c L44697/933/964).

## 2026-05-29 — Cf writer-hunt RESOLVED: FUN_00436f97 block-21 fires on HOUSE entry (survey was wrong)

The HOUSE shop_table render gap's "writer we can't find" is found — and it
was never missing, only mis-attributed. The 2026-05-26 survey claimed
`FUN_00436f97` does **not** fire on new-game HOUSE entry (it inferred HOUSE
goes only through `FUN_004547ab`→`FUN_00474a9a`). That was a static-analysis
inference and is **wrong**.

**Method.** The D.7 `mem_watch` MemoryAccessMonitor approach hit its
documented hot-page wall on the first run — the phase-2 globals live on data
pages (`0x438b000`, `0x438c000`) busy with per-frame reads, exhausting the
8000-rearm budget before HOUSE entry on *both* candidate regions. Pivoted to
the E.1 Frida **call tracer** (no hot-page problem): traced just `{0x436f97,
0x459dfd, 0x457714}` every frame while `--auto-z-spam` drove a new game.
Result: **FUN_00436f97 called exactly once at engine frame 3200**, 11 frames
before the first `scene1_render_meshes` (3211) and HOUSE furniture walker.
That is the block-21 "alt-stage arm" else-branch (the phase-2 writer at
all.c L34772-34849 / by-address 436f97.c block 21), which populates
`DAT_0438bfb4` count + the furniture arrays the walker reads.

**The real gap.** Cf.minimal (commit 7dbe0b0) already *ported* this writer as
`scene1_postload_walker_phase2_init()` — but left it gated off (scene_type
defaults to -1), unwired into the HOUSE path, and dependent on 3 runtime
inputs (scene_type, ivar8, the 10 stage-position pairs). So the writer logic
exists; it just never runs in production.

**Ground truth + validation.** New tool `tools/dump_phase2_groundtruth.py`
reads the writer's inputs and outputs from retail via the agent `readMemory`
RPC after the writer fires. For new-game HOUSE: stage_idx=0, save_slot=0,
scene_type=0, phase2_count=3; 3 live furniture meshes
(type 3/4/4, rot_y 0/0/(π/2), pos (-2,0,0)/(-4,0,-8)/(-10,0,-2)). Fed those
captured inputs into `scene1_postload_walker_phase2_init()` in a new host test
(`..._retail_groundtruth_new_game_house`) — the port reproduces every field
**bit-for-bit**. Cf.minimal is now ground-truth-verified, not just
asm-decoded. (2871 tests pass.)

**Next chip** (HOUSE-visible): wire `scene1_postload_walker_phase2_init()`
into the INGAME-entry arm path with the 3 inputs sourced from engine state —
scene_type from the `DAT_068dd3fc[stage*0x6cf]` selector, ivar8, and the
stage_positions from the per-save-slot record (`&DAT_044e3798 + slot*0x2dfc8
+ 0x2ce10`). Those three are themselves unported dependencies; an MVP that
hardcodes the captured new-game-HOUSE inputs behind a flag would surface the
first visible furniture pixels for visual A/B while the proper input ports
land.

## 2026-05-29 — D.7 mem-watch tool built + validated; unpacked-exe regression fixed

Built the Phase D.7 memory-access-watch capability (commits 8ba6a93,
3b02666) — the unblock path for the HOUSE shop_table render gap. Arms
Frida's `MemoryAccessMonitor` over an engine region in retail, traps the
writer, maps the faulting instruction to its owning engine function via
the port ledger. New surface: `installMemoryWatch()` + `mem_access`
batching in `openrecet-agent.js`, `mem_watch[_regions/_precise]` in
`frida_capture.py`, and the standalone `tools/mem_watch.py` ranker.
Validated end-to-end against retail.

Two findings shaped it (full detail in `plans/d7-mem-watch.md` Build log):
- `MemoryAccessMonitor` is **page-granular + op-blind** — first access of
  each 4KiB page, can't distinguish read/write. Fine for a *cold* region
  written at a discrete event (expected HOUSE case); a *read-hot* page
  (the `var_input_mask` smoke target) burns the precise-mode re-arm budget
  on neighbor reads before the write lands. Added precise mode (re-arm
  past page neighbors, record only in-region) as the default.
- HW write watchpoint (`Thread.setHardwareWatchpoint`, Frida 17.5.1) is
  the byte-granular fallback but **crashed retail** when armed on all
  threads — not shipped.

Mid-session: discovered `vendor/unpacked/recettear.unpacked.exe` had been
silently replaced (≈05-27 14:12) by a non-loadable memory-image dump —
Windows rejected it as "not a valid application for this OS platform",
surfacing only as an opaque frida "unsupported file format" on spawn.
Isolated test confirmed: the real `recettear.exe` spawns, the dump
doesn't. Re-ran Steamless (`setup.sh --force`) to regenerate the rebuilt,
loadable PE (deterministic — same sha as prior good unpack, so the Ghidra
analysis is unaffected). Added a `setup.sh` overwrite guard + read-only
bit + sha marker so a stray write can't recur (commit 91e2782).

Remaining: the writer-hunt run itself — pin the exact stale save-record
field (`&DAT_044e3798 + slot*0x2dfc8 + off`) via a HOUSE-frame
call/render-trace diff, then point `mem_watch` at it.

## 2026-05-28 — Backfill: 05-25 → 05-27 (reconstructed from git + memory)

> Backfill entry. The per-commit detail (115 commits) lives in `git log
> --since=2026-05-25`; this is the arc-level summary the narrative was missing
> after the log lapsed on 05-24. Durable per-subsystem RE is in `findings/`;
> live counts in `STATUS.md`.

Four parallel work-streams landed in this window:

1. **scene1 records-B tick + combat state machine (C8j-tick.0–16, C8jb.0–fin).**
   The bulk of the work — ~70 commits porting `FUN_0043ae20` (per-record tick,
   ~100+ entity "body" types dispatched by type byte: anchor cascades, ground
   bouncers, walker/shop-walker driven motion, homing drift, trail-cull
   variants) and `FUN_0043865e` (per-record combat state machine: Phase A entry
   gates, Phase B attacker-scan + AABB collision + damage formula + hit-effect
   emit, Phase C projectile-table scan + TYPE-dispatched spawn/sound clusters).
   `combat_sm` wired as the production `state_machine_hook` (f3939b8). Several
   PHCs resolved by static analysis along the way (PHC #1, #10, #18, #26).

2. **scene1 render — PII + Cf chips.** `FUN_00457714` HOUSE furniture renderer
   ported (PII.survey→PII.3b: setup phase 2, outer loop, draw loop B);
   `scene1_render_overlay` + `scene1_render_fx_tail` wired (Cr.2). `FUN_00436f97`
   alt-stage arm writer chunk ported minimally (Cf.minimal) — visible HOUSE
   shop_table pixels, but with the diagnosed translucent/orientation/scale bugs
   that motivated the Phase D render-diff harness. **Cf.\* writer chunk remains
   the top render blocker** (see memory house_visible_blockers).

3. **Verification harness — Phase D + E build-out.** D.1 pure-function diff
   scaffolding (`diff_test.py` + `libengine_diff.so`, rng_next15 target); D.4/D.5
   D3D state-trace emitters (Frida + port side, `src/d3d_trace.c`); D.6
   `render_diff.py`. Then the Phase E leaf-first pivot: E.0 TTD record/query
   scaffold, E.1 Frida call tracer, E.2 port-side `CALL_TRACE_ENTER` annotation
   scheme + `call_trace_diff.py` (the annotation scheme superseded the
   roadmap's sketched cyg_profile/call_graph_diff design — see harness-roadmap
   §E note), E.3 `pre_3d_trace` mode. Methodology proven by surfacing + fixing
   the alpha-walker gap. (See harness-roadmap.md for reconciled phase status.)

4. **Pre-3D / post-new-game parity chips.** Title-phase frame-1 gap driven to
   benign-only (music `FUN_00499583`, `scene1_fx_overlays`). New-game →
   frame-59 gap narrowed 24→17 via probe coverage + micro-helper ports
   (`stage_gate` 0x4319d6/0x43195d/0x431990, `stage_post_load` cluster,
   `title_save_dialog`, `npc_schedule`, `chara_skills`, `chara_equip`,
   `xp_curve`, `d3d_pool`, etc.). Cutscene parity goal clarified: the pre-3D
   cutscene is `recet_op.wmv` (DirectShow video) — skipping it is correct port
   behavior (see memory cutscene_is_directshow_video).

## 2026-05-24 — Process-supervision: Job-Object launcher + singleton mutex

Closes a long-standing class of test-iteration bugs where openrecet
children survived their harness (paused-window state blocked the
in-engine `--max-duration-ms` timer, and `taskkill /F /IM` was too
blunt to use safely in parallel). New surface:

- `tools/supervisor/run-supervised.c` + Makefile build
  `build/openrecet-supervisor.exe`. The supervisor wraps any Win32
  child inside a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
  When the supervisor exits for any reason (timeout, Ctrl+C, SIGKILL
  on the WSL stub, parent shell dying), the kernel closes the job
  handle and unconditionally reaps every process inside it.
  Targets PID, not image name — concurrent runs never collateral-
  kill each other. Exit codes: child code on clean exit, 124 on
  timeout (coreutils convention), 125 on supervisor self-error, 130
  on Ctrl+C.
- `tools/run-openrecet.sh` — supervised launcher for ad-hoc bash use.
  Always `cd`'s to `vendor/original/` (the engine-assets cwd) and
  injects a default `--max-duration-ms 3000` if the caller didn't
  pass one.
- `tools/smoke-test.py` and `tools/scenario-test.py` route every
  launch through the supervisor. The old SIGTERM-stub + taskkill-by-
  image dance is gone.
- `src/main.c` — cross-process singleton via `Global\\openrecet-
  singleton` mutex acquired in WinMain right after `parse_cmdline`.
  A second instance refuses to start (stderr + MessageBox), exit
  code 2. Bypass: `--no-singleton` or `OPENRECET_NO_SINGLETON=1`.
  Skips the modal MessageBox in test mode (`--max-duration-ms` set)
  so a CI-style runner doesn't hang on a dialog. Critical for
  iteration: WSL caching the running exe + a stray instance from a
  previous run could otherwise mask updated builds.

Smoke verified: supervisor timeout-reaps a `ping -n 20`, SIGKILL on
the WSL-side proxy reaps both supervisor and child, singleton
rejection during a 4s background run cleanly exits a concurrent
attempt with code 2, `tools/scenario-test.py boot-idle` still passes
3/3 frames through the new path, no orphan processes after any run.

## 2026-05-23 — scene-1 render: C7d stage palette stub (`src/stage_palette.{c,h}`)

Fourth chip on the scene-1 render ladder. Adds the per-stage palette/
state record (engine `DAT_068dd2f8`-based, stride 0x1b3c) and the
current-entry pointer (engine `DAT_068dd2f0`). HOUSE-only for now —
the stage 0 record is statically allocated, zero-initialised, and
the pointer is parked at it during boot.

Engine layout: `DAT_068dd2f0 = &DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c`
in FUN_00474681 / FUN_00436f97. The 0x1b3c (7036-byte) record is the
per-stage palette: scene-1 reads `mode` at +0, the gravity / light
direction vec at +0x1a7c/80/84, lighting flags at +0x1a88/8c, fog
state at +0x1a38..40 + +0x1a90..98 + +0x1adc/e0, clear color at
+0x1aa8..b0, and a boot-trigger flag at +0x1b28. Total of ~15 distinct
typed reads across 459dfd / 4547ab / 405d70 / 458f67 / 4552d0 / 4597ad
/ 4597dd / 458bdf / 436f97 / 4176ff.

The stub types only the fields scene1-render.md C7d explicitly calls
out (`mode`, `gravity_x/y/z`, `lighting_flag_1a88/8c`, `clear_r/g/b`).
The rest stay as opaque `_pad_<offset>` arrays; each gets named when
its reader ports (C7g/C7h or later). Every typed field has a
`_Static_assert(offsetof)` so layout drift surfaces at compile time.

HOUSE defaults are all zero — same observable state as engine BSS, so
nothing renders differently today (and the load chain is still
dormant; that's C7e). The point is to seat the global so future
function ports can read `g_stage_palette->clear_r` etc. directly
instead of inventing yet another local.

Wired into main.c boot right after `stage_init_house()`. Idempotent
+ overwrite-zero contract enforced by tests (same shape as C7c).

6 new host tests covering layout / pointer wiring / zero defaults /
padding-zone scrub / idempotence (848 total from 842). title-z-press
14/14 bit-exact.

**Next:** C7e — port `FUN_00474a9a` (760 B, scene-1 pre-load entry).
With C7c (selectors) + C7d (palette pointer + mode flag) in place the
function has enough state to read `*DAT_068dd2f0 == 0` and take the
HOUSE branch. The port will spawn the secondary worker bodies that
have been registered since C0A but never fired.

## 2026-05-23 — scene-1 render: C7c minimal stage-state seed (`src/stage_state.{c,h}`)

Third chip on the scene-1 render ladder. Adds an explicit
`stage_init_house()` hook that seeds the four scene-1 prop selectors
(walls / floor / jutan / table) to engine fresh-game defaults.

Engine layout: a 0x2dfc8-byte (188360-byte) record per stage at
`&DAT_044e3798`, indexed by `DAT_0438b1e0` (current stage). The
selectors live in the record's "selector zone" at engine absolute
addresses `DAT_0451057c` / `0x04510580` / `0x04510584` / `0x04510588`
(walls / floor / jutan / table). The port currently exposes these as
four standalone int32 BSS globals (`g_scene_*_selector`) — when the
full stage record ports, they fold back into the record at the engine
offsets.

The values themselves are all zero — which IS the right HOUSE default
(engine quirk: each table's slot 0 happens to be the starter asset,
`kabe_sikkui.bmp` / `yuka_ki.bmp` / first jutan / `shop_table01`).
BSS-zero init produces the same observable state, so why have an
explicit hook?

  1. Documents the "stage 0 defaults are zero" contract so it can't
     drift silently when tables get reordered.
  2. Gives future stage-transition code (dungeon exit → shop re-entry)
     a single place to fan out from save-bank state.
  3. Worker-body tests get a known-good baseline call to reset before
     exercising the load chain.

Wired into main.c boot right after the scene_*_init calls
(`scene_walls/floor/jutan/pause/worldmap/table/sc1_init`) so the
selectors are seeded before the title scene starts ticking. Idempotent
+ overwrites stale values (verified by tests).

3 new host tests covering defaults + idempotence + overwrite-stale
(842 total from 839). title-z-press 14/14 bit-exact; the load chain
is still dormant — that's C7e (`FUN_00474a9a` port).

**Next:** C7d — `DAT_068dd2f0` stage palette stub. Needed for the
`*DAT_068dd2f0 == 0` HOUSE/DUNGEON branch test at the top of
FUN_00474a9a, plus the render-side `FUN_00459dfd` reads palette
fields like fog start/end (`+0x1a38..40`), ambient (`+0x1a40`),
lighting flag (`+0x1ae0`), backcolor (`+0x1aa8..b0`).

## 2026-05-23 — scene-1 render: C7b depth + lighting render-state (`src/mesh_draw.{c,h}`)

Second chip on the scene-1 render ladder. C7a got geometry on screen
with a flat-textured pipeline; C7b makes that pipeline match the
engine's pre-mesh-draw render state and adds the depth + lighting
state every later C7 chip will inherit. Visible result: meshes go from
flat-textured silhouettes to properly shaded 3D geometry with correct
Z-ordering across submeshes.

- **`mesh_set_default_render_state(dev)`** refactored to match the
  full state set in `FUN_00459dfd` L86..L198 (`docs/decompiled/
  by-address/459dfd.c`). Every line annotated with the engine source.
  Net changes vs C7a:
  - `D3DRS_CULLMODE` NONE → CCW (val 3 in D3D8's enum, matches engine
    L86).
  - `D3DRS_LIGHTING` FALSE → TRUE (engine L132 starts FALSE for the
    sky pass; L230 turns it on conditionally for the mesh walk — we
    land at TRUE since the preview always wants shading).
  - `D3DRS_AMBIENT` = 0xff000000 (engine L191; per-stage palette
    overrides via `FUN_00454f03` at L185).
  - `D3DRS_COLORVERTEX` = TRUE, `D3DRS_DIFFUSEMATERIALSOURCE` /
    `AMBIENTMATERIALSOURCE` = `D3DMCS_COLOR1` (engine L192/194/195 —
    vertex diffuse drives the material diffuse + ambient channels).
  - `D3DRS_SHADEMODE` = GOURAUD (L198); `D3DRS_ALPHAFUNC` = GREATER
    (L193); `D3DRS_WRAP0` = 0 (L190).
  - `D3DTSS_COLORARG1` flipped to DIFFUSE / `COLORARG2` to TEXTURE
    (engine L196/L197 — the modulate result is identical but order
    matches engine fidelity).
  - `D3DTSS_ALPHAOP` = DISABLE (engine L153 — opaque pass).
  - `D3DTSS_MIPFILTER` = NONE (engine L106, gated on `DAT_0438b178 == 0`
    which is the shipped recet.ini default; mipmaps gate deferred).

- **`mesh_setup_preview_light(dev)` (new)** — preview-only D3DLIGHT_
  DIRECTIONAL setup: light 0 white diffuse, direction normalized
  `(+0.5, -1.0, -0.3)` so the upper-front-right octant gets the bright
  side. Bumps `D3DRS_AMBIENT` from the engine's pitch-black 0xff000000
  to 0xff404040 so shadowed faces stay readable. The eventual
  `FUN_0040a765` port (C7j+) supplies its own light from
  `palette + 0x1ae0`; preview helper goes away for non-`--show-mesh`
  paths when that lands.

- **`mesh_orbital_view_proj` updates** — fov_y switched from 60° to
  the engine default of 45° (`DAT_073de3a0` initial value =
  `0x42340000` at `all.c:34225`, used in every `FUN_004a3ee8` call in
  scene-1 render). Aspect now honors the actual back-buffer dims
  instead of the engine's hard-coded 4/3 — widescreen runs of
  `--show-mesh` aren't letterboxed.

- **`--mesh-zoom <factor>` CLI flag** — multiplies the orbital eye
  distance (default 1.0 = 3·radius). `mesh_compute_bounds` is a
  centroid+max-radius bound that gets inflated by outlier vertices
  on real scene-1 props (the shop interior has a horizon marker at
  ~300 units pulling its radius to 311 even though the visible
  building is ~60 units across). Passing `--mesh-zoom 0.2` pulls
  the camera in to actually frame the content. Z-near/far track the
  same scale.

- **Smoke** —
  - `xfile/etc/ice01.x` (1 submesh, 1 material): now visibly shaded
    instead of flat-textured. Light/dark facets clearly distinguished,
    cull=CCW didn't drop any visible faces. `runs/mesh-ice01/`.
  - `xfile/shop/shop_1st.x` (48 submeshes, 19 materials, 21 textures)
    with `--mesh-zoom 0.2`: full shop interior renders — wood floor,
    walls, doorway, shelves all Z-ordered correctly across 48
    submeshes. `runs/mesh-shop1st/`.
  - 839 host tests still pass; title-z-press 14/14 bit-exact (render
    path still guarded behind `--show-mesh`).

**Next:** C7c — minimal stage state seed (populate the
`g_scene_*_selector` ints so the worker bodies wired in C6 actually
have something to fetch). Lighting state for the eventual walker is
in place; the walker needs ASSETS to draw, which needs the asset
load chain (C7c → C7d → C7e).

## 2026-05-23 — scene-1 render: C7a `--show-mesh` visual smoke (`src/mesh_draw.{c,h}`)

First chip on the scene-1 render ladder (`docs/findings/scene1-render.md`).
Wires the C1-C6 mesh pipeline end-to-end to pixels: a `--show-mesh
<path>` CLI flag that runs the mesh through `mesh_load` +
`mesh_load_finalize_win32` and orbits a fixed camera around it every
frame via `DrawIndexedPrimitive`. Visual smoke for everything that
landed in C1-C6.

- **`src/mesh_draw.{c,h}` (new)** — four pieces:
  - `mesh_resolve_texture_slot(m, mat_idx)` — pure-C: material index →
    global cache slot. Host-testable; clamps OOB / NULL / stale-past-
    cache.count to -1.
  - `mesh_set_default_render_state(dev)` — Win32-only: FVF 0x152, Z on,
    lighting OFF (deferred to C7b), cull NONE, modulate-texture stage 0,
    linear sampler, wrap address. Idempotent.
  - `mesh_orbital_view_proj(dev, centroid, radius, phase, w, h)` —
    SetTransform VIEW + PROJECTION via `math3d`'s lookat_rh +
    perspective_fov_rh. Camera at distance 3·radius, fov_y 60°,
    z_near 0.05·r, z_far 5·r. Y-axis orbit; phase ∈ [0,1).
  - `mesh_draw_d3d8(dev, m)` — per-submesh SetStreamSource + SetIndices
    (BaseVertexIndex=vertex_offset) + SetTexture (via
    `mesh_resolve_texture_sprite`) + SetMaterial (ambient = diffuse) +
    DrawIndexedPrimitive(TRIANGLELIST). Falls back to SetTexture(NULL)
    on materials with no uploaded sprite — geometry still draws against
    vertex white.

- **`src/main.c` (extended)** — new `--show-mesh <path>` CLI flag.
  Loads at boot via `mesh_load(path, -1) + mesh_load_finalize_win32`,
  draws each frame in `render_dispatch` between the scene switch and
  the fade overlay. When set, the scene render is skipped so the mesh
  sits on the pink-blue clear color alone (cleaner contact-sheet
  review). Phase derived from `g_tick.frame_count % 360` → one orbit
  every 6 s at host pace. Shutdown frees the mesh + clears the global
  texture cache.

- **Tests** — `tests/test_mesh_draw.c` (5 new tests, 839 total from
  834) covering the pure-C slot resolver: 3-material happy path,
  no-texture sentinel, OOB material indices, NULL mesh / NULL
  texture_slots, stale slot past cache count. The Win32 draw walker
  itself isn't host-testable (needs a device); validated manually via
  the smoke below.

- **Smoke (`xfile/etc/ice01.x`)** — captured at frames 5/30/60/90 via
  `--capture-frames`; output is a rotating textured ice crystal on the
  pink-blue clear color. End-to-end pipeline (parser → builder →
  bounds → texture-cache dedupe → VB/IB upload → per-submesh draw)
  works in one chip on the first run. Captures live in
  `runs/mesh-ice01/` for review.

- **Regression** — title-z-press 14/14 bit-exact (canary for render-
  path changes). title-options 2/4 unchanged from the pre-existing
  baseline. The render mod only activates when `--show-mesh` is set.

**Next:** C7b — depth + lighting render-state for the eventual scene-1
walker. Today's preview runs unlit (vertex diffuse white modulate
texture); the engine uses fixed-function lighting via `SetLight` /
`LightEnable` + a configurable ambient.

## 2026-05-23 — mesh loader: C6 worker bodies (AAB + C0A, `src/scene_{sc1,table}.{c,h}`)

Sixth chip on the FUN_00472836 family — wires the last two NULL
secondary worker inner bodies through C5's `mesh_load`. All 9 worker
inner-body slots are now bound (modulo the C96 state-machine
FUN_0049de20 first-call, still deferred). 13 new unit tests (834 total
from 821). boot-idle 3/3 + title-z-press 14/14 bit-exact.

### C0A — `scene_table` (`src/scene_table.{c,h}`)

Ports FUN_004748f8 (169 B). Direct structural sibling of the
wall/floor/jutan loader trio — same per-stage selector predicate
inverted by `param`, but each matching slot now loads a PAIR of `.x`
meshes via `mesh_load` instead of a single sprite.

- 8 pairs × 2 names = 16 mesh slots in `g_scene_table[16]`.
- Filename table pre-baked from .rdata 0x5c8018..0x5c8058: shop_table /
  shop_danbo / shop_desk / shop_tarudesk / shop_shokutaku /
  shop_kyoudan / shop_jya / shop_jwel — each in 01/02 variants.
- Format `"xfile/table/%s"`. Selector at stage offset 0x588
  (`g_scene_table_selector`, standalone int32 until stage state ports).
- Win32 body: `scene_table_body` → `scene_table_load_with(...)` →
  `mesh_load(path, -1)` + `mesh_load_finalize_win32`.
- Pure-C `scene_table_load_with(load_fn, userdata, param)` is the
  test-injectable entry point — load_fn captures `(path, slot)`
  dispatches without dragging in mesh_load / D3D.

### AAB — `scene_sc1` (`src/scene_sc1.{c,h}`)

Ports FUN_0046bf38 (230 B). Last of the 9 secondary worker inner
bodies; structurally distinct from the wall/floor/jutan/table siblings
— runs 4 buckets:

1. **Two unconditional fixed sprite_loads**: `bmp/ivent/ive_window.tga`
   and `bmp/ivent/chrname.tga`, both 0x200×0x200, into
   `g_scene_sc1_ive_window` / `g_scene_sc1_chrname`.
2. **Variable `.x` mesh loop** gated by `g_scene_sc1_mesh_count`
   (engine DAT_073a3dfc). Names at `g_scene_sc1_mesh_names[100][256]`
   (engine DAT_0734fff0, 0x100 stride). Dest at `g_scene_sc1_meshes[100]`
   (engine DAT_0735dd88, 0x28 stride — the D3DX mesh-dest struct
   array). Dormant by default.
3. **Variable sprite loop** gated by `g_scene_sc1_sprite_count`
   (engine DAT_073a3df0). Names at `g_scene_sc1_sprite_names[100][256]`
   (engine DAT_07350df0). Dest at `g_scene_sc1_sprites[100]` (engine
   DAT_073571f0, 0x10 stride). Dims 0x400×0x200. Dormant.
4. **Fixed 100-slot sprite array** (engine puVar5 range DAT_073a3ab8 ..
   DAT_073a3dd8 = 100 × 8-byte size pairs). Names at
   `g_scene_sc1_item_names[100][256]` (engine DAT_07357830), sizes at
   `g_scene_sc1_item_sizes[100][2]` (engine DAT_073a3ab8, w/h dwords),
   dest at `g_scene_sc1_items[100]` (engine DAT_0734f9b0). Slot skipped
   when name is the empty string.

State arrays are named BSS-zero externs — once item-table / scene-1
init code ports, those writers populate the names + sizes + counts and
this body picks them up automatically. The body itself is dormant in
practice today (the AAB spawner has no port-side caller until INGAME
starters port).

Test entry point: `scene_sc1_load_with(sprite_fn, mesh_fn, userdata)`.
`sprite_fn(path, kind, slot, w, h, ud)` distinguishes buckets via a
`SCENE_SC1_KIND_*` enum (IVE_WINDOW / CHRNAME / VAR_SPRITE / ITEM).

### Wiring

Both modules `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_{AAB,C0A},
…)` from `main.c` after the existing wall/floor/jutan trio. With C6
landed, worker_load.h's inner-body table marks AAB + C0A both WIRED;
the only remaining gap on the worker side is the C96 state-machine
FUN_0049de20 first-call (deferred — see src/scene_worldmap.h).

### Tests (13)

scene_table (7): introspection (format, filename), param==1 loads 7
non-selector pairs (14 dispatches), param==0 loads only the selector
pair (2 dispatches), OOB selector both directions, pair slot ordering
(pair*2 then pair*2+1), NULL load_fn dry-run.

scene_sc1 (6): dormant default fires only the 2 fixed sprites,
variable mesh / variable sprite loops fire when count > 0, fixed
100-slot loop skips empty names (sparse population), cap clamps over-
sized count, NULL callbacks count-only run.

## 2026-05-23 — mesh loader: C5 orchestrator (`src/mesh_load.{c,h}`)

Fifth chip on the FUN_00472836 family, picking up where C4 left off.
14 new unit tests (821 total from 807). Boot-idle scenario re-passes
bit-exact 3/3.

`src/mesh_load.{c,h}` ports FUN_00472836 (1609 B) end-to-end:

- **`mesh_classify_texture_name`** — pure function emitting 10 mode
  flags from a texture filename, mirroring lines 138..273 of the
  engine. Five prefix checks at offset 0 (water/hikari/kabe_/yuka_/
  shop_jutan), then a 256-position sweep for `n_`/`w_` boolean
  matches + `u0_`..`u3_`/`v0_`..`v3_` index matches (last match
  wins; defaults 0). Includes the engine's dead `.t` 2-char compare
  at DAT_005c8450 for fidelity. `ext_tga` set from the filename's
  `.tga` substring (engine checks the dir+name buffer; static-mesh
  dir prefixes never contain `.`, so the answer is identical).

- **Global texture-name dedupe cache** — `g_mesh_tex_cache` (200
  entries × `{ name[256], flags, sprite }`), mirroring the engine's
  `&DAT_073be908` array + count at `DAT_073cb108` + the 10 parallel
  uint8 side-tables at `&DAT_073cb10c..&DAT_073cb814`. Flags
  intentionally frozen on first insert (engine writes side-tables
  only inside the `bVar15` branch, never on a cache hit). Cache
  layout consolidated into one struct-of-entries — semantically
  equivalent to the engine's struct-of-arrays.

- **`mesh_load(path, param_3)`** — orchestrator. Resolves path
  (with the easydisp `_s.x` variant gated by
  `mesh_load_set_easydisp`, fed from `g_ini.easydisp` in main.c
  after recet_ini_load), reads via `storage_read` with a disk-fopen
  fallback, runs `xfile_parse` + `mesh_build_from_xfile` +
  `mesh_compute_bounds`, then per-material classifies + dedupes
  into the global cache, writing the slot into a new
  `m->texture_slots[]` field (parallel to `m->materials[]`, -1 for
  textureless materials).

- **`mesh_load_from_buf(data, len, path, param_3)`** — buffer-input
  variant. Skips the storage/disk read; everything else identical.
  Used by host unit tests (storage.c pulls `<windows.h>` so it
  doesn't link on the Linux test target).

- **Win32 `mesh_load_finalize_win32(m, dev)`** — VB/IB upload via
  the existing `mesh_upload_d3d8` + a pass over the cache to create
  `sprite_t`s (via `sprite_load("{dir-prefix-from-m->path}{name}")`)
  for any entries whose `sprite` is still NULL. Not hooked into the
  boot path yet — AAB/C0A wiring (C6) does that.

Engine state deferred:
- 12-byte dynamic-bone scratch at `&DAT_073cc950 + (param_3*200+i)*0xc`
  (param_3 >= 0 path; FUN_00472836:118..123). All static-mesh callers
  pass -1, so dormant.
- The `.tga → .bmp` on-disk override inside FUN_00471b24 (sprite
  loader). Our `src/sprite.c` doesn't rewrite the filename; if the
  vendor's `.tga` lives in the archive as `.bmp` and the override
  triggers there, we'd diverge. Watch for it during C6 visual
  validation.
- The 5-line texture-name flag block FUN_004cd30e takes as
  `0xff00ff00` color key — we already pass this in our sprite_load
  for BMPs via `BMP_COLOR_KEY`.

Corpus result: `mesh_load_from_buf` runs across all 223 `xfile/*.x`,
yields 144 unique textures (corpus survey reports 165 — the delta is
materials defined in the file but never referenced by a face index;
both engine and mesh_build_from_xfile drop those). All
`texture_slots[i]` land in `[-1, cache.count)`.

Unit tests (14):
1–9. classifier truth-table over the engine's 10 prefixes/tokens.
10. cache dedupe semantics — same name reuses slot, different name
    grows slot count, flags frozen on first insert.
11. cache capacity — fills 200 then rejects (-1) on overflow.
12. mesh_load_from_buf synthetic — single textured material:
    `texture_slots[0]==0`, cache count == 1, flags match classifier.
13. mesh_load_from_buf no-texture mesh — `material_count==0`,
    `texture_slots == NULL`, cache count unchanged.
14. vendor corpus walk — 223 files load clean, all slots in range,
    cache stays ≤ 200.

`mesh_t` extended with `int32_t *texture_slots` (NULL when the mesh
is built without going through `mesh_load`; freed by `mesh_free`).

Next (C6): wire AAB + C0A worker bodies (FUN_0046bf38 / FUN_004748f8)
to call `mesh_load` + `mesh_load_finalize_win32`. Per-stage selector
already in place from the earlier scene_walls/floor/jutan chips.

## 2026-05-23 — mesh loader: C2 Python oracle + C3 C parser + C4 D3D8-ready mesh

Three chips on the FUN_00472836 .x mesh-loader family, building on the
C1 survey landed earlier today. 14 new unit tests (807 total from 793).
All 242 .x files in `vendor/original/xfile{,2}/` parse + build clean
under ASan + UBSan.

### C2 — Python parser oracle (commit `4af5fe3`)

`tools/extract/xfile.py` grows from a 130-line stub histogrammer to a
full recursive-descent parser (~1130 lines, stdlib-only). Templates
recognised: Mesh / MeshNormals / MeshTextureCoords / MeshMaterialList /
MeshVertexColors / Material / TextureFilename / Frame /
FrameTransformMatrix / Header. Skinning + animation templates
(SkinWeights / XSkinMeshHeader / Animation / AnimationSet /
AnimationKey) brace-skipped and counted.

Output schema (per-file JSON): `path`, `size`, `header`, `stats`,
`textures[]`, `global_materials[]`, `meshes[]` (with vertices, faces,
normals, UVs, material refs, inline materials, face_material_indexes),
`frames[]` (DFS-flat with `children_names`), `skipped_templates`.
Two modes: `--full` (default, includes all arrays) and `--brief` (just
counts + metadata, useful for corpus-wide scans).

Pinned ice01.x assertions in `--self-test`: mesh_count=1,
total_vertices=41, total_faces=30, total_normals=17, two global
materials (`xof_default` + `Material__25` with texture `w_ice.bmp`),
first vertex ≈ [-8.577065, -3.734980, -7.484766], Frame_World hierarchy
with Frame_Box01 child.

Format quirks surfaced + documented in `docs/formats/xfile.md`
(commit `d65f885`):
1. MeshVertexColors per-item separator polymorphism (`cave_dun`
   `;,` vs `boss_omu`/xfile2 `;;,`).
2. MeshMaterialList face_indexes terminator variance (`0;;` vs
   `0,0,...,0;`).
3. Material reference blocks have no interior `;` (just `{Name}`).
4. Hyphen-in-identifier stitch (`PDX02_-_Default` round-trips
   lossy but consistent — `-` drops in tokenizer, IDENTs concat).

Corpus survey output:
- `xfile/`: 223 files, 17.5 MB, 2347 meshes, 118,897 vertices,
  87,029 faces, 165 unique textures.
- `xfile2/`: 19 files, 40 MB, 86 meshes, 8747 vertices, 210
  SkinWeights instances skipped.

### C3 — pure-C parser (`src/xfile.{c,h}`, commit `6c38622`)

Recursive-descent over a hand-rolled token stream. Same template set
+ same quirk handling as the Python oracle:
- Tokenizer strips line + block comments preserving line numbers.
- Numbers: signed int/float, scientific notation, decimal-only and
  exponent-only forms accepted.
- Hyphen-stitch via instance-name reader: scans ahead through
  consecutive IDENTs until LBRACE/UUID, concatenating.
- Material reference blocks (`{Name}` with no interior `;`):
  consumes all non-RBR tokens between braces and concats — natural
  hyphen-stitch fallthrough.

Public API: `xfile_parse(data, len, path) → xfile_t*`,
`xfile_free(xfile_t*)`. `xfile_t` owns its sub-arrays; partial data
preserved on parse error (caller still must free). Memory model: lots
of small mallocs with paired frees in `xfile_free`. ASan-clean across
the full corpus.

Tests (9, all pass):
1. bad_header (16+ bytes with bad magic)
2. empty (header only)
3. bare Mesh{} (3 verts, 1 triangle)
4. Mesh + MeshNormals + MeshTextureCoords + MeshMaterialList with
   referenced + inlined Materials
5. Frame hierarchy + FrameTransformMatrix + DFS ordering
6. Hyphen-stitch round-trip (`PDX02_-_Default` → `PDX02__Default`
   both sides)
7. Vendor ice01.x pinned to same numbers as Python oracle
8. Vendor xfile/ corpus walk: all 223 parse clean
9. Vendor xfile2/ corpus walk: all 19 parse clean (skinning/animation
   silently skipped)

### C4 — D3D8-ready mesh build (`src/mesh.{c,h}`, commit `d3bf126`)

`mesh_build_from_xfile(xfile_t*) → mesh_t*` flattens the per-Mesh{}
data into a single (vertices, indices, materials, submeshes) tuple.
Each submesh = one (Mesh{} block, material) pair so the renderer can
SetMaterial+SetTexture then DrawIndexedPrimitive on a contiguous
index range — matches the engine's D3DX attribute-table model
without reimplementing ID3DXMesh.

Vertex layout: FVF 0x152 (XYZ + NORMAL + DIFFUSE + TEX1, 36 B per
vertex) — same FVF the engine's D3DXLoadMeshFromXof produces (the
literal 0x152 compare at FUN_00472836:350).

Triangulation: fan (0,i,i+1) per face. No welding pass — 3 expanded
vertices per triangle (3 unique (pos, normal, uv) tuples per tri).
Simple, visually correct, slight memory overhead vs welded. Corpus
totals: 261,087 expanded verts / 87,029 faces / 3041 submeshes.

`mesh_compute_bounds`: centroid + max-radius pass, mirrors
FUN_004aaad7. Idempotent.

`mesh_upload_d3d8` (Win32-only, behind `#ifdef _WIN32`):
CreateVertexBuffer + CreateIndexBuffer (managed pool, write-only)
+ Lock + memcpy. Not unit-tested — verified visually at render time.

Known TODOs deferred to C7:
- Frame transforms not pre-applied (vertices in Mesh-local space).
  Most shipping files have identity Frame transforms in the .x
  itself; positions come from external level/stage data — fine for
  AAB/C0A unblock.
- Per-vertex MeshVertexColors not consumed (white diffuse).
- Material ref-then-inline order assumed in MeshMaterialList
  (matches ice01.x exporter).

Tests (5, all pass):
1. empty xfile builds empty mesh
2. single triangle: 3 verts / 3 indices / 1 submesh / Red material
3. bounds_cube: 8 verts → centroid at origin, radius == sqrt(3)
4. vendor ice01.x: 30 faces → 90 verts, 1 submesh,
   Material__25 with w_ice.bmp, radius ≈ 28
5. vendor xfile/ corpus walk: all 223 build clean

### Next (C5)

`mesh_load` orchestrator equivalent to FUN_00472836. Adds:
- Path resolution + the `_s.x` "quality 1" variant + the
  `xfile2/`-or-fallback path resolution from `DAT_005c8400`.
- Global texture-name dedupe cache at `&DAT_073be908` (process-wide;
  shared across all `mesh_load` calls).
- 10 per-texture mode-flag byte side-tables at `&DAT_073cb10c..814`
  driven by texture-name prefix scans (water / shop_jutan / `_a` /
  `_s` / 6 more).
- `FUN_00471b24` equivalent (texture-load wrapper) wiring our
  `sprite_load`.

The texture-name flag matching is the engine's most arbitrary-looking
logic — natural place to do Frida-harness validation against the
retail exe per user ask 2026-05-23. Plan: hook FUN_00472836 entry
+ exit, capture (material count, texture filename list, per-texture
side-table bytes) per .x file, diff against our C output across the
242-file corpus.

## 2026-05-23 — mesh loader: survey + strategy doc (`docs/findings/mesh-loader.md`)

Opening chip on the FUN_00472836 family — the .x text-format mesh loader
that's been the Mt. Everest blocker on scene_walls AAB
(FUN_0046bf38) and scene_floor/jutan C0A (FUN_004748f8) worker-thread
bodies, and ultimately on visible scene-1 INGAME geometry. No code
landed yet; this chip just documents what we found and the path we'll
take.

### Architecture (in the engine)

- **`FUN_00472836`** (1609 B) — orchestrator. Path build → DirectXFile
  walk → per-material copy / texture dedupe / sprite_load → bounds →
  FVF clone.
- **`FUN_004c8f74`** (704 B) — d3dxof.dll dynamic load,
  `DirectXFileCreate`, registers two large custom-template decl
  blocks (XSkinMeshHeader, VertexDuplicationIndices, FaceAdjacency,
  SkinWeights, Patch, PatchMesh, FVFData, PMAttributeRange,
  PMVSplitRecord, PMInfo), then walks top-level templates.
- **`FUN_004c8baa`** (970 B) — recursive Mesh/Frame/Matrix
  dispatcher. Calls `FUN_004c75e3` for Mesh, recurses for Frame,
  multiplies for FrameTransformMatrix.
- **`FUN_004c75e3`** (4634 B) — engine's RE'd `D3DXLoadMeshFromXof`
  clone. The biggest single chunk in the family.
- **`FUN_00471b24`** (467 B) — texture-load wrapper (sprite_load
  equivalent for materials).
- **`FUN_004aaad7`** (278 B) — bounds (centroid + max radius).

Total ~8400 bytes of engine code.

### Toolchain availability (mingw-w64-i686 13.0.0)

- `<dxfile.h>` + `libd3dxof.a` — DirectXFile available.
- `libd3dx8d.a` only — **no D3DX8 headers**, so we can't link
  `D3DXLoadMeshFromXof` directly.
- D3DX9 exists but targets D3D9 device interfaces, useless to us.

### Strategy decision: custom text parser, no D3DX

- Skip d3dxof + D3DX8 entirely. Write a pure-C `.x` text parser.
- Upload to raw `IDirect3DVertexBuffer8` / `IndexBuffer8` directly,
  no `ID3DXMesh` wrapper.
- Static meshes only at first — `xfile/` (223 files, 17 MB, no
  skinning, just Mesh / MeshNormals / MeshTextureCoords /
  MeshMaterialList / Material / TextureFilename / Frame /
  FrameTransformMatrix). Skinning + animation in `xfile2/` (19
  files, 40 MB) defer to character-rendering work months out.
- Trade-off: not byte-identical to FUN_004c75e3 (4.6 KB of D3DX8 we
  skip). Acceptable per `openrecet_constraints.md` — project goal
  is drop-in, not byte-identical.

### Corpus survey (via existing `tools/extract/xfile.py --scan`)

- 100% of files are `xof 0303txt 0032` (no bin/tzip/bzip variants).
- 242 files, 57 MB total. 1.6 M lines of text.
- Top templates in `xfile/`: Material 5780, TextureFilename 2734,
  FrameTransformMatrix 2610, Frame 2578, MeshMaterialList 2347,
  MeshNormals 2347, Mesh 2323, MeshTextureCoords 2071,
  MeshVertexColors 1860.
- `xfile2/` adds SkinWeights 210, XSkinMeshHeader 35, Animation 485,
  AnimationKey 1455, AnimationSet 12.

### Chip plan (smallest-first)

1. **this doc** ← here.
2. Python parser oracle (`tools/extract/xfile.py` flesh-out) — emit
   per-file JSON (vertices/faces/materials/textures/transforms).
   Validate format quirks across all 242 files.
3. C parser skeleton `src/xfile.{c,h}` — static-mesh only, tested
   against the Python oracle on 223 `xfile/` files.
4. `src/mesh.{c,h}` D3D8 upload — vertex/index buffers, FVF 0x152,
   bounds, sprite_load integration.
5. `mesh_load` orchestrator (FUN_00472836 equivalent) — texture
   dedupe global + per-texture mode-flag side-tables.
6. Wire AAB + C0A worker bodies via `mesh_load`.
7. Scene-1 render path — Mt. Everest, ports as separate roadmap
   items.

Full discussion + struct layouts + parser grammar in
`docs/findings/mesh-loader.md`.

## 2026-05-23 — scene_buy: B13 secondary inner-body wired (page-indexed)

Sixth of the 9 secondary worker-thread inner bodies — sibling to AE8
(landed earlier today). `src/scene_buy.{c,h}` extended to handle BOTH
bodies; per-page state promoted from page-0-only globals to 50-element
per-page arrays.

### What changed

`g_scene_buy_page0_valid` / `_count` / `_names` / `_sprites` removed in
favour of:

- `g_scene_buy_current_page` — engine `DAT_0730b56c` (selector read by
  B13). Range [0, 50); engine also uses -1 as "no page" sentinel.
- `g_scene_buy_valid[50]`
- `g_scene_buy_count[50]`
- `g_scene_buy_names[50][10][256]`  (125 KiB BSS)
- `g_scene_buy_sprites[50][10]`     (8 KiB BSS)

AE8 still reads page 0 unconditionally; B13 reads
`g_scene_buy_current_page`. A new `scene_buy_page_dispatch` helper
factors the shared dynamic loop.

### B13 body (FUN_0047333b @ 0x47333b, 145 bytes)

Single-phase: same as AE8's phase 1 but page-indexed. Gated on
`(valid[page] != 0 && count[page] != 0)`. Iterates `count[page]` times
reading from `names[page]` → `sprites[page]`. Dims 0x200×0x200. Engine
sprintf format `bmp/%s` (.rdata @ 0x5c8680 — different address from
AE8's 0x5c864c, same literal). Engine sprite_load format flag 0x11
(dropped). **No singletons** (unlike AE8).

### Out-of-range page handling

Engine reads `(&DAT_06a63bdc)[page * 0xb19c]` with NO bounds check —
would OOB for page = -1 or page >= 50. Port clamps via
`scene_buy_page_dispatch` (page out of range → 0 dispatches, no-op).
Tests cover -1, 50, and 9999 → all no-op.

### Inner-body call shape

LAB_00452b13 (objdump @ 0x452b13..0x452b3e): bare `call 0x47333b` with
NO pre-arg push — same shape as AE8. Confirmed via disassembly.

### Wiring

`scene_buy_init` now registers BOTH bodies in one call:
`worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AE8, scene_buy_ae8_body)`
+ `(...SEC_BODY_B13, scene_buy_b13_body)`. main.c's init comment
updated. Two distinct Win32 wrappers: AE8 dispatches via
`sprites[0][slot]` + the two singletons; B13 dispatches via
`sprites[current_page][slot]` only.

### Validation

- `make -C tests run` → 782 passed (+10 new B13 tests; AE8 tests
  refactored to use the per-page array API: 10 → 11 AE8 tests,
  including a new `_ae8_ignores_current_page_selector` that pins the
  AE8/B13 distinction)
- `make -C src` builds both `openrecet.exe` + `openrecet-debug.exe`
- `tools/scenario-test.py boot-idle` → 3/3 bit-exact
- `tools/scenario-test.py title-z-press` → 14/14 bit-exact

No visual change vs prior commit (both bodies dormant — no caller
wired). worker_load.h banner updated to mark B13 as WIRED.

### Deferred

- Per-page state writers (buy-phase customer arrival code) — not
  reverse-engineered yet; lands with the buy-phase scene loader.
- AAB / C0A / C96 are the remaining 3 NULL secondary inner-bodies.
  AAB + C0A both need FUN_00472836 (.x mesh loader, 1609 bytes) first;
  C96 is the world-map state machine (2067 bytes).

## 2026-05-23 — scene_buy: AE8 secondary inner-body wired (buy-phase loader)

Fifth of the 9 secondary worker-thread inner bodies to land —
`src/scene_buy.{c,h}` ports `FUN_0047329b` (151 bytes) end-to-end and
registers it as `WORKER_LOAD_SEC_BODY_AE8`. Structurally distinct from
the wall/floor/jutan/pause group: instead of a fixed N-entry .rdata
table, AE8 walks a runtime name buffer (page 0) for a dynamic count of
items, then dispatches two fixed singletons unconditionally.

### Three-phase body

1. **Dynamic per-item icon loop (page 0)** — gated on `(valid != 0 &&
   count != 0)`. Iterates `count` times reading 256-byte names from
   the per-page name buffer at `&DAT_06a5ead4`, formats `bmp/<name>`,
   and dispatches each to a sprite slot in `&DAT_073aa7e8` (stride 0x10
   = sprite_t). Dims `0x200×0x200`. Engine sprintf format `bmp/%s`
   (`.rdata @ 0x5c864c`); engine sprite_load format flag `0x10`
   (dropped — openrecet sprite_load doesn't carry format flags).

2. **Fixed `bmp/ivent/chrname.tga`** → `g_scene_buy_chrname`
   (`DAT_073cc8d0`), dims `0x200×0x200`. Always fires.

3. **Fixed `bmp/shopmode.tga`** → `g_scene_buy_shopmode`
   (`DAT_073a9580`), dims `0x400×0x200`. Always fires.

### Page-0 scope

AE8 only reads **page 0** of the per-page state — does NOT consult
`DAT_0730b56c` (current-page selector). The B13 sibling (FUN_0047333b,
next chip) is the page-indexed variant; this chip exposes page-0
state as standalone globals (`g_scene_buy_page0_valid` /
`g_scene_buy_page0_count` / `g_scene_buy_page0_names[10][256]`) and
the B13 follow-up will promote them to 50-element arrays.

### Slot count + overflow

Sprite-array per-page stride is 0xa0 bytes = 10 sprites/page
(`SCENE_BUY_SLOT_COUNT`). Engine has no bounds check; counts above 10
overflow into adjacent pages' sprite memory. Port clamps the dynamic
loop at 10 for memory safety; tests cover the clamp behaviour
(`scene_buy_ae8_dynamic_loop_count_overflow_is_clamped`).

### Inner-body call shape

LAB_00452ae8 (objdump @ 0x452ae8..0x452b13) just `call 0x47329b` with
NO pre-arg push — argument-less call, unlike B3E/B82/BC6/C0A which
push literal `1` first. Confirmed via disassembly; no fidelity issue
to fix in our port.

### Wiring

`main.c` calls `scene_buy_init(g_dev)` after `sysassets_load_all`,
before the wall/floor/jutan/pause inits. Caches the D3D device and
registers the body via `worker_load_set_sec_body(
WORKER_LOAD_SEC_BODY_AE8, …)`. Dormant until something calls
`worker_load_spawn_d3e(0)` — buy-phase scene transition will do this
once it ports.

### Validation

- `make -C tests run` → 772 passed (+12 new scene_buy tests)
- `make -C src` builds both `openrecet.exe` + `openrecet-debug.exe`
- `tools/scenario-test.py boot-idle` → 3/3 bit-exact
- `tools/scenario-test.py title-z-press` → 14/14 bit-exact

No visual change vs prior commit (body dormant — no caller wired).

### Deferred

- B13 sibling (FUN_0047333b) — page-indexed variant; next chip.
  Will promote `g_scene_buy_page0_*` to per-page arrays.
- Per-page state writers (buy-phase customer arrival code) — not
  reverse-engineered yet; lands with the buy-phase scene loader.

## 2026-05-22 — scene_walls: B3E secondary inner-body wired (wall asset loader)

First of the 9 secondary worker-thread inner bodies to actually land —
`src/scene_walls.{c,h}` ports `FUN_0047474e` (142 bytes) end-to-end and
registers it as `WORKER_LOAD_SEC_BODY_B3E`. The body is functionally
dormant: no caller invokes `worker_load_spawn_d85()` yet (waits on the
scene-1 stage transition to port), but the registration plumbing is
proven by the new unit tests + non-regression on existing scenarios.

### Module

`src/scene_walls.{c,h}` — 142-byte engine fn collapsed to a 15-iteration
loop with a 1-bit predicate inverted by `param`:

- `param == 0` → load ONLY the slot whose index matches the per-stage
  wall selector (engine `*(int *)(&DAT_0451057c + DAT_0438b1e0 * 0x2dfc8)`).
  "Load the destination room's wall."
- `param != 0` → load every slot EXCEPT the selector. "Load all other
  variations for snappier room changes."

Selector is exposed as a single int32 (`g_scene_walls_selector`, BSS-zero
default) until the stage-state record (0x2dfc8 stride) ports. Range
check is implicit: out-of-range selector (e.g. boot-default 0 is in
range; -1 or 15+ is out of range) means "no slot matches" — `param=0`
loads nothing, `param=1` loads everything.

### Filename table

15 entries extracted from `vendor/unpacked/recettear.unpacked.exe` via
`tools/analyze/pe.py str 0x005ca11c..0x005ca200`:

```
kabe_sikkui.bmp, kabe_ita.bmp, kabe_hosi.bmp, kabe_umi.bmp,
kabe_moru.bmp, kabe_renga.bmp, kabe_giseki.bmp, kabe_8bit.bmp,
kabe_jya.bmp, kabe_iseki.bmp, kabe_euria.bmp, kabe_namako.bmp,
kabe_chuka.bmp, kabe_kouhaku.bmp, kabe_check.bmp
```

Engine sprintf format `xfile/wall/%s` (`.rdata` @ 0x5ca210) — `xfile/`
prefix is shared with the engine's .x mesh tree even though wall assets
are BMPs.

### Test injection

The pure-C `scene_walls_load_with(load_fn, userdata, param)` takes a
test-replaceable load callback (just `(path, slot, userdata)` — no
sprite_t in the signature, so the test build is portable without d3d8).
On Win32, the body wraps `sprite_load` against `g_scene_walls[slot]`
(15-entry sprite array). Pre-existing 699-test suite + 12 new tests
all pass (711 total).

### Wiring

`main.c` calls `scene_walls_init(g_dev)` once at boot, right after
`sysassets_load_all`, which caches the device and registers the body
via `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B3E, …)`.

### Banner update

`src/worker_load.h` per-slot inner-body table now marks B3E as **WIRED
— see src/scene_walls.{c,h}**. The other 8 slots stay NULL until their
scene loaders (FUN_0046bf38, FUN_0047329b, FUN_0047333b, FUN_004747dc,
FUN_0047486a, FUN_004748f8, FUN_00473a3e, FUN_0049de20+FUN_004735ad)
port.

### Validation

- `make -C tests run` → 711 passed (+12 new scene_walls tests)
- `make -C src` builds both `openrecet.exe` + `openrecet-debug.exe`
- Boot smoke clean (4 s; full table-loader log unchanged)
- `tools/scenario-test.py boot-idle` → 3/3 bit-exact
- `tools/scenario-test.py title-z-press` → 14/14 bit-exact

No visual change vs prior commit (B3E body is dormant — no spawner
caller wired).

## 2026-05-22 — worker_load secondary inner-body docs + post-body fidelity fixes

Decoded each of the 9 LAB_00452* secondary thread-proc inner bodies via
objdump (Ghidra missed them as code labels inside the asset-load worker
region). Recorded the call-target map in `src/worker_load.h`'s banner so
the future scene-1 port knows what to register for each
`worker_load_set_sec_body(slot, cb)`. While verifying I found two
dormant fidelity drifts in the existing post-body switch — both fixed
here.

### Inner-body call targets (objdump @ 0x452aab..0x452cdd)

| slot | LAB         | engine inner-body call(s)                                        |
|------|-------------|------------------------------------------------------------------|
| AAB  | 0x452aab    | `FUN_0046bf38()` — sc1 inventory/chrname/icon loaders            |
| AE8  | 0x452ae8    | `FUN_0047329b()` — buy phase: per-entry + chrname + shopmode     |
| B13  | 0x452b13    | `FUN_0047333b()` — buy phase alt, per `DAT_0730b56c` page        |
| B3E  | 0x452b3e    | `FUN_0047474e(1)` — wall asset loader (param=1 inverts predicate)|
| B82  | 0x452b82    | `FUN_004747dc(1)` — floor asset loader                           |
| BC6  | 0x452bc6    | `FUN_0047486a(1)` — jutan (rug) asset loader                     |
| C0A  | 0x452c0a    | `FUN_004748f8(1)` — table asset loader                           |
| C4E  | 0x452c4e    | unnamed @ 0x435873 (FPU state init) + `FUN_00473a3e()` (pause/status assets) |
| C96  | 0x452c96    | `FUN_0049de20()` (world-map state machine) + `FUN_004735ad()` (world-map BMPs) |

All 12 targets are scene-1 (INGAME) specific — they'll wire up when the
respective scene-1 loaders port. None of the inner bodies port today;
the slots stay NULL by default.

### Fidelity fixes (dormant — no caller invokes these spawners yet)

- **Fade-kick polarity** (b3e/b82/bc6/c0a/c4e/c96, `worker_load_sec_post_body`).
  Engine pattern at e.g. 0x452b57:
    ```
    cmp [DAT_06a49980], esi    ; esi = 1
    ...
    jne SKIP_FADE
    fade_phase_out_start(0, 0x11)
  SKIP_FADE:
    ```
  `jne` is "jump if not equal", so the fade-kick is the **fall-through**
  branch — engine fires fade when `param == 1`, not `param != 1` as the
  port had. Both the code and the header banner were inverted; both
  flipped here.

- **aab audio reset** (`g_worker_sec_state_audio` in `WORKER_LOAD_SEC_BODY_AAB`).
  Engine assembly @ 0x452abd-0x452ad8:
    ```
    push $0x1 ; xor eax,eax ; pop esi    ; esi=1, eax=0
    mov eax,[handle]                      ; handle=0
    push eax                              ; push 0 as FUN_00499579 arg
    ... (zero busy_sec, now_sec; state_1c8=1)
    call FUN_00499579                     ; DAT_09643120 = 0
    ```
  Engine XORs eax to zero **before** pushing it as the arg, so
  `FUN_00499579(0)` → `DAT_09643120 = 0`. This RESETS the audio LFO
  context (read by `FUN_0049966a`'s `DAT_09643120 == 0` clause), it
  doesn't raise it. Port had `audio = 1`; flipped to 0.

### What landed

- **`src/worker_load.c`** — `worker_load_sec_post_body` switch updated:
  aab audio write 1 → 0; 6 fade-kick gates inverted `!= 1` → `== 1`.
  Per-case comments updated with engine asm refs.
- **`src/worker_load.h`** — banner gains the inner-body call-target
  table; fade-kick + audio polarities corrected; state-bytes
  description for audio updated.
- **`tests/test_worker_load.c`** — `aab` audio expected = 0; `b3e/b82/
  bc6/c0a/c4e/c96` fade-kick expected on `param==1` (test names + body
  arg latches updated accordingly); full-cycle simulations now pass
  `param=1` to trigger the fade-kick branch. **699 tests pass**
  (unchanged count — 4 tests renamed, 0 added/removed).
- **`tests/test_main.c`** — registry updated for two renamed tests.

### Verified

- `make -C tests run` → 699/699 pass.
- `make -C src` clean.
- `tools/scenario-test.py title-z-press` → 14/14 bit-exact (no
  regression on the only path that currently touches worker_load).

### Still deferred (unchanged)

- The 9 secondary inner-body callbacks themselves (now documented in
  the banner — each scene-1 loader registers its slot when it ports).
- FUN_0046c01e (d07's pre-spawn) — register via
  `worker_load_set_sec_d07_pre_spawn` when it ports.
- Render-side counter pump at FUN_004547ab L51055.
- Nowloading gate split (fidelity follow-up; still dormant since no
  secondary spawner is called yet).

## 2026-05-22 — sim guard wires worker_load to the loading overlay

Wires the per-tick "if asset-load worker is done, drop the Now Loading
overlay" behavior into the sim loop. The worker_load module had been
fully ported across three earlier chips today but its `g_worker_busy`
flag wasn't observed anywhere — the overlay gate stayed raised forever
after the first spawn. This chip closes that loop.

### What landed

- **`sim_loading_pump` / `sim_loading_pump_pure`** in `src/sim.{c,h}`:
  port of FUN_004532df (129 bytes @ 0x4532df). Four scene-effect
  counters (DAT_06a49990/94/98/9c) + one mode flag (DAT_06a499a0)
  pumped every frame the worker is busy. All five are BSS-zero on
  init; counters only advance once their starter (FUN_004532b1 etc.,
  unported) writes a positive value. Today they sit dormant — ported
  in this chip so the sim-loop guard matches the engine's control
  flow shape, ready for scene-1 render to start consuming them.
  - 990: cyclic 1..0x1f, wraps to 0 at 0x20.
  - 994: cyclic 1..(threshold-1), threshold latched by FUN_004532bc.
  - 998 mode==0: cyclic 1..0x13.
  - 998 mode!=0: monotone with ceiling 0xc.
  - 99c: pumped by FUN_004547ab (render side), not from here.

- **Per-tick busy guard** at the top of `sim_step_a`:
  ```
  font_age_tick();                  // L50362 — runs unconditionally
  if (worker_load_busy()) {         // L50363
      sim_loading_pump();           // L50364 — scene-effect counter tick
      return;                       // L50365 — skip rest of sim
  }
  nowloading_set_active(0);         // L50367 — drop the overlay gate
  ... button ring + scene dispatch + fade_tick + frame++ ...
  ```

  Two effects:
  - **Input + scene sim freeze during loading.** Button ring stops
    advancing, scene dispatch is skipped, `g_sim_frame_count` does
    not advance. Matches the engine's "no interaction while loading"
    behavior.
  - **Overlay drops the tick after the worker thread finishes.** On
    Win32, `worker_load_thread_proc` completes within milliseconds
    of CreateThread (case-1 INGAME loader callback is unregistered →
    immediate cleanup); the very next sim tick reads busy=0 and
    clears `nowloading.g_active`.

- **font_age_tick reorder.** The engine calls FUN_0047c29d at L50362,
  *before* the busy check and *before* the button ring. The prior port
  ran it after the button ring (a wrong-order port from the first
  font landing). Corrected here — glyph cache aging now ticks during
  the loading screen too, matching the engine.

- **`sim_init` clears the pump state.** All 5 counters + threshold94
  + mode reset to 0.

### Tests

10 new tests under `tests/test_sim.c` (689 → 699):
- `sim_loading_pump_pure` cold-start no-op (all-zero in, all-zero out).
- 990 cycles 1..0x1f then wraps.
- 994 wraps at threshold; threshold=0 special case (immediate wrap).
- 998 mode==0 cycles to 0x13 then wraps.
- 998 mode!=0 clamps at 0xc (monotone).
- Module-level `sim_loading_pump` drives globals.
- `sim_init` zeros all counter state.
- `sim_step_a` busy → pump fires, ring frozen, frame count NOT advanced.
- `sim_step_a` idle → nowloading gate cleared on the very tick busy
  drops to 0.

`test_sim_step_a_advances_frame_count` + `test_sim_step_a_pipes_input_into_ring`
also gained `worker_load_reset()` calls in setup — sim_step_a now
depends on worker_load state, and the existing tests would have been
fragile against cross-test contamination.

### Smoke + regressions

- 699/699 unit tests pass (was 689).
- title-z-press scenario re-blessed (10 frames re-captured): the
  Now Loading overlay now correctly drops between frames 85 and 90
  in our build, where the prior goldens had it raised through frame
  115. Frames 73/74/80/85 remain bit-exact pass (overlay still up
  during these — worker thread hadn't finished yet). 14/14 across
  3 stability runs at exact same pixel-diff counts → timing is
  deterministic under turbo mode.
- boot-idle (3/3), title-down-press (4/4) re-pass bit-exact.
- title-options (4 captures) has 479 px diff at frames 39/60 — that's
  a pre-existing regression on this branch (the audio slider in the
  golden shows "5", current local recet.ini state writes "9";
  reproduces identically against `master`); unrelated to this chip.

### Engine fidelity notes

- The engine pumps FUN_004532df TWICE per frame during loading: once
  in sim (busy branch, FUN_004536cb L50364) and once in render
  (FUN_004547ab L51055 — unconditional). Outside loading it's once
  per frame from render only. We port only the sim-side call; the
  render-side pump is observably inert (no scene-effect counter has
  a render consumer today) and will land with FUN_004547ab.
- The engine's gate-clear is `DAT_06a49958 = 0` (primary nowloading
  gate only). The secondary gate (DAT_06a49960) is cleared by its
  thread procs' cleanup tails, not from here. Our `nowloading.g_active`
  collapses both into one boolean; calling `nowloading_set_active(0)`
  here clears the collapsed bit. That's a fidelity gap that only
  bites if a secondary worker is in flight while the primary is not,
  which doesn't happen in the vendor exe's call paths today
  (secondary spawners aren't called from anywhere yet — they unlock
  as scene loaders port). A proper split lives in a follow-up chip.

## 2026-05-22 — Asset-load worker thread (secondary family, second half)

Completes the worker_load module's "second half" — the 8 secondary
spawners + 9 secondary thread procs that the prior two chips
(`worker_load: asset-load worker thread`, `worker_load: alt primary
worker`) explicitly deferred. The primary worker dispatches on
`g_scene_state` via a 17-entry jump table; the secondary family is a
zoo of 8 named spawn entries, each with its own pre-spawn writes,
post-body cleanup, and (six of nine) conditional fade-kick.

### What landed

- **8 secondary spawn entry points** in `src/worker_load.{c,h}`:
  `worker_load_spawn_d07/d3e/d85/dc1/dfd/e39/e75/eb1`. Each ports its
  matching engine FUN_00452XXX (28-78 bytes per spawner @ 0x452d07
  through 0x452eb1). Shared shape: optional per-kind "pending=2" state
  byte write, raise secondary gates (4995c+49960) via
  `worker_load_begin_secondary`, latch param into DAT_06a49980,
  CreateThread on the picked thread proc. The d3e spawner sub-dispatches
  between LAB_00452ae8 (param==0) and LAB_00452b13 (param!=0).
  FUN_00452d07 alone has a pre-spawn hook (engine calls FUN_0046c01e
  before CreateThread) — exposed as `worker_load_set_sec_d07_pre_spawn`.

- **9 secondary thread proc bodies** factored into:
  - One shared Win32 thread-proc helper (`worker_load_thread_proc_sec`)
    that takes a `body_id` and does: dispatch_sec_pure → cleanup tail →
    sec_post_body → return 1.
  - Nine thin wrappers (`thread_proc_sec_aab` through `thread_proc_sec_c96`)
    that pin the body_id for each `LAB_00452*` entry.
  - One shared `secondary_thread_cleanup` (close handle, zero handle,
    zero 4995c, zero 49960 — same shape as the engine's per-LAB_* tail,
    distinct from FUN_00452917's gated three-flag wipe).

- **Per-LAB_* post-body machinery** in `worker_load_sec_post_body` — a
  switch on `body_id` reproduces each LAB_*'s tail-specific writes:
  - `AAB` → DAT_0438b1c8=1, DAT_06a49984=1, DAT_09643120=1
    (last via the inlined FUN_00499579(1)). No fade-kick.
  - `AE8` / `B13` → DAT_0438b1cc=1. No fade-kick.
  - `B3E` / `B82` / `BC6` / `C0A` → DAT_0438b1d4=1, fade-kick if
    DAT_06a49980 != 1.
  - `C4E` → DAT_0438b1d0=1, fade-kick.
  - `C96` → DAT_0438b1d8=1, fade-kick.

  Fade-kick is `fade_phase_out_start(0, 0x11)` (FUN_0045281c, already
  ported in `src/fade.c`).

- **9 inner-body callback slots** + their getter/setter pair
  (`worker_load_set_sec_body(body_id, cb)`). All slots default NULL —
  the per-LAB_* "scene work" calls (FUN_0046bf38, FUN_00473*, FUN_00474*,
  etc.) aren't ported yet, so the bodies are no-ops until consumers wire
  in. The cleanup + post-body machinery still fires either way.

- **7 named per-kind state byte globals** exposed for observability:
  `g_worker_sec_state_1c8/1cc/1d0/1d4/1d8/984/audio` (with `audio`
  serving as DAT_09643120, written via the engine's FUN_00499579(1)
  call). Plus `g_worker_sec_param` (DAT_06a49980) for the fade-kick
  gate readers.

- **Non-Win32 spawn stubs** for the 8 spawn entry points — gates-only
  shape (mirrors how `worker_load_spawn`/`spawn_alt` already split).
  Pending-flag writes and param latching are observable from tests
  even though no thread runs.

### Tests

27 new tests under `tests/test_worker_load.c` (662 → 689):
- Body slot registration: count=9, set/get round-trip, out-of-range
  guard, NULL clear, last-write-wins.
- d07 pre-spawn round-trip.
- begin_secondary/end_secondary gate transitions.
- dispatch_sec_pure: registered cb, unregistered no-op, out-of-range.
- sec_post_body: each LAB_*'s state writes + fade-kick gate (per-body
  param!=1 → fade triggered; param==1 → suppressed).
- 8 spawn entry points: per-kind pending flag write, gate raise, param
  latch, d07's pre-spawn invocation.
- 3 full-cycle simulations end-to-end (d85→B3E with fade-kick, e75→C4E
  with 1d0 ready, d07→AAB with no fade-kick + three-flag aab writes).
- Reset zeroes all secondary state.

### Engine cross-references

Caller mapping from `docs/decompiled/all.c`:
- `FUN_00452d07` — 9+ callers (most "background load" sites).
- `FUN_00452d3e` — 2 callers in scene transitions.
- `FUN_00452d85/dc1/dfd/e39` — single iVar6-keyed dispatch at line
  86961, picking 1/2/3/4 → dc1/d85/dfd/e39 (i.e. the four `1d4`
  spawners share one caller).
- `FUN_00452e75 / FUN_00452eb1` — no callers found in decomp. Treated
  as dead code in the vendor exe; ported for completeness with the
  same shape as their siblings.

### Deferred (still part of the wider worker-system port)

- Inner-body callbacks for the 9 LAB_*'s — register via
  `worker_load_set_sec_body` as each scene's loader/post-load code
  ports (FUN_0046bf38, FUN_0047329b, FUN_00473c15, FUN_004746fc, etc.).
- FUN_0046c01e (d07's pre-spawn) — register via
  `worker_load_set_sec_d07_pre_spawn` when that lands.
- Per-tick clear of DAT_06a49958 at top of FUN_004547ab. Still a
  render-dispatch concern; unaffected by this chip.

### Smoke + regressions

- 689/689 unit tests pass.
- title-z-press scenario: 14/14 frames bit-exact.
- Both `build/openrecet.exe` and `build/openrecet-debug.exe` link
  cleanly via the Win32 build.

## 2026-05-22 — Asset-load worker thread (alt primary + close fidelity)

Follow-up chip to the first-half worker landing earlier today. Ports
the alt primary worker (FUN_00452eed + LAB_00452a6b) — sibling of the
already-ported FUN_00452cde + LAB_0045293d that shares the same primary
gates but runs a fixed 5-call body instead of jump-table dispatch — and
closes the close-helper fidelity gap that the first-half chip left open.

### What landed

- **`worker_load_spawn_alt`** — ports FUN_00452eed (41 bytes @ 0x452eed),
  structurally identical to FUN_00452cde but targeting the alt thread
  proc (LAB_00452a6b) instead of LAB_0045293d. Same primary gates raised
  (DAT_06a49954 busy + DAT_06a49958 nowloading); same CreateThread call
  shape.

- **`worker_load_thread_proc_alt`** (Win32) — ports LAB_00452a6b body
  (~74 bytes @ 0x452a6b). The engine's body is a fixed sequence:

  ```
  if (DAT_06a4996c == 0) {
      FUN_0047472c();  // pre-room-change A
      FUN_00474681();  // pre-room-change B
  }
  FUN_004746fc();
  FUN_00473c15();
  FUN_00436f97();
  <primary cleanup: close handle, busy=0, return 1>
  ```

  Collapsed into a single registered callback (`worker_load_set_alt_cb`)
  — the scene module that owns the body decides internally whether to
  short-circuit on the `DAT_06a4996c` "same room" flag. Same shape, scene
  logic stays in scene-land.

- **`worker_load_dispatch_alt_pure`** — pure-C side of the alt thread
  proc, invoked by the Win32 thread proc and by unit tests directly.
  Always returns 1 (engine LAB_00452a6b never short-circuits — no input
  to range-check).

- **`primary_thread_cleanup`** helper — extracted from the inline tail
  of both LAB_0045293d and LAB_00452a6b (engine literally repeats the
  identical 4-instruction tail at both labels). Close handle, clear
  primary busy, leaves secondary flags alone. The new alt proc and the
  pre-existing primary proc share it now.

- **Close-helper fidelity fix** — `worker_load_close` now also clears
  the secondary flags (DAT_06a4995c + DAT_06a49960) when the handle is
  non-NULL, exactly matching FUN_00452917's three-flag wipe. The
  first-half chip explicitly deferred this with `// we don't have those
  yet, so omitted` — they exist now (declared in worker_load.c, even
  though no spawner writes them yet). The secondary nowloading gate is
  served by nowloading.c's collapsed-OR `g_active` so we don't
  blanket-clear nowloading here; same observable as the engine when only
  one side is in flight, which the engine's call paths appear never to
  violate.

- **`worker_load_busy_secondary` accessor** — reads `DAT_06a4995c`.
  Returns 0 always for now (no spawner raises it yet), but exposes the
  final shape so any consumer wired today won't break when the
  secondary spawners port.

- **`worker_load_reset` extended** — now also clears the alt cb slot
  and the secondary busy flag, alongside the 17-slot table.

- **8 new unit tests** covering: secondary busy defaults to 0; alt cb
  round-trip + NULL-clears + last-write-wins; alt dispatch with a
  registered cb / without one (still returns 1) / busy-flag agnosticism;
  alt spawn on non-Win32 raises gates without dispatching; full alt
  cycle simulation matches the primary cycle (busy bounces, nowloading
  gate stays raised); reset clears the alt cb. **662 tests total (was
  654).** title-z-press scenario re-passes 14/14 bit-exact.

### Engine fidelity notes

- The engine's primary thread proc (LAB_0045293d) and alt thread proc
  (LAB_00452a6b) share an *identical* cleanup tail — the same four
  instructions repeated inline at both labels. We extract that into a
  static helper (`primary_thread_cleanup`) since it's verbatim shared.

- The engine's close-helper (FUN_00452917) clears only the secondary
  flags, not the primary. The contract appears to be "shut down any
  in-flight secondary worker; primary may still be running on a
  parallel transition". We match.

- DAT_06a4996c (the "same room" gate at the alt body's entry) is set
  by the alt's sole caller in the engine — a fade-driven room
  transition handler at the FUN_00452f16 surroundings. We don't need
  it inside worker_load: the registered alt cb is responsible for the
  internal skip-prelude decision.

### Deferred (the remaining "second half")

- **Eight DAT_06a49960-gated spawners** — the original session note
  listed six (FUN_00452d07 / d3e / d85 / dc1 / dfd / e39), but
  disassembly of the 0x4528d0..0x452f50 range turned up two more
  (FUN_00452e75 + FUN_00452eb1) past the close-helper. **Nine thread
  routines** (LAB_00452aab / ae8 / b13 / b3e / b82 / bc6 / c0a / c4e /
  c96) — original count was seven, plus the two newly-found at c4e
  and c96. Each thread proc clears all three flags
  (handle + secondary busy + secondary gate); several also call
  `FUN_0045281c(0, 0x11)` (fade kick) conditional on DAT_06a49980.
  Lands when any of those transition consumers ports.

- **Per-tick gate clear** at top of FUN_004547ab — "if `worker_busy
  == 0` then clear nowloading gate". Render-dispatch concern, lands
  with the scene-1 render port.

## 2026-05-22 — Asset-load worker thread (first half)

Worker-thread infrastructure for the scene-transition asset loader
lands. The engine spawns a one-shot worker on every cross-scene
transition that dispatches a per-scene loader callback against
`g_scene_state`; this chip ports the spawn + dispatcher + busy +
close machinery, leaving the per-scene loader callbacks unregistered
(every case is a no-op until each scene's loader ports).

Single file pair: **`src/worker_load.{c,h}` + `tests/test_worker_load.c`**.

### What landed

- **`worker_load_spawn`** — ports FUN_00452cde (41 bytes @ 0x452cde).
  Win32 build: raises busy + nowloading gates, then `CreateThread`
  on the internal thread proc which reads `g_scene_state`, calls
  `worker_load_dispatch_pure`, and cleans up. Non-Win32 unit-test
  build: raises the gates only (no real thread) so unit tests can
  observe the "busy + nowloading set" window without threading.

- **`worker_load_dispatch_pure`** — ports the 17-entry jump table
  at LAB_0045293d (~302 bytes @ 0x45293d). Engine table at 0x452a27
  isn't decompiled as a function (Ghidra leaves the LAB targets as
  raw bytes); decoded via `objdump` + a Python dword reader. Map:

  | case | target(s)                          | ported? |
  |-----:|------------------------------------|---------|
  |  0   | FUN_004733d5 + FUN_0049a3a3 (title)| no (callback) |
  |  1   | FUN_00474a9a + FUN_00436f97 (ingame)| no    |
  |  2   | FUN_0047355d                       | no      |
  |  3   | FUN_004736bd + FUN_0041edf1        | no      |
  |  4   | (engine no-op, jump to cleanup)    | n/a     |
  |  5   | FUN_0046c01e + FUN_0046bf38        | no      |
  |  6   | FUN_00473769                       | no      |
  |  7   | FUN_00473585                       | no      |
  |  8   | FUN_0049de20 + FUN_004735ad        | no      |
  |  9   | sub-dispatch on DAT_06a4997c       | no      |
  | 10   | FUN_0047347d                       | no      |
  | 11   | FUN_0045bdc2 + FUN_00473874        | no      |
  | 12   | (engine no-op, jump to cleanup)    | n/a     |
  | 13   | FUN_00473972                       | no      |
  | 14   | FUN_00473991                       | no      |
  | 15   | FUN_004739fb                       | no      |
  | 16   | FUN_004739dc                       | no      |

  Per-case wiring uses `worker_load_set_cb(N, fn)` registration so
  worker_load stays decoupled from scene-specific modules. Cases
  without a callback are no-ops — exactly matching the engine's
  cleanup-only behaviour at cases 4 and 12, and what we want for the
  14 other cases pending their loader ports.

- **`worker_load_busy`** — ports FUN_00452911 (6 bytes @ 0x452911,
  just `return DAT_06a49954`). Used by the engine at the top of
  FUN_004547ab to early-exit the per-tick render dispatch while a
  worker is still loading.

- **`worker_load_close`** — ports FUN_00452917 (38 bytes @ 0x452917).
  Closes the worker thread handle if any + zeros it. Idempotent.
  Engine also clears the secondary worker's busy + gate flags here;
  we don't have the secondary worker yet, so just the primary handle.

- **`scene_post_fade_init` re-wired** — the LOADING→INGAME transition
  now calls `worker_load_spawn()` instead of `nowloading_set_active(1)`
  directly. Same observable: the nowloading gate stays raised after
  the worker completes because the engine's per-tick "clear if worker
  done" lives at the top of FUN_004547ab (not ported yet). The
  `title-z-press` scenario re-passes 14/14 bit-exact across the
  transition window.

- **15 unit tests** covering: case count == 17, reset zeroes all
  state, begin raises both gates, end clears busy but NOT nowloading,
  callback round-trip + out-of-range guard + overwrite semantics,
  dispatch invokes registered cb / no-ops the unregistered slots /
  returns 0 for out-of-range scene_state, dispatch doesn't touch
  busy, close idempotent, spawn (non-Win32) only raises gates, and
  a full-cycle simulation of the thread proc body. **654 tests
  total (was 639).**

### Engine fidelity notes

- The engine has a latent race: `CreateThread` can return + the
  thread can start before the spawner assigns the handle to
  `DAT_06a49950`, so the thread's self-close may stale-read. Match
  preserves this; in practice real case-0..16 loaders take ms so the
  race never bites.

- The engine clears busy AFTER closing the handle (`andl $0x0` on
  `0x6a49950` THEN on `0x6a49954`). Our thread proc mirrors the order.

- Case 9's sub-dispatch on `DAT_06a4997c` (0/1/2/default) is treated
  as a single callback slot — when the case-9 loader ports, its
  callback will internally do the sub-dispatch. Same shape.

### Deferred (the "second half" of the worker system)

- **Six DAT_06a49960-gated spawners** (FUN_00452d07 / d3e / d85 / dc1
  / dfd / e39) + their **7 thread routines** (LAB_00452aab / ae8 /
  b13 / b3e / b82 / bc6 / c0a) — alternate worker family for
  non-loading scene transitions (dungeon-rest, etc.). Same close/busy
  machinery; lands when any of those transition consumers ports.

- **Alternate DAT_06a49958 worker** at FUN_00452eed + LAB_00452a6b —
  simpler routine (5 calls + cleanup) shared with the primary's
  busy/nowloading flags.

- **Per-tick gate clear** at top of FUN_004547ab — "if `worker_busy
  == 0` then clear nowloading gate". Lives in the render dispatcher,
  not the worker module. Until that ports, the gate stays raised
  after the worker completes — same observable as the previous
  `nowloading_set_active(1)` stub.

- **Per-case loader callbacks** — every case slot is unregistered.
  Each scene's loader port will end with a `worker_load_set_cb(N,
  scene_X_load)` line wired from the appropriate module init.

## 2026-05-22 — Save-back (FUN_004905a8 simplified) + settings persistence

Persistence loop closes: settings-menu slider changes now survive
across boots when the user opts in via `--save-write`.

Single commit, four pieces:

1. **`save_io_write_arena(primary, backup)`** — simplified port of
   FUN_004905a8(-1). Writes the in-memory arena (header + 100 banks)
   to both files unconditionally — matches the engine's
   no-atomic-temp+rename behaviour. The engine's full FUN_004905a8
   takes a slot index that triggers a "working-bank → arena bank"
   copy + checksum re-stamp; we don't have a working-bank scratch
   yet (no gameplay state to sync), so that branch is omitted. Pass
   `-1` in the engine for matching semantics.

2. **`scene_title_settings_apply_slider`** — each `audio_fade_set_*`
   call is now paired with the corresponding `save_header_set_*_slider`
   write. The header is the persistence source of truth; audio_fade
   is the runtime slider state synced from it at boot. Settings
   changes propagate both ways simultaneously.

3. **`--save-write` CLI flag** in main.c (default OFF). When set,
   shutdown calls `save_io_write_arena("save.dat", "_save.dat")`
   right before the rest of the shutdown chain. Default OFF so
   harness/smoke runs don't trample the user's real save with
   whatever in-memory state they ended in. Manual UX test:
   ```
   ./build/openrecet-debug.exe --save-write
   # ↓ → ↓ → A on Options → adjust Music slider, exit
   ./build/openrecet-debug.exe
   # boot trace shows the new slider value
   ```

4. **Round-trip + write tests** (4 new, 639 total). Covers both files
   written, NULL paths skipped, one-valid-one-NULL succeeds, and a
   full `set → write → clear → load → assert` slider round trip.

### Engine fidelity notes

- The engine writes both save.dat AND _save.dat in sequence — no
  atomic rename. We match. Either file is independently readable on
  next boot via save_io_try_load.
- The engine's full FUN_004905a8(N) where N != -1 has a working-bank
  merge step (DAT_044e3798 + N * STRIDE → bank[N], re-checksum). That
  scratch region (DAT_044e3798) isn't populated by anything we've
  ported yet — it's where the active in-play game writes its
  modifications. Lands with the scene-1 gameplay state machine.
- The 4 known save-back callers in the engine (FUN_004902aa,
  FUN_00450a59, FUN_004907cd, FUN_00490a05) all use either -1 (no
  bank merge) or the active slot index. The -1 path is the one
  shutdown-save-back uses.
- The recet.ini overlay was removed in the previous chip (save_io
  load); audio_fade sliders now flow exclusively through save_header.
  Combined with this chip, the full persistence loop is:
  `save.dat (boot) → save_header → audio_fade → settings_apply
  → save_header (mutation) → save.dat (shutdown)`. Clean.

### Deferred (gated on this chip)

- **Working-bank scratch** (DAT_044e3798) and the bank-merge branch
  of FUN_004905a8 — lands with the scene-1 gameplay state machine
  where actual game modifications happen. Until then, only the
  shared header (sliders) usefully persists; the per-bank dwords
  remain whatever was loaded from disk.
- **Periodic auto-save** during gameplay (engine calls FUN_004905a8
  from various scene-1 sites). Same dependency.
- **Save-slot UI** — engine has multiple save slots; our shutdown
  save-back writes the entire arena, so all 100 slots persist, but
  there's no UI to choose between them yet.

## 2026-05-22 — Save-load probe (FUN_004902fe) + title-menu unlock plumbing

Boot-time save-load probe lands. The engine reads save.dat (then
_save.dat as backup) at boot and either copies its contents into the
save arena or, if neither file is readable, leaves the fresh arena
state intact.  The title menu is then rebuilt against the (possibly
loaded) save state — CONTINUE_ANY / NEW_HAS_SAVE / CONT_HAS_SAVE /
SURVIVAL / HIDDEN_CHAR menu items now unlock based on actual bank
contents rather than the all-zero fresh save we used to assume.

Single commit: **`src/save_io.{c,h}` + `tests/test_save_io.c` + main.c
wire-up + boot-order refactor**.

### What landed

- **`save_io_try_load(primary, backup)`** — ports FUN_004902fe.
  Tries `primary` (`save.dat`) first via libc `fopen("rb")`, falls
  back to `backup` (`_save.dat`).  Three engine size buckets:
   - `0x011efce0` (modern JP) — legacy-modern path; sets
     `g_save_loaded_known_format = 1`.  Per-bank parser is stubbed
     (verbatim-copy fallback); the user's saves don't hit this size.
   - `0x00f30ae0` (ancient pre-release) — symmetric stub.
   - **any other size ≤ ARENA_BYTES** — verbatim-copy.  The
     Carpe Fulgur English Steam release writes saves at exactly
     `ARENA_BYTES = 0x011f7530` (18,838,832 bytes), so the user's
     saves land here.  Engine quirk preserved: this path does NOT
     set `g_save_loaded_known_format`.
  Each path calls `save_bank_init_all()` after the copy, which
  per-bank checksum-validates and re-inits any bank whose checksum
  doesn't match.  Returns 1 if either file was read, 0 if neither.

- **`save_io_scan_for_title_menu(out)`** — fuses FUN_0049a324 +
  FUN_0049a43d's three reads against the loaded banks into the
  `scene_title_save_t` struct that `scene_title_menu_init` already
  consumes.  Drives all four flags:
   - `has_any_score`        — any bank `[2]` > 0 (the per-bank score)
   - `has_any_adv_cleared`  — additionally `bank[0xb759] == 3`
   - `has_any_adv8_cleared` — any item in `bank[6..6+bank[0]-1]`
                              has `(item >> 6)` in `[0xd49, 0xd50]`
   - `hidden_char_unlocked` — shared-header dword 6
                              (engine DAT_056e5788)
  A safety cap bounds the item-list scan to `STRIDE - 6` dwords so a
  corrupt `bank[0]` count can't walk past the bank end (the engine
  has no such cap; we add one because the alternative is an OOB
  read across 18 MB of arena).

- **main.c boot order refactor**.  Before this chip:
  `save_bank_init_all → recet.ini overlay → audio_fade sync`.
  After: `save_bank_init_all → save_io_try_load → audio_fade sync`.
  The recet.ini mu/se overlay is removed entirely — it was a
  stand-in for save-persisted sliders until save-load ported.  The
  engine itself ignores recet.ini's mu/se at boot; we now match.
  (recet.ini's audio sliders ARE still WRITTEN by FUN_0047a804's
  shutdown save-back, deferred.)  After save_io, the title menu is
  rebuilt against the loaded save (`scene_title_menu_init` with
  scanned flags) so CONTINUE_ANY etc. appear iff the save backs them.

### Engine fidelity notes

- The engine's `DAT_095d3728` is a "skip per-bank checksum
  revalidation" hint flag, NOT a "save exists" flag.  Set only on
  the two legacy size buckets.  Title-menu unlocks are independent
  — they scan the bank contents directly.
- `save_bank_init_all` post-load behaves exactly like the engine:
  any bank whose checksum doesn't match `XOR(dwords[0..0xb7f0))`
  gets re-init'd to a fresh new-game state.  The verbatim-copy from
  disk + the per-bank re-validation together produce the same
  end-state as the engine's per-bank parse + checksum stamp.
- `g_ini.mu` / `g_ini.se` continue to be parsed (recet.ini reader)
  for future shutdown save-back; they're just not consumed at boot
  any more.
- Audio sliders now flow `save.dat → header → audio_fade` cleanly.
  The user's CF EN save ships with bgm=5 (engine default never
  adjusted), so the boot bgm slider drops from 9 (recet.ini stand-in)
  to 5.  This is the new authoritative source.

### Deferred (gated on this chip)

- **Modern JP per-bank parser** (engine FUN_004902fe lines 47-101):
  reads each bank at disk-stride 0xb7a5 dwords, validates checksum
  against the stored value, copies only 0xb78d dwords (the in-memory
  bank has additional scratch fields that aren't on disk).  Stubbed
  with verbatim copy + `save_bank_init_all` validation today.
  Lands if a vintage JP save surfaces.
- **Ancient pre-release per-bank parser** (lines 128-198): symmetric.
- **Shutdown save-back** (`FUN_0047a804`): writes recet.ini values
  + a final `FUN_004902aa` `save_clear_all` to disk.  Engine writes
  the full ARENA_BYTES verbatim — see save_bank.h's "engine call
  sites" doc block.  Deferred until the shutdown chain ports.
- **Title-screen save-slot UI** (engine `FUN_0049a59e` L213 reads
  `DAT_0438b1e0` for the active slot index): currently hardcoded to
  slot 0 in `scene_post_fade_init`.  Save-slot menu lands when the
  UI ports.

### Tests + scenarios

15 new unit tests (635 total, was 620): 8 cover the arena-scan path
(fresh-arena zero flags, score-in-bank, adv_cleared requires both
score + flag, adv8 range coverage end-to-end, header hidden-char
read, bank-99 coverage, bogus-count cap), 7 cover the disk-probe
path (no-files → 0, primary-exists → 1, fall-through-to-backup,
oversized → re-init, arena-sized verbatim copy survives the
checksum revalidation pass, known-format flag set on legacy size,
known-format flag stays 0 on fallback).

Scenarios: `title-options` re-blessed (Music slider visibly steps
from 9 → 5 because save.dat is now authoritative — the only
4-pixel-region diff in the settings panel).  Other 3 scenarios
unchanged (their captures don't enter the audio-slider readout).

Boot trace now logs:

```
save_bank: arena initialized (header magic=0x341944da, sliders se=9 bgm=5 se-b=9 slider3=1)
save_io: loaded save.dat (18838832 bytes)
save_io: title menu rebuilt — items=4 (adv_cleared=0 adv8=0 score=0 hidden=0)
audio: sliders seeded — bgm=5 se-a=9 se-b=9 (authoritative source: save_header)
```

User's save file is a fresh new-game state (gold=1000, no
adventure progress) so the menu still shows 4 base items.  When a
save with real progress lands, the same code path will surface
CONTINUE_ANY / NEW_HAS_SAVE / RANKING-with-cursor etc.

## 2026-05-22 — Now Loading overlay (FUN_00453147 + FUN_004063c7)

Next deferred scene-1 chip from the sysassets entry: the engine's
"Now Loading…" overlay. Drawn AFTER the scene render and the cross-
fade alpha quad, every frame the worker-thread gate
(`DAT_06a49958` / `DAT_06a49960`) is set. Two layers:

- A static 128×64 panel sampling the "Now Loading…" text bitmap from
  bmp/nowloading.tga's (64, 0)-(192, 64) region, drawn at screen
  position (512, 400).
- A rotating 64×64 spinner sampling the (0, 0)-(64, 64) disc graphic
  from the same texture, centred at (496, 440). Rotation accumulates
  at 0.3 rad/tick.

Three commits land the chip:

1. **`src/render_quad.{c,h}` — `render_quad_draw_rotated`** (+
   pure-C `render_quad_fill_rotated_vbuf` helper for testing). Mirrors
   FUN_004063c7 (394 bytes): writes 4 vertices to slots 0..3 of the
   static vbuf, calls `DrawPrimitiveUP(TRIANGLESTRIP, 2, …)`, resets
   the vertex counter. Pure-C inner loop computes per-corner offsets
   as `x_off = -sin(angle)*r`, `y_off = -cos(angle)*r` with
   `r = half_size * sqrt(2)` and `angle = (i/4)*2π + rotation + π/4`
   for `i` in the engine's iteration order `{0, 1, 3, 2}`. UV writes
   match the engine's hardcoded VA writes at DAT_00605220/240/260/280.

2. **`src/nowloading.{c,h}`** — ports FUN_00453147 (362 bytes) end
   to end. State module owns the alpha counter (engine
   `_DAT_06a49988`, decays 32/tick when gate off, clamped at 0), the
   rotation accumulator (engine `_DAT_06a4998c`, +0.3 rad/tick when
   gate on), and the active gate. `nowloading_render(dev)` fuses the
   tick with the per-frame draw exactly like the engine does:
   defers to `nowloading_tick()` for the pure state update, then
   either bails (gate off) or sets up alpha-blend + linear-filter
   state, binds `g_sysassets.nowloading_tga`, draws the static panel
   via `render_quad_add`+`flush`, and finishes with
   `render_quad_draw_rotated` for the spinner.

3. **`src/main.c`** wires `nowloading_render(g_dev)` into the per-
   frame render dispatch immediately after `fade_render(g_dev)`
   (mirrors FUN_004547ab L203 position). Also adds the
   `D3DRS_CULLMODE = D3DCULL_NONE` write at the top of render dispatch
   to mirror FUN_004547ab L60 — without it the TRIANGLESTRIP rotated
   quad's CCW-in-Y-down winding gets dropped by the default
   D3DCULL_CCW (the static panel survives because render_quad_add's
   triangle ordering happens to be the opposite winding). The cull-
   mode fix is broader than the spinner: any TRIANGLESTRIP drawn from
   here on inherits the correct face-direction-agnostic behaviour.

4. **`src/scene.c::scene_post_fade_init`** sets
   `nowloading_set_active(1)` after the INGAME state flip — fakes
   the engine's FUN_0049de18 worker-thread gate so the overlay
   actually draws during the LOADING→INGAME transition. The flag
   never clears in our build (no worker thread yet) so the overlay
   stays on indefinitely; that's fine while the placeholder scene_1
   render lives there too.

12 new unit tests (620 total, was 608): 5 cover the rotated-quad
vertex math (axis-aligned at rotation 0, quarter-turn corner roll,
screen_w scaling, no-counter-touch, z/rhw/specular preservation),
7 cover the nowloading state machine (reset, gate normalisation,
alpha decay clamp at 0, rotation 0.3/tick, decay-and-rotation
mutual exclusion, tick return-value contract). The D3D render path
is Win32-only and verified by the harness re-bless.

Title-z-press scenario re-blessed: 14/14 capture frames now include
the spinner+panel in the post-fade frames (90..115). Other 3
scenarios (boot-idle, title-down-press, title-options) re-pass
bit-exact unchanged — they never enter INGAME state.

### Engine fidelity notes

- The engine's render dispatch calls `SetRenderState(D3DRS_CULLMODE,
  D3DCULL_NONE)` at L60 (right after `BeginScene`), then reverts to
  `D3DCULL_CW` at L207 (after everything has drawn). We set it once
  per frame at the top of render dispatch; the revert is dormant
  because nothing in our render path relies on CW culling.
- FUN_00453147 fuses the alpha-decay tick with the render path. The
  port preserves this fusion (the render function calls
  `nowloading_tick()` internally) but exposes `nowloading_tick()`
  publicly for the unit tests.
- Engine `DAT_06a49958` and `DAT_06a49960` are kept as a single OR'd
  gate in the port (`g_active`). Every consumer takes the OR; growing
  the port to two fields can wait until FUN_0049de24 (the secondary
  gate's producer) lands.
- The engine's `_DAT_06a49988` counter feeds OTHER UI elements that
  fade out in sync with the loading overlay; the overlay itself is
  gate-driven, not alpha-driven. The counter is faithfully updated
  in our port even though no consumer uses it yet.

### Deferred (gated on this milestone)

- **Worker thread + scene asset loader** (FUN_0049de18 + LAB_0049de24
  + FUN_0049dfd2) — the producer of the gate flag. Without it, the
  overlay stays active forever in our build. Lands as part of the
  scene-1 ramp.
- **`DAT_06a4998c` continuous animation** — works while the gate is
  set. A future stop-condition (worker done) will pin rotation to the
  last computed value rather than freezing mid-frame.
- **Secondary gate `DAT_06a49960`** — set by FUN_0049de24 and several
  other load paths. Currently collapsed into the primary; teasing
  apart lands when those callers port.

## 2026-05-22 — System asset loader (FUN_00472f5d)

Next scene-1 chip: ports the engine's shared system-overlay texture
loader.  Loads the ~30 textures every post-title UI overlay consumes
— "Now Loading…" panel, save/data/item windows, character portraits,
HP/MP gauges, status effect sprites, per-category item icon pages.
None of these are drawn yet (the placeholder INGAME render is
unchanged), but they're a hard dependency for the next round of port
work: the Now Loading overlay (FUN_00453147 — uses nowloading.tga),
the inventory windows, and the scene-1 HUD all consume one or more
of these.

Single commit: **`src/sysassets.{c,h}` + `tests/test_sysassets.c` +
wire-up in `src/main.c`**.

The module exposes:

- `g_sysassets` — Win32-gated struct of named sprite slots, one per
  engine `.data` global at &DAT_073aa188 / &DAT_073d9fe0 / &DAT_073cc770
  / etc.  Three loop-loaded sub-arrays: `chara_variants[3]` for
  `bmp/chr/chr%02d.bmp`, `item_icons[100]` for per-category icon pages,
  and the 20 single-load entries.
- `sysassets_load_all(IDirect3DDevice8 *dev)` — calls `sprite_load`
  for each filename in source order, matching FUN_00472f5d L27..L61.
  Per-category icon pages are loaded only for categories that have at
  least one valid item record.
- `sysassets_unload_all()` — releases every D3D texture.  Safe to
  call on a zero-init struct; safe to call repeatedly.
- `sysassets_compute_icon_sizes(items, out)` — pure helper that
  reproduces the per-category page-height math from FUN_00472f5d
  L73..L97: count valid records per category in pass 1, then return
  `max(64, ceil(count_per_cat / 8) * 32)` for each category that has
  any items.  Exposed for tests (the loader's only non-trivial math).

Wired into `src/main.c` at boot, immediately after
`scene_title_load_assets(g_dev)` — the same relative position
FUN_00472f5d holds in FUN_0047b29e (the title-bootstrap chain) at L233.
The post-device-reset reload site at FUN_004547ab L231 is deferred
until D3D8 lost-device handling lands.

New boot trace line confirming the load (against vendor data):

```
sysassets: 55 textures loaded (static=20 chara=3 item_categories=33/33)
```

20 static + 3 chara variants + 33 item categories (one per
populated 100-id band in item.txt: 100s/200s/300s through 5400s).

### Engine fidelity

- Asset filenames extracted via `tools/analyze/pe.py str` at
  0x005c84c0..0x005c8634; ordered identically to the engine's source
  order so the load trace lines up with the original on-the-wire.
- Texture (w, h) hints recovered directly from the engine's per-call
  literals (e.g. nowloading.tga is `0x100 × 0x40`). `sprite_load`
  doesn't yet resample — every audited asset ships at native
  resolution — so the hints are stored on the sprite but unused
  today.
- Chara portrait sub-loop uses a BSS-zero size table at &DAT_0438cec8
  on a fresh boot (the chara-select scene populates it later).
  Port matches by passing (0, 0) to `sprite_load`, which loads at
  native resolution.  When the chara-select port lands, the table
  will be wired in and the hints become live.
- Item-icon loop: the engine uses one register (iVar5) as both
  "max category seen" tracker and the temporary that holds the
  computed page height — overwriting itself mid-iteration.  Our port
  uses two named variables for clarity (semantically identical:
  records are sorted by item_id and hence by category, so the
  max-tracker fires the load exactly once per category).
- Two sub-blocks intentionally deferred (both BSS-zero on the boot
  path, so dormant):
  - 20-dword zeroing loop at &DAT_068dccc4 (stride 40 bytes) — only
    needed on the device-reload path, where the consumer state needs
    a reset.  First-touch-is-zero covers our boot.
  - `DAT_0076b948`-gated array load (custom-image icon pages added
    by FUN_00474f4f — vendor never populates them).

12 new unit tests (608 total, was 596).  Tests cover the pure
icon-size helper across empty/single/eight/nine/seventeen/large
counts, invalid records, multi-category, out-of-range categories,
max-category tracker semantics, and the two `_Static_assert`-like
constant pins (chara variant count, item category slot count).
Win32 surface (sprite_load → IDirect3DTexture8 upload) is not
testable from the host driver — same constraint as
test_scene_title.c.

All 25 captures across 4 scenarios re-pass bit-exact.  No visible
change today (assets load but aren't drawn yet).

### Deferred (gated on this milestone or related)

- **Now Loading… overlay** (FUN_00453147, 362 bytes) — uses
  `g_sysassets.nowloading_tga` plus a rotated quad render
  (FUN_004063c7, 394 bytes).  Gate flag (`DAT_06a49958`) is set by
  the worker thread; without the worker, the overlay stays invisible.
  Next chip candidate: port the overlay + fake the gate flag for the
  17-tick post-fade window.
- **Device-reset reload path** (FUN_004547ab L228..L231) — re-calls
  FUN_00472f5d after `IDirect3DDevice8::TestCooperativeLevel` returns
  `D3DERR_DEVICENOTRESET`.  Lands with general lost-device support.
- **Chara size table producer** (chara-select scene) — populates
  &DAT_0438cec8 so the chara portrait loads use real dimensions.
- **Custom-image array** (`DAT_0076b948` path, FUN_00474f4f) — for
  user/modder-added portraits; not present in vendor data.

## 2026-05-22 — Save-arena init (FUN_004901c2 + FUN_0049001c)

Past-the-placeholder foundation chip: ports the engine's full save
arena bootstrap + per-bank fresh-state initializer.  Largest
single-module port this session (~1150 lines of C + 14 unit tests),
and the gating dependency for all further scene-1 work — scene-1 sim
+ render fns read from the 188360-byte-per-slot save bank, which now
exists with correct field constants.

Two commits:

1. **`src/save_bank.{c,h}` + tests/test_save_bank.c** — pure-C module
   owning the full 18.84 MB arena (shared header + 100 × bank).
   Public API: `save_bank_init_all` (= FUN_004901c2), `save_bank_init_one`
   (= FUN_0049001c), checksum verify/stamp, named-field constants
   (gold=1000, week=7, rank=100, SE/BGM/SE-B/slider3 defaults
   9/5/9/1), and shared-header slider get/set accessors.

   Three engine helpers folded in:
   - `FUN_0048ff93` (starter items) — encoded slot IDs from
     STARTER_ITEMS[8][5] (DAT_005cf788, 40 dwords extracted via
     pe.py) written into per-chara inventory windows.
   - `FUN_0048ffd9` (starter flag-pairs) — 10 pairs per chara from
     STARTER_FLAG_PAIRS[8][10][2] (DAT_005cf864). **Engine quirk
     preserved verbatim**: the table is undersized (64 valid pairs
     of 80 declared); the last 16 overrun into adjacent .data
     strings ("wb"/"_save.dat" file-mode literals).  Dormant in
     vendor because NEW GAME only reads chara[0]'s row.
   - `FUN_0047a8c0` (per-chara stat interpolation) — pure-C
     equivalent of the FPU sequence at 0x47a8c0 in the unpacked
     binary, formula `value = base + (lv100 - base) * level / 100`
     reading from `g_chara[]` (populated by chara.txt parser).

   14 unit tests pin arena geometry, slider defaults, idempotent
   re-init, checksum tamper detection, RNG state advancement (1 LCG
   draw per chara × 8 charas × 100 banks = 800 draws at init_all),
   plus two overlapping-write quirks documented via assertion:
     - the named mini-block at bank[0xb388..0xb38d] (constants
       3,3,1,0,0,1) are DEAD writes — fully overwritten by
       apply_starter_flag_pairs' span [0xb384..0xb397].
     - chara record dwords [0xb..0xf] are DEAD writes — overwritten
       by apply_starter_items' encoded slot IDs (id<<6 | 0x20).
   Total 596 unit tests (was 582).

2. **`src/main.c` + `src/scene.c` wire-up** — promotes save_bank
   from "compiled but unused" to live in both init paths:

   - **Boot:** `save_bank_init_all()` runs immediately after
     audio_init, replacing the prior recet.ini-only slider seed.
     Engine defaults (9/5/9/1) populate the shared header first;
     recet.ini's mu/se values then overlay on top to preserve user
     preference until save-load (FUN_004902fe) ports.  audio_fade
     sliders are then synced from the header so the per-channel
     apply hook draws from one source of truth.  The engine's
     FUN_00499583 callback (BGM SetVolume re-apply on header init)
     is wired via `save_bank_set_header_init_hook` + a tiny
     `save_bank_apply_bgm_via_audio_fade` bridge so save_bank
     doesn't link against audio.c.

     New boot trace lines:
     ```
     save_bank: arena initialized (header magic=0x341944da,
                sliders se=9 bgm=5 se-b=9 slider3=1)
     audio: sliders seeded — bgm=9 se-a=9 se-b=9 (save_header
                overlay from recet.ini bgm=9 se=9)
     ```

   - **NEW GAME post-fade:** `scene_post_fade_init()` now calls
     `save_bank_init_one(0)` between the LOADING and INGAME state
     writes — mirrors FUN_0049a59e L213's `FUN_0049001c(active_bank)`.
     Slot index hardcoded to 0 until save-slot UI lands (matches
     engine on a fresh boot with DAT_0438b1e0 BSS-zero).

All 25 captures across 4 scenarios (boot-idle, title-down-press,
title-options, title-z-press) re-pass bit-exact.  The placeholder
INGAME chip is unchanged frame-for-frame — bank-0 reset writes to
memory no consumer yet reads.

### Engine fidelity notes

- The chara loop and FUN_0047a8c0 collapse: the engine calls
  FUN_0047a8c0 INSIDE the 8-iter chara loop, but FUN_0047a8c0 itself
  walks all 8 records each call — 7× redundant work.  Our port
  collapses to one post-loop call (same final memory state).
- One RNG step is consumed per chara record per bank, faithfully
  reproduced via `rng_next15()`. Net result: 800 draws per init_all.
- The 100-iter scratch loop at bank offset 0x9e78 is a no-op given
  the preceding memset — kept as a doc comment, not a runtime loop.
- The conditional carry-over branch gated on DAT_005c80ac is
  skipped — no upstream sets it pre-NEW-GAME, so the engine takes
  the false branch at first boot too.

### Deferred (gated on future ports)

- **Worker thread + asset loader** (FUN_0049de18 chain) — still
  blocks the Now Loading… overlay and real scene-1 init.
- **Now Loading… overlay** (FUN_00453147) — gated on DAT_06a49958 /
  06a49960, which only the worker thread sets.
- **Scene-1 sim + render** — FUN_004547ab state==1 branch (6 render
  fns: FUN_0045bbf9 / 0040a765 / 00417504 / 0045404b / 0040c962 /
  004358cc / 00453d9c).  Mt. Everest scope.
- **save-load (FUN_004902fe)** — 682 bytes; reads save.dat with
  format migration (older 0x011efce0 vs newer 0x011f7530 layout);
  unlocks CONTINUE_ANY title menu items.
- **UI scratch resets** (FUN_004060ff/4682d0/452917/etc) — small
  named-global setters; deferred until their consumers (scene-1
  render path) port, otherwise dead code.

## 2026-05-22 — Post-fade scene transition + placeholder INGAME render

Past-title-fade-out chip — first time openrecet shows anything other
than the title screen. After NEW GAME, the screen now transitions
through the black fade-OUT to a placeholder dark-navy clear with a
debug label, rather than hanging on solid black forever.

Three pieces:

1. **`src/scene.{c,h}` — `scene_post_fade_init()`** — collapses the
   engine's `DAT_0438b1c0 = 8; FUN_0049de18(); DAT_0438b1c0 = 1;`
   sequence at FUN_0049a59e L64-77 into one call. Engine writes
   LOADING then INGAME within the same sim tick so no observer ever
   sees LOADING mid-flight; the same-tick INGAME write is the
   observable endpoint. Also kicks `fade_phase_out_start(0, 0x11)`
   (FUN_0045281c) at FUN_0049a59e L235 polarity, so the alpha quad
   ramps phase-(-1) over the next 17 sim ticks, revealing the
   destination scene. Save-bank reset + UI-scratch reset
   (FUN_004060ff / 4682d0 / 452917 et al, ~150 lines of decomp)
   intentionally deferred — none of their consumers are ported yet,
   so writes would land on unread globals.

2. **`src/scene_ingame.{c,h}` — placeholder INGAME renderer** — clears
   to `0xff203050` (dark navy, intentionally distinct from the title
   clear `0xff17f0ff`) plus two `font_draw_text` lines so the
   scene-state transition is visually unambiguous. Replaces with the
   real engine's per-stage palette clear + scene-1 render functions
   (FUN_0045bbf9 / FUN_0040a765 / FUN_00417504 / FUN_0045404b /
   FUN_0040c962 / FUN_004358cc / FUN_00453d9c) as they port one
   subsystem at a time.

3. **`src/scene_title.c` + `src/main.c` — wire-up** — scene_title_sim
   calls scene_post_fade_init() when fade_is_done() returns 1
   (replacing the prior bare `g_scene_state = LOADING` write).
   render_dispatch in main.c picks the per-state clear color and
   routes to scene_ingame_render when scene_state == INGAME. The
   old "holding on black" log line is replaced with "menu item N →
   INGAME (placeholder)".

`tests/test_scene.c` adds 4 unit tests covering the transition
endpoint, substate clear, and fade-phase flip. 582 tests total
(was 578).

`tests/scenarios/title-z-press/scenario.yaml` extended from 11
captures (last at frame 95, max_frames=100) to 14 captures (last at
frame 115, max_frames=120) covering the new fade-IN → placeholder
arc:

| frame | g_fade_counter | phase | alpha | scene_state | visual |
|------:|---------------:|------:|------:|------------:|--------|
| 90 | 1 | -1 | 255 (clamped) | INGAME | solid black (quad fully opaque) |
| 92 | 3 | -1 | 238 | INGAME | placeholder showing through faintly |
| 100 | 11 | -1 | 119 | INGAME | placeholder ~50% visible |
| 108 | 0 | 0 | (no quad) | INGAME | clean placeholder visible |
| 115 | 0 | 0 | (no quad) | INGAME | steady-state |

openrecet golden re-blessed (14/14 bit-exact on re-run). Retail
golden also re-blessed at the new frame indices, but cross-target
divergence is by design: retail shows solid black with a faint
"Now Loading…" overlay (FUN_00453147) at frames 92-115 because the
worker thread is loading scene-1 assets; ours skips that thread and
jumps straight to the placeholder. Captured at
`runs/comparisons/title-z-press/sidebyside.png` for visual reference.

All other scenarios (boot-idle / title-down-press / title-options)
re-pass bit-exact.

### Deferred — gated on this milestone

- **Save bank init** (FUN_004901c2 + FUN_0049001c + the ~150-line save-
  bank reset chain inside FUN_0049a59e L64-211) — needed before any
  scene-1 sim/render reads from the 188232-byte bank. The audio
  slider defaults at DAT_056e5774/_5778/_577c (9/5/9) are nominally
  set here, but our audio_fade defaults of 9/9/9 + recet.ini override
  cover the visible behaviour today.
- **Now Loading… overlay** (FUN_00453147) — gated on DAT_06a49958 /
  06a49960 BSS-zero flags, which only the loader worker thread sets.
  Lands with the worker-thread port.
- **Worker thread + asset loader** (FUN_0049de18's downstream — the
  engine's CreateThread / LAB_0049de24 / FUN_0049dfd2 chain that
  loads scene-1 BMPs/TGAs/.x meshes). Big chunk.
- **Scene-1 sim + render** — actual gameplay. Mt. Everest scope. The
  placeholder gets replaced one render fn at a time as these port.

## 2026-05-22 — Title fade-out lands (port of the RE writeup)

Acted on the title-fade-out findings doc — three small commits:

1. **`src/fade.{c,h}` + `tests/test_fade.c`** — pure-C counter/phase
   machinery. Mirrors FUN_004526f5 (phase-1 init), FUN_0045281c
   (phase-(-1) init), FUN_004526ab (per-tick advance with the
   `duration+1` clamp on phase 1 and the reset-at-`>duration` on
   phase -1), FUN_004528b3 (done-query — `counter == duration`,
   special-cased to `counter == 0x1f` for mode 2). The 100-particle
   pre-roll inside FUN_004526f5 is omitted — vestigial, no consumer
   reads `DAT_06a48d6c` / `DAT_06a4921c`. 10 new tests (total 578).
2. **Wire-up**: `sim_step_a` tail calls `fade_tick()` (mirrors
   FUN_004536cb LAB_00453cfb line 318). `scene_title_sim`'s
   `fade_counter == 30` site calls `fade_phase1_start(0, 0x11)` —
   replacing the prior no-op increment — and when
   `fade_is_done()` returns 1, transitions `g_scene_state` to
   `SCENE_STATE_LOADING`. Render dispatch in `main.c` calls
   `fade_render(g_dev)` after `scene_title_render`. `fade_render`
   lazy-loads `bmp/system.bmp` (the 128×128 UI sheet with the (9,1)-
   (15,7) black patch + (1,1)-(7,7) white patch) on first frame and
   emits a 640×480 alpha-blended quad via the existing
   `render_quad_add` path. Alpha formula is the recovered
   `(int)(256/(duration-2) * counter)` clamped to [0,255] — NOT the
   `alpha = counter` that Ghidra produced.
3. **Snap-back removed**: previously main.c caught
   `title.fade_counter >= 0x1e` and reset both fade counter and
   select_phase so the title would reappear. Replaced with a
   one-time log when `g_scene_state` transitions to LOADING; the
   fade quad keeps drawing (counter pinned at `duration+1` = 18,
   alpha clamped to 255), so the screen stays solid black until
   --max-duration-ms or user-close terminates the process. This is
   the engine's behaviour during the gap between fade complete and
   destination scene init (the worker-thread loader hasn't run yet).

Visual verification against retail (title-z-press scenario, mean-RGB
delta vs frame 50 reference):

| frame | predicted alpha | ours dmean | retail dmean |
|-------|-----------------|------------|--------------|
| 73    | 34              | 28         | 28           |
| 80    | 153             | 124        | 124          |
| 85    | 239             | 193        | 193          |
| 90    | 255 (clamped)   | 207        | 205          |

Side-by-side comparison at
`runs/comparisons/title-z-press/sidebyside.png` — visually
indistinguishable in the fade range; retail-only per-frame px
differences are ~440-491 / 786432 (~0.06%) from non-pinned
particle/pulse jitter in the un-instrumented retail capture path.

Scenario.yaml comments updated to reflect the actual alpha schedule
(prior comments referenced 1/7/12/17, which were derived from the
Ghidra mis-decomp; correct values are 34/153/239/255).

Worker-thread loading overlay (FUN_00453147 "Now Loading…") still
deferred — it's gated on `DAT_06a49958 != 0 || DAT_06a49960 != 0`
(both BSS-zero today; only the loader worker thread sets them).
Lands with the destination scene init.

## 2026-05-22 — Title fade-out RE: corrects same-day "Deferred — big" misreading

No code change this session — purely a corrective writeup. The
"Scene-state global + title fade-out counter" entry below filed the
title→NEW_GAME fade as "DEFERRED — big" based on a wrong reading of
FUN_004526f5 + FUN_00452cde. We dug into it expecting a multi-session
port, then found the real mechanism is ~250 lines of pure C plus
existing render-quad infrastructure.

Two corrections matter for future sessions:

1. **There are no fade-out particles.** The 100-element float-vec
   tables at `DAT_06a48d6c` and `DAT_06a4921c` that FUN_004526f5
   initialises are dead writes — verified via objdump that nothing
   in the binary reads them. The 30-tick pre-roll loop touches only
   itself. The "100-particle 3D mesh fly-off running on a worker
   thread" description in the prior entry was reverse-engineered
   from the init code without checking whether any consumer existed.

2. **Ghidra mis-decomps FUN_00453e8f's alpha formula.** The decompiled
   `iVar1 = __ftol()` after a plain `(float)counter` push suggests
   `alpha = counter` (max 17 ≈ 6.7% opacity). The actual x86 at
   `0x453ed5..0x453f5b` has a `flds 0x519390 (= 256.0)` + `fdivs` that
   Ghidra dropped, so the real formula is `alpha = (int)(256 *
   counter / (duration - 2))`. For NEW_GAME's `(0, 0x11)` that's
   `256/15 ≈ 17.07` per step → full opacity at counter 15.

The off-screen render target system (`DAT_06a4999c`, FUN_00454191)
that the investigation initially fixated on is a real engine
feature, but it's used for in-game scene-to-scene transitions
(triggered via FUN_00453384 — from WndProc ESC, in-game NPC
interactions, etc.) — **not** the title→NEW_GAME fade.

Full writeup with the corrected pipeline, the asm of the missing
multiplier, the dead-particle-table provenance, and the actual port
plan: `docs/findings/title-fade-out.md`.

`title-z-press` scenario captures extended from 5 frames (0/30/35/
44/50) to 11 (+73/74/80/85/90/95) so the fade-out is now within the
captured range. `max_frames` bumped from 60 to 100. Retail goldens
re-blessed. Our goldens re-blessed too (same snap-back behavior,
just more frames captured); cross-target diff at frames 73+ is now
visible in `runs/comparisons/title-z-press/sidebyside.png`.

Updated session-start memory + this PROGRESS entry. No source files
touched. Port is filed as ~3 small commits when picked up.

## 2026-05-22 — Scene-state global + title fade-out counter

First two steps in the "past the main menu" thread. The skeleton was
hardcoded to dispatch title sim + render every frame; the engine
actually fans both halves out of `DAT_0438b1c0`. And the title scene's
A-press on NEW GAME was insta-snapping back via a `pending_action`
stub; the engine actually starts a 30-frame countdown
(`DAT_0964351c`) that gates the title sim out while a fade animation
plays in the background.

Two commits:

1. **`scene: extract g_scene_state (DAT_0438b1c0) into its own module`**
   — new `src/scene.{h,c}` owns the global + a `scene_state_set_title()`
   helper mirroring FUN_0047b29e's first two writes (`DAT_0438b1c0 = 0;
   DAT_0438b1c8 = 0;`). `prewindow_init()` now also writes 1 to the
   global, matching FUN_00451790. `sim_step_a` + `render_dispatch`
   switch on `g_scene_state` — only TITLE has a producer/consumer
   today, other states drop through. Pure refactor, no behavior change.

2. **`scene_title: port DAT_0964351c fade-out counter; NEW GAME freezes
   title`** — new `fade_counter` field in `scene_title_anim_t`. At
   `select_phase == 0xf`, codes 0/4/5 (NEW_GAME / NEW_HAS_SAVE /
   CONT_HAS_SAVE) latch `fade_counter = 1` instead of routing
   through `pending_action`. Once set, the counter ticks every
   frame; `scene_title_sim` gates all menu input + the cursor_anim
   ramp out while counter > 0 (engine FUN_0049a59e L53-77). Only
   `pulse_phase` keeps advancing — BG scroll continues during the
   freeze. `main.c` watches for `fade_counter >= 0x1e` (30 frames),
   logs "destination not ported" once per code, and snaps back for
   recovery (fade_counter + select_phase reset to 0).

Visible change in `title-z-press` golden frame 50: previously showed
the post-snap-back state (NEW GAME dim, default pulse). Now shows the
mid-freeze state (NEW GAME pinned brightly highlighted at
select_phase=0xf, frozen for 30 frames). Retail at frame 50 is
deep into the 3D particle-scatter fade-out — the visible cross-target
mismatch remains until that's ported (see "Deferred" below).

### Deferred — scene fade-out particle animation (FUN_004526f5 +
FUN_00452cde thread)

The actual engine fade is a 100-particle 3D mesh fly-off running on
a worker thread (`FUN_00452cde` spawns `CreateThread` → `LAB_0045293d`,
ticks `DAT_0438bf78` once per frame). Particles are textured 3D
quads with per-particle position+rotation transforms; the back buffer
gets captured to a texture then re-rendered as fly-off tiles over
~17 ticks. `FUN_00452917` is the thread *cleanup* (CloseHandle), not
the per-frame tick as the function name might suggest.

Not a one-session task. Needs thread plumbing, back-buffer→texture
capture, 3D particle quad renderer with per-particle transforms, and
`FUN_004528b3` completion polling. Filed under "future" for now —
the existing snap-back covers the UX gap.

### Other deferred (NEW GAME destination)

`FUN_0049a59e` lines 65-200 — the post-fade NEW GAME init block —
reads/writes the 188448-byte save bank at
`DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8`, calls FUN_004060ff /
FUN_004682d0 / FUN_00490e56 (init-from-scratch) / many per-slot
resets, then transitions `scene_state` through 8 (LOADING) to 6
(game world entry, via `FUN_00490e16`). Save bank format port +
in-game scene renderer are both Mt. Everest scope from here.

568 unit tests pass; all 4 scenarios capture bit-exact.

## 2026-05-22 — Harness turbo mode (frame-limiter bypass + silent audio)

Both the retail Frida agent and `openrecet.exe` gain matching `--turbo`
and `--silent-audio` flags. Together they let `tools/scenario-test.py`
run scenarios at host-CPU speed (no Sleep) while keeping the engine's
internal wall-clock advancing at exactly the 60 FPS budget per loop
iteration — so animations / fades / RNG all stay consistent with what
they'd be at 60 FPS, just compressed in wall time. Goldens regenerated
under turbo are bit-exact against the non-turbo goldens.

Measured speedup on `title-z-press` retail capture: 1705 ms → 854 ms
(~2x). Larger scenarios benefit proportionally — the savings scale
with the number of "idle" frames between capture anchors. `boot-idle`
is already short enough that startup overhead dominates; gain is small
there but still positive. `--turbo` works under both
`--input-trace-replay` (no change — replay already runs at host speed)
and free-running mode.

### What landed

- **`tools/frida/openrecet-agent.js`** — `installTurboHooks()` replaces
  `FUN_0047be2f` (the QPC ms reader) with a `NativeCallback` that
  returns a virtual clock, and attaches `FUN_0047be92` (dispatcher)
  entry to bump that clock by `g_turbo_step_ms` (default 17) per call.
  Engine's `delta_thirds` is always 51 ≥ 50 (the 60 FPS threshold), so
  the sim+render branch fires every loop iteration with no Sleep.
  `installSilentAudioHook()` waits for `FUN_00498ef4` exit, reads
  `DAT_09643108` (BGM AudioPath), and `Interceptor.attach`'s its
  `vtable[5]` (SetVolume) to rewrite `lVolume → -10000`. All three
  audio paths share a vtable so one hook silences BGM + SE-A + SE-B.
  RPC `init` accepts `turbo` / `turbo_step_ms` / `silent_audio`.

- **`tools/frida_capture.py`** — `CaptureConfig` gains `turbo` /
  `turbo_step_ms` / `silent_audio`; `run_capture` + CLI plumbed
  through (`--turbo`, `--turbo-step-ms`, `--silent-audio`).

- **`tools/scenario-test.py`** — `--turbo` and `--silent-audio` flow
  to both `run_scenario_capture` (openrecet) and
  `run_scenario_capture_retail` (Frida). `--target both` honors them
  on both halves.

- **`src/tick.{c,h}`** — `tick_set_turbo(enabled, step_ms)` /
  `tick_turbo_enabled()`. When enabled, `tick_step_win32` feeds the
  pure-C dispatcher a virtual clock advancing by `step_ms` per call
  and skips Sleep on `TICK_RESULT_DELAYED`. Pure-C
  `tick_step_with_now` unchanged — the speed-table math and state
  machine are byte-for-byte identical regardless of clock source.

- **`src/audio.{c,h}`** — new `silent_audio_apply_hook` function
  matching `audio_fade_apply_hook_t`'s signature; clamps every
  forwarded centibel to `AUDIO_FADE_SILENCE_CENTIBEL` (-10000)
  before calling `IDirectMusicAudioPath_SetVolume`. Game's audio
  code (PlaySegmentEx, fade math, segment-state queueing) runs
  untouched.

- **`src/main.c`** — `--turbo` and `--silent-audio` parsed in
  `parse_cmdline`. Turbo applied right after `tick_init()`; silent
  audio replaces the default apply hook right after `audio_init`.

### Smoke-test results

- openrecet `boot-idle --turbo --silent-audio`: 3/3 bit-exact, 1.3 s.
- openrecet `title-z-press --turbo --silent-audio`: 5/5 bit-exact,
  1.7 s.
- retail `boot-idle --turbo --silent-audio`: 3/3 bit-exact, 0.8 s.
- retail `title-z-press --turbo --silent-audio`: 5/5 bit-exact, 0.8 s
  (vs 1.7 s without turbo — ~2x).
- `boot-idle --target both --turbo --silent-audio`: 6/6 bit-exact,
  side-by-side renders.
- 568 unit tests pass.

### Caveats

- Turbo + Frida currently lets the retail process linger ~1 s after
  scenario completion (still inside `device.kill`'s timeout). Not
  related to turbo — same pre-turbo behaviour — but the speed-up
  makes it more noticeable as a fraction of total run time. The
  belt-and-braces `tasklist | grep -i recettear` after a batch
  remains a good idea.
- DirectMusic doesn't love being clocked at 200+ fps; that's exactly
  why `--silent-audio` is recommended alongside `--turbo`. Without
  silencing, the audio backend may drop / glitch (cosmetic — game
  state stays correct because the audio fade math drives off engine
  ticks, not wall time).

## 2026-05-22 — Phase B input injection

The retail-capture pipeline now replays the same sparse JSONL trace
Phase A does, so `tools/scenario-test.py --target retail <name>` drives
the real game through the scenario's input sequence instead of capturing
an idle title screen. Unblocks every future retail golden capture that
needs menu navigation.

### What landed

- **`tools/frida/openrecet-agent.js`** — added `g_input_trace` /
  `g_input_trace_i` / `g_input_force_active` / `g_input_last_forced`
  globals. `installInputHook`'s onLeave now advances a monotonic
  cursor through every entry with `frame <= current_frame`, applies
  the sticky mask via `writeU16` to `DAT_073dddd0`, then re-reads
  for the `input_state` event so the recorded trace reflects what
  the engine actually saw. `init({input_trace, force_input})`
  accepts the trace as `[{frame, mask}, ...]` from the driver.

- **`tools/frida_capture.py`** — `CaptureConfig` gains
  `input_trace_path` + `force_input` fields; `_run_capture_impl`
  loads the JSONL (tolerating `#` comments to match
  `src/input_trace.c`), passes through to the agent. CLI adds
  `--input-trace` / `--force-input` for ad-hoc replay.

- **`tools/scenario-test.py`** — `run_scenario_capture_retail`
  always enables injection, pointing at the scenario's existing
  `trace.jsonl`. Old "no input replay yet" docstring removed.

- **`tests/scenarios/title-down-press/`** — new scenario: DOWN
  press at frame 30, cursor steps NEW GAME → MINIGAME. Strictly
  more visible than `title-z-press` in thumbnail-sized contact
  sheets (the tooltip text changes; the highlighted row changes),
  so eyeball regressions are easier to spot.

### Verification

- `boot-idle/golden-retail/` re-blessed: 3/3 frames, no behavioral
  change vs the previous bless (no input → injection is a no-op).
- `title-z-press/golden-retail/` re-blessed: agent.log records all
  three trace transitions (0→0x10→0); engine fires `se_play` slot 7
  at frame 30 (menu-confirm sound), and the NEW GAME row brightens
  through frames 30→44 (hottest diff rows 316-328 in a per-pixel
  delta vs frame 0). User-confirmed visual: NEW GAME button
  brightens through the select_phase ramp on the right column of
  a side-by-side render.
- `title-down-press/golden-retail/` blessed: cursor steps from
  NEW GAME to MINIGAME between frames 0 and 30, tooltip text
  on the left swaps, both visible at thumbnail size.

### Out of scope (deferred)

- **RNG / clock pinning.** Retail still runs at real wall-clock pace
  during capture, so cross-run bit-exactness within retail (and
  cross-host portability of retail goldens) remain undetermined.
  Re-bless on each capture host until/unless this gets pinned.
- **Joystick / mouse injection.** Only the 14-bit `DAT_073dddd0`
  player-0 mask is forced. Joystick axes / mouse position would
  need separate hooks.
- **`force_input=False` regression.** Phase B+ state-forcing
  drivers (`tools/state_diff/`) already skip the capture hooks via
  `install_hooks: false`; the injection plumbing defaults to off
  so they don't accidentally inherit forced input.

## 2026-05-22 — Settings submenu render (FUN_0049c050)

The "Options" submenu now draws. Producer landed at `d34079e` two days
ago but the render was gated on the font system; with text rendering up
since `e2ded60`, the render port now lights up the panel.

### What landed

- **`src/font_draw.{c,h}`** — added `font_draw_text_centered`, port
  of FUN_0047d14c. Walks the string with `font_slot_alloc` + immediate
  `font_slot_upload` on each fresh slot, sums per-glyph advance via
  `effective_width`, then calls `font_draw_text` at
  `center_x - width/2`. The explicit upload-on-allocate matters:
  the engine's FUN_0047cbcb is atomically alloc-and-upload-if-new,
  but our pure-C split separates them — without uploading inside
  the measure walk, glyphs first-seen by the centered draw end up
  with no texture installed (font_draw_text's draw walk sees
  `is_new=0` and skips its own upload). Symptom was missing letters
  in "Clear Save Data" rendered after the row labels: every char
  already used in the labels rendered fine, but C / l / v / D —
  only first-seen in the centered draw — came out invisible. Skips
  the dead `DAT_0438b784 & 1` legacy branch of FUN_0047d14c.

- **`src/scene_title.{c,h}`** — `scene_title_settings_render_panel`
  (FUN_0049c050 port) draws the dungeonbord panel BG + 6 row labels
  + 5 slider value strings + dormant Saving overlay. Wired into the
  end of `scene_title_render` with the gate
  `cursor_anim > 0 && submenu_state == 2`, plus the two outer
  header chrome quads from FUN_0049c644 L234-244 (item_win.tga tab
  + fuki.tga OPTIONS label) at the engine's
  `x = 640 - cursor_anim*64` slide offset.

  Row layout (top-down): MUSIC / SOUND / VOICE / MESSAGE SPEED /
  UNREAD TEXT SKIP / CLEAR SAVE DATA. Numeric sliders show 0-9;
  Message Speed shows SLOW/MED/FAST; Unread Text Skip shows OFF/ON.
  Yellow on the cursor row, grey elsewhere; engine's three
  bit-twiddle inlines for the same yellow/grey pair collapsed to
  one ternary. Engine writes both `D3DTOP_ADDSIGNED=8` and
  `D3DTOP_MODULATE2X=5` back-to-back at FUN_0049c050 L35-36, second
  wins — collapsed to a single MODULATE2X write here.

  Hard-coded 6 rows because this is the title-side caller; engine
  conditionally drops to 5 when `DAT_0438b1c0 != 0` (in-game pause
  menu, FUN_0047fc44, not yet ported). Will need a scene-state arg
  when the pause menu lands.

- **`SCENE_TITLE_TEX_ITEM_WIN` = slot 7** in `scene_title_assets`.
  Asset list grew 7 → 8 (loader, tests, fixture data all updated).
  `bmp/item_win.tga` is a boot-time UI atlas in the engine
  (FUN_0047193c context=1, alongside system.bmp, savewindow.tga,
  etc.) but parked on the title-scene loader pragmatically until a
  boot-time-textures module exists.

- **`tests/scenarios/title-options/`** — new scenario covering
  DOWN×2 → A → slide-in. 4 captures at frames 0 (baseline), 10
  (OPTIONS highlighted in main menu), 39 (panel fully slid in), 60
  (held). Bit-exact against blessed goldens.

### Known visual followups (font-system class)

- ~~Lowercase glyphs render at uppercase height~~ **fixed in
  follow-up commit**. `font_draw_text` now folds the
  `(origin_x, ascent - origin_y) * fVar2` baseline offset into
  the dst rect and uses `(tex_w, tex_h) * fVar2` for the dst size,
  keeping the small-texture upload. Lowercase glyphs now sit
  baseline-aligned with proper x-height; capital letters extend
  above. Visible win across every font draw site (smoke text,
  settings menu labels + slider values). Skipped the engine's
  `(cell_inc_x, line_height)` cell-pad approach as it would burn
  ~3x more GPU memory per slot for the same on-screen result —
  the cell pad is what the engine uses but we don't need it
  given we drive baseline via the dst rect instead.

- "Clear Save Data" centering is still ~10px off on first draw
  (engine quirk — measure walk reads `effective_width=0` for fresh
  slots; engine has the same misalignment).

### Deferred (still)

- Clear-data confirm modal (row 5 + FUN_00434def) — no save IO yet.
- Filename SE feedback on row 2 inc/dec (engine quirk #50) — uses
  generic SE 0x146 in the existing sim.
- Saving overlay visuals — needs `savewindow.tga` loading + actual
  save IO before the branch ever fires. Wired through as
  `saving_flag` param but no-op'd in the render.
- In-game pause sound menu (FUN_0047fc44) — same FUN_0049c050 with
  5 rows; lands when an in-game scene ports.

## 2026-05-22 — Font system, end-to-end (FUN_0047c228 / c474 / c3a5 / c29d / cbcb / cf22 / ca05)

Seven functions, six commits, ~1700 lines of new C — the whole text
rendering pipeline now works. A scene can call `font_draw_text(dev,
x, y, str, argb, scale)` and pixels come out. Title scene now shows
"openrecet 0.1" in the bottom-left as a smoke test.

### Architecture

```
WinMain:
  font_init()              ← clears 200-slot LRU cache + texture table
  audio_init()
  font_atlas_build_win32() ← GDI builder, conditional on g_config.font_set
                             or missing ./font/fontdata.bin (drop-in path)
  font_atlas_load()        ← reads back fontdata.bin + fontidx.bin

Per-frame (sim_a):
  font_age_tick()          ← bumps age on every in_use slot

Per-glyph (in scene render path):
  font_slot_alloc(b0, b1)  ← 200-slot LRU, age-gated eviction
  font_slot_upload(slot)   ← D3D8 CreateTexture + LockRect + ARGB expand
                             (texture release on evict hooked via callback)
```

### What landed

- **`src/font.{c,h}`** — 200-slot LRU cache state. `font_init` (port
  of FUN_0047c228) zeros the slot + texture tables, seeds slot_id with
  each entry's index. `font_age_tick` (port of FUN_0047c29d) increments
  `age` on every in_use slot — engine's debug-overlay scan is dropped
  since FUN_00451874 is a release-build stub. Wired into `sim_step_a`
  after the button ring.

- **`src/font_atlas.{c,h}`** — record format (40 bytes) + GDI atlas
  builder + disk loader. The builder mirrors FUN_0047c474:
  CreateFontIndirectA at 42px / SHIFTJIS / ANTIALIASED, walks 256
  single-byte + 288 special-table 2-byte + SJIS double-byte from 0x88
  with the engine's gap-skip pattern, rasterizes each via
  GetGlyphOutlineA(GGO_GRAY4_BITMAP), pads to a 4-pixel border, applies
  5×5 radial edge dilation, writes both files. Output goes to
  **`./font/`** (not the vendor dir — fresh path so retail and
  openrecet don't fight over atlas files). Loader (FUN_0047c3a5)
  reads them back into `g_font_atlas`. Pure-C parts (record packing,
  blit, dilation) are Linux-testable; GDI driver behind `_WIN32`.

- **`src/font_alloc.{c,h}`** — codepoint→record-id lookup + 3-phase
  slot allocator. find_existing → find_free → find_evictable (age > 3).
  Release callback hook lets the Win32 layer Release the GPU texture
  on eviction without dragging D3D into the pure-C module.

- **`src/font_upload.{c,h}`** — Win32-only D3D8 texture upload. Skips
  the engine's TGA-then-D3DX dance; uses CreateTexture + LockRect with
  D3DFMT_A8R8G8B8 directly. ~150 lines less code, same on-GPU result.
  Pure-C pixel-expansion helper (`font_upload_expand_pixel`) is
  Linux-testable.

- **`src/font_draw.{c,h}`** — `draw_text(x, y, str, argb, scale)`
  port of FUN_0047ca05. Walks the SJIS string, routes each codepoint
  through alloc → upload (if new) → SetTexture + render_quad_add +
  render_quad_flush. Per-glyph dst is `(eff_w * scale*0.494,
  42 * scale*0.494)`, advance is `(eff_w - 3) * scale*0.494` — matches
  engine math. Departure: src rect uses `[0, 0, tex_w, tex_h]`
  (full texture) instead of the engine's fixed `[1, 1, 41, 41]`
  (WRAP-relying for smaller textures). Pixel-exact match isn't a
  project goal; the eyeball test is "readable text in the right place."

- **`src/scene_title.c`** smoke: draws "openrecet 0.1" at (8, 460,
  scale=1.0). Visible in the boot-idle golden, blesses applied.

- **Atlas output gitignored**: `./font/` lands under `vendor/original/`
  in dev workflow, which is already gitignored. Atlas regenerates on
  first boot of a fresh install (no `font:` in config.idx needed —
  the loader's "files don't exist" branch triggers regen with a
  default face name).

### Tests

24 (atlas builder/record) + 6 (pixel expansion) + 15 (codepoint
lookup + slot allocator) + 5 (cache init/age) + 4 (loader) = 54 new
unit tests. Total test count 568 from 514. Both boot-idle and
title-z-press scenarios pass (re-blessed with the smoke text overlay).

### Engine quirks documented

See `docs/findings/winmain-and-bootstrap.md` §"Font system" for the
full list. Highlights:

- **kanjioff polarity inversion** in FUN_0047c474: Ghidra renders the
  break check with `== 0` but the byte-level semantic must be `!= 0`
  (otherwise vendor default would skip all kanji)
- **Phantom 0x883f glyph**: first phase-1 atlas-walker iter renders
  the invalid SJIS codepoint 0x883f → GDI returns nothing → fontidx
  slot 544 = empty record. Harmless.
- **Slot-overlap return pointer**: FUN_0047cbcb returns `slot - 12`
  so `piVar4[3]` reads slot.slot_id. The 12-byte "pre-slot" region
  is actually slot[i-1]'s pad20/pad24 + the start of slot[i] —
  effective_width gets written into pad20 during upload. We give
  effective_width its own field and skip the trickery.

### Known follow-ups

- **Visual aspect**: glyphs of varying texture height get stretched
  into the engine's fixed 42-unit dst height. Text reads as
  tall-and-narrow vertical bars at scale 1.0. The engine has the
  same math — possibly the engine's textures are all sized so the
  WRAP-sampling in [1,1,41,41] produces a consistent visible glyph
  area. Worth a second look once scene text consumers (settings menu,
  shop UI) land.
- **Title menu labels are still sprite-baked** in `fuki.tga` — the
  draw_text smoke is a separate overlay, not a replacement. Wiring
  the menu items through draw_text is for a later milestone.
- **Engine variant of upload** (TGA-in-memory → D3DXCreateTextureFromFileInMemoryEx)
  isn't byte-identical to our CreateTexture+LockRect path. Doesn't
  matter for runtime visual but means the texture in GPU memory won't
  literally match the engine's. Project memory says "not byte-identical"
  so this is fine.

## 2026-05-22 — Harness: pre-resume state-forcing + first differential test (LCG + cos-curve fade)

Phase B's deferred half — calling vendor functions with forced state to
diff against our ports — lands as MVP infra plus one end-to-end test.
The two pure-math subsystems we picked first (RNG and audio_fade) both
come back **bit-exact** to retail across the full input range. No
divergence, no need for tolerance. The RPC + oracle plumbing generalises
to any future pure-fn diff (LZSS/LZW decoders, lnkdatas_hash CRC, input
mask decoder, tick scheduler).

### What landed

1. **`tools/frida/openrecet-agent.js` — state-forcing RPC surface.**
   Five new RPC methods alongside the existing capture-side hooks:
   - `readMemory(va, len)` / `writeMemory(va, hex)` — generic
     byte-window access keyed on Ghidra VAs (preferred ImageBase
     0x00400000, recomputed against actual load base on every call).
   - `readU32(va)` / `writeU32(va, val)` — primitive shortcuts; the
     two used by the LCG diff.
   - `callU32NoArgs(va)` — invoke a u32-returning, no-arg cdecl
     function via `NativeFunction`. Used to drive `FUN_005041f6`.
   - `captureFadeCentibel(slider)` — purpose-built for the audio_fade
     diff: plants a fake `IDirectMusicAudioPath` in `DAT_09643108`
     whose vtable[5] (SetVolume) is a `NativeCallback` that records the
     centibel argument before returning S_OK. Forces `DAT_056e5778`
     (BGM slider) to the requested value, calls `FUN_00499583`,
     restores both globals. Side-effect-free — host audio is never
     touched.

   Two breakages found and fixed along the way:
   - `rpc.exports` keys must be **camelCase** in JS (not snake_case as
     the original `queue_capture` / `get_frame` were). Frida-Python
     auto-converts snake_case Python method calls to camelCase before
     dispatch, so `write_u32` on the Python side maps to `writeU32` on
     the JS side. The two existing exports were silently broken from
     day 1; Phase B's driver only ever called `init` (no underscores),
     so it never tripped. Filed as a project memory.
   - `NativeFunction` rejects `'cdecl'` as an explicit ABI on x86 — the
     valid token would be `'mscdecl'`, but the platform default does the
     right thing for no-arg / void-return calls so we omit the argument.

   `init({install_hooks: false})` skips the Phase B capture hooks —
   the state-forcing tests never resume the main thread, so the
   D3D/audio/input interceptors would never fire anyway.

2. **`tools/state_diff/oracle.c` + Makefile — local "ground truth".**
   Tiny host binary linking `src/rng.c` + `src/audio_fade.c`. Stdin
   protocol:
   - `rng_seq <seed_hex> <n>` → prints `n` post-step seed values
     (raw `DAT_006023a0` state after each LCG call — directly
     comparable to what `readU32(DAT_006023a0)` reads after
     `callU32NoArgs(FUN_005041f6)`).
   - `fade_compute <slider>` → prints
     `audio_fade_compute(slider, 0)` for the BGM diff.
   Built with host gcc, no sanitizers (it's not a unit test).
   `audio_trace_emit_fade_start` stubbed in-file so we don't drag in
   the 700-line `audio.c`.

3. **`tools/state_diff/lcg_fade.py` — driver.** Spawns retail under
   Frida in `CREATE_SUSPENDED` state and **never resumes the main
   thread**. The Frida helper thread that runs the agent is independent
   of the target's threads — it can invoke `NativeFunction` calls and
   read/write process memory without any engine code executing. No
   races against `FUN_00451790` (engine particle init advances the LCG)
   or against the real audio backend. The oracle runs concurrently as a
   long-lived stdin subprocess.

### Results (cutestation.soy:27042, retail unpacked exe)

```
# LCG step (FUN_005041f6, DAT_006023a0)
  pass seed=0x00000001  (256 steps bit-exact)
  pass seed=0x00003039  (256 steps bit-exact)
  pass seed=0xdeadbeef  (256 steps bit-exact)
  pass seed=0x80000000  (256 steps bit-exact)
  pass seed=0xfffffff0  (256 steps bit-exact)
  pass seed=0x00000000  (256 steps bit-exact)

# BGM fade curve (FUN_00499583)
  pass slider=0  retail=-10000  ours=-10000  Δ=+0cb
  pass slider=1  retail= -5391  ours= -5391  Δ=+0cb
  pass slider=2  retail= -4231  ours= -4231  Δ=+0cb
  pass slider=3  retail= -3176  ours= -3176  Δ=+0cb
  pass slider=4  retail= -2245  ours= -2245  Δ=+0cb
  pass slider=5  retail= -1458  ours= -1458  Δ=+0cb
  pass slider=6  retail=  -829  ours=  -829  Δ=+0cb
  pass slider=7  retail=  -371  ours=  -371  Δ=+0cb
  pass slider=8  retail=   -93  ours=   -93  Δ=+0cb
  pass slider=9  retail=     0  ours=     0  Δ=+0cb

16 passed, 0 failed
```

- LCG: 6 seeds × 256 steps = 1536 individual u32 comparisons, all
  bit-exact. Expected — the LCG is one `imul` + `add`, no FP, no
  platform variation.
- Fade: 10 slider values, all bit-exact (the `±1 centibel` tolerance
  in the driver was never tripped). libm `cos()` and MSVC's
  `FUN_00503994` round to the same `int32` after `__ftol` truncation
  for every (slider, target=0) point on this curve.

Deterministic across re-runs.

### Follow-up candidates (same harness, same agent surface)

The plumbing is generic — any pure or near-pure ported function gets a
short driver script:

1. **`lnkdatas_hash` CRC** — call `FUN_00474f14` with arbitrary buffers
   via `Memory.alloc` + `writeMemory` + a `callU32_ptr_u32` variant.
   Targets `src/lnkdatas_hash.c`.
2. **LZSS decompress (`FUN_004349e5`)** — write a compressed buffer +
   output buffer, call, `readMemory` the result; diff against
   `src/lnk_lzss.c`. Already validated vs `recettear-repacker` Python.
3. **LZW decompress (`FUN_00434b32`)** — same pattern; diff against
   `src/bmp_lzw.c`. Already validated vs `recettear-repacker`.
4. **Input mask decoder (`FUN_0047b73c`)** — synthesize a raw
   DI keyboard buffer + joystick state + per-binding table, call,
   `readU16(DAT_073dddd0)`; diff against `src/input.c` decoders.
5. **Tick scheduler (`FUN_0047be92`)** — fixture engine ms-clock global,
   tick once, observe state advance; diff against `src/tick.c`.

The remaining audio-backend "Next steps" item (settings-menu slider
producer `FUN_0047fc44`) and the splash/title-bootstrap port don't need
state-forcing tests yet but will benefit from this surface once they
land.

## 2026-05-22 — Harness Phase B: retail capture via Frida

Phase B lands as planned at the bottom of yesterday's Phase A entry:
`tools/scenario-test.py --target retail <name> --bless` drives the
SteamStub-decrypted retail exe (`vendor/unpacked/recettear.unpacked.exe`)
through the same scenario plumbing and writes BMPs / audio.jsonl /
trace.jsonl into a per-target `golden-retail/` directory. Output schemas
match Phase A exactly so the bless + bit-exact diff path is shared.

Five pieces:

1. **`tools/frida/openrecet-agent.js`** — Frida JS agent. Hooks the
   D3D8 init wrapper (`FUN_0047ac6a`) to capture the
   `IDirect3DDevice8*` (`DAT_073dfcbc`) once it's live, then installs:
   - `IDirect3DDevice8::Present` (vtable[15]) — frame capture
   - `FUN_00499200` (BGM swap)            — `{kind:bgm_swap, track}`
   - `FUN_00499c63` (SE play)             — `{kind:se_play, slot}`
   - `FUN_0047b73c` (input poll) onLeave — reads `DAT_073dddd0`,
     emits `{kind:input_state, buttons:0xNNNN}`
   The frame number for each event is read from `DAT_073dfcfc`
   (engine global frame counter), so capture filenames match the
   scenario's `capture_frames:` list bit-for-bit.

2. **Sysmem-bounce frame capture.** First cut hit `D3DERR_INVALIDCALL`
   on `IDirect3DSurface8::LockRect` — the retail back buffer is
   *non*-lockable (no `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER`). Workaround:
   `CreateImageSurface(w, h, fmt, &sys)` + `CopyRects(bb → sys)`,
   then lock the sysmem surface (lockable by construction) and copy out
   the BGRA pixels. The captured format echoes whatever
   D3DFMT_X8R8G8B8 / A8R8G8B8 the engine asked for — both are
   compatible with our BMP layout.

3. **`tools/frida_capture.py`** — Python driver. Connects to a remote
   `frida-server.exe` (default `127.0.0.1:27042`; overridable via
   `--frida-remote` or `$OPENRECET_FRIDA_REMOTE`), spawns retail
   suspended via `device.spawn()`, installs the agent + hooks, then
   resumes. Emits BMPs bit-identical to `src/main.c::capture_backbuffer`
   so the shared diff path works. Includes `ensure_frida_server()`
   helper that auto-launches `frida-server.exe -l 0.0.0.0:<port>` via
   `powershell.exe Start-Process -Verb runAs` (UAC prompt) when the
   port isn't already reachable. Server exe location pulled from
   `$OPENRECET_FRIDA_SERVER_EXE` with a sensible default.

4. **`tools/scenario-test.py --target {openrecet,retail}`**. Per-target
   golden dirs: `golden/` (openrecet, unchanged) vs `golden-retail/`
   (Phase B). Bit-exact diff within a target; cross-target diff is
   out of scope (different draw call ordering / font system — never
   bit-comparable, deferred to a future contact-sheet tool).

5. **`tests/scenarios/boot-idle` blessed under retail.** First retail
   golden: 3/3 frames captured at the engine's 640×480 back-buffer
   (window stretches it to 1024×768), audio trace caught the title
   BGM swap on frame 0 (`{track:0}`), input trace recorded the
   all-zero idle mask. Re-running without `--bless` shows 3/3
   bit-exact pass — retail's boot-idle path is deterministic enough
   to gate against under the same NAT-mode wall-clock conditions.

WSL2 networking note: NAT mode (the default) doesn't expose Windows
`127.0.0.1` to WSL. `frida-server.exe` therefore needs
`-l 0.0.0.0:27042` (the auto-start helper passes this), and the WSL
side connects via the host's actual IP or hostname. The user's
`cutestation.soy` works; the default `127.0.0.1` does not. Mirrored
networking would let `127.0.0.1` work both ways — left as a user
preference, not a project requirement.

Known limitations / what Phase B intentionally **doesn't** do:

- **No input injection.** Retail's recorded `trace.jsonl` reflects
  what the engine polled (i.e. live keyboard); the scenario's input
  `trace.jsonl` is unused under `--target retail`. Anything beyond
  the title-idle scene requires a human at the keyboard.
- **No RNG / pause / clock pinning.** Retail's title-idle happens to
  be deterministic across runs (no RNG reads during the idle window);
  scenes that touch RNG would drift. Cross-run stability evaluated
  scene-by-scene as new retail goldens land.
- **State-forcing (save inject + scene jump) is deferred.** Same hook
  surface, separate session per the scope decision in the harness
  roadmap.

## 2026-05-21 — Harness Phase A: input-trace record/replay + scenario runner

Closes the "build-system regression hid between commits" gap that
prompted the harness roadmap two days ago (see "Build-system header
dep tracking" entry below). End-to-end pipeline now lands and locks
in the two scenarios that cover the original failure mode.

Five pieces, three commits:

1. **`src/input_trace.{c,h}` + 20 unit tests (514 total, was 494).**
   Sparse-JSONL parser + writer + lookup. Schema:
   `{"frame":N,"buttons":"0xNNNN"}` — one line per mask change, with
   "the most recent entry's mask holds until the next entry"
   semantics. Strictly-increasing frame order enforced at parse.
   Comments + blank lines tolerated. Pure C; tests cover happy path,
   sparse hold, malformed input, file round-trip, record/replay
   behavior.

2. **`src/main.c` CLI integration.** Five new flags:
   - `--input-trace-record <file>` wraps `tick_cb.input_poll` to
     snapshot `g_input_state[0].buttons` each frame.
   - `--input-trace-replay <file>` replaces `input_poll` with a
     trace lookup, skips `input_init` / DirectInput entirely, pins
     `g_paused=FALSE`, drives a 20 ms virtual clock so the tick
     scheduler always returns TICKED (no Sleep, no DELAYED).
   - `--rng-seed <n>` pins the LCG seed (skipping
     `rng_seed_from_now()`) so title BG scroll + cursor pulse phase
     stay frame-identical across replays.
   - `--max-frames <n>` PostQuitMessage after n rendered frames.
   - `--capture-frames i,j,k` captures ONLY at the listed sim-frame
     indices; filename `frame_<sim_frame>.bmp` so the scenario
     runner can match by number. Legacy `--capture-every-ms`
     untouched when this isn't set.

3. **`tools/scenario-test.py`** Phase A regression harness.
   Discovers `tests/scenarios/<name>/`, runs the exe with the right
   flags, **bit-exact** diffs captured BMPs against `golden/`.
   Mismatches emit a red-tinted overlay PNG so visual review is one
   `Read` away. `--bless` regenerates goldens from a fresh run; that
   path doesn't fail.

4. **`tests/scenarios/boot-idle/`** (3 captures @ 0/30/60, 60-frame
   idle). The trivial baseline — title boots, nothing pressed, cursor
   pulse + BG scroll roll on under the pinned RNG seed.

5. **`tests/scenarios/title-z-press/`** (5 captures @ 0/30/35/44/50).
   Z held for one frame at index 30 → 14-frame select countdown →
   dispatch on frame 44 ("Start a new game" tooltip visible) → main.c
   logs "destination scene not ported yet" + snaps `select_phase=0`
   → frame 50 shows the post-snap pulse. This is the exact failure
   mode of the 2026-05-21 input-bypass bug: a stale `main.o` would
   miss the dispatch entirely, frame 44 would still look like
   frame 30, the diff would land loud.

Pixel-diff strictness decision: **bit-exact**. The smoke validation
showed 3/3 boot-idle frames and 5/5 title-z-press frames bit-exact
across two back-to-back replay runs AND across record-mode vs
replay-mode capture. Mismatch produces a red-tinted overlay PNG;
re-bless after intentional behavior changes. SSIM was rejected
because threshold tuning hides single-pixel offset bugs.

Goldens are gitignored — they're rendered output that embeds vendor
textures (RECETTEAR logo, BG art). `scenario.yaml` + `trace.jsonl`
ship; `golden/` is regenerated locally on first checkout via
`--bless`. See `tests/scenarios/README.md`.

Determinism pins under `--input-trace-replay`:
- RNG seed forced via `--rng-seed`
- DirectInput init skipped (live keypresses can't bleed in)
- WM_ACTIVATE pause pinned off (focus loss can't stall replay)
- Tick scheduler bypassed for a manual 20-ms-per-iter virtual clock

Phase B (Frida hooks on retail exe for ground-truth comparison)
shares this scenario layout — same JSON/PNG schemas — and is the
next session's target if priorities don't shift.

## 2026-05-21 — Build-system header dep tracking (input-bypass regression fix)

The user reported on RDP that arrows + Z had stopped doing anything in
the title menu. Bisecting from the last verified-good commit
(`c2b144c`, title sim port) walked through five known-good intermediate
builds and isolated `d34079e` (settings submenu) as the regression.

Smoke-runs of master here showed a spurious "title: menu item 0
selected" log at boot with no keypress, which led to a per-frame stderr
trace inside `scene_title_sim`: `submenu_state` was reading `-1`
(`0xFFFFFFFF`) by the second sim call even though the init memset had
just set it to 0. A clean `rm -f *.o && make` made the corruption stop
— diagnostic of a stale object file.

Root cause: `src/Makefile` and `tests/Makefile` only declared `%.o:
%.c`, with no header-dep tracking. `d34079e` inserted three fields
(`submenu_state`, `submenu_cursor`, `settings_dirty`) into
`scene_title_anim_t` ahead of `pending_action`. `main.c` was not
touched by that commit, so `make` did not rebuild `main.o`; the stale
object kept writing the action sentinel `-1` to the *old*
`pending_action` offset, which is now occupied by `submenu_state`.
With `submenu_state == -1`, the `scene_title_sim` main-menu input gate
(`cursor_anim == 0 && submenu_state == 0`) failed every frame → arrows
and Z were dead but the dispatch leg fired phantom selections via the
same offset confusion clobbering `select_phase`.

Fix (`520a349`): add `-MMD -MP` to CFLAGS in both Makefiles and
`-include $(DEPS)` so each `.o` declares its real header deps via
generated `.d` files. Touch-test confirms `scene_title.h` →
`main.o`, `music.o`, `sim.o`, `scene_title.o` all rebuild. `*.d`
added to `.gitignore`. 494 tests still pass.

Lesson: every C build for this repo needs header dep tracking from
day one — the cost of `-MMD -MP` is one CFLAGS flag and one
`-include`, and the failure mode (offset corruption on header
extension) is silent. Next time `tests/Makefile` or any new build
unit gets created, copy the pattern.

Follow-up (next two sessions, see `docs/harness-roadmap.md`): set up a
deterministic input-trace harness so this class of regression can't
hide between commits again.

## 2026-05-21 — Title settings submenu producer (FUN_0049a59e state 2)

Ports the bare-path slider producer inside FUN_0049a59e — the title-
screen "Options" submenu that the engine reaches by selecting OPTIONS
on the main menu and pressing A. Lands the input/state-machine half
of the audio-cleanup track's "settings menu slider producer" item.

Result: pressing A on the OPTIONS row of the title menu now (a)
transitions the title sim into submenu state 2 with cursor on row 0,
(b) accepts UP/DOWN/LEFT/RIGHT to navigate the 6-row sliders, (c)
fires SE feedback (0x143 for confirm, 0x146 for cursor/slider tick)
via a new `audio_play_se_by_id` helper, (d) calls
`audio_fade_apply(BGM)` on every BGM-slider change so the running
music re-attenuates immediately, (e) accepts A or B to exit; the
exit handler folds back to main with the cursor seeded on the
OPTIONS row.

- **Module shape:** the producer lives inside `src/scene_title.c`
  (the engine's FUN_0049a59e is the title sim, all submenus
  included). Two static helpers + one new exit-handler call from
  the top of `scene_title_sim`. Non-audio rows 3 & 4 live in a new
  module `src/settings.{c,h}` so other subsystems can read
  text-speed / boolean state without pulling `scene_title.h`.
- **New audio helper:** `audio_play_se_by_id(uint16_t)` in audio.c
  walks the existing 110-entry SE table, finds the slot for the
  resource ID, and delegates to `audio_play_se(slot)`. Pure C, used
  by the title scene to mirror the engine's SE-by-id call sites
  (FUN_00499519). Sibling `audio_se_slot_for_id` exposed for tests.
- **One-shot dispatch fix:** the main-menu select pulse now only
  dispatches on the *first* frame `select_phase` reaches 0xf (was:
  dispatched every subsequent frame, relying on a pending_action
  guard to mask re-publication). The behaviour difference is visible
  for the new OPTIONS branch — without the fix, every frame after
  the select pulse would re-enter the settings submenu.
- **Engine deviations documented** (`docs/findings/title-settings-submenu.md`):
  - Row 2 (SE-B) inc/dec plays SE 0x146 instead of the engine's
    filename-based `re_sys01a_b` SE pair (FUN_0049933c). Filename-
    based SE loading isn't ported yet; cursor SE keeps the user in
    audible feedback.
  - "Clear all data" modal (row 5 + A) is gated but the modal flow
    itself isn't implemented — no save IO to clear. Engine fidelity
    holds: A on row 5 consumes the press + plays SE 0x143 but does
    not exit settings.
  - Save-on-exit (FUN_004905a8) is stubbed; slider state persists
    in the audio_fade module and `settings.{c,h}` for the lifetime
    of the process. Engine saves to `save.dat` + `_save.dat` on the
    exit-dirty path — lands with the save-IO milestone.
- **Tests:** 19 new (513 total, was 494). Coverage: state transitions
  (A on OPTIONS → state=2, exit handler → state=0), 6-row cursor
  wrap mod 6, per-row slider targeting (BGM/SE-A/SE-B/slider3/slider4),
  bounds clamping at both ends, dirty-flag transitions (0→1→2 vs
  0→3), B-also-exits, re-entry clears dirty + cursor, OPTIONS does
  NOT publish to `pending_action`, regression guard that other menu
  items (EXIT etc.) still do.
- **Render deferred:** `FUN_0049c050` (1001 bytes — the settings
  panel renderer) depends on `FUN_0047ca05` (text helper / font
  system). Without the font system the per-row slider values can't
  be drawn. Slated for the font-system milestone — see
  `docs/findings/title-settings-submenu.md` "What's deferred".

Visible verification: smoke boot still clean, exit 0, BGM unchanged.
Settings interactivity verifiable via audio_trace JSONL (paired
`fade_start` + `se_play` lines fire on slider adjust); a manual test
on the user's host where the player navigates to Options will
audibly hear BGM volume drop / restore via LEFT/RIGHT on row 0.

## 2026-05-21 — Audio: per-tick fade animation (FUN_0049966a tail)

Closes item #2 from `audio-backend.md` "Next steps". The volume tail at
LAB_00499a00 walks a two-axis cosine product over `DAT_005d1964`
(=600 by default) frames; this commit ports it end-to-end and wires it
into `music_step`.

- **Pure math** in `src/audio_fade.{c,h}`:
  `audio_fade_progress_centibel(phase, progress, duration, slider)` is
  the two-cos product
  `cos(angle_progress) * cos(angle_slider) * 9600 - 9600` with the
  slider angle reused from the existing per-frame cos arc and a new
  per-progress angle that spans `[0, π/2]`. Defensive clamping on
  slider/progress/duration so call sites stay simple.
- **Hook wrapper**: `audio_fade_apply_progress(channel, phase,
  progress, duration)` mirrors the existing `audio_fade_apply` but
  bypasses the trace emit (the per-tick path can fire up to 600 times
  per fade — a per-frame `fade_start` event would swamp the JSONL).
- **Integration** in `src/music.c::music_step`: replaces the stubbed
  volume-animation tail with the real flow — advance `fade_progress`,
  call `audio_fade_apply_progress` against the BGM channel, set
  `pending_swap_clear = 1` at fade end, reset `fade_progress` on
  phase clear. Engine's `DAT_0438cd70` "carry-over" gate is BSS-zero
  in every observed boot/play trace, so the port pins it to
  "always clear" (annotated; revisit if a future scene flips it).
- **Phase semantics correction**: the music.h comment had
  `1=in, 2=out`. Re-reading the assembly at 0x499a2b (phase==1) vs
  0x499a9e (else) showed the opposite — phase 1's cos(angle_progress)
  starts at 1.0 and decays to 0.0 across `progress`, i.e. audible
  fade-OUT; phase 2 is the inverse. Comment fixed. Setter call
  signature also lines up: `FUN_00499538(duration)` takes a duration
  arg and sets phase 1, `FUN_0049954c()` takes no args and sets
  phase 2 — "here's how long to fade out" + "now fade back in".
- **Tests**: 17 new (475 total, was 458). Pure-math coverage of both
  phases at endpoints + monotonicity, slider/progress clamping,
  degenerate-duration fallback. music_step integration tests run
  short-duration fades to completion under a captured apply hook,
  asserting per-tick centibel direction + final `pending_swap_clear`
  + progress reset.

Smoke boot: title BGM still audible, exe exits 0, no warnings. The
fade tail itself is dormant at boot because nothing yet sets
`pending_fade_phase` — that comes with the title→submenu transition
or the settings-menu producer (item #3 on the queue).

## 2026-05-21 — Audio: `audio_fade_apply` live + revert phase-B deviations

Closes the audio-cleanup track that was queued after SE phase B landed.
Three behaviours converge in one commit:

1. **`audio_fade_apply(channel)` is now real.** Engine call site
   FUN_00499583 is the cos-curve volume mapper that fires before every
   BGM swap and SE play. The math half (`audio_fade_compute`) was
   ported earlier; this commit adds:
   - **Per-channel slider state** in `src/audio_fade.c` for BGM /
     SE-A / SE-B. Defaults 9/9/9 (full volume). The engine's BGM=5
     default in `FUN_004901c2` is a save-data thing — intentionally
     not mirrored until save-load lands; until then, 9 matches the
     audible-volume baseline users already heard. Public setters/
     getters (`audio_fade_set_slider` / `_get_slider`) + a
     `audio_fade_reset` test affordance.
   - **Apply hook** — `audio_fade.c` calls a registered function
     pointer with the computed centibel; the Win32 backend
     (`src/audio.c::audio_fade_apply_hook_win32`) routes to the
     matching AudioPath's `IDirectMusicAudioPath::SetVolume`. The
     indirection keeps `audio_fade.c` test-buildable (no dmusici.h).
   - **Trace event `fade_start`** added to the JSONL schema —
     `{"channel":N,"slider":N,"centibel":N}`. Fires from
     `audio_fade_apply`, so it pairs back-to-back with each
     `bgm_swap`/`se_play` event.

2. **Phase-B engine deviations reverted.** Both wired into the
   audio-fade hookup:
   - `DMUS_SEGF_SECONDARY` → `DMUS_SEGF_QUEUE` in
     `audio_play_se_win32`'s PlaySegmentEx. Engine fidelity. Queueing
     is scoped per-AudioPath, so SE on `path_se_a` doesn't preempt
     BGM on `path_bgm`. The explicit per-trigger Stop right before
     PlaySegmentEx still defeats same-slot re-trigger queueing.
   - Init-time `SetVolume(0, 0)` on both SE paths dropped — the
     per-call `audio_fade_apply(SE_A)` covers it now.

3. **Tests + docs.** 12 new tests (slider get/set, hook capture,
   invalid-channel guards, fade_start trace round-trip). Total 458 in
   the host test suite (was 446). `docs/findings/audio-backend.md`
   updated: status block strips the deviation list, trace schema +
   call-site table updated, Next-steps rewritten (recet.ini → slider
   seeding, per-tick fade animation, settings-menu producer).

Smoke test: title BGM continues to play; `--play-se` fires SEs into
the trace (paired `fade_start`/`se_play` lines per trigger) with
PlaySegmentEx returning S_OK.

**Audible regression on the user's Windows host:** SEs are inaudible
after the revert. BGM is unaffected. Trace events fire normally and
PlaySegmentEx succeeds, so the hooks are wired correctly. The pre-
revert configuration (init-time SetVolume + SECONDARY flag) was
audible on the same host. Treating as an open issue rather than
re-applying the deviation — likely missing a piece of engine init we
haven't ported (FUN_004901c2 save-arena init / recet.ini → slider
seeding / something else). Will surface as we port more of the audio
boot chain.

## 2026-05-21 — Audio: SE backend phase B (live SE playback)

Picks up where the autonomous session left off. Phase A had the
110-entry SE resource table + a trace-only `audio_play_se` shell; phase
B wires the Win32 backend end to end. User-verified audible on the
Windows host: BGM continues uninterrupted while a sequence of SE plays
fires over it.

Four commits land the work:

1. **Mojibake fix** (`main.c`, commit `4740a96`).
   `SetConsoleOutputCP(CP_UTF8)` at WinMain entry. Source files use
   literal `—`/`→`/`⚠` in log strings; the default Windows console
   was decoding them as CP437 (`ΓÇö` etc.) on most hosts. One-line
   fix; no-op for the GUI build (no attached console).

2. **SE table column 2 quirk** (commit `83a3cb5`).
   Reading FUN_00499c63 revealed the +4 column of the 110-entry SE
   table at `&DAT_005d1584` is actually a voice-group / SE-AudioPath
   selector, not "zero padding" as earlier notes claimed. In vendor
   data every +4 cell is zero (verified by re-reading the table from
   the unpacked exe), so path B + the cross-slot voice-stealing scan
   are dead code at runtime — every SE in vendor data routes to path
   A. New engine-quirks #46 documents the dormant routing; the C
   port keeps a single-column resource-ID table since +4 is constant
   zero. `audio.h` schema doc + SE-trigger header comment refreshed.

3. **`--play-se <slots>` harness flag** (`main.c`, commit `e134361`).
   Comma-separated SE slot indices fired post-boot via SetTimer at a
   configurable delay + interval
   (`--play-se-after-ms` / `--play-se-interval-ms`, defaults 1000 / 250).
   Bad indices rejected; cap 16 slots per invocation. Gives phase B
   an in-isolation tester without needing to wire SE calls into the
   title scene's still-unported sim_a body.

4. **SE phase B: live `audio_play_se`** (the main course; this commit).
   - `tools/extract/se-rc.py`: walks `vendor/unpacked/se-extracted/`
     and emits a windres `.rc` with one `<id> WAVE "<abs_path>"`
     entry per WAV (109 entries — slot 2's `0x0135` is in the
     lookup table but absent from `.rsrc`, faithful to engine).
     Gitignored output (`src/se.rc`).
   - `src/Makefile`: regenerates `se.rc` → `se.res.o` via
     `i686-w64-mingw32-windres -O coff -c 65001`; both .exe outputs
     now embed the 3.1 MB SE blob payload (binary grew ~2 MB → ~4 MB).
   - `src/audio.c`: `audio_init` gains 2× `CreateStandardAudioPath`
     for SE-A / SE-B paths (per engine-quirks #46 path B is dead in
     vendor data but the engine creates both, so we do too), plus
     a per-slot `FindResourceA`/`LoadResource`/`LockResource`/
     `IDirectMusicLoader::GetObject(DMUS_OBJ_MEMORY)` loop for the
     110 SE segments. Missing-resource slots silently skip (slot 2
     case). `audio_play_se` gains a Win32 body that mirrors
     `FUN_00499c63`'s bare path: Release prior SegmentState8 →
     explicit Stop → PlaySegmentEx → QueryInterface-upgrade to
     `IDirectMusicSegmentState8` → Release the un-upgraded pointer.
   - Boot log gains the SE preload count:
     `audio: init ok — 21 BGM segments + 109/110 SE segments preloaded`
     `(1 missing/skipped)`.
   - **Two documented engine deviations** (revert when
     `audio_fade_apply` lands — see `audio-backend.md` "Next steps"):
     1. PlaySegmentEx uses `DMUS_SEGF_SECONDARY` (0x8000) instead
        of the engine's `DMUS_SEGF_QUEUE` (0x80). Without SECONDARY,
        primary-segment semantics duck BGM under every SE — Recettear
        doesn't do that. The engine sidesteps via a per-call
        SetVolume that we haven't ported yet.
     2. Explicit `SetVolume(0, 0)` on both SE paths at the tail of
        `audio_init`. Defensive nudge after observing inaudible SE
        on at least one Windows host; the engine never relies on
        path defaults because it SetVolumes per call.

Test count unchanged at 452/452 — phase B's Win32 body is `#ifdef _WIN32`
so the Linux unit suite can't exercise it directly; the trace-shell
tests cover the slot-bounds + JSON escape path that runs unconditionally.

Smoke trace from the final run (`--play-se 0,12,2,69`):
```
{"t_ms":107, "kind":"bgm_swap","track":0,"name":"bgm/retitle2010.wav"}
{"t_ms":1099,"kind":"se_play","slot":0,"name":"se_000_id013d"}
{"t_ms":1714,"kind":"se_play","slot":12,"name":"se_012_id0148"}
{"t_ms":2325,"kind":"se_play","slot":2,"name":"se_002_id0135"}   ← traced; PlaySegmentEx skipped (NULL segment)
{"t_ms":2930,"kind":"se_play","slot":69,"name":"se_069_id029d"}
```

**Carries to next session:** `audio_fade_apply` is the unlock for both
reverting the two engine deviations AND making the per-tick BGM/SE
fade animations real. Need a per-tick fade-counter producer (decay
the path-A/B and BGM fade counters each frame, fire SetVolume against
`audio_fade_compute`). Probably lands next to a wider sim/render
ticker port.

## 2026-05-21 — Autonomous session: 6 audio + harness tasks landed

Worked the queue at `docs/autonomous-session-tasks.md` end to end.
Six commits, ASan/UBSan clean, Win32 build clean, smoke run clean.
Test count 413 → 452.

1. **Per-pixel diff overlay** (`tools/smoke-test.py`). `diff_runs`
   now emits `<run>/diff/frame_NNNNN.png` (new frame with red-tint
   on pixels where any RGB channel diverged ≥ 4) + a tiled
   `<run>/diff-overlay.png` via contact-sheet. Synthetic tests in
   `tools/test_smoke_diff.py` cover self-diff (zero mask),
   hand-modified rect (mask matches exactly), and size-mismatch
   clipping. Mean SSIM self-diff = 1.0000.

2. **MCI debug command recorder** (`src/audio_mci.{c,h}`). Faithful
   port of FUN_00451874 + FUN_00451863. 60×80 buffer at
   `&DAT_06a47aac` (size derived from the dword-zero loop —
   880-byte-per-row arithmetic in earlier notes was wrong; actual
   is 4800 bytes total). 10 new tests including the
   channel-spans-into-next-row engine quirk.

3. **Volume cos-curve fade — math half** (`src/audio_fade.{c,h}`).
   Reverse-engineered: FUN_00503994 is actually a CRT cos() wrapper
   (Ghidra showed it as a 9-byte stub but the disassembly is full
   FPU plumbing). The actual fade math is
   `cos(angle) * (target_centibel + 9600) - 9600`, with frame 0
   short-circuited to hard -10000 (the engine's math curve only
   asymptotes to -9600 — preserved as engine inconsistency).
   `tools/plot/curve.py` + `tools/plot/render_audio_fade_curve.py`
   write `runs/audio-fade-curve.png` (the C tests pin endpoints
   + monotonicity in 1..8 + one hand-computed spot value).
   SetVolume hookup deferred to SE phase 2.

4. **`--audio-trace` JSONL emitter** (`src/audio.{c,h}` +
   `src/main.c` + `tools/smoke-test.py`). Opt-in NDJSON log of
   audio events. Schema:
   `{"t_ms":<u32>,"kind":"bgm_swap"|"se_play","track":<int>|"slot":<int>,"name":<str>}`.
   `audio_trace_json_escape` exposed as a pure-C helper (test
   build doesn't need windows.h). `--audio-trace` flag on the
   smoke harness writes to `runs/.../audio-trace.jsonl`. Verified
   end-to-end against the title-music boot — one line, parses as
   valid JSON.

5. **SE backend phase A** (`src/audio_se_names.{c,h}` +
   `src/audio.c::audio_play_se` + `tools/extract/se-wavs.py`).
   **Major correction**: the autonomous-session brief said "27
   SE entries under RT_RCDATA"; the engine ships **110 entries**
   under a custom *named* resource type `"WAVE"` (string type
   name at `&DAT_005d1ac8`). Two disjoint ID ranges
   (`0x13d..0x182` and `0x29d..0x2c6`) with documented out-of-order
   pairs at slots 2 and 39/40 plus a missing ID at slot 107/108.
   The C table reproduces all the quirks; the extractor walks the
   PE `.rsrc` tree (custom-type-aware) and dumps 109 of 110 WAVs
   (slot 2's id 0x0135 is referenced but absent from `.rsrc` —
   handled by FindResourceA returning NULL). Vendor cross-check
   test re-reads the table from the exe at boot. `audio_play_se`
   is a trace-only shell for now (bounds + se_play emit + return 1);
   **defers** the windres .rc + 2 SE AudioPaths + FUN_00499c63
   live PlaySegmentEx to a follow-up.

6. **Audio-backend doc refresh** (`docs/findings/audio-backend.md`).
   Fade-curve formula + per-frame centibel table, SE resource
   layout (110 entries, custom WAVE type, the two disjoint ranges),
   `--audio-trace` schema + JSON escape rules, constants table
   gained the four fade/SE-type addresses, next-steps list rewritten
   around what actually remains (SE phase 2 dominates).

**Carries to next session:** SE phase B is the unblock for any
in-game SFX. The extractor + table + trace surface are all in place,
so phase B is mechanical Win32 wiring + windres glue. The
`audio_fade_apply` SetVolume hookup naturally lands at the same
time (its second consumer is SE volume blending).

## 2026-05-21 — Harness: auto contact-sheet on smoke runs + ranked roadmap

Small harness commit that lands ahead of the SE-backend port. Two halves:

**Auto contact-sheet.** `tools/smoke-test.py` now invokes the existing
`tools/contact-sheet.py` after a `--capture` run and writes
`runs/<...>/contact.png` (single-source grid) — and additionally
`diff-contact.png` (golden | new, side-by-side rows) when
`--diff-against` is set. Subprocess-shells so behavior matches running
the script by hand. Default cadence unchanged (still 1 fps); the new
PNG is what makes a smoke run multimodally inspectable from inside
assistant conversations. `--no-contact-sheet` for opt-out.

**Roadmap doc.** `docs/harness-roadmap.md` captures the ranked
graphics/audio tooling plan (Tier 1 immediate wins → Tier 3 heavier
work). Notable Tier 3 entry: retail-side Frida instrumentation with
**state-forcing hooks** — inject save / scene state into the
unmodified retail exe so deterministic golden frames can be produced
without an interactive play-through. Cross-link added to `PLAN.md` §6.

**No source code changed.** Existing `--capture` still works exactly as
before; the contact sheet is an additive output. Verified end-to-end
with a back-to-back boot run + self-diff (SSIM 0.9996, both contact
PNGs render cleanly).

## 2026-05-21 — Engine quirk #44 filed (button auto-repeat double-fire)

Retro doc entry into `docs/findings/engine-quirks.md`. Quirk was
already cited in the title-sim port commit (`c2b144c`) and reproduced
by `test_sim_button_ring_repeat_pulses_after_settle`, but the engine-
quirks tour was missing the writeup. Now between #43 and #45 with the
fire/fire/gate/gate/gate steady-state pattern explained, the
unintentionality argument, and refs to `src/sim.c` + the test. The
"(Quirk #44 not yet retro'd)" placeholder note at the top of #45 is
removed.

## 2026-05-21 — DirectMusic 8 audio backend: init + BGM playback (FUN_00498ef4 + FUN_00499200)

Title music is now audible. The selector's stubbed swap-dispatch (from
the sim_b port two commits ago) now drives real `PlaySegmentEx` calls
on a `DMUS_APATH_DYNAMIC_STEREO` audio path. User confirms `bgm/
retitle2010.wav` plays on Windows host via WSLInterop. SE / volume-fade
/ MCI debug bridge still stubbed — next commit.

**What landed:**

- **`src/audio.{c,h}` — DirectMusic 8 backend (BGM-only slice).** Mirrors
  `FUN_00498ef4` (init: CoInitialize → CoCreateInstance Performance →
  InitAudio → CreateStandardAudioPath BGM → CoCreateInstance Loader →
  SetSearchDirectory → preload all 21 BGM segments with SetRepeats +
  Download) and `FUN_00499200` (track-swap: guard duplicate, release
  prior segment-state, PlaySegmentEx with the new segment).
- **21-entry BGM filename table** extracted from `.data` at `0x005d190c`
  via `tools/analyze/pe.py`. Lives in `audio_bgm_filenames[]` as a pure-C
  array so tests can verify track indices.
- **One-shot lookup** — `audio_is_one_shot_track(int)` reproduces the
  engine's `(iVar5 == 0x28 || 0x2c || 0x34 || 0x4c)` guard. Treasure,
  fanfare, clear, staff get `SetRepeats(0)`; everything else gets
  `0xffffffff` (infinite).
- **Music-bridge** — `src/music.{c,h}` exposes a new
  `music_swap_fn_t g_music_swap_fn` pointer. `audio_init` installs
  `audio_play_track_adapter`; `audio_shutdown` clears it. Test builds
  (host gcc, no `_WIN32`) leave the pointer NULL → selector still does
  bookkeeping (`swap_call_count++`, etc.) but doesn't fire a real play.
  This keeps the test build free of `windows.h`/`dmusici.h`.
- **`src/main.c` wiring** — `audio_init(g_hwnd)` slot 17 in the WinMain
  bootstrap (per `docs/findings/winmain-and-bootstrap.md`), right after
  `tables_load_all()` + `scene_title_*_init`. Shutdown call before
  `timeEndPeriod`. Failure is non-fatal (logs to stderr and continues
  muted) — matches the engine's behavior.

**Identified GUIDs (extracted via pe.py + matched against mingw-w64
`dmusici.h`):** see `docs/findings/audio-backend.md` for the full table.
The mingw-w64 `libdxguid.a` exports them natively so the build links
against the standard symbols (no inline GUID definitions needed).

**Tests.** 6 new (total 413, was 407):
- `audio_bgm_table_has_21_entries` — table size + every slot non-NULL +
  every filename has a `bgm/` prefix.
- `audio_bgm_table_well_known_indices` — track 0=retitle2010, 1=town,
  7=over, 11=fanfare, 20=water.
- `audio_bgm_filename_bounds` — bounds-check the indexing helper.
- `audio_one_shot_set_is_exact` — every index `i ∈ [0,21)` correctly
  classified.
- `audio_music_bridge_fires_on_swap` — installing a stub fn into
  `g_music_swap_fn` causes it to fire on a track change (selector ran
  → bridge called with `MUSIC_TRACK_TITLE`).
- `audio_music_bridge_skipped_when_null` — with NULL pointer, selector
  still bookkeeps but doesn't call out.

**Verified at boot:**

```
audio: init ok — 21 BGM segments preloaded
music: swap #1 → track 0 (frame 1)
```

**Not yet ported (next-commit candidates):**

- **SE backend** — port the SE-init loop (27 `RT_RCDATA` resources via
  `FindResourceA` + `loader->GetObject` with `DMUS_OBJ_MEMORY`) + two
  SE AudioPaths + `FUN_00499c63` (per-channel start/stop). Unblocks all
  UI sound cues (cursor move, button click, etc.).
- **Volume animation** — `FUN_00499583` sin-curve fade. Needed for the
  title-screen fade-out band (frames `0x1b6d..0x1ba7`) and in-game fade
  transitions. The selector already computes `g_music.target_volume`;
  the apply call is what's missing.
- **`DMUS_AUDIOF_3D` warning under Wine** — DirectMusic with full audio
  flags can be brittle on some Wine builds (we run on native Windows
  via WSLInterop, so this isn't a current issue, but documenting for
  the Wine port).

## 2026-05-21 — title menu A-press → real EXIT (FUN_0049a59e press-dispatch, item==3)

The smallest scene-transition slice: the EXIT menu item now actually
quits the game. Pressing A on the EXIT line plays the 15-frame select
pulse, then `PostMessageA(hwnd, WM_CLOSE, 0, 0)` fires (the engine's
literal dispatch for `iVar1 == 3` in FUN_0049a59e L524-528), the main
loop's `GetMessageA` returns 0, and shutdown runs cleanly.

This is the first real A-press transition out of the title — every
prior commit landed the player on the title indefinitely.

**What landed:**

- **`scene_title_anim_t.pending_action`** — new outbox field. The
  pure sim sets it to the menu item code (`SCENE_TITLE_MENU_*`) on
  the frame `select_phase` reaches 0xf. Default `SCENE_TITLE_ACTION_NONE
  = -1`. Consumer (main.c) clears it after handling.
- **`scene_title_sim` select-pulse tail rewritten.** Previously
  resetting `select_phase` to 0 at 0xf; now matches the engine —
  pins at 0xf and writes `menu->items[cursor_pos]` into
  `pending_action`. Subsequent frames don't replace the latched
  value (so consumer sees the same action on every poll until
  cleared).
- **`main.c` press-dispatch consumer.** After each
  `tick_step_win32` call, polls `g_scene_title_anim.pending_action`:
    - `SCENE_TITLE_MENU_EXIT` (3) → `PostMessageA(g_hwnd, WM_CLOSE, 0, 0)`,
       leaves `select_phase` at 0xf (window's closing anyway).
    - Anything else → log "menu item N selected — destination scene
      not ported yet" once per item per session, snap `select_phase`
      back to 0 so the player can pick a different item.
- **Engine fidelity for EXIT is bit-for-bit:** same window handle
  (the engine's `DAT_073dfc7c` is our `g_hwnd`), same `WM_CLOSE`
  (0x10) message, same source line in `FUN_0049a59e:526`. The
  engine's `DAT_0964356c = 1` set before the PostMessage is a
  flag we don't need (`scene_title.c` doesn't have any reader of
  it yet; will land if/when we find one).

**Tests.** 4 new (total 407, was 403):
- `test_scene_title_sim_select_phase_pins_at_fifteen` — replaces the
  old "resets at fifteen" test; verifies new pin behavior.
- `test_scene_title_sim_pending_action_default_is_none` — init seed.
- `test_scene_title_sim_pending_action_set_on_select_complete` —
  full pulse cycle latches `pending_action = items[cursor_pos]`.
- `test_scene_title_sim_pending_action_exit_on_exit_item` — move
  cursor to EXIT via DOWN×3, run pulse, assert
  `pending_action == SCENE_TITLE_MENU_EXIT`.
- `test_scene_title_sim_pending_action_set_once_not_replaced` —
  subsequent frames preserve the latch.

**Boot trace unchanged** (same 17-table init, same recet.ini values).
EXIT verification requires synthetic input which the smoke harness
doesn't support yet — covered by the unit tests instead.

**Not yet ported (every other A-press destination):**
- `SCENE_TITLE_MENU_NEW_GAME` (0) / `NEW_HAS_SAVE` (4): engine sets
  `DAT_0964351c = 1` + `DAT_0438bed4 = 1` (loading transition that
  spins for 30 frames then jumps to scene 1 "town"). Needs the town
  scene at minimum.
- `SCENE_TITLE_MENU_OPTIONS` (2): engine sets `DAT_09643524 = 2` +
  `menu_folding_out = 0` (options submenu slides in). Needs the
  options-submenu render branch of FUN_0049c644 + an "options" sub-
  state machine.
- `SCENE_TITLE_MENU_RANKING` (7): engine sets `DAT_09643524 = 3` +
  calls FUN_0049f012(1). Score/ranking persistence not ported.
- `SCENE_TITLE_MENU_CONTINUE_ANY` (1) / `CONT_HAS_SAVE` (5): save-
  bank reader → scene 1 with restored state. Needs save-bank port.
- `SCENE_TITLE_MENU_SURVIVAL` (6) / `HIDDEN_CHAR` (8): even further
  out; both gate on unlock flags we don't simulate.

## 2026-05-21 — sim_b music selector ported (FUN_0049966a)

Second half of the per-frame sim, the music-track selector. Wired into
`tick_cb.sim_b` so the scheduler now drives both halves. No audible
output — the actual DirectMusic backend (FUN_00499200 load+play,
FUN_00499583 volume apply, FUN_00499c63 SE stop) is still stubbed —
but the selector picks the same track index the engine would on every
frame, and a Win32 boot still renders the title pixel-identically to
the prior commit.

**What landed:**

- **`src/music.{c,h}` — full FUN_0049966a port.** Pure-C
  `music_select_track(state, ctx)` returns the desired track index
  (or `-1` keep / `-2` stop sentinel) for any combination of
  `scene_state` + `title_frame_counter` + pause/modal flags +
  forced override. Pure-C `music_step(state, ctx)` runs the whole
  body: SE-stop sweep (110 slots), fade-phase latch, music-speed
  update (0.75 at state 10, 1.0 elsewhere), frame-count advance,
  pause-modal-clear, target-volume curve for the title fade band,
  selector dispatch, and the stubbed swap call.
- **Title-screen specifics:** state 0 + `frame_counter ∈ [0, 0x1b6d)`
  → track 0 (`bgm/retitle2010.wma`) via the `-1 → 0` masking quirk;
  `[0x1b6d, 0x1ba7)` → fade-out volume ramp (1.0 → ~0.90 over 59
  frames, formula `1.0 - (f - 0x1b6c) / 600.0`); `f == 0x1ba7` →
  STOP sentinel (-2); `f > 0x1ba7` → no change.
- **Non-title state branches ported faithfully:** state 7 returns
  NONE (no change); state 9 + `quest_pending != 0` → FANFARE (0xb);
  states 6/8/0xb/0xd/0xe/0xf/0x10 → TOWN (1); pause-modal-override
  (`pause_modal_state != 0 && pause_modal_a == 0 && pause_modal_b
  == 1`) → OVER (7). Stage-dispatch branch (states 1..5, 10, 11, 12
  reading `&DAT_068dd3fc[stage * 0x6cf]`) is stubbed to NONE — lands
  when the stage descriptor table loads.
- **Track-table extracted:** `tools/analyze/pe.py` pulled the 12
  music filenames from .rdata 0x5d1ae4..0x5d1b98 and the 8-entry
  title BGM table at 0x5d1be0 (entries 0/1/3/4/5/6/7/8 — track id
  in low dword, `1` in high dword as some kind of mode flag).
  Stable across rebuilds; embedded in `src/music.c` as constants.
- **Wired into the scheduler.** `tick_cb.sim_b = music_step_default`
  in `src/main.c`. The default wrapper pins scene state to 0 and
  reads `title_frame_counter` / `title_cursor_anim` from
  `g_scene_title_anim`. `title_submenu_state` is hardcoded to 0
  (the press-dispatch branches of FUN_0049a59e that would mutate
  it haven't ported yet — for now the title BGM lookup gate never
  fires and we stay on track 0 forever, matching the engine).

**Stubbed (with comments at each cut-point):**

- The track-swap call (`FUN_00499200`) — would normally load the
  DirectMusic segment from the per-track filename pointer at
  `DAT_09643038[track]` and start playback. Stubbed to just update
  `current_track` and bump `swap_call_count`. When the audio
  backend lands, replace the increment with a real swap.
- The SE-stop call (`FUN_00499c63`) — clears the slot and bumps a
  counter; backend would actually stop the SE channel.
- The volume-animation tail (`FUN_00499583` + `FUN_00451874` MCI
  "VOL %d" command + `DAT_09643108->SetVolume`) — short-circuited
  the same way the engine does it (`DAT_09643108 == 0`).

**Engine quirk #45 — title BGM lookup masks `-1` to `0`.**
`FUN_0049a558` returns `-1` when the cursor-anim+submenu gate fails
(which is always at boot). The caller in `FUN_0049966a` does
`uVar5 = -(uint)(uVar5 != -1) & uVar5`, which masks `-1 → 0`. So
the title screen always plays track 0 (`bgm/retitle2010.wma`)
regardless of language. The table at `0x5d1be0` only gets consulted
once the player opens a submenu (cursor folds fully out,
submenu enters state 4). Documented as
`docs/findings/engine-quirks.md` §45. Faithfully reproduced by
`src/music.c:title_bgm_select` + the `(pick == -1) ? 0 : pick`
conditional in `music_select_track`.

**Tests.** 27 new (total 403, was 376):
- `test_music.c`:
  - `music_init_engine_data_defaults` — initial values match
    .rdata writers (current=-1, forced=-1, duration=0x258, vol=1.0,
    speed=1.0, language=-1, pending_swap_clear=1).
  - 8× selector cases (title bare/fade-band/stop/post-stop,
    submenu-open with valid + invalid language, forced override,
    pause-modal-override on/off, state 7/8/9 quirks, town states
    sweep).
  - 12× step cases (frame-count advance, bare-path dispatches
    track 0 once, state-10 drops speed to 0.75, global pause
    blocks dispatch, SE-stop sweep clears and counts, pending
    fade-phase latches, no-modal clears fade + override, paused_b
    preserves override, target_volume default + fade band ramp,
    stop sentinel dispatches -2, forced override dispatches with
    modal active).

**Visible result.** Boot trace unchanged from the previous commit
(all 17 tables load, recet.ini reads, title BG renders at 1024x768,
exit clean). No audio output yet — backend isn't ported. The
`current_track` global progresses from -1 → 0 → 0 → 0 → … silently;
once the audio backend lands, `bgm/retitle2010.wma` will start
playing on the second sim_b tick.

**Still deferred from the prior commit (this one didn't fix):** the
non-selected menu items still look washed out vs retail — needs an
RE pass on FUN_0049c644's draw block (likely a missing SPECULAR
texture-stage overlay). Unrelated to sim_b.

**Not yet ported (per sim_b's contract):**
- The actual DirectMusic backend (FUN_004902fe init,
  FUN_00498ef4/FUN_00499200 segment load+play, MCI volume bridge).
  Bigger separate concern; needs DirectSound + WMM glue.
- The stage-dispatch branch (states 1..5/10..12 with per-stage
  music ID via `&DAT_068dd3fc[stage * 0x6cf]`). Gated on stage
  scenes porting — the stage descriptor table at `DAT_068dd3fc`
  has stride 0x6cf bytes per entry; data loader not ported.
- The title submenu carrier (`DAT_09643524`). Gated on the
  press-dispatch branches of FUN_0049a59e. Until then, the
  music_step wrapper hardcodes submenu_state = 0.

## 2026-05-21 — Title sim ported (FUN_0049a59e bare path + minimal sim_a)

The title menu now animates: BG scroll keeps going under focus loss
(scheduler drives it now, not the render path), the selected item's
brightness pulses via the slow `pulse_phase` LFO, and UP/DOWN move the
cursor with auto-repeat. Three new pieces:

**What landed:**

- **`src/sim.{c,h}` — minimal sim_a (FUN_004536cb).** Ports the
  button-state ring at the top of the function: per-bit
  current/prev/pressed/held-with-repeat masks for two players (the
  engine's DAT_073dddd0..d6 quad + the 16-short DAT_073dddda repeat
  counter array). Pure-C helper `sim_button_ring_update` exposes the
  per-bit math for tests. Scene dispatch is wired only for state==0
  → `scene_title_sim_default`; the 16 other scene arms (1..16) and
  the four mode-escape sub-blocks (DAT_06a499.. flags) are omitted
  until those scenes port.

  Tail of FUN_004536cb is reduced to `g_sim_frame_count++` — the
  time-dilation float math and the `FUN_004526ab` post-frame helper
  it calls were stubbed (no consumers yet in our skeleton).

- **`scene_title_sim` in `src/scene_title.c` (FUN_0049a59e bare path)**.
  Pure-C; mirrors the path through the function that's actually
  reached at end of `FUN_0049a3a3` ("bootstrap done"), with no scene
  transitions pending and no submenu open (DAT_09643524 stays 0,
  cursor_anim stays clamped at 0 because `menu_folding_out=1`). Runs
  per frame: `cursor_anim` slide (decrement toward 0), `frame_counter`
  advance, A-pressed → `select_phase = 1`, UP/DOWN held with
  auto-repeat → `cursor_pos = (cursor_pos ± 1) mod count`, tail
  `pulse_phase++`. Once `select_phase` reaches 0xf the engine would
  dispatch a scene transition — bare-slice resets it to 0 (no scenes
  to receive control yet, so the player can't actually leave the
  title).

- **`scene_title_anim_t` extended with `menu_folding_out`** (mirrors
  DAT_09643528 — the direction flag for `cursor_anim`). New
  `scene_title_anim_init_fresh` seeds the post-FUN_0049a3a3 state
  (all zero except `menu_folding_out = 1`). Scene-0 state moved from
  static locals in `src/main.c` into `g_scene_title_menu` /
  `g_scene_title_anim` / `g_scene_title_assets_loaded` exports in
  `src/scene_title.c` so sim.c and main.c both reach them by name.
  `main.c::render_dispatch` lost its placeholder `frame_counter++`
  — the sim owns that now.

**Engine quirk #44 — button auto-repeat double-fires across reload.**
The 16-short repeat counter in FUN_004536cb uses two mutually
exclusive `if` branches: `(rep < 1) → rep = 4` (no decrement) and the
`else { rep--; if (rep > 0) clear bit }` gate. So when a held bit's
counter drops to 0, the bit fires on *that* frame (the `> 0` test
fails), AND on the next frame (the reload-to-4 path skips the gate
entirely). Net auto-repeat pattern after the initial 12-frame settle
is fire/fire/gate/gate/gate, period 5 frames. Reproduced exactly;
covered by `test_sim_button_ring_repeat_pulses_after_settle`.

**Tests.** 20 new (total 376):
- `test_sim.c` (8 tests) — button ring rising/held/release/multi-bit,
  the full auto-repeat cycle including the double-fire quirk,
  `sim_init` zeroing, `sim_step_a` frame advance + input piping.
- `test_scene_title_sim.c` (12 tests) — init seeding, idle frame
  advance, pulse-phase ticking under all `cursor_anim` values,
  cursor wrap UP/DOWN, A-press select-pulse start + 15-frame reset,
  input gating during select-pending, A-on-`held`-only is no-op,
  frame-counter past 0x1bc6 ignores input, NULL guards.

**Visible result.** Title screen at 1024×768 looks the same as the
prior commit (positions unchanged), but the selected-menu item's
brightness now visibly oscillates from frame to frame (the engine's
`pulse_phase / 0x2d` slow LFO), and cursor movement responds to the
keyboard/pad bindings. BG vertical scroll continues uninterrupted
when the window loses focus (sim runs from the scheduler at its
fixed 60 Hz cadence, not piggybacking on render).

**Still open from the render commit (this one didn't fix):** the
non-selected menu items look washed out compared to the retail
build — likely a missing texture-stage SPECULAR overlay or per-item
outline pass we haven't found yet. Not gated on the sim port; needs
its own RE pass on FUN_0049c644's draw block.

**Not yet ported:**
- `FUN_0049966a` (sim_b — music track selector). Independent of
  sim_a; lands as its own commit. Scheduler still tolerates a NULL
  `.sim_b`.
- The 16 non-title scene arms of FUN_004536cb (states 1..16) and
  the mode-escape paths (DAT_06a499.. flags). Each is gated on a
  scene that hasn't ported.
- A-press scene transitions out of the title (NEW GAME / OPTIONS /
  RANKING / etc.). Each is gated on its destination scene's port;
  for now the player is parked on the title indefinitely (the 15-
  frame select pulse plays then resets cleanly).
- The intro-movie attract loop (`recet_op.wmv` at frame_counter ==
  0x1be4) — waits on a video player port.

## 2026-05-21 — Title scene wired into main loop (partial FUN_004547ab)

Fifth and final commit of the title-screen port. The render
dispatcher now drives `scene_title_render` on every frame — debug
magenta gone, actual title art on screen. Also includes a critical
correction to `render_quad_add`'s screen-resolution scaling.

**What landed:**

- **`render_dispatch` in `src/main.c`.** Replaces `frame_render_stub`
  as the tick scheduler's `.render` callback. Clears to the engine's
  state-0 ARGB `0xff17f0ff` (pink-blue, visible only at the edges
  before bg2.bmp covers everything), BeginScene, calls
  `scene_title_render`, EndScene, frame-capture sample, Present.
  The full FUN_004547ab dispatch (state 1..16 + device-loss recovery
  + the inner-scene sub-block) lands as those scenes port; for now
  state==0 is the only path.
- **`scene_title_load_assets` + `scene_title_menu_init_fresh` now
  wired** into WinMain after `tables_load_all`. `render_quad_init`
  runs once before that to prefill the static vbuf.
- **Position-scaling bug fixed in `render_quad_add`.** Ghidra's
  decomp of FUN_00404efc hides two FPU multiplications inside its
  `__ftol` artifact calls; the engine actually scales ALL FOUR dst
  components by `screen_w / 640`, not just the width/height. Caught
  by visual comparison against the stock title at 1024×768: with
  positions un-scaled, the menu items + corner element + copyright
  ribbon all sat ~150 px too high. Fix: scale + truncate `dst.x` and
  `dst.y` the same way as `dst.w/h`. One existing test
  (`render_quad_scale_widens_but_not_position`) renamed and updated
  to assert the new, correct scaling. The PROGRESS entry from the
  earlier render-quad commit had the wrong claim — superseded.
- **Animation hack.** `g_title_anim.frame_counter` advances from
  `render_dispatch` so the BG-scroll counter keeps ticking until
  the sim port (FUN_0049a59e) lands and takes over. As a result,
  the BG stops scrolling when the window loses focus
  (`g_paused → WaitMessage → tick scheduler idle`); harmless and
  self-resolves with the sim port.
- **Tests still 356/356.**

**Visible result.** Stock-equivalent title-screen layout at 1024×768:
RECETTEAR logo + scrolling town background + "An Item Shop's Tale"
ribbon + scrolling fuki band + "Start a new game" bubble + NEW
GAME / ITEM ENCYCLOPEDIA / OPTIONS / EXIT menu + EasyGameStation
copyright at the bottom. Positions match the retail build pixel-
for-pixel on the static frame.

**Known visual differences (deferred to sim port):**
- Non-selected menu items render slightly different from stock —
  likely a missing texture-stage SPECULAR overlay or a per-item
  outline pass. Engine's `D3DTSS_COLOROP = D3DTOP_ADD` blend is
  matched, but there may be a second draw pass we haven't found.
- Selected-item brightness is frozen at 0x9f (frame 0 of the
  pulse) — the sin-driven pulse will animate once sim ticks
  `select_phase` / `pulse_phase`.
- Cursor-anim slide (`cursor_anim` counter) is frozen at 0 —
  the menu-fold-in tween needs sim wiring.

## 2026-05-21 — Title scene render ported (FUN_0049c644 — bare path)

Fourth commit of the title-screen path. The actual draw routine —
`scene_title_render` in `src/scene_title.c` — emits the BG, frame
overlay, fuki corner, title01 band, and the menu glyphs (+ selected
highlight) via the new render_quad batcher. Sub-menu sub-screens,
the 7110-frame fade-in overlay, and the trailing UI helpers
(FUN_0043537e/47/17) are intentionally NOT ported here — all gated
on engine counters that stay at BSS-zero until the sim port lands.

**What landed:**

- **`scene_title_anim_t` struct** (`scene_title.h`) — captures the
  5 engine counters the render reads from (frame, cursor pos,
  cursor anim, select pulse phase, slow pulse phase). All five are
  consumed without any wiring; the sim port will advance them.
- **`scene_title_render(dev, menu, anim)`** — direct-line port of
  FUN_0049c644's bare path. Six draw passes:
  1. `bg2.bmp` 640x480 window vertically scrolled by frame counter
     (scroll_y = 360 - frame * 360 / 7140)
  2. `title_waku.tga` full-screen frame overlay
  3. `title_fuki.tga` 416x32 strip at the bottom (corner element)
  4. `title01.tga` 512x256 animated band, x = 64 - cursor_anim * 64
  5. Menu items loop (additive blend, `D3DTSS_COLOROP = ADD`):
     each item is a 160x32 tile (1.0× selected, 0.8× others) from
     fuki.tga at (224, code*32)..(384, (code+1)*32). Selected item
     pulses brightness via two sin()-driven layers (centered on
     0x7f + 0x20 = 0x9f at BSS-zero); non-selected use the engine's
     "1.33123e-43 denormal" trick which we resolve to literal 0x95
     greyscale.
  6. Selected-row decoration (3 tiles: top strip, big cursor glyph
     via the 9-entry LUT at PE 0x005d1cd4, bottom strip).
- **LUT extracted via** `tools/analyze/pe.py bytes 0x005d1cd4 36` —
  `{0,1,2,3,4,0,7,6,5}` mapping menu code → fuki tile. Embedded
  in `title_cursor_glyph_lut[]`.
- **Engine quirks faithfully reproduced.** The selected-item color
  expression `(((v | 0xffffff00) << 8 | v) << 8 | v)` is the engine
  literal — equivalent to `0xFF000000 | v<<16 | v<<8 | v` greyscale.
  Non-selected items get the bit-pattern-as-float trick where
  Ghidra shows `1.33123e-43` for what is really integer 0x95 stuck
  into a float-typed slot to defer the float→int conversion.
- **No call sites yet.** Compiles clean, but `frame_render_stub` in
  `main.c` still emits debug magenta. The next commit wires the
  render-dispatcher and replaces magenta with the actual title.
- **No new tests.** scene_title_render is D3D-bound and tested
  end-to-end via boot smoke once it's wired up. Existing 356 tests
  still pass.

**Deferred (lands with sim port FUN_0049a59e):**
- Sub-menu sub-screens (file-select, options, survival)
- Fade-in overlay (DAT_09643518 > 0x1bc6)
- Final UI helpers FUN_0043537e (sub-cursor), FUN_00435747 (frame
  counter overlay), FUN_00435117 (system-state overlay)
- Animation: the 5 anim counters stay at 0 until the sim ticks
  them; rendered title is static-frame-0 until then.

## 2026-05-21 — Title menu init ported (FUN_0049a324 + FUN_0049a43d)

Third commit of the title-screen path. The engine's menu-items
builder lands as `scene_title_menu_init` in the existing
`src/scene_title.{c,h}` module — pure-C, deterministic, no D3D
dependency. The function takes a save-bank query (4 booleans) and
produces the same 1..8-entry menu the engine generates at
`DAT_09643358..0x09643374`, including the cursor default and the
count-based Y stride / origin (`DAT_005d1bb4` / `DAT_005d1bb8`).

**What landed:**

- **`scene_title_menu_init(save, out)`** — pure-C builder. Encodes
  the engine's nine menu-item codes (named via a new enum, e.g.
  `SCENE_TITLE_MENU_NEW_GAME = 0`, `_RANKING = 7`, etc.), the
  layered "uVar1 = (adv_any ? 1 : 0) | (adv8_any ? 2 : 0)" check
  for which New / Continue variant slots in, and the count-based
  layout switch (counts 6/7/8 each have their own (stride, origin);
  ≤5 hits the default branch).
- **`scene_title_menu_init_fresh(out)`** — convenience wrapper for
  the fresh-boot path (no saves), which is what the wired-up code
  uses until save loading lands.
- **Engine quirk reproduced.** The "hidden character" menu slot
  (item 8, gated on `DAT_056e5788`) is also let in when
  `(uVar1 & 1) != 0` — i.e. when any save bank has Adventure
  cleared. That's because the engine's branch is
  `if ((bVar5) || ((uVar1 & 1) != 0))`, not the conjunction. Port
  matches.
- **Tests.** 6 new menu-builder tests, plus the existing 4 asset-
  table tests:
  - Fresh boot → 4 items `[0, 7, 2, 3]`, cursor 0, stride 33 / origin -16
  - Adv-cleared no adv8 → 6 items `[5, 4, 7, 8, 2, 3]`, stride 33 / origin -30
  - Adv-cleared + populated save → 7 items, cursor 2, stride 30 / origin -36
  - Full unlock (adv1 + adv8 + score) → 8 items, cursor 3, stride 27 / origin -36
  - Hidden-char alone → 5 items including item 8
  - Survival requires `uVar1 == 3` exactly, not just adv8 bit set
  Total: 356 passing (was 350).
- **No call sites yet.** Like commits 1 + 2, this lands the
  building block without wiring. The render-dispatcher port will
  call `scene_title_menu_init_fresh` on first scene-0 enter.
  Direct boot smoke exit=0; magenta unchanged.

## 2026-05-21 — Title scene texture loader ported (FUN_004733d5)

Second commit of the title-screen path. The engine's scene-0
asset-prepare function — called by FUN_004547ab's render dispatcher
the first time it sees `DAT_0438b1c0 == 0` — lands as
`scene_title_load_assets` in a new `src/scene_title.{c,h}` module.

**What landed:**

- **`src/scene_title.{c,h}`.** 7 sprite_t slots and a constant
  asset table (`scene_title_assets[]`) listing `(path, expected_w,
  expected_h)` for each:
  - `bmp/title_bg2.bmp`     1024×1024 — scrolling background panel
  - `bmp/title01.tga`        512×256  — animated band sprite
  - `bmp/title_fuki.tga`     512×1024 — menu glyph atlas
  - `bmp/title_waku.tga`    1024×512  — frame overlay
  - `bmp/pause.tga`         1024×512  — pause-menu submenu (loaded
                                        here, consumed elsewhere)
  - `bmp/result_bord01.tga`  512×256  — result screen
  - `bmp/dungeonbord.tga`   1024×512  — dungeon banner
  Asset paths verified byte-for-byte against PE rdata at
  VA 0x005c8688..0x005c86fc via `tools/analyze/pe.py str`. Texture
  sizes match the literal arguments passed to `FUN_0047193c` in the
  engine; all 7 are powers of two (engine convention).
- **Two-layer split.** Asset table is pure-C and unit-testable on
  Linux. The Win32 layer (`scene_title_load_assets`,
  `scene_title_get`, `scene_title_unload_assets`) wraps `sprite_load`
  and holds the 7 static `sprite_t` slots.
- **No wiring yet.** The render dispatcher port (later commit)
  will call `scene_title_load_assets` on first transition into
  state 0; until then nothing invokes it. Boot smoke direct exit=0,
  magenta clear unchanged.
- **4 new tests** in `tests/test_scene_title.c`: slot count, path
  match against the PE rdata strings, power-of-two sizes, and exact
  (w, h) match against the engine call sites. Total: 350 passing.

## 2026-05-21 — Render-quad primitives ported (FUN_00404efc + FUN_00405354 + FUN_0049b425 + FUN_00404e44)

First commit of the title-screen port path. The engine's 2D draw is
batched: every textured quad is appended to a static 8544-vertex
buffer at VA `&DAT_00605208`, then a single `DrawPrimitiveUP` call
emits all triangles at frame flush. This commit lands the batching
core — pure-C math + Win32 D3D wrappers — with no scene code on top
of it yet. The magenta debug clear is still the only visible output
of the boot smoke; that changes once the title texture loader and
the `FUN_0049c644` render port land in the next two commits.

**Subsystems landed:**

- **`src/render_quad.{c,h}`.** Four engine functions folded into one
  module:
  - `render_quad_add` — FUN_00404efc. Appends one quad (6 vertices,
    2 CCW triangles). `dst[4]` is xywh, `src[4]` is xyxy (engine's
    asymmetric input convention, faithfully reproduced). Width and
    height scale by `(screen_w / 640.0)`; top-left x/y do *not*
    (so UI position stays in 640-relative space while sprite size
    grows at higher resolutions). Top-left is integer-truncated to
    match the engine's `__ftol` pattern. UVs apply the +0.5
    half-texel inset on top/left only, not bottom/right — engine
    quirk, again reproduced.
  - `render_quad_flush` — FUN_00405354. Sets vertex shader to FVF
    `0x1c4` (XYZRHW | DIFFUSE | SPECULAR | TEX1, stride 32) then
    `DrawPrimitiveUP(TRIANGLELIST, count/3, vbuf, 32)` and zeroes
    the counter.
  - `render_quad_state_setup` — FUN_0049b425. Sets the 2D pre-draw
    states: SetVertexShader 0x142 (overridden by flush), FOG off,
    ALPHABLEND on, SRCALPHA / INVSRCALPHA. SRCBLEND/DESTBLEND are
    set *twice* in the original; the dup is reproduced. Texture
    stage 0 gets ALPHAOP/COLOROP=MODULATE + MIN/MAGFILTER=LINEAR.
    Engine relies on D3D8 defaults for COLORARG1/2 and ALPHAARG1/2
    — port does too.
  - `render_quad_init` — FUN_00404e44. One-shot vbuf initializer:
    z=0.0, rhw=1.0, specular=0 on all 8544 vertex slots. Render-
    quad-add never rewrites these fields, just like the engine
    (which leaves bytes 8..15 + 20..23 of each vertex untouched
    after the prefill).
  - `render_quad_bind` — small wrapper around `SetTexture(stage 0)`,
    matches the engine pattern of `dev->SetTexture(0, tex)` calls
    sprinkled between quad-add batches.
- **Two-layer file split.** Top of `render_quad.c`: pure-C math +
  buffer state (compiles on Linux for the ASan test build). Bottom
  (`#ifdef _WIN32`): D3D wrappers. Matches the convention from
  `src/input.c` and `src/tick.c`.
- **Screen-shake hook.** `render_quad_set_offset(ox, oy)` mirrors
  the engine's `DAT_0438cc18 / DAT_0438cc1c` global — added to every
  dst top-left at quad-add time. Untouched in this commit; lands as
  wired-up state when the camera-shake path ports.
- **Bounds check.** Engine has no overflow guard at 8544 vertices —
  a runaway frame would corrupt the global memory immediately past
  `DAT_00647e14`. Port returns 0 from `render_quad_add` once the
  buffer is full so a recoverable failure shows up in tests rather
  than an unrelated crash.
- **Tests.** 10 new pure-C unit tests in `tests/test_render_quad.c`:
  vbuf-prefill spot-checks, 6-vertex emission order (BR / BL / TR /
  BL / TL / TR), UV half-texel-inset asymmetry, screen scaling
  applied to size but not position, screen-shake offset, dst top-
  left integer truncation, buffer-full bounds check, zero-tex-dim
  rejection, reset-keeps-prefill, default-screen-w-zero-means-640.
  Total: 346 passing (was 336).
- **No call sites yet.** `main.c` is unchanged — the new module is
  compiled into `openrecet.exe` (matches `src/Makefile`'s
  `$(wildcard *.c)`) but no boot code calls it. Boot smoke direct
  (`build/openrecet.exe --max-duration-ms 3000` from
  `vendor/original`) exits cleanly with debug magenta unchanged.

**Pre-existing harness flake (unrelated to this commit):**
`tools/smoke-test.py` uses `preexec_fn=os.setsid` which breaks the
`SetTimer → WM_TIMER → DestroyWindow` self-termination path —
reproduced *with the prior input-poll build* (different exe SHA) as
well as the current build. Direct exe invocation terminates cleanly
at `--max-duration-ms`. Track-and-fix later; not blocking. Frames
are still captured and pixel-sampled magenta unchanged, confirming
no functional regression.

## 2026-05-21 — Input poll ported (FUN_0047b73c)

First of the four tick callees now lands real code instead of a NULL
stub. `tick_callbacks.input_poll` is wired to `input_poll` in
`src/input.c`, which mirrors the engine's keyboard + multi-joystick
DInput poll, decodes raw button state, and OR's it through the
recet.ini binding table into `g_input_state[0].buttons` each frame.

**What landed:**

- **`src/input.c` — three new pure-C decoders.** `input_joystick_decode`
  fans a `DIJOYSTATE2`-like input into a 20-bit "pressed" array
  (4 D-pad bits OR'd from POV-hat + stick axes, 16 buttons). POV is
  the standard DInput angle-times-100 encoding with explicit cases
  for all 8 cardinals and diagonals; centered (-1 / 0xFFFFFFFF) gives
  zero. Stick dead-zone is fixed ±500 on `lX`/`lY` (range was set to
  ±1000 in init, so 50% deflection). `input_apply_joystick_block`
  matches binding values against a per-joystick virtual-button range
  (`0x27 + joy_idx * 0x14`) and OR's the slot's bit into the output
  mask; `input_apply_keyboard_block` does the equivalent via the
  41-byte DIK lookup at `0x005cbc2f`.
- **`src/input.c:input_dik_table[40]` + `input_binding_mask[14]`.**
  Bytes extracted via `tools/analyze/pe.py bytes 0x005cbc2f 41`.
  Binding-slot bit layout (UP=0x04, RIGHT=0x01, DOWN=0x08, LEFT=0x02,
  A=0x10..E=0x100, skill0..4=0x200..0x2000) matches downstream
  readers — verified the camera-cursor code at lines 50410-50420 of
  `all.c` reads exactly these bits.
- **`src/input.c:input_bindings_load`.** Flattens
  `recet_ini.pad[2][9]` + `skill[2][5]` into the engine's
  interleaved per-controller layout (`pad[N][0..8]` then
  `skill[N][0..4]`, 14 shorts per controller block). 4 blocks total —
  blocks 2..3 stay zero (the engine's outer joystick loop reads BSS
  past the 2-controller end; see quirk #41).
- **`src/input.c:input_poll`.** Win32 wrapper that queries each
  acquired DI device, decodes raw state via the helpers above, and
  walks the 4 (joystick) / 2 (keyboard) binding blocks. Pre-clears
  the button accumulator at poll start — at the default
  `speed=0 / 60FPS` path this is bit-identical to the engine's
  "clear after render" pattern; at higher speeds the engine
  accumulates multiple polls per render and we don't. Revisit when
  the FUN_004547ab render port lands a post-render clear hook.
- **Init-side fix.** Switched the joystick `SetDataFormat` from
  `c_dfDIJoystick` (80 bytes) to `c_dfDIJoystick2` (272 bytes) to
  match the engine's custom DIDATAFORMAT at `0x0051c4cc`
  (`dwDataSize = 0x110`). The 80-byte format would have made
  `GetDeviceState(sizeof(DIJOYSTATE2), &st)` fail with
  `DIERR_INVALIDPARAM` — the previous boot smoke didn't hit this
  because nobody was calling GetDeviceState yet.
- **`src/main.c`.** Wires `input_bindings_load(&g_ini)` after
  `input_init`, and replaces the NULL `tick_cb.input_poll` with the
  real `input_poll` function. 4 engine quirks documented (#40-43).
- **Tests.** 20 new tests in `tests/test_input_poll.c` cover POV-hat
  all 8 directions, stick dead-zone, button-high-bit-only decoding,
  binding application with per-joystick virtual base, keyboard DIK
  mapping (with default vendor bindings), and the recet.ini
  flattening round-trip. Total: 336 passing (was 316).
- **Smoke boot.** `tools/smoke-test.py --scenario boot --duration 4
  --capture`: exit=0, 4 frames captured, all solid debug magenta —
  unchanged from the pre-input-poll baseline.

**Engine quirks documented (#40-43):**
- #40: both controllers' bindings funnel into player-0's single
  output slot (`(local_8 / 2) * 0x2a` integer divide).
- #41: joystick scan iterates 4 outer binding blocks but only 2 are
  populated; blocks 2..3 read BSS zero bindings and never match.
- #42: Poll-failure retry loop checks Acquire's return against
  `DIERR_NOTACQUIRED`, a code Acquire never produces; effectively a
  single-iteration loop.
- #43: each joystick is `Poll()`'d four times per frame (once per
  binding block); port collapses to one Poll + per-block apply for
  the same bit-for-bit output.

**Deferred until the next big port:**
- Post-render input clear with multi-poll accumulation semantics —
  needs `tick.c` to grow a callback hook; lands with `FUN_004547ab`
  (frame render).
- Sim halves `FUN_004536cb` / `FUN_0049966a` — they're the first
  readers of `g_input_state[0].buttons` and will exercise this
  port end-to-end.

## 2026-05-21 — Game-tick scheduler ported (FUN_0047be92 + FUN_0047be2f)

Heart of the engine's main loop is now driven by our own code instead
of the magenta-clear placeholder. The scheduler dispatches at the
configured fixed-timestep frame rate (60 FPS by default, selectable
via the speed table at `0x005cbc58`); the four callees it hands off to
— input poll, two sim halves, frame render — are stubbed for now and
land one-per-commit.

**Subsystems landed:**

- **`src/tick.{c,h}` — FUN_0047be92 + FUN_0047be2f.**
  - `tick_step_with_now(now_ms, has_device, &callbacks, &out_sleep_ms)`
    is the pure-C dispatcher, taking the four big callees as function
    pointers so the scheduler can stand alone and tests can mock them
    under ASan. All arithmetic in 1/3 ms units (matching the engine's
    `*3` + `% threshold` residue pattern), so sub-ms frame budgets
    work without floating point.
  - `tick_step_win32(has_device, &callbacks)` is the Win32 wrapper that
    bundles QPC + Sleep on top.
  - `tick_now_ms()` mirrors FUN_0047be2f: `QPC.QuadPart * 1000ull /
    QPF.QuadPart` truncated to uint32, with `timeGetTime()` fallback
    when either QPC value reads zero.
  - Speed-threshold table `g_tick_speed_thresholds[5]` extracted via
    `tools/analyze/pe.py bytes 0x005cbc58 32` and verified
    byte-for-byte against the engine.
  - All scheduler globals (`now_ms`, `prev_ms`, `delta_thirds`,
    `leftover_thirds`, `speed`, `pending_speed`, `state`, `state_alt`,
    `state_seed`, `frame_count`, `flag_dddd0`, `flag_dddfa`) live in
    a `g_tick` struct with named members matching the engine's
    DAT_073de618.. / DAT_073dfca4.. / DAT_0438ccd8.. globals.
- **`src/main.c` — main loop now drives the scheduler.** Replaced the
  `tick_and_present()` placeholder call with
  `tick_step_win32(g_d3d && g_dev, &tick_cb)`. The old debug-magenta
  clear/draw/capture/present body now lives in `frame_render_stub`,
  which is passed as the `render` callback — same visible behaviour,
  but now exercised through the real dispatcher. The other three
  callbacks (`input_poll`/`sim_a`/`sim_b`) are NULL until their ports
  land — the scheduler tolerates NULL callbacks.

**Behavioral validation:**

- 316 unit tests pass under ASan/UBSan (was 298). 18 new tests for
  `tick.c`:
  - Speed-threshold table bytes vs `.rdata` dump.
  - First-frame huge-delta normalisation (prev=0 → one tick + leftover=0).
  - Sim-loop count vs latched speed (`speed=0` → 1 sim, `speed=4` → 5).
  - Adaptive-sleep band (delta=29..40 in 1/3 ms steps; sleep_ms = 5, 4,
    1, 1, 0=busy-spin at the boundary).
  - Steady-state 60 FPS residue accumulation (delta=51 each frame with
    threshold=50 carries 1, 2, 3, … in `leftover_thirds`).
  - Input poll firing at ≥1/60 s delta but NOT when delta is smaller.
  - State machine: state=1 skips sim/render (but commits leftover/prev),
    state=2 transitions to 1 after one tick, state_alt mirrors state_seed.
  - `has_device=0` early-return after sim, before render (engine order).
  - Per-frame flags clear on tick, persist on delayed pass.
  - Pending-speed latches at the top of the next frame, not mid-frame.
  - NULL callbacks are safe (shell-port scaffolding).
- Boot smoke (`./tools/smoke-test.py --target openrecet --scenario boot
  --duration 3 --capture`): `exit=0, 3 frames`. Captured frames are
  solid debug magenta (160,32,96) at 1024×768 — visually identical to
  the pre-scheduler boot, just driven by `tick_step_win32` now.

**Engine quirks documented:**

- **Speed-threshold lookup is OOB-unsafe.** `(&DAT_005cbc58)[DAT_0438ccd8]`
  has no bounds check; the engine relies on the unmapped F-key handler
  only ever writing values in `[0..4]`. Test for `speed = -1`
  intentionally skipped — would force ASan to read OOB into adjacent
  globals.
- **Dead clamp in adaptive sleep.** Inside `if (remaining < 0xb)` the
  engine has `if (0x1e < remaining) remaining = 0x1e;` — unreachable
  given the outer guard (remaining is already < 11). Preserved as a
  comment in `src/tick.c`; harmless leftover from an earlier formula.
- **`state_alt = state_seed` is a no-op at boot.** Both globals are
  BSS-zero, so the per-frame copy doesn't do anything in practice. We
  preserve the write for byte-identical behaviour once whichever code
  writes `state_seed` lands.

**Deliberate divergences:**

- The four big callees (FUN_0047b73c input poll, FUN_004536cb /
  FUN_0049966a sim halves, FUN_004547ab frame render) are NULL stubs
  in this commit. The render callback is filled in by
  `frame_render_stub` (the old magenta-clear path) to preserve the
  visual smoke-test signal until FUN_004547ab lands.
- Pure-C scheduler entry takes callbacks as function pointers, where
  the engine has direct calls. Necessary for ASan-clean testing and
  to keep `tick.c` decoupled from the four big functions; once they
  all land we could fold them into direct calls again, but there's no
  real upside.
- Engine writes to `DAT_0438ccd8` and `DAT_0438ccdc` from an unmapped
  F-key handler. Our `g_tick.pending_speed` stays 0 until that
  handler lands — meaning we always run at the 60 FPS target.

**Not in this commit (deferred):**

- `FUN_0047b73c` — input poll. 325 lines of keyboard + joystick state
  read with POV-hat angle decoding (centidegree values 4500/9000/
  13500/18000/22500/27000/31500 → direction bits). Next.
- `FUN_004536cb` / `FUN_0049966a` — the two sim halves. 322 / 267
  lines respectively. Will read decomp before scoping.
- `FUN_004547ab` — frame render. 303 lines. Replaces the magenta-clear
  stub with the engine's real Clear+BeginScene+...+Present sequence;
  likely drives the 24 render-layer objects already initialised in
  `src/layers.c`.

**Files:**

- new `src/tick.{c,h}`, `tests/test_tick.c`
- updated `src/main.c` (include tick.h, replace tick_and_present with
  tick_step_win32 + rename old body to frame_render_stub),
  `tests/Makefile`, `tests/test_main.c`,
  `docs/findings/winmain-and-bootstrap.md` (new §"Game tick scheduler"
  + main-loop annotation + open-subsystems table refresh)

## 2026-05-21 — Pre-window block closed: RNG + math3d + FUN_00451790

Closes the last three open steps in the WinMain pre-window chain (steps
2, 3, and 5 from `docs/findings/winmain-and-bootstrap.md`). After this
commit, every call between `timeBeginPeriod` and `create_main_window` is
either ported or documented as a deliberate no-op.

**Subsystems landed:**

- **`src/rng.{c,h}` — engine LCG + time-to-seed.** Reimplements
  FUN_005041f6 (`x = x * 0x343fd + 0x269ec3; return (x >> 16) & 0x7fff`),
  FUN_00471089 (`rand / 32768.0` unit float), FUN_0050bcff (time → seed
  scalar with tzset-style constants pulled from `DAT_006038d0`: TZ
  offset 28800s, DST bias -3600s, epoch literal 0x7c558180), and a Win32
  wrapper for FUN_005045eb that bundles `GetLocalTime` +
  `GetTimeZoneInformation` → DST flag → seed write. The engine's RNG
  constants are bit-identical to MSVC's `rand()` so the first values
  from seed=1 are the canonical 41 / 18467 / 6334 / 26500 / 19169
  sequence — covered by a unit test (one of those compiler-fingerprint
  facts that's nice to have pinned).
- **`src/math3d.{c,h}` — vec3/mat4 helpers.** Portable C
  implementations of `vec3_normalize` (FUN_004a1f67),
  `mat4_lookat_rh` (FUN_004a3b52), `mat4_perspective_fov_rh`
  (FUN_004a3ee8), and `mat4_mul` with internal-temp aliasing support
  (thunk_FUN_004a2a03 = D3DXMatrixMultiply). The engine reaches D3DX
  through `FUN_004cdd9f`'s indirect-dispatch table (x87 / MMX / SSE
  backends selected at boot); we use a single portable implementation
  since algebraic equivalence is what matters at this layer.
- **`src/prewindow.{c,h}` — FUN_00451790 (WinMain step 2).** Writes the
  six named globals: `flag_b1c4=0, flag_b8cc=0, camera=(10,61,-203),
  flag_b1c0=1, flag_bf84=0, flag_bf88=0`. Then runs FUN_00404e44
  (8544-entry object table — each 32-byte entry gets field0=0, y=1.0,
  field12=0 written; other 5 dwords stay BSS-zero) and FUN_00452569
  (100 randomized particles, 6 rand calls + alive=1 per particle =
  600 LCG steps total). Finally constructs the boot view+projection
  matrices: lookat with degenerate eye=target=(0,0,0) (`DAT_06a47110`
  is BSS-zero at this point in WinMain — see the engine quirk note
  below) and perspective with fov=π/4, aspect=4/3, near=10, far=2000.
- **`src/main.c` — wiring.** Pre-window block now reads:

  ```c
  timeGetDevCaps + timeBeginPeriod(min);
  prewindow_init();      // step 2 — particle table from seed=1
  rng_seed_from_now();   // step 3 — reseed for game-tick randomness
  timeBeginPeriod(10);
  // step 4: recet.ini path build (already done)
  // step 5: FUN_0047aa30 — empty stub (intentionally omitted)
  // step 6: log no-op
  recet_ini_load(...);   // step 7
  create_main_window();  // step 8
  ```

**Behavioral validation:**

- 298 unit tests pass under ASan/UBSan (was 271). New tests:
  - 9× rng — sequence vs MSVC, time-seed determinism, year-range
    rejects ([0x46, 0x8a] = 1970..2038), leap-year bumps (Feb→Mar +86400s).
  - 9× math3d — lookat translation correctness, perspective field map,
    matmul with output aliasing.
  - 9× prewindow — named globals, object table first/last + zeros at
    untouched fields, particle alive flags, value-range checks
    (pos.x/y ∈ (-5,5), pos.z ∈ (-17.5,-12.5), rot ∈ ±π/20), particle 0
    bit-exact against hand-computed seed-1 reference, post-init RNG
    state matches 600 manual LCG steps, proj-matrix field values, view
    contains NaN/inf (degenerate-input documentation).
- Boot smoke: exit=0, all 17 tables load, recet.ini loads, window
  1024×768. No visible regression on the magenta-clear+sprite tick.

**Engine quirks documented (and faithfully reproduced):**

- **Particle randomization runs before the time-based reseed.** WinMain
  step 2 (FUN_00451790) consumes the RNG with its `.data` initial value
  of 1 *before* step 3 (FUN_005045eb) replaces the seed. So the 100
  particles end up identical every boot — same sub-pixel jitter on
  whatever effect ends up consuming them. Almost certainly deliberate:
  developers wanted the deterministic boot scene without wiring a
  separate RNG.
- **Lookat eye position `&DAT_06a47110` is in the BSS-uninitialised
  region of .data.** Raw size in the unpacked binary (0xdbe00) doesn't
  cover that VA — so the vector reads as (0, 0, 0) when FUN_00451790
  runs. Combined with target=(0,0,0) that makes the lookat
  mathematically degenerate (zaxis tries to normalise (0,0,0) →
  divide-by-zero → NaN/inf). Engine produces a garbage view matrix at
  this point and never reads it — a later in-game camera setup
  overwrites it before any vertex transform consumes it. Port
  reproduces the call as-is; one prewindow test pins the
  NaN-or-inf-somewhere expectation so a future "let's clean up the
  garbage matrix" refactor would have to deliberately stomp on it.
- **FUN_0047aa30 is a 1-byte empty stub.** Vestigial leftover from a
  removed log call between init phases (FUN_0047aa31 is similarly
  empty — the one documented as the release-build logger). Port
  intentionally omits the call.

**Deliberate divergences:**

- The engine's `FUN_005045eb` caches the last-checked UTC year/month/
  day/hour/minute and skips `GetTimeZoneInformation` when unchanged
  — an optimisation that mattered when GTZI was slow. Port doesn't
  cache: it's called once per boot.
- The engine's matmul dispatcher (`FUN_004cdd9f`) picks between x87,
  MMX, and SSE backends at startup. Port uses a single portable C
  implementation; bit-exact match with the engine's per-CPU path
  isn't pursued (the engine itself drifts across CPUs).
- `mat4_mul` adds an internal temporary so `mat4_mul(view, view, proj)`
  works. D3DXMatrixMultiply in the official D3DX runtime does the same;
  the engine's per-CPU paths may or may not. Safer to do it
  unconditionally.

**Not in this commit (deferred):**

- Consumers of the camera globals (`DAT_0438cd64..6c`) and the
  particle table. We've found the initialiser but no reader of the
  particle pos/rot/alive arrays yet — they likely feed an as-yet-
  unported render path (title screen effect? loading-screen flair?).
  When that reader lands, it will reuse `g_prewindow.particle_*`
  directly and rename the field accessors at that point.
- Consumers of the 8544-entry object table at `DAT_00605214`. Same
  story — initialiser-only port; `struct prewindow_object` has named
  fields for the three writes but the other 5 dwords stay as `pad08`
  / `pad16_28` until we find a real consumer.

**Files:**

- new `src/rng.{c,h}`, `src/math3d.{c,h}`, `src/prewindow.{c,h}`
- new `tests/test_rng.c`, `tests/test_math3d.c`, `tests/test_prewindow.c`
- updated `src/main.c` (call order before create_main_window),
  `tests/Makefile`, `tests/test_main.c`
- updated `docs/findings/winmain-and-bootstrap.md` (steps 2/3/5 closed)

## 2026-05-21 — `recet.ini` reader ported (FUN_0047a474, pre-window init)

**Subsystems landed:**
- `src/recet_ini.{c,h}` — pure-C parser for FUN_0047a474
  (`docs/decompiled/by-address/47a474.c`). Handles 33 keys: a
  2×9 pad grid + 2×5 skill grid under `[option]` (formatted-key
  match on `padNM`/`skillNM`), 22 `[setup]` scalars (`winmode`,
  `screen`, `fps`, `windowpos`, etc.), 1 `[debug]` key (`camfree`),
  and 2 `[config]` keys (`se`/`mu`, clamped to `[0,9]`).
  Pre-baked defaults match the byte tables at `0x005c81d8` (pad)
  and `0x005c8204` (skill) in the unpacked binary, with the engine's
  `+1` adjustment baked in.
- Win32 entrypoints `recet_ini_default_path()` (mirrors the engine's
  `_splitpath(argv[0]) + wsprintfA "%s%s/recet.ini"` dance via
  `GetModuleFileNameA` + tail-strip) and `recet_ini_load()` (fopen+
  fread+parse). Parser stays pure-C so ASan tests run on Linux.
- `src/main.c` — `recet.ini` now loaded in `WinMain` **before**
  `create_main_window` (matching engine step 7 in `winmain-and-
  bootstrap.md`). `g_windowed` and the window's initial RECT now
  come from `g_ini.winmode` / `g_ini.width` / `g_ini.height`;
  same with the D3D `BackBufferWidth`/`Height`. Boot trace logs
  `recet.ini: winmode=1 screen=2 (1024x768) se=9 mu=9` against the
  vendor file.
- `tests/test_recet_ini.c` — 14 unit tests covering: empty-input
  defaults, default pad/skill tables byte-for-byte, all four
  `screen`→(w,h) branches incl. fallthrough, every `[setup]`
  scalar in engine order, `[option]` grid override, case-insensitive
  section+key match, `;`/`#` comments + blank lines, whitespace
  around `=`, **bgnodisp auto-derives from easydisp (quirk #37)**,
  se/mu clamp [0,9] (over + under), unknown keys/sections ignored,
  no-trailing-newline parse, vendor recet.ini round-trip.

**Behavioral validation:**
- 271 unit tests pass under ASan/UBSan (was 257).
- Boot smoke (`./tools/smoke-test.py --target openrecet --scenario boot
  --duration 3 --capture`): `exit=0, 3 frames`. Window now opens at
  1024×768 instead of the hardcoded 800×600 — matches what the
  original Recettear opens at on this user's machine.
- Path resolution: CWD-first (matches our dev convention of
  `cd vendor/original` before invoking the exe), falls back to
  next-to-exe via `GetModuleFileNameA` for the eventual deployment
  shape where `openrecet.exe` lives alongside the data files.

**Engine quirks documented (and faithfully reproduced):**
- **`bgnodisp` is dead text — overwritten from `easydisp` (#37).**
  Vendor `recet.ini` carries `bgnodisp=0` under `[setup]` but the
  loader doesn't read it; instead, after the main read loop,
  `DAT_0438b18c = DAT_0438b19c` unconditionally aliases the field to
  `easydisp`. Any explicit value in the ini is dropped.
- **`[debug] camfree` is read twice with the same section+key (#38).**
  Two adjacent `GetPrivateProfileIntA` calls write to the same
  global; second value sticks but both calls hit the same ini
  entry. Dead duplicate code from a refactor. Port reads once.
- **Three more keys never read anywhere in the binary (#39).**
  `pfnouse`/`fontmode1`/`fontmode2` ship in vendor `recet.ini` but
  no `GetPrivateProfile*` call touches them. Likely vestigial from
  earlier engine revisions. Port silently ignores (matches Win32
  semantics for missing keys).

**Deliberate divergences:**
- Path resolution adds a CWD-relative `recet.ini` lookup before the
  engine's next-to-exe path build. Required for our dev workflow
  (exe in `build/`, data in `vendor/original/`); behaviour identical
  for a deployment where the exe sits alongside its data.
- `recet_ini_parse` uses an in-process INI tokenizer instead of
  per-key `GetPrivateProfileIntA` calls — same semantics for every
  key in the engine's read set (case-insensitive lookups, `atoi`
  parsing, defaults on missing key). The only edge case we don't
  match is Win32's `0x` / `0` → hex/octal prefix handling; not used
  anywhere in vendor `recet.ini`.

**Not in this commit (deferred):**
- **`FUN_0047a804` shutdown save-back** (`[config] se`/`mu` always,
  `[setup] winx`/`winy` when `windowpos != 0`). Belongs to the
  shutdown chain — lands when that whole chain is ported.
- **Consumption of `pad[]`/`skill[]`** by the input subsystem. The
  values are loaded into `g_ini` but `src/input.c` currently only
  initialises DInput devices; wiring lands with the input-poll port.
- **`FUN_00451790`** (early camera/particle math init, step 2 of
  WinMain). Sized small in decomp (36L) but pulls in
  lookat/perspective/matmul/normalize/RNG helpers — deferred to a
  later milestone where those helpers earn their keep with real
  rendering.

**Files:**
- new `src/recet_ini.{c,h}`, `tests/test_recet_ini.c`
- updated `src/main.c` (load + wire into window/D3D init order),
  `src/Makefile` (picks up `*.c` automatically — no edit needed),
  `tests/Makefile`, `tests/test_main.c`
- new `docs/formats/recet-ini.md`
- new entries (#37, #38, #39) in `docs/findings/engine-quirks.md`
- updated `docs/findings/winmain-and-bootstrap.md` step 7

## 2026-05-21 — Phase B [+1]: `idx/stage.idx` parser

**Subsystems landed:**
- `src/tables_stage.{c,h}` — pure-C parser for FUN_00475270
  block #1 (`docs/decompiled/by-address/475270.c` L55..L329 +
  L3174..L3957 — the largest table parser in the loader by far,
  ~1000 lines of decomp). Defines the 21-record stage table
  (`stage:0-1`..`0-5` + `stage:1-1`..`1-16`) at base `&DAT_068dd2f8`,
  stride 0x1b3c = 6972 bytes. `_Static_assert` guards on 24 critical
  field offsets + total record size.
- `src/tables.c` — replaced the stage.idx stub with the real
  loader. No new resolver wiring (stage.idx is self-contained —
  no cross-table refs). Boot trace logs `(stages=S maps=M
  mapcameras=MC sunpos=S1 sunset=S2 moonpos=MP)`.
- `tests/test_tables_stage.c` — 28 unit tests covering: byte-offset
  layout, empty input, comments/blanks, lines-before-header
  dropped, all 21 stage-ID dispatch entries (both 3-byte and
  4-byte forms), unknown-ID fallback (quirk #34), every shape
  class (int / int→float / float / flag / string / slot string /
  int×3 / float×3 / float×2-colon / float×2-space), sunpos numeric,
  sunpos:off short-circuit, sunset numeric, **sunset:off broken
  (quirk #36)**, **moonpos shared coords (quirk #35)**, multi-record
  threading, no-trailing-newline EOF, map[] slot overflow safety,
  mapcamera[] threading, and a vendor-shape miniature integration
  smoke.

**Field key inventory:** 57 fully-dispatched keys covering map
geometry, camera, lighting (directional + ambient + maplight pairs),
water surfaces, weather flags, fog/colour ramp, and misc. ints. All
documented with their byte offset, type, default value, and source
key in `docs/formats/data-text.md`.

**Behavioral validation:**
- 257 unit tests pass under ASan/UBSan (was 229).
- Boot smoke: `idx/stage.idx — 22434 bytes (stages=20 maps=219
  mapcameras=0 sunpos=15 sunset=0 moonpos=0)`. Cross-checked:
  vendor file has exactly 20 `stage:` headers, 219 uncommented
  `map:` lines (`/map:...` comment lines correctly skipped), 15
  `sunpos:N:N:N` numeric lines, 5 `sunpos:off` short-circuits
  (mode=0, not counted in the sunpos= tally), 0 `sunset:` or
  `moonpos:` lines.

**Engine quirks documented (and faithfully reproduced):**
- **Unknown stage IDs alias to `1-16` (#34).** The chain-default
  `uVar5 = 0x14` collides with the last entry's index, so a
  typo'd `stage:foo` opens a record indistinguishable from a
  real `stage:1-16` on read-back. Dormant in vendor.
- **`moonpos:` shares X/Y/Z storage with `sunpos:`/`sunset:` but
  not the mode flag (#35).** Only `sunpos:`/`sunset:` touch
  `sunpos_mode`; `moonpos:` sets a separate `moonpos_set` flag and
  overwrites the sun coords. A record with both sunpos and moonpos
  ends up with sunpos's mode and moonpos's coords. Dormant in vendor.
- **`sunset:off` is broken (#36).** The "off" short-circuit
  compares against the literal string `"sunpos:off"` (the binary
  has two interned copies of `"sunpos:off"` at 0x005cab4c and
  0x005cab80 — but no `"sunset:off"` anywhere), so a real
  `sunset:off` line falls through to the numeric path. Dormant
  in vendor.

**Safety divergences (documented, not present in engine):**
- `map:` slot cap (engine bumps count unconditionally; port stops
  writing past slot 19 to avoid clobbering the minimap field).
- `mapcamera:` slot cap (engine bumps count unconditionally; port
  stops writing past slot 1 to avoid clobbering mapcamera_count).
- Post-loop unrelated globals (13 writes to `_DAT_0438cc6c..`) are
  player-inventory defaults, not stage state — deferred to the
  gameplay-state init port.

**Files:**
- new `src/tables_stage.{c,h}`, `tests/test_tables_stage.c`
- updated `src/tables.c`, `tests/Makefile`, `tests/test_main.c`
- new docs section in `docs/formats/data-text.md`
- new entries (#34, #35, #36) in `docs/findings/engine-quirks.md`

**Phase B fully complete.** All 15 of the originally-tracked Phase B
files (14 named + the tuto loop counted as 1) had parsers landed
in the 2026-05-20 sweep; this commit closes out the remaining
`stage.idx` stub — file 0 of the engine's load order, deferred at
the time because of its size. The full loader chain is now end-
to-end real: no stubs remain in `tables.c`.

## 2026-05-20 — Phase B [15/15]: `data/enemylist.txt` parser

**Subsystems landed:**
- `src/tables_enemylist.{c,h}` — pure-C parser for FUN_00475270
  block #14 (`docs/decompiled/by-address/475270.c` L2581..L2899).
  Two engine globals populated: a 10×60 grid of 752-byte
  `enemylist_section_t` at `&DAT_0053f8e8` (451200 bytes), and a
  10-dword wisp drop table at `&DAT_073d8630`. Each section carries
  `floor_lo`/`floor_hi` + 31 enemy slots (`{enemy_id, variant,
  count}`) + 31 drop slots (`int32_t item_id[3]`). `_Static_assert`
  guards on all four major byte offsets + the total 0x2f0 stride.
- `src/tables.c` — replaced the enemylist.txt stub with the real
  loader. Reuses the existing `resolve_via_item_state` adapter
  (already wired for enemy.txt and gousei.txt drop resolution).
  Boot trace logs `(sections=S enemies=E drops=D resolved=R
  wisps=W wisp_resolved=WR)`.
- `docs/formats/data-text.md` — appended full enemylist.txt
  section: 5-way line dispatch, sticky state semantics
  (dungeon-slot / section index / enemy slot), section byte-layout
  table, longest-prefix enemy-name lookup vs the pre-baked
  `g_enemy[]`, item-name → id resolution via `tables_item_resolve`,
  faithfully-reproduced quirks (#21 reused, plus new #31/32/33),
  vendor file shape.
- `docs/findings/engine-quirks.md` — added quirks #31 (10 dungeon
  slots reserved, only 6 keyed), #32 (`wisp10:` lands on the `:`
  byte and silent-drops), and #33 (slot-30 terminator hazard
  clobbers slot-0 drop ID — dormant in vendor).
- `tests/test_tables_enemylist.c` — 22 cases: byte-offset
  layout sanity, empty input, comments/blanks, wisp basic /
  empty / wisp10 silent-drop / unknown-item, dungeon-header
  resets section index, `f:N` single-floor, `f:` empty +
  loop-err-16 path, multiple `f:` lines thread sections, enemy
  basic (one drop), multi-drops (up to 3), variant `(N)` suffix,
  count `xN` suffix, longest-prefix wins, unknown enemy name
  skipped, per-line drop reset, NULL resolver yields -1, no-
  trailing-newline EOF, enemies thread across consecutive `f:`
  blocks, end-to-end vendor-shape integration smoke.

**Behavioral validation:**
- 229 unit tests pass under ASan/UBSan (was 207).
- Boot smoke: `data/enemylist.txt — 28281 bytes (sections=100
  enemies=696 drops=1118 resolved=1118 wisps=4 wisp_resolved=4)`.
  100 floor-sections matches the 100 `f:` lines in the vendor
  file. 4 populated wisps = vendor's `wisp3..wisp6` (the parser
  honours `wisp1:`/`wisp2:` ship-empty by leaving slots 0/1 at -1).
  All 1118 drop references resolved to real item ids via the
  shared `g_item` table.

**Engine quirks documented (and faithfully reproduced):**
- **10 dungeon slots, only 6 keyed (#31).** Init scrubs all
  10×60 = 600 sections to `floor_lo = -1`, but the SJIS key
  chain at L2690..L2702 only matches `ダンジョン１..６`. Slots
  6..9 are dead storage with no possible writer.
- **`wisp10:` silent-drops (#32).** Init reserves 10 wisp dwords,
  but the name-copy loop reads from `line[6]` — which is the
  trailing `:` for `wisp10`, terminating the copy immediately.
  Slot 9 storage exists but no `wispN:` line can populate it.
  Vendor only ships `wisp1..wisp6`.
- **Slot-30 terminator hazard (#33).** Engine writes `enemy_id
  = -1` to slot `local_18 + 1` after each enemy line. If a
  section hits 30 enemies, the terminator lands at slot 31's
  enemy_id field — which is the first drop dword of slot 0.
  Vendor never gets close (max ~12 per f-block). Port logs
  overflow + skips the line rather than clobbering drops[0].
- **Per-line drop reset.** drops[slot].item_id[0..2] reset to
  -1 at line start so a line with fewer drops than the previous
  one doesn't inherit stale ids.
- **State sticky across lines.** Dungeon slot, section index,
  enemy slot all persist until the next header. An enemy line
  emitted before any `ダンジョン`/`f:` lands in dungeon 0 /
  section 0 — vendor never does this.
- **`f:N` (no dash) → `floor_hi = floor_lo`.** Dash-scan stops
  on `\r`/`\n`; the second atoi never runs.
- **`f:` (empty) → "loop err 16" + line skipped.** Engine writes
  `atoi("") - 1 = -1` to floor_lo BEFORE bailing — leaving the
  section in a half-init state. Port preserves the write.
- **Effective-exact item-name lookup.** Engine's double-`FUN_00479f4d`
  pattern (memcmp twice, once with each side's strlen) behaves
  like exact match. Port routes through `tables_item_resolve`
  which is strncmp-up-to-32.

**Phase B complete.** All 14 file parsers (counting the 3-file
tutorial loop as one) plus the resolver-wiring follow-up are
landed. Remaining `tables` work for OpenRecet's surface mapping:
`stage.idx` (still a stub at `load_stage_idx`, 22434 bytes —
likely Phase C). Phase 3 next milestone candidates to confirm
with user at session start.

## 2026-05-20 — Phase B [14/15]: `data/news.txt` parser

**Subsystems landed:**
- `src/tables_news.{c,h}` — pure-C parser for FUN_00475270 block
  #11 (`docs/decompiled/by-address/475270.c` L1583..L2236). One
  global at `&DAT_056e0e00`, stride 0xbc (188 bytes), no engine
  cap on count (port reserves 100 slots). Each record carries a
  128-byte body, 16-byte name (parser CAN write 20 → overflows
  into rate, quirk #27), `rate` / `price_lo` / `price_hi`, the
  three lookup results (`attr_mask` / `category` / `item_id`)
  with their sentinel values, the sticky `target_group` from
  `対象者:`, optional `days_lo` / `days_hi`, and the sticky
  `period_start` / `period_end` from `時期:`. `_Static_assert`
  guards on all 13 field offsets.
- `src/tables.c` — replaced the news.txt stub with the real
  loader. Two new resolver adapters `news_resolve_category` and
  `news_resolve_item` prefix-match (engine `FUN_00479f4d`-style)
  against `g_item.categories[].singular` and
  `g_item.records[].singular` respectively, both wired through
  `tables_parse_news`. Boot trace logs
  `(news=N dash=D special=S attr=A category=C item=I)`.
- `docs/formats/data-text.md` — appended full news.txt section:
  file shape, sticky-header semantics, data-row layout with
  optional days range, name-resolution lookup chain (special →
  attr → category → item), record byte-offset table, all faithfully-
  reproduced quirks, vendor file shape.
- `docs/findings/engine-quirks.md` — added quirks #27 (name buffer
  overflow into rate), #28 (prefix-by-name-length lookup, not
  exact match), #29 ("-" rows leave target_group / item_id /
  days_lo / days_hi at BSS-zero), and #30 (body retains trailing
  `\r` on CRLF lines).
- `tests/test_tables_news.c` — 20 cases: empty input, byte-offset
  layout sanity, comments/blanks, `特殊` sentinel, SJIS attr-mask
  hit (`武器` / `防具`), category resolver hit (`Daggers`), item
  resolver hit (`Candy`), lookup-chain precedence (attr wins over
  cat over item), days-range optional, `-` row with all the BSS-
  zero defaults, sticky `target_group` across rows, sticky
  `period_start` / `period_end` across rows, period defaults
  (0, 100) before any header, malformed `時期,A` (no `-`) leaves
  `period_end` unchanged, no-trailing-newline EOF, body retains
  `\r` on CRLF, body strips on LF-only, resolver miss is silent
  (logs but counts), max-records cap, end-to-end vendor-shape
  integration smoke.

**Behavioral validation:**
- 207 unit tests pass under ASan/UBSan (was 187).
- Boot smoke: `data/news.txt — 6342 bytes (news=80 dash=43
  special=2 attr=22 category=12 item=1)`. Each bucket
  cross-checked against an independent Python re-count of the
  vendor file, with the only discrepancy being `アクセサリー` —
  it matches the SJIS attr tag `アク` (0x83 0x41 0x83 0x4e, bit
  0x0010), which the Python re-count's curated attr-tag list
  initially missed. Port matches the engine.

**Engine quirks documented (and faithfully reproduced):**
- **Name buffer can overflow into rate.** Parser caps the
  name-write loop at 20 bytes, but the structural field is 16
  bytes (rate follows at +0x90). For names ≥ 16 bytes the NUL
  terminator lands in rate / price_lo / category. Dormant in
  vendor (longest name = `アクセサリー` at 12 bytes).
- **Lookup chain is prefix-by-name-length.** All three name
  lookups (special / attr / category / item) use
  `memcmp(name, candidate, name_len)`. A short news.txt name
  matches any candidate it's a prefix of. Vendor names always
  fully equal their candidate.
- **`-` rows leave BSS-zero fields.** The `-` branch skips the
  `target_group` / `item_id` / `days_lo` / `days_hi` writes,
  leaving them at memset-zero. Consumers that expect -1 for "no
  match" see 0 for "-" rows.
- **CRLF body keeps trailing `\r`.** Line-collect stores the
  terminating `\r` in the line buffer; body-copy stops at `\0` /
  `\n` but not `\r`. Vendor file is CRLF so every body has a
  trailing `\r` byte.
- **`時期,A` (no `-`) leaves `period_end` unchanged.** Engine
  "loop err 6"; port skips the second atoi via `strchr` miss.

**Note for the next milestone:**
- Phase B 15: `enemylist.txt` (28281 bytes — substantially
  larger than news.txt). Confirm with user at session start —
  enemylist.txt's parser block is much further down in the
  binary and may need its own discovery doc.

## 2026-05-20 — Phase B [13/15]: `data/event.txt` parser

**Subsystems landed:**
- `src/tables_event.{c,h}` — pure-C parser for FUN_00475270 block #10
  (`docs/decompiled/by-address/475270.c` L1521..L2235). 4 in-town
  location categories (広場/市場/教会/酒場), each with up to 100
  records (50-dword stride = 200 bytes); each record carries an
  event id, a "flag to set on trigger", 4 hex-encoded prereq slots
  (lowercase `0..9/a..f`, with sticky `-` → -1), first/max weekday-
  of-day index (NOT a bitmask like kyaku.txt — single 0..3 indices
  for 朝/昼/夕/夜), 20 day-range pairs, a `loop_min` gate, and a
  `decay_or_max` field that pre-bakes to 100000 for the seed record
  and 0 for all parsed records. `_Static_assert` guards on every
  field offset so the layout stays byte-identical to the consumer
  `FUN_0045de68`'s negative-offset reads.
- `src/tables.c` — replaced the event.txt stub with the real loader.
  Boot trace logs
  `(hiroba=H ichiba=I kyokai=K sakaba=S with_prereqs=N)`. No
  resolver wiring needed — `event.txt` has no cross-table lookups.
- `docs/formats/data-text.md` — appended full event.txt section:
  category-header table, record layout with field offsets, data-line
  shape annotated, prereq encoding (hex + sticky -), weekday-of-day
  tag table with the 1-byte-mismatch quirk, day-pair format,
  pre-baked default record, all faithfully-reproduced quirks,
  vendor file shape.
- `docs/findings/engine-quirks.md` — added quirk #26 ("`event.txt`'s
  weekday-tag mismatch advances 1 byte, not 2") with the engine
  decomp snippet and dormant-but-real explanation.
- `tests/test_tables_event.c` — 15 cases: empty-seeds-default,
  byte-offset layout sanity, comments/blanks, basic 広場 record,
  prereq hex+minus, time_first/max tracking, time_max clamps to
  no-higher, unknown-only tokens leave 0/0, loop_min atoi, 20-pair
  cap, all 4 categories dispatched, pre-header data-line goes to
  広場, decay_or_max=100000 only for seed, no-trailing-newline,
  vendor-shape integration smoke.

**Behavioral validation:**
- 187 unit tests pass under ASan/UBSan (was 172).
- Boot smoke: `data/event.txt — 8901 bytes (hiroba=39 ichiba=9
  kyokai=9 sakaba=19 with_prereqs=76)`. The four counts match an
  independent Python re-count of the vendor file's headered
  sections + data lines, including the pre-baked seed contributing
  1 to 広場. Every record has `prereq[0] >= 0` (vendor convention:
  the "must NOT be set" flag is always populated).

**Engine quirks documented (and faithfully reproduced):**
- **Pre-baked record 0 of category 0.** Before parsing, the engine
  hand-writes a "default 広場 event" with id=0x0b, prereq[0]=0xa3,
  time 0..1, day range (0,40), loop_min=0, decay_or_max=100000.
  Sets `counts[0] = 1`, so the first parsed 広場 line lands at
  slot 1.
- **Lines before any header dispatch to 広場.** Init leaves
  `local_18 = 0` (= 広場). Vendor data has 広場 as the first
  header so this is dormant.
- **Hex-only prereq with sticky `-`.** `:`-delimited fields accept
  lowercase `0..9/a..f` only; any byte that's not hex/`-`/`:` is
  silently skipped (e.g. leading spaces). A `-` anywhere in a
  field's tail nukes that field to -1 regardless of any hex value
  accumulated before it. So `100` = 0x100 = 256, `-1`/`-2`/`f-f`
  all = -1.
- **Weekday-tag mismatch advances 1 byte, not 2.** New quirk #26.
  Unknown 2-byte SJIS chars get scanned twice (once at byte 0,
  once at byte 1). Dormant in vendor data thanks to full-width-
  space padding `81 40`.
- **`time_first == 0` is overloaded.** "No matched token" and
  "first matched token was 朝" both result in `time_first = 0`.
  Consumer interprets as "morning-only" in both cases.
- **End-of-list sentinel.** Loader writes `id = -1` to the slot
  one past `counts[cat]` for each category — the consumer's loop
  terminator.

**Note for the next milestone:**
- Phase B 14: `news.txt` (6342 bytes, ~330 C lines in 475270.c
  block #11) — pre-categorised by `対象者:`/`時期:` headers, then
  `品名,カテゴリ,価格-高値,日数-日数` data lines (5 fields with
  comma + dash separators). Larger than event.txt, but still
  smaller than the average tables file.

## 2026-05-20 — Phase B [12/15]: `data/kyaku.txt` parser

**Subsystems landed:**
- `src/tables_kyaku.{c,h}` — pure-C parser for FUN_00475270 block #4
  (`docs/decompiled/by-address/475270.c` L469..L832). 18 active
  records out of 50 in vendor data; each record carries a
  singular/plural name, name-table index, 2-axis attribute pair,
  up-to-20 preferred item categories, preferred-attribute bitmask
  (uses the same 16-tag SJIS table as oder/item), budget range,
  activity-time mask (朝/昼/夕/夜 → bits 1/2/4/8), 6 haggle-tuning
  ints, and a per-customer dialog-file path.
- `src/tables.c` — replaced the kyaku.txt stub with the real loader.
  New `resolve_via_item_category` adapter resolves `好き種類:` lines
  against `g_item.categories[].singular` (populated earlier by
  item.txt's category headers). Boot trace logs
  `(customers=N like_kinds=K with_budget=B)`.
- `docs/formats/data-text.md` — appended full kyaku.txt section: per-
  line dispatcher table, header singular/joint quirk details, the
  activity-time and preferred-attribute token tables, all 8
  faithfully-reproduced quirks, vendor file shape.
- `tests/test_tables_kyaku.c` — 23 cases covering: empty input,
  comments / blanks, header singular-only + with-plural, attr X/Y
  (full + empty), budget range (full + empty), like-kind resolver
  hit / null-resolver-skip / 20-cap, like-attr SJIS mask, the
  `嫌い:` orphan-noop, file_path copy, activity-time (all 4 tokens,
  partial, unknown token), atoi scalars, lines-before-header
  dropped, no-trailing-newline, multi-customer threading,
  resolves-via-item-category end-to-end with a hand-populated
  `item_state_t`, and a vendor-shape integration smoke.

**Behavioral validation:**
- 172 unit tests pass under ASan/UBSan (was 149).
- Boot smoke: `data/kyaku.txt — 7603 bytes (customers=18
  like_kinds=111 with_budget=15)` matches the vendor file (manual
  count of `好き種類:` lines totals 111; only Recette / Tear /
  Euria have empty `予算:` → 18 - 3 = 15 with-budget).

**Engine quirks documented (and faithfully reproduced):**
- **`嫌い:` is an orphan match.** The 5-byte `嫌い:` key match has
  an empty body — match-but-discard. Almost certainly a dialled-back
  feature; vendor data still ships dozens of `嫌い:` lines but nothing
  consumes them.
- **Header singular/joint write-position reset.** On `NNN:S#P` the
  joint cursor resets to offset 0 at the `#`, so the plural
  *overwrites* joint[0..] starting from the beginning. If plural is
  shorter than singular, the tail of singular leaks into joint —
  but vendor data never triggers (all plurals ≥ singular length).
- **Singular NUL at off-by-five.** Engine's `puVar14[iVar17 + 5] = 0`
  writes NUL at `singular[iVar17 + 1]`, NOT `singular[iVar6 + 1]`.
  For `#`-containing headers it lands several bytes past singular's
  end. Harmless thanks to BSS zero-init.
- **Header gated by leading `0`.** Dispatcher only tries the 50-iter
  `%03d:` match if `line[0] == '0'`. Records 0..49 always start
  with `0` so this is a perf optimisation in practice; record IDs
  ≥ 100 would be silently ignored.
- **`属性:` / `予算:` unbounded delimiter scans.** Once the first
  numeric is parsed, the engine walks forward looking for `,` /
  `-` with NO upper bound. Vendor data always has the delimiter;
  the port also stops at NUL.
- **`好き種類:` cap of 20** + MessageBoxA on overflow / unknown
  category. Port logs to stderr.
- **Lines before any header are silently dropped** (engine's
  `local_14 < 0` sprintf-to-discarded-local branch).

**Resolver wiring:**
- `好き種類:` resolves through `resolve_via_item_category` (new in
  `src/tables.c`) — different from the existing `resolve_via_item_state`
  (which probes `g_item.records[].singular` for full item names).
  Kyaku resolves against the **category-name** table at
  `g_item.categories[].singular`, populated by item.txt's
  `:Category#(tag)` headers. The 111 vendor `好き種類:` lines all
  resolve successfully against the populated category table.

**Note for the next milestone:**
- Phase B 13: `event.txt` (8901 bytes, ~62 C lines in 475270.c
  block #10) — likely the next-easiest remaining file. Or `news.txt`
  (6342 bytes, 655 C lines — larger but more boxed-in to a single
  format). Confirm priority with the user.

## 2026-05-20 — Phase B [11/15]: resolver-wiring follow-up

**Subsystems touched:**
- `src/tables_enemy.{c,h}` — `tables_parse_enemy` gains an
  `enemy_resolve_fn (resolve, user)` pair, replacing the dead-stub
  `lookup_item_id` that always returned -1. NULL resolver collapses
  to the previous behaviour (tests use this).
- `src/tables_gousei.*` — already accepted the resolver; no change.
- `src/tables.c` — new `resolve_via_item_state` adapter wires
  `tables_item_resolve(&g_item, name)` into both `load_enemy_txt`
  and `load_gousei_txt`. Boot trace now reports resolution counters
  (`drops_resolved`, `outputs_resolved`, `ingredients_resolved`).
- `tests/test_main.c` — registry is X-macro-driven now (separate
  cleanup commit). 149 tests pass (was 147): two new tests cover
  the resolver wiring end-to-end (`tables_enemy_drop_resolves_via_callback`
  via stub; `tables_gousei_resolves_via_item_state` via a real
  hand-populated `item_state_t`).

**Observed boot deltas** (vendor data):
- `enemy.txt — drops_resolved=70` (was 0 — 54 enemies × ≤2 drops).
- `gousei.txt — outputs_resolved=101 ingredients_resolved=268`
  (was 0 — every recipe output name has a matching item.txt singular,
  so 100% of outputs resolve).

**Out of scope (still deferred):**
- `oder.txt` attribute-table fallback — its name table at
  `&DAT_0963e5f8` is populated by item.txt's category-header path,
  so the lookup already works; no rewire needed.
- Drop-name → item-id misses for the ~38 enemy slots that still
  resolve to -1. These are vendor-data spelling mismatches and need
  per-name investigation; out of scope for the wiring pass.

## 2026-05-20 — Phase B [10/15]: `data/item.txt` parser

**Subsystems landed:**
- `src/tables_item.{c,h}` — pure-C parser for FUN_00475270 block #3
  (`docs/decompiled/by-address/475270.c` L428..L468 main dispatch +
  L815..L829 cross-block record fallback reached via
  `goto LAB_00476d04`). The two sub-parsers are FUN_00491044
  (category header, 81 bytes) and FUN_004912de (item record, 820
  bytes). 716-byte record layout (stride 0x2cc) populated end-to-end:
  rank, price, atk, def, mt, mf, attr_mask (incl. category-class OR
  via FUN_0049eb2a), equip_class (FUN_0049ed75), stock_info[9]
  (FUN_00491095 — 7 SJIS tags incl. `ダ`'s ×10-if-<10 quirk),
  aud_mask (FUN_0049e849 — 11 SJIS audience tags including `男`/`女`
  composites), singular[64], plural[64], desc_line1[256],
  desc_line2[256]. Static asserts validate every offset.
- `src/tables.c` — replaced the item.txt stub with the real loader.
  Boot trace logs `(items=N max_id=M equippable=K cats=C)`.
- `docs/findings/item-table.md` — captures the chained-dispatcher
  discovery (the cross-block `goto LAB_00476d04` is real, not a
  decompiler artifact), the scratch-buffer flow (FUN_00491044 writes
  scratch consumed by FUN_004912de's sprintf copies), the per-record
  byte layout, and the resolver implications for the three already-
  ported parsers that defer item-name lookup (oder, enemy, gousei).
- `docs/formats/data-text.md` — appended a full item.txt section:
  per-line dispatcher table, 12-field record format, attribute /
  stock / audience tag tables, the `##`-makes-desc1-the-real-content
  semantics, vendor file shape.
- `tests/test_tables_item.c` — 23 cases covering: empty input,
  comment/blank/indent-space skipping, basic record (with and without
  `+` plural), full stat fields, category header routing,
  multi-category index threading, attribute-mask + category-class
  OR, audience tags (全 → 0xff, 男 → 0x55, リ → 0x01,
  empty-field → 0xff), stock tags (在庫(N) basic + ダ(N) ×10 quirk),
  out-of-range item_id dropped, no-trailing-newline, description-
  line1+line2 split on embedded `#`, phase-2 `/` truncation,
  unknown-line stderr fallback, resolver lookup, slot cap, and a
  vendor-shape integration test against
  `/tmp/openrecet-extract/data/item.txt`.
- `tools/analyze/pe.py` — added the `bytes` subcommand earlier;
  reused here to identify the dispatcher sentinels `':'` at
  `0x5cacf0` and `' '` at `0x5cacf4`, plus the stock-info /
  audience SJIS tag tables.

**Behavioral validation:**
- 147 unit tests pass under ASan/UBSan (was 124).
- Boot smoke: `data/item.txt — 121998 bytes (items=571 max_id=5408
  equippable=331 cats=33)` matches the vendor file's actual counts
  (Python analysis: 571 records, 33 categories, IDs 0..5408).
- ASan caught one early-iteration bug: `item_class_bits` had a
  hand-written length table (`{ "Arm Parts", 12, ... }`) that
  memcmp'd past the C string literal's bounds. Fixed by switching to
  `strlen(.name)` + exact-NUL terminator check. Test for this is
  implicit in the vendor-shape run, which would crash under ASan if
  the OOB read reappeared.

**Engine quirks documented:**
- **Cross-block dispatcher goto.** The non-`:` line path inside
  item.txt's loop is reached via `goto LAB_00476d04` that physically
  lands inside the next block's (kyaku.txt) function body. Real code
  layout, not a decompiler artifact — the port linearises it.
- **Most-recent-header semantics.** Category headers don't index
  into the per-category table directly; the next item record copies
  the scratch buffer into `categories[item_id/100]`. Vendor files
  respect the convention; an adversarial reorder would scramble the
  category-name lookup.
- **Phase-1-immediate-`#` empties desc_line1.** Vendor `##` between
  AUD and DESC means AUD is empty (engine ORs `aud_mask |= 0xff`)
  and DESC1 starts AT the byte after the second `#`. desc_line1
  ends up with the first half of the description; desc_line2 gets
  the second half after the `#` between them.
- **Description phase 2 ends on `/`.** A literal `/` in the second
  description line truncates the field. Phase 1 has no such check.

**Note for the next milestone:**
- Resolver wiring (Phase B 11 follow-up) is now unblocked. A single
  pass through `src/tables.c` can wire `tables_item_resolve` into
  the deferred hooks of `tables_parse_enemy` (drop refs, currently -1)
  and `tables_parse_gousei` (ingredient/output IDs, currently -1).
  `oder.txt`'s attribute-table lookup doesn't actually need
  resolution — its `attr_index = -1` placeholder was a misread; the
  oder parser already references the singular-name table via
  `oder_attr_hash`, and the table will be populated automatically
  now that item.txt has been parsed. Cleanup is mostly removing the
  TODO comments from `src/tables.c`.

---

## 2026-05-20 — Phase B [9/15]: `data/gousei.txt` parser

**Subsystems landed:**
- `src/tables_gousei.{c,h}` — pure-C parser for FUN_00475270 block
  #13 (LAB_004790cd / `docs/decompiled/by-address/475270.c`
  L2402..L2579). 12-dword (0x30-byte) record layout: output_id, rank,
  ingredient_id[5], ingredient_count[5]. Header-vs-recipe dispatch
  on the 7-byte SJIS `ランク:` prefix. Recipe lines skip the 5-byte
  `NNNN:` prefix wholesale (engine: `pcVar16 = local_27c + 0x25`),
  then walk colon-separated fields with `#N` count modifiers.
- `src/tables.c` — replaced the gousei stub with a real loader.
  Threads a NULL item-name resolver for now (item.txt parser hasn't
  landed); when it does, tables.c will pass a real callback into
  `tables_parse_gousei` without touching the parser. Boot trace
  logs `(recipes=N max_rank=M)`.
- `docs/formats/data-text.md` — appended a full gousei section:
  per-record layout, header dispatch, the discarded 4-digit prefix,
  the ing1-write quirk, the exact-name lookup, the index-0
  MessageBox quirk, the 200-record cap, vendor file shape.
- `docs/findings/engine-quirks.md` — added quirk #23: the
  `Master's Plate` recipe line ships without a trailing `:`, which
  trips the engine's unbounded `:` hunt past the line terminator and
  into surrounding memory. Record still commits; port detects EOL
  in the hunt and finalises the column cleanly.
- `tests/test_tables_gousei.c` — 15 cases covering empty input,
  comment/blank skipping, basic recipes, rank header dispatch,
  rank-0 recipes preceding any header, prefix-discarded behaviour,
  3- and 5-ingredient widths, NULL-resolver fall-through, unknown-
  name → -1, the EOL-without-trailing-':' recovery, no-trailing-
  newline, the 200-record cap, embedded-NUL early-exit, and a
  vendor-shape integration test.

**Behavioral validation:**
- 124 unit tests pass under ASan/UBSan (was 109).
- Boot smoke: `data/gousei.txt — 6252 bytes (recipes=101 max_rank=5)`
  matches the vendor file's actual recipe count (22+22+17+19+21 = 101
  across ranks 1..5). Pre-fix, my parser was reporting 100 — the
  missing recipe was the Master's-Plate-without-trailing-':' line,
  which my -1-return path was silently dropping; chased it down via
  per-line debug instrumentation, then replaced the bail with an
  EOL-aware fall-through.

**Note for the next milestone:**
- Item resolver hook is now the gating dependency for *several* of
  the already-ported parsers (oder.txt attribute lookup, enemy.txt
  drop refs, gousei.txt output/ingredient IDs). Once item.txt's
  parser is in, a single resolver callback wired into each loader
  will populate the long-deferred ID fields without re-touching the
  parsers.

---

## 2026-05-20 — Phase B [8/15]: `data/tuto[123].txt` parser

**Subsystems landed:**
- `src/tables_tuto.{c,h}` — pure-C parser for FUN_00475270 block #15
  (L2898..L3123). The three tutorial scripts (`tuto1.txt` /
  `tuto2.txt` / `tuto3.txt`) share a single 296-byte-per-record
  array (`g_tuto[600]`). Per-line CSV with a 16-token opcode
  dispatch (ASCII tokens `CHR0`/`CHR1`/`TAGD`/`PRID`/`PRIA`/`BUN0`/
  `GOTO`/`TAGN`/`TOUT` and SJIS keywords `値段`/`高く`/`値引`/`値上`/
  `アイテム`/`剣選択`/`初期金額決定`). Two payload families: 1 int +
  text for CHR0/CHR1, 7 ints for the price/branch opcodes, none for
  the rest. Handles the `id == -1` sentinel and `id <= -2`
  text-only branches faithfully.
- `src/tables.c` — replaced the tuto stub-loop with a real loader.
  Mirrors the engine's hard-coded 3-file iteration (no early-exit
  on miss) and logs `(records=N)` with a ⚠ when N exceeds the
  50-slot parser cap (which it does on all three vendor files).
- `docs/formats/data-text.md` — appended a full tuto section:
  opcode table, record layout, the parser-vs-consumer stride
  mismatch, vendor-data overflow numbers, and the final
  cross-overwritten array state.
- `docs/findings/engine-quirks.md` — added quirk #22 (parser stride
  50 vs consumer stride 200, both pointing at `&DAT_005d1fc8`;
  three of four `FUN_00461bf6` callers push `2` so the consumer
  reads a never-written region).
- `tests/test_tables_tuto.c` — 18 cases covering empty input,
  blank/comment skipping, every ASCII opcode, every SJIS opcode,
  the `id < 0` branches, the 7-int reader with short-arg fallback,
  the file_index×50 stride, the 50-slot overflow, and a vendor-
  shape integration test.

**New persistent tooling:** `tools/analyze/pe.py` — PE32 helper
module + CLI for the unpacked vendor exe, used for VA → file offset
mapping, NUL-terminated cp932 string dumps, raw byte / blob
extraction, and call-site discovery with PUSH-imm decoding.
Replaces the ad-hoc inline Python scripts that kept getting
reinvented for each RE session. `docs/AGENT-WORKFLOW.md` got a new
"Persistent analysis tooling" section pointing at it.

**Engine fidelity divergences (documented):** the 7-int reader on
short lines reads stack garbage in the engine; our port zeros the
line buffer between records so missing args read as 0 (benign —
gameplay code only uses `args[0]` for `GOTO`). The parser-vs-
consumer stride mismatch (quirk #22) is preserved on the parser
side; the consumer port will inherit whatever the engine actually
does at runtime.

**Boot trace** (smoke test, vendor data):
```
tables: data/tuto1.txt — 8978 bytes (records=135 ⚠ overflows 50-slot cap)
tables: data/tuto2.txt — 5828 bytes (records=90 ⚠ overflows 50-slot cap)
tables: data/tuto3.txt — 4064 bytes (records=60 ⚠ overflows 50-slot cap)
tables: tuto overflow — 3/3 files exceed the 50-slot parser cap (engine quirk: stride mismatch vs consumer)
```

**Tests:** 109 pass (was 91), 0 fail, 0 skip.

**Remaining Phase B order:** `gousei.txt → kyaku.txt → event.txt → news.txt → stage.idx → enemylist.txt → item.txt`.

## 2026-05-20 — Phase B [7/15]: `data/enemy.txt` parser

**Subsystems landed:**
- `src/tables_enemy.{c,h}` — pure-C parser for FUN_00475270 block #5
  (L834..L1026). 64 fixed enemy records at `&DAT_005c23f0` (stride
  0x68 = 104 bytes). Per-line **longest-common-prefix** match
  against the pre-baked record names, then 6 ints (HP/EXP/AT/DF/MA/MD)
  + 2 drop-item name lookups; both drops reset to -1 at line start.
  Pre-baked NAMES + boss flags live in `.data` (extracted from
  `vendor/unpacked/recettear.unpacked.exe` at file offset
  `0x1c0bf0`) and are populated via `tables_enemy_init` before the
  parser runs.
- `src/tables.c` — replaced the enemy.txt stub with a real loader.
  Init-then-parse pattern: call `tables_enemy_init(g_enemy)` to
  copy the 64 names + flags from `.data`, then `tables_parse_enemy`
  to overlay the stats. Boot trace logs `(enemies=N bosses=M)` with
  a counter that handles outlier vendor rows.
- `docs/formats/data-text.md` — appended a full enemy.txt section:
  line shape, 0x68-byte record layout, longest-prefix lookup with
  worked examples, pre-baked-record metadata, engine quirks, and
  lnkdatas-vs-overlay vendor shape (2801 vs 3589 bytes).
- `docs/findings/engine-quirks.md` — added quirk #21 (`enemy.txt`
  unmatched lines fire MessageBoxA on every boot of the original
  exe; `アルマ*` lines collapse onto a single record via the
  alias-prefix path).
- `tests/test_tables_enemy.c` — 10 cases: pre-baked init, basic
  record, longest-prefix wins (アーリマン緑 over アーリマン), shorter
  prefix when no longer match available, comments + blank lines
  skipped, per-line drop reset, unknown-name silently skipped,
  placeholder records (`name = " "`) skip match, no-trailing-newline,
  vendor-shape end-to-end with mixed-prefix routing.

**Engine fidelity divergences (documented):** the port silently
skips unmatched lines (engine pops a blocking MessageBoxA on every
one — vendor data triggers this 9 times per boot via the overlay
file's late-content lines). Drop-name → item-id resolution is
deferred until `item.txt` lands (slot #3, still a stub) — drops
resolve to -1 unconditionally. The seven runtime floats at
+0x44..+0x5f (collision/sprite-scale data, populated by
not-yet-ported runtime code) are left at zero in the port; the
engine ships them with a baked snapshot in `.data` that the parser
overwrites for stats but not these.

**Boot verification:** stderr now shows
`tables: data/enemy.txt — 3589 bytes (enemies=54 bosses=6)` against
the bmpdata overlay (which `storage_read` picks first). The 54
records match the count of unique pre-baked record names that the
overlay's 67 data lines route into via longest-prefix match. The
6 bosses come straight from the pre-baked flags table.

**Test status:** 91 tests pass (up from 81), no fails, no skips.

## 2026-05-20 — Phase B [6/15]: `data/snews.txt` parser

**Subsystems landed:**
- `src/tables_snews.{c,h}` — pure-C parser for FUN_00475270 block #12
  (L2238..L2401). Two unrelated globals populated from one file: a
  flat 64-slot name table keyed by 3-digit ID (`NNN:<text>` lines)
  and a 10×30 grid of floor-range sections keyed by SJIS dungeon
  names (`ダンジョン1`..`ダンジョン6`) with per-section weighted
  entry lists (`NNN,W` and `NON,W`). Only 6 of the 10 outer dungeon
  slots are reachable; the other 4 stay empty.
- `src/tables.c` — replaced the snews.txt stub with a real loader.
  Boot trace logs `(names=N sections=M)`, where `sections` counts
  records with non-sentinel `floor_start`.
- `docs/formats/data-text.md` — appended a full snews.txt section
  with line-shape table, the SJIS dungeon-key bytes, record layout
  for both globals, engine quirks (including the dungeon-transition
  off-by-one), and vendor-file shape with per-dungeon f: counts and
  weights.
- `docs/findings/engine-quirks.md` — added quirk #20 (snews.txt
  dungeon-transition floor-range corruption) with the full
  pointer-juggling story.
- `tests/test_tables_snews.c` — 10 cases: empty (sentinel init),
  name table (basic, empty value, overlong→truncated), comments +
  blanks skipped, single dungeon + section (with engine off-by-one
  verified), multiple sections within one dungeon, dungeon
  transition floor-end corruption (the quirk pinned in a dedicated
  test), entry-slot overflow dropped at port cap, and a full
  vendor-shape end-to-end with spot checks on every f:-line's
  landing position.

**Engine fidelity divergences (documented):** the dungeon-transition
floor-range corruption (quirk #20) is reproduced faithfully — the
first `f:N-M` line of every new dungeon writes its floor info to the
*previous* dungeon's last section before advancing. Vendor data is
structured so this is benign; consumers querying floor ranges still
see plausible matches. Port adds safety caps for overlong names
(>= 64 chars), name-table OOB IDs, and per-section entry-slot
overflow (>20 entries).

**Boot verification:** stderr now shows
`tables: data/snews.txt — 2230 bytes (names=25 sections=10)`. The 10
sections matches the trace: 11 `f:` lines across 6 dungeons, with
the off-by-one shifting the last-section-of-each-dungeon writes onto
the next-dungeon's first section, leaving dungeon 6's section [5][0]
with floor_start = -1 (no successor to write over it).

**Test status:** 81 tests pass (up from 71), no fails, no skips.

## 2026-05-20 — Phase B [5/15]: `data/chara.txt` parser

**Subsystems landed:**
- `src/tables_chara.{c,h}` — pure-C parser for FUN_00475270 block #6
  (L1030..L1146 outer + L76547..L76593 LAB_00477931 continuation).
  Two interleaved CSV sub-blocks share the same 8 records:
  `000:`..`007:` populates base stats (10 fields, 7 ints + 3 floats);
  `100:`..`107:` populates the level-100 endpoints (6 ints, permuted
  AT/DF/MT/MF/HP/SP → hp_lv100/sp_lv100/at_lv100/.../mf_lv100).
  Engine init seeds nine of the ten base fields per record (LV=1,
  HP=50, SP=30, AT=10, DF=13, MT=5, MF=10, move=0.15f, dash=0.20f);
  the port memsets to zero first so crit_rate and all lv100 stats
  start at 0 — a harmless superset.
- `src/tables.c` — replaced the chara.txt stub with a real loader.
  Heuristic for the boot trace: `level_threshold != 1` flags a
  parsed record (default is 1; vendor unlock-levels 1/8/10/15/20/30
  store as 0/7/9/14/19/29, none equal to 1). Boot trace now logs
  `(adventurers=N lv100=M)`.
- `docs/formats/data-text.md` — appended a chara.txt section with
  line-shape table, record layout, field-order permutation
  (file order vs in-memory layout for both sub-blocks), defaults
  table with bit-exact float values, engine quirks, the 10×8
  parse-loop overrun bug, and full vendor-shape table for the 8
  adventurers (Louie through Arma).
- `tests/test_tables_chara.c` — 9 cases: empty (defaults only),
  defaults bit-exact (0x3e19999a / 0x3e4ccccd match `0.15f` / `0.20f`
  byte-for-byte), basic record, lv100 alone, both blocks combined,
  comments skipped, OOR-index 008/009/108/109 guarded (no OOB
  write), lv100 field permutation with distinct sentinels,
  vendor-shape end-to-end with spot checks on Louie/Griff/Arma.

**Engine fidelity divergence (documented):** the engine's parse
loop iterates 10 times per sub-block even though only 8 records are
initialized — a 2-record overrun bug that would write into the
adjacent `g_models[0..1]` globals at `&DAT_073ae258` if chara.txt
contained any `008:` / `009:` / `108:` / `109:` lines. Vendor data
ships only `000:`..`007:` and `100:`..`107:`, so the bug is
dormant. The port caps the inner match loop at `CHARA_COUNT` and
silently drops out-of-range indices.

**Boot verification:** stderr now shows
`tables: data/chara.txt — 1868 bytes (adventurers=8 lv100=8)`,
matching the vendor file's 8 adventurer rows + 8 lv100 endpoint
rows. All other stubs continue to log as before; tutorial loop
still stops correctly at `tuto4.txt`.

**Test status:** 71 tests pass (up from 62), no fails, no skips.

## 2026-05-20 — Phase B [4/15]: `data/model.txt` parser

**Subsystems landed:**
- `src/tables_model.{c,h}` — pure-C parser for FUN_00475270 block #9
  (L1422..L1520). Fixed array of 20 records at `&DAT_073ae258` (stride
  0x2b8 bytes). Per-line dispatch: `no:N` sets current model index
  (atoi), `fname:` copies the `.x` filename, `NN:` (00..19) copies a
  bone/attachment-point name and increments `count`. Engine quirks
  faithfully reproduced: `local_c` defaults to 0 (writes before `no:`
  go to record 0); `used[slot] = 1` and `count++` fire unconditionally
  on every matching `NN:` line (no gate on `!used[slot]`); all 20 slot
  prefixes checked on every line. Safety divergences: fname + point
  names truncated at 31 chars + NUL to prevent field-overflow into
  adjacent record fields; out-of-range `no:N` (N < 0 or N ≥ 20) skips
  subsequent writes rather than computing an out-of-bounds pointer.
- `src/tables.c` — replaced the model.txt stub with a real loader.
  Counts `defined` (records with `count > 0`) and `max_points` (max
  `count` value across all records). Boot trace now logs
  `(models=N max_points=M)`.
- `docs/formats/data-text.md` — appended a model.txt section with
  line-shape table, record layout, engine quirks and safety
  divergences, and vendor-file shape including the out-of-order
  indices (17/18 appear swapped in the file).
- `tests/test_tables_model.c` — 9 cases: empty, basic one record,
  index threading (records 0 and 5), comments/blanks skipped, fname
  before any no:, repeated-slot count increment, overlong fname
  truncation (count field not corrupted), out-of-range no: skipped
  (no OOB write), vendor-shape end-to-end fixture with all 17 models
  and spot-checks on fname, point names, and gap indices 9/16/19.

**Engine fidelity divergence (documented):** the engine's write cap
for both fname and point names is 0x100, but the fname field is only
0x20 bytes before the `count` field — an overlong fname would
silently corrupt adjacent fields. Our port truncates at
`MODEL_DEF_NAME_MAX - 1 = 31` chars. Out-of-range `no:N` indices
are also guarded (engine would compute an out-of-bounds pointer on
`no:25` etc.). Vendor data has fnames ≤ 12 chars and indices 0..18,
so both guards are dormant against real input.

**Boot verification:** stderr now shows
`tables: data/model.txt — 1758 bytes (models=17 max_points=8)`,
matching the vendor file's 17 defined records and 8-point maximum
(kani models at indices 10 and 11). All other stubs continue to log
as before; tutorial loop still stops correctly at `tuto4.txt`.

**Test status:** 62 tests pass (up from 53), no fails, no skips.

## 2026-05-20 — Phase B [3/15]: `data/oder.txt` parser

**Subsystems landed:**
- `src/tables_oder.{c,h}` — pure-C parser for FUN_00475270 block #8
  (dispatch L1378..L1421 + inner CSV loop reached via
  `goto LAB_00477ffe` at L1813..L1931). Two parse phases plus a
  `LV:`-header dispatch: each data row is `<singular>,<plural>,
  <attribute>`, where field 1 writes at column position into the
  record (engine quirk faithfully reproduced with a safe truncation
  guard), field 2 writes sequentially after the first comma, and
  field 3 is hashed against a 16-tag SJIS attribute table at
  `&DAT_005fd7fc`. Record stride 0x4c (76 bytes) matching the engine.
- `src/tables.c` — replaced the oder.txt stub with a real loader.
  Boot trace now logs `(orders=N max_lv=M)`.
- `docs/formats/data-text.md` — appended an oder.txt section with
  line-shape table, record layout, the full 16-tag attribute table
  (SJIS bytes + kanji + romaji + meaning), inner-loop quirks
  (100-char cap, tab skipping, column-position writes), and the
  fallback name-table lookup that we intentionally suppressed
  until `item.txt` lands.
- `tests/test_tables_oder.c` — 9 cases: empty, single record, LV
  threading across data lines, all 16 SJIS tags → expected bits,
  English fallback (mask=0, attr_index=-1), tab skipping inside
  fields, 100-char inner-loop cap, no-trailing-newline EOF,
  vendor-shape end-to-end fixture with mixed SJIS/English rows
  across LV groups 1, 2, and 5.

**Engine fidelity divergence (documented):** the engine's fallback
linear search through `&DAT_0963e5f8` (item-name table, populated
by item.txt) is deferred — populated as `attr_index = -1`. When
item.txt parses we'll add a name-lookup callback hook. The
engine's MessageBoxA on unknown attributes is intentionally
suppressed so the port doesn't pop up "属性不明な登録" on boot.

**Boot verification:** stderr now shows
`tables: data/oder.txt — 1686 bytes (orders=24 max_lv=5)`,
matching the vendor file's 24 records across LV groups 1-5. All
other 16 stubs continue to log as before; tutorial loop still
stops correctly at `tuto4.txt`.

**Test status:** 53 tests pass (up from 44), no fails, no skips.

## 2026-05-20 — Phase B [2/15]: `data/config.idx` parser

**Subsystems landed:**
- `src/tables_config.{c,h}` — pure-C parser for FUN_00475270 block #2.
  Five live keys (`kanjioff`, `edgewi`, `effectmode`, `edgedel`, `font`)
  + one dead key (`makefont` — the engine matches 8 bytes against the
  bare word but assigns to nothing; we mirror the dead check). The
  `font:` value is copied as raw bytes into a 256-byte fixed buffer
  with safe truncation on overlong input.
- `src/tables.c` — replaced the config.idx stub with a real loader.
  Path-mismatch quirk still sidestepped via the read-side spelling
  (`"data/config.idx"`) for both `storage_get_size` and `storage_read`.
- `docs/formats/data-text.md` — appended a config.idx section with
  full key table, dead-makefont quirk, and the line-terminator
  handling difference from buysell.txt.
- `tests/test_tables_config.c` — 7 cases: empty input, all five
  live keys parsed together, `makefont:` no-op, SJIS font name
  (`ＭＳ Ｐゴシック`), font over-length truncation at the 256-byte
  cap, comment-only file (everything `/`-prefixed → all defaults),
  vendor-shape end-to-end (only `edgewi=2 edgedel=6` active).

**Boot verification:** stderr now shows
`tables: data/config.idx — 950 bytes (kanjioff=0 edgewi=2 edgedel=6 effectmode=0 font=(default))`,
matching the shipping vendor file's active key set exactly.

**Test status:** 44 tests pass (up from 37), no fails, no skips.

## 2026-05-20 — Phase B [1/15]: `data/buysell.txt` parser

**Subsystems landed:**
- `src/tables_buysell.{c,h}` — pure-C parser for FUN_00475270 block #7.
  Mirrors the engine's "match every prefix on every non-comment line"
  structure with five key forms: `ok:` (debug flag), `客番号:` / `種類:`
  (SJIS scalars), and `msg%02d:` / `rmsg%02d:` (two 20-int arrays).
  Engine-global instance `g_buysell`; tests use the out-parameter form.
- `src/tables.c` — replaced the buysell stub with a real loader that
  storage_reads the file, calls `tables_parse_buysell`, and logs the
  three scalars to the boot trace.
- `docs/formats/data-text.md` — new format-spec doc for the
  `data/*.txt` + `idx/*.idx` group. Documents shared conventions
  (Shift-JIS, CRLF, leading-`/` comments, two format families) plus a
  full section for buysell.txt (key table with byte-level SJIS
  identification, engine-side global addresses, the
  rmsg-before-msg in-memory layout quirk, vendor file sample).
- `tests/test_tables_buysell.c` — 8 cases covering empty input,
  comment-only files, the `ok:` toggle, the two SJIS scalar keys
  (using the exact byte sequences from the engine's `.data`),
  msg/rmsg arrays at boundary indices 0 and 19, EOF-without-newline,
  embedded-`\0` early-termination, and a vendor-shape end-to-end
  fixture that reproduces the actual file's CRLF + SJIS + comment
  layout with non-zero values.

**Boot verification:** stderr now shows
`tables: data/buysell.txt — 504 bytes (debug=0 kyaku=14 kind=2)`,
matching the vendor file's expected values (debug commented, customer
14, kind "about"=2). All other 16 stubs continue to log size lines as
before; tutorial loop still stops correctly at `tuto4.txt`.

**Test status:** 37 tests pass (up from 29), no fails, no skips.

## 2026-05-20 — FUN_00475270 ("init indexfile ok") skeleton + Phase A discovery

**Subsystems landed:**
- `docs/findings/tables-loader.md` — discovery doc for the gameplay
  tables loader: caller context (it's the boot trace step right after
  `init render ok`), full file list with sizes and per-block C-line
  ranges, helper identities (storage_get_size / storage_read / atoi /
  atof / free), the two format families observed (`/key:value` for
  `config.idx`; CSV-with-comments for the `data/*.txt` files), and
  the proposed one-commit-per-file Phase B plan.
- `src/tables.{c,h}` — skeleton dispatcher `tables_load_all()` calling
  fourteen stub loaders (one per file) plus a tutorial-loop stub. Each
  stub exercises `storage_get_size` + `storage_read` end-to-end and
  logs the byte count to stderr; the real parsers will replace the
  printf in Phase B without touching the dispatcher.
- `src/main.c` — wired `tables_load_all()` into the boot chain at the
  TODO marker that was already pinned for `FUN_00475270`. Position
  matches the engine's `init render ok → [HERE] → init fontsys ok`
  ordering.

**Engine quirks documented this turn:**
1. `FUN_00475270` calls `storage_get_size` and `storage_read` with
   different `.data` addresses in every block — usually two interned
   copies of the same path string. For `config.idx` the developer
   accidentally typed two **different** spellings (get_size with
   `"config.idx"`, read with `"data/config.idx"`), so the original
   silently `malloc(0+10) = 10` and overruns by 940 bytes on every
   boot. Our stub uses the read-side spelling to avoid the bug.
2. Tutorial format string is `"data/tuto%d.txt"` (no underscore).

**Boot verification:** stderr trace from `openrecet.exe
--max-duration-ms 2000` shows all 17 storage reads succeed (14 fixed
+ 3 tutorials), and the loop correctly stops at `tuto4.txt`. Several
files come back larger than the lnkdatas size because they have a
bmpdata-overlay patched version (e.g. `enemy.txt`: 2801 → 3589).

**Test status:** 29 tests pass (no new tests yet — Phase B will add
per-file fixture tests as each parser lands). Boot smoke clean.

## 2026-05-20 — FUN_004341d4 bookkeeping (file-size helper)

Pinned candidate #2 closed as already-done. `FUN_004341d4` is the
trivial `fseek(0,SEEK_END); ftell; fseek(0,SEEK_SET)` file-size
helper, and it was already faithfully translated as
`storage_file_size` in `src/storage.c:139` during the
`storage_init`/`FUN_004341fe` port (with an in-file comment naming
the original). All four in-engine call sites we've ported (the ones
inside `storage_init` itself) route through it.

The other three inlined `fseek/ftell/rewind` idioms in `src/tga.c`,
`src/sprite.c`, and `src/lnkdatas_hash.c` were written by us, not
ports — they intentionally check the fseek/ftell return values
(the original doesn't). Left untouched so the defensive coverage
stays in place; promoting a 5-line static into a shared util module
would have been premature abstraction. Dropped from the
session-starter pin list.

## 2026-05-20 — lnkdatas content read + LZSS

**Subsystems landed:**
- `src/lnk_lzss.{c,h}` — port of FUN_004349e5 (the lnkdatas LZSS decoder).
  Pure C, no Win32 surface.  ~50 LOC. Stream is self-delimiting via the
  back==0 sentinel, so no input size is required.
- `src/storage.c` — extended `storage_get_size` and `storage_read` to fall
  back to the lnkdatas index when the asset isn't in the bmpdata overlay.
  Adds a 1-deep `bin/data%03d.bin` FILE* cache and a 10 MiB chunk-spanning
  reader (handles entries that straddle a `bin/data*.bin` boundary).
  Skips the original engine's 3× `Sleep(500ms)` retry loop around the
  fopen — that was robustness against transient I/O on 2007 spinning
  drives, not load-bearing for a modern Steam install.
- `tests/test_lnk_lzss.c` — 7 synthetic unit tests covering single
  literals, short / extended back-references, self-overlap RLE, the
  end-of-stream sentinel mid-control-byte, high-bit back-distances, and
  mixed flags within one control byte. Plus a vendor round-trip that
  iterates every entry in `vendor/original/lnkdatas.bin`, reads its
  slice (across chunk boundaries as needed), decompresses, and verifies
  the result length matches the declared `dsize` + that a one-byte
  output canary is intact.

**Two case-sensitivity quirks worth knowing:** the bmpdata branch of
`FUN_00434585` / `FUN_004346bf` does case-insensitive name matching
(A..Z folded to a..z) over 88 bytes; the **lnkdatas branch does a
straight byte compare** over 128 bytes — no fold. Our port mirrors
both. Callers relying on case-insensitive lookup must hit through the
bmpdata path.

**Pixel-exact validation:** rebuilt the standalone harness
`/tmp/storage_extract.exe` (built from `src/storage.c` with
`-DSTORAGE_TEST_EXTRACT`) and confirmed byte-identical output vs the
Python reference (`tools/extract/data-bin.py`) on 5 entries including
4 chunk-straddling ones (`xfile/koku_last/mahoujin.tga`,
`xfile/wall/kabe_check.bmp`, `bmp/chr/chr31.bmp`,
`bmp/worldmap_yugata.bmp`). Hashes match (SHA-256).

**Test status:** 29 tests pass (up from 21), no fails, no skips.
Sanitizer-clean. ASan caught two bugs while writing tests — both in
the *test fixtures*, not in the decoder: a mis-computed control byte
in `test_lnk_lzss_self_overlap` (0x28 should have been 0x30) and a
use-after-free on the canary value in the vendor round-trip. Good
ASan-pays-for-itself moment.

**Engine smoke:** boot scenario `tools/smoke-test.py` still exits 0
in ~4s on the rebuilt exe, debug-magenta clear color unchanged.

**Next pin (per session-start):** `FUN_00475270` is the big one —
3965 decompiled lines of `data/*.txt` parsing (item / kyaku / chara
/ enemy gameplay tables) plus `idx/stage.idx` and `idx/config.idx`.
Will likely need splitting across multiple commits.

## 2026-05-20 — Sanitizer-instrumented unit tests

**Subsystems landed:**
- `tests/Makefile` — Linux-native test harness. Host gcc +
  `-fsanitize=address,undefined -fno-sanitize-recover=all`. Run with
  `make -C tests run`.
- `tests/t.h` — dependency-free assertion macros (T_ASSERT, T_FAIL,
  T_SKIP) + UBSan-safe byte writers.
- `tests/test_main.c` — runs registered tests, supports name-substring
  filter via `argv[1]`, exits non-zero on any failure (skips don't
  count).
- `tests/test_{bmp,tga,bmp_lzw,lnkdatas_hash}.c` — 21 tests covering
  every audited code path in the four portable decoders.

**Why now:** the Win32 sprite loader just landed, and every decoder
ingests user-controlled bytes (BMP/TGA from `bmpdata.bin`, LZW slices,
CRC over the whole lnkdatas blob). Memory bugs in these can pass
pixel-equality checks while still being broken. Valgrind/ASan can't
run Win32 PE binaries, so the natural split is "Linux test target for
the portable .c files". The Win32 layer stays exercised by smoke
tests.

**Doc fix discovered during test writing:** `src/lnkdatas_hash.{c,h}`
called the engine's hash "CRC-16/CCITT-FALSE". It's *shaped* like
CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, final invert) but
the feedback step uses **subtraction** instead of XOR. The standard
CCITT-FALSE check value for "123456789" is 0x29B1; ours is 0xF5B7
because of borrow propagation. Cross-checked against
`/opt/src/recettear-repacker/crc.py`.

**Sanity check the harness catches real bugs:** temporarily injected
a 1-byte OOB read past the BMP pixel buffer and reran. ASan
pinpointed the exact `bmp.c:77` line with the heap region details.
Reverted; all 21 tests pass green.

## 2026-05-20 — `FUN_0047193c` ported — engine-style sprite loader

**Subsystems landed:**
- `src/bmp.{c,h}` — 24-/32-bit BI_RGB DIB decoder with color-key
  application (engine passes `0xFF00FF00` to D3DX → we test exact-match
  pure-green and zero the alpha). Top-down + bottom-up.
- `src/tga.c` extended — now also handles Type 10 RLE, plus an
  `tga_load_mem(buf,size,*img)` variant so the loader can decode
  storage-fetched bytes without round-tripping through disk.
- `src/sprite.c` — new `sprite_load(dev, name, w, h, *out)` entry
  point. Mirrors `FUN_0047193c`: tries `fopen(name,"rb")` first, falls
  back to `storage_read(name)`, sniffs `'BM'` → BMP-with-key vs TGA,
  decodes, uploads via the existing `sprite_create`.
- `src/main.c` — replaced `--show-tga <path>` (direct-file shim) with
  `--show-sprite <name>` that routes through the new `sprite_load`.

**Identification fix carried into `docs/findings/texture-loader.md`:**
the earlier write-up had the disk and storage calls swapped (claimed
the engine tried storage first with disk as fallback). Re-reading
`FUN_005038b0` as a thin `fopen` wrapper (forwards to `FUN_00503890`
with a `0x40` buffer-size hint) flipped it — **disk is tried first,
storage second**. The user-facing implication: external/mod overrides
on disk take precedence over the packed asset, consistent with how
`recettear-repacker` works.

**Validation (pixel-perfect, max diff 0/255 in all four cases):**
- Storage path: `--show-sprite bmp/ivent/ed_kasi11.tga` (resolves via
  `storage_read` from `bmpdata.bin`) renders byte-identical to a
  reference Python decode composited over the debug-magenta clear
  color, across all 512×32 pixels.
- Synthetic disk fixtures (built in-place, then cleaned up): a 64×64
  Type-2 TGA, a 64×64 Type-10 RLE TGA, and a 64×64 24-bit BMP with one
  half pure-green keyed and the other half opaque blue. All three
  round-trip 0 mismatches against the expected composite.

**Not yet engine-accurate:** D3DX-style resampling to `(expected_w,
expected_h)` is still skipped. Every audited asset ships at native
resolution, so this matters mainly for forward-compat / mod paths
that intentionally scale.

**Lifecycle fix (orphan-window cleanup, follow-up commit):** ad-hoc
`timeout 3 openrecet.exe …` runs kept leaving the host's Windows
side with orphan windows because `g_paused` blocks the main loop in
`WaitMessage` when the window loses focus — any deadline check
inside `tick_and_present` is never reached. Added `--max-duration-ms
<ms>` (also taken up by `tools/smoke-test.py`) that registers
`SetTimer` → `WM_TIMER` → `DestroyWindow`, which fires regardless of
pause state. Smoke harness now reaches `exit=0` gracefully instead
of falling through to SIGTERM/taskkill.

Next-milestone candidates: lnkdatas content-read path (so `storage_read`
also services `bin/data_NNN.bin` + the LZSS decompressor at
`FUN_004349e5`); `FUN_004341d4` standalone port (mostly mechanical);
diving into `FUN_00475270` (gameplay-text-table parser, 3965 lines —
needs splitting across commits).

## 2026-05-20 — `bmpdata.bin` LZW decoder + storage_read overlay path

**Subsystems landed:**
- `src/bmp_lzw.{c,h}` — 12-bit MSB-first LZW decompressor. Translation
  of `FUN_00434b32` (main loop), `FUN_00434c2c` (bit reader), and
  `FUN_00434ca9` (dict-chain walker). Dictionary frozen at 3839 entries,
  matches `recettear-repacker/bmp_unpack.py` exactly.
- `src/storage.{c,h}` extended — now also opens `bmpdata.bin`, slurps
  it into memory, validates the hash sentinel `0x21dc`, and exposes
  `storage_get_size(name)` + `storage_read(name, dst)`. Mirrors
  `FUN_00434585` (size lookup) and `FUN_004346bf` (read into buffer) for
  the bmpdata branch.

**Identification fix:** `FUN_00475270` (originally pinned as "likely
bmpdata.bin LZW loader" in PROGRESS) turned out to be the global
gameplay-text-table loader — a 3965-line parser for `data/item.txt`,
`data/chara.txt`, the `idx/stage.idx` chain, etc. The real LZW lives in
the much smaller `FUN_00434b32` + helpers, called lazily from the
storage read path. Plan annotation corrected.

**Engine deviation, by design:** the engine's `FUN_00434b32` doesn't
handle code 256 (LZW reset/EOS) — it walks past the dict base on the
sentinel and emits a few garbage bytes past the caller's `dsize`
buffer. Benign in the shipping game (callers tolerate the overrun) but
we'd rather not write past the asked-for size, so our decoder honors
256 explicitly. End result: byte-for-byte identical with
`bmp_unpack.py` output, not byte-for-byte identical with the engine's
overrun-prone output.

**Validation:** all 22 entries in the shipping Steam `bmpdata.bin`
round-trip through `storage_init → storage_read → stdout` to
byte-equal output vs the Python reference (see `/tmp/storage_diff.py`).
Boot smoke (`tools/smoke-test.py`) still green — exit signal, 3 frames
captured, no early-exit error from the now-stricter `storage_init`
(which is required to find `bmpdata.bin`).

**New format spec:** `docs/formats/bmpdata.md` — 84-byte names, 3 ×
int32 (dsize/offset/csize) per entry, 96-byte stride, 12-bit LZW
payload, hash sentinel `0x21dc`.

Next-milestone candidates (unchanged): `FUN_0047193c` (proper sprite
loader using `storage_*` with BMP + green-key + RLE-TGA — now possible
because `bmpdata` lookups work), `FUN_004341d4` standalone port, or
diving into the `FUN_00475270` gameplay-data parser.

## 2026-05-19 — Render-layer init ported (`FUN_00454e69` + `FUN_004038e4`)

**Subsystem landed:** `src/layers.{c,h}`. The "init render ok" hand-off
isn't device creation (that's step 11) — it's the engine fanning
`GetDeviceCaps` + back-buffer-desc + the live device pointer out into
its 24 per-layer state objects (each 0x2f0 bytes). Two arrays:
`g_layers_b[20]` (loop, `DAT_073da2f0` stride 0x2f0) and `g_layers_a[4]`
(unrolled in asm at `DAT_073cba20`/`+0x2f0`×3). See
`docs/findings/winmain-and-bootstrap.md` §"Render-layer init" for the
RE writeup + offset table.

**Layout corrections from the earlier guess:**
- The previous notes claimed the loop "zeros" the structs via
  `FUN_004038e4`. It doesn't — it actively writes `device` (`+0x108`),
  the back-buffer `D3DSURFACE_DESC` (`+0x10c`, 32 bytes), and a copy of
  `D3DCAPS8` (`+0x12c`, 212 bytes), then nulls `+0x200`.
- The 20-element loop is only *one* of two arrays; the 4 unrolled
  trailing calls operate on a *separate* 4-element array — easy to miss
  from the decompiler output because Ghidra strips the ECX setup before
  each thiscall.

**Skeleton wiring (`main.c`):**
- Removed the placeholder `IDirect3D8_GetDeviceCaps` from `init_render`
  — the real owner is now `layers_init`.
- `layers_init(g_d3d, g_dev)` slotted in after `input_init`, matching
  the original's `…dinput ok → init render ok` ordering (the previous
  comment had this misplaced).
- Bootstrap-order comments now mirror the actual call sequence.

**Why the struct is field-by-field (not a byte blob):** mingw's `d3d8.h`
ships `D3DCAPS8 = 212`/`D3DSURFACE_DESC = 32` — exact match to the
original's `rep movsl 0x35` and `0x12c−0x10c = 0x20`. Five
`_Static_assert`s on the known offsets + total size catch any future
header drift at build time.

**Verified:** `tools/smoke-test.py --target openrecet --scenario boot
--duration 4 --capture` — debug magenta `(160, 32, 96)` reads flat
across all 4 captured frames; no crash on init or shutdown.

Next-milestone candidates (unchanged): `FUN_00475270` ("init indexfile
ok" — likely `bmpdata.bin` LZW loader, cross-ref
`/opt/src/recettear-repacker/bmp_unpack.py`), `FUN_004341d4` (file-size
helper, quick mechanical port), or porting `FUN_0047193c` properly to
read assets via `storage_*` with BMP+green-key + RLE-TGA support.

## 2026-05-19 — Project bootstrap

**What landed**
- Decisions (see [`PLAN.md`](PLAN.md) §3): C + mingw-w64 32-bit, DirectX
  direct, MIT, Win32-first drop-in.
- `flake.nix` with full RE toolchain (ghidra 12, radare2, rizin, cutter,
  retdec, imhex, wine staging, frida-tools, mingw32 i686 cross compiler,
  python env with construct/scikit-image/pillow/opencv, xvfb-run, scrot,
  ffmpeg, imagemagick, pandoc).
- Directory structure: `src/`, `tests/`, `tools/`, `docs/`, `vendor/` (gi),
  `ghidra/` (gi).
- Plan, README, MIT license, `.gitignore` that aggressively protects any
  derived game data, `.editorconfig`.

**What we know about the original**
- `recettear.exe`: 32-bit PE, **SteamStub-packed** (VLV signature @0x80),
  5.6 MB on disk.
- `custom.exe`: ~462 KB config tool reading `recet.ini`.
- Assets: `bin/data###.bin` (custom archives, format TBD), `xfile/*.x` and
  `xfile2/*.x` (DirectX retained-mode `.x` text models — open spec),
  `bgm/*.wav`, `ef/effect*.dat`, `bmpdata.bin`, `lnkdatas.bin`,
  `recet_op.wmv`.
- `recet.ini` exposes: `winmode`, `fps`, `dispfps`, `usefog`, `usemipmap`,
  `usetree`, `windowpos`, `uselighttex`, `nolight`, `easydisp`, `bgnodisp`,
  `texlevel`, `toorioff`, `s_easydisp`, `sfnouse`, `pfnouse`,
  `fontmode1`/`fontmode2`, `screen`, `texmode`, `mapmode`, `demomode`, plus
  `pad##` / `skill##` key bindings and `[config] se`/`mu` audio levels.
  These are the engine's main feature toggles — each one is a hint about
  a code path we'll meet.

**Note on wine vs WSLInterop** — WSL has `WSLInterop` registered as a
binfmt handler, so `.exe` invocations run natively on Windows by default.
We use that for Steamless and for casual play. The automated test harness
still uses wine + Xvfb so that (a) the runtime is pinnable via the flake
and (b) original-vs-ours diffs share the same backend and don't surface
wine-vs-Windows differences as phantom bugs. See `PLAN.md` §6 for details.

**Tooling landed**
- `tools/setup.sh` — symlinks game, runs Steamless via WSLInterop, prints sha256s.
- `tools/ghidra-headless.sh` + `tools/ghidra-scripts/ExportDecompiledC.py` —
  batch decompile every function to `docs/decompiled/` (gitignored).
- `tools/smoke-test.py` — Xvfb+wine runner with frame capture and SSIM diff
  against a golden run.
- `tools/contact-sheet.py` — single-set or side-by-side downscaled grids, with
  optional `--zoom` full-res crop strip.
- `tools/extract/xfile.py` — DirectX `.x` text-format summarizer + tree scanner.

**First extractor result (validates pipeline)** — running
`xfile.py xfile/city/dun_city00.x` on the user's Steam install reports:

| template               | count |
|------------------------|------:|
| Material               |    58 |
| TextureFilename        |    25 |
| Frame                  |    13 |
| FrameTransformMatrix   |    13 |
| Mesh                   |    12 |
| MeshMaterialList       |    12 |
| MeshNormals            |    12 |
| MeshTextureCoords      |    12 |
| MeshVertexColors       |     1 |

All **standard DirectX 9 retained-mode** templates. No custom extensions →
`xfile/` and `xfile2/` can be parsed with stock `D3DXLoadMeshFromX*` or any
open-spec parser. (Phase 2 will scan the full tree for a definitive answer.)

**Flake quirk** — `retdec` currently fails to build in nixpkgs (capstone
sub-build error). Disabled it. Ghidra + radare2/rizin cover decompilation
cross-checks. Re-enable if upstream fixes.

**Next** (Phase 1 entry)
- User runs `./tools/setup.sh` to unpack the exe.
- Then `./tools/ghidra-headless.sh` to produce `docs/decompiled/`.
- First subsystem to map: WinMain + main loop. Find `D3D*Create*` calls in
  the decompiled output → confirms DirectX version (likely 8 or 9).

---

## 2026-05-19 — Setup ran, first findings from unpacked binary

- ✅ `tools/setup.sh` ran successfully on the user's machine — Steamless via
  WSLInterop produced `vendor/unpacked/recettear.unpacked.exe` (5.0 MB,
  down from 5.6 MB packed; 7 PE sections → 6 sections; VLV signature gone).
- Fixed `tools/ghidra-headless.sh`: nixpkgs ghidra names its binaries
  `ghidra-<tool>` (e.g., `ghidra-analyzeHeadless`), not bare `analyzeHeadless`.

**New findings — recorded in [`findings/imports-and-layout.md`](findings/imports-and-layout.md):**

- **DirectX version is 8** (d3d8.dll / d3d8d.dll / D3DERR_*). Fixed-function
  pipeline only — no shaders, no HLSL compiler required.
- **DirectX is loaded dynamically** via `LoadLibraryA` + `GetProcAddress` —
  static imports are only `KERNEL32`, `USER32`, `SHELL32`, `WINMM`,
  `ole32`, `ADVAPI32`. Six DLLs total. Very tight.
- **`DirectXFileCreate` is used** for `.x` model parsing (open DX File API).
- **No `dsound.dll` / `dinput.dll` static imports.** Audio likely via
  `WINMM` (`mciSendString` / `waveOut*`) or dynamically-loaded DSOUND;
  input likely raw `USER32` `WM_KEYDOWN` / `GetKeyState`. To be confirmed.
- **Asset layout discovered from strings:** the binary references
  `bmp/item/item%02d.bmp`, `bmp/item_win.tga`, `data/item.txt`,
  `bin/se/.../*.bin`, etc. None of these paths exist on disk — confirming
  that `bin/data###.bin` archives contain the `bmp/` (TGA/BMP textures)
  and `data/` (plain text gameplay tables) trees. Cracking this format
  unlocks all 2D art and all gameplay data.
- **All UI/dialogue strings are inline in `.rdata`** — no string table,
  no `.po` files. i18n story is "rebuild the binary".

**Next**
- ~~Run `./tools/ghidra-headless.sh`~~ Done — 2620 functions decompiled.
- ~~Locate the function that opens `bin/data000.bin` → archive format~~
  Pre-empted: spec was already cracked by UnrealPowerz/recettear-repacker.
- Locate `WinMain` and the `LoadLibraryA("d3d8.dll")` site → document
  the window+device init sequence for the skeleton in phase 3.

---

## 2026-05-19 — Ghidra working, cross-references absorbed

**Ghidra:**
- Fixed the post-script: nixpkgs Ghidra 12 isn't built with PyGhidra, so
  `.py` scripts fail. Rewrote `ExportDecompiledC` in **Java** — works
  in plain headless mode without flags.
- Also fixed a latent bug: the script had `-deleteProject` set on the
  first import, which would have wiped analysis state. Removed.
- Result: **2620 functions** decompiled into `docs/decompiled/all.c`
  (6.3 MB), `by-address/*.c`, `by-name/*.c`, `functions.csv`.

**Cross-reference projects** (cloned to `/opt/src/`):
- **UnrealPowerz/recettear-repacker** — full spec for `bin/data*.bin`
  archives. Format: 10 MiB chunks of LZSS-compressed blobs indexed by
  big-endian `lnkdatas.bin`. Custom LZSS variant with 12-bit back-distance
  + MSB-first ctrl byte. `bmpdata.bin` is a separate LZW-compressed
  update overlay.
- **ribeena/RecettearXTools** — `.x` ↔ USD Blender 4.1 converter; useful
  for double-checking our `.x` parser.
- **just-harry/FancyScreenPatchForRecettear** — runtime widescreen
  patcher; useful as a map of engine offsets we'll want to understand.

**New format spec:** [`formats/data-bin.md`](formats/data-bin.md).

**Our own extractor:** `tools/extract/data-bin.py` — clean Python
reimplementation matching the spec. Validated against upstream:
**byte-identical** output on the current Steam build (1188 files extracted).
Run `./tools/extract/data-bin.py vendor/original --validate-against
/opt/src/recettear-repacker` to re-verify.

**Newly confirmed about the engine:**
- **DirectInput 8** (`dinput8.dll`) is also dynamically loaded — found
  `DirectInput8Create` symbol at `0x4a1cc0`.
- **C++ compiled with MSVC** — `vector_constructor_iterator` /
  `vector_deleting_destructor` indicate array new/delete scaffolding.
- **MFC is statically linked** — `RFX_Text_Bulk` (MFC ODBC field exchange)
  present. Probably leakage from `custom.exe` sharing libs; possibly
  engine uses some MFC for save serialization (TBD).
- PE entry is at `0x5046c7` — MSVC `__tmainCRTStartup`. `WinMain`
  symbolic name not yet auto-resolved.

**Next investigation targets**
1. Trace `__tmainCRTStartup` → `WinMain`. Rename in the Ghidra project.
2. From `WinMain`, find the `LoadLibraryA("d3d8.dll")` call → document the
   DX8 device-creation sequence. (Skeleton for phase 3.)
3. Read `recettear-repacker/crc.py` — the engine probably uses the same
   CRC as a path hash for `bmpdata.bin` lookups. Worth porting.
4. Optional: read `FancyScreenPatchForRecettear` patch sites to find
   resolution-clamping code (a likely candidate for an early test
   subsystem since the patches are small and well-isolated).

---

## 2026-05-19 — Engine bootstrap mapped end-to-end

Full writeup in [`findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md).
Highlights:

- **WinMain at `0x47bfb3`**. Identified by the standard MSVC
  `__tmainCRTStartup(hInst=GetModuleHandleA(NULL), 0, lpCmdLine, nCmdShow)`
  call signature in the PE entry.
- **Engine internal name is "Azumanga"** — that's EGS's name for their
  custom engine (also powers Chantelise). Window class is literally
  `"Azumanga Main Window"`.
- **Window title is `"RECETTEAR Ver 1.108"`** — exact version string for
  drop-in compatibility.
- **Debug logger `FUN_0047aa31` is a 1-byte `return;` stub** — all
  logging compiled out in the release build. The `s_init_*` string
  constants remain in `.rdata` as breadcrumbs, which **gave us the full
  subsystem init order for free**:
  `start → strage → print → dinput → render → indexfile → fontsys →
  daoudio → fontsystem → systemtex → savefile → titletex → main loop`.
  (Note Japanese-English typos preserved: `strage`, `daoudio`.)
- **Main loop function: `FUN_0047be92`** — the game tick.
- **`Direct3DCreate8(0xDC)` at line 77975 of `all.c`** — `0xDC = 220 =
  D3D_SDK_VERSION`. Global `IDirect3D8 *` is `DAT_073dfcb8`.
- **DirectInput 8 init: `FUN_0047af52`** — keyboard + EnumDevices for
  joysticks, with axis range ±5000 and 100-unit deadzone.
- **Storage init: `FUN_004341fe`** — tries `lnkdata.bin` (JP name) first,
  falls back to `lnkdatas.bin` (EN name). Validates the index via
  `FUN_00474f14` which must return `-0x7456` (`0xFFFF8BAA`); this is the
  engine's integrity hash, almost certainly matches
  `recettear-repacker/crc.py`.
- **WndProc `FUN_0047b2e7`** handles `WM_CREATE`, `WM_DESTROY`,
  `WM_ACTIVATE` (pause + DI un/acquire), `WM_CLOSE` (confirm dialog in
  windowed), `WM_KEYDOWN` (ESC only — rest of input via DInput).

**Process docs added:** [`AGENT-WORKFLOW.md`](AGENT-WORKFLOW.md) — codifies
the Opus-orchestrator / Sonnet-subagent split + briefing template + stop
conditions. Read at the start of every new session.

**Stop point:** engine bootstrap is mapped. Logical next milestone:
either (a) write the phase-3 skeleton drop-in `src/main.c` matching the
init order, or (b) start translating the high-value individual functions
(`FUN_0047be92` game tick, `FUN_00474f14` integrity hash, the d3d8 wrapper
that calls `Direct3DCreate8`). User to choose.

---

## 2026-05-19 — Phase 3 skeleton drop-in runs

**`src/main.c` + `src/Makefile` written and building.** The skeleton
mirrors the bootstrap chain from
[`findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md):
high-resolution timer setup → window class register (`"Azumanga Main
Window"`) → CreateWindowExA (`"RECETTEAR Ver 1.108"`) → LoadLibraryA
(`d3d8.dll` → `d3d8d.dll` fallback) → `Direct3DCreate8(D3D_SDK_VERSION)`
→ `IDirect3D8::CreateDevice` → message pump with `PeekMessage`/
`WaitMessage`/`tick_and_present`. Each subsystem in the original's init
order is a `TODO` comment naming the `FUN_XXX` we still need to
translate.

**Builds at 77 KB** via `i686-w64-mingw32-gcc` from inside `nix develop`.
Static libs: `-ld3d8 -ldinput8 -ldsound -lwinmm -lgdi32 -luser32
-lkernel32 -lole32 -ladvapi32 -lshell32`.

Tick path currently does `Clear → BeginScene → EndScene → Present` with
a distinctive **debug magenta** clear color (`160, 32, 96`) so a working
boot is visually obvious vs a black-screen failure.

**Test harness pivoted to WSLInterop** (see updated `PLAN.md` §6):

- Modern nixpkgs `wineWow64Packages.stagingFull` skips the 32-bit
  `syswow64/` layer → 32-bit binaries fail to load `kernel32.dll`.
- `wineWowPackages.stagingFull` (classic dual-arch) builds from source on
  every machine, slow.
- WSL2 + WSLInterop is rock solid and runs the exe natively on Windows.
  Trade-off: tests pop a window on the desktop. We'll work around this
  with self-emitting back-buffer captures inside the exe
  (`--capture-to <dir>`, not yet wired).
- Wine dropped from the flake entirely.

`tools/smoke-test.py` rewritten — no Xvfb, no wine, no scrot. Launches the
exe via WSLInterop, captures exit code + duration + stdout/stderr + sha256.
First run: `openrecet.exe` ran cleanly for 3 seconds, was killed by
timeout (exit code -15, SIGTERM), `taskkill /F /IM openrecet.exe` confirmed
clean shutdown.

**Next**
1. Wire `--capture-to <dir>` into `src/main.c` — save back-buffer as 32-bit
   BMP every N frames, into the harness's `runs/<scenario>/<id>/frames/`.
2. Translate `FUN_00474f14` (the lnkdatas integrity hash) to validate our
   `tools/extract/data-bin.py` matches the engine's expected sentinel.
3. Start filling in subsystem stubs — first target: `FUN_004341fe`
   (storage init / lnkdatas loader) so the skeleton actually opens the
   game's index file. Good Sonnet-subagent task.

---

## 2026-05-19 — All three subagent tasks landed; capture pipeline works end-to-end

**Subagent 2: CRC hash port** — `src/lnkdatas_hash.{c,h}` +
`tools/extract/lnkdatas_hash.py`. Algorithm identified as
**CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`, MSB-first, final
`~crc`). Validates byte-identical against `recettear-repacker/crc.py`;
on the real `lnkdatas.bin` (sha256 `6c5b93cf…`) returns `0x8BAA`
(= `-0x7456`, the engine's "valid" sentinel).

**Subagent 3: Storage init port** — `src/storage.{c,h}`. Caught a
critical detail the first writeup missed: **`FUN_004341fe` has two
distinct format paths**. The JP build's `lnkdata.bin` (singular) has a
5-byte header skipped + a `byte' = 0x01 - byte` payload transform +
sentinel `0xC5E1`. The EN build's `lnkdatas.bin` (plural) is raw +
sentinel `0x8BAA`. Both implemented. Findings doc
[`winmain-and-bootstrap.md` §"Storage init"](findings/winmain-and-bootstrap.md)
corrected.

**Subagent 1: Frame capture** — `src/main.c` gained `--capture-to <dir>`
+ `--capture-every N` CLI flags. Renders BMPs at intervals via
`GetBackBuffer → LockRect → fwrite`. Initially silent-failed; root cause
identified as needing `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER` in the present
parameters AND capturing **before** `Present()` (we use
`D3DSWAPEFFECT_DISCARD` which makes post-Present back-buffer undefined).
Both fixed. Capture now runs at ~5000 FPS in the empty-tick state
(660 frames captured in 4 seconds of un-vsync'd rendering — capture
overhead is negligible).

**Subagent integration issues** worth noting for future use of the
AGENT-WORKFLOW pattern:
- The first attempt at subagent 1 (frame capture) failed because
  `isolation: worktree` requires an existing git commit — we have none
  yet. Re-ran without isolation; safe because the other two created only
  new files. **Action:** make an initial commit before relying on
  worktree isolation.
- Subagents 2 and 3 raced on the `lnkdatas_hash` signature: subagent 2
  used `(buf, size) → int16_t`, subagent 3 assumed `(size, buf) →
  uint16_t` and inlined a fallback impl in `storage.c`. Caused a
  duplicate-symbol link error. **Action:** when subagents share an
  interface, brief them with the exact signature, not "infer it".
- Subagent 3 caught the JP/EN dual-format detail in `FUN_004341fe` that
  the orchestrator (me, Opus) had missed in the initial writeup. Good
  outcome — second-pass careful reading by a fresh agent surfaced
  something a quick first read glossed over.

**End-to-end visual confirmation:**
- User reported seeing the debug-magenta window during a manual run.
- Captured BMP frame 60 center pixel = exactly `RGB(160, 32, 96)`.
- 4-tile contact sheet via `tools/contact-sheet.py` shows all magenta.
- Pipeline: mingw32 build → `openrecet.exe` (91 KB) → WSLInterop →
  Windows 32-bit process → `storage_init()` loads 1188 lnkdatas entries
  via the EN path → DX8 device with `LOCKABLE_BACKBUFFER` → tick loop
  → BMP captures → contact sheet → visual diff ready.

**Tooling fix:** `tools/contact-sheet.py` now sets
`ImageFile.LOAD_TRUNCATED_IMAGES = True` to bypass PIL's strictness on
32-bit BI_RGB BMPs with an X-padding byte. The BMPs are structurally
valid (file size, headers, pixel layout all correct — verified manually);
PIL treats the X byte as alpha and trips a bounds check. Not actually
truncated.

**Stop point.** Skeleton boots, has working frame capture, has real
lnkdatas integrity-validated load. Logical next milestones (pick one,
or parallelize via subagents per AGENT-WORKFLOW.md):

1. **Initial git commit** so future subagents can use `isolation: worktree`.
2. **Translate `FUN_004341d4`** — the file-size helper used by storage init
   (currently we reimplemented it inline; matching the original is cleaner).
3. **Translate `FUN_0047af52`** — the DirectInput8 init chain.
4. **Translate `FUN_00475270`** — the "init indexfile ok" subsystem, almost
   certainly the `bmpdata.bin` LZW loader (cross-reference with
   `/opt/src/recettear-repacker/bmp_unpack.py`).
5. **Translate `FUN_00454e69` + surroundings** — the D3D8 device creation
   site, so we can match the original's exact present parameters and
   render-state initial values (necessary for pixel-identical diffs once we
   have real rendering).
6. **First real rendering** — load a single TGA from the extracted assets
   and draw it via a screen-aligned quad. Confirms the texture pipeline
   before we tackle any of the engine's actual draw paths.
- Wire up `tools/ghidra-headless.sh` for batch decompilation.
- Confirm DirectX version from unpacked imports.
- First extractor: `xfile.py` (validate pipeline against known format).

---

## 2026-05-19 — First real rendering: TGA + screen-aligned quad

**What landed:** `src/tga.{c,h}` (uncompressed truecolor TGA Type 2, 24/32-bit,
bottom-up or top-down → BGRA), `src/sprite.{c,h}` (`IDirect3DTexture8` via
`CreateTexture(D3DPOOL_MANAGED) + LockRect + memcpy`, screen-aligned quad
via `D3DFVF_XYZRHW | DIFFUSE | TEX1` + `DrawPrimitiveUP`, with
`SRCALPHA/INVSRCALPHA` blending and the standard half-pixel offset).
Wired into `src/main.c` behind a `--show-tga <path>` CLI flag.

**Verification.** Ran `openrecet.exe --show-tga bmp/window.tga
--capture-to <dir> --capture-every-ms 500` for 4 seconds via WSLInterop;
captured 8 BMPs showing the 64×64 `window.tga` (a rounded UI button)
correctly alpha-blended over the debug-magenta clear. Math check on
frame 4: TGA pixel `(32,32) = (23,23,47, α=133)` blended over
`(160,32,96)` predicts `(88, 27, 70)`; captured pixel reads `(89, 27,
70)` — agrees to within rounding.

**Engine-accuracy gap recorded** in
[`findings/texture-loader.md`](findings/texture-loader.md). `FUN_0047193c`
(the original's loader) hands work to `D3DXCreateTextureFromFileInMemoryEx`
(identified by 15-arg call site + D3DXERR_INVALIDDATA error path). For
BMPs it applies the green color-key `0xFF00FF00`; TGAs use native alpha.
We deliberately bypass d3dx8 (not in nixpkgs, deprecated) and will grow
our own decoders to match the engine's output. Current `tga.c` handles
Type 2 only — BMP-with-color-key and RLE-TGA come next.

**Next milestones (unchanged from prior stop point except #6 done):**

1. Translate `FUN_004341d4` (file-size helper).
2. Translate `FUN_0047af52` (DInput8 init chain).
3. Translate `FUN_00475270` (`bmpdata.bin` LZW loader; cross-ref
   `recettear-repacker/bmp_unpack.py`).
4. Translate `FUN_00454e69` + neighbours (D3D8 device creation, for
   matching the original's present parameters and initial render states).
5. Port `FUN_0047193c` properly — read assets via `storage_*`, accept
   BMPs with the green color-key, add RLE-TGA. Replaces `--show-tga`'s
   direct-file path.
6. ~~First real rendering~~ — done (this entry).

---

## 2026-05-19 — DirectInput 8 init ported (keyboard + joysticks)

**Subsystem landed:** `src/input.{c,h}` — full port of `FUN_0047af52`
("init dinput ok") plus its cleanup at `FUN_0047b0ef` and the
WM_ACTIVATE Acquire/Unacquire dance. Wired into `main.c` after
`init_render` and into `WndProc:WM_ACTIVATE` so deactivation correctly
releases device focus (matches the original's behavior).

**Pieces traced and ported:**
- `FUN_0047af52` — outer init: `DirectInput8Create`, keyboard create +
  `SetDataFormat(c_dfDIKeyboard)` + `SetCooperativeLevel(FOREGROUND|NONEXCLUSIVE)`
  + `SetProperty(DIPROP_BUFFERSIZE = 100)` + `Acquire`; then
  `EnumDevices(DI8DEVCLASS_GAMECTRL, ATTACHEDONLY)` followed by per-joystick
  `SetProperty(DIPROP_AXISMODE = ABS)` + `DIPROP_BUFFERSIZE = 100` + `Acquire`.
- `LAB_0047b167` — joystick enumeration callback. Ghidra never decompiled
  this (came up as a label, not a function); read directly from objdump on
  `vendor/unpacked/recettear.unpacked.exe`. Calls
  `IDirectInput8::CreateDevice(lpddi->guidInstance, ...)` into a 4-slot
  array, then `GetCapabilities` as a liveness probe — failure releases the
  device and zeroes the slot. Caps the joystick count at 4
  (`cmp 4; setl` — explains the static `g_joys[INPUT_MAX_JOYS]` layout).
- `FUN_0047b1f2` — per-object enum callback for `IDirectInputDevice8::EnumObjects`
  with filter `DIDFT_AXIS|DIDFT_POV`. Sets each enumerated object's
  `DIPROP_RANGE` to ±1000 via `DIPH_BYID`. (Earlier writeup said ±5000 — that
  was wrong; bytes are `0xFFFFFC18` = −1000 and `0x03E8` = 1000.)
- `FUN_0047b0ef` — symmetric shutdown: Unacquire+Release for the keyboard,
  each joystick slot, then Release the `IDirectInput8` factory.

**Other corrections to the bootstrap findings:**
- Keyboard `SetProperty` is `DIPROP_BUFFERSIZE=100`, not "DIPROP_RANGE ±5000"
  as I'd transcribed initially. The ±5000 number was never in the binary.
- The `WM_ACTIVATE` decision uses both `LOWORD(wParam)` (active/inactive)
  and `HIWORD(wParam)` (minimized flag): paused = inactive OR minimized.

**Toolchain note:** had to add `-ldxguid` to `src/Makefile` so the linker
resolves `IID_IDirectInput8A`, `GUID_SysKeyboard`, and the data-format
GUIDs that `c_dfDIKeyboard` / `c_dfDIJoystick` reference internally.

**Verified:** `tools/smoke-test.py --target openrecet --scenario boot
--capture` runs cleanly for 5 frames — debug-magenta still reads
`(160, 32, 96)` flat across the back-buffer, no crash on init or
shutdown, no MessageBox.

Next-milestone candidates (unchanged ordering from the session-starter
memo): `FUN_004341d4` (file-size helper), `FUN_00475270` (bmpdata.bin
LZW loader), `FUN_00454e69` ("init render ok" — post-device render-state
init), or porting `FUN_0047193c` properly to read assets through
`storage_*` and accept BMP+green-key in addition to TGA.

## 2026-05-19 — D3D8 device creation properly identified + matched

**Correction:** the bootstrap doc previously labeled `FUN_0047ac6a` as
"second-stage init" and `FUN_00454e69` as "init render ok". After
reading the `WinMain` dispatch carefully, **`FUN_0047ac6a` is the actual
D3D8 device-creation function** (`Direct3DCreate8` + `CreateDevice`,
present-params, behavior-flag fallback) and `FUN_00454e69` is post-device
render-state init that runs the "init render ok" log on completion.

**Findings updated** in
[`winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md): full
present-params field map, the unusual fullscreen=COPY+VSYNC swap-effect
choice (vs. windowed=DISCARD), the CreateDevice behavior-flag fallback
chain `0x44 (HW+MT) → 0x80 (MIXED) → 0x20 (SW)`, and the
`[setup] screen` resolution-lookup table
(0=640×480, 1=800×600 default, 2=1024×768, 3+=1280×960).

**Skeleton updated** — `src/main.c init_render()` now mirrors the
present-params layout (windowed/fullscreen split, COPY+VSYNC for
fullscreen) and walks the same `0x44 → 0x80 → 0x20` BehaviorFlags
fallback. Deliberate deviations recorded in code comments:
`hDeviceWindow=hwnd` (original leaves NULL → focus-window fallback,
behaviorally equivalent), `Flags=LOCKABLE_BACKBUFFER` only when
`--capture-to` is set (capture-only toggle), and hardcoded 800×600 until
the `recet.ini` parser lands.

**Verified:** smoke test runs cleanly with the new HW+MT-first chain;
sprite blend pixel still reads `(89,27,70)` — no regression.

Next: port `FUN_0047af52` (DInput8) — next subsystem in the bootstrap
order, contained scope.
