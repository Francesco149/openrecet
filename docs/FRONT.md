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
- **✅✅✅ 2026-07-02 — the ANIM SEED-ORIGIN arc CLOSED + USER-CONFIRMED "the trace is basically fully 1:1"**
  (`house-firstcust-arrprobe` win-0-1500; commits `6f0993b`+`2537904`+`2038905`, RE §21.28/§21.28.1, ledger).
  Five tick-cadence roots — conv-pose `cc08!=4` release gate · cc08==4 set-then-tick · entry-frame no-tick ·
  the never-ported cs-walker set-anim (chibi slid in idle = notes #20/#22) · the cs-leave tick-only frame (+20
  pose-era = note #21).  cframe/ccnt/ctimer/canim `✓ aligned` WHOLE [224,1722], n0* aligned, rng bit-exact
  225→1722, all 3 note crops diff BLACK.  **REMAINING RESIDUALS (user 2026-07-02, small, NEXT):**
  - **(A) "recette phase at the very start"** = the player `pframe/pcnt` window-start load region (@224, ~45f).
    Recette's anim cycle phase at the load-in; realigns ~frame 270.  Same tick-cadence class, initial-load era.
  - **(B) ✅ 2026-07-02 the VASE SHADOW — FIXED, note #23 crop diff BLACK (RE §21.29; pending user studio
    re-confirm).**  NOT the shadow pass: fade.c mistranscribed the engine's ALPHAREF(0x18)=0 as
    ALPHATESTENABLE(0xf)=FALSE ⇒ every pause/fade frame leaked alpha-test-OFF into the next frame's mesh
    pass ⇒ flower-fringe texels z-wrote and clipped the display-stand shadow decal.  +2 walker ALPHAOP
    BLENDDIFFUSEALPHA(12)→MODULATE(4) value-vs-name fixes (pixel-neutral, program-parity).  Pause frames
    125→2 px (2 scattered 1-px speckles).
  - (C, lower) companion `coct/cx` tutorial-cs facing blips (@389, 48/33f — facing write-order, scoped in task
    #5); probe-only init leftovers `ask/base/b5b0` (retail 1000/1 vs port 0 pre-cs, cosmetic).
- **✅ 2026-07-02 — `house-firstcust-cutscene-day2` carries the FULL pin set** ({csloadpin:24} +
  {primaryloadpin:16} + {tutloadpin:8} + {bgnpcseed}; same savefile ⇒ same naturals; both sides auto-skip its
  own `{bgnpcpin}` inject per §21.25).  **VERIFIED `--target both`: raw rng bit-exact frames 225→1934 = the
  ENTIRE first-customer region** (arrprobe's confirmed span was 225→1722); anchor sequence identical, initial
  load 1505→223.  NB the whole-capture `flow_diff --verdict` shows bgx1..5 "DRIFT @81" + "rngcalls DESYNC @3"
  even on the USER-CONFIRMED-1:1 arrprobe capture — that signature is the accepted pre-pin/warmup region +
  probe print-precision (retail f64-prints, port %.9g), NOT a regression; judge by the aligned span.
- **★ OPEN GAPS queued on this trace (user-flagged):**
  - **✅ 2026-07-02 USER-CONFIRMED ("everything matches"): gap (ii) dialogue-under-ESC-modal (already fixed
    by §21.15; stale-window flag) + #7/#19 modal double-blend (the FUN_0040a765 HUD-tail ungated
    FUN_0043537e pass — NOT an RT effect; ported bc114cd, box-UI draw region 81==81 bit-1:1).**  Story:
    RE §21.26, PROGRESS 2026-07-02, ledger.
  - **NEW note #8 (user 2026-07-02): the choice-box commit FLASH — FIXED, bit-exact (RE §21.27,
    engine-quirk #128).**  Retail pulses the CHOSEN label 0x7f→217→254→217 over close-frames ac14 1..3
    (`0x7f − ftol(sin(ac14·π_f/4)·−128)`, ADDSIGNED brighten); the peak is 254 NOT 255 (double sin of the
    float-rounded π/2 — `sinf` would be 1 LSB off).  Verified max-px-diff 0 on the pause confirm +
    sub-LSB on the ESC-skip confirm.  **Pending user studio re-confirm of #8.**  Note #9 (wing-flap
    residual) = the ★★★ anim seed-origin arc above, acknowledged.
  - **NEW tool lead 2026-07-02: v3 PORT replay hash-verify fails 5/2895 frames** (presents
    239/542/661/2184/2303, cache 7ea1eab3) — capture replays ≠ live pixels on scattered frames; window
    built with `--no-verify`.  Investigate the replayer gap before trusting single-frame pixel verdicts
    at those presents.
  - Pending explicit user re-confirm (minor): the note-#1 sell-counter "!" emote fix (2026-06-20,
    `house-customer-walk-probe`) was never separately re-confirmed.
- **★★ QUEUED ARC — CUSTOMER INTERACTIONS deep-dive (user directive 2026-06-22).**  Mechanics the user named
  (verify in code, don't trust the wiki): (1) CLOSENESS/affinity per customer; (2) ATMOSPHERE score from shop
  DECORATION.  Includes: **L1b** real accept side-effects (cs-live-sale-fx: gold += ask, stock decrement,
  catalog/inventory/payout FUN_00460d52/b3a/606fc/00083/0002a/00b93, all f404==0); the roster scan
  (cs-roster-scan); live-haggle render fidelity (customer art/dialogue); **the iv1_8 chain** (f406→f402
  post-first-customer EXTRA_SPRITE cutscene) → the cutscene series → day-2 brooming.  Separate follow-up:
  the DAY-2 cutscene blink-stall (~frame 21259+, does not affect the cc08 survey window).
  **DAY-END entry point pinned down 2026-07-02 (the day2 rng fork):** on the day2 trace, the Z at
  PAUSE_CLOSE#3+89 (frame 1934) makes retail draw **+261 rng calls in ONE frame** (the suspected next-day
  layout/roster regen) while the port draws none; the port's day-end transition then fires (music swap
  frame 2274 == retail LOADING_START 2273) but **emits NO LOADING_START anchor** (different/unported load
  path) ⇒ the trace's `{wait: LOADING_START}` never releases and replay stops.  Two concrete leads:
  (a) the +261-draw day-end consumer, (b) route the day-end load through the anchor-emitting load machinery.
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
