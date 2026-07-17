<!--
  The ONE hand-edited status block.  tools/gen_port_ledger.py injects everything
  below the marker line verbatim into docs/STATUS.md's "Current front" section, so
  STATUS can never drift from reality.  Update THIS when the active front moves;
  keep it short (a 60-second read).  Everything else in STATUS is derived from code.

  RULES (the 2026-06-09 cleanup): keep ONLY open/forward-looking items here. When
  an item RESOLVES, move its story to PROGRESS.md / the findings doc in the same
  edit — do not let resolved arcs accumulate.  Full pre-cleanup snapshots:
  archive/FRONT-2026-06-09-full.md (world-map backlog, dialogue fixes, load-arc,
  shop-display chips) and archive/FRONT-2026-07-01-full.md (the whole
  customer-service/haggle + §21 determinism arc, title/pause/guild/prologue arcs,
  trace-studio-v3 build-out).  Code-tagged PORT-DEBT lives in docs/port-debt.md
  (derived) — FRONT only carries debts NOT yet tagged in src/.
-->
<!-- FRONT:BEGIN -->
- **▶ ACTIVE ARC (2026-07-16) — PARITY EVIDENCE COMPILER (truthful-ledger program).** Building the
  evidence compiler of `docs/plans/parity-evidence-roadmap.md`: a scenario contract + a v3 window →
  a content-addressed proof bundle with fail-closed per-pillar verdicts (identity/state/render/pixels/…),
  so a claim is scoped to real evidence, not a source marker. **Wave-0 EP-00→EP-05 LANDED; M0 REACHED
  2026-07-16** (R3 adversarial review of the first real bundle — `findings/parity-M0-adversarial-review.md`).
  First corpus contract lives on `house-firstcust-arrprobe` (scenario.yaml `proof:` block, `HOUSE_FREEROAM#1
  [1,80]`); its bundle is a **truthful FAIL** (identity PASS · render_program FAIL on the b494 80-tri 0-px
  strip · pixels+later pillars NOT_CAPTURED) — our most-confirmed scene is pixel-1:1 by eyeball yet NOT
  tool-proven parity. Review fixed a `proof_id` portability leak (abs path in a hashed pillar note →
  `observations.portable_reason`; regression `test_proof_id_portable`). **✅ EP-06 LANDED 2026-07-16 —
  truthful two-axis ledger** (`findings/parity-EP06-ledger-lifecycle.md`; `gen_port_ledger.py` rewrite +
  `test_gen_port_ledger.py` 152 checks): INVENTORY rung `discovered→source-referenced→implemented→instrumented`
  (src markers) SPLIT from RUNTIME rung `retail-executed→…→matrix-proven` (needs a `docs/parity-proof-index.json`
  bundle). STATUS headline flipped "2.8% runtime-verified" (a lie) → **"0% runtime-proven — 85 instrumented,
  index empty"** (index SEEDED at ★NEXT-b′ 2026-07-17, 1 runtime binding — see below); the 501 "ported" are now honestly `source-referenced` (a `FUN_` mention ≠ a port claim). New
  `PORT-OF(0xVA)` opt-in attestation reaches `implemented` w/o a probe (0 seeded — backfill is author work).
  `status` enum kept as a DEPRECATED alias (mem_watch byte-stable); `--check` idempotent. **✅ EP-08 LANDED
  2026-07-16 — HOLE-2 CLOSED + cache re-keyed by full provenance; Wave-0 EP-00→EP-08 COMPLETE**
  (`findings/parity-EP08-cache-provenance.md`; commits `62ece6e`+`5713074`). **(B)** `orv3_view` now bakes
  `port/retail_container_sha256`; `parity_prove.resolve_observations` threads them as `source`+`expected_containers`
  into adapt_pixels/adapt_render_program ⇒ a foreign/stale metrics doc (matching frame keys, DIFFERENT source
  container) → INCONCLUSIVE, not a false PASS (regression `test_container_provenance`). **(A)** the v3 cache dir
  key is now `sha256(common_provenance)+arm` **128-bit** over `{cache_schema,trace,proxy,assets,recet.ini}`;
  per-side `{pe,agent}` in `v3meta.prov` validated on lookup (`side_provenance`/`_staleness`) ⇒ a rebuilt
  proxy/agent/exe re-drives the RIGHT side, a port fix STILL never invalidates the retail cache; corrupt +
  pre-EP08 entries rejected + every stale decision logged (`test_provenance_keying`). **NB one-time:** every
  pre-EP08 entry (8-hex, no prov) is STALE ⇒ next `orv3_window` re-drives it once (retail = the serialized
  load-stretch). **✅ PIXELS PILLAR PRODUCER LANDED 2026-07-16** (`findings/parity-pixels-producer.md`; commit
  `8514b9d`): the headless `pixel-metrics.json` producer M0's last required pillar was waiting on.
  `replay.exe --render-dump` (resident, RT-correct RGB dump) + `tools/parity/pixel_producer.py` (pure core +
  driver, `differ = pixel_diff.amplified_diff`, stamps `source` container hashes, FAIL-CLOSED) + CLI
  `tools/parity_pixels.py` + 24-check test. **`house-firstcust-arrprobe` [1,80] proof is now `identity PASS ·
  render_program FAIL · pixels FAIL`** (first div `HOUSE_FREEROAM#1+1`) — a TRUTHFUL FAIL: our most
  human-confirmed-1:1 scene is visually 1:1 (`gt8` 3–5 px/frame) but NOT bit-exact (±1 sub-perceptual
  cross-target noise, `meanabs`≪1; the near-black off=2 fade frame dominates `differ` at 517046/786432). `mode:
  exact` = strict bit-equality is the honest gate; a "visually 1:1" contract would need an R3-approved threshold
  mode (schema extension), NOT a silent comparator tolerance — not added. **✅ ST-00/ST-01 LANDED 2026-07-16**
  (`findings/parity-save-producer.md`; commits `fe4101f`+`33f706b`): the **canonical STATE MODEL**
  (`schemas/state-map-v1.json` region map + `reference/canonical-state.md` 4 classes + `tools/parity/state_map.py`
  offset→region localizer, from `save_bank.h`) + the **`save` pillar PRODUCER** — `tools/parity/save_producer.py`
  compares the two `save.dat` a `--target both` drive writes (port `--save-write-dir` vs retail CreateFileW/A Frida
  hook, BOTH seeded from the same `{savefile}`) byte-for-byte, localizes the first div to a named region + buckets
  every diff by region; `save.py` adapter (PASS/FAIL/NOT_CAPTURED/INCONCLUSIVE); `parity_save.py` CLI; wired into
  `parity_prove` (`save` off `UNBUILT_PILLARS`); +41-check test incl. the **M1 one-byte-mutation negative test**.
  Capture needed NO new engine/agent work (survey-confirmed). **First verdict `house-pause-save-commit`: save FAIL**,
  6836/18.8M bytes, first div `bank0/occupied_playtime` (phase-origin frame count), 4 regions — incl. a REAL
  `ranking_records` divergence across banks 1–99 (port fresh-bank init / checksum-gate ≠ retail for UNUSED banks;
  invisible to every frame pillar — the save pillar's raison d'être). **✅ SAVE-PILLAR CATCH FIXED 2026-07-16 —
  the confirmed port bug is CLOSED, byte-exact vs retail** (`findings/parity-save-producer.md` §Leads; commit
  pending). The "ranking_records" region is a MISNOMER — dword `0x9e76` is the **encyclopedia (図鑑) discovery
  store** (`FUN_0049f012`=`encyclopedia_setup`, mislabeled "RANKING" by the port author). 3-way seed/port/retail
  bytes: the seed's banks 1–99 are never-committed slots (valid magic, **stale checksum `0x0`≠computed**, key+count
  populated by a prior 図鑑 open); retail's `FUN_004901c2` **gates its verify sweep on `DAT_095d3728`** (set on
  save-load) ⇒ preserves them verbatim (`retail==seed`, 0 diffs). The port IGNORED the gate ⇒ always swept ⇒
  `save_bank_init_one` re-inited the stale banks, zeroing key+count + re-stamping checksum. **Fix: modeled the gate**
  (`g_save_bank_skip_verify` in save_bank.c, gate the sweep, set in `save_io_try_load`, reset by `arena_clear`) +
  renamed the state-map region `ranking_records`→`encyclopedia_discovery`. **VERIFIED `--target openrecet`: banks
  1–99 `port^seed=0` `port^retail=0`; save diff `6836→6` bytes** (only bank-0 `occupied_playtime` phase-origin + its
  cksum echo + 1 unmapped remain — all non-logic). +2 host tests; 3432/0. **✅ ★NEXT(a) BUNDLE LANDED 2026-07-16 — full `parity_prove` bundle**
  (`findings/parity-save-producer.md` §"Full proof bundle LANDED"): `house-pause-save-commit` carries a `proof:`
  block (schema_v2, join `SAVE_PICKER_READY#1 [1,19]`, `required_pillars:[identity,save]`) → **identity PASS ·
  save FAIL (exit 1)** @ `bank0/occupied_playtime` (stable id = `contract_sha256 77e8e3f4…`; the proof_id binds
  git_commit+drive so it advances each commit ⇒ regenerate via `parity_prove`, don't hard-cite) — the phase-origin near-PASS (6
  non-logic bytes, recorded as an R3 save exception), idempotent+portable, the save-pillar analogue of arrprobe's
  honest FAIL. **★ GOTCHA: arm the v3 window at `--anchor SAVE_PICKER_READY`** (the default HOUSE_FREEROAM desyncs
  under load-stretch ⇒ 0 pairs; arming right ⇒ 19 gap-free pairs). Committed canonical env-json
  `docs/reference/parity-host-environment.json` (8 operator-attested EP-02/HOLE-3 fields ⇒ reproducible
  proof_ids). Also **fixed a stale parity test** (`ranking_records`→`encyclopedia_discovery` refs left RED by
  6c9c85d — pre-commit runs C tests, not the Python suites; suite now green, `test_parity_schema` auto-validates
  2 contracts). Lead: the commit-anim frames DON'T identity-join (port stays SAVE_PICKER_READY, retail re-anchors
  PAUSE_OPEN during the disk write). **✅ ★NEXT(b) LANDED 2026-07-17 — save PASS, the FIRST fully-passing
  multi-pillar bundle** (`findings/parity-save-producer.md` §"★NEXT(b) LANDED"; commits `d686739`+`64d2e3f`).
  `house-pause-save-commit` → `parity_prove` verdict **PASS: identity PASS · save PASS · 0 divergences** (+render_program
  PASS bonus; `contract_sha256 9c2d2755…`). TWO real port bugs the save pillar caught, both fixed byte-exact
  (invisible to every frame pillar): **(1) house_cam_flag 0xb37d** — the continue-resume `scene_post_fade_init`
  didn't clear the from-world-map camera flag retail zeroes (`FUN_0049a59e` all.c:100642), so a `{savefile}` taken
  after a map→house return re-committed a stale 1 (NOT phase — a stable port=1/retail=0; `d686739`). **(2)
  occupied_playtime** EXPLAINED then pinned: the drive-variance is the port counting the TWO completion-based
  async-load brackets (house + pause menu) into playtime — a wall-clock CreateThread race under turbo (house-load
  Δ2387 + pause-load Δ1641 = the observed Δ4028; retail's are the deterministic intro-video cadence Δ48/Δ8). Both
  sides tick identically (`playtime = engine_frame + 13598`); the port is lower + variable only because it commits
  at an earlier, race-variable frame. Fix = a NEW bilateral **`{playtimepin:[F,V]}`** (mirrors `{gsimpin}`): force
  the active slot's playtime accumulator to a canonical V at SAVE_PICKER_READY (first anchor past both variable
  loads); both port+agent fire pre-sim ⇒ both land on V+K=29687, no off-by-one. FORWARDED to the agent (retail's
  natural swings 29643/29683/**29830** run-to-run, so a port-only pin can't match). VERIFIED `--target both` ×2:
  save.dat byte-identical (ndiff 0), drive-stable; +2 host tests (3434/0). Dropped the contract's save exception.
  **✅ ★NEXT(b′) LANDED 2026-07-17 — the FIRST runtime-axis binding; STATUS off "0% runtime-proven"**
  (`findings/parity-save-producer.md` §"★NEXT(b′) LANDED"). `docs/parity-proof-index.json` binds `FUN_004905a8`
  (`save_io_commit_slot`) → **scenario-pillar-proven**, keyed on the DURABLE `contract_sha256 9c2d2755…`
  (parity_prove's stable, drive/commit-independent scenario-contract hash — reproduces from the committed
  `scenario.yaml` ⇒ self-cites inside its own commit, defeating the `proof_id` volatility that deferred it).
  Schema tweak: `gen_port_ledger.load_proof_index` now REQUIRES `contract_sha256` (64-hex, fail-closed) +
  `proof_id` OPTIONAL/advisory; the ledger runtime table keys on it. STATUS **"0% runtime-proven, index empty" →
  "1 function runtime-proven"** (0.0% of 2548 — the first non-empty rung, INVENTORY≠PARITY made real). +155-check
  suite (fail-closed on missing/malformed contract_sha256 or malformed proof_id; live-tree test pins the shipped
  binding), `--check` idempotent. NB `proof_id ba2c0c8c…` = advisory snapshot from the pre-commit dirty drive
  (binding rests on contract_sha256; a clean-HEAD re-drive freshens it — optional). **✅ ★NEXT(c) LANDED
  2026-07-17 — the volatile `state` pillar (ST-02 Merkle roots + ST-03 producer)** (`findings/parity-state-producer.md`;
  commit `efaf2e7`). The per-frame VOLATILE-state sibling of the persistent `save` pillar: proves the once-per-frame
  engine state (rng/phase/player/companion/interaction/customer-service/dialogue/title) bit-identical port↔retail.
  Mirrors the save pillar — a canonical encoder + domain-separated **Merkle roots** (ST-02, `tools/parity/state_codec.py`
  + `state_merkle.py`; R3 subsystem tree `docs/schemas/state-volatile-v1.json` over the 4 STATE_VA fields, types resolved
  from `retail_fields.json` so a grouping can't drift) as the comparison mechanism of a **view.json producer**
  (ST-03, `state_producer.from_view_json` → `state-metrics.json`) + `adapt_state` (PASS/FAIL/NOT_CAPTURED/INCONCLUSIVE,
  `match_frames` coverage-gated) + CLI `tools/parity_state.py`, wired into `parity_prove` (`state` OUT of
  `UNBUILT_PILLARS`), +72-check gate. **R3 finding: `rngcalls` is BENIGN-EXCLUDED** — the Frida-agent cumulative
  counter (`src:rngcalls`) has a per-SIDE ORIGIN (port from process start, retail from hook-install) ⇒ its absolute
  value is capture-origin-dependent (class-3 environmental), NOT a game global; the deterministic RNG value is the raw
  `rng` LCG state (matching it frame-over-frame is STRONGER than a counter). **VALIDATED on two real captures:** (1)
  `house-firstcust-arrprobe` (HOUSE free-roam, full subsystems) — raw `rng` **1498/1498**, and the pillar is MORE
  SENSITIVE than pixels: it FAILs on the FRONT's KNOWN-OPEN companion residuals (`companion/cx` ~3-ULP facing blip,
  `companion/ccnt` +20 pose-era tick offset) that pixels round away — documented residuals, not false positives. (2)
  `house-pause-save-commit` (save-picker, rng-only scene) — full-window Merkle-IDENTICAL (port volatile state IS
  bit-1:1). **✅ ★NEXT(d) LANDED 2026-07-17 — retail `--state` head warm-up CLOSED ⇒ the FIRST three-pillar
  (identity·save·state) volatile+persistent proof** (`findings/parity-state-producer.md` §"★NEXT-d LANDED").
  ROOT (settled, NOT hook-install latency — the STATE_VA hooks install pre-resume): the `{calltrace}` WINDOW GATE.
  A `--state` v3 drive kept the scenario's `{calltrace}` op, armed in `segtraceOnSegmentEnter` (input_poll, ONE cycle
  AFTER the anchor's Present) — and each frame's SIM runs BEFORE its Present, so a window keyed to an anchor detected
  at Present F can NEVER cover sim F..F+1 ⇒ the state stream began at anchor+2 (`7120` vs d3d `present_first 7118`)
  and, `hi=lo+len` inclusive, ran 1 PAST the d3d tail. The d3d proxy has no warp (arms in-process at the anchor
  Present, zero latency), so the two arm paths were desynchronized at the head. FIX (`orv3_state`'s ORIGINAL "emit
  broadly, window the OUTPUT by identity" design, restored): (1) **un-gate** — `frida_capture` strips `{calltrace}`
  on ANY v3 drive ⇒ the 4 once-per-frame VAs emit EVERY frame (live during the head sim); (2) **slice at cache** —
  `v3cache.store()` windows the sidecar to `kept_presents=set(c.presents)` (frame==present, the join key) ⇒ drops the
  pre-window stretch AND the tail. VERIFIED (re-drove `--force-retail`): retail state == the d3d window EXACTLY (offset
  0 AND 1 covered); `parity_state` **`[1,19]` PASS 19/19, `--all-frames` 200/200**; `state` ADDED to
  `required_pillars` ⇒ `parity_prove` **PASS: identity·save·state (+render_program bonus), 0 divergences**.
  `contract_sha256 9c2d2755…`→`c8c9a6a5…`; `parity-proof-index` `FUN_004905a8` re-keyed + pillars `[identity,save,state]`.
  +`test_orv3.test_state_sidecar_slice`; 72 + orv3 gates green. **✅ ST-04 LANDED 2026-07-17 — first-divergence
  state report** (`findings/parity-state-producer.md` §"ST-04 LANDED"; commit pending). The DIAGNOSTIC sibling of the
  `state` ADAPTER: given a FAIL, localizes the first divergent LOGICAL frame → leaf ROOT PATH + schema TYPE + TYPED
  VALUES + RAW BITS (canonical encoder bytes) + LAST MATCHING frame + value TRANSITION (state-derivable mutation
  provenance: port-MISSED / port-SPURIOUS / port-WRONG) + every CO-DIVERGENT leaf. `tools/parity/state_diff.py` (pure
  core, reuses ST-02 codec+Merkle — new `all_divergent_leaves`) + CLI `tools/state_diff.py` + 43-check gate;
  fail-closed §4.1 verdict, a test proves it AGREES with `adapt_state`; `provenance:null` seam for ST-05. VERIFIED:
  `house-pause-save-commit` **PASS 200/200**; `house-firstcust-arrprobe` **FAIL @ `LOADING_START#1+0 companion/cx`**
  retail_bits `2709b5bc`/port `2309b5bc` (the documented ~3-ULP facing residual — 1 mantissa bit) + 23 co-divergent.
  **✅ ST-05 CONSUMER LANDED 2026-07-17 — semantic-mutation causal layer** (`findings/parity-state-producer.md`
  §"ST-05 CONSUMER LANDED"; `reference/state-mutations.md`; commit pending). The layer BENEATH ST-04 — names the
  WRITER behind a transition. Per rule 11 the CONSUMER lands before the platform: R3 mutation model (event shape +
  semantic/derived/noise class gate + grounded event catalog, `schemas/state-mutation-v1.json`) + the consumer
  (`tools/parity/state_mutation.py`) that RECONSTRUCTS a subtree (idempotent/dedup), localizes the FIRST WRONG WRITE
  (cumulative per-frame value; shared window-start recovered from a write's `old` ⇒ a one-sided write IS a real
  divergence), enforces **first-wrong-write ≤ first-state-root-divergence** (the ST-04/ST-05 link), + fills ST-04's
  `provenance:null` seam (`state_diff.py --mutations`, host-tested end-to-end). 44-check gate. **DEFERRED:** the Frida
  post-write/TTD CAPTURE PLATFORM (owners `attested-at-capture`; only `save_slot_commit→FUN_004905a8` certain).
  **★ NEXT:** the ST-05 capture platform (lands when a scenario needs provenance) + ST-06 scene-by-scene subsystem
  expansion (needs new RE + live introspection + R3 field-approval — a drive-capable-session task; the schema already
  groups every declared STATE_VA field, only 2 benign exclusions remain). Follow-up: the port `--state` sidecar
  re-slices on its next drive (un-gated-wide but join-correct).
  **✅ GX-00 STATIC + DYNAMIC CENSUS LANDED 2026-07-17 — D3D8 capture completeness + first live verdict**
  (`findings/gx00-d3d-method-census.md`; `schemas/d3d8-method-census-v1.json`;
  `tools/parity/d3d_census.py`+`tools/d3d_census.py`; commits `cac0840`+pending). A `pixels`/`render_program` PASS
  is only sound if every render-affecting D3D8 call was RECORDED. **STATIC:** censuses the v3 proxy's 113 vtable
  methods → 23 recorded / 6 wrapper / 45 query-only / 6 irrelevant / **33 render_affecting_unsupported** (fail-closed
  RISK: Reset·SetViewport·state-blocks·ProcessVertices·shaders·palettes·resource-creation·cursor — forwarded-uncaptured);
  lead `SetPixelShader` forwarded while `SetVertexShader` recorded. **DYNAMIC + GX-01 gate:** `gen_forwarders.py`
  instruments all 84 `fwd_` thunks with a process-lifetime `InterlockedIncrement` (zero hand-edits; recorded =
  captured-by-construction) → proxy emits `v3cap.census.json` per kept frame (threaded through the v3 cache);
  `d3d_census.py --dynamic` gates it SAFE(0)/VIOLATION(1)/INCONCLUSIVE(2). **★ First verdict —
  `house-firstcust-arrprobe` [1,80] → VIOLATION both sides** (NOT the expected SAFE — the census earns its keep on
  our most-confirmed scene): `CreateVertexBuffer`+`CreateIndexBuffer` forwarded-uncaptured (retail 130×/port 13×
  each), **31/33 risk methods 0-observed** (no Reset/SetViewport/state-blocks/shaders/cursor) — a SURGICAL
  resource-creation gap = the **GX-03/GX-04 hinge**. Content IS snapshotted late (`snap_vb`/`snap_ib`); residual risk
  is same-frame re-mutation. Sharpens arrprobe's M0 honest-FAIL with a concrete mechanism (distinct from the b494
  render_program FAIL; count magnitude 130vs13 = process-lifetime scope, NOT a parity signal). 63-check guard incl.
  the roadmap negative test (a deliberate `SetViewport` can't pass as complete).
  **✅ GX-03/GX-04 LANDED 2026-07-17 — VB/IB WRAPPED, arrprobe census VIOLATION → SAFE**
  (`findings/gx03-resource-versions.md`; commits `2cd7401`(probe+spec)+`403ae49`(wrap)+`9c3d298`(fixture)).
  Probe (`resource_binds` sidecar, kept-frames-only): arrprobe reuses **3 VB+3 IB/frame, 0 snapfail** — risk
  surface active but well-scoped. **Completeness key:** D3D8 `CreateVertexBuffer` has NO init-data param ⇒ VB/IB
  content is set ONLY via Lock/Unlock (+ ProcessVertices, census-gated 0-observed), so WRAPPING the buffers +
  intercepting every Lock/Unlock is **provably** complete (not "probably static"). Mechanism: `my_CreateVB/IB`
  wrap the real buffer (`WrapVB/WrapIB`); Lock/Unlock maintain a content shadow + generation;
  `SetStreamSource/SetIndices` UNWRAP (pass real) + FREEZE the shadow into a per-frame arena at bind;
  `write_frame` snaps the frozen bytes (body-identical to the old `snap_vb` ⇒ static buffers dedup to the same
  id, replay unchanged; a same-frame re-mutation now yields TWO versions). Census: Create VB/IB → recorded, +2
  buffer interfaces (Lock/Unlock recorded); drift guard follows generically (141 methods, 31 risk; +9 buffer
  checks = 72). **VALIDATED both paths:** arrprobe re-drive (cache `30d6b861`) BOTH sides **80/80 bit-exact**
  (transparent), census dynamic **SAFE** (31/31 risk 0-observed), `vb/ib_fallback=0` (every bind frozen),
  **RES_VB stays 5** with full Lock/Unlock visibility ⇒ EMPIRICALLY proves the reused buffers are static; + the
  synthetic `gx04_fixture.exe` (`test_gx04_fixture.py`, 6 checks) drives A,B,A binds → **2 distinct RES_VB**
  (SPLIT A≠B + DEDUP the re-bind) — the positive mutation path the old frame-end snapshot got wrong.
  **✅ GX-01-full LANDED 2026-07-17 — census wired as a HARD pixels/render_program precondition**
  (`findings/gx00-d3d-method-census.md` §"GX-01-full LANDED"; commit pending). R3 policy call: the census is a
  fail-closed bilateral PRECONDITION on the two D3D-stream-replay pillars in `parity_prove` (`capture_completeness`,
  `parity/d3d_census.py`) — both `pixels`+`render_program` reconstruct the frame from the captured D3D8 stream ⇒
  SOUND only if the capture was COMPLETE on BOTH sides; not-SAFE (VIOLATION/INCONCLUSIVE/ABSENT either side) ⇒ BOTH
  OVERRIDE to **INCONCLUSIVE** (never a false PASS/FAIL — an incomplete capture makes a FAIL as untrustworthy as a
  PASS). `identity`/`state`/`save` don't read the D3D stream ⇒ NOT gated (correctly scoped). Plumbing: census is
  process-lifetime (1 sidecar/side); `orv3_slice` carries `v3cap.census.json` forward verbatim (a slice inherits its
  drive's census, like `call_trace.jsonl`), `orv3_view` bakes the RAW per-side sidecar (`port/retail_census`) into
  view.json (single source of truth = the committed census; `parity_prove` recomputes the verdict against it so a
  GX-02 update re-adjudicates without a re-bake), `_census_gate` stamps `capture_completeness` on each gated pillar's
  observation + `census_schema_sha256` into `tools`. **VALIDATED:** unit (`test_d3d_census` +28=91; `test_parity_prove`
  census gate — SAFE no-op, `SetViewport` VIOLATION→INCONCLUSIVE = the GX-01 acceptance negative, ABSENT→INCONCLUSIVE,
  VIOLATION overrides an intrinsic pixel FAIL, identity ungated) + **end-to-end on the real M0 window** (`house-firstcust-
  arrprobe [1,80]`, pure cache re-slice, no drive): census carried→baked→**SAFE both sides** (31/31 risk 0-observed) ⇒
  gate no-op ⇒ **identity PASS · render_program FAIL · pixels FAIL** unchanged, each render pillar stamped
  `census[SAFE/SAFE]`. The b494 render FAIL + sub-perceptual pixel FAIL are now PROVEN over a complete capture (not
  artifacts of a forwarded-uncaptured call) — arrprobe's honest FAIL is now census-SOUND.
  **✅ GX-05 LANDED 2026-07-17 — dedup byte-compare + reader corruption-safety** (`findings/gx03-resource-versions.md`
  §"GX-05 LANDED"; roadmap §9 GX-05). Decision: **hash-plus-byte-compare, NOT SHA-256** — collision-PROOF not merely
  -resistant (the arc's ethos), no crypto ported into 3 languages, and the ONLY option that makes the forced-collision
  acceptance CONSTRUCTIBLE. **(1)** `dedup_or_write` now BYTE-COMPAREs the retained body (+type+len) on a FNV-64 hash hit
  ⇒ a collision → a NEW id (both kept distinct), NEVER a false dedup; size/type/format in the identity domain; distinct
  bodies retained in RAM (bounded by the dedup'd set, process-lifetime like `g_cb`); per-drive `dedup` soundness block in
  the census sidecar (`{distinct,collisions,retained_bytes,force_collision}` — a real drive MUST show `collisions:0`; the
  census parser ignores the extra key, `test_d3d_census` 91/0). **(2)** the AUTHORITATIVE C reader (`replay_core.c` —
  viewer + pixel producer): `cspan`/`cspan_n` (division-domain, 32-bit-overflow-safe) bound every variable-length span +
  fit checks (`dl<=size`, `lh<=ld/lrb`, `up_fits`/`idxup_fits`) ⇒ no memcpy/draw reads OOB; the cursor poisons to EOF on
  overflow ⇒ walk terminates. **(3)** the Python parsers (`orv3.py` primary + `orv3_draws.py` render_program) bounds-check
  every read → explicit ValueError (not a raw `struct.error`/silent short-slice). **Acceptance MET + both proven DECISIVE
  (fail on the pre-GX-05 code):** `gx05_fixture` (A,B,A,C under a forced-collision seam → **3 distinct RES_VB, resids
  [0,1,0,2]**, census `collisions:2`; two vacuous-pass guards), `corrupt_fuzz` (**40000 fuzz** + crafted truncation/
  overflow → cursor never escapes `[buf,end]`), `test_orv3.test_corrupt`. **Transparent on valid data:** `replay
  --verify-hashes` `title-encyclopedia` **120/120 BIT-EXACT**, `pause/port` byte-identical to HEAD (its DIVERGENT is
  pre-existing). GX-04 unregressed. 3 GX tests wired into `run_python_tests.py`.
  **✅ GX-06 LANDED 2026-07-17 — graphics-capture regression corpus; the GX ARC IS COMPLETE**
  (`findings/gx06-graphics-corpus.md`; commits `b1ae8de`(fixtures)+`82591ad`(gate)+`f6d47b2`(GX-05 residual)+pending(docs)).
  The capstone: GX-00→05 proved the capture COMPLETE, GX-06 proves the record→REPLAY path for every recorded opcode is
  itself correct + regression-guarded. Coverage unit = the container OPCODE (`orv3.OPNAME`, tied to the census recorded
  method, drift-guarded both ways), two axes: a FIXTURE (synthetic controlled capture → bit-exact replay) + a REAL PROOF
  (real cached scenario containing it → v3verify bit-exact). **Sweep (0/134 cached containers): DrawPrimitive/
  DrawIndexedPrimitiveUP/CopyRects UNOBSERVED** ⇒ fixture-only, honest (engine draws via DrawIndexedPrimitive+
  DrawPrimitiveUP; pause backdrop is a SetRenderTarget re-render, NOT a CopyRects screen-capture). Corpus: 2 new fixtures
  (`gx06_sink`=all 22 non-RT opcodes, lit+textured+transformed; `gx06_rt`=RES_RT_TEX/SetRenderTarget/CopyRects+4 SURFREF
  kinds via render-to-tex→composite→CopyRects — both 0-diff) + gx04/05 (VB mutation) + 3 real proofs (title 2D 120/120,
  arrprobe HOUSE 3D 1500/1500, pause RT 240/240, all REPLAY_EXACT). Gate `tools/gx_corpus.py`: FAST (manifest+census
  coverage math+drift, no caches/replay ⇒ host-suite) + `--verify` (drive-capable re-parse/v3verify/fixture-run + re-STAMP
  so an attestation can't rot; VALIDATED e2e). **Verdict COMPLETE — 25 opcodes, 22 observed proven, 4 SURFREF kinds;
  acceptance MET.** GX-05 residual CLOSED (diagnostic reader corruption-safety: `orv3.checked_reader` into orv3_xform/rt
  re-walks; root: they only walk `Container.load`-validated bytes so the corrupt-input path was already closed —
  +defense-in-depth for a desync; orv3_state isn't a container reader). New: `orv3.Container.opcode_counts/surfref_counts`,
  `docs/parity-graphics-corpus.json`, `tools/parity/gx_corpus.py`, +6-check gate test +2 fixture tests +test_orv3.
  **★ NEXT:** GX-02 (implement a census-observed missing method — NONE currently; reactive, lands when a new scene
  surfaces one) or a new parity front beyond the GX arc.
  **✅ EP-07 LANDED 2026-07-17 — human-review bridge (additive, non-hashed, verdict-preserving)**
  (`findings/parity-EP07-human-review.md`; commit pending). Unblocks the f29f553 handoff. R3 decision:
  **`human_review` → `canonical.NON_HASHED`** (not the envelope) — frozen schema SHAPE unchanged (stays a
  required first-class top-level field), change confined to the canonicalization rule §4.4 places under R3,
  code now CONFORMS to §4.4 ("proof_id excludes human display notes"); NOT a schema major bump.
  `prove.py:attach_human_review` (asserts review-neutrality under the CURRENT rule — robust to a stale stored
  id; a confirming review over a non-PASS gate → `confirmed-despite-<MACHINE>` + `machine_verdict` stamp, NEVER
  a silent pass; `gate()`/exit stays machine-driven) + `tools/parity_review.py` CLI (writes back into the SAME
  content-addressed bundle; `required_pillars` auto-resolve from the bundle's scenario contract w/ a drift
  check; exit = the MACHINE gate). VERIFIED e2e on the REAL `house-firstcust-arrprobe` bundle →
  `confirmed-despite-FAIL` (its honest sub-perceptual pixel FAIL) — the human-1:1-yet-not-tool-proven scene can
  now carry a scoped, auditable attestation that can't flip the FAIL. +host tests (test_parity_prove 72/0 incl.
  a stale-id regression; test_parity_schema NON_HASHED+neutrality). NB pre-EP07 bundles' proof_ids re-address on
  next drive (advisory; durable key `contract_sha256`; no bundle carried a real review). DEFERRED (opt-in):
  confirmed-parity-ledger→structured-records migration.
  Residuals (logged, non-blocking): arrprobe is a PRE-EP08 window ⇒ pixel/render provenance is CAVEATED not
  verified (auto-resolves on the next `orv3_window … --view`; the producer's `source` already matches by
  construction, VERIFIED `orv3_view._sha256_file == parity.sha256_file`); the proof `tools` group is still
  current-on-disk — thread `v3meta.prov` into `gather_provenance`; HOLE-3 (env attested) + HOLE-4 (exceptions
  not gate-enforced) stay by-design. Tooling:
  `tools/parity_prove.py`, `tools/parity_pixels.py` (pixels producer CLI), `tools/parity/`
  (incl. `pixel_producer.py`), `tools/trace_studio_v3/v3cache.py`, schemas
  `docs/schemas/parity-{contract,proof}-v1.schema.json`, vocab `docs/reference/parity-vocabulary.md`,
  proof index `docs/parity-proof-index.json`. Serialize retail drives (singleton).
- **▶ ACTIVE ARC (2026-07-10) — LIVE-PROBE HARNESS built + customer-behavior grounding → openrecet plays a
  real autonomous day-2.** New this session: the **`openrecet` live-probe MCP** (drive live retail via Frida:
  faithful button-mask input, memory read/poke, engine-thread `call_function`, screenshots, anchor stream,
  teleport/set_facing/set_gold cheats, move_to/waypoint nav, no-focus preview window w/ human input
  locked-by-default), in `.mcp.json`. How-to
  `docs/live-probe-harness.md`; reusable **recipes** `docs/findings/game-recipes.md` (R1 title→shop, R3
  day1→day2 flag-cascade); day1-2 RE `docs/findings/live-playthrough-day1-2.md`. Reached day-2 live (via a
  faithful iv1_5→iv2_3 tutorial-flag cascade through the REAL dispatcher FUN_0044bd0d) — **NB not economy-clean:
  +100g artifact + the day-2 morning bedroom cutscene was likely a flag-skip artifact, not the genuine opening;
  for accuracy work drive a real sale, don't poke f402.**
  - **Haggle deep-dive (findings `customer-service-haggle-RE.md` §22 + the full model-map):** the decision math
    (haggle_decide ±0.5%/±5% bands, per-round offer evolution, budget ceiling, all 16 kyaku fields) is ALREADY
    disasm-exact + host-tested + ported — **NOT a probe target.** **CRACKED + live-validated (poke the news
    list, re-call): `FUN_004361b2` = the daily-NEWS price-trend classifier** — `DAT_0450ad68` 20-entry news
    list (stride 0xc `{target_id:-8,news_id:-4,trend_char:0}`) → `{-2,-1,0,1}` → `haggle_round0_tilt`
    ×2.0/×0.45/×0.35; returns 0 during the tutorial sell (f404 set). **Scope correction (verified 2 ways):
    NO closeness/affinity/atmosphere feeds the haggle DECISION (お得意様度 loyalty parsed-then-DISCARDED,
    apply_dislikes_noop); if such a mechanic exists it's in the UNPORTED roster-scan (customer spawn) — a
    live-probe target, not settled.**
  - **★ NEXT-SESSION TARGETS (revised, ranked):**
    1. **✅✅✅ Roster-scan VERIFIED 1:1 2026-07-10 (commit `f4e52e2`; PORT-DEBT(cs-roster-scan) RETIRED).** The
       general customer eligibility/spawn (WHO walks in + WHAT item) — THE blocker for an autonomous day — is
       now BIT-EXACT vs retail: the golden-replay gate confirmed count/eligible/queue/cand_score AND `final_seed`
       (the whole RNG stream) match across 6 day-2 seeds. **3 bugs found+fixed via the gate:** centroid never
       computed (stale (0,0) → wrong bands), + BOTH the KYAKU_ATTR_TAGS and ITEM_ATTR_TAGS 16-tag SJIS tables
       scrambled at bits 0,2,3,4 (bit0 武器 0x95BE vs 0x9590 — corrupted every attr_mask game-wide; oder was
       correct). **★ The unlock: a seed-capture Frida hook** (`seed_at_call` — reads DAT_006023a0 on the engine
       thread the instant before a callq, defeating the live-game rng drift that made the poke+callq golden
       non-deterministic) + the FRESH-CENTROID capture protocol (callq FUN_0048439a before the scan). Harness
       `src/roster_golden_replay.{c,h}` (OPENRECET_ROSTER_GOLDEN env). Full story: `findings/roster-scan-RE.md`
       §ROSTER-SCAN-VERIFIED. Follow-ups still open below (news-list population; the serve-time closeness
       incrementer). **↓ history:** **M1 DONE — `src/customer_roster.{c,h}` + 8 host tests (3402/0):** the pure,
       objdump-exact building blocks — `roster_customer_weight` (e55c, incl. the hidden per-tier f32 MULTIPLIER
       DAT_005c6bd0 {6/7,2/3,3/7} Ghidra dropped as a bogus __ftol), `roster_dist_band` (a68f = the FULL band
       classifier Ghidra folded into callers), `roster_compute_centroid` (0048439a = the DAT_0438b4b8/bc
       attribute centroid, was PORT-DEBT A3 UNPORTED; 4 deco coord tables baked from rodata), `roster_shuffle`
       (e505, n-draw) + save_bank consts (closeness 0xb484, news-list 0x9d74, news-latch 0xb778, sched 0xa97e,
       deco 0xb379-c). **News-def confirmed 1:1 with g_news** (no new news loader). **★ CLOSENESS/DECORATION
       ANSWERED** (as before): decoration→WHO via e55c weight, closeness→budget/quality. **✅ GOLDEN REFERENCE**
       (`tools/roster_scan_capture.py` + `findings/data/roster-golden-day1.json`): 6-seed retail sweep w/ rng-draw
       counts 134-176 (the data-dependent-rng gate). **M2 PROGRESS 2026-07-10:**
       - **(a) DE-SCOPED — the "item/request pool `DAT_06a5dbd8`" is NOT a missing table; it IS `g_oder`**
         (oder.txt, ALREADY PORTED as tables_oder.c). Proof: oder base DAT_06a5db98 stride 0x4c=0x13dw, count
         DAT_06a5d448; dbd8=+0x40=attr_mask, dbdc=+0x44=attr_index, [+0x48]=level_minus_1(=quality-tier idx).
         Item catalog = `g_item` (stride 0xb3dw; attr_mask/category/price/id + FUN_004681f6=find_slot_by_id) —
         also fully ported. So the scan's table deps ARE available. Finding roster-scan-RE.md §OBJDUMP-CORR.
       - **(b) LANDED — e6e0/e80f/ed12 ported + 5 host tests (3407/0), objdump-exact.** customer_roster.c:
         `roster_event_state` (0..4 relic event), `roster_pick_item` (oder pick, 0/1 rng), `roster_range_gate`
         (row-0 quest gate + the index-mismatch quirk = engine-quirk #131). New save_bank consts SHOP_DAY/
         SHOP_RANK/EVENT_FLAG_BYTE/EVENT_DAY_BYTE.
       - **(c) M3 LANDED 2026-07-10 (commit `dbea6a5`) — the 740-line scan body ported (UNVERIFIED).**
         `cs_roster_scan(bank)` in customer_service.c ports all.c 57474-58212 in full (news block → candidate
         build kyaku 2..49 + 13-17 triple-spread → jitter#1 100 draws + shuffle → sched-appointment inject →
         rejection-sample → tier select → shuffle eligible+pool → extra count → news-featured inject → queue
         fill ×3 → perm shuffle + roster build), on the M1/M2 helpers. + the buysell-debug else branch. Build
         clean, host 3407/0. Gotchas baked: a68f called `(attr_y,attr_x)` axis-swap; Ghidra float subnormals =
         int bit-patterns (band 25/40/55/100); news-branch eligible/pool bound 20; `e80f(param1=kyaku,
         param2=closeness_idx)`. RNG accounting lands in the golden 134-176 band. Finding roster-scan-RE.md §M3
         has the full phase/rng dataflow map. **★ UNVERIFIED — bit-exact RNG NOT confirmed; PORT-DEBT(cs-roster-scan)
         STAYS.**
       - **(a') M2a' LANDED 2026-07-10 (commit `89f4a19`) — oder.attr_index category resolution wired.**
         `tables_parse_oder_resolved(...,resolve_via_item_category,&g_item)` (item.txt loads first); category-named
         (mask==0) oders now get attr_index (load-bearing: roster_pick_item's category-match consumes 1 rng iff
         ≥1 oder matches). + host test. Build clean, host 3408/0.
       - **(d) GATE RUN 2026-07-10 (commit `9d1e5d4`) — built the port-side replay harness, fixed 2 real bugs,
         hit a golden-determinism wall.** Built `src/roster_golden_replay.{c,h}` (headless: runs cs_roster_scan
         on a captured arena `.bin` at boot behind `OPENRECET_ROSTER_GOLDEN` env, dumps diffable JSON) +
         live-drove a day-2 shop-open golden. **Found+fixed: (1) `roster_compute_centroid` was NEVER called
         (stale (0,0) centroid → wrong bands); (2) `KYAKU_ATTR_TAGS` bits 0,2,3,4 scrambled (bit0 武器 0x95BE
         vs 0x9590; verified vs engine 0x5fd7fc) → corrupted EVERY customer's like_attr_mask game-wide.** The
         host test carried the same typo → fixed. **Deterministic scan output now BIT-EXACT vs retail** (cand
         kyaku/flag/flag-0 scores); port RNG bit-aligned to the LCG (verified vs Python sim, 124 draws).
         host 3408/0. **★ BLOCKER: the poke+callq golden is NON-DETERMINISTIC** — the live game ticks the RNG
         between the seed poke and the callq, so retail's scan-start seed drifts run-to-run ⇒ the rng-dependent
         fields aren't a reproducible oracle (pause 0x0438b150 doesn't stop it). **NEXT (rng-order gate, the
         only thing left): a Frida hook on FUN_0045edaa entry to capture the TRUE scan-start seed (add
         Interceptor.onEnter — daemon has no hook cmd yet), then diff the port at that exact seed; OR the
         `--target both` turbo trace (frame-deterministic).** Finding roster-scan-RE.md §M3-GATE. arena.bin +
         day2 golden are gitignored/local (regenerable). PORT-DEBT(cs-roster-scan) STAYS.
       - **(e) DRIFT-SEARCH GATE 2026-07-10 (commit `9ffcff0`) — worked around the golden non-determinism +
         found a REAL rng-order bug.** Retail's scan-start seed is `LCG^D(before)` (small drift D); harness
         now file-seeds + dumps `final_seed`. Swept D=0..2500 for a day-2 run (total before→after=176 ⇒ D≤56):
         **NO seed reproduces retail's final_seed / eligible+queue+count / rng_draws==176−D** ⇒ the port's
         RNG-consumption ORDER diverges (the deterministic candidate-build/weights/tier/eligible are still
         verified; the bug is in the rng-tail: rejection/pool-shuffle/news-inject/queue-fill/perm — likely the
         news-inject per-cust draws or an extra/admit off-by-one changing the e80f pick count). **NEXT: Frida
         hook on FUN_0045edaa entry → exact scan-start seed → bisect the divergent phase's draw count.**
       ~~Open lead: the serve-time `DAT_045109a8` closeness incrementer~~ **RESOLVED 2026-07-10 — see target 3.**
    2. **✅✅✅ News-list population VERIFIED 1:1 2026-07-10 — the daily-news subsystem is PORTED + the
       live golden gate PASSES: 18/18 samples (3 arena variants × 6 seeds) BIT-EXACT vs retail
       FUN_00436623 on every field incl. `final_seed`** (harness `src/news_golden_replay.{c,h}` +
       `tools/news_gen_capture.py`; ★ methodology unlock: the agent callq now returns an ATOMIC
       `seed_at_call`/`seed_after_call` window — a client-side final-seed read races the resumed sim;
       finding §VERIFIED).  (finding
       `news-daily-RE.md`; PROGRESS 2026-07-10).  Writer = `FUN_00436623` (daily generator: random pick +
       dedup, player-driven boom off the sold-pairs, expiry headlines, day-range news, ticker offsets) +
       picker `FUN_004363c6`, ported objdump-exact as `src/news_daily.c` (+16 host tests); the news-def
       table was already `tables_news.c` (fixed its swapped field names: +0x94/98 = LIFETIME, +0xac/b0 =
       target-item PRICE window).  All 3 call sites wired (leave-restore rng%3 / morning beat + f488 +
       npc_schedule_apply(0) / b92c pump + SE 0x2bd), ALL gated day>8 ⇒ rng-neutral on existing traces
       (VERIFIED vs the pre-change arrprobe cache: rngcalls 1723/1723 bit-identical).  **The
       FUN_004361b2 trend classifier is LIVE** (retired PORT-DEBT(cs-price-trend)): haggle round-0 tilt +
       price-panel High/Base/Low tint + merchant-HUD name colour.  **Remaining debts:**
       PORT-DEBT(news-ticker-render) (FUN_00436f97 newspaper/ticker DRAW consuming b92c + the headline
       buffers — also owns the ≤2f ticker-arm skew note), PORT-DEBT(news-clock-advance) (the unported
       timed-shoptime mechanic's news site, all.c:86733), PORT-DEBT(day9-morning-arm) (f488's b924==0x276
       day-9 morning cutscene consumer).  Day-9+ behavior needs a live probe/trace to verify end-to-end
       (no pre-day-9 save yet).
    3. **✅✅ CLOSENESS serve-time deltas PORTED + LIVE-VERIFIED 2026-07-10 (retires
       PORT-DEBT(cs-shop-stock); finding haggle-RE §23).** The "per-item sold-streak" framing was WRONG:
       `DAT_045109a8[b570]` is CANDIDATE-indexed closeness (lo ×10) + latched loyalty LEVEL 0..8 (hi
       DAT_045109aa); `d564` = the +0xc field of the STRIDE-16 candidate records (gotcha #20 — `(&DAT)[i*4]`
       int-base = 16i).  cs_live_machine (FUN_004658ab) decision now 1:1: round-3 −1, reject −1, accept
       +5/+2/+1 by the FUN_00460672 grade, clamp ≥0; FUN_00460e50 = the loyalty LEVEL-UP latch (b53c flash,
       suppressed on spread copies); FUN_00460f16 = pushback variant AND PATIENCE (level 0→2 rounds, 1-4→3,
       ≥5→4, f404→3 — loyal customers haggle longer).  VERIFIED: 8/8 live callq goldens bit-exact
       (rng-neutral), +6 host tests (3430/0), arrprobe rng stream unchanged (1723/1723 rngcalls).
       **Left in this area:** the b53c rank-up flash RENDERER; the other kind machines' closeness blocks +
       FUN_00460eba reject wanted-list + budget FUN_0045ecc0/00461011 (→ PORT-DEBT(cs-other-kinds));
       accept/reject itself IS raw `offer>=ask` in retail — no hidden accept-eval decision existed.
    4. **★ NEXT — `PORT-DEBT(cs-kind-select-general)`: the general SELL customer's display-grid item
       pick (FUN_00461303), THE remaining blocker for an autonomous live sale.**  Full RE map baked:
       haggle-RE **§24** (pass structure request/normal, row/col shuffle rng order, want/afford
       gates, the Ghidra-dropped front-counter budget multiplier @0x4616b4 → objdump).  Deps to port
       first: budget FUN_0045ecc0 + FUN_00461011 (closeness-indexed, §23 array), request chain
       FUN_00468ddc (ALSO retires PORT-DEBT(cs-news-suggest)), kyaku like-blob fields (+0x51ac/
       +0x5158/+0x51a8 vs g_kyaku).  Verify = the §23/roster golden pattern (poke grid+closeness+
       queue, atomic seed-window callq vs a port replay harness, seed-sweep the rng gates).
  - **Tooling ready:** `tools/haggle_probe.py` (live haggle monitor/poker — reads b590/b574/b584/b588/ask/base/
    b5a8/…), `tools/openrecet_mcp.py`, `tools/probe_daemon.py`, `tools/probe.py`. **Get into a live haggle:**
    either stock+open (walk—not teleport—onto a stand to arm cbfc via FUN_0048619f, `press a` place, **C** open,
    customer approaches) OR force it (findings §5: cc08=4, b51c=0, b5a8=2, b56c, b5a4=id<<6, b534=6/0xf, b1cc=1).
    A daemon may be left running (detached) on a flag-hacked day-2 — `probe.py quit` + relaunch clean, or MCP
    `launch` (it detects a live daemon). Valid stock item: catalog row 4 = 200pix (handle id<<6); tutorial
    bread handle 0x3ea00, sword 0xc0.
- **▶ PRIOR ARC — DAY2 day-transition RENDER gaps (the 5 the DAY2 pixel-confirm surfaced; finding
  `cutscene-replay-anchor-drift.md` §2026-07-03-later).** The Residual-B beat is Δ0 but the anchor-match
  MASKED unported day-1→day-2 RENDER. Progress:
  - **✅ #2 HUD day-counter** (`c63ee20`): live-read `g_hud_day` from `working[CARD_DAY]` each INGAME
    frame (was cached at load). Port reads "Day 2" @f15820.
  - **✅ #1 "Day 2" CARD** (`a77c46b`): ported `scene1_day_card_render` (FUN_0040a765:7500 / 0x40c209),
    driven by the b928/b924 beat — black backdrop + centred "Day %d" + white exit-fade. **VERIFIED
    `--target both` BIT-EXACT (0px)** at the opaque hold + mid-exit. RE correction: it's NOT the bf74
    blackout+glyphs; the card's own backdrop blacks the screen (bf74 arm not needed). **✅ b924 fade-seam
    residual CLOSED (commit PENDING):** the scoped `{calltrace}` probe showed **port b924 == retail
    DAT_0438b924 FRAME-FOR-FRAME (155/155, MAX|Δ|=0)** incl. the fade-in (b924 0→17) — the "counts ahead"
    hypothesis was WRONG (pre-#4 pairing artifact); with the bit-identical alpha formula the card is bit-exact
    over the whole fade. Tooling: b924/b928 now emitted continuously at always-ON 0x4536cb (0x48670f is dark
    on the pose-held beat) + retail_fields mirror. Finding §2026-07-04-b924.
  - **✅ #4 actor re-placement FIXED + PIXEL-CONFIRMED (commit PENDING).** The prior "RENDER BLOCKER" was
    REFUTED (flawed first-cut, NOT a render-source gap): body sprites render position DIRECTLY from
    `g_scene1_actor_pos[i]` (`scene1_shop_walker.c:779`, no snapshot) and `g_scene1_player_pos ==
    g_scene1_actor_pos[0]`, so a sim re-place IS the render source. Cached-trace ground truth: the ONLY day2-beat
    divergence was positions (port px 0.796/cx -0.694 SWAPPED vs retail -0.30/0.6; cc08/panim/canim already
    matched). FIX = `scene1_postload_day2_actor_replace()` (positions-only, RNG-neutral) re-seats the actors at
    the house-standing pose, armed at the iv2_5 beat (`g_day2_replace_pending`) + consumed at the
    `scene1_ingame_default_arm_tick` TOP → fires @15470 (retail's day2), pose driver re-derives facing (6/2).
    VERIFIED `215022Z`: px -0.30/poct 6, cx 0.6/coct 2, RNG 0-diff, host 3394/0; PIXEL-CONFIRMED (caprange
    15799-15838 vs retail PNGs, feed montage): before=swapped → after=matches retail. Finding §2026-07-04.
  - **✅ (4b) companion cx EASE FIXED + BIT-EXACT (commit PENDING).** Retail eases cx 0.6→1.0 over the beat via
    `FUN_0048a833`'s ELSE-branch (±1.3 X offset on the player's side, factor 0.1, no CO_THRESHOLD/vel-clamp —
    all.c:89434-73); the port modeled only the free-roam branch so cx held 0.6. Ported as a beat-gated branch in
    `scene1_companion_ctrl_tick` (CO_INTRO_SPRING 0.1, target player_x±1.3). VERIFIED **MAX|Δcx|=0.0 over the
    full 190f beat** (settles 1.0), RNG 0-diff, host 3394/0.
  - **OPEN residual post-#4:** **(4c)** @15838 retail dialogue PORTRAIT the port lacks (~40f ahead of the port's
    box-open @15878; PRE-EXISTING/RNG-neutral — day2-dialogue timing or portrait-slide-in gap, distinct arc).
  - **✅ #3 Now-Loading disc PORTED + PIXEL-CONFIRMED (commit PENDING; engine-quirk §129).** Retail's nowloading
    has TWO gates: `DAT_06a49958` (scene load) + `DAT_06a49960` (DIALOGUE load, armed FUN_00452d07 = iv2_6's
    `.ivt`); `FUN_00453147` draws the disc iff EITHER. A faithful DAY2-load frame = **live house scene + "Now
    Loading" disc + NO camera hint** (brightness-confirmed retail 104 across 15655→15775). Three-part fix,
    render-only (no sim/rng): (a) sim.c arms `g_active` on `scene1_intro_dialogue_loading()` (disc); (b) main.c
    renders the scene when `!nowloading || dialogue_loading` — L51100 skips the scene only for the PRIMARY gate,
    so the port's OR-collapse would've blacked it; (c) scene1_top_hud gates the camera hint on `_busy()` not
    `_active()` (retail hides "Button 4" during the load, shows only the disc). VERIFIED `--target both` @15700:
    port==retail modulo a 2-3f load-bracket skew (disc-spinner/anim phase — pillar-B CreateThread race, not
    logic). host 3394/0. **Pending: user viewer/feed confirm (montage pushed).**
  - **OPEN: #5 wing-sparkle (minor).**
  - Verify path: scenario-test caprange `--target both` (orv3 is occurrence-blocked on this drop-fragile
    trace — a separate occurrence-aware-windowing tooling arc, deferred).
- **✅✅ 2026-07-03 — cutscene-replay DEADLOCK fixed + PARITY-validated (commit `b82d2df`; finding
  `cutscene-replay-anchor-drift.md`).** The `house-firstcust-cutscene-day2` prologue (Tear meets Recette →
  debt story → shop setup) FROZE the replay ~14000f (both port+retail) — the anchor-segmented trace
  deadlocked at `{wait: EXTRA_SPRITE_FADED_IN}` because cosmetic/FX anchors (blinks, sprite fades, text-anim,
  collapse-prone LOADING) DRIFT in relative order between the frame-dropped real-time recording and the
  deterministic turbo replay (the fade fires early → the wait needs a future firing that's already past).
  Fix: dropped 586 fragile intra-cutscene `{wait}`s (line 465+, confirmed first-customer region untouched),
  keeping only reliable boundaries (CONV_POSE_START/END). Deterministic auto-play + held-X carry between them.
  **VALIDATED: replays end-to-end (line_row 0→157), port vs retail line_row seq BYTE-EQUAL (85 transitions),
  RNG BIT-EXACT through the cutscene, join 13863 paired/0 port-only gaps.** New: `seg` call-trace probe
  (the trace segment the harness is parked on — cracks replay stalls in one drive).
  **✅ 2026-07-03 (c) DONE — folded drop-fragile into `distill_trace.py`** (commit `59c4124`):
  `FRAGILE_ANCHORS` + `--drop-fragile-after FRAME`/`--drop-fragile-region LO:HI` for `--anchor-segments`
  (drop drift-prone cosmetic/FX syncs in auto-play regions, keep reliable boundaries; loss-free). Retires
  `PORT-DEBT(distill-drop-fragile)`. +`tools/test_distill_trace.py`. **★ RE-DISTILL GOTCHA found:** a naked
  re-distill STALLS at the post-first-customer PAUSE region (~raw 2994) — NOT the drop; it's missing the
  hand-added load pins (`csloadpin:24`/`primaryloadpin:16`/`tutloadpin:8`/`bgnpcseed` head + rng-anchored
  `bgnpcpin`) which are NOT in the raw. New `--carry-pins-from TRACE` copies them (re-anchors mid pins by
  segment rng). **Any re-distill of a pinned trace MUST pass `--carry-pins-from <old trace>`.** Principled
  auto-play boundary for this trace = raw **3468** (last interactive tap; 2195 is too early → still
  interactive → stalls). Finding has the full story.
  **✅ 2026-07-03 (a)+(b) PORT-SIDE DONE — candidate scenario `house-firstcust-cutscene-day2-full`**
  (commits `43b57de`+`86d4a0d`+`ef28140`; `--drop-fragile-after 3468 --carry-pins-from <committed>`, full
  16291 range). **Port drive replays END-TO-END exit=0 in ~100s: all 855 anchors, past the pause region
  (where a pinless re-distill stalls), through the fast-forward/original-deadlock zone, reaches the
  SIGN-HAMMER (CONV_POSE_END@15390), then the trailing-hold plays the DAY 2 brooming tail (901 frames, +~6s).**
  Three distiller fixes landed en route: caprange must NOT carry (it dumped a 33GB BMP frames/ dir → disk
  hazard); the anchor-segment TRAILING HOLD (post-last-anchor idle like DAY2 was silently trimmed); pins
  carried correctly. **★★ 2026-07-03 — ≥2-drive retail verification DONE. VERDICT: the retail replay is
  LOAD-FLAKY on this trace (3/3 drives STALL to max_frames, at a WANDERING load-phase-dependent point) —
  a pillar-B load-determinism gap, NOT a port gap** (finding `cutscene-replay-anchor-drift.md` §2026-07-03).
  drive-1 (`--target both`): stalled @ raw≈837, the FIRST wrap-up skip — `TEXT_ANIM_START@837` reordered
  BEFORE the 2nd `{wait CONV_POSE_BLINK}` ⇒ harness parked ⇒ box armed (driver on) but the confirming X
  never injected ⇒ 1396 free-running blinks. drive-2 (`--target retail`): stalled @ `PAUSE_CLOSE@1600` /
  rng 2246047975 = the FRONT's ORIGINAL point (harness parked on the next `{wait PAUSE_OPEN}`; menu-nav
  taps missed retail's drifted cadence). **Port is deterministic (855/855 both drives).** Root: retail's
  completion-based (`CreateThread`-race) loads drift the frame cadence run-to-run; the INTERACTIVE region
  (<3468) keeps fragile syncs (`CONV_POSE_BLINK`/`TEXT_ANIM_*`/`PAUSE_OPEN`) whose order/timing flips ⇒
  the anchor-segment harness parks. `--drop-fragile` only fixes AUTO-PLAY regions (>3468). The committed
  sibling's past `--target both` pass was load-phase LUCK (cf. §21.6). **✅✅✅ 2026-07-03 — FIXED via path
  (B) [user-chosen]: the drifting brackets were ALREADY pinned but the extend-only min-gate VALUES were
  too small to bind on retail** (agent.log: `csloadpin: real load >= pin ... left alone`). Retail cs
  loads are 43-62 (pin was 24), tut cutscene loads 28-97 (pin was 8). **Raised `{csloadpin}` 24→72 +
  `{tutloadpin}` 8→36** ⇒ the gate now binds on BOTH sides ⇒ port==retail cadence. Result (drive
  `…123809Z`): **port still COMPLETES at the raised cadence (order preserved), retail COMPLETES the whole
  trace for the first time** (all 4 prior drives stalled) — reaches CONV_POSE_END@16101 (sign-hammer) +
  DAY2 trailing hold; non-blink anchor seq port↔retail first-505 + last-14 IDENTICAL. RNG-safe (every
  LOADING_END is `{rngseed}`-pinned ⇒ offer structurally unchanged; both sides now share the SAME
  cadence). The retail v3 replay is 16291/16291 BIT-EXACT; anchor path port↔retail matches first-505 +
  DAY2-tail. **✅✅✅ 2026-07-03 — segment-scoped `{tutloadpin}` LANDED + VERIFIED (commit `3669cbe`;
  finding `cutscene-replay-anchor-drift.md` §NEXT-ARC-LANDED).** Made `{tutloadpin}` per-segment (applied
  at segment ENTRY via `rearm_tutloadpins`, sticky; Frida-agent mirror in `segtraceOnSegmentEnter`); head
  36 + `{tutloadpin:110}` before the bracket-7 LOADING_START (seg 26). **`--target both` (`142827Z`): both
  COMPLETE exit=0 (retail no stall); brackets 7-12 ALL `released`@110 (0 "left alone"); frame-delta Δ0 for
  frames 206→15348 = the WHOLE cutscene arc (raw 0→15390) is now BIT-FRAME-ALIGNED** (was: retail +1056f
  adrift, DAY2 join 232/1091). **RETAIL DETERMINISTIC ≥2 drives** (`142827Z`≡`144555Z` bit-identical
  anchors+brackets ⇒ reproducible, not single-drive luck). +host tests (3395).
  **✅✅✅ 2026-07-03 — RESIDUAL B FIXED (commit `d064cf0`; finding §RESIDUAL-B-FIXED). The DAY2 pixel confirm is
  UNBLOCKED (Δ0 tail) — pending the user's viewer pass.** ROOT (confirmed by scoped `{calltrace}` `--target
  both` + b924/b928 retail reads, drives `160204Z`+`161316Z`): NOT a dialogue-length gap — retail withholds
  iv2_6 (the DAY2 load) for a **~190-frame "Recette looks up at Tear" idle BEAT** after iv2_5. iv2_5 arms
  `DAT_0438b928=1`/`DAT_0438b924=0` (all.c:45798-99); the master tick counts b924 every free-roam frame
  (86801); `FUN_0044bd0d` gates the cascade behind `b924 < 0xbe(190)` (45489 → `if(!bVar1) return` @45507);
  both iv2_5 dialogues END bit-identical @15469, then retail free-roams 189f (b924 0→189, `f470=0`/no
  scene-out) before iv2_6@15659. **Ported the beat** in `scene1_tutorial_dispatch` (arm/count/gate
  IV2_BEAT_FRAMES=0xbe); pose held across it via the conversation-pose gate + the player walk-arm
  suppression (`scene1_player_ctrl.c:2453`, else the freeroam arm resets Recette's anim → the pcnt-stuck
  symptom). VERIFIED `--target both`: DAY2 `LOADING_START` **15659 both** (was port 15470), DAY2-tail anchors
  **Δ0 (50/51** — blinks 15494/15558/15622 + day2 lines/sprites all match); host 3394/0.
  **★ TWO smaller residuals remain at the DAY2 boundary (both pose-flag PORT-DEBT class, distinct arcs):**
  - **(A, @15470 CLOSED / @14193 OPEN) port-only `CONV_POSE_END`+`CONV_POSE_START` blip.** The @15470 pair
    is GONE (the beat holds the pose). The @14193 pair (iv2_5's OWN load bracket) REMAINS: the pose-flag
    PORT-DEBT (`scene1_conversation_pose.c:104-108` derives DAT_0450f470 from the dialogue lifecycle; the
    derived flag blips at EVERY tut-load boundary, retail's REAL flag blips at SOME (11726 — aligned) not
    others (14193)). Fix = port the faithful producer FUN_00470a46/FUN_004852fb (retires the pose-flag
    PORT-DEBT). Also a NEW tiny tail residual: the FINAL day2 `CONV_POSE_END` is Δ−9 (16380 vs 16389; the
    day2 dialogue itself is Δ0 through DLG_LINE_CLEAR@16332) — same class, low-priority.
  - **orv3 DAY2 viewer BLOCKED (tooling):** `orv3_window` needs a `{caprange}` full-extent, but the
    re-distill DROPPED it (33GB-BMP hazard). Re-add a SCOPED caprange (DAY2 window only, ~1091f ≈ 5GB
    both sides) or teach orv3 to inject one for `--window`; keep the committed trace caprange-free.
  - flow_diff rng-verdict structurally guaranteed in the Δ0 region (rng force-pinned at every LOADING_END
    + frame-exact ⇒ matched consumption); explicit verdict deferred (needs a scoped `{calltrace}`).
  Watch the known DAY-2 blink-stall lead (~frame 21259+ — did NOT manifest port-side here). GOTCHAS:
  `--bless` with a carried caprange dumps ~2.3MB/frame BMPs (33GB) — keep caprange out / tiny; `--call-trace`
  WITHOUT a `{calltrace}` window dumps the FULL call graph (~1.4GB/run) — always scope it.
- **✅ 2026-07-02 — day-end cutscene: served customer DESPAWN ported, PIXEL-1:1 (viewer notes #24/#25; RE §21.33).**
  The port drew a chibi customer still roaming the shop floor (and through Tear's hair) through the whole
  day-end CONV_POSE cutscene; retail has none.  Root cause: the cs-leave restore (FUN_00462403 @60337) calls
  **FUN_0046f892** (cs-NPC array reset) which the port DEFERRED in PORT-DEBT(cs-leave-restore) for
  rng-neutrality — but it's not RENDER-neutral, so the served customer never despawned.  Ported
  `scene1_customer_npc_reset()` into the leave block (retail order, after b7b0=0, before the §21.32
  shoptime++; rng-neutral — no LCG).  Verified: port re-drive **2887/2887 BIT-EXACT**; tex 747d/16d2 now
  1/7 == retail on EVERY cutscene frame (0 diffs), sale region untouched; notes #24/#25 diffs BLACK; raw rng
  bit-exact at both note frames.  Narrows PORT-DEBT(cs-leave-restore) to FUN_0048439a/473332/45e028/octant.
  Only OPEN item: the user's click to clear notes #24/#25 in the viewer (win-0-3000 re-gen'd, ready to scrub).
- **✅ 2026-07-02 — day-end DUSK tint + clock dial PORTED, PIXEL-1:1 (viewer notes #20-23; RE §21.32; commit `64ccb98`).**
  HOUSE `maplight:3` (time-of-day directional light) was stubbed to daytime row0 forever — the port stayed
  bright while retail warms to amber dusk over the day-end CONV_POSE cutscene.  Unstubbed the real 3-row
  interp (FUN_00458f67 L53746-93; objdump-verified the integer `cmp ecx,1/2/3` past the Ghidra denormal
  gotcha) + the per-frame clock-phase ease (FUN_004536cb tail → sim.c) + the scripted `shoptime++` at
  customer-leave (FUN_00462403 L60339, gated f404==0 — narrows PORT-DEBT(cs-leave-restore)).  Ground truth
  (added clock VAs to retail_fields.json 0x4536cb): shoptime **1→2 @ frame 2274**, clock_phase eases **1→2**
  over 200f — **port BIT-EXACT** vs retail.  Trace Studio re-rendered: **all 4 note diffs BLACK** (dusk +
  clock dial pixel-1:1).  +5 maplight host tests.  Only OPEN item: the user's click to clear notes #20-23
  in the viewer (win-0-3000 view re-gen'd, ready to scrub).
- **✅✅✅ 2026-07-02 — `house-firstcust-arrprobe` win-0-1500 USER-CONFIRMED "this whole trace is 1:1 now"**
  (ledger; RE §21.28-§21.30; commits `6f0993b`+`2537904`+`2038905`+`29ecc72`+`ae0f5ed`).  Closes the anim
  seed-origin arc + residuals (A) recette start-phase (pose_house_standing snapshot seed → fresh 0/0/0 reset)
  and (B) vase shadow (fade.c ALPHAREF(0x18) mistranscribed as ALPHATESTENABLE ⇒ alpha-test-off leak z-clipped
  the display-stand shadow decal) + note #8 choice-box flash (§21.27).  Full stories in PROGRESS + the ledger.
  Residual frame diffs on the trace: 2-3 scattered 1-px sprite-edge speckles/frame — accepted.
  - (C, lower, OPEN) companion `coct/cx` tutorial-cs facing blips (@389, 48/33f — facing write-order, scoped in
    task #5); probe-only init leftovers `ask/base/b5b0` (retail 1000/1 vs port 0 pre-cs, cosmetic).
- **✅ 2026-07-02 — `house-firstcust-cutscene-day2` carries the FULL pin set** ({csloadpin:24} +
  {primaryloadpin:16} + {tutloadpin:8} + {bgnpcseed}; same savefile ⇒ same naturals; both sides auto-skip its
  own `{bgnpcpin}` inject per §21.25).  **VERIFIED `--target both`: raw rng bit-exact frames 225→1934 = the
  ENTIRE first-customer region** (arrprobe's confirmed span was 225→1722); anchor sequence identical, initial
  load 1505→223.  NB the whole-capture `flow_diff --verdict` shows bgx1..5 "DRIFT @81" + "rngcalls DESYNC @3"
  even on the USER-CONFIRMED-1:1 arrprobe capture — that signature is the accepted pre-pin/warmup region +
  probe print-precision (retail f64-prints, port %.9g), NOT a regression; judge by the aligned span.
- **★ OPEN leads queued on this trace:**
  - ~~v3 PORT replay hash-verify 5/2895 fail~~ CLOSED 2026-07-02 as NON-REPRODUCING: reproducible on
    re-verify of the OLD capture (replay-deterministic, material draw-diff ALIGNED ⇒ pixel-level), but the
    fresh re-driven capture verifies 2887/2887 BIT-EXACT; the failing capture was overwritten by the
    re-drive.  **WATCH: if hash-verify ever fails again, capture a `--raw-refs` window at a failing present
    BEFORE any re-drive** (re-drives destroy the evidence).
  - Pending explicit user re-confirm (minor): the note-#1 sell-counter "!" emote fix (2026-06-20,
    `house-customer-walk-probe`) was never separately re-confirmed.
  - Day2 viewer note #9 ("wing flap residual", TEXT_ANIM_START#1+8) renders BLACK on the fresh
    win-0-3000 capture (it predated the §21.28.1 wing fix) — clear it in the viewer when convenient.
- **★★ QUEUED ARC — CUSTOMER INTERACTIONS deep-dive (user directive 2026-06-22).**  Mechanics the user named
  (verify in code, don't trust the wiki): (1) CLOSENESS/affinity per customer; (2) ATMOSPHERE score from shop
  DECORATION.  Includes: **L1b** real accept side-effects (cs-live-sale-fx: gold += ask, stock decrement,
  catalog/inventory/payout FUN_00460d52/b3a/606fc/00083/0002a/00b93, all f404==0); the roster scan
  (cs-roster-scan); live-haggle render fidelity (customer art/dialogue); **the iv1_8 chain** (f406→f402
  post-first-customer EXTRA_SPRITE cutscene) → the cutscene series → day-2 brooming.  Separate follow-up:
  the DAY-2 cutscene blink-stall (~frame 21259+, does not affect the cc08 survey window).
  **2026-07-02 both day-end leads RESOLVED (RE §21.31):** (a) the "+261-rng day-end consumer" was the
  SALE-COMMIT coin shower (FUN_00460d52 → Table-A alloc entry 100 → 69-particle burst = +261 int/+207 float),
  NOT next-day regen — the f404==0 accept block (gold+=ask, SE 0x14d, FUN_00460d52 stats+fx+SE 0x17b/0x156)
  is now PORTED (customer_service.c `cs_sale_commit_stats_fx`); PORT-DEBT(cs-live-sale-fx) narrowed to
  FUN_00460b3a/4606fc/00083/00f59/0002a/00b93.  (b) there IS no separate "day-end load path" — the post-sale
  flow is the SCRIPTED story chain iv1_8→iv2_1→iv2_2→iv2_3(DAY ADVANCE: fb84++, fb88=0, f400=0…)→iv2_5→iv2_6
  (FUN_0044bd0d all.c:45726-45813), now ported into scene1_tutorial_dispatch.c riding the existing
  start_single load bracket (⇒ LOADING_START emits; the day2 trace's `{wait: LOADING_START}` releases).
  PORT-DEBT(blackout-tut-dispatch) still unwired on the iv2 entries; PORT-DEBT(tut-dispatch-iv2-fx) =
  iv2_5's FUN_004852fb + b928/b924 arm.
  **✅ VERIFIED (drive 074133Z, RE §21.31.2/.3):** burst 261@commit+1 == retail; **the whole SALE SEGMENT
  (PAUSE_CLOSE#3, 428f: commit → burst → money-roll → coin flight → 24/24 landings+shake) raw-rng 428/428
  BIT-EXACT**; landing SEs start at retail's aligned frame (1985==14897); gold count-up frame-exact
  (129==129); 94/94 anchors, day-end cadence frame-exact; day-end segments at the known +1 load seam.
  Landed en route: shape_mode=PARAM8 mistranscription fix (asm [esi+0x10]), template sets 1-3 load
  (TEMPLATE_COUNT 400), money-roll in INGAME, shake pulse+jitter (FUN_0040656e/406584), and **gotcha #19**
  (GCC x87 -O2 excess precision breaks MSVC float-equality — the PFO.4 terminal gate; bit-pattern compare).
  **✅ 2026-07-02 — coin-shower RENDER FIXED (RE §21.31.4, needs human viewer confirm):** TWO FUN_00452f58
  bugs — HUD camera eye/lookat SWAPPED (DAT_06a47120=(0,0,−550) IS the eye; port identity VIEW put the
  z≈−520 particle plane 520 units out = sub-px dots) + pre-matrix atan2 ARG ORDER (FUN_00503dd0(hyp,dy);
  DAT_0438cdf8 = IDENTITY under HUD state, NOT RotY(π/2) — that turned quads edge-on; PHC #16 can close).
  Burst frame coins now pixel-1:1 (diff shows only the known opens).  New tool: `orv3_draws.py --verts N`
  (UP vert decode + transforms + screen footprint).  New minor lead: tex b494 80tris retail-only EVERY
  frame, first draw, paints 0 px solo (strip warm-up? benign-invisible, unchased).
  **✅✅ 2026-07-02 — the WHOLE sale fanfare is now PIXEL-1:1 (RE §21.31.4-.6; needs the user's viewer
  confirm to clear notes #10-19):** coin shower (eye/lookat + pre-matrix fixes) + sold-item display clear
  (accept-block helpers FUN_00460b3a/4606fc/00083/00f59/0002a ported; PORT-DEBT(cs-news-suggest) =
  FUN_00460b93 only) + the popup TIMELINE (master-tick b5c0/b304 windows + merchant EXP 0xb0fd write —
  REQUIRED: its b5c0 clear opens the leave-dissolve gate) + the TOTAL-EXP popup ROWS (asm 0x467db5) +
  the merchant-XP bar animator (b91c ease + flash + level-up; FUN_00407ab4 pop =
  PORT-DEBT(merchant-levelup-pop), unexercised here).  Verified: raw rng 1856/1856 bit-exact; pixel
  sweep +89..+243 = 0 px on 6 frames, 1-px speckle on 2 (accepted residue).
  **Remaining fanfare residue:** ~~SE same-frame dedup~~ CLOSED §21.31.7 (FUN_00499519 = a request-FLAG,
  the FUN_0049966a pump plays once/frame — ported: audio_play_se_by_id flags, audio_se_flush at
  music_step_default head) · tex b494 80tris/1draw retail-only EVERY frame (invisible — paints 0 px;
  suspect a strip warm-up; unchased lead).
- **★ QUEUED — guild LEAVE transition (the user-confirmed next guild target).**  PORT-DEBT(guild-leave-transition,
  NOT yet code-tagged): the proper iv1_16 bread cutscene = the flag==1 (post-purchase) Leave path = the c2c/c28
  transition → iv1_16 fade + world-map swap + the return-to-Recettear Tear cutscene (+ the buy-commit
  `save[0x2bc5d]=1`).  Other guild debts (untagged): Sell (mode 3), first-buy tutorial/limit gates, Talk
  submenu / Fusion (FUN_00493616) / Expansion flows, the FUN_004922c0 daily-event probe + group-6 cutscenes,
  the mid-transition bg path, the variant-1 (ichiba, dest 1) set.  Also queued: town scenes off the world map.
- **★ QUEUED — menu-boundary residuals (item-display arc's worst frame).**  Menu-OPEN slide-in ramp phase in
  `DAT_0734b98c` (label 125, gt8≈29k; ASYMMETRIC — pin by instrumenting the port counter vs the untraced
  retail counter) · rngcalls ±31 at the boundary · companion cx/cz/canim/cframe + pcnt micro-DRIFT · retail
  draws the hand-cursor snap 1f EARLIER at menu open (label 587) · menu-close camera pan-out whole-frame
  offset (label 441).  Menu polish: description-panel line layout (price / "Number possessed" X) · slide-in
  check · row flash.  Detail: `findings/shop-display-menu-RE.md`.
- **★ DEFERRED (polish pass, user-OK):**
  - **LOADING-SCREEN FIDELITY (user direction 2026-06-11):** replicate retail's load fades/screens, don't
    fast-load/suppress.  First instance (not yet RE'd): ESC-skipping the guild first-visit cutscene plays a
    brief fade-to-black in retail (lead: the stubbed skip-teardown FUN_00473c03 restore); + the retail-only
    screen-blackout layer (quirk §122); + PORT-DEBT(blackout-tut-dispatch) wiring for guild/tutorial
    cutscenes.  Keep in mind on ANY transition / dialogue-skip teardown / `{caprange}` load seam.
  - faint ambient particle dots (user ref-crops 2026-06-05) · next-line "book" arrow blink phase (draw from
    per-script-reset `rt->blink`, add to `{phasepin}`) · per-line pose precision ~2px (prompt/markup glyphs;
    if real, cross-ref the 0x467664 right-align scale) · price-trend tints FUN_004361b2 (several sites,
    registry-tracked) · extend the RETAIL 0x48670f hook with b598/b58c for full state-panel parity ·
    pause-xp-anim (XP animator, only matters mid-rank-up; NOT code-tagged) · title-picker-overwrite (code-4/6
    new-game overwrite-dim + per-slot avail; NOT code-tagged; needs a new-game-into-slot trace).
  - **Title NEW GAME (the "hardest, last" intro/prologue thread):** retail intro-video force-skip D4 + the
    prologue mid-load actor-spawn gap (`findings/conversation-pose-driver.md`) — the last title item.
  - TODO: run the pinned RETAIL census (Frida host) + census other scenes (`findings/phase-state-census.md`).
  - Tooling caveats (revisit only if bitten): `merge_anchor_seq` column ordering is best-effort;
    `orv3_view.build_view` (legacy PNG-bake) is not threaded — add the `join_anchor` pass-through if ever
    needed for cc08.
- **Study toggles (filming tool, 2026-07-06 — READY for the lighting-video shoot):** SHIFT+1..6 (or
  `--study-off mod2x,keylight,ambient,fog,hikari,blob`) kill the six HOUSE lighting tricks individually,
  combos free; all default ON = retail, parity untouched. `src/study_toggles.{c,h}` +
  `findings/study-toggles.md` (hook map + measured verification; harness passthrough
  `scenario-test.py --exe-arg=…`).
- **Phase:** frame-by-frame 1:1 parity sweep along the player path (title →
  prologue → HOUSE → shop loop → world map → dungeon). Strategy + tooling roadmap:
  **`audits/2026-06-09-methodology-audit.md`** (settled verdicts — behavioral-vs-
  byte-exact CLOSED, x87 invariant, T1–T12 tooling roadmap, milestone-ladder KPI).
  Read it before re-litigating strategy or building new parity tooling.
- **SETTLED VERDICTS (do NOT re-litigate):** the WALL-CLOCK pin is REFUTED — do not build it (RE §21.2 +
  the 2026-06-28 time-source sweep: QPC feeds only frame-pacing; loads are completion-based; phase consumers
  are frame-based — a clock pin fixes only the cosmetic FPS counter + audio fade).  `{gsimpin}` was removed
  (`9e1db6f`) — do NOT re-add (it forced gsim 1 behind).  The bilateral `{rngseed}` works as designed — no
  target-scoping/foundational pin needed.  The accepted cc08 render residuals (character +1f arrival-origin
  phase, ~2px per-glyph text precision, honest load-region join gaps) are NOT logic gaps.
- **Authoritative parity facts:** `findings/confirmed-parity-ledger.md`.  A tooling "divergence" on a
  human-confirmed-1:1 item is a lead to investigate, NOT an assumed regression.
<!-- FRONT:END -->
