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
  **→ ACTIVE ARC (2026-06-11 PM): the COMPOSITE guild trace
  `guild-skip-dialogue-talk-leave-20260611-204101`** (served :8778; the fresh recording the
  bread cutscene needed + much more). Walks: door→leave→guild→ESC-skip the first-visit cutscene
  →Talk submenu (no-wrap nav, first dialogue clears the New badge, re-enter shows it persists)
  →try-leave tutorial (nothing bought)→buy 1 sword→leave→BREAD cutscene (iv1_9)→return to
  Recettear (Tear cutscene)→walk. Working it gap-by-gap; **re-window per gap** — anchor the
  `{caprange}` right after the FIRST `LOADING_END` (house freeroam, `window_at_freeroam`=True so
  recapture won't self-heal), count spans forward through suppressed loads; pin `{phasepin N}`+
  `{rngseed [N,19937]}`+`{tutloadpin 8}` (mirror the buy-flow trace). Caveat: capturing from a
  *distilled* recording places the caprange in the FINAL segment (0 frames) — hand-place it; and
  retail load-stretches the prologue (≥40000 `--retail-max-frames` to reach a guild window).
  **GAP 1 — ESC skip-confirm box ✅ DONE + verified 1:1 2026-06-11** (`f26eb40`): port showed a
  half-expanded textless panel, cursor stuck on Yes. Cause = mode-6 ran the dialogue tick WITHOUT
  the skip modal ("prologue-only") so the choice box never ticked to cb_active==4, AND gated the
  shared-cursor anim tick on !busy() so the box cursor never slid. Fix = merge mode 6 into the
  INGAME skip-modal block + tick the cursor when `skip_event_open()`. Box geometry/text/Yes-No,
  cursor slide Yes→No→Yes, and confirm Yes→skip→guild menu all match retail (direct frame compare
  at the box; the studio diff is kept-count-seam-shifted — accept). esc-skip-event.md "Guild".
  **GAP 2 — Talk submenu (`FUN_004922c0` mode 2) ✅ DONE + studio-verified 1:1 2026-06-11**
  (`88b666a`): A on Talk now opens the submenu (was stalled — mode 1 armed c1c=1 but the nav
  gate blocked the ramp, so the submenu never opened). Ported the mode-1 c1c-ramp→mode-2 flip,
  the mode-2 state machine (c1c slide in/out, no-wrap U/D nav over 7 rows = 6 topics + "Never
  mind"; A→topic dialogue iv1_0a/0b/0c/0e/18/19 fired via `scene1_intro_dialogue_start_single`
  + per-topic seen-flag `save[0x2bc98+i]`; B / A-on-"Never-mind" close), the main option-list
  c1c slide/cull (the rows collapse + the Talk row heads the submenu as it opens), and the
  submenu render (6 sliding topic rows + per-topic New sparkle + the up/down scroll arrow).
  Re-windowed the composite to `caprange [250,1250]` (labels ~903-1140 = the submenu) +
  recaptured both sides: the studio diff is **bit-identical** at the open (label 1015, cursor
  "What is the guild?") AND the scrolled state (1140, cursor "Never mind", no-wrap held, "Never
  mind" correctly badge-less) — the ONLY residual is the cursor-bob + New-sparkle PHASE (the
  load-seam phase pillar, same as the main menu; accept). The 17 audio "misses" are all
  POST-window (retail 15984-18750 = the buy/leave/bread the port stalls at — the Leave handler
  is the NEXT gap); every within-window nav/select SE fires. PORT-DEBT(talk-confirm-flash): the
  c20 selected-row brightness pulse (Ghidra-dropped FPU amplitude), settled to grey-127 like
  the main-menu pulse. **✅ USER-CONFIRMED 1:1 2026-06-11** ("can confirm the talk menu looks
  correct") — parity ledger (served :8778; backup `edit.trace.jsonl.gap1-escskip.bak`).
  **GAP 3 — hand cursor vanished after an ESC-skip ✅ FIXED 2026-06-11** (`a7209a2`,
  user-flagged: "skip a Talk dialogue with ESC → the cursor disappears until I press
  arrow/go back; retail doesn't"). The choice box's close snaps the SHARED cursor
  offscreen to (0,-64); Phase C had dropped the engine's resume-state snapshot/restore
  as PORT-DEBT (harmless for the prologue, wrong for the Talk submenu where the cursor
  sits on a row). Ported `FUN_0046c2cb` arm-snapshot + `FUN_0046c320` close-restore
  (snap back if visible, else hide) + `FUN_00435644` capture helper; host-tested
  (`test_skip_event` ×3). Recapture (`--only port`) shows the SAME latent gap on the
  first-visit-cutscene skip is now closed too: **direct frame compare label 800 (menu
  resting after the skip, pre-nav) = port == retail, cursor on Buy** — where the old
  offscreen-snap diverged (the seam had hidden it from GAP-1's at-the-box diff). **The
  actual Talk-dialogue ESC-skip is NOT in this recording (user found it by playing) but
  ✅ USER-CONFIRMED 1:1 2026-06-11** ("can confirm the bug is fixed") — parity ledger.
  esc-skip-event.md "Guild (mode 6)" gap 3.
  **GAP 4 — leave-guild TRY-LEAVE "no purchase" reminder (iv1_9) ✅ DONE + PHASE-CLEAN 2026-06-12**
  (`b5ba796` dispatch + `aa773d0` menu-backdrop). **Structure (user-clarified): TWO separate
  dialogues** — (a) **iv1_9** = the Tear *"You have not yet purchased anything! Please properly
  stock up…"* reminder, fired on **try-leave BEFORE buying** (overlay, you stay in the guild);
  (b) the **proper "bread" cutscene = iv1_16**, fired on the **actual leave AFTER buying** (the
  c2c/c28 transition → world-map). GAP 4 ports **(a)**. `A` on **Leave** fires
  `scene1_intro_dialogue_start_single(1,9)` (= `FUN_0044ba2c(1,9,0)`, `LAB_00492ad7`) when the
  first-leave flag (`save[0x2bc5d]`/`DAT_0450f3f5`) is clear, + SE 0x13d. Re-windowed the composite
  to `caprange [250,4300]`; **focused `flow_diff --align-anchor TEXT_ANIM_START --frame-from 17150
  --frame-to 17430` (281 frames) = ✅ PHASE-CLEAN — fade/dialogue(box_open/reveal/line_row/st5)/
  rngcalls/raw-rng all bit-exact** vs retail (retail arms it @frame17181; order: first-visit @14695
  → Talk-topic @15858 → reminder @17181). **Menu-backdrop fix (`aa773d0`):** retail keeps the main
  menu (Buy/Sell/Talk/Leave + the "come back any time!" bubble + cursor) rendered BEHIND the
  reminder — the port hid it (the menu-UI draw was gated on `!dialogue_busy()`, right for the
  first-visit cutscene but wrong here since iv1_9 fires from the menu). Now drawn when
  `!busy() || (mode==1 && entry_tick>0xe)` (first-visit + mode-2 Talk topics unchanged). **The
  flow_diff verdict MISSED this** (render-side, not dialogue_tick) — the **visual frame compare
  caught it**; port label 3010 now matches retail (feed: "iv1_9 menu backdrop fix"). **The studio
  PIXEL diff is seam-unusable here** (8 load-seams ⇒ kept-count port≈3860/retail=3740 ⇒ port a few
  frames ahead at each label; verify via the anchor-aligned flow_diff + content-matched frames, not
  the pixel curve). **✅ USER-CONFIRMED 1:1 2026-06-12** ("looks correct to me") — parity ledger. RE:
  `merchant-guild-RE.md` "Leave dispatch RE". **PORT-DEBT(guild-leave-transition):** the proper
  iv1_16 bread cutscene = the flag==1 (post-purchase) Leave path = the c2c/c28 transition → iv1_16
  fade + world-map swap + the return-to-Recettear Tear cutscene (+ the buy-commit flag-set
  `save[0x2bc5d]=1`, also PORT-DEBT, so the port currently re-fires iv1_9 on the post-buy leave) —
  the follow-on gap (the user-confirmed next target).
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
- **Deferred (polish pass) — LOADING-SCREEN FIDELITY (user direction 2026-06-11):**
  replicate the way retail loads things in — its fades + load screens — for an
  authentic loading feel, instead of the port's fast-load/suppress. First concrete
  instance (user-flagged, NOT yet RE'd): ESC-skipping the guild first-visit cutscene
  (→Yes) plays a brief fade-to-black in retail (likely a load screen) the port skips
  straight past; likely lead = the stubbed skip-teardown `FUN_00473c03` (the
  camera/player reseat the port marks `PORT-DEBT(simplified)` in
  `scene1_intro_dialogue_skip_to_end`) and/or a real post-skip asset reload. Keep
  this in mind whenever touching scene transitions / dialogue-skip teardowns /
  `{caprange}` load seams — it's the same class as the kept-count seam these traces
  fight. (Confirm + RE it before it graduates to a finding/quirk.) **A concrete fade-system
  component is now RE'd (2026-06-13, from the render-program drill):** the screen-blackout draw
  `FUN_00453d9c` (gate `DAT_0438bf74`, armed by `FUN_00452809`, tex `bmp/system.bmp` =
  `g_sysassets.system_bmp`, ALREADY loaded; draw+gate unported) — blits a full-screen opaque-
  black quad after the scene block / before the dialogue every frame the blackout flag is set.
  Wire it here (it's invisible until a transition actually blacks the screen). quirk §122.
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
- **TRACE STUDIO v3 (new parallel arc, 2026-06-12) — "capture the render program,
  not its output".** Radical rebuild to kill v2's three pains (slow captures, sync
  whack-a-mole, fragile diffs) at the root: a shared **proxy `d3d8.dll`** captures the
  exact D3D8 command stream + dedup'd resources (both sides) → **replay re-renders each
  frame bit-exactly** (display + oracle) → **sync by stored `(anchor,offset)` identity**
  → integrated **semantic diff** (which draw/state/texture). **P0 ✅ GO (`65bcdd7`)** +
  **P1 ✅ DONE (2026-06-12):** a real HOUSE **3D** frame captured **8797 load frames deep**
  replays **BIT-EXACT** (0 px; 55 VB/IB indexed + 31 UP draws, 46 res, ~26 MB). Got there via
  the **deferred-snapshot two-section container** (per-frame calls buffer in RAM + drop every
  Present; resources snapshot only at finalize ⇒ the load costs zero snapshot work, 963 MB
  balloon + throttle gone, no stale/pointer-reuse bug) + the **device-state-shadow preamble**
  (R4 inherited-state was real for 3D — overbright/black-blended without it; fixed by
  shadowing every scalar Set + emitting at each frame boundary). Title regression still
  bit-exact. Built in isolation under `tools/trace_studio_v3/` (`inspect_cap.py` = container
  analyzer); **v2 stays the working tool until v3 is proven + archived.** **R2 ✅ DONE
  (2026-06-12, `fe3722a`) — the SAME proxy d3d8.dll captures BOTH sides.** The Windows loader
  picks up the app-local proxy for the SteamStub-unpacked retail exe (even from a
  `\\wsl.localhost` UNC path under Frida spawn — unpack doesn't interfere), and the retail
  **title** frame replays **0 px / 0 byte** vs the proxy reference (port regression still
  bit-exact). Findings: retail's OWN `recet.ini` read **fails over the `\\wsl.localhost` UNC
  path** (`GetPrivateProfileIntA` can't read it ⇒ `screen` defaults to 0 ⇒ 640×480), so retail
  is **pinned to 1024×768 via the agent's `force_resolution` hook** (the same mechanism v2
  scenario-test uses) — both sides now 1024×768 + bit-exact; retail's **backbuffer is
  non-lockable** (flags=0x0; port 0x1) ⇒ CopyRects-via-sysmem readback (shared proxy+replayer
  helper). Config via `v3proxy.cfg` next to the dll (env vars don't cross to the Frida-spawned
  exe); kill-safe unbuffered log; driver `r2_retail_probe.py`.
  **P1 TAIL — full-extent MULTI-FRAME capture + content-hash dedup ✅ DONE (PORT, 2026-06-12,
  `da5f601`):** the single-frame proxy is generalized to a windowed multi-frame container (the
  storage model the P2 retail-once-cached/sliced loop needs). A real HOUSE 3D free-roam WINDOW
  of **48 consecutive frames** (caprange LOADING_END+120..168, past an ~8800-frame load)
  captures into **ONE 27.6 MB container** and **every frame replays 0 px / 0 byte — 48/48
  BIT-EXACT**. Dedup win PROVEN: content-hash (fnv1a-64) resource dedup keeps the store at one
  frame's worth (`48 res total` CONSTANT across all 48 KEEP lines — 48 frames = 27.6 MB where
  the unique resources alone are 26.6 MB; adding 47 frames is +1 MB of call deltas). `write_frame`
  per kept frame writes `[new RES][preamble][calls][Present]`, fflush per frame (kill-safe);
  GetBackBuffer keeps every caprange frame (port MULTI mode); retail capframe path stays
  single-frame (R2 regression bit-exact); replayer renders any kept-frame INDEX. Committed
  driver `port_capture.py`; `inspect_cap.py` multi-frame aware.
  **RETAIL present-WINDOW keep mode + ANCHOR-RELATIVE arming ✅ DONE (2026-06-12):** the retail
  full-extent CAPTURE MECHANISM is proven end-to-end (4 commits). Retail has no per-frame readback,
  so the window is addressed by PRESENT-COUNT (`capframe`/`capcount`) — keep every present in
  `[start,start+count)`, the retail counterpart of the port's GetBackBuffer trigger. Proven
  bit-exact: **PORT** `--window 944:44` (re-captures the HOUSE 3D caprange via present-count →
  44/44; per-frame call-bytes CONSTANT ⇒ the new per-present `cb_reset` fixes a latent frame-0..N
  call-accumulation); **RETAIL** `--window 120:48` (48-frame TITLE → ONE 8.7 MB container, 48/48,
  48 DISTINCT frames ⇒ a real multi-frame test, not 48 copies). Anchor-relative arming (so a
  post-load window lands despite the nondeterministic load-stretch): the `OrV3ArmWindowAt(start,
  count)` proxy export (WINAPI/`--kill-at` undecorated; `--arm 120:48` via Frida NativeFunction →
  48/48, proving the ABI + that d3d8 is a static import) + the **agent IN-PROCESS arm**
  (`config.v3_arm={anchor,offset,count}`; `sendAnchor`→`v3ArmOnAnchor` calls the export the first
  time the anchor fires — gated/no-op for v2; `--arm-anchor BOOT+120:48` → "armed BOOT@frame 0 →
  [120,168)" → 48/48). Drivers: `retail_capture.py` (`--window`/`--arm`/`--arm-anchor`) +
  `port_capture.py --window`.
  **HOUSE-DRIVE retail full-extent capture ✅ DONE — P1 COMPLETE (2026-06-12, `b034849`):** drove
  the REAL retail exe through the save-load to the HOUSE (save-virt + input-segtrace) and armed the
  proxy at `HOUSE_FREEROAM+120` for a real post-load 3D free-roam window — **48/48 BIT-EXACT**,
  29.3 MB. **HOUSE_FREEROAM fired at retail present 13912** (the ~13k-frame load-stretch E3 predicted
  vs the port's 824), the agent armed `[14032,14080)` IN-PROCESS 120 frames ahead. Three gated
  pieces: (1) `frida_capture` `v3_arm` field on `CaptureConfig`/`run_capture` → `init_cfg` (implies
  `anchor_trace`; None ⇒ v2 no-op) so the FULL scenario machinery carries the anchor-arm; (2) the
  proxy **`armwait=1`** cfg — suppresses the GetBackBuffer MULTI keep-trigger through the long
  pre-anchor load so a stray readback can't mis-keep a load frame (proxy idles until the in-process
  arm; port MULTI path unaffected — `port_capture` re-ran 48/48); (3) `house_capture.py` (load
  segtrace scenario → resolve `{savefile}` → stage proxy+armwait → `run_capture(... v3_arm ...)` →
  pull + replay every frame bit-exact). HOUSE_FREEROAM is the robust anchor (fires ONCE, same frame
  as the final LOADING_END ⇒ `HOUSE_FREEROAM+120` == the port's `LOADING_END+120..168`). **The
  retail full-extent capture mechanism is proven end-to-end on a real 3D scene. Next → P2:** the
  content-addressed slice cache (capture retail once, slice sub-windows zero-re-drive) +
  window-aware early-exit (kill the post-window over-run) → sync-by-identity (E3 `(anchor,offset)`
  join as the real alignment authority). Plan: **`plans/trace-studio-v3.md`** (P2 section).
  **P2 IN PROGRESS (2026-06-12) — window-aware early-exit + sync-by-identity + slice cache, all
  proven on real port+retail HOUSE captures.** (1) **Window-aware early-exit (`1f54dd8`):** the
  agent schedules shutdown 2 frames past the armed window end (proxy present-counter == agent
  frame-counter, the same Present clock), reusing the `max_frames_reached` teardown — the HOUSE
  drive stops at frame 14158 instead of grinding to max_frames 22000 (~7800 over-run frames gone),
  **48/48 BIT-EXACT in 53 s** (was multi-minute); a no-op for v2 (gated on an armed v3 window).
  (2) **`orv3.py`** Python container reader + **bit-exact slicer** — re-emits any sub-window `[a,b)`
  as a standalone container (pulls forward dedup'd resources first defined before the slice);
  proven: slice `[10,20)` frame 0 replays **0 differing bytes** vs the original ref. (3)
  **`v3cache.py`** content-addressed cache + **STORED identity** — copies the transient
  `%LOCALAPPDATA%` capture into a keyed entry (`runs/studio-v3-cache/<scen>-<key>/{port,retail}/` +
  `v3meta.json`); the key hashes only retail's pixel-determining inputs (trace + arm) so a port fix
  never invalidates the retail cache. (4) **`orv3_sync.py`** the **sync-by-identity JOIN** (the v3
  alignment authority): **48/48 ALIGNED, 0 gaps** on the real HOUSE window — port present 619..666,
  retail 14108..14155, a **+13489-frame load stretch**, every frame paired by `(HOUSE_FREEROAM,
  offset 120..167)`; **naive absolute-present pairing = 0/48** (the v2-class frame-number scheme is
  hopeless across the load stretch). Writes `pairs.json` (computed once, for the future diff/seek/
  state/marks). Both drivers (`house_capture`/`port_capture`) now auto-cache with identity.
  (5) **`orv3_slice.py`** slice-serve a cached sub-window with **ZERO re-drive** (the cache win):
  re-window to offsets 130..149 → slice BOTH cached sides (instant) → join → **20/20 ALIGNED**
  (retail slice 20/20 bit-exact); a re-window that cost a multi-minute retail drive in v2 is now
  instant. (6) **`orv3_window.py` the auto-drive WINDOW LOOP — P2 ✅ COMPLETE (2026-06-12).** One
  command (`orv3_window.py <scen> --window OFF:COUNT`) ties it together: per side, `find_extent`
  asks "is the window already in a cached full-extent for (scenario, anchor), from the CURRENT
  trace?" — HIT ⇒ slice (instant, zero re-drive), MISS ⇒ drive the full caprange extent then slice;
  then `sync_entries` JOINs → pairs.json + verdict. Two guards keep it honest (kill the v2
  "filenames silently lie" class): a **dir-key re-hash** (reconstruct the arm from the stored meta,
  require `cache_key(current_trace,arm)`==the dir key ⇒ an edited trace can't match a stale entry) +
  a **port-exe-mtime freshness** check (a rebuilt `openrecet.exe` ⇒ cached PORT pixels stale ⇒
  re-drive port, retail untouched). Proven on the real HOUSE cache: re-window `130:20` = **pure
  slice, nothing re-driven, 20/20 ALIGNED**; `120:48` = "full-extent (no slice)" 48/48; `110:20` =
  clean out-of-extent error; `--force-port` = **"drove only: port"** (port re-driven 48/48, retail
  sliced 20/20, joined 20/20) — v2's `--only port` loop, now immune to window changes. Lookup logic
  hermetic-unit-tested; `slice_entry`/`sync_entries` factored out of the CLIs (behavior-preserving).
  **P3 VIEWER — PIVOTED to NATIVE, N0/N1/N2 DONE + user-confirmed (2026-06-12).** A web
  prototype (PNG-bake + `orv3_serve` + preact `web/`) works but the user rejected it (the
  bake reintroduces v2's pains: stale PNGs, ~150 MB dup pixels, ~150 ms/frame encode that
  caps faster-than-realtime replay); kept only as a fallback. The native viewer
  (`tools/trace_studio_v3/viewer/`, C++/Dear ImGui/d3d9, mingw **i686** to match the real
  d3d8) replays the container ON DEMAND — the container is the only artifact. **N1
  `replay_core.{c,h}`** = the proven replayer factored RESIDENT (device + 26 MB resources
  created once; render any frame on demand) — 48/48 HOUSE regression still bit-exact, and
  the perf proof that settles the wiring question: **resident per-render = 1.42 ms best**
  (1083 calls + readback) vs ~620 ms cold (~200×) ⇒ 60 fps+ scrub, faster than realtime.
  **N2** = the full **port|retail|diff** viewer reading `view.json` (orv3_view `--native`:
  the identity-join timeline, no bake): panels replayed live (diff CPU-computed, gt8 same
  law as pixel_diff), diff ribbon (heat/click-seek/worst-next), per-frame state table
  (present/draws/calls retail-vs-port, diff-highlighted — surfaces the **125-vs-98 draw**
  divergence that is bit-exact in pixels: identical output from different render programs,
  invisible to v2), scrub+keys+panel-toggles, honest-gap panels, **synced entirely by
  stored identity** (no align/renumber/seam). Stack pinned via `flake.nix`
  ($IMGUI_SRC/$NLOHMANN_JSON_INC); headless `--shot out.bmp` self-verifies with no display.
  d3d8→d3d9 bridge gotcha: alpha-less backbuffer needs an **X8** texture (A8 ⇒ transparent).
  **N3 SEMANTIC DIFF/PICK LAYER ✅ DONE (2026-06-13) — "which draw/state/texture
  differs", the v2-blind divergence.** Landed (a–e, all committed + feed-demoed,
  user "demo looks great"): **(N3a)** `replay_core` **render_range(idx,lo,hi)** — draw
  isolation: issue only draws [lo,hi) (state/clear always issued); [0,K)=prefix,
  [J,J+1)=one draw over the clear; regression bit-exact. **(N3b)** `orv3_draws.py` —
  enumerate a frame's draws WITH bound state (tex/VB/IB/FVF + render/stage states),
  cross-side-keyed by CONTENT hash; **material_diff** verdict ALIGNED/BATCHING/DIVERGENT
  (per-texture triangle totals, batching-robust), baked into view.json. **(N3c/e)** native
  viewer gains a **draw-step** slider (prefix build-up), **solo** toggle (isolate one
  draw), a **draw-program panel** (verdict + divergent textures), and **pixel→draw pick**
  (click a panel pixel → linear-scan prefixes → the draw that painted it, auto-solo'd).
  **(N3d)** `orv3_window --view/--launch` = the one-command loop (drive/slice/sync →
  view.json → viewer), 1.2 s on the cached HOUSE window. **THE HEADLINE FINDING (HOUSE,
  v2-invisible):** the port-98/retail-125 draw gap on a PIXEL-bit-exact frame = **26
  batching splits** (retail splits draws the port batches; per-texture triangle totals
  IDENTICAL) **+ 1 genuinely-extra retail draw** (texture `ea99`, 80 tris, drawn FIRST,
  SRCALPHA-blend with effective src-alpha 0 / ZENABLE off ⇒ paints **0 px**, proven in
  true isolation; the port omits it). Pixels ALIGNED, render-program DIVERGENT.
  **Perf fix (necessary for scale):** the material-diff bake was re-hashing the ~26 MB
  resource set per-frame ×96 with pure-Python fnv1a (2.5+ MIN); a shared per-container
  C-speed blake2b `ResHash` made it **0.41 s** (350×).
  **N4 THOUSANDS-of-frames stress ✅ DONE (2026-06-13)** on the new **`guild-ui-flow`**
  scenario (the v2 buy-flow session's trace promoted; caprange [330,2600], multiple
  mid-window load seams). Two scale walls fixed first, HOUSE-regression-proven: **hash
  refs + one-process resident batch verify** (`92cce65` — 8 GB/side of raw refs → a
  155 KB v3refs.txt; `replay --verify-hashes`) and **per-frame MULTI-ANCHOR identity +
  arm-spec-stored meta v2** (`1e03b72` — the single-anchor `offset0+index` identity
  would silently mispair everything past the first seam, and the kept-count-derived
  cache key could never re-match a suppressed-load window; keys now resolve per frame
  from the stored anchor stream, aliases tie-break to the base anchor). **Numbers:**
  port 1785 kept/58 MB/1785 bit-exact; retail **2600/2600 kept**/91 MB/**2600/2600
  bit-exact** (raw pixels = 7.8 GB); join **1784/1785 paired** across a **+13,272**
  load stretch and **63 segments**, every gap named (798 = port TAS ends pre-window-end,
  18 = suppressed load screens, 7 = one alias skew); naive pairing 0. **Pairing proven
  visually: paired frames deep past seams are bit-identical (gt8=0)**; menu pair = the
  known accepted phase residue only. Viewer opens 2601 columns in ~15 s. **Two NEW
  v2-invisible findings:** retail draws a fullscreen SRCALPHA overlay quad LAST
  (tex `…9fd8`, ~1094 columns, sub-gt8 net effect — port omits; RE the engine layer) +
  the port double-draws the guild bg (tex `…2780`, 2 draws vs 1, ~1076 columns —
  invisible overdraw, port cleanup lead). **Live viewer at 2601 columns
  ✅ USER-CONFIRMED 2026-06-13** ("works perfectly and scrubs instantly").
  Pixel→draw pick live click-test **also user-confirmed** (the F un-pick keybind).
- **VIEWER NOTES + CROP REGIONS ✅ DONE + USER-CONFIRMED 2026-06-13** (`db28c34`,
  `f20b5ea`) — the v2 `edits.jsonl` notes loop, native, and **the last v2-parity gap →
  v2 is RETIRED as the working tool** (user call 2026-06-13: "retire all v2 studio
  stuff … archived unless we hit blockers"). In the native viewer: **note mode (m)** →
  drag a crop box on any panel (or **note frame**) → type a note; notes overlay as green
  boxes pinned to their column by **identity label** (stable across re-windows) + a
  seek/del list. Persistence dodges the UNC-write limit (the Windows viewer can't
  fopen-write `\\wsl.localhost`): notes go to a **Windows-local**
  `%LOCALAPPDATA%\openrecet\v3\notes\<scenario>.json` (view.json carries its `notes_path`;
  `v3cache.notes_file`). **`orv3_notes.py`** reads them on WSL — `list` prints the flags,
  `--render [--id N] [--feed]` replays the flagged frame port|retail|diff, crops to the
  box, outlines it, composes a PNG (+feed) so Claude SEES the flag. Live drag→save→read
  round-trip user-confirmed; a window-vs-client backbuffer-size bug (cursor offset +
  squished pixel font) fixed (`f20b5ea`, user-confirmed). **The parity loop now runs on
  the v3 native viewer:** `orv3_window <scen> --window OFF:COUNT --launch` → drag notes →
  `orv3_notes.py <scen> --render`. Detail → PROGRESS + `plans/trace-studio-v3.md` P3.
  **Follow-ups (perf, the NEXT arc — user-sanctioned 2026-06-13):**
  **CACHED re-window loop ✅ DONE 2026-06-13** — parse-once container handoff
  (`v3cache.LoadedSide`/`load_side`/`as_side`: parse meta+container+identity-index ONCE,
  thread the SAME object through sync→view instead of re-parsing the 91+58 MB containers
  ~3× per side) + a **material aggregate bake** (`orv3_draws.material_agg` →
  `{tex_hash:[tris,draws]}` directly, skipping the Draw objects / geo_hash UP byte-loops /
  rs-tss copies `material_diff` discards). Both **behavior-preserving** (view.json +
  pairs.json byte-identical pre/post). Numbers (2600-col guild pair): per-column bake
  **6.71 s→0.36 s (~18×)**, sync+view compute **8.98 s→1.40 s (~6.4×)**, end-to-end loop
  10 s→7 s (rest = fixed nix/python startup); the "~5 min" was a stale pre-ResHash figure.
  Guards: `test_material_agg` + `test_load_side`. (A baked-draws cache was the alt lever —
  unneeded now.) The other big drive-time follow-up — skipping the v2 PNG/montage bake on a
  v3 retail drive (~5 of ~13 min) — already landed in `4f7cfed`.
  **Lazy viewer metric precompute ✅ DONE 2026-06-13** (`perf(studio-v3): viewer — background
  …`) — the diff-metric fill (2 renders/col) moved off the open path to a background pump
  (~8 ms/UI-frame): the window is responsive instantly + the ribbon colours in over ~1-2 s
  (was ~15 s of black at 2601 cols); the `--shot` path keeps the synchronous full precompute.
  **GAME-STATE PANEL + `--state` capture ✅ DONE + measured 2026-06-13** — the v2 StatePanel,
  native + identity-keyed. `orv3_window/house_capture/port_capture --state` caches each side's
  `call_trace.jsonl` (the 4 once-per-frame VAs); `orv3_state.py` keys it by `(anchor,offset)`
  (call-trace frame == present-count, verified); the viewer shows engine fields (rng/rngcalls,
  player+companion px/py/anim, menu, dialogue) port-vs-retail, diff-highlighted (f32-normalised
  ⇒ a red row is REAL — a 1-ULP cx gap or the +737 rngcalls phase offset), filter + diffs-only.
  **Opt-in** (negligible: the probes are window-gated by the `{calltrace}` op — HOUSE 98 events /
  36 KB, NONE in the load-stretch; +60 ms / ~1%). Measuring it **surfaced + fixed a latent
  waste**: a v3 drive auto-loaded the heavy ~1979-VA call-graph (120k events / ~11 MB, NEVER
  cached) from the trace's `{calltrace}` op — now NOT auto-enabled on a v3 drive (lean by
  default), kept only with `--state` (to window-gate the 4-VA probes). **`flow_diff --verdict
  --align-field db054` runs UNCHANGED on the v3 cache** (HOUSE = ✅ PHASE-CLEAN, rng 48/48
  bit-exact, rngcalls per-frame match) — the whole RNG/phase determinism check, drop-in; the
  `flow_diff` suite (`--field-timeline`/`--rng-drill`) reads the v3 traces as-is. **CLAUDE.md
  now points the parity loop at v3** (v2 retired). **P4 CORE CHECK ✅ PASSED 2026-06-13:** the v3
  HOUSE verdict (`flow_diff --verdict --align-field db054` on the `--state` cache) = ✅ PHASE-CLEAN
  (33 counters bit-exact, rng 48/48), matching v2's PHASE-CLEAN on the same pinned HOUSE; full
  formal send-off (byte-level v2-vs-v3 call_trace on a rich session, then the v2-code archive
  move) is the deliberate user-overseen remainder. Plan: `plans/trace-studio-v3.md` P3/N4/P4.
- **RENDER-PROGRAM DRILL (using the v3 draw-program panel) — new active parity thread,
  2026-06-13.** First parity fix found purely from the render-PROGRAM (a divergence on
  pixel-bit-exact frames, v2-invisible). **Guild conversation bg+keeper double-draw ✅ FIXED
  (`2a2d84d`):** the port drew bg_guild twice (scene render + the conversation's own bg) +
  a fully-overdrawn keeper on all 1076 guild conversation frames; retail skips the whole
  mode-6 scene block when a full-bg conversation covers the screen (`FUN_004547ab` gate
  `DAT_0438b1c8 && FUN_0046c869()`=n_bg). Gated bg+keeper on the existing
  `scene1_intro_dialogue_covers_screen()`; bg draws/frame `{0,1,2}`→`{0,1}`, **zero pixel
  change** (pre/post fnv64 pixel-hash identical at 1749 identities), per-conversation-frame
  port-only draws 2→0. `findings/merchant-guild-RE.md` "Render-program drill", quirk §122.
  **Same class FIXED in the INGAME path (`7d119af`):** retail's `FUN_004547ab` skips the
  WHOLE scene block (3D scene + HUD + overlay + cc04) under a covering cutscene; the port
  only gated the HUD (right when the 3D walkers were stubs, stale now the 3D render is live).
  Gated `scene1_render_camera_setup`+overlay+`display_menu_render` on the same
  `covers_screen()` (one `covers` local); pixel-safe by construction (covers ⟺ full-screen-bg
  ⟹ scene covered) + verified BYTE-IDENTICAL on `intro-dialogue-lines` (16/30 pass-fail same
  pre/post; the 30 fails = pre-existing deferred iv1_2 gaps, covers=0 ⇒ no-op there). Formalises
  retail's structure + prevents the over-draw for any INGAME cutscene over a loaded 3D scene
  (the traced covering cutscenes — bedroom iv1_1 — are pixel-identical here; the *measured*
  over-draw removal is the guild's).
  **The covers_screen scene-block-gate class is now SYSTEMATICALLY COVERED** across the
  port's implemented scene modes (INGAME mode-1 + guild mode-6 fixed; worldmap mode-8 draws
  no dialogue so N/A; the guild gold/clock HUD's `!busy()` gate is correctly call-trace-
  evidenced — FUN_00406d50 fires only pre/post, not a divergence). The `display_menu_render`
  (cc04 / guild buy-list, FUN_0046b00a) render program is clean modulo phase (the guild drill).
  **Two draw-program leads** (both v2-invisible): (a) the retail-only **screen-blackout layer**
  (tex `9fd8`, fullscreen `0xff000000`, covered by the opaque bg ⇒ 0 px) — **SOURCE NAILED
  2026-06-13: `FUN_00453d9c`** (runs unconditionally in `FUN_004547ab` after the scene block,
  before the dialogue ⇒ FIRST draw when the scene is skipped), gate `DAT_0438bf74` (armed by
  `FUN_00452809`), tex `DAT_073aa188`=`bmp/system.bmp` (the port ALREADY loads it as
  `g_sysassets.system_bmp`; the draw + gate are unported). It's a fade/transition blackout ⇒
  **port it in the LOADING-SCREEN-FIDELITY pass** (the user-direction deferred item), where the
  transition system is RE'd; quirk §122 + merchant-guild-RE.md (2). (b) the **HOUSE 3D batching
  divergence** (port 98 vs retail 125 draws; 26+
  batching splits + the `ea99` 80-tri src-alpha-0 first draw — a separate, larger effort).
  The drill workflow: `orv3_window <scen> --window OFF:COUNT --view` → `orv3_draws.py
  <port.bin> <pf> <retail.bin> <rf> --list` (cross-side content-keyed draw diff).
- **Authoritative parity facts:** `findings/confirmed-parity-ledger.md`. A tooling
  "divergence" on a human-confirmed-1:1 item is a lead to investigate, NOT an
  assumed regression.
<!-- FRONT:END -->
