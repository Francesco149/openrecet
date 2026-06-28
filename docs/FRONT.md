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
- **★★★ §21.10.1 LANDED 2026-06-27 — the cc08==4 d3e-LOAD TIMING fix (first −1 drift + studio note #1, ONE root).**
  The cc08==4 arm gated its WHOLE body on the live `b1cc!=2`, but `notify_loaded` clears b1cc INLINE at the top ⇒ on the
  load-release frame the gate ran everything that frame. Two gaps vs retail (RE §21.10.1): (a) the master tick ran ON the
  release frame ⇒ `b524` idle counter +1 early ⇒ the 2nd d3e load (`b524==0x3c`) fired 1f early = the **first −1**;
  (b) the arrival anim + camera + sprite were SUPPRESSED during the d3e load ⇒ stool-jump/camera-zoom played ~23f late =
  **note #1**. Retail: the d3e load is a BACKGROUND load (b1cc==2) that doesn't raise the load-SCREEN (be94<0x78), so
  house_update keeps ticking the arrival THROUGH the load; only the master tick self-gates on b1cc and resumes the frame
  AFTER the release. **Fix** (`scene1_player_ctrl.c`): gate ONLY the master tick on `b1cc_pre` (frame-start b1cc); UNGATE
  arrival_tick + ground_y + chr_anim_tick (rng-neutral). **✅ VERIFIED** (probe `house-firstcust-arrprobe`, port↔cached
  retail): anchor drift **0** CSE#1→CONV_POSE_START#1 (was −1; end-to-end −4→−2); arrival **BIT-IDENTICAL through the load**
  (panim 0→5@off2, pcnt 23→1→28, camex −1.5→−2.85 == retail); rng survey **3/200→1/200** 0/200 gsim%8; 3372 host pass.
  **TOOLING:** widened `{calltrace}`/`{caprange}` to HF occ1 (retail was bound to occ3, missed off 0-84); +probe scenario
  `house-firstcust-arrprobe` (trimmed to the first customer, no day-2 stall) + `tools/anchor_drift.py`. **PENDING USER
  STUDIO CONFIRM** (arrival/camera + companion visual).
- **★ §21.10.2 LANDED 2026-06-27 — the COMPANION (Tear) arrival, completing the arrival region (RE §21.10.2).** The port
  ran the companion ctrl every frame ⇒ Tear walked to the counter THROUGH the d3e load; retail keeps her IDLE (canim 0, cx
  frozen) during the load, then walks her in (canim 1→4) after b1cc clears. **Fix** (`scene1_companion_ctrl.c`): gate the
  companion ctrl inert during the cc08==4 load — run only the idle anim — on `(d3e_loading() || load_at_frame_start())`
  (the load spawns+clears mid-frame and the companion runs after the player ctrl, so the spawn frame needs the live b1cc
  and the release frame needs the frame-start snapshot; new `customer_service_note_frame_load`/`_load_at_frame_start`).
  **✅ v3-verified BIT-IDENTICAL** companion canim/cx/octant + the walk-in vs retail; survey 1/200, drift 0, 3372 host pass.
  Tiny residual: the idle wing-flap cframe ~1f ahead during the load (sub-frame anim phase; position/arrival exact). **NEXT:**
  (b) task #2: the wrap-up region drift (LOADING_END#4 +1 = occ4 load 2f vs retail 1f; DLG_LINE_SHOW +2). (c) task #3: note
  #2 scold-reaction render. Plan: `plans/rng-consumer-survey.md`, RE §21.10/§21.10.1/§21.10.2.
- **★ USER-CONFIRMED 1:1 2026-06-27 — the cc08==4 ARRIVAL region (player + camera + companion) "nicely synced"** (commits
  a93413a §21.10.1, dd98991 §21.10.2; recorded in `confirmed-parity-ledger.md`).  **Two NEW gaps the user flagged in the
  first-customer HAGGLE (both AFTER the arrival, in the live machine):**
  **(A) the OFFER diverges → different reaction — ★★★ RESOLVED 2026-06-28 (user chose option 1 = fix the port; RE §21.11.3);
  the cs-walker "−12" was a MISDIAGNOSIS (debunked: `npcdr=0` EVERY frame, no burst — a destabilised bg-probe artifact).**
  Root = the port cleared the held ESC-skip b150 (`s_skip_modal`) INLINE at the b534=1→2 edge, but **retail clears b150 the
  frame AFTER** (its b534 1→2 has no FUN_00435612) ⇒ the port's PAUSE_CLOSE fired 1f early ⇒ the input-replay offer-Z + the
  L90 {rngseed} re-pin both applied 1f early ⇒ re-pin jumped the seed before the +2 draws ⇒ poseR 1 / offer 120.  (anchor_drift
  proved it a PORT BUG not the skipped-intro phase: CSE/b534 0→1/1→2 all bilaterally aligned; only `PAUSE_CLOSE − b534_1to2`
  diverged −1 vs 0.)  **FIX (committed): defer the `s_skip_modal=0` clear 1f** → cs_live_machine's first b534==2 frame; PAUSE_CLOSE
  now lands on the b534==2 frame like retail, the bilateral L90 re-pin applies correctly.  **✅ VERIFIED bit-identical** (fixed
  port WITH committed L90 vs retail d43dafe9): offer `120→119`, variant `poseR 1→3`, b534 2→6 `off389→390` all == retail; **699
  offsets, ZERO rngΔ mismatches** (whole wrap-up→offer→round-2 window); 3372 host pass; no regression.  **The bilateral
  {rngseed} works as designed — no target-scoping/foundational pin needed.**  cs-walker-rng-phase + the offer arc CLOSED.
  **✅ USER-CONFIRMED 1:1 2026-06-28** ("can confirm it's aligned up to haggle prompt"; ledger).
  **(B) the missing HAND CURSOR at the haggle prompt — ★★★ PORTED + USER-CONFIRMED 1:1 2026-06-28 (`9f6c19e`, RE §21.12).**
  The shared menu hand-pointer (`title_save_dialog_cursor_render` FUN_00435747, nowloading.tga {192,0,232,40}) never drew in
  cc08==4: the cc08 path skipped retail's top-level cursor draw (all.c:7498) AND the machine's show/hide/snap/slide were
  stubbed `PORT-DEBT(cs-cursor)`.  **Fix:** append the cursor render at the house-aggregator TAIL (`scene1_hud.c`, = retail
  7498, drawn LAST after the overlay, ungated, self-gates on g_cursor_visible) + wire the 8 driver sites in
  `customer_service.c` — scripted FUN_00461c00 (b608==3 edit hide / offer snap / b608==4 commit hide); cs_input_poll
  FUN_004622d9 (up/down → SLIDE to the toggled Yes/No row, x=192 y=b540·0x30+386); live FUN_004658ab (b534==2 greet hide /
  b534==6 edit hide / 6→0xf snap / 0xf commit hide).  **rng-safe** (cursor draws no rng — verified rng VALUE bit-identical
  at the decision: b574=119, poseR=3 == retail).  +host test `cs_cursor_snap_and_slide` (3373 pass); PORT-DEBT(cs-cursor)
  retired (SE 0x143/0x146 audio stays cs-offer-fx/cs-poll-fx).  **✅ v3-verified** (orv3_shot, win-0-1500 decision frame
  port1062/retail1162): the white hand renders pointing at "Okay!" 1:1 with retail; **USER-CONFIRMED 2026-06-28 ("can
  confirm the cursor matches"; ledger).**
  **★ NEXT — gaps the user queued for AFTER the cursor + a NEW one this session surfaced:** (i) a GAP immediately AFTER the
  haggle prompt; (ii) **retail shows NO dialogue behind the ESC-skip while the port is mid-reveal of a line** (a
  dialogue-visibility-under-the-modal gap — should the port hide/clear the underlying dialogue when the ESC choice box is up?).
  **(iii) NEW (autonomous, on the cursor-verify drive 2026-06-28): the REACTION line VARIANT diverges** — at the b534==6→0xf
  decision the port renders recette msg09 **"Capitalism, ho!"** vs retail **"How much should I?..."** (the two rand%2 variants
  of msg09, the L1c pair).  NOT rng-value: rng is **bit-identical at every b534==6 reaction-entry** (the pick frame) yet the
  text differs ⇒ a VARIANT-ORDERING / draw-COUNT puzzle in `cs_pick_line` (FUN_00460a1a) or the parsed `customer_dialogue`
  data, NOT a seed/phase gap.  Likely == gap (i).  Investigate the msg09 variant index path next.
- **Phase:** frame-by-frame 1:1 parity sweep along the player path (title →
  prologue → HOUSE → shop loop → world map → dungeon). Strategy + tooling roadmap:
  **`audits/2026-06-09-methodology-audit.md`** (settled verdicts — behavioral-vs-
  byte-exact CLOSED, x87 invariant, T1–T12 tooling roadmap, milestone-ladder KPI).
  Read it before re-litigating strategy or building new parity tooling.
- **ACTIVE ARC → SHOP CUSTOMER-SERVICE / price-haggle tutorial** (the cc08==4 selling mode),
  scenario **`house-customer-tutorial`** (user recording rec-20260617-051426: LOAD cad868 → walk to
  the sell counter → the haggle tutorial that alternates Tear's dialogue with the BARGAIN!! price UI
  → first real customer). Full RE: **`findings/customer-service-haggle-RE.md`**.
  **★ CURRENT FRONT (2026-06-19, user redirect) — RENDER THE cc08==4 SCENE.** The cc08==4 STATE
  machine works (Chips 1/2a-e) but was **INVISIBLE** (port stayed in free-roam top-down). Retail's
  stage = counter camera + Recette/Tear 2D art + the "Tear" dialogue box (NORMAL `dialogue_tick`
  fires 0×; it's the scripted machine's own render). **Chip 3a ✅ LANDED 2026-06-19 — the DIALOGUE
  BOX + typewriter line now render** (`src/customer_service_render.c`: FUN_0046602e a/b/c +
  FUN_00466b7b §6, dispatched into FUN_00409925/FUN_0040a765; typewriter via the EXISTING
  `font_draw_text_box`). v3-verified `house-customer-tutorial-a361c768`; feed "cc08==4 render — Chip 3a". **Chip 3b ✅
  LANDED 2026-06-19 — the big Recette/Tear CHARACTER ART now renders** (`fed54cf`): ported the
  UNPORTED startup `grp:` per-stage file parser (FUN_00475270 block#4 → `scene_buy_parse_stage_buffer`
  + the `tables.c` driver) that populates `g_scene_buy_names`/count from each customer's `file:` data;
  AE8 loads page 0 (Recette/Tear), B13 page b56c (customer). v3-verified: the port now draws the
  standees over the dialogue box (feed "Chip 3b: CHARACTER ART renders"); +2 host tests. **Chip 3c
  ✅ LANDED 2026-06-19 — the cc08==4 ARRIVAL anim + camera ramp; PLAYER POSE/POS BIT-EXACT 1:1**
  (`player_ctrl_cs_arrival_tick` in scene1_player_ctrl.c, the FUN_0048670f all.c:87367-87432 arm;
  retires the player-pose part of PORT-DEBT(cs-arrival-anim)): Recette faces octant 0, hops on the
  merchant stool (anim 5), and `g_scene1_player_pos` ramps to the counter (px −1.5→−4.5 @0.125/f,
  pz 9.35→8.6 @0.05/f, py→0.5 once db04c>10) — **v3-verified `panim/pframe/pcnt/poct/px/py/pz`
  BIT-EXACT vs retail across all 2569 cc08==4 frames** under a constant +1-frame arrival-origin
  PHASE shift (CONST-OFFSET, accept). Also gated the room px-clamp on cc08!=4 (FUN_00486435) +
  advance the player anim via chr_anim_tick (rng-free). 3335 host pass. **NEXT (note #3 follow-up):
  the camera FRAMING residual** ✅ ROOT-CAUSED + PORTED as **Chip 3d (the cc08==4 cinematic COUNTER
  camera, RE §8.7.3).** It was a real NEW camera mode (user: "investigate this new camera mode"):
  retail's master tick (`FUN_00462403`) DECOUPLES the camera from the player and pins the lookat to
  a FIXED per-shop-tier counter target (tier-0 (-3.0, 0.0), eye (-3.0, 14.0)) with `stage_class=1`;
  the port had stubbed those eye writes as "cs-bubble-pos" + hardcoded stage_class 0 (player-follow
  → over-pan to -4.5). Ported `scene1_camera_cs_counter_cam` + the class-1 pass-through + the
  master-tick call + the free-roam reset; added a camera eye/lookat probe (both sides). **v3-VERIFIED
  BIT-EXACT** camex/camez/camlx/camlz = (-3.0, 14.0, -3.0, 0.0) at the settled view (off 80-120),
  ramp within the +1-frame phase. 3335 host pass. PORT-DEBT(cs-cam-tier): tier-2/3 eye-height ramps.
  **TRANSITION ✅ FIXED 2026-06-19 (`7046edf`, user-flagged: "camera wrong-then-converges while
  Recette's anim is 1:1"):** the settled camera was 1:1 but the RAMP lagged. CSE-aligned `--state`
  probe: the arrival anim is bit-exact + the camera ramp is bit-exact in SHAPE (`camlz(off)==retail
  camlz(off-1)`) — only its START frame trailed the anim ~2f. Cause: the cc08==4 arm advanced
  `arrival_tick`(anim)+`chr_anim_tick`(sprite) UNCONDITIONALLY through the b1cc==2 customer-asset
  load while the camera (master-tick, self-gated b1cc==2) waited — Recette hopped onto the stool
  BEFORE the camera zoomed in. Retail suppresses its ENTIRE house_update during the load (anim+
  sprite+camera start together at load-end). Fix: gate the whole arm body on `b1cc != 2`. Master
  tick was already b1cc-gated ⇒ **haggle/offer UNCHANGED**. v3-verified (fe530872): anim a5/0/1 +
  camera step now fire the SAME frame, like retail. Residual: the async d3e worker clears b1cc ~1f
  later than retail (LE+2 vs LE+1) = nondeterministic load-duration phase (accept).
  **NOTES #8/#9 DIAGNOSED 2026-06-19 (probe `4c8ae57` added poseL/poseR/b53c/b5d0/b5d8/b1cc) — a
  pure RENDER gap, NOT logic.** The cc08==4 dialogue STATE is **bit-identical port==retail** across
  the window (poseR/poseL=0, b534, b53c, b5d0, b56c=1 all match; only b524 off by the 1f load
  phase) — so the "standees" aren't a 2D-pose/timing bug. The real gap is the **3D COMPANION
  (customer = Tear, actor 2): retail puts her at `canim=4`** (the at-counter ready pose, octant 2,
  walked to (-3.2, 8.6)) **while the port leaves her at `canim=0`** (idle, octant 0, (-3.0, 8.8)).
  The player (Recette) is `canim 5` (stool-jump) 1:1 on both (Chip 3c); only the COMPANION arrival
  was unported. **★ COMPANION at-counter pose ✅ LANDED 2026-06-19 as Chip 3e** (`1cbdaf1`,
  RE §8.7.4): ported **`FUN_0048a833`'s `local_c!=0` (f404 sell-active) at-counter branch**
  (by-address 0x48ace7-0x48aeda) into `scene1_companion_ctrl.c::co_at_counter_tick`, branched in
  `scene1_companion_ctrl_tick` on `cc08==4` (the part the partial port — only the FUN_0048a4d1
  free-roam spring-follow — didn't cover): canim 4, step 0.1/f toward (player.x±1.3, player.z),
  octant 2/6, NO rng (haggle stream + frozen-db054 sparkle untouched). **v3-verified
  (house-customer-tutorial-a361c768, --state): `canim` 2546/2546 + `coct` 2546/2546 BIT-EXACT
  (4 / octant 2); `cx/cz` settled BIT-EXACT at (-3.2, 8.6)**; the ramp transient inherits the
  accepted Chip-3c player-px arrival phase (re-converges bit-exact). +2 host tests; 3337 pass; feed
  "cc08==4 companion at-counter pose — Chip 3e". **REMAINING (note #8): the manga-lines (集中線)
  effect** = the retail-only `b494` draw (80-tri, R[0], renders BLACK alone in v3 ⇒ **RT-based** = v3
  replays it empty, the known SetRenderTarget limitation). **NEXT (fresh arc): the manga-lines RT
  effect** — needs a v3 RT-capture extension first to even replay it (improve the tool, then port).
  Frame-sync residual: port b1cc
  clears at CSE-off 3, retail off 1 (2f) — load-duration phase; the "frames not synced" the user saw.
  **★ DIALOGUE NAMEPLATE (slot-1) ✅ LANDED 2026-06-19 (`feb2254`) — the user-flagged "names not
  showing for a lot of the lines."** FUN_00466b7b §6 drew the left-speaker (Recette) name plate from
  the fixed chrname.tga cell {0,32,128,64} but STUBBED the right speaker (the customer): retail indexes
  the cell by the active kyaku record's name_index (`*(int*)(&DAT_06a5ea90 + b56c*0x2c670)`, the
  record's first field — already parsed by `tables_kyaku` from 名前番号:).  Snapshot now carries
  `cust_name_index = g_kyaku.records[b56c].name_index`; the slot-1 branch computes the chrname cell
  (low range ≤0x15 = 7 rows/col, high range 8 rows/col at src-y +256) at dst (204,300).  Retires
  PORT-DEBT(cs-nameplate-slot1).  3337 host pass.
  **★ HAGGLE UI (FUN_00466b7b §2-4) ✅ LANDED 2026-06-19 (`12d668e`) — the user-flagged "haggling ui
  missing entirely."** Ported the three blocks the scripted tutorial sell exercises: **§2 price-INFO
  panel (b5a0)** = the shopmode reference panel (slide-in + arrival flash) + "Base Price N,NNN"
  (comma-grouped) + the showcase item name/icon + data_win frame; **§3 BARGAIN!! banner (b598)** = the
  shopmode banner + the asking-price NUMBER (new `FUN_00468034` port — 7-cell %7d digit row, shopmode
  y352, 36px pitch +8 group gap) + the digit cursor + the "What should I pay?"/"How much should I?"
  prompt + the "NN% Of Base Price" markup; **§4 BUTTONS (b58c)** = the 2 item_win panels + Okay!/
  Start-Again labels (select-pulse / fade-out).  All geometry/colour/scale objdump-transcribed via the
  new **`tools/decode_exe_const.py`** (the decompile drops x87: trend tint trend-0→grey 0x7f7f7f, the
  ramps, the cursor pulse).  Broadened the 0x48670f probe with b598/b59c/b58c/b560/b540.  **v3-verified
  LAYOUT/CONTENT 1:1** content-matched at ask=1300/base=1200 (port kept 2552 vs retail 2563): the
  BARGAIN banner, Base Price 1,200, the "1 300" number, "108% Of Base Price", Okay!/Start-Again all in
  the right place with the right text (feed "cc08==4 haggle UI ported").  Retires PORT-DEBT(cs-render-rest).
  **★ DIM-NUMBER BUG ✅ FIXED 2026-06-19 (autonomous) — the user-flagged dim NUMBER + buttons** (a
  MODULATE-vs-ADDSIGNED COLOROP gap, same class as the sold-out-text bug, `feedback_verify_1to1_before_done`).
  Retail wraps the ADDSIGNED-designed draws in `SetTextureStageState(0,COLOROP,D3DTOP_ADDSIGNED=8)` then
  resets to MODULATE(4); under ADDSIGNED a 0x7f7f7f diffuse passes the texture at full brightness, under the
  port's `render_quad` default (MODULATE, render_quad.c:269) it HALVES it ⇒ dim.  **Ported the three
  objdump-transcribed ADDSIGNED brackets** in `customer_service_render.c` (FUN_00466b7b), each resetting to
  MODULATE so the panels + icon (MODULATE on both sides) are untouched: price TEXT (name / Base Price /
  Showcase Item) `0x467143`→`0x467233`; NUMBER+cursor+prompt+markup (b598≥0xa) `0x467493`→`0x4675d3`; BUTTONS
  (panel + label, per-iteration, set AFTER the faded-out `continue` so a culled button skips the bracket like
  retail's `js` skip) `0x4677d6`→`0x4678f3`.  Key insight: `render_quad_bind/_flush` DON'T touch COLOROP
  (only the once-per-batch `render_quad_state_setup` sets MODULATE) ⇒ one SetTextureStageState before the
  draw block persists through `cs_quad`/the font draws (mirror of scene_guild.c / scene1_merchant_hud.c).
  **v3-verified at the SAME occ2-relative moment (port idx 2405 / retail idx 2563, both ask=1300): the
  '1300' number + cursor, the 'Longsword'/'Base Price 1,200'/'TARGET' top panel, and Okay!/Start-Again all
  render BRIGHT and 1:1 with retail.**  Number-region crop: before = dim grey digits + dull cursor (mean
  134) → after = bright white digits + bright orange cursor (mean 147) == retail (144) (feed "cc08==4 haggle
  NUMBER: dim→bright").  The top-INFO panel (TARGET/Longsword/icon/Base Price 1,200) is **also 1:1** at this
  matched frame — an earlier "panel differs" read was a FRAME-MISMATCH artifact (port idx 2697 = occ2+2697
  vs retail occ2+2406, 291f apart), not a gap.  3337 host pass.
  **★ TWO MORE COLOROP errors found by the USER on the now-synced viewer + ✅ FIXED 2026-06-19 (notes #14/#15):**
  (#14 "target! panel dimmer on port") the **b5a0 price-INFO panel BACKDROP** drew grey 0x7f under the default
  MODULATE (half) — retail wraps it in its OWN ADDSIGNED bracket (asm `0x466f7e`→`0x46702a`, separate from the
  panel text); my comment said "ADDSIGNED" but never set the COLOROP.  Now bracketed → panel-band diff 0.3%
  (mean 0.5) == retail.  (#15 "top+bottom haggle text too bright") the **prompt + markup** ("How much should
  I?" / "N% Of Base Price") were left under my Section-3 ADDSIGNED bracket, but their WHITE/yellow diffuse goes
  OVERBRIGHT under ADDSIGNED — retail resets to MODULATE at `0x4675d3` right AFTER the cursor (number+cursor
  ONLY stay ADDSIGNED).  Moved the reset → central-UI band 10.9%→6.8%.  **Rule learned: GREY 0x7f diffuse ⇒
  ADDSIGNED (passes texture); WHITE/coloured diffuse ⇒ MODULATE (ADDSIGNED would overbright).**  3337 host
  pass.
  **★ PROMPT ELLIPSIS ✅ FIXED 2026-06-19 (`df58859`, user-flagged):** the port dropped the trailing "..." on
  both scripted-sell prompts — restored from .data verbatim: `DAT_005c6e28` "What should I pay?..." +
  `DAT_005c6e40` "How much should I?...".  Checked per the user's caution: these are the ONLY two "should I"
  strings in `src/`, both the cs haggle prompt ⇒ no other UI instance touched.  Rendered + verified on the
  re-driven port (idx2405).
  **★ cc08==4 HAGGLE-UI RENDER — USER-CONFIRMED 1:1 2026-06-19 ("everything else looks 1:1 now").**  Recorded
  in `findings/confirmed-parity-ledger.md`.  Accepted residuals (NOT logic): (a) the character +1f
  arrival-origin phase (the "not perfectly synchronized" — UI phase-0, sprites 1 anim-frame off, can't
  co-align); (b) the ~2px per-glyph prompt/markup text precision (`per-line pose precision` PORT-DEBT); (c) the
  honest load-region join gaps.  Open cc08 PORT-DEBT (unexercised here): cs-price-trend, cs-render-priceinput,
  cs-haggle-prompt-live.  **Manga-lines (集中線): per the user (2026-06-20) ALREADY PORTED for this scene** —
  NOT a remaining gap (an earlier autonomous note wrongly listed it as "the next arc"; the RE §8.8 / note-#8
  "RT-replays-empty" caveat is a v3 TOOL limitation, not an in-game miss — reconcile that doc note).
  **★ NOTE #1 — sell-counter "!" affordance emote ✅ FIXED 2026-06-20 (autonomous; user: "the last major
  remainder for this trace"):** the free-roam "!" interaction prompt above Recette at the counter.  The
  emote-bubble RENDER (`scene1_hud_emote_bubble` = FUN_0040a765) was already generic over db004; only the EMIT
  was missing — `player_ctrl_cc08_proximity_detect` drove just the door "GO!" (bVar17→type 7).  Ported the
  **bVar3 counter affordance** (all.c:87542-87588): factored the position+facing predicate into
  `player_ctrl_at_sell_counter()` (shared with the Z-entry), and raise db000 + set **db004=0** (the "!" cell;
  not the be7c NPC-approach type 1 — no live customer ⇒ be7c never arms) when at the counter (door wins;
  carrying suppresses).  **v3-verified on `house-customer-walk-probe` (port idx100 at the counter, px∈(-5,0)
  pz=9.35 facing -π/2): the "!" speech bubble bit-matches retail's note-#1 cell** (feed "note #1 FIXED"; Tear's
  red "+" emote is on both sides too).  Shares PORT-DEBT(cs-entry-flags) with the Z-entry (the DAT_0450f3fd
  customer-queued gate, unported iv1_7); PORT-DEBT(emote-npc-approach) = the be7c/type-1 NPC path.  Pending
  user re-confirm.  3337 host pass.
  **★ CAMERA-HINT OVERLAP ✅ FIXED 2026-06-20 (user-flagged):** the bottom-right "Button 4: Change Camera"
  free-roam hint (`scene1_top_hud_camera_hint`, FUN_00409925 tail) drew during cc08==4, OVERLAPPING the
  haggle's own "Button 3: Item Details" at the same (440,440) data_win slot.  Retail gates it on
  `b1c8==0 && DAT_0438b4e8==0`; b4e8 (a menu/overlay flag the port treats as 0 in free-roam) is set while an
  overlay owns the hint slot, incl. cc08==4 — mirrored via the cs-active flag `!customer_service_active()`.
  v3-verified (idx2405): bottom-right shows ONLY "Button 3: Item Details" == retail.  PORT-DEBT(camera-hint-
  b4e8): the other b4e8 menu/transition states aren't tracked.
  **★ BUY-HAGGLE SOFTLOCK ✅ REAL ROOT CAUSE FOUND + FIXED 2026-06-20 (`29e167a`) — the parser stride was
  50, must be 200 (RE §9.8, engine-quirks §22).** User (2026-06-20): the `5c0493a` 値段→op5 fix did NOT work;
  the buy prompt still softlocks ("price lower than base even when you price it lower").  Per the porting loop,
  dumped retail's parsed `g_tuto` (`&DAT_005d1fc8`) via Frida: **tuto1@slot 0, tuto2@200, tuto3@400 — each
  file in its own clean 200-slot region, NO overlap.**  The "stride 50 / overlap" the port (+ RE §9.1-9.7)
  believed was a **Ghidra decompile error** (`imul …,0xe740` rendered as `local_c*0x32`=50).  The port's
  `TUTO_PARSER_STRIDE=50` overlap-corrupted tuto2/tuto3 into tuto1's tail → garbled op-5 args / cs_goto / text,
  and the "hold X → buy prompt" path was that corruption (NOT a real retail path).  **Fix: stride 50→200** →
  the port's g_tuto bit-matches retail; tuto2's buy 値段 is clean at slot 232 (op5 args [10..16], id-11
  "Excellent…"→GOTO 17), fileidx=1 gives BUY tiers, cs_goto resolves within tuto2.  3337 host pass.  **★
  USER-CONFIRMED 2026-06-20:** "it's all sell prompts up to here… up to this softlock it is correct" — the
  buy part was the previous porting mistake; the REAL buy tutorial is a SEPARATE DAY (fileidx=1, not reachable
  in this sell-tutorial trace).  The 値段→op5 fix (`5c0493a`) stays (correct, now operative on clean data).
  **★ CLOSING SOFTLOCK ✅ FIXED 2026-06-20 (`0c0331c`) — the user-hit "If you can sell me an item…" freeze.**
  The stride fix unblocked progression to the next stub: the master tick's whole b534-closing branch was a
  bare `return` (PORT-DEBT(cs-closing-states)), so the scripted PC hit tuto1's -1 sentinel → b534=0xc and
  FROZE.  Ported the b534==0xc scripted close (all.c:60605-13): b52c countdown → reset b51c/b524/b534/b55c → idle.
  v3-verified on the committed trace: resets at b534=0xc→0 (frame 6956), idles up, reaches the **first real
  customer's greeting** (b534=1, b51c=0) — the softlock is gone.  +1 host test; 3338 pass.
  **★ OPEN GAPS (next arcs):** (1) **the first real customer's live SELL machine `FUN_004658ab`
  — UN-SOFTLOCK ✅ LANDED 2026-06-20 (`7dfc611`, Chip L1a).**  The "softlock once Tear tells you to sell her
  something": after the scripted tutorial closes (b534=0xc→0 @port-fr6956, b51c→0), the first real customer
  greets (b534=1, b51c=0 @fr7115) but the master tick's b51c==0 arm was a bare return ⇒ frozen.  Ported the
  master-tick live arms (the b51c==0 greeting 1→2, the b5a8==2 dispatch, the live closing/queue 0xa/0xb/0xc/
  0xd/0x14/0x15) + **`cs_live_machine` (FUN_004658ab)** (states 2 greeting → 6 reaction → 0xf decision →
  7 accept / 8 pushback / 9 reject) + the helpers `cs_accept_eval` (FUN_00460672, objdump x87 bands ±0.5%/
  ±5% of b588), `cs_pushback_line` (FUN_00460f16), `cs_pick_line` (FUN_00460a1a — the load-bearing rng line-
  pick).  **Verified:** the un-softlock on the real flow (first-customer b534 1→2→6→0xf, **offer b574=3870 =
  retail's §9.2-observed value**) + a deterministic host test (`cs_live_machine_sell_cycle`: greeting→2→6→
  0xf→7→0xa→0xc→leave).  3341 pass.  **REMAINING (next chips):** **L1b** the accept side-effects
  (PORT-DEBT(cs-live-sale-fx): gold += ask + stock decrement + the catalog/inventory/payout FUN_00460d52/b3a/
  606fc/00083/0002a/00b93, all f404==0); **L1c** the per-customer dialogue buffer (PORT-DEBT(cs-kyaku-
  dialogue): real line text/count/sprite/voice from kyaku/fN.txt — currently a placeholder drives the reveal);
  + a bit-exact retail trace comparison, **blocked on the multi-round nav gap** (RE §9.6 — the port's scripted
  tutorial closes after 3 rounds, retail after 5, so port↔retail diverge BEFORE the first customer; the host
  test + the §9.2 offer-match stand in until that's fixed).  PORT-DEBT also: cs-shop-stock, cs-other-kinds
  (b5a8 0/1/3/4/5 buy/chat), cs-queue-line, cs-sold-pause.
  **★★ NEXT ARC (user directive 2026-06-21) — the FULL POST-TUTORIAL FLOW, "down to all the details / the
  various transitions between dialogues."**  User-tested the L1a build + flagged: the tutorial itself + the
  no-pix haggle prompts are CORRECT, but what comes AFTER is unported.  Retail's target flow:
  scripted tutorial (5 BARGAIN rounds, no pix) → the last scripted lines incl. **Tear's wrap-up** *"And that
  is, essentially, how it goes.  You are quite good for someone who has just started…"* (a free-roam
  scene1_intro_dialogue, the rounded box; **X-HOLD skippable**) → the **FIRST ACTUAL CUSTOMER = the real
  haggling loop where real pix IS earned** (a real kyaku, real dialogue, the live machine FUN_004658ab with
  f404==0 + the L1b side-effects).  The SKIP path converges to the same: ESC→fade→wrap-up→first customer.
  **The port's gaps (root causes):** (a) the scripted tutorial **closes EARLY (~3 rounds, hits the −1
  sentinel) instead of 5** — the multi-round nav gap (RE §9.6); so it never reaches the later scripted lines
  incl. the wrap-up, and my L1a live machine kicks in on the leftover tutorial queue → the "rolled back to
  before the tutorial" the user sees.  (b) **no wrap-up dialogue** (close + skip both miss it).  (c) **no
  transition to a REAL customer** — needs the autonomous customer-arrival / a fresh session_init with the
  **roster scan (PORT-DEBT(cs-roster-scan))** + f404==0, so the live machine runs on a real kyaku, not the
  tutorial's kyaku=1 placeholder.  (d) **"…" placeholder dialogue** everywhere past "if you can sell me an
  item" (PORT-DEBT(cs-kyaku-dialogue) = L1c) + **not X-hold fast-forwardable** (the reveal-budget X boost).
  (e) the real haggle needs **L1b** (real pix).  **Staged plan (multi-session):** P1 finish the tutorial —
  the round-2..5 nav (§9.6) so it reaches the −1 sentinel after the FULL script incl. the wrap-up lines;
  P2 the wrap-up dialogue trigger + the cc08-exit→free-roam→wrap-up→first-customer transition (close + skip);
  P3 the first REAL customer (roster scan + f404==0 + the live machine on a real kyaku); P4 L1c the per-kyaku
  dialogue buffer (kyaku/fN.txt → real lines + X-skip + inter-dialogue transitions); P5 L1b the side-effects
  (real pix).  **RE foundation (anchors, `_cs-firstcust-re-retail` 2026-06-21):** retail's post-tutorial flow =
  tutorial (cc08==4) → **CONV_POSE wrap-up dialogue** (the committed trace's CONV_POSE/TEXT_ANIM cutscene =
  the "And that is…" lines, retail frames 23259-23707; the existing scene1_conversation_pose system, NOT the
  cs box) → **LOADING_START/END → HOUSE_FREEROAM @23782+23850** (cc08 exits 4 → free-roam, a real scene
  reload) → the FIRST CUSTOMER arrives in free-roam.  So the transition goes THROUGH free-roam (a LOADING),
  NOT a re-greet inside cs mode — P2 must EXIT cc08=4 + fire the CONV_POSE wrap-up + the LOADING→free-roam,
  then P3 is the free-roam autonomous arrival.  (The skip path converges at the same wrap-up — `_cs-skip-
  probe-retail` frames 15700-15997 show its content.)  The first-customer real-haggle FRAMES still need a
  re-drive with the caprange on the post-tutorial region (this drive's caprange only covered round 1; the
  anchors traced the rest).
  **★★ P1 RESOLVED + the whole framing REFRAMED 2026-06-21 (`e42921a`, RE §11) — "scripted closes after 3
  rounds instead of 5" was a MISDIAGNOSIS.**  A wide-window `--call-trace` drive of BOTH sides (retail via
  frida, on the recording's own inputs) proves: (i) §9.6's "f3360 harness early-exit" was just a *BMP-dump
  slow-drive* hitting the 480 s wall ceiling — a `--capture-trigger-only` drive runs the whole flow with NO
  early exit; (ii) the SCRIPTED tutorial is **exactly 3 rounds and BIT-IDENTICAL port↔retail** (PC 38/73/106
  → −1 sentinel @PC131 → close) — no scripted-round bug; (iii) retail's **"5 rounds" = 3 scripted + 2 LIVE**
  first-customer (Tear) practice-sale BARGAINs (`b51c==0`, `b534==0xf`), bit-identical too (offer
  `b574=3870`).  The ONE port gap was a missing signal: `customer_service_bargain_active()` only checked the
  scripted `b608==4`, so the LIVE BARGAINs never raised `PAUSE_OPEN` → the round-4/5 trace segments never
  synced → stall.  **Fix:** OR in the live decision (`b51c==0 && b534==0xf`) — both run the same
  `cs_input_poll` retail backs with `b150`.  **Anchor-verified:** the port now fires **5 PAUSE_OPENs**
  (3134/5000/6315 scripted + 7559/8600 live) + the scripted→live / inter-customer LOADINGs at retail's
  offsets (±~1%); both live sales complete (`0xf→7 accept→0xa→0xc`) and the port **exits cc08 4→1 to
  free-roam @~9124**.  +host assertion; 3341 pass.  So **P1 ("round-2..5 nav") is DONE** (the tutorial
  reaches the sentinel after its full script AND the live practice rounds navigate).
  **★ P2 — the post-sale wrap-up dialogue iv1_7 ✅ PORTED + host-tested 2026-06-21 (RE §12.1); the §12
  "f400 flag-conflation BLOCKER" was a MISDIAGNOSIS.**  In a FRESH shell: (a) `DAT_0450f400` has EXACTLY ONE
  writer — set to 1 ONLY at the cs leave/dissolve (all.c:60389), cleared ONLY deep in scene-2 (45781); no
  prologue writer ⇒ **it is 0 at the cad868 LOAD** (probe-confirmed 3014/3014 free-roam rows f400==0), so the
  iv1_7 gate `(f401==0 && f400==1)` CANNOT fire prematurely.  (b) The "frame-231 hang" was an **env/9p
  confound** (the call-trace crawling over 9p looked like a hang — the user rebooted + I fixed the call-trace
  I/O, below); the committed exe drives clean past 231.  **Ported** `scene1_tutorial_dispatch.c` (the iv1_7
  `if` after iv1_5/iv1_6, mirroring all.c:45715: `start_single(1,7); f401=1; f406=1`) + **host test**
  `test_cs_iv1_7_wrapup_trigger` (3342 pass): f400==0⇒no fire, f400==1⇒fire+latch, once-only.  **★
  INTEGRATION-VERIFIED 2026-06-21:** a full drive to the cs-exit navigates all 5 rounds (no mid-haggle
  collision), then post-R5 (`PAUSE_CLOSE@9805`) fires `LOADING_START@10278`+`CONV_POSE_START@10279` — iv1_7
  at `LOADING_START+1`, **bit-matching retail §11e** (`CONV_POSE_START@22963 = LOADING_START@22962+1`); the
  multi-line wrap-up runs (TEXT_ANIM×N) + ends clean (`CONV_POSE_END@10927`); drive exit 0, NO hang.
  **★ USER-CONFIRMED 1:1 2026-06-22 in the v3 viewer** (full port↔retail re-drive, win-8000-2500; retail
  13000/13000 bit-exact, port 12992/12994): "the wrap up dialogue is correct" — the iv1_7 wrap-up TEXT +
  cutscene render 1:1 (same `scene1_conversation_pose` system, the real `iv1_7.ivt` lines).  So **P2 / iv1_7
  is DONE.**  Recorded in `findings/confirmed-parity-ledger.md`.
  **★★ NEXT SESSION (user 2026-06-22, "several major gaps to un-mvp before moving past them") — two gaps the
  wrap-up viewer surfaced, both PRE-/POST- the (correct) wrap-up:**
  **(1) the live first-customer haggle dialogue "..." PLACEHOLDER ✅ PORTED 2026-06-22 as L1c (RE §13) —
  retires PORT-DEBT(cs-kyaku-dialogue).**  Was: `cs_pick_line` (`FUN_00460a1a`) drew the variant rng but
  stubbed `s_b270="..."` (the per-kyaku dialogue tail unmodelled).  Ported the **per-kyaku dialogue buffer +
  loader**: new `customer_dialogue.{c,h}` (slot grid `s=variant+type*0x14`, text/sprite/voice/count at record
  +0x6e70/0x51d8/0x5b38/0x6df8, the pure `kyaku_dialogue_parse` of the fixed-width `msgNN:SS:Vvv:text` lines)
  + `tables.c::load_kyaku_dialogue` (reads each customer's `kyaku/<name>.txt` via storage after kyaku.txt) +
  `cs_pick_line(rec,type,slot)` reading the real text/sprite (slot-0=record 0 Recette, slot-1=customer b56c)
  + the factored `cs_split_line` `<C>` split.  **RNG STEP unchanged** (one draw when !f404 either way) ⇒ the
  verified-1:1 LCG holds; only the now-used variant VALUE picks the real line.  **Verified:** loader on the
  user's real data = **18 scripts / 1229 lines** (no errors); 3 host tests (3345 pass); the reaction
  `cs_pick_line(0,9,0)`=recette msg09 = **"How much should I?..." / "Capitalism, ho!"** (count 2, rand%2) =
  the line the `...` hid.  PORT-DEBT added: cs-dlg-override (the DAT_073dddb8 buysell variant table, inactive
  here), cs-voice (playback, audio).  **v3-VERIFIED + USER-CONFIRMED 2026-06-22** (win-5900-2800,
  --join-anchor CUSTOMER_SERVICE_ENTER): the live greeting renders "Tear / I would like this, please."
  BIT-IDENTICAL to retail; user "looks good".
  **★ FOLLOW-UP (user note #2) — the `<I>`/`<Y>` TEXT MACROS ✅ PORTED 2026-06-22 (RE §14, retires
  PORT-DEBT(box-text-macros)).**  The post-sale close line "Yay! I sold `<I>` for `<Y>`!" (recette msg08)
  rendered raw/mangled markers; retail substitutes the item name + pix.  Two fixes: (a) `font_draw_text_box`
  (FUN_00465db4) pass-1 macro expansion was stubbed AND leaked the trailing `>` — ported to new
  `dialogue_macros.{c,h}` (`dlg_macro_expand`, the 6 tag buffers); (b) the close branch now sets `<I>`
  (`cs_set_item_macro` = FUN_004607f3(b5a4) → `g_item.singular`) + `<Y>` (`snprintf("%dpix", s_price_ask)`).
  +4 host tests (3349 pass).  PORT-DEBT new: cs-item-macro-kinds (the b534==0x1e / b5a8==4 name sources).
  **v3-VERIFIED + USER-CONFIRMED 2026-06-22**: the close line renders "Steel Sword / for 3600pix"
  BIT-IDENTICAL to retail (was the mangled markers).
  **★ TWO MORE user notes ✅ FIXED 2026-06-22 (RE §15):** **(#3)** the live price-input prompt "How much
  should I?..." was MISSING — the render gated it on `b51c!=0` (scripted); objdump shows the `b51c==0` live arm
  draws the dialogue line `b270` (recette msg09) at (312,250), b5a8-coloured (retires
  PORT-DEBT(cs-haggle-prompt-live)).  **(#4)** the counter "!" emote LINGERED through cc08==4 idle — the Z-entry
  must clear `db000=0` (all.c:87696); `player_ctrl_cc08_sell_counter_enter` now does (doesn't touch the
  free-roam approach "!", note #1).  v3-verified 1:1.
  **★ TWO MORE notes (#5/#6) ✅ FIXED 2026-06-22 (RE §16, retires PORT-DEBT(cs-queue-line)):** the post-practice
  scripted CONCLUSION line "Expertly done. If you ever wish to practice again, simply ask me<C>any time we are
  in the shop." (tuto1 id **-4**) showed "○○○"/ended early — the master tick's b534==0x14 queue-advance stubbed
  it.  Ported the engine's negative-id scan (all.c:60540-60549): find g_tuto[fileidx] record `id==(b528==2)-4`,
  load its text via `cs_dialogue_line_setup` (the `<C>` page-advance is the shared pre-dispatch check); the
  long real line reveals at retail's rate so the timing tracks (no early-exit).  3349 host pass; v3-verified 1:1.
  **★★ GAP (1) — the WHOLE live-haggle dialogue — USER-CONFIRMED 1:1 2026-06-22:** "everything matches now
  besides the usual phase residuals."  Four chips (325a226 L1c / b6c545f `<I>`/`<Y>` macros / 35ab2d4 prompt+
  emote / 3d3f9c7 queue-line conclusion); +7 host tests; recorded in `findings/confirmed-parity-ledger.md`.
  Accepted residuals: the +1f arrival-origin phase + the load-region join gaps.  **DONE — gap (2) is next.**
  **★ (2) POST-FADE CAMERA ✅ ROOT-CAUSED + PORTED 2026-06-22 (RE §17) — it was the MISSING first-customer
  cs ENTRY, the camera a symptom.**  User: after the (1:1) wrap-up retail's camera CUTS to a new angle, the
  port's "is completely wrong."  Root (e8f49cb7 cache): retail post-wrap-up enters **cc08=4** (the first real
  customer) → the COUNTER cam eye=(-3,14) look=(-3,0); the port dropped to **cc08=1 free-roam** (player-follow
  cam eye=(-1.5,15) look=(-1.5,1.0)).  The gap was ONE branch — `FUN_0048670f` all.c:87485
  `if (f406 != 0) { cc08=4; FUN_0045edaa(); }` — the AUTONOMOUS f406 entry: iv1_7 sets f406=1, the next
  free-roam frame flips cc08=4 + the session init (the f406 forced-kyaku-13, Chip 1, b51c=0 ⇒ live machine).
  The port already set f406=1 + ported the f406 session-init + the counter cam, just never CONSUMED f406.
  **Ported** `player_ctrl_cc08_f406_entry` (scene1_player_ctrl.c, the free-roam arm, engine order; gated so it
  can't fire mid-cutscene).  **✅ v3-VERIFIED BIT-EXACT** (port re-drive): the port now fires
  `CUSTOMER_SERVICE_ENTER#2`@10956 (= `CONV_POSE_END`+`LOADING`, matching retail's @12249) and the post-fade
  camera converges to **(-3,14,-3,0) == retail** (was the free-roam cam).  +1 host test (3350 pass).  Accepted
  residual: the ramp-transient origin (load/phase), both converge bit-exact.  **✅ USER-CONFIRMED 2026-06-22:
  "camera looks correct once settled"** (commit `3d8f6ce`; ledger).
  **★★ (3) ARC = P3, the first REAL customer INTERIOR (user-flagged 2026-06-22, gap (2) opened it).
  COMPANION POSITION ✅ PORTED + v3-VERIFIED 2026-06-22 (RE §18.1) — PENDING USER CONFIRM.**  The port ran the
  WRONG companion arm: `co_at_counter_tick` (FUN_0048a833's `local_c!=0` / f404 at-counter ±1.3 walk, canim 4)
  fired for ALL cc08==4, so the f404==0 first customer walked Tear off to **cx=-5.80, canim 4** + never settled.
  Root (PROVEN off the e8f49cb7 cache + decompile, no guess): the first customer is **f404==0, f406==1** (the
  i32 read of 0x450f404 = 0x10000 = f404 byte 0, f406 byte 1); the scold/idle-WANDER arm (`local_28==0`) needs
  **f407**, whose ONLY writer is the iv1_8 start (all.c:45736, gated f402; **f402 is set ONLY at the cs LEAVE,
  all.c:60383**, right after f406 is cleared) ⇒ f407==0 during the haggle ⇒ FUN_0048a833 takes
  `local_c==0 && local_28!=0` = the **free-roam spring-follow (FUN_0048a4d1)** the port already ships (validated
  0.0036).  **Fix:** gate `co_at_counter_tick` on `customer_service_f404()` (the 0x2bc6c bank byte = f404 bit0);
  f404==0 falls through to the free-roam follow — Tear just trails the player to the counter and idles.
  **v3-verified: the port companion now BIT-MATCHES retail** (e8f49cb7, --state, --join-anchor
  CUSTOMER_SERVICE_ENTER, window `win-8400-600`): **canim 0, coct 0, cx=-3.0** (was -5.80), cz 8.6 (vs retail
  8.65 = const-offset load phase), greeting `b534` 0→1 aligned @off160; `cwander`/`cstate` 0 (no scold-wander).
  +1 host test (`companion_first_customer_freeroam`); 3351 pass.
  **★ USER NOTE #8/#9 "camera angle wrong" ✅ ROOT-CAUSED + FIXED + v3-VERIFIED 2026-06-22 (RE §18.3, the
  --d3d-trace) — it is the WRAP-UP (CONV_POSE) camera, NOT the greeting; §18.2 analyzed the WRONG frame.**  New
  tool `tools/trace_studio_v3/orv3_xform.py` extracts the actual SetTransform VIEW/PROJ from the EXISTING v3cap.bin
  (NO re-drive) + decodes eye/lookat/fov/near/far + `--draws-by-view`.  Note #9 = `CONV_POSE_BLINK#1+36` = the iv1_7
  wrap-up cutscene (user moved it to a no-standee frame, same region).  The CSE#2 first-customer GREETING §18.2
  measured is in fact **camera-1:1** (counter-cam VIEW + all 5 perspective PROJ BIT-IDENTICAL port==retail; pixel
  diff 1.19% — §18.2's "floor reprojection 54-diff" was a frame-misalignment artifact).  The REAL gap: through the
  whole wrap-up the scene rendered under the SAME orientation/FOV but a different CENTER — PORT eye=(-3,22.2,14)
  look=(-3,1.0) (the stale cc08==4 counter cam) vs RETAIL eye=(-1.5,22.2,15) look=(-1.5,1.0) (FREE-ROAM); pixel diff
  92.8%.  **Fix:** the port's cs LEAVE omitted retail's Recette HOP-DOWN reposition (`g_scene1_player_pos`→(-1.5,9.0),
  clamped to camera bias (-1.5,1.0)) + the `stage_class=0` reset (all.c:60349-394); the other reset site never runs
  once iv1_7 routes the sim to the EVENT arm.  Ported both into `customer_service.c` (RNG-safe; +host test
  `cs_leave_resets_freeroam_camera`, 3352 pass).  **v3-VERIFIED:** the wrap-up scene cam is now **eye=(-1.5,22.2,15)
  == retail**; pixel diff **92.8%→2.54%** (residual = player sprite + accepted +1f phase).  feed "NOTE #9 FIXED".
  **✅ USER-CONFIRMED 1:1 2026-06-22** ("yes the camera looks correct"); ledger recorded.
  **REMAINING P3 (next chips):** (a) **the WRAP-UP companion HEIGHT (viewer note #1, user 2026-06-22 "tear
  position slightly off / lower in port") ✅ FIXED 2026-06-22 (RE §18.4) — PENDING USER CONFIRM.**  Root: the
  companion free-roam hover Y = `sin·0.2 + g_scene1_player_ground_y + 3.0`, but the port NEVER WROTE
  `g_scene1_player_ground_y` (stuck 0 ⇒ companion at 3.0); retail's `DAT_056daf88` (floor-under-player) FREEZES
  at the counter-platform height 1.27 through the CONV_POSE wrap-up (the house_update floor writer doesn't run
  in the EVENT arm) ⇒ companion at 4.35.  Flat-floor free-roam (floor≈0) hid it; only the frozen raised counter
  exposed it.  (Note #1's reported player "(-2.0,8.8)" drift was a MISREAD off the clobbered call_trace — the
  v3cap WORLD shows the player at (-1.5,9.0) bit-matching retail; XZ was never wrong.  集中線 + the f404 scold
  gating were already done — not this.)  **Fix:** wire `g_scene1_player_ground_y` = queried floor in BOTH
  house_update paths — free-roam (`collision_resolve_player` = FUN_00483170 daf88 write) + cc08 (new
  `collision_set_player_ground_y` from the arrival arm, mirroring the engine's cc08-block daf88 write @87749);
  both gated off in the EVENT arm ⇒ freezes at 1.27 like retail.  RNG-safe; cc08 at-counter companion untouched
  (fixed hover, no ground_y term).  +2 host tests (3354 pass).  **v3-VERIFIED** (4dfe654b/port re-drive): the
  wrap-up companion chibi Y **port 4.08–4.46 == retail 4.09–4.46** (was flat 3.0) + the cutscene effect sprites
  rose to match (4.7–5.9 == 4.9–5.9), within ±0.03 bob/anim phase.  **✅ USER-CONFIRMED 1:1 2026-06-22** ("yes
  that looks correct"; ledger).  **FOLLOW-UP (same root) — the player CONTACT SHADOW frozen-floor ✅ FIXED +
  v3-verified + USER-CONFIRMED 2026-06-22** ("can confirm the shadow is correct"; commit 8bd2bc2; the WHOLE
  wrap-up cutscene — camera + companion + shadow — is now 1:1) (user: "fix that port debt too, if structurally equal"): the
  shadow render LIVE-queried the floor per actor; retail's shadow is a pure READER of the cached per-actor floor
  daf94 (filled in house_update, frozen during CONV_POSE).  Faithful fix: cache the player floor hit
  `g_scene1_player_floor` (= daf94[0], where collision_resolve_player + the cc08 writer already query) and the
  shadow reads it for actor 0 — bit-exact on flat floors (other scenes unchanged), frozen during CONV_POSE.
  v3-verified: wrap-up player shadow **(-1.5,1.392,9.0) == retail EXACT** (was 0.121).  Remaining
  PORT-DEBT(cs-shadow-frozen-floor): companion/actor-1 shadows still live-query (FUN_0048a833 per-actor daf94
  cache unported) — invisible here (companion = fairy hover, no contact shadow).  USER: re-open the viewer
  (win-8400-2500) → note #1 (companion height + Recette's shadow).
  (b) **the customer CHIBI NPC (viewer note #2, user 2026-06-22 "walks around and checks out the shop")** — the
  port doesn't render the first real customer's chibi sprite AT ALL (retail shows it browsing the shop; the §18.2
  "yellow element"/80-tri b494 = THIS).  Per the user, on the first customer the ONLY diffs are NPC rng + item-
  sparkle phase + this unrendered walking chibi ⇒ it's the customer walk-in/browse NPC system = part of the
  CUSTOMER-INTERACTIONS arc (the next trace).
  (c) the live haggle render fidelity (customer art/dialogue).  (d) **L1b** real pix (gold += ask + stock decr +
  payout).  (e) the roster scan (PORT-DEBT(cs-roster-scan)).  (f) the iv1_8 chain (`f406→f402`).
  **★★ NEXT ARC (user 2026-06-22): CUSTOMER INTERACTIONS — the user will RECORD a trace skipping the tutorial +
  doing a real customer session (overpricing items, etc.).**  User directive: **DEEP-DIVE the customer-interaction
  code** for the full OUTCOME SPACE (the user doesn't remember all outcomes) — the live machine FUN_004658ab
  (greeting→reaction→decision→accept/pushback/reject), the accept/price-eval bands (`cs_accept_eval` FUN_00460672,
  ±0.5%/±5% of b588), pushback (FUN_00460f16), the over/under-priced reactions, the per-customer DIALOGUE variants.
  **Mechanics the user named (verify in code):** (1) **CLOSENESS/affinity** — building rapport with a customer
  makes them more lenient on price (likely a per-kyaku save field feeding the accept bands); (2) **ATMOSPHERE
  score** from shop DECORATION — **won't change at this game stage** (a constant input here, but find where it
  feeds customer behavior).  Plus the **customer CHIBI NPC** (note #2 above) = the walk-in/browse system.  Includes
  L1b (real pix) + the roster scan.  Deep-dive WITH the recorded trace (don't guess the outcomes).
  **★★ IN PROGRESS 2026-06-22 (autonomous) — the user's RETAIL recording `rec-20260622-182618` (raw preserved
  in `runs/recordings/`) → scenario `house-firstcust-cutscene-day2`: load→tutorial→ESC-skip→first-customer→a LONG
  story-cutscene series (title drop, Recet hammering the recettear sign, dusk)→DAY 2 (idle, Tear flying around +
  brooming = new mechanic).  16291 frames; worked in capture windows.  USER DIRECTIVE: grind to 1:1, NO MVPs.**
  Landed: **(a) ESC-skip PAUSE_OPEN** (`2406349`) — the cc08 cancel-tutorial choice box (b5e4) now raises the
  PAUSE_OPEN anchor (retail's b150 choice box; port had split b150).  **(b) b150 MODAL-HOLD** (`3f6c407`, RE'd via
  a retail call-trace probe `_firstcust-skip-probe`) — retail does NOT clear the cancel-prompt b150 on the Yes; it
  HOLDS through leave→wrap-up→first-customer arrival, closing at the live greeting b534==2 (FUN_00435612).  New
  `s_skip_modal` latch.  Port now traverses + bit-matches retail through skip→wrap-up→greeting→1st BARGAIN.
  **(c) IN-SHOP CHIBI NPC system (FRONT note #2)** — **cs-walker-rng-phase ✅ ROOT-CAUSED + FIXED 2026-06-22
  (autonomous), RE §19.**  The cc08 first-customer NPC pump had TWO RNG bugs, drilled via the new
  `tools/cs_walker_drill.py` (port↔retail per-frame rngcalls+npc-state diff; +npcfr/npcsp/npcn/npcdr probes in
  0x48670f, port + `retail_fields.json`).  **(1) PUMP GATING** — the engine runs `FUN_0047019f` UNCONDITIONALLY
  in the cc08==4 arm (all.c:87432; the caller 40591 gates house_update only on the be94 load-SCREEN counter, NOT
  b1cc; the master tick self-gates on b1cc, the pump does not).  The port wrapped the pump inside its `b1cc != 2`
  load-suppression block (added for the arrival/camera transition) ⇒ at the f406 entry the pump was suppressed
  ~17f during the d3e load while retail's kept incrementing s_cs_frame ⇒ the WHOLE spawn cadence mis-phased
  (retail npcfr increments from entry off=1 while b1cc==2; port stuck at 0 until off=18).  **Fix:** run the pump
  in engine order (after the RNG-neutral arrival, before the self-gating master), unconditionally.  **VERIFIED:**
  npcfr/spawn/sparkle align, spawn fires at npcfr=30 on BOTH, per-frame rngΔ 55/60, cumulative drift 4/400f (was
  wholesale mis-phase).  **(2) GHOST SLOTS** — retail resets the NPC array (FUN_0046f892) at the HOUSE load
  (34885) + cs leave (60337) so slots are free before any cc08 tick; the port ports neither, and only the
  forced-sale session-init path reset them ⇒ the scripted-tutorial (sell_active) + general first-customer
  (roster-scan) paths left 30 ZERO-INIT ghost slots (ACTIVE==0, read as active) the now-unconditional pump ticked
  ⇒ spurious draws.  **Fix:** reset unconditionally at the top of `customer_service_session_init` (mirrors
  retail's house-load+leave, covers ALL paths).  **VERIFIED:** npcn 30→max-1, 0 ghost frames, BOTH scenarios.
  3364 host pass.
  **★★ b5a4 BASE-PRICE ✅ FIXED 2026-06-22 (autonomous, user-directed) — the f406 first customer now haggles the
  RIGHT item.**  `cs_kind_select` was f404-ONLY (hardcoded `b5a4=0xc0`, item id 3 = Steel Sword, base 3000).
  Ported `FUN_00461303`'s THREE branches: f404→0xc0 (scripted), **f406→0x3ea00 (id 4008 = WALNUT BREAD, base
  100)** + the showcase scan (`SAVE_BANK_FIELD_DISPLAY_GRID` row-0, find the `(handle&~0x3f)==0x3ea00` cell →
  `b564=1` iff its slot ∈ {1,2,3,4,11,12,13}=`DAT_005c6be0`; **b564 gates a 2-rng particle emit @all.c:60240 ⇒
  RNG-load-bearing**, so it's ported data-driven not stubbed), else→rng-drawn (PORT-DEBT(cs-kind-select-general),
  not reached by f404/f406 traces).  **VERIFIED:** the port's first-customer base flips 3000→**100** + ask
  100→120 == retail; +2 host tests (3366).  **★ USER-CONFIRMED 1:1 2026-06-22 in the v3 viewer** ("the item is
  correct") — added a `{caprange:[0,2600]}` to the scenario so the first-customer haggle is Trace-Studio-viewable.
  **★★ BUT the user flagged (same viewer) a NEW visible gap — the REACTION-LINE VARIANT + Recette's FACE: port
  shows recette msg09 "How much should I?…" + open-eyes; retail "Capitalism, ho!" + closed-eyes-smile.**  PRECISELY
  DIAGNOSED (not the item): `cs_pick_line(0,9,0)` takes the live `!f404` arm ⇒ `variant = rng_next15() % count[9](=2)`
  and `s = variant + 9*VARIANTS` indexes BOTH the text AND the sprite — so ONE rng draw decides the line + the face,
  and the port lands on the wrong PARITY.  Root = the rng-VALUE drift (same drift as offer 119 vs 123): **db054 is
  ALIGNED (274, frozen in cc08) — NOT the driver.**  The driver is the **目玉商品 sparkle** (`player_ctrl_display_sparkle_emit`,
  gated `g_sim_frame_count(DAT_0438b8cc) % 8 == 3`, 3 rng_next_unit × occupied display column ⇒ ~25 draws/fire): the
  port's `g_sim_frame_count` is **off-phase by 3** vs retail (sim-frame origin differs) AND the port's async **d3e
  load runs ~8 frames longer** (b1cc=2 ~15f vs ~7f; varies 15-18 run-to-run) ⇒ the sparkle fires a DIFFERENT NUMBER
  of times during the load ⇒ an ODD cumulative-draw drift ⇒ the rand%2 variant flips.  **{csloadpin} load-bracket
  pin LANDED 2026-06-23 (port `4eeb88a` + Frida `9b92b0a`, RE §20)** — holds b1cc==2 for N=24f on both targets
  (port: a frame counter ANDed with the async-done check `customer_service_load_pin_elapsed`; retail: a 2nd
  tutloadpin-CModule blocker on the d3e tails 0x452af9/0x452b24, objdump-verified; the engine load is a
  `CreateThread` race, no min-gate ⇒ genuinely non-deterministic).  **BUT it is ONE PIECE, NOT the full rng-match
  (I over-claimed "SOLVED 1:1" — CORRECTED).**  `--target both` (all 4 loads pinned) gave port==retail offer
  119/150, but the `orv3_window` v3 capture gave retail **117** (port stays 119): the offer DRIFTS because (a) the
  v3 harness pinned only 1 of 4 loads (TOOL gap) AND (b) other rng consumers diverge — the **shop-window NPCs**
  (bg_npc, user-flagged), the in-shop chibi NPC (cs-walker), the sparkle g_sim phase.  A "matches" off one drive
  was a lucky alignment.
  **★★★ NEW ACTIVE ARC (user directive 2026-06-23) — MAKE THIS TRACE FULLY PHASE-MATCHED + DETERMINISTIC
  side-by-side (the FOUNDATION; see CLAUDE.md "phase-matched DETERMINISTIC trace is the FOUNDATION").  "Go as
  slow as we need to have everything 1:1, side by side, deterministic." Every divergence = a PORT gap or a TOOL
  gap to CLOSE (no accepted residuals).**
  **★★★★ REFRAMED 2026-06-23 PM (user directive) — the minimum FOUNDATION is a FULL RNG SURVEY at the CONSUMER
  LEVEL, NOT single-value matching.**  After the {csloadpin} tool fix below the trace studio STILL shows the port
  reaction line "How much should I?..." (retail "Capitalism, ho!") — so matching ONE rng OUTPUT (b574=119 in the
  call_trace) did NOT make the rendered variant 1:1.  User: *"just matching 1 rng element isn't really useful — for
  rng stuff you need a full rng survey and work at the rng-consumer level until we have every consumer ported, then
  the rng stuff will fall into place IF the phase pinning is also solid.  That is the minimum foundation."*  So the
  plan is two pillars: **(A) survey EVERY rng consumer** in the cc08==4 + skip→wrap-up→first-customer window and
  **port each 1:1** until the per-frame rng draw COUNT+ORDER matches frame-for-frame (tools: the CLEAN rng-callsite
  capture — NO phasepin, which contaminates the measurement per RE §8.4 — + `flow_diff --rng-drill` +
  `cs_walker_drill.py`, per-frame `rngcalls` port vs retail); **(B) make the PHASE PINNING solid** (the work-list
  below).  ONLY when both hold is the variant/face/offer robustly 1:1 — do NOT declare any single value "matched"
  as progress.  Plan: **`docs/plans/rng-consumer-survey.md`**; existing rng findings:
  `findings/scene1-rng-stream-parity.md`, `findings/freeroam-rng-consumption.md`.
  PHASE-PINNING work-list (pillar B): **(1)** fix the `{phasepin}`-BREAKS-the-wrap-up TOOL gap
  — its bg_npc LCG re-seed stalls the skip-path iv1_7 CONV_POSE (1176 blinks, never reaches the customer), so the
  bg_npc + g_sim CANNOT currently be pinned (likely the Frida lazy re-seed vs the port's immediate one — isolate +
  fix); **(2)** the **WALL-CLOCK pin** (user idea: hook GetTickCount/QPC/timeGetTime → a virtual clock synced at
  anchors, like `{rngseed}`) for the load/time non-determinism; **(3) ✅ TOOL FIX LANDED 2026-06-23 (necessary, NOT sufficient)** the v3-harness
  `{csloadpin}` coverage (was 1 of 4 brackets vs 4 in `--target both`) — root was NOT the early-exit/v3_arm but a
  **Frida worker-tail re-arm RACE** (RE §20.1): the CModule blocks the d3e tail only if `flags[0]==0` on entry, but
  `csloadpinPresentRelease` left it 1 (open) between loads ⇒ a FAST (warm-cache) load's worker passed the tail
  before `csloadpinTick` re-armed ⇒ b1cc cleared in 1f ⇒ the bracket never armed; `--target both` fired 4 only by
  luck (slower cold-disk loads won the race).  Fix: a 3rd `csloadpinTick`/`tutloadpinTick` branch restoring the
  default-BLOCKED invariant between loads (`!armed && b1cc!=2 → flags=0`).  v3-VERIFIED on 2 captures (committed
  `9c455f3`): retail now arms **4** brackets + the offer VALUE matches (b574 119→119→119→150 port==retail; was
  retail 117).  ⚠ **BUT this did NOT fix the rendered variant** — user 2026-06-23 PM: the trace studio STILL shows
  port "How much should I?..." ⇒ a single matched value is NOT stream parity (the `cs_pick_line %2` variant draw
  still lands wrong because OTHER consumers diverge ⇒ pillar A above).  Keep the tool fix (the v3 harness SHOULD
  pin all 4 loads), but it is necessary-not-sufficient.  **(4)** the cs-walker
  in-shop NPC (`PORT-DEBT(cs-walker-rng-phase)`, the npcsp divergence) — a pillar-A CONSUMER; **(5)** the bg-window
  NPC match — a pillar-A CONSUMER.  VERIFY ≥2 captures + BOTH harnesses bit-frame-by-frame.  Drill:
  `cs_walker_drill.py`, `flow_diff --rng-drill/--verdict`, the v3 state/draw panels.
  **★★ PROGRESS 2026-06-24 (RE §21/§21.1, plan `rng-consumer-survey.md`):** (a) **目玉 sparkle (g_sim%8) — ✅ PINNED
  1:1** via the new **`{gsimpin:[F,V]}`** op (commit b2ba55f) — a clean g_sim phase pin (port + frida, mirror of
  `{gframe}`/`{rngseed}`, touches ONLY g_sim, NOT the bg-NPC reseed `{phasepin}` bundles).  Root: the port's
  `g_sim_frame_count` origin differs (intro skip) AND was NON-deterministic run-to-run (entry gsim 785/792 = +1/+2
  phase); pinned to retail's recorded 810 at the f406 entry (`{gsimpin:[0,811]}`, the value = the house_update gsim
  at the fire frame).  Verified: port gsim now deterministic + bit-identical to retail from the entry to the b534=6
  reaction, sparkle fires at the SAME offsets.  +2 host tests (3368).  (b) **cs-walker spawn — ✅ ALIGNED** (npcsp
  matches off 30/60/90…; `(4)`'s "npcsp divergence" was a drill-column MISREAD — that pillar-A worry is CLOSED for
  this trace).  (c) **ROOT of the remaining ≈+10 LCG diff at the reaction = bg_npc `(5)` position phase** — the
  shop-window townsfolk (`FUN_0046f2a3`) sit at different positions port↔retail (warmup off a different LCG origin),
  so boundary-respawns (3 draws) fire on different frames (first at off 7), mis-aligning the LCG at the off-30
  cs-walker spawn ⇒ cascade.  The sparkle pin is NECESSARY, NOT SUFFICIENT; **NEXT = pin bg_npc to retail's NATURAL
  positions** (capture `DAT_073a7f80` SoA + a `{bgnpcpin}`, or fix the `{phasepin}`-wrap-up break) — synthetic
  re-seed corrupts the variant vs the recording.  Method recipe (cumulative-rngcalls-from-entry + FUN_005041f6
  ret_va attribution): RE §21.1.
  **★★ bg_npc `{bgnpcpin}` ✅ LANDED 2026-06-24 (RE §21.2, `2207c1a`+`d9abe4e`) — option (a).** A CONDITION-gated agent
  dump captures retail's NATURAL `DAT_073a7f80` SoA at the f406 entry (the segment-gated `{memsnap}` can't — the
  wrap-up desync stalls it); `{bgnpcpin:[F,[150 dwords]]}` pins `g_scene1_bg_npc` field-by-field (port struct NOT
  byte-compatible w/ the 0x64 engine record). Host-tested (+5), baked, fires at off 0. NPC0 = inert cs-leave slot.
  PORT-ONLY (retail = un-pinned source).
  **★★ rng-drill ✅ UNBLOCKED 2026-06-25 (RE §21.3) — the condition-gated rng hook.** The boot-installed rng-callsite
  hook taxed the initial cad868 Continue-load (trampoline/draw) ⇒ retail mis-timed the esc-skip ⇒ ran the SCRIPTED
  tutorial, never reaching the b51c==0 f406 entry. **Fixed:** defer `installRngCallerHook` from boot to the f406
  entry (`cc08==4 && b51c==0`, the agent's `segtraceTick`, same gate as the bgnpc SoA dump); AUTO-enabled in
  `frida_capture` when the segtrace carries a `{bgnpcpin}` (so `scenario-test … --target both --call-trace` just
  works). VERIFIED: the deferred retail drive (`…215600Z`) ARMS the hook @frame 14658 + reaches the entry (the
  boot-hook run `…203209Z` never did, NEW_GAME@206→HF@14367 stretch→scripted tutorial). NB the initial load STILL
  stretches ~14000f under the 2000+ call-trace trampolines (the rng-hook tax SPECIFICALLY was the esc-skip tipping
  point, not the whole stretch). **FIRST DRILL VERDICT** (`cs_walker_drill` port `203038Z` ↔ retail `215600Z`,
  --span 200, both bgnpcpin+gsimpin): **14/200 frames diverge in per-frame rngΔ; gsim%8 ALIGNED off≥1; cs-walker
  spawn cadence (npcsp) ALIGNED.** Remaining rngΔ gaps (off 8,30-34,57,60,82,107,132,191,198; biggest = the off
  30-34 spawn cluster, retail +11) = the pillar-A consumer COUNT gaps to survey. off=0 = a measurement boundary.
  **★★ SURVEY 2026-06-25 (RE §21.4) — the off-30-34 cluster traced to TWO roots; bg_npc off-by-one FIXED, two new blockers.**
  Per-frame attribution (`/tmp/rng_sxs.py`: retail exact `lcg_rows`+ret_va vs port `rngcalls`-delta; the existing
  `215600Z` call_trace ALREADY carries the `FUN_005041f6` ret_va rows — no `--rng-callsites` re-drive needed).
  **(1) bg_npc off-by-one PIN TIMING ✅ ROOT-CAUSED + FIXED + port-VALIDATED.** off-7 diverged because (a) retail's
  bg_npc entry state is **non-deterministic run-to-run** (NPCs tick a variable # of frames during the variable load
  ⇒ the PORT-ONLY `{bgnpcpin}` pins to a STALE capture that can't match a fresh retail drive — see
  [[openrecet_bgnpc_nondeterministic]]) AND (b) the `{bgnpcpin}` landed **one tick late (effective off1, not off0)**:
  a segtrace `base+0` op fires at *anchor+1*, so the LOADING_END-segment pin (off0) hit off1; the dump captures D₀
  (off0 pre-tick) ⇒ +1 lag. **Fix:** move the bgnpcpin op to the **CONV_POSE_END segment** (off0-effective) +
  **bilateral** pin (agent WRITES the canonical to retail too; `frida_capture` forwards it; `--no-bgnpc-pin-retail`
  = capture mode). **STEP-1b VERIFIED:** port (off0-effective, re-baked to 215600Z's own dump) ↔ natural retail
  215600Z = off-7 respawn 1:1 (6==6), stream bit-identical off 0-28. Port logic is correct; only the pin timing was off.
  **(2) cs-walker GRID gap (off 29-32) = a SEPARATE pillar-A consumer**, NOT a bg_npc cascade: with off-7 fixed +
  identical rng VALUES at off 29, the cs_npc_tick (FUN_0046fbee) retarget still retries a different # of times ⇒ the
  furniture-layout grid `DAT_074b28e8` (rebuilt by `shop_display_grid_rebuild`/FUN_0048960d) differs port↔retail in
  the probe region (cols 1-8, rows 1-7). The retarget LOGIC matches the decompile (verified) ⇒ the grid CONTENT
  differs. **NEXT ARC:** dump retail's `DAT_074b28e8` + the port's grid, diff, root-cause the rebuild/furniture gap.
  **★★★ (3) THE FOUNDATIONAL BLOCKER — the cross-target WRAP-UP DESYNC (pillar-B task #1) just MANIFESTED as a HARD
  STALL.** The bilateral `--target both` drive STALLED retail in the iv1_7 wrap-up CONV_POSE (**1180 CONV_POSE_BLINK,
  CSE fired only for the scripted tutorial, never reached the f406 entry**) — the exact "1176-blink" signature.
  NOT my changes (the bilateral write never fired — PINNED:0; no retail pre-entry effect; NO {phasepin} in the scenario)
  and NOT deterministic (215600Z reached the entry; this run didn't) ⇒ **load-dependent intermittent**. So bg_npc/g_sim
  pinning CANNOT be reliably validated until this is fixed. The port side is solid (reaches the f406 entry @2230,
  off0-effective). **THE deterministic-trace goal requires fixing the wrap-up desync FIRST** (a TOOL gap: the segtrace
  {wait} stalls when a wrap-up anchor — DLG_LINE_CLEAR? — doesn't fire under retail's load jitter). Lead: §21.2.
  **★★ ROOT-CAUSED 2026-06-25 (RE §21.5) — deeper than framed; PARTIAL fix + a NEW blocker found.** From the
  stalled vs OK agent logs: the blink-stall = retail's ESC-skip ARM intermittently failing — the wrap-up is SKIPPED
  via ESC (arms the "Skip this event?" box FUN_0046c2cb) + X "Yes"; the arm gate (all.c:67129) is
  `1<skip_prompt(DAT_073a3e18) && box(DAT_073a3dec)==0`, and skip_prompt RESETS on dialogue re-init (67083), so under
  load jitter the recording's single ESC@+25 hits skip_prompt<=1 ⇒ no arm ⇒ the line runs FREE (TEXT_ANIM_END) ⇒ the
  skip-structure {wait}s deadlock (1176 blinks). Built a CONDITION-GATED **ARM-ONLY** skip driver in the agent
  (re-post ESC until the box opens; let the recording's X confirm) — `--skip-wrapup`, EXPLICIT-only (NOT auto-on).
  **✅ it FIXES the blink-stall** (re-drive `001837Z`: clean DLG_LINE_CLEAR@+68 mid-reveal, 3 blinks not 1176).
  **❌ BUT the f406 entry STILL doesn't fire** — a DEEPER softlock: after the skip, no CSE#2 / CONV_POSE_END, instead
  a HOUSE_FREEROAM→PAUSE_OPEN→LOADING reload LOOP. The skip→f406-entry transition is itself **load-jitter-fragile**
  (whether the skip lands the entry vs a free-roam softlock depends on the non-deterministic CreateThread-race wrap-up
  LOAD durations). ⇒ this is the **load-determinism FOUNDATION (pillar-B)**, NOT just an input-driver fix. NEXT
  (needs direction): pin the wrap-up LOAD brackets (a csloadpin-analogue for the iv1_7/cs-leave loads) so the skip
  timing is reproducible, OR a phase-matched condition-gated confirm. The bilateral {bgnpcpin} is RULED OUT as the
  softlock cause (its gate cc08==4&&b51c==0 never held — it never fired). RE §21.5.
  **★★★ RESOLVED 2026-06-25 (RE §21.6) — §21.5's "load-determinism FOUNDATION / needs a wrap-up loadpin" was a
  MISDIAGNOSIS; the post-skip softlock was the DRIVER's OWN ESC spam, and the arm intermittency was the driver being
  OFF.** Two roots: **(1)** the driver's continuation gate was `!g_bgnpc_soa_dumped` — it kept posting ESC through the
  ENTIRE post-skip free-roam window; after the skip fired + the box closed, each ESC re-opened the pause box (`b150` =
  `PAUSE_OPEN`) → reload → BLOCKED the f406-entry arm (gated not-mid-pause) → the self-reinforcing
  `HOUSE_FREEROAM→PAUSE_OPEN→LOADING` loop (tell-tale: the loop ONLY ever appeared WITH the driver). **Fix:** latch
  `g_wrapup_box_was_open` once the POST-tutorial skip box (`DAT_073a3dec`, gated `seen_tutorial && cc08!=4` — the
  tutorial's own box also touches it) opens ⇒ NEVER post ESC again ⇒ the entry window is undisturbed. **(2)** the driver
  was NOT auto-on through `scenario-test` (frida_capture left `skip_wrapup` explicit-only); my first re-drives ran it OFF,
  so the "1 OK / 1 stall" was the RECORDING's esc@25 working only on a long (call-trace-stretched) load = load-phase luck,
  NOT the driver. **Fix:** re-instated auto-on with `{bgnpcpin}` (mirroring `rng_hook_defer`). The DRIVER arm is
  load-ROBUST (posts ESC every frame; `skip_prompt`=121>>1 by line-show ⇒ box arms on the 1st post, next frame) where the
  recording's single esc is not. **✅ VERIFIED DRIVER-ON 2/2 reproducible (`014156Z` call-trace + `014427Z` no-call-trace,
  different load phases):** clean `DLG_LINE_CLEAR` skip (no `TEXT_ANIM_END`) → `CSE#2` → `bgnpc-rng off 0..199` (f406 entry,
  `b534`→1 greeting), 2-3 PAUSE_OPEN (not 79), 3 blinks (not 1176). **The f406 entry is now RELIABLY reached → the
  deterministic-trace FOUNDATION blocker is CLEARED; the rng survey (pillar A) can proceed on a reproducible retail entry.**
  Added throttled `wrapup_dbg` agent logging + freed ~31 GB of stale stall-drive `frames/` artifacts. **OUT-OF-SCOPE
  follow-up (future iv1_8/day-2 arc):** a SEPARATE blink-stall in the DAY-2 cutscene region (~frame 21259+, the driver
  auto-disables at the f406 entry so it doesn't cover later cutscenes) — does NOT affect the cc08==4 survey window/caprange.
  **NEXT once the trace is deterministic (BOTH pillars):**
  iv1_8 (the post-first-customer EXTRA_SPRITE cutscene, f402-triggered, PORT-DEBT P3) → the cutscene series →
  day-2 brooming.  Sub-agent retro: `docs/AGENT-WORKFLOW.md` "Calibration — 2026-06-22".
  **★ HARNESS — call-trace 9p fix 2026-06-21 (user request "make it all go directly to windows storage").**
  The scenario arms `{calltrace}=[0,9500]` (~100 MB) and the exe wrote it line-buffered + fflush-per-frame
  over the 9p `\\wsl.localhost` mount → ~150 write syscalls/frame → full-haggle drives crawled / looked hung.
  Fixes: (1) `call_trace.c` full-buffers (`_IOFBF` 1 MiB) + drops the per-frame fflush (the real lever — kills
  the syscall storm on 9p AND NTFS); (2) `scenario-test.py` STAGES call_trace/d3d_trace on a Windows-local
  NTFS dir (`local_stage_root` → `C:\…\cap`) + copies back, so the per-frame writes never touch 9p; (3) new
  `--no-calltrace` (skip the {calltrace} window for a fast verification drive when you only need anchors —
  ~40fps vs ~15fps; NB it lengthens the load FRAME-count so don't use it where a timeout-gated segment cares)
  + `--max-duration-ms` (override the wall-clock ceiling for long flows past the default).
  **Drive the haggle ONLY with `--capture-trigger-only` (or the v3 window tool) — a naive BMP drive times out
  at the wall ceiling long before the haggle.**
  (2) **ESC "Cancelling tutorial?" skip during cc08==4 ✅ LANDED 2026-06-20 (`031581d`).**  The fix was NOT
  the prologue `skip_event` path (an earlier note guessed that) — retail's cc08==4 ESC is a SEPARATE mechanism
  (`FUN_00453384` @ 0x4533ce → **`FUN_0045e6a5`**): during the scripted tutorial (b51c==1) it opens the
  **"Cancelling tutorial. Are you sure?"** Yes/No choice box + latches **b5e4**; the master-tick b5e4 poll
  (all.c:60168-60186) drives Yes→leave / No→resume; **Yes → the b520 leave/dissolve** (all.c:60325-60396:
  `fade_phase1_start(0,0x5a)` → free-roam cc08=1 + clear f404/f3ff/f400/f405).  A live customer (b51c==0) is
  NOT skippable + ESC never opens the pause during cc08==4.  v3-verified vs retail
  (`house-customer-skip-tutorial`, ESC@off300+Z@off360): prompt pixel-1:1, b520 0→1→2 → cc08 4→1 matches the
  ~0x5a-frame dissolve.  +2 host tests; 3340 pass.  **Residual (Task-1-adjacent, NOT a softlock):** retail
  plays a Tear wrap-up dialogue ("And that is…") after the skip; the port returns to plain free-roam (the
  post-haggle wrap-up is part of the tutorial→first-customer flow, gap #1's territory).
  **PAUSE_OPEN/b150 at the BARGAIN ✅ FIXED for round 1
  (`2fb5b39`, RE §9.6):** the port now fires PAUSE_OPEN@f3128 + PAUSE_CLOSE@f3259 (131f, matches retail b150
  130f) via `customer_service_bargain_active()` (scripted b608==4) OR'd into the anchor's pause_active (signal
  only — no gameplay effect).  **Round 2 navigation UNVERIFIED** — the exe self-exits at ~f3360 (a WM_CLOSE, NOT max-frames/duration/
  window-end) ~3f after the round-2 segment's first input, cc08 still 4 + the machine progressing; a HARNESS
  early-exit on the unfired round-2 `{wait}`, not a confirmed scripted gap.  Next: find/fix the stuck-wait
  WM_CLOSE (or add a `{wait …, timeout}`), THEN re-verify rounds 2-5.  RE §9.6.
  The cc08==4 SELL haggle-UI render + the "!" tooltip + the camera-hint overlap remain DONE for this trace.
  **Residual (the user's "other than those... it looks 1:1"): the central-UI band's remaining 6.8%** = (a) the
  digit CURSOR pulse (`b5b4` per-frame phase — zeroes on a b5b4-aligned frame), (b) the 3D CHARACTER (Tear)
  1-frame anim phase bleeding into the band edge, and (c) a small ~2px per-glyph text-precision residual on the
  prompt/markup (`per-line pose precision` PORT-DEBT; isolate on the viewer at equal b5b4 + solo the text draws,
  then if real cross-ref the `0x467664` prompt + markup right-align scale vs font_draw_text_centered/_right).
  Note #1 (a free-roam "!" exclamation tooltip @HOUSE_FREEROAM#1+79 we don't render) is a SEPARATE pre-cc08 gap.
  **★ SIDE-BY-SIDE TOOLING (user ask 2026-06-19: "figure out how to drive retail and port side by side
  properly on this") — ROOT-CAUSED + CORE FIX LANDED 2026-06-19 (autonomous, `--join-anchor`).**  The data
  was ALWAYS alignable: the haggle aligns occ2/CSE-relative within a CONSTANT phase (the `b544` monotonic
  counter is port = retail+2 across the WHOLE window — off 200/600/1000/1500/2000/2400 all +2, no drift; and
  measured from the post-load HOUSE_FREEROAM that +2 CANCELS the occ2→load-HF spacing diff (62f port / 60f
  retail) → phase 0).  **Real cause (NOT the load-stretch, NOT a per-frame phase): a WINDOW-OCC MISMATCH.**
  The two sides' windows armed on DIFFERENT occurrences of the base anchor — port on HOUSE_FREEROAM#2 (@637,
  the post-cc08 asset-load HF, since the port's shorter prologue puts present_first there) but retail on
  HOUSE_FREEROAM#1 (@2137, post-prologue) — so `_window_occ` re-based each to a different SEMANTIC origin and
  the shared post-load HF became window-occ **2** on the port vs **3** on retail ⇒ the (anchor,occ,delta) keys
  never matched ⇒ 119/2698.  Empirically proven by re-keying both sides from CUSTOMER_SERVICE_ENTER (occ-1 on
  BOTH, port @636 / retail @2293): **2498/2698 pair** (the rest = honest load-region gaps).  **✅ FULLY
  LANDED end-to-end (core + viewer), opt-in, zero-regression:** `FrameIdentity.key_of_present_rebased` +
  `LoadedSide.reindex` + opt-in `--join-anchor` threaded through `orv3_sync` (pairs.json),
  `orv3_view.write_view_json` (the viewer's view.json COLUMNS, via `reindex`), `orv3_state.collect/
  build_state_rows` (the STATE-panel labels), and the `orv3_window --join-anchor` CLI (default None →
  byte-identical; +`test_rebased_join`, 14 v3 tests pass).  **Validated on house-customer-tutorial-a361c768:
  view.json default = 119 paired / 59 state-aligned columns (UNCHANGED); `--join-anchor
  CUSTOMER_SERVICE_ENTER` = 2498 paired / 2499 state-aligned (gaps 5160→402).**  **USE IT:** `nix develop
  --command python3 tools/trace_studio_v3/orv3_window.py house-customer-tutorial --window 0:2700 --state
  --join-anchor CUSTOMER_SERVICE_ENTER --launch` → the native viewer's port|retail|diff + state panels are
  now identity-synced across the whole haggle (the ADDSIGNED fix + future cc08 work verifiable in-tool).
  Caveats (both minor, left as-is): (1) `merge_anchor_seq` column ORDERING is best-effort (uses global occ);
  the dominant `(HF,2,delta)` bucket self-orders by delta so the timeline reads right — revisit only if it
  looks out of order; (2) `orv3_view.build_view` (the legacy PNG-bake path, NOT used by `orv3_window`/the
  native viewer) is NOT threaded — add the same `join_anchor` pass-through there if the PNG bake is ever
  needed for cc08.  **Also** the content-match-by-occ2-offset recipe still works for one-off shots: render
  port idx (occ2_port − present_first + N) vs retail idx (occ2_retail
  − present_first + N) for the same haggle offset N (orv3_shot --frame; this session used N≈2406 → port 2405 /
  retail 2563, both ask=1300, 1:1).
  Remaining cs-render PORT-DEBT: per-line pose precision; cs-render-priceinput (b5d0 digit-entry, inert);
  cs-price-trend (FUN_004361b2 High/Low tint→0); cs-haggle-prompt-live (b51c==0 live machine); extend the
  RETAIL 0x48670f hook with b598/b58c for full state-panel parity.
  **Plan + caveat: RE §8.6/§8.7/§8.7.2/§8.7.3/§8.9.**
  The **rng-rate gap ✅ RESOLVED 2026-06-19** (`8cd0389`, RE §8.8): §8.5's deep open problem cracked
  via a pool-dump + spawn-hook probe — the cc08==4 ambient particles are **type-0x1f, emitted by
  Tear's COMPANION controller** (FUN_0048a833 wing-glow sparkle, gated `db054%4==0`). Root cause:
  **retail FREEZES db054 in cc08==4** (frozen at 156, %4==0 → emits every frame); the port kept
  incrementing it (scene1_sim.c non-walk fallback) → every 4th frame = 1/4 the rng. Fix: freeze
  db054 on `cc08==4`. **Verified: db054 156 (port==retail), rng rate 5.53→10.03/f == retail
  10.02/f.** The first-customer offer is NON-DETERMINISTIC (recording 1536 / capture 1548 / post-fix
  1572 — retail varies), so rate+order matching is the goal, not a single value. Landed 2026-06-17 night (autonomous): the **`{wait,timeout}` harness
  unblock** (`47cdd8c`, port captures 1200/1200 BIT-EXACT) + the **haggle math**
  `src/customer_haggle.{c,h}` (`d0ac215`, DISASM-exact, +9 host tests). **2026-06-17 day (this session):**
  (A) ⚠⚠ **CORRECTION² (2026-06-17 PM, USER-CONFIRMED) — the tutorial sell is the SCRIPTED machine
  `FUN_00461c00` (`b51c==1` path), NOT `FUN_004658ab`** (RE doc **§3.7**, supersedes §3.5/§3.6's
  machine choice). Proven from the SAME `34f44b18` capture: **b534 only ever ∈{0,1}** (the
  kind-machine states 2/6/0xf are never entered), `b544` climbs 2350+ without resetting, yet
  base/`b5a0`/`b574` all change *during* b534==1 — only `FUN_00461c00` (dispatched from the master
  tick's b534==1 arm when `b51c!=0`) does that. `b56c=1`/`b5a8=2` come from `FUN_0045edaa`'s
  **sell-active (f404) else-branch** + `FUN_00461303`'s f404 head (the f406 tutorial branch would
  give b56c=13); offer `1536 = base 1200×1.28` with NO f406 override clinches the f404 path.
  **Corroborated by the port's own `tables_tuto.{c,h}`** (already parses `tuto1..3.txt` + names
  `FUN_00461c00` as the consumer). §3.5 inferred FUN_004658ab from b5a8==2 alone and missed the b51c gate.
  (B) extended the **0x48670f probe** (`retail_fields.json`) with the customer-service state
  (b534/b5a8/b56c/b574/b584/b590/ask/base/…) + **captured BIT-EXACT ground truth** (offset 0:2700,
  2700/2700) → cache `runs/studio-v3-cache/house-customer-tutorial-34f44b18/retail`. Empirical
  timeline: cc08=4 entered during the load; greeting b534=1@off90 (b524>0x77 & b52c>=0x20, base price
  computed there); arrival anim b5a0@off969; first customer offer b574=1536/b584=1@off2440.
  (C) **Chip 1 ✅ LANDED** (`db9f02f`): `src/customer_service.{c,h}` — the entry `FUN_0045edaa` (the
  forced-kyaku-13 f406 branch + the load-bearing 1-RNG customer-count draw + worker spawn). NB the
  CAPTURE uses the OTHER (f404 sell-active) branch — see Chip 2a.
  **Chip 2a ✅ LANDED + host-tested (2026-06-17 PM, this session):** the corrected scripted-sell
  scaffold — `FUN_0045edaa`'s **sell-active else-branch** (`b51c=1`, queue[0..2]={kyaku:1,kind:0},
  count=3), `FUN_00461303`'s **f404 head** (`b56c`=queue[0].kyaku=1, `b5a8`=2, offered handle
  `b5a4`=0xc0→item id 3), and the **master tick `FUN_00462403` skeleton** (idle b524 counter +
  greeting trigger b524>0x77&b52c>=0x20 → b534=1 + base/ask compute [base=item.price·count;
  ask=ftol((float)item.price) since b5a8==2] + the b5a0 arrival ramp scaffold + the b534==1+b51c →
  scripted-tick dispatch). The off-window branches (leave/closing/sold-pause/pose/bubble-pos/fx) are
  tagged PORT-DEBT. +3 host tests reproduce the capture's greeting frame (b56c=1, b5a8=2, base=ask=3000,
  b534=1); 3329 host pass, exe clean.
  **Chip 2b ✅ LANDED + host-tested (this session):** the SCRIPTED machine **`FUN_00461c00`** ported
  in full (transcribed from by-address/461c00.c with its literal LAB_* goto structure) + the helpers
  **`FUN_004623bc`** (GOTO id→PC), **`FUN_0045ff11/31`** (digit count/edit), **`FUN_004622d9`** (the
  price-confirm poll), and the **`FUN_00460161` binding** (`cs_offer_up` → the pure
  `customer_haggle.haggle_offer_up` over `g_kyaku.records[b56c]`). The PC `b604` walks
  `g_tuto[b5b0*200+pc]`; opcodes wired: dialogue (CHR0/1 speaker toggle), price-set (op 2 → base/ask
  from `g_item` item 2), PRID/PRIA (op 3/4 → digit editor + the offer at the 3→4 Z transition),
  conditional GOTOs (op 5/6/0xc/0xd/0xe → `cs_goto` on ask/base ratio thresholds), SET_INITIAL/TAGN/
  TOUT, end. PORT-DEBT: the render/audio/cursor externals (`FUN_0046098f` dialogue-line buffer,
  `FUN_00499519` SE, `FUN_00435612/693` cursor, details overlay) + the item-menu / sword-select
  (op 10/0xb) buy-from-customer sub-flows. **+1 host test (`cs_scripted_first_offer`) reproduces the
  capture trajectory END-TO-END: idle→greeting base=3000 (item 3) → scripted op-2 base→1200 (item 2)
  → op-4 PRIA + Z → offer `b574`=1536 (=1200·128/100, no f406 override), round `b584`=1.** 3330 host
  pass, exe clean.
  **cc08==4 ENTRY/dispatch/call-trace WIRED 2026-06-18 (`7e163bb`)** — the bVar3 Z-at-counter
  entry (f404→session_init→cc08=4), the per-frame master-tick dispatch, the `notify_loaded`→b1cc=1
  fix, the broadened 0x48670f call-trace (cc08 + the cs fields). 3330 host tests pass. **USER
  DIRECTION 2026-06-18: port the FULL tutorial up to the first real customer; this trace must play
  in full on BOTH sides before moving to real (kind-2) haggling.** ⚠ **The scenario is a *LOAD*
  (Continue) trace, NOT new-game** (loads cad868 slot 0 = pre-entry shop state → walk to counter →
  scripted haggle → 5 rounds → closing dialogue → first customer).
  **TRACE-REPLAY BLOCKER ✅ ROOT-CAUSED + FIXED 2026-06-18 — it was a SEGTRACE (tooling) bug, NOT
  the cc08/LOADING_END timing the first pass guessed.** Probed via a `_probe-cust-load` scenario +
  an early `{calltrace}` over the walk window (the always-on `0x452cde`/`0x4850ec`/`0x48670f` VAs):
  on the port the Continue-load fires `LOADING_END`+`HOUSE_FREEROAM`@~f476 with `cc08=1` set 1 frame
  before — so **LOADING_END IS the free-roam boundary** (no 156f dialogue gap; the "f310 HOUSE
  render" was just the load-fade window). Driving the walk segment ALONE the player walks px
  −0.30→−1.50 to the counter and the Z@rel156 flips **cc08→4** (3/3, load-stretch-immune). The real
  cause: `input_segtrace.c` measured the `{wait LOADING_START, timeout 60}` timeout from segment
  ENTRY, so it fired at **rel60 — before the segment's own walk@rel66/Z@rel156** — eating them ⇒
  player frozen ⇒ no counter ⇒ no cc08==4. **FIX (`input_segtrace.c`):** measure the optional-wait
  timeout from the segment's LAST entry (a segment's recorded inputs must all apply before its
  terminating wait can time out); +1 host regression test (3331 pass); only ever DELAYS a timeout
  (no-op for every other committed timeout-wait, all rel0). **Validated end-to-end**: the extended
  probe with the committed timeout-60 now reaches the d3e `LOADING_START` + **cc08==4**, 2/2 across
  load-stretch; the full committed scenario now fires the 2nd `LOADING_START/END` (the entry's d3e
  load) where it stalled at 1 before.
  **FULL-WINDOW DRIVE + 2 STATE FIXES LANDED 2026-06-18 (this session) — the cc08==4 state machine
  is now verified ~1:1 to the customer arrival.** Drove the port over the full caprange `[0,2700]`
  with `--state` vs the retail cache `34f44b18` (align on the cc08 entry, NOT db054 — it freezes in
  cc08==4). (1) **fileidx BUG ✅ FIXED (`2f360ff`)**: the player-Z sell-counter entry seeded
  `set_script_file(2)` → `cs_queue_advance` took the `b5b0==2→FUN_00460fa7` (b5a8=1) path + SKIPPED
  the kind-select ⇒ the customer never bound (b5a8=-1, b56c=0, base=-1, no offer). Disasm of the
  entry (0x488bbf `push 0x0`) + retail's `b5a8=2` (only from `FUN_00461303`, fileidx∉{1,2}) ⇒
  fileidx=**0**; the "push 2" sites are the OTHER (autonomous) entries. → greeting binds 1:1
  (`base=3000 ask=3000 b56c=1 b5a8=2`). (2) **Chip 2c ✅ LANDED (`5c48508`)**: the dialogue
  text-reveal / script-advance (`FUN_0046098f` line-load + `<C>` split, the master-tick pose section
  `b278`+`b548` reveal counter @60198-60234 was wrongly stubbed inert, the `b55c` reveal-complete =
  the `FUN_00465db4`@62835 count with budget=`b548` ⇒ **1 char/frame**). → the script now advances
  greeting→dialogue→**op2** (base 3000→1200, arrival `b5a0`) @off+1026, **greeting→op2 span 873 vs
  retail 879 = 6 frames over ~880 (~1:1 reveal timing)**. 3331 host tests pass; port self-verify
  1743/1743 BIT-EXACT.
  **CUSTOMER_SERVICE_ENTER anchor + re-window ✅ LANDED 2026-06-18 (`e72daa8`) — the offer fires on
  the PORT (was window-blocked).** Added the `CUSTOMER_SERVICE_ENTER` anchor (cc08 non-4→4) to the
  port (`anchor_trace`) + retail agent, and re-anchored the haggle window on it: the old
  `{wait LOADING_START/END, timeout 60}` chain expected retail's SECOND d3e load (occ3) the port lacks
  ⇒ the port's window opened ~158f off and the Z missed PRIA. New: `{wait CUSTOMER_SERVICE_ENTER}` →
  `{wait LOADING_END}` (occ2 = master-tick start, both sides) → inputs occ2-relative (+60). **Port:
  `b574=1536 b584=1` (round 1) @ occ2+2501, with greeting/op2(occ2+1030)/ask→1300(occ2+2406) on
  retail's EXACT offsets.**
  **Chip 2d ✅ LANDED + VERIFIED (`99214a8`) — occ3 ported, but §8.3's occ3-hypothesis is REFUTED; the
  offer (1536) is unchanged.** Ported `FUN_00452d3e(1)` at the master-tick queue-advance tail
  (all.c:60998-61000; param **1** by disasm `0x463435 push 0x1`, the b13 thread proc). Re-drove the port
  (bit-exact 2698/2698): the port **now emits `LOADING_START/END` occ3 @ frame 687-688** (occ2+61,
  1-frame inert) — matching retail's occ3 @ 3060-3061 (occ2+59, 1-frame). The occ3 durations MATCH, yet
  the port's offer stays `b574=1536` (retail fe530872 = 1548) ⇒ **occ3 was NOT the cause.**
  **REAL BLOCKER — a cc08==4-SPECIFIC per-frame RNG consumer the port STUBS (a real logic gap).** What's
  1:1: base=1200, ask=1300, b56c=1, b5a8=2, b534=1, AND the offer fires at the **IDENTICAL occ2-relative
  offset (occ2+2501) on both** (proven via the `0x47be92` rng-probe + `0x48670f` state-probe joined on
  occ2). But between occ2 and the offer **retail draws 25051 rng, the port only 13812** (~half) — offer
  `1200·init_eff/100`, init_eff 129(retail)/128(port) = a 1-step phase diff downstream of the ~11000-draw
  gap. Rate: retail `7` SMOOTH/frame +`31`@8 (=10/f); port `1`/frame +`7`@4th-frame +`25`@8 (=5.5/f). The
  8-frame +24 spike MATCHES (sparkle). **NOT the bg-NPCs (hypothesis tested + REFUTED):** the port's rate
  is bursty AND continuous across the cc08 1→4 seam, AND retail's own HOUSE free-roam is ALSO bursty
  (`house-loaded-display-pinned`, cc08==1: 4.8/f `[7,1,1,19,…]`) = same shape as the port ⇒ the bg-NPC
  rng MATCHES. Retail's SMOOTH `7`/frame is cc08==4-ONLY, **constant from off 0 (b534==0 idle, pre-greeting)**.
  **CONSUMER IDENTIFIED (static, high-confidence): `FUN_0047019f`** — the cc08==4 on-screen-character pump
  the port SKIPS (PORT-DEBT(cs-arrival-anim)). The cc08==4 arm calls it EVERY frame (all.c:87432) before the
  master tick; it loops over the character array `DAT_073a6ea8` calling `FUN_0046fbee` (3 rng/char) per active
  actor (player+companion+customer ≈ 7/frame). Ruled out: master-tick rng is conditional (haggle states,
  ported); the prologue customer-spawn (`FUN_0046f914`) is gated `f404==0` (inert here); `FUN_00461068` fires
  once at b524==0x14. This is a **DRIFT (missing-consumer logic gap), NOT an accepted RNG pillar** — the COUNT
  differs (port draws half); the 1536 matching `34f44b18` is phase coincidence (the FORMULA is right,
  init_eff's draw lands wrong). **NEXT — Chip 2e: port `FUN_0047019f`** (the `DAT_073a6ea8` character pump +
  per-char `FUN_0046fbee` arrival-anim/rng; a SIZEABLE arc = the cc08==4 character sim) ⇒ closes the rng gap +
  the offer aligns; (opt) rng-drill to confirm the count; the missing `{phasepin}` is a separate policy gap
  (does NOT fix this); THEN the 5 `PAUSE_OPEN` rounds + closing, THEN **Chip 3** (`FUN_0046602e` portrait +
  `FUN_00466b7b` BARGAIN!! UI + `FUN_00465db4` glyphs). Full diagnosis: `findings/customer-service-haggle-RE.md` §8.4.
  **v3 TOOLING ✅ FIXED 2026-06-18 (`ec6b494`): the port drive no longer dumps BMPs** — it was leaking
  ~8 GB/run of unused screenshots (MULTI keep-trigger piggybacked on `capture_backbuffer()`'s readback);
  `--capture-trigger-only` fires the GetBackBuffer keep-trigger but writes no BMP (v3 reconstructs from
  the draw-call stream). Full-window port drive: 29 s, 0 BMPs, replay 2699/2699 bit-exact. Drive WITHOUT
  `--launch` when autonomous (it opens the blocking viewer; the user opens the studio themselves).
  `plans/trace-studio-v3.md` "CORRECTION — port MULTI drive was leaking…".
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
- **GUILD buy-list SOLD-OUT status overlay ✅ PORTED + 1:1-VERIFIED 2026-06-13 (`d69f3e2`
  text + `1049cdd` colour fix).** The retail buy-row loop tail draws a per-row status overlay
  Ghidra DROPPED (bare `FUN_0047ca05` at all.c:66703-66708; recovered from objdump
  @0x46b7f9-0x46b8a3): mode-7 cap==0 → "Out Of Stock" (`0xff9b0000`,×1.2) / "Not For Sale"
  (`0xff00007f`,×1.0, trend≤−2); mode-6 bit5 → "Adventurer's Possession" (`0xff00c87f`,×0.65).
  **The colour was wrong first pass (committed unverified — DON'T):** the port drew the rows
  under COLOROP=MODULATE (dark red) but **retail uses ADDSIGNED** (salmon) — `FUN_0046b00a`
  sets `SetTextureStageState(0,COLOROP,ADDSIGNED)` @66607, resets MODULATE @66775. Found via
  the v3 draw panel (`orv3_draws`: every retail row draw colorop=8, port was 4). Fixed: rows
  under ADDSIGNED + grey-0x7f passthrough diffuses (white/grey content unchanged, the colour
  diffuses render as retail). **Exercised + verified on the NEW `guild-buyout` scenario** (buy
  the Worn Sword out to cap 0 → "Out Of Stock"; tight caprange EXTRA_SPRITE_END+480..840 ⇒
  fast ~108 s port drive): sold-out frame **meanabs 0.4→0.04, OOS text RGB [196,156,124] on
  BOTH sides, row draws colorop=8 both** (draw-match 140→164). PORT-DEBT: the FUN_00468ddc
  availability greying (icon 0x4d4d4d) + price-trend tints (FUN_004361b2) stay deferred. RE:
  `merchant-guild-RE.md` §2. **Lesson (`feedback_verify_1to1_before_done`): always exercise +
  trace-compare a renderable port before committing — RE of the diffuse value isn't enough
  without the inherited COLOROP.**
- **ACTIVE ARC → PAUSE MENU (in-game ESC menu, mode 9).** Plan + full RE:
  **`plans/pause-menu.md`**. Scenario **`house-pause`** (`0250347`); v3 cache
  `house-pause-f1bf56e7` has the retail menu rendered. **M1 ✅ DONE 2026-06-13** (`52133bd`):
  the pure-C state machine — `pause_dispatch` (FUN_00453384 trigger/toggle), `pause_menu_setup`
  (FUN_0047f2f6 entry-list build), `pause_menu_update`/`pause_menu_nav` (FUN_0047fa76/00480614).
  Key RE: the ramp counters ALREADY existed in sim.c (`g_sim_counter_998/99c` + `g_sim_mode_9a0`
  == DAT_06a49998/9c/a0, ported as the dormant FUN_004532df) — M1 added the SETTER + consumers.
  Entry list = base **[1,6,2,3,4]** (Items·Encyclopedia·Options·Save·Exit) +type-0 Status when a
  party exists (DAT_0741bed8>0, PORT-DEBT stub 0) +type-5 in a dungeon (saved_mode==1 &&
  *DAT_068dd2f0>0). 16 host tests. **M2 ✅ DONE + USER-CONFIRMED 2026-06-13** (`aef7d89`):
  wired the trigger (esc_dispatch in-game→pause_dispatch, gated on worker idle) + the ramp
  consumers (sim ramp==3→mode9+setup+worker spawn, dispatch skipped while ramping; render c99c
  pump + the mode-9 render) + the primary-worker **case-9** load (objdump 0x4529c6 — the LIVE
  path; the C4E secondary FUN_00452e75 is dead) + `pause_menu_render` drawing the **pause_bg_rete
  backdrop**. v3: port re-drives **240/240 BIT-EXACT**, at offset 190 renders the pause backdrop
  (1 draw = bg_rete Recette art) where it used to swallow ESC; retail = 10 draws (full menu).
  **User-confirmed: "we're rendering the correct artwork for the pause menu."** The cyan is the
  mode-9 clear (retail clears cyan too — both `0xff17f0ff`; retail covers it with the menu draws,
  so the port is faithfully at "bg_rete drawn, rest pending"). Retail doesn't slice bit-exact
  across the pause's mid-window async load (known v3-replayer limit; verified by direct frame
  render). **M2b ✅ DONE 2026-06-13 (`e00f622`):** ported the rest of `FUN_004820ba`'s
  resting-menu draws — the **COLOROP=ADD option-list rows** (Items·Encyclopedia·Options·Save·
  Exit Game: per-row icon+label from pause.tga; selected row = bigger 160×128 icon + sin-flash +
  sub_anim slide), the **"PAUSE MENU" header** (COLOROP=MODULATE, src(64,384-320,424)→
  dst(368,428,256,40)), and the **overlay tail** (choice_box_draw / hand cursor / save-frame — all
  self-gating no-ops at rest). Geometry+diffuse from objdump 0x4820ba-0x482400 (decompile dropped
  the register-built diffuse + FP consts; verified vs the .rdata float table). **v3 draw-program
  1:1:** orv3_draws shows the port's 3 draws now match retail's **[1] bg_rete / [2] option-list
  (colorop=7 ADD, ×20 tris = 5 icons+5 labels) / [3] header (colorop=4, ×1)** exactly; the
  direct-frame overlay (port119 vs retail119 — the async load gaps the standard join) shows the
  menu at retail's exact staircase positions (feed "PAUSE MENU M2b"). **Pixel-1:1 confirmation is
  ENTANGLED with M2c:** the missing board background swamps the diff (bg_rete art is black/matching,
  everything else white = port's cyan clear vs retail's board).
  **M3 — the captured-screen BACKDROP [0] + the radial-blur composite ✅ DONE + USER-CONFIRMED 1:1 2026-06-13**
  (parity ledger; user: in-game "the pause menu looks correct on port", v3 viewer "confirmed 1:1 when
  settled" + the open animation eyeball-1:1 — the open-anim TIMING can't be frame-confirmed, the port's
  async pause-asset load shifts it = the load-seam phase pillar, accept)
  (`screen_rt.{c,h}` RT infra + `scene1_fx_overlays.c` = FUN_00454191 body + the render_dispatch
  c99c==2 capture redirect + `render_quad_add_unscaled` = FUN_00404e98). The port now re-renders
  the live scene into the capture RT (#56) at pause-open (c99c==2), builds the **2-pass radial-blur
  composite ONCE** (c99c==3: downsample RT#56→1280×256 RT#57 clear `0xff0000c8`, then a 24-prim
  12-tap zoom-blur RT#57→RT#56 clear `0xff173c8c`), and samples the blurred RT#56 full-screen as
  **[0]** every rest frame with the open/close fade ramp (`min(c99c·0x16,0xff)`, attenuated past
  0xc). **Verified vs the retail v3 cache** (re-drove the port over `house-pause` HOUSE_FREEROAM+
  160:80): the **RT command program is bit-exact** — frame 40 capture = SetRenderTarget(RT#56)+
  Clear black+scene+restore (2 SRT); frame 41 blur = Pass A (RT#57, clear 0xff0000c8, 2-prim quad
  RT#56) + Pass B (RT#56, clear 0xff173c8c, **24-prim** RT#57) + [0] (2-prim RT#56→backbuffer),
  every clear+prim-count matching retail's f40/41. **Pixel proof (`replay.exe --history`):** blur
  frame f41 = **gt8 0.46% / meanabs 0.11** vs retail (near-identical); resting f119 RIGHT-half (the
  menu) matches (Δ≈+2). The capture is 97 draws/2597 prim vs retail 124/2677 — the 80-prim delta is
  the known benign HOUSE batching + the retail-only inert `b494` draw (0 px). **Load-bearing fix —
  the clear gate (FUN_004547ab L51070): the backbuffer is NOT cleared while c99c∈[3,0xc]** (the
  backdrop+menu composite over the prior frame so the blur fades IN over the captured scene); the
  first cut cleared every frame ⇒ the partial-alpha ramp frames were non-deterministic — the port's
  resident `replay.exe --verify-hashes` caught it (232/240, frames 162-169 FAILED; retail 240/240).
  Skipping the clear for c99c∈[3,0xc] → **port self-verify 240/240 BIT-EXACT** (the authoritative
  proof the backdrop render is deterministic + structurally 1:1). RE/empirics:
  `pause-menu.md` M3; quirk §123; tooling `trace-studio-v3.md` P5. **The M2b "pixel diff is swamped"
  entanglement is HALF-resolved:** the backdrop is now correct (right half 1:1); the remaining f119
  LEFT-half diff (Δ+42 mid-left, 43% of the frame) is **just the missing calendar/numbers below**
  (the orv3_draws diff = port 4 draws / retail 10; the 6 retail-only being the calendar + glyphs).
  **M2c — calendar / merchant-rank XP bar / numbers ✅ DONE + PIXEL-BIT-EXACT 2026-06-13**
  (`scene_pause.c`: the [4-9] block + helpers `pause_day_index`/`pause_period_end`/
  `pause_weekly_quota` = FUN_00482033/59/d997). Re-drove the port over HOUSE_FREEROAM+120:240:
  the resting-menu **draw program is ALIGNED — 10/10 draws matched by content hash, 0 divergent**
  ([4] panel 6q / [5] today 1q / [6] period-end 3q / [7] quota 6q / [8] gold 4q / [9] level 1q,
  every tex/colorop/tri/**geometry hash** = retail), and **history-replay pixels are BIT-EXACT
  (meanabs=0, gt8=0%) across every resting pair** (the pause menu is fully static at rest ⇒ NO
  phase residue — the entanglement that swamped M2b's diff is GONE). Port self-verify still
  **240/240 BIT-EXACT** (M3 backdrop preserved). **RE correction:** `_DAT_0438b91c` is the
  animated **merchant-rank XP** (the bottom-left-HUD value), NOT a "current-day" — [5]/[6]'s
  `+0x2c3f8`/`+0x2c3fc` are the XP level-start/next thresholds, the real calendar day is CARD_DAY
  (+0x2c3ec). The stubbed `g_dat_0438b91c` (`stage_post_load.c:562`) is DEAD (no consumer) — NOT
  touched; the render reads the bank directly, bit-identical at rest. **✅ USER-CONFIRMED 1:1
  2026-06-13** ("can confirm pause menu is fully 1:1 based on the pushes" — parity ledger; ⇒ the
  WHOLE resting pause menu — backdrop + option list + header + calendar + numbers — is now 1:1).
  **PORT-DEBT(pause-xp-anim):** the XP-display animator (shared with the merchant HUD's stubbed
  `set_xp`) stays unported (only matters mid-rank-up).
  **SAVE submenu (entry type 3) RENDER ✅ DONE + PIXEL-1:1 + USER-CONFIRMED 2026-06-14**
  ("looks good to me"; `7affa5f` render + `351654e` tooling). Full status: `plans/pause-menu.md` M4.
  The card-list render `FUN_0049b556` + perm `FUN_0049b537` are ported as **`save_picker.{c,h}`**
  (shared with the title picker), driven by the pause type-3 commit + wrapper `FUN_004812e4` +
  the `sub_anim>0` render dispatch (`scene_pause.c`); transcribed 1:1 from objdump (Ghidra
  dropped the FP consts, SetTexture args, the 4 format strings, the TIME seconds vararg).
  **Trace `house-pause-save`** (ESC → 3×down → Z; `PAUSE_READY` anchor, v3 join 299/299).
  **Verified vs the retail v3 cache** (PAUSE_READY+250 at rest, `orv3_shot` per-frame render):
  the submenu is **PIXEL-1:1 — meanabs 0.13, gt8 0.00%, max 2** (occupied slot-0 card:
  portrait/clock + Merchant Level 1 + SCORE 0 + LOOP 0 + TIME 0:03:50, + NO-DATA cards +
  headers all match; feed "M4 pause Save submenu port|retail|diff"). +4 host tests (3253).
  **DRAW-PROGRAM divergence (OFF-SCREEN, root-caused, accept):** retail draws 3 pages (center +
  L/R WINGS, gate `DAT_09643520>=10`) where the port draws 1 (3.00× box geometry, zero pixel
  impact). `DAT_09643520` is the **title continue picker's** open-ramp (left at 10 post-Continue);
  the port's `title_continue_picker.c` uses instance fields, never ramps the shared global ⇒
  `g_save_picker_hpage_anim` stays 0. **PORT-DEBT(save-picker-wings)** — closes with the title
  picker render port (shares `FUN_0049b556`). Quirk §124. **Verify caveat (M3 limit):** the
  per-frame `--verify-hashes` self-verify is DIVERGENT (can't rebuild the RT backdrop); `orv3_shot`
  composites it ⇒ its pixel-diff is the valid check.
  **SAVE submenu NAV (M4b) ✅ DONE + USER-CONFIRMED 1:1 2026-06-14** (`b46858a`+`461d873`; user
  "can confirm the save picker is 1:1" — parity ledger; ⇒ the save picker render+nav+globals is fully 1:1).
  `FUN_0047f5bc` → `pause_save_submenu_update` (scene_pause.c), dispatched at sub_anim==10 / type-3
  (FUN_0047fa76 L82031): **U/D ±1** cursor + c894 row-slide (→scroll ±1), **L/R ±3** + c898
  column-slide (→scroll ±3, clamp 0..97), **B-cancel** (slides closed); A-confirm/commit = M4c.
  **New `SAVE_PICKER_READY` anchor** (port + frida agent) rebases the nav past the per-side-variable
  pause-OPEN ramp phase (async load) so the inputs are picker-time-relative. Trace
  `house-pause-save-nav` (ESC→3×down→Z→{wait SAVE_PICKER_READY}→DOWN×5/UP×4→B; join 239/239).
  **Verified vs the retail v3 cache** (`orv3_shot`): cursor/scroll **PIXEL-1:1** — the
  breathing-aligned nav frames + the post-close option list are bit-identical (gt8 0.000%).
  **`save-picker-shared-globals` ✅ CLOSED same day** (`461d873`, user-requested "fix the selected
  brightness"): the selected-card breathe (`_DAT_09643574`) + the off-screen wings (`DAT_09643520`)
  were BOTH the engine's ONE shared render (`FUN_0049b556`) modeled by the port as two divergent
  copies — the title Continue picker (retail runs it, ~100+ frames) never carried its history into a
  later pause Save submenu. Fix: the title render now drives the SHARED `g_save_picker_frame`
  (breathe) + mirrors `g_save_picker_hpage_anim` (wings). Re-drove the port ⇒ nav **PIXEL-1:1**
  (gt8 0.000%, was a 22% breathe beat) AND **draw-program 1:1** (0 draw-divergent, was 169). Quirks
  §124/§125 (retail ground-truth).
  **SAVE submenu A-confirm + COMMIT (M4c) ✅ DONE + USER-CONFIRMED 1:1 2026-06-14**
  (user "the push looks good" — parity ledger; ⇒ the save picker render+nav+commit is 1:1)
  (`scene_pause.c` A-branch/commit_tick/progress-bar + `save_io.c` `save_io_commit_slot`). A on the
  cursor's slot (`FUN_0047f5bc` 0x47f889): SE 0x143, then an EMPTY slot commits at once
  (`g_pause_save_phase`=1) / an OCCUPIED slot pops the **"Overwriting file. Are you sure?"** choice box
  (`FUN_00434def`, the exact single-row string), Yes→commit / No|B→cancel (`choice_box_poll`). The
  phase>=1 commit (0x47f63f): card-field snapshot (0xb381 type + clear the 0xb75a/0xb78e preview
  blocks — pixel-invisible, the picker render reads GAME_MODE/SCORE/… not these) + the streamed save
  jingle + **`save_io_commit_slot`** (= `FUN_004905a8`: working bank → save bank + checksum + write
  save.dat/_save.dat, `{savefile}`-sandboxed) + the 1→0x3c counter. The **save-progress bar**
  (`FUN_004812e4` c89c>0): 2 item_win.tga quads over the selected card under ADDSIGNED — empty-bar
  frame + a fill quad growing with c89c/30, grey pulsing with the −128·sin the card uses, alpha fading
  past c89c>0x34 (geometry + the dropped sin amplitude from objdump 0x481358-0x481408). **Verified vs
  the retail v3 cache** (new **`house-pause-save-commit`** trace; `orv3_shot`): **port#N vs retail#N+1
  (the 1-frame async-pause seam) ~0.12% / meanabs ≤0.10 across the whole window** — dialog (0.07%),
  commit ramp, progress bar, post-commit card all within M4's accepted breathe/seam envelope; only the
  dialog-close transition frame elevated (1.86%, the box text 1 frame off at the seam). +6 host tests
  (3270 pass). **PORT-DEBT(save-commit-dungeon):** the dungeon "Saving here…<BR>" warning
  (`FUN_00434ceb`) + the `FUN_0047f1a0(0)` town swap (inert in the house); **PORT-DEBT(save-card-type-
  modes):** the mode-6/0xb card-type branches (un-modeled data, unreachable; pixel-invisible). RE/full
  status: `plans/pause-menu.md` M4c. Tooling: `v3cache`
  `localappdata_v3` got an env override + `/mnt/*/Users/*/...` glob fallback (cmd.exe WSL interop
  was wedged, blocking the cache step).
  **ENCYCLOPEDIA submenu (type 6) — GRID RENDER + NAV ✅ DONE + PIXEL-BIT-EXACT 2026-06-15**
  (`src/encyclopedia.{c,h}` = FUN_0049f012 setup / FUN_0049efb8 cursor / FUN_0049f365 nav /
  FUN_0049f8b8 render; `b7318e7`+`a15e4a8`+`189a103`). The item catalog (図鑑): a 33-category
  horizontal carousel of 3-column item grids (icon+name, packed-discovered slot table sorted by
  rank+id), completion %, bottom description, scroll/category arrows. Wired into scene_pause.c
  (setup in pause_menu_setup, nav-commit type-6 open, sub_anim==10 update / sub_anim>0 render
  dispatch). **Verified vs the retail v3 cache: gt8 0.0000% BIT-EXACT** on the usual save
  (house-pause-encyclopedia, ESC→1×down→Z) AND the **hacked MAXED save** (house-pause-encyclopedia-max,
  f693fbd6 every item discovered — every item's icon+name across the full 33-category carousel
  matches retail). New **ENCYCLOPEDIA_READY anchor** (port+frida) for clean nav sync (join
  559/559). **The maxed full-grid nav caught a LATENT pause bug — the shared hand cursor's
  6-frame slide (FUN_004356cd) was never ticked during mode 9** (sim.c ticks it for modes 1/8/6,
  mode 9 skips the per-mode dispatch; the Save submenu hides its hand cursor so it never surfaced)
  → ported the tick to pause_menu_update's tail (engine L82104); maxed grids → bit-exact,
  house-pause M2/M3 stays bit-exact, no regression, +8 host tests. RE: `findings/encyclopedia-RE.md`.
  **ITEM-DETAIL OVERLAY (`FUN_0046a336`, A-press) ✅ DONE + BIT-EXACT 2026-06-15** (`f51af86`):
  the item stats card (Title / Type / Effect [equipment ATK/DEF/MAG/MDEF · food Recovers HP/SP ·
  medicine Gives Max HP] / Base Price / Highest+Lowest Sale Price) over a data_win panel; helpers
  `enc_category_is`/`enc_hp_recovery`/`enc_sp_recovery`/`enc_maxhp_tier` + exposed
  `chara_equip_item_stats`. **gt8 0.0000% BIT-EXACT** across the whole open→shown→close window on
  `house-pause-encyclopedia-detail` (usual save: examine the Worn Sword). Decompile-var trap caught:
  `local_18`=X-shift / `local_30`=Y-shift (lines 3/4/5 set X=−32, not Y). **PORT-DEBT(encyclopedia-
  detail-combine):** the combine/recipe icon grid is gated on the adventurer model
  (`DAT_0741bed8`, the `pause_status_count` stub 0) — closes with the party port. **✅ USER-CONFIRMED
  1:1 2026-06-15** ("can confirm the encyclopedia feels right and looks 1:1 visually" — parity ledger;
  the WHOLE encyclopedia screen — grid + nav + detail overlay — is now 1:1).
  **OPTIONS submenu (type 2) — config-panel render + nav ✅ DONE + BIT-EXACT + USER-CONFIRMED 1:1
  2026-06-15** (`b3288b8` render+nav+tests, `fc14f2d` OPTIONS_READY+nav trace; user "can confirm its
  1:1"): the in-game Options menu — Music/Sound/Voice volume 0..9 numerics + Message Speed
  SLOW/MED/FAST + Unread Text Skip OFF/ON, A/B exit (saving when dirty). Shared render
  **`settings_panel.{c,h}`** = `FUN_0049c050` (dungeonbord.tga backdrop + 5 rows label+value,
  selected row yellow / rest grey under MODULATE2X, "Saving" overlay); the engine shares this ONE
  render with the TITLE settings submenu — `g_scene_state` picks 5 rows (pause) / 6 (title, adds
  "Clear Save Data"); the label/value/word strings were recovered from the PE .data/.rdata (the
  decompile dropped them). Update `pause_options_submenu_update` = `FUN_0047fc44` (U/D row %5, L/R
  adjust the row's slider + per-row SE + clamps — Music re-applies BGM, Voice is silent — A/B
  dirty/exit-save → `save_io_commit_slot(-1)`) + the nav-commit type-2 init (row 0 + cursor snap
  168,168). **gt8 0.0000% BIT-EXACT + draw program 56/56 ALIGNED** on `house-pause-options` (resting
  AND the slider nav: cursor on every row + numeric/word value changes, every probed offset gt8=0).
  New **OPTIONS_READY anchor** (port+frida) — the robust v3 join anchor (fires AFTER PAUSE_OPEN on
  both sides; PAUSE_READY is STRADDLED by PAUSE_OPEN ⇒ the window mispaired, the 0-paired desync —
  join 239/239 once rebased). +9 host tests (3290). **PORT-DEBT(options-config-arena):** the
  exit-save writes the arena but the port's live slider values aren't synced into the save-header
  config region (config is module state, pixel-invisible). **Follow-up (tooling/cleanup):** the
  TITLE settings render (`scene_title.c` `scene_title_settings_render_panel`, still its own copy)
  should adopt the shared `settings_panel_render` — needs a title-settings trace to verify (the
  engine's `FUN_0049c050` is ONE function; the pause copy is now the verified shared one).
  **ITEMS submenu (type 1) — inventory grid render + nav ✅ DONE + BIT-EXACT 2026-06-15, AWAITING
  USER 1:1** (`f639d3d`): ESC → Z opens the in-game Items submenu (entry type 1, index 0/default in
  the house list) — the player's inventory via the display-menu grid (`FUN_0046b00a`) sliding in
  from the right (Swords tab: Worn Sword/Dark Sword + category banner + the bottom description
  panel), over the M3 pause backdrop. Engine HOUSE path: setup `FUN_00480614` type-1 (slide-activate
  `FUN_004682c5` + `display_menu_open(mode 5,1)` inventory scan + cursor snap), update `FUN_0047ff40`
  (`display_menu_update(1)` grid nav + B-close), render `FUN_0048196b`→`FUN_0046b00a(640-sub_anim*64,
  0)`. Threaded a `slide_x` param through `display_menu_render` (shop/guild pass 0.0f). **Also FIXED a
  latent SHARED-display_menu price-label bug the Items submenu exposed:** the engine keys the
  description price label off the SCENE (`DAT_0438b1c0`) — guild (scene 6) "Purchase/Sell Price-",
  every other scene "Base Price-" — but the port keyed off the display-menu MODE, and mode 5 is shared
  by the guild SELL *and* the pause Items, so it mislabeled the pause Items "Sell Price-" (now
  scene-gated; the guild buy stays "Purchase Price-", unchanged). New **ITEMS_READY anchor** (port +
  frida) — fires AFTER PAUSE_OPEN on both sides (PAUSE_READY straddle), v3 **join 199/199** across the
  +4410-frame async pause-load stretch (was 43/199). **Verified vs the retail v3 cache on the new
  `house-pause-items` trace: the grid + rows + description + the corrected "Base Price-" label are
  PIXEL-BIT-EXACT** (description panel gt8 0.0000%/0 px; absolute best resting pair gt8 0.0422%); the
  ONLY residual is the shared hand-cursor's sub-pixel BOB (~330 px, x443-510 y130-179) at the 1-frame
  async-pause seam — the accepted seam/bob phase pillar (same class as M4c). +3 host tests (3292).
  **PORT-DEBT(pause-items-dungeon):** the dungeon variant (display_menu mode 6 + place-an-item /
  use-medicine / equip-readout, `FUN_0047ff40` DAT_074b28a4!=0 branch) — needs a dungeon-pause trace.
  **EXIT-confirm (type 4) — ✅ DONE + USER-CONFIRMED 1:1 2026-06-15** (`b32be5d`
  mechanics + `8303fef` title re-init; user "Looks 1:1" — parity ledger): ESC → 4×down → Z opens the **"Returning to title screen.
  Are you sure?"** choice box — **No** cancels to the menu, **Yes** quits to the title (fade-out →
  scene→0 → title load → fade-in). Ported the nav-commit type-4 (`g_pause_exit_confirm=1` + cursor
  snap, NO submenu) + `pause_exit_confirm_update` (choice_box Yes=1/No=2 + the quit sequence:
  `g_pause_exit_phase` 1→0xf → `fade_phase1_start` → `fade_is_done` → `sim_set_mode_9a0(0)` +
  `d3d_pool_release_type(0xc)` + `g_scene_state=0` + `worker_load_spawn` + `fade_phase_out_start`).
  **The DIALOG + the fade-out were 1:1 from the first cut** (user "can confirm the prompt looks
  correct"; per-frame brightness tracks retail — dialog ~78, black ~0, title ~190). **The
  title-re-init blocker is now CLOSED (`8303fef`):** the Yes→title used to land in the WRONG sub-state
  (the boot Continue-picker left `submenu_state==1` ⇒ the load-game card list rendered) because the
  engine's title re-init runs on the primary load **worker case-0** (`LAB_0045293d` case 0 @ 0x452961
  = `FUN_004733d5` asset reload + `FUN_0049a3a3` menu reset) which was UNREGISTERED in the port ⇒
  `worker_load_spawn` was a no-op + the title resumed stale. Ported `FUN_0049a3a3` as
  **`scene_title_reinit`** (pure-C) + registered `worker_load_set_cb(0, …)` at boot: resets the title
  anim block (`scene_title_anim_init_fresh` → submenu_state=0, cursor_anim=0, menu_folding_out=1,
  continue_mode=0), rebuilds the menu (`save_io_scan_for_title_menu`+`scene_title_menu_init` =
  `FUN_0049a43d`), snaps+hides the shared hand cursor (`FUN_00435693`/`FUN_00435612`), clears the
  forced BGM (`music_clear_forced_track` = `FUN_00499560`). `FUN_004733d5` reload = faithful no-op
  (title textures persist from boot; the Exit's `d3d_pool_release_type(0xc)` frees only pool-tagged
  assets). **Verified vs the retail v3 cache** (`house-pause-exit`, re-drove the port over
  PAUSE_READY+180..480): the resting title menu is **PIXEL-BIT-EXACT — gt8 0.0000%, meanabs 0,
  maxdiff 0** at every settled offset (RECETTEAR logo + NEW/LOAD/Options/Exit, not the load-picker;
  feed "Pause EXIT (type 4) → quit-to-title — FIXED"). The studio auto-join is seam-split (the port
  emits no PAUSE_CLOSE), so the compare is by aligned window-index, per this scenario's known
  load-seam. +3 host tests (3298). **PORT-DEBT(exit-house-teardown):** `FUN_00474d92` (house D3D
  free) — faithful no-op here. ⇒ **all 5 base pause entries now interactive + the Exit lands on the
  correct title menu**; the broader title-screen render arc (the load-PICKER render is already wired
  + shared via `save_picker`) is the next focused effort — first chip below.
- **TITLE-SCREEN RENDER ARC → Continue/load PICKER unified onto `save_picker_render` ✅ DONE +
  BIT-EXACT + USER-CONFIRMED 1:1 2026-06-15** (user "was able to confirm it was 1:1" in the studio —
  parity ledger). The title's Continue/LOAD-GAME slot picker
  (`FUN_0049c644`→`FUN_0049b556`) had its OWN copy of the card-grid render
  (`scene_title_continue_render_panel`) — a 2nd FUN_0049b556 port that DIVERGED from the verified
  shared `save_picker_render` (the pause Save submenu's, M4): it (a) skipped the off-screen wing-page
  rows retail draws (an extra `slot < 0` guard the engine lacks) and (b) **never drew the bottom-right
  game-mode tag** (`PORT-DEBT(modetag)`). **Unified the title onto `save_picker_render`** (`save_picker.{c,h}`
  gained a `plaque` param — the "Merchant Level" banner texture differs by scene: pause passes
  `g_scene_pause_pause`, the title passes its own `SCENE_TITLE_TEX_PAUSE`, since `g_scene_pause_pause`
  is unloaded at the title; the title wrapper copies its picker state into the shared
  perm/count/restricted/wing-anim globals) — **closing modetag + the wings + the >999h TIME clamp +
  the duplicate (−218 lines)**. New **`TITLE_PICKER_READY` anchor** (port `anchor_trace.c` + frida
  agent — rising edge of scene 0 / submenu_state 1 / cursor_anim 10; the title picker has NO async
  load so it's a clean +0-stretch join) + v3 `title-load-picker` scenario (converted to a
  `{caprange}` window). **Verified vs the retail v3 cache** (`title-load-picker-60516ab3`, join
  119/119 @ +0 stretch): the unification took the draw program **162→193 draws = retail's 193**
  (matched 160→**190**; the wings now draw like retail) AND tightened pixels **946→54 px / gt8
  **0.0000% BIT-EXACT** (maxdiff 2 — the 54 sub-LSB px are the rotated-portrait rasterization +
  the 1-step breathe seam, same accepted class as the pause). +1 host test (3299). The residual 3
  "replace" draws = the off-screen wing portraits' OOB-perm garbage (invisible, UB on both sides).
  **PORT-DEBT(title-picker-overwrite):** the code-4/6 new-game "choose a file" overwrite-dim +
  per-slot avail (`g_save_picker_avail`) stay unported (inert for a normal Continue; needs a
  new-game-into-slot trace).
- **TITLE-SCREEN RENDER ARC → Options/settings panel adopted onto `settings_panel_render` ✅ DONE +
  BIT-IDENTICAL + USER-CONFIRMED 1:1 2026-06-15** (user "the options panel push also looks correct" —
  parity ledger). The title's `scene_title_settings_render_panel` was
  a 2nd copy of `FUN_0049c050`; the pause Options arc had produced the verified shared
  `settings_panel_render` (the engine shares ONE FUN_0049c050 — `g_scene_state` picks 6 title rows
  [adds "Clear Save Data"] / 5 pause rows). Made the title a thin wrapper calling the shared render
  with its OWN dungeonbord (`SCENE_TITLE_TEX_DUNGEON`; savewindow NULL ⇒ the dirty-exit "Saving"
  overlay stays PORT-DEBT, inert for view/nav). Restores fidelity the old copy dropped (the engine's
  ADDSIGNED→MODULATE2X back-to-back COLOROP + the saving overlay). New **`TITLE_SETTINGS_READY`
  anchor** (port + frida; scene 0 / submenu_state 2 / cursor_anim 10 — no async load ⇒ +0-stretch
  join) on the v3-converted `title-options` scenario. **Verified vs the retail v3 cache**
  (`title-options-522438b9`, 39/39 paired @ +0): draw program **73=73 ALIGNED (0 divergent)**,
  pixels **0/786432 differ — BIT-IDENTICAL** across all settled pairs. +1 host test (3300). (Port
  TAS ran 40 frames vs retail's 120 — a post-wait TAS-length quirk on `@fresh`; the static panel is
  fully verified on the 39 aligned frames.)
- **TITLE-SCREEN RENDER ARC → LOAD-confirm flow (A on the picker → in-game load) ✅ VERIFIED 1:1
  BIT-EXACT + USER-CONFIRMED 1:1 2026-06-15** (user "can confirm the fade out matches" — parity
  ledger). The picker A-confirm was already fully WIRED
  (`scene_title.c`: `title_continue_picker_step` → `save_work_load_slot` + `continue_mode=1`/
  `fade_counter=1`; the fade ramp → `fade_phase1_start` → `fade_is_done` → `scene_post_fade_init`
  → house) — so this was a flow VERIFICATION, no code change. Converted `title-load-confirm` to a
  v3 window at `TITLE_PICKER_READY` over the **2nd-A confirm → card-pulse → fade-to-black**.
  **Verified vs the retail v3 cache** (`title-load-confirm-f00eae67`, +0 stretch): settled 54px /
  **confirm+card-pulse gt8 0.0000% maxdiff 1** / mid-fade 60px / near-black 1px — all meanabs 0.00,
  the accepted breathe/seam envelope; draw program 193=193 (same 3 off-screen wing-portrait
  residuals as the picker). The selected card "lights up" (the `fade_counter` `phase` param the
  picker unification already carries) + the fade-out match retail frame-for-frame. The window
  crosses the title→INGAME transition (NEW_GAME/LOADING_END gaps after the fade — the HOUSE arrival
  is verified separately, bit-clean). ⇒ **all title MAIN-MENU render gaps + the load flow are
  closed** (picker + settings bit-exact, load-confirm 1:1; the New Game flow remains a separate
  non-render arc — the "hardest, last" intro/prologue thread).
- **TITLE-SCREEN RENDER ARC → all-banks ENCYCLOPEDIA (図鑑, submenu_state 3) ✅ DONE +
  PIXEL-BIT-EXACT + USER-CONFIRMED 1:1 2026-06-16** (user "can confirm the encyclopedia is 1:1";
  parity ledger) (`9d2c435` board refactor + `2d2597c` title
  integration + `768759f` working-arena fix). The title menu's code-7 row (the port author's
  "RANKING" is a MISNOMER — the dispatch `FUN_0049a59e` L101130 runs `FUN_0049f012(1)` = the
  **all-banks encyclopedia setup**, and the render is `FUN_0049f8b8`; both unambiguously the 図鑑).
  Mostly integration over the just-verified pause encyclopedia: A on code-7 → `encyclopedia_setup(1)`
  (all-banks vs the pause's `(0)`) + submenu_state 3 + slide-in; per-frame `encyclopedia_update` at
  cursor_anim==10 (returns 1 on B — plays its own 0x13d, the title layers the menu-back 0x143, folds
  out, hides the cursor); render `encyclopedia_render(640-cursor_anim*64, 0, board)` gated on
  cursor_anim>0 && state==3 (identical slide formula to the pause). New **TITLE_ENCYCLOPEDIA_READY**
  anchor (port + frida; scene 0 / submenu_state 3 / cursor_anim 10 — no async load ⇒ +0-stretch v3
  join), +1 host test. **TWO real gaps caught by the trace loop (the @fresh case would have hidden
  both):** (1) **the pause.tga `board` texture** (`g_scene_pause_pause`) is FREED at the title, so
  the completion-rate board + slot frames vanished (the port skipped the 4 unloaded-texture draws) —
  fixed by parameterizing the board per-scene (the title passes `SCENE_TITLE_TEX_PAUSE`; same split as
  the picker plaque); refactored encyclopedia_render to take `board`. (2) **the WORKING arena (g_work,
  read by the discovery scan via `enc_disc_rec`) is empty at the title** (the port loads save.dat into
  g_arena — the picker reads it — but only copies to g_work on game-load), so a save with discoveries
  rendered an EMPTY grid (33 draws vs retail's 464) — fixed by `save_work_sync_from_save()` (the engine
  keeps its single DAT_044e3798 populated at the title) before the all-banks setup. **Verified vs the
  retail v3 cache on TWO scenarios:** `title-encyclopedia` (@fresh, empty grid: chrome/bubble/completion
  0%/arrows) AND `title-encyclopedia-max` (the MAXED save, every item discovered: the populated Swords
  grid — icons+names, 100% completion, item description) — **both PIXEL-BIT-EXACT (0/786432 px differ
  at every probed offset, draw-program material verdict 0-divergent), join 119/119 @ +0 stretch**. The
  populated frame's orv3_draws per-DRAW `--list` shows 287 sub-LSB-geometry-hash pairs (icons whose
  carousel-math floats differ sub-pixel but rasterize identically) — accepted (pixels exactly 0 differ,
  material aggregate ALIGNED), the same class as the picker's 54 sub-LSB px. (The code-8 submenu
  — state 4 — is the **Records screen**, now DONE, next bullet; the port author's "HIDDEN-character"
  name was a misnomer.)
- **TITLE-SCREEN RENDER ARC → RECORDS / high-score screen (code 8 / submenu_state 4) ✅ DONE +
  PIXEL-BIT-EXACT + USER-CONFIRMED 1:1 2026-06-16** (user "survival score screen looks good to me";
  parity ledger) (`scene_title.c` `scene_title_records_render` =
  FUN_0049c439 + the code-8 dispatch/state-4 close in `scene_title_sim` + the state-4 render arm in
  `scene_title_render`). The title menu's **"Survival Score"** row (the port author's `HIDDEN_CHAR`
  name is a MISNOMER, like `RANKING` was for the encyclopedia — the in-game tile literally reads
  "SURVIVAL SCORE", dispatch `FUN_0049a59e` code-8 → `FUN_0049c439`). A display-only personal-best
  panel that slides in like the settings/encyclopedia submenus and closes on A/B: a dungeonbord board
  (the SAME sheet settings uses) + 4 centered label/value rows under the ADDSIGNED→MODULATE2X dance
  (grey-0x7f, scale 0.8) — **Record End-game Score `%d pt`** · **Record End-game Money `%d pix`** ·
  **Survival Hell Record `Day %d`** · **Normal Survival Record `Day %d`** (zero ⇒ `--`), + the
  item_win/fuki code-8 header chrome. **Key RE find: the four record values are persistent
  SAVE-HEADER fields, not runtime globals** — `FUN_004905a8` writes the whole arena from
  `&DAT_056e5770`, so `DAT_056e60f0/f4/f8/fc` sit at arena offsets 0x980/0x984/0x988/0x98c (inside
  the 0xb10 header), round-trip through save.dat, and are already in the port's `g_arena` at the
  title — the render reads them straight from `save_arena_base()` (no separate loader; **no
  PORT-DEBT on the populated path**). New **TITLE_RECORDS_READY** anchor (port + frida; scene 0 /
  submenu_state 4 / cursor_anim 10 — no async load ⇒ +0-stretch v3 join). **Verified vs the retail
  v3 cache on `title-records`** (crafted save: `hidden_char` set to unlock code 8 + four DISTINCT
  record values in the header — `123456 pt / 654321 pix / Day 88 / Day 33` — so the POPULATED `%d`
  path renders, not just `--`): **PIXEL-BIT-EXACT — 0/786432 px differ** across the whole window
  (join 119/119 @ +0 stretch, **0 draw-divergent**, each side self-verifies bit-exact 120/120
  retail · 119/119 port). The pushed port render shows all 4 crafted values exact under the
  "SURVIVAL SCORE" header. +2 host tests (`records_opens_on_code8` / `records_closes_on_ab`, 3303).
  RE: `findings/title-records-RE.md`. **PORT-DEBT:** the end-of-game record PRODUCERS
  (`FUN_0049d8a4`/`FUN_0049db8a` — write the high-watermarks at game-over) stay unported
  (game-completion arc; the title render is fully 1:1 given the header values, all the title ever
  reads).
- **TITLE-SCREEN RENDER ARC → SURVIVAL difficulty selector (code 6) ✅ DONE + PIXEL-BIT-EXACT +
  USER-CONFIRMED 1:1 2026-06-16** (user "that looks good to me"; parity ledger;
  `2f1eb20` sim+render+anchor+tests+scenario · `0c489a4` save_io scan fix · `02bc952` toggle+docs).
  The title "Survival" row (unlocked when `FUN_0049a324` returns uVar1==3: a
  save bank with GAME_MODE==3 + an "adventure-8" item) opens a 2-option difficulty selector
  ("Survival Hell" / "Normal Survival") that slides IN over the **still-visible main menu** — NOT a
  submenu_state, it's its own `DAT_096435{50,54,58,5c}` overlay. **Sim** (`scene_title_survival_
  selector_tick` + the code-6 dispatch): ramp `survival_state` 1→8, pressed-edge Hell/Normal toggle
  + shared hand cursor, B-cancel slide-out → main menu, A-confirm closing ramp (`survival_anim`
  1→0xf) → save picker (reuses `title_continue_picker_open`, which already models code 6). **Render**
  (`FUN_0049c644` @ 0x49cbe8, transcribed from objdump — the decompile drops the FPU):
  `savewindow.tga` backdrop (the choice-box banner; ADDSIGNED grey-127, t=state/8 → dst(320−t·256,
  288−t·64, t·512, t·128), alpha=(int)(t·255)) + 2 centered labels (x=320, y=264/296, scale 1.0) —
  selected row pulses `127+sin(survival_anim·π/15)·64`, other flat grey `0x60`. **The porting loop
  caught a latent save_io bug:** `save_io_scan_for_title_menu` read the menu-scan item count from
  `bank[0]` instead of `bank[0xaec6]` (ITEM_COUNT) — a no-op loop that NEVER unlocked Survival once
  GAME_MODE==3; retail unlocked it, the port didn't, and that divergence pinned the fix (latent
  because no prior save had GAME_MODE==3, so the `bank[0xb759]!=3` test short-circuited first). New
  **TITLE_SURVIVAL_READY** anchor (scene 0 / submenu_state 0 / cursor_anim 0 / survival_state 8 —
  no async load ⇒ +0-stretch join) + `title-survival` scenario on a crafted save
  (`tools/craft_survival_save.py` pokes the unlock onto a bank + restamps the bank checksum, else
  `save_bank_init_all` resets it on load); the scenario also toggles Hell→Normal mid-window
  (DOWN @ anchor+40) to verify BOTH highlighted rows + the cursor ease. **Verified vs the retail v3
  cache** (`title-survival-a6a6526f`, join 119/119 @ +0 stretch): the selector is **PIXEL-BIT-EXACT
  — 0/786432 px differ at EVERY offset across the whole window (Hell-selected, Normal-selected, AND
  the cursor-ease transition), draw program 0 draw-divergent**, each side self-verifies bit-exact
  (120/120 retail, 119/119 port). The 1-frame anchor offset (port reaches rest at present 42, retail
  41) is the same benign title boot-phase seam Records has (retail 120 / port 119 there too); the
  +0-stretch deterministic title keeps even the input-driven toggle/ease in phase. +5 host tests
  (3308). RE: `findings/title-survival-RE.md`.
  **PORT-DEBT(survival-picker):** the `FUN_0049b4f4` survival bank-FILTER + the survival game LAUNCH
  (picker-confirm → load survival mode) stay deferred (survival gameplay arc). **⇒ the title's
  renderable submenus (picker · settings · encyclopedia · records · survival selector) are all
  bit-exact; the last title item is New Game (the "hardest, last" intro/prologue thread).**
- **ACTIVE ARC → OPENING-PROLOGUE v3 draw-program / flow parity (2026-06-17).** The Recette/Tear
  opening cutscene was fully ported BEFORE Trace Studio v3, so it had never been v3-verified for
  render-PROGRAM / flow parity (the user: "the prologue was fully ported way before v3 so there's
  probably drawcall/flow trace parity gaps"). New v3 scenario **`intro-prologue-v3`** (new game →
  1st `HOUSE_FREEROAM` = iv1_1 cutscene start, caprange `[0,900]`, NO advance input ⇒ line 0
  reveals + holds deterministically; canonical `{phasepin 0}`+`{rngseed [0,19937]}`).
  **SCREEN-BLACKOUT layer ✅ PORTED + VERIFIED 2026-06-17** (`2fa50b3`): the dialogue region was
  PIXEL-bit-exact but the draw PROGRAM diverged on **752/1021 columns** — retail draws an extra
  **draw [0]** every cutscene frame the port omitted: the screen-blackout `FUN_00453d9c` (gate
  `DAT_0438bf74`, tex `bmp/system.bmp`=`9fd8`, full-screen `0xff000000`/SRCALPHA/MODULATE, drawn
  AFTER the scene block + BEFORE the dialogue at `LAB_00454a90`; for iv1_1's covering bg the scene
  block is skipped ⇒ it's retail's draw [0]).  Under the opaque cutscene bg ⇒ **0 net px** (v2
  pixel-diff never saw it — the v3 draw-program panel did).  Ported as `scene1_fx_screen_blackout`
  (`scene1_fx_overlays.c`) gated on `scene1_intro_dialogue_blackout_active` (D_SCRIPT1|D_LOAD|
  D_SCRIPT2 — armed once at the iv1_1 dispatch, active across the whole cutscene), called before
  `scene1_dialogue_draw`.  Re-drive: **draw-divergent 752 → 81**, held-line pair **43=43 ALIGNED**
  (was DIVERGENT) AND still **pixel BIT-IDENTICAL** (inert).  **The remaining 81 are NOT a gap —
  the load-origin PHASE pillar:** the fade-in `748c` bg quad is **pixel-bit-identical at port#K vs
  retail#K+2** (a 2-frame lead — the port collapses retail's intro-video load ⇒ `HOUSE_FREEROAM`
  lands 2 frames offset from the fade origin; ALIGNED draws at +2), and the dialogue is +0
  bit-exact (the held line absorbs the lead).  **⇒ the prologue first window (iv1_1 line 0) is
  1:1.**  RE: `findings/opening-prologue.md` "v3 DRAW-PROGRAM parity".
  **PORT-DEBT(blackout-tut-dispatch):** the guild/tutorial cutscenes (`start_single`/D_TUT* path,
  the same `9fd8` layer per `merchant-guild-RE.md`) dispatch the blackout separately — wire when
  those scenes are v3-checked.
  **iv1_2 REGRESSION (my session break) ✅ FIXED 2026-06-17** (`2cd1e71`): the first blackout gate
  was `D_SCRIPT1|D_LOAD|D_SCRIPT2`, reasoning it stayed armed the whole cutscene — WRONG.  iv1_2 is
  the OVERLAY path (covers=false), so the opaque blackout drew AFTER the HOUSE scene and BLACKED IT
  OUT (the Tear/Recette portraits over black vs retail's HOUSE scene, `intro-iv2-gap` golden
  cap_31).  The blackout is the iv1_1 transition quad ONLY (cleared at the iv1_1 dialogue-end,
  not re-armed) ⇒ gate = `D_SCRIPT1`.  New `intro-iv2-v3` window caught it; iv1_2 port now renders
  the HOUSE scene behind the portraits; iv1_1 unchanged.  **(User framing confirmed: the prologue
  has NO visual gaps — a visual "gap" is a trace artifact, a fade phase, or a session regression;
  this was the last.)**
  **v3-JOIN window-relative occurrence ✅ FIXED 2026-06-17** (`e16bd82`): the identity join keyed
  by GLOBAL anchor occurrence, which mispairs a cutscene window — retail renders the conv-pose/FX/
  blink during the intro-video+load tail the PORT collapses, so the streams carry asymmetric
  PRE-base firings (retail's `CONV_POSE_BLINK` fires once at offset −30 before the 21/85/… cadence
  both share ⇒ every in-window blink is global occ N+1 on retail).  Re-based the occ to the window
  base anchor (subtract pre-base firings; symmetric windows = no-op, all v3 tests pass +new
  `test_window_relative_occ`).  intro-prologue-v3 re-join: paired 693→**810**, port-only 121→**4**,
  draw-divergent 81→**23** (the residual = the +2 fade phase pillar).  **⇒ iv1_1 now joins honest.**
  **iv1_2 v3 JOIN ✅ FIXED + USER-CONFIRMED 1:1 2026-06-17** (parity ledger; user "can confirm this
  is bit exact and synchronized other than the known book animation remainder" — the book-arrow
  next-line anim is the lone DEFERRED residual, `rt->blink` not yet in `{phasepin}`). Two fixes + a re-drive.
  The iv1_2 join was 0/299 — and my FIRST fix attempt mis-diagnosed it. Both pieces, in order:
  (1) **`ddeb421` base-anchor auto-detect** (VALID, KEEP): `preserve_live` resolved a window's BASE
  anchor by occurrence #1; `resolve_base_anchor` now auto-detects it as the most-recent firing ≤
  present_first (so a side with multiple base-anchor firings re-bases via `_window_occ`). NO-OP across
  the whole v3 cache (fixes a latent `guild-ui-flow` window) + `test_base_anchor_auto_detect`.
  (2) **`869375f` occurrence-aware retail arm** (the REAL cause): the base-anchor fix alone gave a
  bogus "152/299 honest" because the two cached sides were DIFFERENT cutscenes — **port#0 = the SHOP
  (iv1_2)** but **retail#299 = the BEDROOM (iv1_1)**. The `intro-iv2-v3` trace waits for the **2nd**
  HOUSE_FREEROAM (iv1_2); the port replays the segtrace and captures HF#2 ✓, but the retail v3 arm was
  occurrence-BLIND (`house_capture` armed "the first time the anchor fires" = HF#1 = iv1_1 ✗). Fixed:
  the agent's `v3ArmOnAnchor` now counts firings + arms at the occ-th; `house_capture --arm-occ`
  (default AUTO = `wait_occ` = count of `{wait:<anchor>}` in the trace ⇒ 2 for iv1_2, 1 for every
  unique-anchor scenario = no-op). **Re-drove retail** (`--force-retail --max-frames 40000`): retail
  now arms at HF#2 present 5329 = **the SHOP** (Recette+Tear, "Sorry Tear, I kept you waiting"),
  matching the port. **Join = 279/299** (genuine iv1_2-vs-iv1_2; the 20/21 gaps are the load-origin
  opening ramp), **paired frames PIXEL-1:1** (settled 0.2-0.5%, mean|abs| 0; opening ~1.1% converging =
  the load-origin phase, port ~1f ahead — same pillar as iv1_1's +2). port self-verify 299/299
  bit-exact (retail 299/300, 1 non-deterministic frame). **⇒ the iv1_2 opening IS 1:1** — the earlier
  "240f fade / gap #4" was a PHANTOM of the mis-armed iv1_1 capture (`fadeinb` is a genuine compiler
  no-op; that 240f bedroom fade was iv1_1's load-transition). Viewer pointed at `intro-iv2-v3 win-0-300`.
  RE/correction: `opening-prologue.md`. **Lesson (`feedback_verify_1to1_before_done`): eyeball both
  cached sides render the SAME scene before trusting any join verdict — a join pairs by anchor
  identity and will happily pair two unrelated cutscenes that share an anchor name.**
  **INTRO V3 PARITY — VERIFIED COMPLETE + stale gaps CLOSED 2026-06-17 PM** (user: "we can /clear
  and move onto the new trace"). Closing pass over the prologue, reframed by the user as "not whether
  visuals are missing — whether things are out of phase / not rendered faithfully / a logic
  approximation that should be closed": (1) **render program verified FAITHFUL on BOTH cutscenes**
  via the new `orv3_draws --material` (the batching-robust per-texture verdict, `164eae5`) — iv1_1 =
  **ALIGNED** (0 divergent: pure 2D bedroom, no HOUSE-3D batching); iv1_2 = the 88-vs-115 draw swamp
  collapses to **4 benign batched + 1 inert `b494` (0 px) — NO retail-only effect texture**, identical
  at every probed offset ⇒ the anger-marks/radial-lines "gap" is **NOT a render gap** (ledger row
  resolved). (2) **Phase verified IN-PHASE in the visible window** (from the cached anchor streams rel
  HF#2): dialogue `TEXT_ANIM_START/END` = +121/+156 on BOTH; `CONV_POSE_BLINK` cadence 21/85/149 on
  BOTH. The only phase difference is INVISIBLE — retail poses the chibis ~41f before HF#2 (under the
  load overlay), the port at load-end (`CONV_POSE_START` +1) = the documented conv-pose producer debt
  (derived talk-flag + load-end chibi spawn vs the real `FUN_00470a46` + mid-load spawn); 0 pixel
  impact, left as PORT-DEBT. (3) **Stale ledger rows CLOSED:** text-fade-on-dismiss = **already ported**
  (user); the iv1_1→iv1_2 transition is a **plain fade-to-black-and-back, NOT a shatter/melt grid**
  (user ground-truth — the RE'd `FUN_0045281c` shatter machinery is NOT this seam; engine-quirks +
  opening-prologue.md corrected). **Remaining intro gap (DEFERRED, user-OK):** the next-line "book"
  arrow anim slight phase mismatch (`rt->blink` not in `{phasepin}`) — the only thing the user still
  flags. ⇒ **the opening prologue is verified 1:1 (render faithful + in-phase) modulo the deferred
  book-arrow phase.**
- **NEXT ARC (queued for next session) → SHOP CUSTOMER SELLING LOOP** — the core Recettear gameplay
  (customers enter → browse displayed items → haggle/sell), currently **0% ported** (no customer NPC /
  haggle / transaction; the `DAT_0438cc08==4` shop-open path + `FUN_0047019f`/`FUN_0041ee24` are
  stubbed). The user RECORDED the tutorial trace 2026-06-17: **`runs/recordings/rec-20260617-051426.raw.jsonl`**
  — walk to the counter → a "GO!"-style tooltip → press Z → the customer-service tutorial (alternating
  dialogue ↔ price haggling) → stops after the tutorial ends + the first real customer enters. Next
  session: convert the raw recording to a `{caprange}` v3 scenario, RE the customer-spawn/browse/haggle
  subsystem, port the first chip.
- **SHOP-DOOR "GO!" TOOLTIP ✅ DONE + USER-CONFIRMED 1:1 2026-06-17** (`2fb6085`; parity ledger;
  user "looks good to me"). The user-flagged "tooltip at the door" = the free-roam interaction-
  affordance **emote bubble** (the unported inline block of `FUN_0040a765`, decomp L6900-6932): a
  single `hpmp_base.tga` cell (db004=7 → src (464,48), the baked **GO!** sprite) projected at the
  player head, scaled in by the **db000 0→10 gauge** (sin overshoot → settle 32×32), COLOROP=MODULATE.
  Driver = the bVar17 door-zone ramp in `house_update` (L87591-87596), ported into the stub
  `player_ctrl_cc08_proximity_detect` (+ the pure `player_ctrl_emote_ramp_step`); draw =
  `scene1_hud_emote_bubble` (after `scene1_merchant_hud_render`). New **`house-door`** scenario
  (walk to the door + HOLD; caprange LOADING_END+140..260, pinned 282) → re-drove the port vs the
  retail v3 cache (join 120/120 ALIGNED): the bubble region is **pixel-1:1 (meanabs 0.084/px)** + the
  ramp-in matches. +4 host tests (3312). Closes the `town-map-RE` door-tooltip follow-up. **Small
  follow-ups (PORT-DEBT, deferred):** the **bVar3 NPC-approach prompts** (db004 0/1 — talk / talk-
  with-pending-customer; faithful no-op, no live customers in the port yet) + `PORT-DEBT(door-
  proximity, FUN_005031e4)` (the sqrt<1.8 radius — the X>2.895 subset reproduces the deliberate
  approach). The same bubble system serves the DUNGEON combat prompt (db004=4, `scene1_combat_sm`).
  **M3+ (later):** the b1b0==1 system.bmp fade + action-1/2 pause variants (PORT-DEBT in the M3
  code); the other submenus (Items/Options) + type-4 exit-confirm + unpause
  cursor-restore. NB DAT_0438b150 is the SHARED hand-cursor flag (FUN_00435693 sets it too).
  **TWO user-observed gaps to fix as we finish the pause menu (2026-06-14):** (1) **exiting
  the pause menu reseated Recette at the scene SPAWN position** (not where she was paused) =
  `PORT-DEBT(pause-unpause-restore)` **✅ FIXED 2026-06-15** (`scene_pause.c` `pause_dispatch`).
  **RE correction:** the cause was NOT a missing player-pose snapshot (`DAT_06a499ac/b0/b4`
  are the shared HAND-CURSOR snapshot) — the port **re-spawned the load worker on unpause**,
  re-running the INGAME case-1 load (`scene1_preload_house`→`scene1_postload_pose_player`)
  which re-posed Recette at spawn. The engine NEVER reloads on unpause: it freezes the scene
  through the open ramp (per-mode dispatch skipped while `g_sim_counter_998 != 0`) and resumes
  it in place on the close ramp. Fix = drop the worker re-spawn + run the engine teardown
  (FUN_00453384 L50254-50283: stage-pulse clear / cursor hide / equip re-aggregate /
  `d3d_pool_release_type(0xc)` asset free / hand-cursor restore gated on the pause-open
  visibility snapshot). **Verified 1:1 on the new `house-pause-unpause` scenario** (walk RIGHT
  off spawn → ESC → ESC): player px/py **bit-identical port==retail at the walked-to (+3.10,
  +6.04) across all 112 aligned pairs**, never jumping to spawn; resumed-scene pixels match
  (gt8 0.52% @ mean|abs|=0.00/ch = the benign HOUSE batching). +3 host tests. **✅ USER-CONFIRMED
  1:1 2026-06-15** ("can confirm the pause menu behaves correctly now" — parity ledger; `818af18`).
  PORT-DEBT remains: the shop-grid unpause rebuild (`FUN_00468338`, inert outside a shop pause) +
  guild-pause re-init + actions 1/2. **⇒ both user-flagged pause-finish gaps now CLOSED.** (2) **the in-game PLAYTIME never advanced** (sit a minute,
  re-save → same `TIME`) = **`PORT-DEBT(playtime-ticker)` ✅ FIXED 2026-06-14** — ported retail's
  `working[active].dword[2]++` (the frames@60 the card reads) into `sim_step_a`'s head (the
  `FUN_004536cb` port), gated `g_scene_state != 0`. The port's card TIME now advances (0:03:50→
  0:04:03 over the commit trace). **Load-dependent-counter caveat:** in a PINNED trace the TIME
  now lags retail by the load-seam (port 0:04:03 vs retail 0:05:14, retail's slower pause-load
  ticks ~4260 more frames) — the SAME phase pillar as db054, NOT a logic gap (real gameplay
  ticks 1/frame). Follow-up: fold the playtime into `{phasepin}` for a frame-exact pinned
  comparison. Gap (1) detailed in `plans/pause-menu.md` PORT-DEBT registry.
- **NEXT ARCS:** finish item-display gaps → **merchant's guild scene** (now active,
  above) → **pause menu** (now started, above) → town scenes off the world map (world-map
  backlog itself CLOSED 2026-06-08, bit-clean f16→638). Trace-studio v2 **Phase 5** (New-Game
  cross-replay: retail intro-video force-skip D4 + the prologue mid-load actor-spawn gap,
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
  divergence — CHARACTERIZED BENIGN 2026-06-13, no fix needed:** the 98-vs-125 draw gap is
  benign batching (retail splits what the port batches; per-texture triangle totals identical ⇒
  same pixels) + 1 retail-only **INERT** first draw (tex `b494`, not `ea99` — that was the
  imprecise short-hash; 80 tris, `ALPHATEST` pass-only-α0 + SRCALPHA blend ⇒ blends to dst = 0
  colour, AND `ZWRITEENABLE=0` ⇒ 0 depth — a pure no-op the port harmlessly omits; likely an
  invisible/α0 entity). So the HOUSE render program has NO real divergence (vs the guild's, which
  was a real over-draw). The drill workflow: `orv3_window <scen> --window OFF:COUNT --view` →
  `orv3_draws.py <port.bin> <pf> <retail.bin> <rf> --list` (cross-side content-keyed draw diff).
- **Authoritative parity facts:** `findings/confirmed-parity-ledger.md`. A tooling
  "divergence" on a human-confirmed-1:1 item is a lead to investigate, NOT an
  assumed regression.
<!-- FRONT:END -->
