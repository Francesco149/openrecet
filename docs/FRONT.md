<!--
  The ONE hand-edited status block.  tools/gen_port_ledger.py injects everything
  below the marker line verbatim into docs/STATUS.md's "Current front" section, so
  STATUS can never drift from reality.  Update THIS when the active front moves;
  keep it short (a 60-second read).  Everything else in STATUS is derived from code.

  RULES (the 2026-06-09 cleanup): keep ONLY open/forward-looking items here. When
  an item RESOLVES, move its story to PROGRESS.md / the findings doc in the same
  edit — do not let resolved arcs accumulate (the file had grown to 743 lines;
  that snapshot lives in archive/FRONT-2026-06-09-full.md, where all pre-cleanup
  arc history — world-map backlog, dialogue fixes, load-arc, shop-display chips —
  can still be read).
-->
<!-- FRONT:BEGIN -->
- **Phase:** frame-by-frame 1:1 parity sweep along the player path (title →
  prologue → HOUSE → shop loop → world map → dungeon). Strategy + tooling roadmap:
  **`audits/2026-06-09-methodology-audit.md`** (settled verdicts — behavioral-vs-
  byte-exact CLOSED, x87 invariant, T1–T12 tooling roadmap, milestone-ladder KPI).
  Read it before re-litigating strategy or building new parity tooling.
- **ACTIVE ARC → ITEM-DISPLAY interaction flow** on trace-studio session
  **`item-display-2`** (`http://localhost:8778/?session=item-display-2`; load slot 2
  → place 3 items → 2 Tear tutorial dialogues; pinned + call-traced). Landed so far:
  C3a slot-glow, C3b item tooltip, both tutorial dialogues frame-exact (anchors Δ=0
  incl. the CONV_POSE_BLINK cadence fix `a8269f6`), text-reveal gradient, **the
  event-arm routing chip `843b6f1` (2026-06-09): busy frames now dispatch to
  FUN_004427d3 like retail — killed the dialogue-window rngcalls desync (+1375 →
  +31), the db054 menu-close off-by-one, and with them the phantom px/py DRIFT @202
  (gap F sim half) + the placement-dust desync (gap E); px/py/dust/db054/cbfc now
  ALIGNED/bit-exact on the verdict.** Full RE + history (incl. the ±31 residual
  boundary-frame breakdown): `findings/shop-display-menu-RE.md` "Open follow-ups".
  **2026-06-09 PM triage of the recapture also CLOSED two follow-ups without code:**
  placed-item ids (#4) were never wrong — correct raw `id<<6` dwords, placements
  pixel-identical at all 3 confirms; the bread tooltip (#5) is present on both
  sides through the dialogue (old "ord 854" pointer was dead pre-unification
  numbering). Evidence + frame refs: the RE doc's follow-ups #4/#5.
  **NPC desync through the dialogues: user-confirmed GONE 2026-06-10** (parity
  ledger entry; the scare was a stale ordinal-paired diff — fixed, see Tooling).
  **Gap B ("What will you place?" bubble) ✅ DONE 2026-06-10** (`36a8ab2`): NOT a
  string — all 3 prompts are BAKED item_win sprites drawn by FUN_0046b00a at
  dst(menu_x-128,48,191,63), sliding with the panel; flag DAT_0734b990 set by the
  arm (occupied cell → "Exchange with what?", Vender highlight → "Place Vending
  Machine"). 0-1px at labels 439/588-594. RE: `findings/shop-display-menu-RE.md`.
  **Carry pose (queue #2) ✅ DONE 2026-06-10** (`4bc8a0b`): db048==0xc state ported
  (r==3 arm + 26-frame free-roam hold + interaction/impulse gates); carry-window
  frames drop ~2,290→2-65 px each, pose pixel-exact, residue = sparkle phase.
  **Standee horizontal offset (#3) + portrait-outline polish (#7) + NPC note @1844
  ✅ DIAGNOSED 2026-06-10 — all ONE cause, the iv1_6 LOAD-BRACKET length; no logic
  gap.** Slide logic measured 1:1 (template-matched trajectories: same path, speed
  8px/f entry / 16px/f exit, ±1 frame vs script start on both sides); the 4-label
  lead = retail's inter-dialogue bracket 5f (async CreateThread worker = wall-time,
  quirk #119) vs port's 2f + a 1-frame iv1_5-tail slip. Worst frame 1792, the
  1735-81 bursts, note @1448, note @1844 walker offset: all this seam. Settled
  frames are gt8≈2 ⇒ no filtering/sub-pixel residue (#7 dissolved). Don't tune
  `IVE_TUT_LOAD_FRAMES` to 5 (one-run wall-time). Full measurement + corrected
  label↔frame mapping: `findings/shop-display-menu-RE.md` follow-up #8.
  **Text reveal gradient (#4) ✅ DONE + USER-CONFIRMED 1:1 2026-06-10** (ledger):
  per-CHAR law `alpha·clamp((budget−i)·0.2, ≤1.0)` (a278101's per-row read was a
  loop-counter misread, gotcha #18); `font_draw_text_fade`, text strip gt8=0
  across the reveal, session over-threshold 1212→916.
  **iv1_5-tail pose-release slip (was queue #1) ✅ DONE 2026-06-10** (`c8a40df` →
  PROGRESS): the port armed iv1_6 the SAME frame iv1_5 completed (last
  `CONV_POSE_BLINK`→`CONV_POSE_END` 8f vs retail 9f, d=−1/−2 across iv1_6). Retail
  defers the re-arm 1 frame — its gate `DAT_0438b1c8` clears 1→0 in `FUN_004536cb`'s
  tail AFTER `FUN_0044bd0d` ran (call-trace: done@f15933, load-spawn@f15934). Fixed
  with a 1-frame `D_TUT_DONE` settle latch; recapture #7 (`--only port`): iv1_5-tail
  8f→9f, iv1_6 anchors bit-aligned (+733/+734/+1166), `problems: []`, **over-thr
  861→529**, rngcalls +26→+12. **✅ USER-CONFIRMED 1:1 2026-06-10** ("the 2nd
  dialogue is 1:1 aligned now") — parity ledger; closed the standee@~1448 +
  NPC@~1844 seam artifacts (RE #8).
  **Remaining (user-listed 2026-06-10 + triage), the next-session queue:**
  5. **Description-panel CLOSE/open SLIDE — ✅ DONE 2026-06-10 (`d4899bc`).** The
     session's WORST frame (label 181, gt8≈185k) was NOT the Item-Details sub-view
     (RE #8b mis-attributed it): trace shows `cc04` 1→0 at f176→178 (menu
     *closing*), and retail's bottom description panel SLIDES out with the menu
     while the port drew it fixed at x=0. Ghidra arg-drop — `FUN_0046b00a`'s tail
     prints `FUN_00469b3a()` with no args but its `param_1` is the slide x-offset
     `640−(b98c<<7)`. Threaded x0 into the description render; recapture: 181
     185441→near-black, close-slide bit-exact (R[f]=0). **The real `pressed&0x40`
     Item-Details overlay (`FUN_0046a336`) is never exercised by this bench (no
     Button-3 press) — still unported, deferred to a bench that drives it.** RE map
     for the eventual port: `findings/shop-display-menu-RE.md` #8b. Remaining menu
     polish: **description-panel line layout** (price / "Number possessed" X) · (C)
     slide-in check · (D) row flash.
  6. **menu-boundary residuals:** **NOW the session's worst frame — menu-OPEN
     slide-in (label 125, gt8≈29k)**: after the #5 close-slide fix, the open
     ramp-up still diverges ~4 frames (the whole menu — rows + description, ~9
     mean abs) before settling, then is bit-exact. ASYMMETRIC: the CLOSE slide is
     R[f]=0 bit-exact but the OPEN is not, and it best-aligns SAME-frame (not a
     clean 1-frame offset) — a menu-OPEN ramp-phase residual in `DAT_0734b98c`.
     The port order is already tick-before-arm (sim.c:299 `stage_load_pulse_tick`
     → 327 `scene1_ingame_tick`→cc04 open gate), matching retail's
     `FUN_004693e3`@50471-before-`FUN_0048670f`, so the cause is subtle; pin it by
     instrumenting the port counter vs the (untraced) retail counter. · rngcalls
     ±31 (one wing emit per %4==0-frozen pause boundary + load-bracket seams) ·
     companion cx/cz/canim/cframe + pcnt micro-DRIFT around open/close frames
     (pcnt ALIGNED at open f122 P1=R1, port-1-behind by close f176 P4/R5 — gains 1
     on retail across the menu window) · retail menu-window consumption is the
     WING through the hooked thunk (`0xcf05d33`), NOT an unknown menu consumer ·
     hand-cursor snap drawn 1 frame EARLIER by retail at menu open (label 587,
     ~109px, found verifying gap B) · **menu-close camera pan-out whole-frame
     offset** (label 441, ~160k px>8 — pre-exists the carry chip, attributed by
     stash-rebuild-recapture; the scene shifts ⇒ camera, not UI).
  7. **Item-display SOUND — ✅ DONE 2026-06-10 (`af47e08`).** The
     cc04 interaction consumed the SE-variant RNG draws but STUBBED every play, so
     the menuing was silent (audio-trace diff: 14 missing over 6 sounds,
     user-confirmed by ear). Un-stubbed all of it: open `00re_sys04a/b/c` (rand%3),
     confirm clip `00re_sys05b/a` (rand&1, b@index0), confirm/pickup/cancel beep
     `FUN_00499519(0x143)`, and the walk **footstep** `FUN_00499519(0x166)` (asm
     0x48c824 — sibling of the foot-dust emit, gate `STATE==1 && (COUNTER&0xf)==0xa`,
     independent of the dust cadence; foot_dust's early-return restructured to a
     nested gate). All RNG-neutral (no stream shift; triage/px/py/dust unchanged,
     host 3229). Recapture+audio_diff: **missing 14→0, all 6 sounds matched.**
     RE/tooling: `findings/audio-trace-diff.md`. **Then the 3 EXTRA dialogue voice
     grunts (`re_wakata_b`/`tea_sodesu`/`re_un_a`) — ✅ DONE 2026-06-10 (dialogue
     voice-mute chip):** user-flagged that retail MUTES voice lines while holding X
     to skip; RE'd the exact gate (the `se:` opcode plays only when the internal
     step count `DAT_005c78ec == 1` — any fast-forward, X→2 / turbo→0x50 steps,
     skips it; quirk §120). Gated `IVE_OP_SE` on `(held & IVE_BTN_FF)==0`. **The
     whole item-display-2 audio track is now `audio_diff` VERDICT ALIGNED** (0
     missing, 0 extra, 9 sounds matched).
  *(Tooling owed here — recorder `save_capture` clobber + the session kept-count
  mismatch — both CLOSED 2026-06-10: `9a7bf63` stops the save clobber, and
  `{tutloadpin}` equalized the brackets so the kept-count PROBLEM is gone
  (`problems: []`). Story → PROGRESS; label-pairing fix folded into Tooling below.)*
- **ACTIVE ARC → MERCHANT'S GUILD scene** (mode 6 / Market), trace-studio session
  **`merchants-guild-20260608-151902`** (served 8782). **Scene shell + first-visit
  cutscene LANDED 2026-06-10** (`a998fb4` chip, `cb1212c` docs) — `src/scene_guild.{c,h}`:
  mode-6 enters, renders the guild bg (`bmp/ivent/bg_guild.bmp` + the mirrored guildmaster
  `13syounin_01.tga` at dst −64,32,448,448), and plays the iv1_3 first-visit cutscene
  (`scene_guild_sim` = `FUN_00490e24`→`FUN_004922c0` first-visit subset → entry-tick
  counter + flag `DAT_0450f3f4` @ `0x2bc5c` → `scene1_intro_dialogue_start_single(1,3)`;
  dialogue tick/draw wired into mode 6). Build clean, host 3229.
  **CENSUS DONE → cutscene is frame-by-frame 1:1, USER-CONFIRMED 2026-06-10** (parity ledger;
  the user eyeballed the anchor-matched montage: "that looks correct").** The earlier "db054-verdict BLOCKED /
  run the RETAIL census" was a MISFRAMING: the cutscene IS richly probed on BOTH sides
  (`dialogue_tick` `FUN_0046c320` emits box_open/reveal/line_row/st5_* — 774 retail / 895
  port frames). db054 is the wrong clock for a cutscene (its only source `house_update`
  `FUN_0048670f` fires 0× retail / 2× port — a HOUSE counter that doesn't advance in mode 6),
  so `--align-field db054` correctly finds no shared values; NO probe extension was needed.
  Verdict now runs via the new **`flow_diff --align-anchor TEXT_ANIM_START --frame-from N`**
  (constant-offset anchor align; `triage` auto-falls-back to it) → **✅ PHASE-CLEAN** over
  714 cutscene frames (dialogue+fade+rngcalls ALIGNED, raw rng 714/714 bit-exact). Proofs
  (single −14100 offset): 75/75 anchors frame-exact · 8 dialogue fields × 774 frames ZERO
  mismatches · rngcalls 0 desyncs. Residue = load-seam only (port loads faster, renders ~121
  early cutscene frames in retail's load bracket; kept-count 1058/936; phase pillar, accept).
  Canonical pin KEPT (`{phasepin 282}`+`{rngseed [282,19937]}`+`{tutloadpin 8}`). RE + full
  breakdown: `findings/merchant-guild-RE.md` "CENSUS DONE"; recipe: `flow-trace-cheatsheet.md`
  "Cutscene verdict".
  **WORLDMAP door-exit SE ✅ DONE 2026-06-10** (`172ecc9`): the 2 "missing worldmap sounds"
  were the first-shop-door-exit SE (`FUN_0048670f`, RE'd via a retail audio-hook `ret_va`),
  not worldmap — un-stubbed; `audio_diff` ALIGNED (9 sounds). See `audio-trace-diff.md`.
  **MAIN MENU UI ✅ LANDED 2026-06-11** (`06e9fdf` font helper + `6ea5a3a` menu) — the
  post-cutscene guild menu now renders **pixel-identical to retail** at rest (session label
  02191): panel + Buy/Sell/Talk/Leave options + gold HUD + the "Guild Master" speech bubble
  ("Time to stock up a bit, eh?") + the hand cursor + the "New" sparkle badge on Talk all
  match (diff ~99.9% black). Residuals = cursor bob phase (~3px) + New sparkle phase
  (sub-pixel), both the load-seam phase pillar (the menu resumes a few frames apart;
  accept). The stray blue bar is gone. Key structural find: the menu update+render fire
  ONLY pre/post-cutscene, NEVER during it (retail call-trace) → gated on
  `!scene1_intro_dialogue_busy()` so the bubble freezes through the cutscene + pops in
  after. Cutscene verdict unchanged (CONST-OFFSET, audio ALIGNED). Bubble-text scale gotcha:
  FUN_00465db4 passes the box scale (1.0), not ×0.76 (Ghidra FPU mis-group). Full RE +
  per-draw geometry: `merchant-guild-RE.md` "Main menu UI". (Window/sync infra `12f53d2`
  MARKET_ENTER + `6b1d714` anchor-rebase at EXTRA_SPRITE_END; rebase trade-off — pre-cutscene
  worldmap labels 330–~510 read offset, worldmap 1:1 separately.)
  **BUY FLOW step 2 (the milestone) ✅ LANDED 2026-06-11** on session
  **`merchants-guild-ui-flow-20260611-052747`** (served :8778): Z on Buy now slides the main
  panel out + slides the item window IN with the guild-stock buy list populated + rendered.
  The mode-1 A-dispatch + slide-in ramp (`scene_guild_sim`), the **mode-7 guild-stock
  population** (`display_menu_open`, port of `FUN_0049196f` — scans the item DB w/ the
  gi-tier/store-level filter, tables `DAT_005cfabc`/`DAT_005c6ef0` from .data; mode-aware tabs;
  per-item qty-cap), the buy-row `"%s - %d Left"` format, the "Purchase Price-" label, and the
  `display_menu_render` wiring all landed together (the coupling — the menu never blanks).
  **✅ USER-CONFIRMED 1:1 2026-06-11** ("the swords menu indeed matches") — parity ledger; vs
  retail's pre-overlay frame (items Worn Sword/Longsword, order, icons, caps 3/1 Left, desc,
  price 140, possessed 0). The port correctly freezes at the fresh open (steps 3-4 unported).
  Host 3230. PORT-DEBT: price-trend factor (`FUN_004361b2`) + the Out-Of-Stock/Not-For-Sale/
  Adventurer's-Possession post-buy status texts.
  **BUY FLOW steps 3+4 (the qty "Yes/No" prompt — the user-flagged milestone) ✅ LANDED 2026-06-11**
  (`45f5bca`): A on a buy-list item now opens the "Buying N <item>. Are you sure? Yes/No" qty
  overlay, navigable + purchasing. `scene_guild_sim` mode-0 (item list: `display_menu_update`
  → r3 price-preview / r1 open / r2 back) + mode-8 (`FUN_00491bc0` qty input: U/D qty, L/R
  Yes/No, A/B confirm/cancel + the slide/flash) + the purchase (`FUN_00468d22`×qty +
  `FUN_00469a00` stock/daily decrement + gold deduct + SE 0x14d) + `FUN_00491de0` render
  (savewindow.tga box + title + qty + "Stock Price…pix" + Yes/No + arrows, ADDSIGNED grey-127).
  **Tutorial infinite-money** (`FUN_004922c0`:94756 — gold pinned to 10,000,000 while the
  restricted-stock flag is set) ported, so the qty cap = per-item stock (3 Left), not gold, and
  the HUD gold never drops. Recapture (`--only port`): the full flow executes (A=Buy→select→qty
  wiggle→buy1 q1→buy2 q2, 2 purchases, stock 3→0); **audio_diff 51→14 missing**; the qty box
  matches retail. **✅ USER-CONFIRMED 1:1 2026-06-11** ("the panel looks correct other than the
  slight phase desync that was already there") — parity ledger.
  **3 user-flagged polish gaps then ✅ FIXED + verified 2026-06-11** (`922b5be`):
  (1) **green qty outline + PULSE** — the "%2d" uses a green diffuse that THROBS:
  `wob=(int)(sin(c54·0.1)·-16)`, R=`8f-wob` G=`ce-wob` B=`8f-wob` (engine asm 0x492008-7f; the
  -16 amplitude was a Ghidra FPU drop), white body + pulsing-green edge under ADDSIGNED. First
  shipped as the flat midpoint (no pulse — user caught it); now the green-mean tracks retail within
  ±1 across the cycle (trough~130 @1761, peak~145 @1791), phase-aligned via the c54 reset-on-open. (2) **full-width-space (SJIS 81 40) price spacing** — `font_alloc` now pins it to
  0x0d (like the engine's ASCII-space 0x18) + `font_upload` no longer clobbers an alloc pin with
  a blank-glyph 0; "140pix" @x230 vs retail x228 (was overlapping "Price"). **Cutscene
  bit-identical** (diff 0px>8 @labels 900/1100) — the global font change doesn't touch the
  confirmed-1:1 dialogue. (3) **gold rolling-counter** — `scene1_top_hud_money_tick` (FUN_00406584
  @4849) eases the HUD gold toward bank by `rand()%max(|Δ|/25,10)+|Δ|/100` (1 rng_next15/rolling
  frame, no-op at rest), wired pre-sim into `scene_guild_sim`; gold rolls 1000→860→580 across the
  2 buys **matching retail at the same labels** (roll RNG in sync). Restricted flag is 0 here so
  the tutorial gold-pin stays inert (qty cap = stock "3 Left").
  **BUY-FLOW ARC ✅ CLOSED 2026-06-11** — all steps + polish USER-CONFIRMED 1:1; the residual
  audio (14 SE) was VERIFIED accept this session (the dense qty-wiggle cluster is near-identical
  retail~20/port~19 = a 1-beep auto-repeat rounding; the rest are retail triggers PAST the port
  TAS replay end ~f3979 → retail frames 18101-19888 have no port counterpart) — the port fires
  every sound TYPE; post-buy item list 1:1 modulo cursor-bob phase (only bright diff = the cursor
  bbox; "Out Of Stock" text simply isn't exercised, sold-out shows "0 Left" both sides).
  **→ NEXT (active): the leave-guild BREAD CUTSCENE = `scene1_intro_dialogue_start_single(1, 9)`
  (iv1_9), RE'd `cadf75f`** — `FUN_004922c0`'s Leave-option dispatch `LAB_00492ad7` fires it on
  the per-location first-leave flag (`DAT_0450f3f5[loc]==0`); the dialogue machinery is already
  ported, so the port = the Leave handler + gate, no new dialogue code. **NEEDS A FRESH RECORDING**
  (the current trace ends in the buy menu): record enter-guild → main menu → Leave so the bread
  cutscene plays, then empirical RE + port + recapture-verify; then returning to Recettear (Tear
  cutscene). RE: `merchant-guild-RE.md` "Planned follow-on traces".
  **Other PORT-DEBT:** Sell (mode 3), first-buy tutorial/limit gates, the Talk submenu / Fusion (`FUN_00493616`) / Expansion
  flows, the `FUN_004922c0` daily-event probe + group-6 cutscenes, the mid-transition bg
  path, the variant-1 (ichiba, dest 1) set. Follow-on traces queued: leaving the guild
  (bread cutscene) + returning to Recettear (Tear cutscene) — same `FUN_004922c0` machinery.
  **The 2 "worldmap" sounds
  (`se_019_id0150`+`00re_sys09`) ✅ DONE 2026-06-10** (`172ecc9`): a retail audio-hook
  `ret_va` backtrace named the caller `FUN_0048670f` (the HOUSE/shop update, NOT the
  worldmap) — they're the **first-shop-door-exit** SE (the tutorial trip out), played with
  the dissolve fade. The port armed the fade+flags but stubbed the SE (`PORT-DEBT(door-SE)`);
  un-stubbed (RNG-neutral). `audio_diff` merchants-guild: **missing 2→0, track ALIGNED (9
  sounds)**. Tooling: audio se_play hooks now record `ret_va` (names a SE's caller).
- **NEXT ARCS:** finish item-display gaps → **merchant's guild scene** (now active,
  above) → town scenes off the world map (world-map backlog itself CLOSED 2026-06-08,
  bit-clean f16→638). Trace-studio v2 **Phase 5** (New-Game cross-replay: retail
  intro-video force-skip D4 + the prologue mid-load actor-spawn gap,
  `findings/conversation-pose-driver.md`) stays queued — hardest, last.
- **Deferred (polish pass):** faint ambient particle dots (user ref-crops
  2026-06-05); next-line "book" arrow anim frame (draw from per-script-reset
  `rt->blink`, add to `{phasepin}`).
- **Tooling fix (2026-06-10): the caprange.start>0 full-white diff.** A `window_start>0`
  session (`merchants-guild`) showed a fully-white diff over a 1:1 world map — only the
  RETAIL frames were renumbered into label space, the PORT stayed 0-based, so the
  label-keyed diff mispaired by `window_start` (the ordinal video scrub was fine, hiding
  it). `convert.renumber_retail`→side-agnostic `renumber_to_label`, now run on BOTH
  sides; `test_trace_studio_renumber.py` guards it (broken→fixed contrast). `7a7e280`.
  Any session captured at `caprange.start>0` BEFORE this needs a recapture for a true diff.
- **Tooling fix (2026-06-11): the seam-misaligned diff VIDEO.** Across a kept-count load
  seam the port|retail|diff videos have different label↔ordinal maps, so seeking all three
  to a shared scrub ordinal landed each on a different LABEL (the diff "diffing a different
  frame" while port/retail read 1:1). Fixed by seeking each panel to ITS frame for the
  cursor's label (`align.videoFrameOfLabel` + `manifest.frame_labels`); `dbd83eb`,
  user-confirmed. Sessions captured BEFORE this lack `frame_labels` → recapture (or patch the
  manifest) for the per-panel-aligned scrub; the diff DATA/ribbon were already label-true.
- **Tooling (2026-06-09 cleanup, audit T1/T2/T3/T8/T11 — all landed):**
  `trace_studio triage <session>` = one-command divergence report (diff curve
  gt8 metric → first/worst ordinal → state row → verdict → field-timeline);
  working-trace **lint + canonical auto-pin** at capture (pins are now mechanism,
  not prose — `--no-auto-pin` for deliberate unpinned studies); session
  **coordinate contract unified** (frames on BOTH sides label-named, diff
  label-keyed, state ordinal-keyed — the C3a abs-vs-ordinal trap is dead); CI
  **x87 FP guard** (`tools/ci/no_sse_math.py`); **phase-state census**
  (`tools/phase_census.py` + the `{memsnap}` op) — the pin-completeness gate.
  Sessions captured before today lack the `gt8` diff stat until recaptured.
  **Census's first lead:** the 目玉 sparkle overlay-slot array
  (`g_scene1_overlay_slots`) carries load-dependent particle residue even under
  the canonical pin (`{phasepin}` re-seeds RNG + zeroes sim_frame but doesn't
  clear pre-pin particles) — sub-visible, accepted-known; fold into `{phasepin}`
  in a sparkle-parity pass if it's shown to matter. `findings/phase-state-census.md`.
  TODO: run the pinned RETAIL census (Frida host) + census other scenes.
- **Audio-trace diff (new parity pillar, 2026-06-10):** detect sound divergences
  from traces alone, no booting the port — `tools/audio_diff.py` compares port↔
  retail sound triggers by identity+count (phase/load-skew-immune). Foundation:
  frame-stamped port `audio.jsonl`, retail voice/file-SE hook + `se_NNN_idXXXX`
  names, studio sessions now carry `port/audio.jsonl`, folded into `trace_studio
  triage`. First catch = item-display queue #7. `findings/audio-trace-diff.md`.
- **Authoritative parity facts:** `findings/confirmed-parity-ledger.md`. A tooling
  "divergence" on a human-confirmed-1:1 item is a lead to investigate, NOT an
  assumed regression.
<!-- FRONT:END -->
