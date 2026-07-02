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
  - **NEW tool lead 2026-07-02: v3 PORT replay hash-verify fails 5/2895 frames** (presents
    239/542/661/2184/2303, cache 7ea1eab3) — capture replays ≠ live pixels on scattered frames; window
    built with `--no-verify`.  Investigate the replayer gap before trusting single-frame pixel verdicts
    at those presents.
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
  **OPEN — the sale-fanfare residue (needs HUMAN VERIFY in the viewer; win-0-3000 re-driven --state):**
  (1) RENDER: the coins/glow spawn+land rng-exact but DRAW NOTHING (retail draws ~30 extra overlay quads at
  the burst frame; tpl 170-176 tex 20-30 shape 0 layer 0 MODE 1; the tex-19 mode-0 sparkle draws fine —
  chase via the viewer draw-program panel / pixel-pick); (2) the TOTAL-EXP popup chain (FUN_004606fc →
  FUN_00485861 → FUN_00406159 @(412,112), SE 0x174/0x172) — retail shows "TOTAL EXP 10", port nothing;
  (3) retail audio dedups same-frame SE repeats (15 se_069 lines vs port 24, same window) — port SE-play
  dedup not modeled (cosmetic, jitter rng bit-exact anyway).
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
