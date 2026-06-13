# Pause menu (in-game ESC menu) — port plan

Engine scene **mode 9**. ESC in-game opens the pause menu: a full-screen
takeover (calendar / gold / Recette portrait / option list **Items ·
Encyclopedia · Options · Save · Exit Game**) reached through a "Now Loading"
async asset load. Scenario: **`house-pause`** (`0250347`); v3 cache
`house-pause-f1bf56e7` has the retail menu rendered.

Status is tracked here (this doc) + the parity ledger; FRONT carries the
one-line current front.

## Lifecycle (the state machine)

The ramp counters ALREADY EXIST in the port (`sim.c`, ported as part of
`FUN_004532df` but dormant — no setter wired). Engine→port symbol map:

| engine            | port                         | meaning                          |
|-------------------|------------------------------|----------------------------------|
| `DAT_0438b1c0`    | `g_scene_state`              | scene mode (9 = PAUSED)           |
| `DAT_06a49998`    | `g_sim_counter_998`          | **pause ramp** 0→0xc (open)/0x14  |
| `DAT_06a4999c`    | `g_sim_counter_99c`          | **slide ramp** (render-side pump) |
| `DAT_06a499a0`    | `g_sim_mode_9a0`             | direction: 1=opening, 0=closing   |
| `DAT_06a49990/94` | `g_sim_counter_990/994`      | fade sub-counters                 |
| `DAT_06a4997c`    | `g_pause_action` (NEW)       | 0=ESC · 1/2 = other entries       |
| `DAT_06a499a8`    | `g_pause_saved_mode` (NEW)   | mode to restore on unpause        |

Flow (ESC path, action 0):

1. **ESC** in-game → `FUN_00453384(0)` (`pause_dispatch`): gates (no fade
   active, mode pausable = not 7/2/3/10, not in a choice box), then sets
   `saved_mode=mode`, ramp998=1, ramp99c=1, dir=1 (opening), SE **0x16b**.
   *(cursor snapshot `DAT_06a499ac/b0/b4` + `FUN_004681d3` resume-state =
   PORT-DEBT for now; only needed for the unpause restore.)*
2. **Ramp 1→3** ticked by BOTH `FUN_004532df` (sim, only while a worker is
   busy) and `FUN_004547ab` (render, every frame) — the port already calls
   `sim_loading_pump()` in both `sim_step_a` (worker-busy path) and
   `render_dispatch`. The render side ALSO increments `c99c` (the slide
   ramp, `FUN_004547ab` L51057-51064) — **not yet pumped in the port**.
3. **Ramp == 3** (`FUN_004536cb` L50465): `mode=9`, `FUN_0047f2f6`
   (`pause_menu_setup` — build the entry list), `FUN_00452cde`
   (`worker_load_spawn` — the primary worker, NOT the dead C4E secondary).
4. **Worker loads** via the PRIMARY worker case-9 (objdump 0x4529c6 —
   authoritative): `action 0 → FUN_00473a3e` (the 20-asset pause load).
   Register `worker_load_set_cb(9, pause_worker_case9)`. While the worker
   runs, `sim_step_a` short-circuits (the existing worker-busy gate); the
   ramp keeps climbing to 0xc.  *(NB the C4E secondary `FUN_00452e75` is
   unreferenced/dead — the pause load is case-9 of the primary; the FPU
   init `0x435873` is C4E-only, so call `scene_pause_state_init` from the
   menu setup to keep its layout constants set — they may be unused by the
   basic menu.)*
5. **Mode-9 update** `FUN_0047fa76` (`pause_menu_update`): if no submenu
   open (`sub_anim<1`) → `FUN_00480614` (`pause_menu_nav`): U/D wrap the
   selection (SE 0x146), A starts the select anim → opens a submenu
   (PORT-DEBT), B = `FUN_0045337b` = `pause_dispatch(0)` again → UNPAUSE.
6. **Render** `FUN_004820ba` (`pause_menu_render`), gated by
   `FUN_004547ab` L51223 on `3 < c99c < 0xd`: bg (`pause_bg_rete`,
   full-screen, alpha by `sub_anim`) + the option list (sprite rows from
   `pause.tga` indexed by entry type) + calendar + gold + Recette portrait
   + cursor. The fade overlay `FUN_00454191` draws the open transition.

## Menu setup (`FUN_0047f2f6`, all.c:81548)

Entry-type list `g_pause_entries[]` (engine `DAT_074b2844[]`), count
`g_pause_count` (`DAT_073e154c`):

- type **0** (adventurer Status) — IFF `0 < DAT_0741bed8` (party/adventurer
  count). **PORT-DEBT**: port doesn't track recruited adventurers →
  `pause_status_count()` returns 0 (correct for the early-tutorial
  house-pause save).
- type **1** (Items)
- type **5** (dungeon-only) — IFF `saved_mode==1 && 0 < *DAT_068dd2f0`
  (stage type; 0=HOUSE). Port has it (`scene1_current_stage_record()->mode`).
- type **6** (Encyclopedia), **2** (Options), **3** (Save), **4** (Exit Game)

House (no party, stage type 0) ⇒ list = **[1, 6, 2, 3, 4]** = the
retail-observed "Items · Encyclopedia · Options · Save · Exit Game".
Row pitch `g_pause_row_spacing = (0xb - count) * 0xc` (`DAT_005cc678`).

## Update / nav state machine

- `FUN_0047fa76` (`pause_menu_update`, all.c:82008): `sub_anim` (0..10)
  drives submenu open/close; at `sub_anim==10` dispatch to the submenu
  updater by entry type (**all PORT-DEBT this arc**). `exit_confirm`
  (`DAT_074b2830`) runs the "Returning to title screen? Are you sure?"
  flow for type 4 (PORT-DEBT — wire when Save/title-return ports).
- `FUN_00480614` (`pause_menu_nav`, all.c:82609): input bits — A=0x10 (start
  select anim), B=0x20 (`FUN_0045337b` close), up=`DAT_073dddd6&4`,
  down=`&8` (wrap `sel = (count ± 1 + sel) % count`). At `sel_anim==0xf`
  the selection commits → submenu (types 0/1/2/3/5/6, PORT-DEBT) or
  type-4 exit confirm (PORT-DEBT).

## Render (`FUN_004820ba` all.c:83679, `FUN_00454191` all.c:50812)

Draw primitives all already in the port: `render_quad_add`
(`FUN_00404efc`), `render_quad_flush` (`FUN_00405354`),
`render_quad_state_setup` (`FUN_0049b425`), the number row
(`scene1_top_hud_draw_number` `FUN_00406a60`), the level sub-helper
(`FUN_00481ec3`), the shared hand cursor (`FUN_00435747`). Sprites from
`scene_pause` (`g_scene_pause_pause` = pause.tga atlas,
`g_scene_pause_bg_rete`). Calendar date math = `FUN_00482059`/`FUN_00482033`
(port TBD). Portrait = `FUN_0048d997` (TBD).

## Milestones

- **M1 — state machine (pure C, host-tested), inert.** `pause_dispatch`,
  `pause_menu_setup`, `pause_menu_update`, `pause_menu_nav` + the case-9
  worker callback + host tests. NOT wired into sim/esc/render → zero
  behavior change (avoids an ESC-→-black regression between commits).
- **M2 — wire + render the backdrop (done, `aef7d89`).** Wired the trigger
  (esc→dispatch), the ramp consumers (sim mode-9 + ramp==3 spawn; render c99c
  pump + mode-9 render), worker case-9, and `pause_menu_render`'s bg_rete
  backdrop. ESC opens the menu; port re-drives 240/240 bit-exact, draws bg_rete.
- **M2b — option list + header + cursor tail (done, `e00f622`).** Ported the
  rest of `FUN_004820ba`'s resting-menu draws: the COLOROP=ADD icon+label rows
  (Items·Encyclopedia·Options·Save·Exit Game), the "PAUSE MENU" header
  (COLOROP=MODULATE), and the shared overlay tail (choice box / cursor / save
  frame — all self-gating). Geometry+diffuse from objdump 0x4820ba-0x482400
  (decompile dropped the register-built diffuse + FP consts; verified vs .rdata).
  **v3 draw-program 1:1:** port draws now match retail's [1] bg_rete, [2] option
  list (colorop=7, ×20 tris = 5 icons+5 labels), [3] header (colorop=4, ×1).
  Layout matches retail's staircase. **Pixel-1:1 confirmation is entangled with
  M2c** — the missing board background swamps the diff (bg_rete art is black/
  matching; everything else is white because port shows the cyan clear).
- **M2c — calendar / merchant-rank XP bar / numbers ✅ DONE + PIXEL-BIT-EXACT 2026-06-13.**
  Ported FUN_004820ba's [4-9] block in `scene_pause.c` (the calendar board + day
  markers, the merchant-rank XP progress bar, and the gold/quota/level number
  glyphs) + the three value helpers `pause_day_index` (FUN_00482033), `pause_period_end`
  (FUN_00482059, with the +0x2c3e8 cache write), `pause_weekly_quota` (FUN_0048d997).
  **Result (re-drove the port over `house-pause` HOUSE_FREEROAM+120:240, joined vs the
  retail v3 cache):** the resting-menu **draw program is ALIGNED — 10/10 draws matched
  by content hash, 0 divergent** ([4] panel ×12prim=6q / [5] today ×2=1q / [6] period-end
  ×6=3q / [7] quota ×12=6q / [8] gold ×8=4q / [9] level ×2=1q — every tex/colorop/
  tri-count/**geometry hash** identical to retail), and **history-replay pixels are
  BIT-EXACT (meanabs=0.000, gt8=0.000%) across every resting pair** (offsets 100/126/
  152/177; the pause menu is fully static at rest ⇒ no phase residue at all). Port
  self-verify still **240/240 BIT-EXACT** (M3 backdrop determinism preserved).
  **Key RE correction:** the plan's "[4] period-progress bar needs the CURRENT-DAY
  `_DAT_0438b91c`" was a mislabel — `_DAT_0438b91c` is the **animated merchant-rank XP**
  (the global the bottom-left HUD eases toward the rank target; `scene1_merchant_hud.c`
  had it right). [5]/[6]'s `+0x2c3f8`/`+0x2c3fc` are the **XP level-start/next thresholds**,
  NOT a calendar period; the real calendar day is CARD_DAY (+0x2c3ec). The stubbed
  `g_dat_0438b91c` (`stage_post_load.c:562`) is DEAD (no consumer) — NOT touched; the
  pause render reads the bank directly (`save_work_dwords_at`), and at rest the XP-current
  bank field (+0x2c3f4) equals the animated `_DAT_0438b91c` (no XP animating in the house),
  so the read is bit-identical to retail. **PORT-DEBT(pause-xp-anim):** the XP-display
  animator (FUN_00406xxx, shared with the merchant HUD's own stubbed `set_xp`) stays
  unported — only matters mid-rank-up, never in this scenario. **✅ USER-CONFIRMED 1:1
  2026-06-13** ("can confirm pause menu is fully 1:1 based on the pushes" — parity ledger;
  the WHOLE resting pause menu is now 1:1). Commit `f3ef342`.

  Original RE map (kept for reference). The retail resting-menu draw program is
  **10 draws** (orv3_draws on `house-pause-f1bf56e7` frame 119):
    - **[0]** tex `3e66`, full-screen 1024×768, MODULATE — the menu's black
      backdrop. **✅ SOURCE RESOLVED 2026-06-13 — it is the CAPTURED-SCREEN RENDER
      TARGET (`DAT_073de648`), drawn by `FUN_00454191` (the fade/capture system),
      NOT a static board asset.** Four independent lines of evidence:
      (1) **decompile** — at rest (c99c==0xc) the dispatch `FUN_004547ab` runs
      `FUN_0045404b`→`FUN_00454191` right BEFORE `FUN_004820ba` (L51221 then
      L51225). The old correction read `FUN_0045404b`'s OWN draw (gated
      `0 < DAT_06a49994`, no-op at rest) but MISSED that it calls `FUN_00454191`
      unconditionally; `FUN_00454191`'s gate is `1 < c99c` (TRUE at rest), so its
      full-screen draw at L50944-50961 runs every resting frame, binding
      `DAT_073de648`. (2) `DAT_073de648` is a **CreateTexture render target**
      (`FUN_0047ae65` L78061-78063: screen-sized `DAT_005cbc04`×`DAT_005cbc08`,
      surface `DAT_073de630`) — created, never file-loaded. (3) **container dump**
      (`_texdump`) — tex `3e66` is **1024×768, fmt 22 (X8R8G8B8), datalen=0** (a
      screen-sized RT with ZERO stored pixels), vs the real assets bg_rete (#58,
      1024×512, 2 MB) / pause.tga (#59) / item_win (#52, 4 MB) which all carry
      pixel content. (4) **replayer** — `replay.exe --upto 119 1` (clear + [0]
      only) renders PURE BLACK (mean 0.0, every pixel 0,0,0) because the per-frame
      replayer can't carry the RT's captured content across frames. ⇒ **[0] is an
      M3 dependency (the capture/composite mechanism), not a one-line static draw.**
      `dungeonbord`/`result_bord01` were the WRONG candidates (they're loaded
      assets WITH content). **OPEN sub-question for the user** (who has the game):
      what does the live-retail pause backdrop look like — pure black, the dark
      composite (`FUN_00454191`'s c99c==3 path clears `0xff173c8c` + draws a
      `0x14dcdcdc` translucent vignette into the RT), or the frozen scene? The v3
      replayer can't show it (datalen=0); only real retail or a Frida RT-dump can.
    - **[1]** bg_rete · **[2]** option list · **[3]** header (M2/M2b, done).
    - **[4],[5],[6]** tex `e5bd` (pause.tga), MODULATE — the calendar frame +
      labels (engine L83801-83866; `local_8 = sub_anim*-0x40` slide-in x-offset
      at rest 0). Geometry in objdump 0x4824aa+.
    - **[7],[8],[9]** tex `3392` (item_win.tga), MODULATE — the **number glyphs**:
      gold (`FUN_00406a60(local_8+256, 28, *(puVar3+0xc), 1, white, 1)` →
      `scene1_top_hud_draw_number`, ALREADY ported), the calendar day cells, the
      Merchant Level badge (`FUN_00481ec3(local_8+192, 64, *(puVar3+0x2c400))` →
      `scene1_merchant_hud`, ALREADY ported). Date math = FUN_00482059/00482033.
      Retail shows gold **10,000,000** (the tutorial infinite-money pin) here.
  Save-arena fields are addressable (`save_work_bank_at(slot)` byte base +
  `DAT_0438b1e0` slot, the same `bank*0x2dfc8 + offset` geometry chara_equip
  uses): gold `+0xc`, calendar period `+0x2c3f8`/`+0x2c3fc`, level `+0x2c400`,
  mode `+0x2dd64`, day `+0x2c3ec`, period-end cache `+0x2c3e8` (written by
  `FUN_00482059`), discount flag `+0x2bc56` (byte; halves the quota). The
  not-yet-ported helpers are small: `FUN_00482033` (day index, 38 B),
  `FUN_00482059` (period-end, 97 B), `FUN_0048d997` (the WEEKLY QUOTA number —
  500000/80000/… by period, halved if `+0x2bc56`; 218 B). Gold (`+0xc`) + level
  (`+0x2c400`) helpers are ALREADY ported and their fields load 1:1.
  **⚠ [4-9] value dependency (scoped 2026-06-13):** the calendar period-progress
  bar in [4] (L83833-83847, width ∝ `_DAT_0438b91c − period_start`) needs the
  CURRENT-DAY global `_DAT_0438b91c`, which the port **stubs to 0**
  (`stage_post_load.c:562` "Stage record isn't ported → defaults to 0", loaded
  from stage-record `+0x2c3f4`). Until un-stubbed (or read directly from save
  `+0x2c3f4`), [4]'s progress-bar quad geometry will DIVERGE from retail ⇒ the
  [4] geometry-hash won't match on `orv3_draws`. The quota (`FUN_0048d997`) +
  day-cells ([5]/[6]) similarly depend on the day field `+0x2c3ec` being loaded.
  **So [4-9] is NOT a pure-geometry port — verify each draw's field values load
  1:1 before claiming a draw-program match.**
  **Milestone reality (corrected 2026-06-13):** the old "once [0]+[4-9] land the
  whole resting menu is cleanly pixel-1:1-verifiable" is now known to be blocked
  on TWO fronts the original plan missed: **[0] is the M3 captured-screen RT** (so
  a clean pixel diff of the backdrop needs M3 AND the v3 replayer can't reproduce
  the RT regardless), and **[4-9] needs the stubbed current-day un-stubbed**.
  [4-9] is still verifiable STRUCTURALLY (draw-program tex/colorop/tri-count +
  geometry-hash, once values load 1:1), just not via a swamped pixel diff.
- **M3 — the captured-screen BACKDROP [0] + the radial-blur composite ✅ DONE
  2026-06-13** (`src/screen_rt.{c,h}` + `scene1_fx_overlays.c` FUN_00454191 body +
  the render_dispatch capture redirect + `render_quad_add_unscaled` = FUN_00404e98).
  **The port now re-renders the live scene into RT#56 at pause-open (c99c==2),
  builds the 2-pass radial-blur composite once (c99c==3), and samples the blurred
  RT#56 full-screen as [0] every rest frame with the open/close fade ramp.**
  Verified vs the retail v3 cache (`house-pause-f1bf56e7`, re-drove the port over
  HOUSE_FREEROAM+160:80): the **RT command structure is bit-exact** — frame 40
  (capture): SetRenderTarget(RT#56 1024x768 fmt22)+Clear 0xff000000+scene draws+
  restore (2 SetRenderTarget); frame 41 (blur): the live scene to backbuffer, then
  Pass A (SetRenderTarget RT#57 1280x256 fmt21, Clear 0xff0000c8, 1 quad **2 prim**
  sampling RT#56), Pass B (SetRenderTarget RT#56, Clear 0xff173c8c, 1 draw **24 prim**
  = 12 zoom taps sampling RT#57), then [0] (1 quad 2 prim RT#56→backbuffer) — every
  clear colour + prim count matches retail's frame-40/41 exactly. **Pixel proof
  (cumulative `replay.exe --history`):** the blur composite frame f41 is
  **gt8=0.46% / meanabs=0.11 vs retail** (near-identical), and the resting menu f119
  RIGHT half (the option list / header) matches (Δ≈+2); the f119 LEFT-half brightness
  (Δ+42 mid-left) is **the missing calendar board [4-6] + numbers [7-9]** (the orv3_draws
  diff shows port 4 draws / retail 10, the 6 retail-only being tex e5bd calendar + tex
  3392 glyphs — the M2c PORT-DEBT below, NOT an M3 gap). The capture renders 97 draws/
  2597 prim into RT#56 vs retail's 124/2677; the 80-prim delta is exactly the known
  benign HOUSE-batching + the retail-only inert b494 draw (0 px). The empirical blur-tap
  geometry was read off the retail stream (orv3_rt/orv3_draws) and matches the decompile
  formula 1:1.
  **The CLEAR GATE was the load-bearing fix (engine FUN_004547ab L51070):** the backbuffer
  is NOT cleared while c99c is in **[3,0xc]** — the [0] backdrop (faded in by the c99c·0x16
  alpha ramp) + menu composite over the PRIOR frame so the blurred backdrop builds up over
  the captured live scene. The first cut cleared every frame, which (a) wrong-fades and (b)
  made the partial-alpha ramp frames **non-deterministic vs the carried-forward backbuffer**
  — the port's own resident `replay.exe --verify-hashes` flagged it: **232/240 (frames
  162-169 = c99c 4..0xb FAILED)** while retail is 240/240. Skipping the clear for c99c∈[3,0xc]
  (render_dispatch) → **port self-verify 240/240 BIT-EXACT** (matching retail). This is the
  authoritative objective proof the backdrop render is deterministic + structurally sound.
  **Remaining backdrop work = M2c calendar/numbers (below; blocked on the
  current-day un-stub) + the b1b0==1 system.bmp fade variant + the action-1/2 pause
  variants (both PORT-DEBT).**
  **Capture mechanism DECODED (vtable-mapped from the proxy vtable, factual):**
  device vtable offsets — `+0x50` CreateTexture, `+0x64` CreateRenderTarget, `+0x70`
  CopyRects, `+0x7c` SetRenderTarget, `+0x80` GetRenderTarget, `+0x84`
  GetDepthStencilSurface, `+0x90` Clear, `+0x94` SetTransform, `+0xf4` SetTexture.
  - **RT init `FUN_0047ae65`:** `DAT_073de648` = `CreateTexture(screen W×H, 1 lvl,
    usage=1 D3DUSAGE_RENDERTARGET, fmt, pool=0 DEFAULT)` → surface `DAT_073de630`;
    `DAT_073de64c` = `CreateTexture(1280×256, RT)` → surface `DAT_073de634`. Both
    are render targets (this is why tex `3e66` is datalen=0).
  - **`FUN_00454191` (called every frame via `FUN_0045404b`):** gate `1<c99c`. At
    `c99c==3` (open) it builds the composite: GetDepthStencilSurface+GetRenderTarget
    (save the backbuffer), `SetRenderTarget(DAT_073de634)` (render INTO the 1280×256
    RT), `Clear(…, 0xff0000c8)` (dark blue), `SetTransform(PROJECTION)`, then
    composite draws (binds `DAT_073de648`, draws full-screen; a `0x14dcdcdc`
    translucent vignette LOOP L50914-50932 = the radial-blur/zoom build-up), then
    restores the saved RT/depth. At rest (`c99c>3`) it redraws the composited RT
    full-screen (this is **[0]**, alpha `min(c99c·0x16,0xff)`).
  - **✅ EXACT PASSES READ OFF THE REAL STREAM 2026-06-13 (P5 RT-capture tooling
    landed — the decompile's *shape* is now CONFIRMED + made precise; no guessing).**
    `orv3_rt.py` on the re-driven retail `house-pause` (v3 cache, RT-format
    container) gives the mechanism over the ESC frames (offsets 160-161 = window
    frames 40-41), VERIFIED bit-exact (`replay.exe --verify-hashes` = **240/240**,
    incl. these RT frames):
    - **Frame 40 — the CAPTURE is a SCENE RE-RENDER, not a CopyRects.**
      `SetRenderTarget(RT#56 = DAT_073de648, 1024×768, depth=DEPTH)` →
      `Clear(0xff000000 black, COLOR|Z)` → **124 draws re-render the entire live
      scene INTO RT#56** (the 3D house + sprites colorop=5, then the HUD/UI
      colorop=7 ADD / 8 ADDSIGNED) → `SetRenderTarget(BACKBUFFER, DEPTH)`. So
      `DAT_073de648` = a fresh re-render of the scene (NO `CopyRects` from the
      backbuffer anywhere in the pause — the proxy captures CopyRects too; it's
      simply unused here).
    - **Frame 41 — the 2-pass BLUR composite (the radial blur), built ONCE:**
      after the normal live-frame draws to the backbuffer,
      (A) `SetRenderTarget(RT#57 = DAT_073de64c, 1280×256, depth=NULL)` →
          `Clear(0xff0000c8 dark blue)` → **1 quad sampling RT#56** (downsample
          1024×768 → 1280×256);
      (B) `SetRenderTarget(RT#56, depth=NULL)` → `Clear(0xff173c8c)` → **24 prim
          (12 quads) sampling RT#57** (the multi-tap blur accumulation, upsampling
          back into RT#56 — this is the "vignette LOOP" the decompile showed);
      then `SetRenderTarget(BACKBUFFER, DEPTH)` → **draw#126: 1 full-screen quad
      sampling RT#56 → backbuffer** (the visible backdrop [0]). All composite draws
      are colorop=4 (MODULATE), BLEND.
    - **Rest frames (e.g. frame 119, the resting menu): NO SetRenderTarget** — they
      just **sample RT#56** (`SetTexture(RT#56)` + full-screen quad, colorop=4,
      BLEND, alpha `min(c99c·0x16,0xff)`) as draw#0 = **[0]**, then bg_rete / option
      list / header / calendar / numbers on top (the M2/M2b draws). So the blur is
      composited once at open; rest frames redraw the static RT#56 with a fade-in
      alpha. ⇒ **[0] = a full-screen quad sampling RT#56, MODULATE, BLEND, alpha
      ramp.** Cross-frame: RT#56 is filled at frames 40-41 and sampled forever after
      — `replay.exe --history <idx>` (cumulative replay) reconstructs it; the
      per-frame replay shows the [0] backdrop empty/garbage. Ground truth confirmed
      visually: history-replay of frame 119 = the darkened/blurred house behind
      Recette + the menu (feed "P5 RT-capture WORKS"), matching the user's
      description. **Port [0]:** redraw `g_scene_pause` RT (`scene_pause_state`'s
      captured surface) full-screen MODULATE/BLEND at the c99c alpha — but the
      backdrop CONTENT (the capture + 2-pass blur) is the M3 dependency: the port
      must (1) re-render the frozen scene into a screen-sized RT at pause open,
      (2) run the downsample→blur-accumulate→full-screen passes. The exact pass
      geometry/blur taps are now READABLE per-draw via `orv3_rt.py … 41 --full` +
      `orv3_draws.py` (vertex/UV dump) — port from the stream, not the decompile.
- **M4 — the SAVE submenu (entry type 3) — RENDER ✅ DONE + PIXEL-1:1 + USER-CONFIRMED 2026-06-14**
  ("looks good to me"; `7affa5f` render + `351654e` tooling). The card-list render `FUN_0049b556` +
  perm `FUN_0049b537` are ported as **`save_picker.{c,h}`** (shared with the title
  picker), driven by the pause type-3 commit + wrapper `FUN_004812e4` + the
  `sub_anim>0` render dispatch (`scene_pause.c`). Transcribed 1:1 from objdump
  @0x49b556..0x49c050 (Ghidra dropped the FP .rdata consts, the SetTexture texture
  args, the 4 sprintf format strings, and the TIME seconds vararg — all resolved
  from .rdata). **Verified vs the retail v3 cache** (`house-pause-save`,
  PAUSE_READY+250 at rest, `orv3_shot` per-frame render): the submenu is **PIXEL-1:1
  — meanabs 0.13, gt8 0.00%, max diff 2** (occupied slot-0 card: portrait/clock +
  Merchant Level 1 + SCORE 0 + LOOP 0 + TIME 0:03:50, + the NO-DATA cards + headers
  all match; feed "M4 pause Save submenu port|retail|diff"). +4 host tests (3253).
  **DRAW-PROGRAM divergence (OFF-SCREEN, root-caused):** retail draws **3 pages**
  (center + left/right WINGS, gate `DAT_09643520>=10`) where the port draws **1**
  (center) — exactly 3.00× the box geometry (retail 24 vs port 8 box prims), **zero
  pixel impact** (the wings are off-screen at dst_x ±640). `DAT_09643520` is the
  **title continue picker's** open-ramp (`FUN_0049a59e`), left at 10 after a
  Continue-load; the port's `title_continue_picker.c` uses instance fields and never
  ramps the shared global, so `g_save_picker_hpage_anim` stays 0. **PORT-DEBT(save-
  picker-wings)** — the faithful fix is to ramp `g_save_picker_hpage_anim` in the
  port's title continue picker open-anim; it closes when the title picker render is
  ported (which shares `FUN_0049b556` + drives `DAT_09643520`). Quirk §124.
  **Verification caveat (M3 limit holds):** the per-frame **self-verify is DIVERGENT**
  (0/299 hash) — the per-frame replayer can't reconstruct the pause RT backdrop;
  `orv3_shot`'s frame render DOES composite it, so the pixel-diff above is the valid
  check (NOT `replay.exe --verify-hashes`). Tooling: `v3cache.localappdata_v3` now
  has an env override + `/mnt/*/Users/*/AppData/Local/openrecet/v3` glob fallback
  (cmd.exe WSL interop was wedged, blocking the cache step).
  - **Original RE/trace notes (kept for reference):** RE + trace + capture were DONE;
    the FUN_0049b556 render port was the remaining (large) piece.
  - **Trace `house-pause-save` ✅ (committed):** ESC → **3×down** → Z opens the type-3
    submenu. **Correction: the user said "2x down" but it's 3×down** — house list `[1,6,2,3,4]`,
    default sel=0 (Items), Save = index 3 (Items→Enc→Opt→Save). **New `PAUSE_READY` anchor**
    (load-end edge gated on scene==9) is the robust nav sync: PAUSE_OPEN fires PRE-load and
    only on the port (retail's pause never sets the b150 cursor flag), and the pause asset-load
    stretches wildly per side (port ~1-156f, **retail ~1800f**). Nav rebased on PAUSE_READY+30;
    v3 join **299/299** across a +4289-frame stretch. Also fixed a v3 capture-race
    (`wait_for_capture`, DrvFs FINALIZE visibility). Commits: PAUSE_READY anchor + scenario;
    the race fix.
  - **Scope (bigger than the framing):** the Save submenu IS the **full save-file selection
    screen** — a VERTICAL list of slot cards (RE'd + captured, retail frame on feed): file#
    (`000`,`001`,…), and for an OCCUPIED slot a portrait/clock + gold("pix") + "Merchant Level N"
    + SCORE + LOOP + TIME h:mm:ss; EMPTY slots show "NO DATA". This scenario's save occupies
    slot 0 (000, TIME 0:03:50), 001-003 empty. **retail 189 draws vs port 10** at the rested
    submenu (`orv3_draws` on `house-pause-save-f2254122`). **The render `FUN_0049b556` (the
    save-slot card list) is SHARED between TWO wrappers** (user pointer 2026-06-14: "the save
    menu renders very similar to the main-menu load — check if they share code" — they DO):
    `FUN_004812e4` (the pause Save submenu) and **`FUN_0049c644`** (3233 B — the title-screen
    **Continue/load** picker render, called from `FUN_004547ab`@~L51104 gated on the Continue
    mode `DAT_09643524==1`). The port's title picker has the LOGIC
    (`title_continue_picker.c` `FUN_0049a59e`/`FUN_0049b537`) but its render is the **deferred
    PORT-DEBT(render)** — so porting `FUN_0049b556` as a shared `save_picker` lands BOTH the
    pause Save submenu AND the title load render. The two wrappers differ only in the params to
    `FUN_0049b556`: pause `(0, val[cur], val2[cur], c898, c894, save-phase)`; title
    `(x, cursor, scroll, vscroll, hscroll, confirm-countdown)`.
  - **RE map (the type-3 chain):** nav-commit `FUN_00480614` sel_anim==0xf → `LAB_004806a1`
    shared reset + the **type-3 branch** (clear `c89c`/`c8a0`/`DAT_09643564`; `FUN_0049b537`
    inits the slot-perm `DAT_09643380`[0..99] + count `DAT_005d1bbc`=100; `val[cur]=last_slot`
    `DAT_056e578c`, `val2[cur]=last_slot-2`) → sub_anim ramp. Update `FUN_0047fa76` sub_anim==10
    → **`FUN_0047f5bc`** (slot-# picker nav: U/D ±1, L/R ±3, the rolling-number latches
    `c894`/`c898`, A-commit → overwrite-confirm if occupied / dungeon-save dialog, B-cancel).
    Render `FUN_004820ba` sub_anim>0 → **`FUN_004812e4`** (wrapper: calls `FUN_0049b556` at
    rest; + a 2-quad save-progress bar when `c89c>0`) → **`FUN_0049b556`** (2810 B — the card
    list; center pass renders 5 rows, slot = perm[row+prev-1], y=row·140-92; per-card
    box/number/portrait/metadata; the wing passes are off-screen at rest). Commit
    `FUN_004905a8` (working bank → slot bank + checksum + write save.dat/_save.dat) =
    **M4c PORT-DEBT** (the trace doesn't press A on the picker → never exercised).
  - **Data ready:** `save_bank.h` already models the 100-bank arena (`save_bank_dwords_at`) +
    the picker field constants (SCORE 0xb0f7 / LOOP 0xb0f9 / CARD_DAY 0xb0fb / PORTRAIT_ROT
    0xb0fc / CHAR_LEVEL 0xb100 / GAME_MODE 0xb759 / PLAYTIME 2). Render helpers all exist:
    `font_draw_text`/`_centered`/`_right` (FUN_0047ca05/d14c/d2db), `render_quad_add`/`_flush`/
    `_state_setup`, `render_quad_draw_rotated_rect` (FUN_00406241 portrait),
    `scene1_top_hud_draw_number` (gold), `scene1_merchant_hud_draw_level` (level); textures
    `g_sysassets.item_win_tga` (DAT_073d8748) + `g_scene_pause_pause`/board.
  - **✅ DONE (the render arc, 2026-06-14):** `FUN_0049b556` + `FUN_0049b537` perm +
    `FUN_004812e4` wrapper ported as `save_picker.{c,h}` (title can adopt it later),
    exercised by the pause type-3 render dispatch + the nav-commit. **Pixel-1:1 via
    `orv3_shot`** (gt8 0.00%); the per-frame `--verify-hashes` self-verify is DIVERGENT
    (the known RT-backdrop replayer limit) so `orv3_shot`'s compositing frame render is
    the valid check. **M4b NEXT — the picker NAV (`FUN_0047f5bc`):** U/D ±1 / L/R ±3 +
    the c894/c898 slide anims + the overwrite-confirm dialog + B-cancel; needs a
    nav-driving trace to verify (the current trace only opens the picker). **The wing
    fix** (ramp `g_save_picker_hpage_anim` in the title continue picker) rides with the
    title-picker render port.
- **M3+ (later arcs):** the OTHER submenus — Items, Encyclopedia, Options — +
  Exit-confirm (type 4, the `sub_anim>0` dispatch L83931-83952); the b1b0==1 system.bmp
  fade + action-1/2 pause variants; the unpause cursor restore.

## PORT-DEBT registry (this arc)
- `pause-status-count` — `DAT_0741bed8` party count stubbed 0 (no Status entry).
- `pause-submenu-*` — entering Items/Encyclopedia/Options (Save = M4, DONE).
- `pause-exit-confirm` — type-4 return-to-title yes/no + teardown.
- `pause-unpause-restore` — cursor snapshot/restore (`DAT_06a499ac/b0/b4`).
- `save-picker-wings` — the OFF-SCREEN left/right wing pages (`save_picker_render`
  gate `g_save_picker_hpage_anim>=10` = engine `DAT_09643520`): retail draws 3
  pages, the port 1 (zero pixel impact). The shared global is ramped by the title
  continue picker (`FUN_0049a59e`); the port's `title_continue_picker.c` uses
  instance fields and never sets it. Closes with the title picker render port.
- `save-picker-nav` (M4b) — `FUN_0047f5bc`: U/D±1 / L/R±3 + the c894/c898 slide
  anims + overwrite-confirm dialog; needs a nav-driving trace to verify.
- `save-picker-commit` (M4c) — `FUN_004905a8` disk write (the trace never presses A).
