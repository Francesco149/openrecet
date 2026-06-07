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

## #3/#4 determinism verdict — PHASE-ORIGIN offset, NOT a logic error (2026-06-04)

Ran the determinism cross-check the working hypothesis called for: drive the SAME
synced trace (`house-walk-down-dense`) on both targets and log the companion's
phase state **per sim-frame**, then diff at matched anchor-relative frames.

- **Retail:** `frida_capture.py --input-segtrace … --watch db054=0x056db054:s32
  --watch cframe=0x056dab50:s32 --watch ccnt=0x056dab4c:s32 --watch
  coct=0x056dab58:s32 …` → `runs/tear-phase/retail/watch.jsonl` (one row/frame).
  (Companion record VAs from `FUN_0048a4d1`: ANIM=`0x56dab40` TIMER=`0x56dab48`
  COUNTER=`0x56dab4c` FRAME=`0x56dab50` ANIMSEL=`0x56dab54` FACING=`0x56dab58`.)
- **Port:** `export_trace.py … --caprange 1540,140` → `meta.jsonl`, after
  extending `--player-pos-log` (src/main.c) with `cframe/ccnt/ctimer/db054`
  (`scene1_companion_db054()` + actor-2 record).
- **Align by anchor-relative frame** (port meta `i` ↔ retail `abs = retail_anchor
  + port_seg0 + i`; both HOUSE_FREEROAM). Recette's walk is 1:1 so the player is
  the same instant at each matched frame.

**Result (140 frames):**

| metric | port − retail | interpretation |
|---|---|---|
| `db054` (bob / sparkle phase) | **+1518, constant, zero drift** | pure phase shift |
| anim COUNTER (`ccnt`) | **+19 (mod the 40-frame cycle)**, constant | pure phase shift |
| facing octant (`coct`) | **0/140 mismatches** | bit-identical |

The increment law and the facing law are **bit-exact** — the offset is a single
constant with no per-frame drift, on a deterministic (constant-offset, replayable)
harness. So #3/#4 are **NOT a logic error and NOT harness non-determinism**; they
are a deterministic **phase-ORIGIN** offset.

**Root cause:** `DAT_056db054` is frozen at 0 through retail's intro video and only
starts ticking at the conversation/HOUSE per-frame open (engine-quirks §94): retail
`db054 == 43` at HOUSE_FREEROAM. The **port skips the intro video** (§13), so its
HOUSE postload — which resets `s_bob_counter` (`scene1_postload.c:132`, the faithful
mirror of `FUN_00436f97`) — fires ~1518 frames "earlier" in companion-phase terms,
and the counter then accumulates that whole stub through to free-roam (port
`db054 ≈ 1561` at its anchor). The reset *site* is already engine-correct; the
*intro length feeding it* is not. This is the deferred "chase phase later"
([[project_next_char_controller]], §81), now quantified.

**Fix is a structural choice (needs a call):** the video retail uses to "pad"
db054's relationship doesn't exist in the port, so there's no single mechanical
mirror. Options: (a) at free-roam onset, re-seed `s_bob_counter` (and the companion
anim cycle) to retail's measured db054-at-that-moment; (b) make the port's
pre-free-roam HOUSE timeline frame-match retail's (hard — depends on the absent
video); (c) accept a known fixed phase offset and phase-align it out in comparisons
(the `d3d_state_diff.py phase` tool already measures the offset N). Data:
`runs/tear-phase/{port/meta.jsonl,retail/watch.jsonl}`.

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

## DEFERRED — Tear sprite ANIM-FRAME desync on the walk (user-flagged 2026-06-07) — but the flow-trace says it may be a PAIRING ARTIFACT, not a real bug

After the foot-dust RNG-order fix (engine-quirks §114, commit 29e1ee8) the user
re-captured `town-map-load-rerecord` both-sided and confirmed **the foot dust is now
1:1**. The remaining companion divergence the user saw is **Tear's sprite animation
FRAME** appearing out of phase with retail, growing over the walk. SPA diff-panel crops:
- **f=150** — first divergence, a single row of pixels: `box=597,338,628,452`.
- **f=157** — growing: `box=559,331,673,452`.
- **f=163** — culminates: `box=563,324,652,435`.

**Caveat before chasing this as a logic bug (2026-06-07):** on that very session's
call-traces, `flow_diff --verdict --align-field db054` reports **`canim` AND `cframe`
ALIGNED bit-exact** (and `cx/cz/coct` aligned) over the 225 common db054 frames — i.e.
when paired by db054, Tear's body anim cell is 1:1 with retail. The session ALSO has a
**kept-count mismatch: port 270 vs retail 240 frames** (`session.json
kept_count_mismatch`). The Trace-Studio SPA pairs frames **ORDINALLY** (Nth-left vs
Nth-right), so with unequal kept-counts the pairing DRIFTS as the index grows — and the
**progressive worsening** the user saw (1 line @150 → worse @157 → culminates @163) is
the signature of an accumulating pairing offset, not a fixed anim-cell error. Tear's
fast 4-frame wing-flap is simply the sprite where a 1-frame pairing slip is most visible
(the slow-moving player/HUD hide it).

So the honest status is **UNCONFIRMED**: the db054-aligned ground truth says Tear's
anim is 1:1. Before treating this as a real bug, **resolve the kept-count mismatch
first** — why does the port keep 270 walk frames where retail keeps 240 in the same
`{caprange:[0,271]}`? (Capture-window / load-suppression / capstride seam alignment;
`capture.py` already WARNS "ordinal pairing is unreliable" on a mismatch.) Then re-diff
with EQUAL kept-counts (or compare crops at MATCHED db054, not matched ordinal index).
If a `cframe` divergence survives db054-alignment, only THEN is it a real anim bug — and
the next suspect is a render-layer the body `cframe` doesn't cover (the wing-glow
billboard's own anim frame), since the body `cframe` is already proven aligned.

**DEFERRED by the user until the door tooltip + town-map are implemented** (the NEXT
ARC). Recorded here so the crops + onset frames + the pairing-artifact caveat aren't
lost.
