# Tear (companion) visual diffs vs retail — free-roam walk

User-inspected catalog of the remaining Tear divergences in HOUSE free-roam,
recorded 2026-06-03. These are the concrete chips behind the standing
[[project_next_char_controller]] "Tear not-1:1" + the deferred wing-flap phase.

## Exact reference trace (reproduce these diffs)

- **Scenario:** `house-walk-down-dense` run `--target both` (synced port↔retail,
  Recette's WALK phase is 1:1 — see `reference_parity_trace_walk_down_dense`).
- **Run dir:** `runs/scenarios/house-walk-down-dense-both-20260603T204609Z`,
  frame **`cap_03`** (port `openrecet/frames/`, retail `retail/frames/`, sorted
  index 3).
- **Feed comparison:** id `20260603T210245_3a18` — `[RETAIL | PORT | DIFF]` of
  Tear at IDENTICAL screen coords (crop box `(575,600,665,715)` of the 1024×768
  `cap_03` frames, upscaled 6×). Panels: RETAIL x≈10–550, PORT x≈560–1100,
  DIFF x≈1110–1650.
- **Caveat:** this scenario is **NOT RNG-pinned** (`scenario.yaml` is a synthetic
  A-spam boot + hold-DOWN; no `{rngseed}` op), so the wing-sparkle particles and
  foot-dust spawn at **different RNG phase** on the two sides — see diff #5. The
  *sprite/glow/anim* diffs below are NOT explained by RNG and are real.

## The differences (user inspection, cap_03)

1. **Blue glow ON HER FACE — present in retail, MISSING in port. ✅ RESOLVED
   2026-06-03 (commit pending).** It was **neither** an extra layer nor a
   blend/draw-order bug — it was a **per-pass projection (z_far) depth bias** that
   inverted the body↔glow depth so the additive wing-glow was Z-occluded behind
   her head. Ground truth from a SYNCED port↔retail d3d-trace at cap_03
   (`/tmp`-built `d3d_state_diff`-style matrix extract):
   - Both sides draw exactly **body (pc12, alpha, ZW1) + glow (pc14, additive
     ONE/ONE, ZW0)** at the same world pos, glow after body — NO missing pass.
   - The glow is `ZENABLE=1 ZWRITE=0 ZFUNC=LE`, so it is **depth-tested** against
     the body's Z-write. Whichever is nearer in NDC wins.
   - **Retail:** char-body pass projection `z_far=1450` → body ndcz **0.94798**;
     glow pass `z_far=2000` → glow ndcz **0.94780** (NEARER) → glow passes LE →
     **draws over her head**.
   - **Port (bug):** char-body `z_far=3025` → body ndcz **0.94785**; glow
     `z_far=2000` → glow ndcz **0.94801** (FARTHER) → glow fails LE → **occluded**.
   - The body z_far comes from `FUN_004552d0` L334: `2200-(local_14-11)*75`,
     `local_14 = DAT_0438b778 + DAT_044e2c70`. The port's `scene1_shop_walker.c`
     stub `sw_dat_044e2c70()` returned **0** (mislabeled "BSS sub-frame counter")
     so `local_14=0`→`z_far=3025`. `DAT_044e2c70` is actually the camera
     **eye.y-add constant 21.0** (`.rdata DAT_005c4fd8`; `scene1_camera.c` already
     had it). With 21.0, `local_14=21`→`z_far=1450`, matching retail; the glow now
     washes over her head/hair (user-confirmed "perfect" 2026-06-03).
   - **Mechanism is the same flipped-depth class as the dust occlusion** (the port
     drew the char body NEARER than effects that should sit in front; dust is the
     mirror). The per-pass z_far bias is the engine's billboard depth-layering — see
     engine-quirks §93. **Diagnostic toggle proof:** disabling the body Z-write
     (`OPENRECET_NO_CHAR_ZWRITE`) did NOT fix it (it only un-occludes the trail
     sparkles, a separate minor effect) — ruling Z-write out and pointing at the
     projection, before the trace pinned z_far.

2. **Wings render OVER her hair in retail (layering / draw order).**
   (feed crop `box=186,217,246,322`.) In retail the wing sprite draws on top of
   her hair; the port's layering differs. → a draw-order / Z or painter-order
   issue between the wing billboard and the body sprite.

3. **Sprite is a slightly different ANIMATION FRAME.** (same crop as #2.) The
   hand pixels differ between port and retail → Tear's body-anim phase is off by
   a frame or two. Consistent with the companion anim/bob phase riding a
   *local* `s_bob_counter` instead of the engine's shared `DAT_056db054`
   (`scene1_companion_ctrl.c` L86-107) — the deferred "chase phase later".

4. **Eyes are different — and NOT just from the blue glow.** (feed crop
   `box=50,393,137,446`.) The eye pixels differ independently of the face glow →
   another symptom of the wrong anim frame (#3), or a different blink/expression
   sub-state.

5. **Blue wing-sparkle particles: phase/RNG mismatched.** (feed crop
   `box=201,324,353,525` = DIFF panel.) The trailing 0x1f sparkles light up in
   the diff. **Expected** for this un-pinned trace (different RNG phase
   cross-target) — to verify the sparkle render/occlusion the trace must be
   RNG-pinned (`{rngseed}`, [[scene1-rng-stream-parity]]); otherwise this is
   phase noise, not a render bug.

## Position vs phase — verdict

Tear's **world position is NOT the bug**: at the stationary bottomwall her draw
pos = retail's exactly ((0.6, 3.056, 9.35), d3d-trace char#2/#8 vs port actor[2],
`scene1-walk-dust.md` §2026-06-03f). At cap_03 she sits at ~the same SCREEN
position as retail. The divergence is **appearance**: (a) the missing face glow
(#1), (b) wing-over-hair layering (#2), (c) a wrong anim frame (#3/#4). Spring
position during fast motion is still worth a synced check, but the cap_03 diffs
above are render/anim, not position.

## Preliminary root-cause notes (2026-06-03)

- **Companion additive glow billboard (chr02.bmp, ONE/ONE) scale MATCHES retail**
  — d3d-trace basis length 0.030 on both, blend 2/2 on both, pos (0.6, ~3.0,
  9.35). So #1 is NOT a glow-billboard *scale* bug. In the **bottomwall** trace
  the port glow sits at Y≈2.90 vs retail 3.05, but that trace is **unsynced**
  (port f656 vs retail f538) so the 0.15 is just **bob phase** between two sims —
  inconclusive for cap_03. **#1 must be re-checked on a SYNCED walk d3d-trace**
  (port+retail at the same cap), now capturable via `--d3d-trace` windowing.
- Working hypothesis: #1 (face glow position), #3 (anim frame), #4 (eyes) may
  share ONE root — the companion bob/anim **phase counter** (`s_bob_counter` vs
  the engine's shared `DAT_056db054`). #2 (wing/hair layering) is a separate
  draw-order question.

## Next

Capture a SYNCED walk d3d-trace (port + retail, same cap as cap_03) and compare
the companion glow billboard + body-sprite world matrices + textures there, to
separate the bob-phase confound. Then root-cause #1 (face glow) — it's the most visually prominent and is a
render/blend/draw-order question localizable from the wing-glow draw vs the body
sprite (no anim-phase confound). Then #2 (wing/hair layering). #3/#4 (anim frame)
fold into the deferred `s_bob_counter` → shared `DAT_056db054` phase alignment.
#5 needs an RNG-pinned trace to even assess.

Cross-refs: `docs/findings/scene1-wing-glow.md`, `scene1_companion_ctrl.c`,
[[project_next_char_controller]], [[reference_parity_trace_walk_down_dense]],
engine-quirks §71 (bob) / §81 (wing flap).
