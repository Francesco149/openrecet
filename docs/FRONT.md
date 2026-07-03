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
  carried correctly. **★ REMAINS (human): `--target both` for RNG/line_row parity vs retail (only the PORT
  was driven) + viewer confirm of the DAY2 brooming pixels + the sign-hammer close-up.** Watch the known
  DAY-2 blink-stall lead (~frame 21259+ — did NOT manifest port-side here). GOTCHAS burned this session:
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
