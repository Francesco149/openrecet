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
    the valid check. **The wing fix** (ramp `g_save_picker_hpage_anim` in the title
    continue picker) rides with the title-picker render port.
- **M4b — the picker NAV (`FUN_0047f5bc`) ✅ DONE + USER-CONFIRMED 1:1 2026-06-14** (`b46858a`+`461d873`;
  user "can confirm the save picker is 1:1" — parity ledger).
  Ported as **`pause_save_submenu_update`** (`scene_pause.c`), dispatched from
  `pause_menu_update` at sub_anim==10 when Save (type 3) is selected (engine
  `FUN_0047fa76` L82031). Transcribed 1:1 from objdump @0x47f5bc..0x47fa4f: **U/D ±1**
  cursor + the **c894 row-slide** (±1→±5 over 4 frames → scroll ±1), **L/R ±3** cursor +
  the **c898 column-slide** (→ scroll ±3, clamped 0..97), **B-cancel** (SE 0x13d, drop
  sub_dir → the submenu slides closed, hide the cursor `FUN_00435612`). +13 host tests
  (the nav arithmetic, the dispatch gate, the navigable predicate, the anchor edge).
  - **New `SAVE_PICKER_READY` anchor** (port `anchor_trace.c` + the frida agent) — fires
    when the Save submenu becomes navigable (scene 9, sub_anim==10, Save selected). The
    async pause-asset load makes `PAUSE_READY` land at a per-side-VARIABLE open-ramp phase
    (port ~1f, retail ~1800f), so `PAUSE_READY`+offset inputs reach the picker at a
    different *picker-time* per side; rebasing the nav HERE makes the inputs
    picker-time-relative ⇒ cursor/scroll align 1:1. Trace **`house-pause-save-nav`** (ESC →
    3×down → Z opens Save; then `{wait SAVE_PICKER_READY}` → DOWN ×5 / UP ×4 scroll → B
    close; v3 join 239/239).
  - **Verified vs the retail v3 cache** (`house-pause-save-nav-725d14bb`, SAVE_PICKER_READY
    +0:240, `orv3_shot` composite render): the nav **cursor/scroll is PIXEL-1:1** — the
    breathing-aligned nav frames are bit-identical (gt8 **0.000%**) and the **post-close
    option list is bit-identical across 24+ consecutive frames** (offsets 172-236 gt8 0.000).
  - **The one residual (the SELECTED card's breathe brightness) ✅ FIXED same day** (`461d873`,
    user "fix the selected brightness"). It was `sin(g_save_picker_frame·0.1)·32` out of phase:
    `g_save_picker_frame` (engine `_DAT_09643574`, **never reset** — decompile has only `++`
    @0x49b566 + the sin read) is **SHARED with the title Continue picker render**, which retail
    runs (the trace navigates Continue, ~100+ frames) but the port modeled with a SEPARATE
    counter (`g_picker_anim_counter`). Unifying them (the title render increments the shared
    `g_save_picker_frame`) + mirroring the wing gate ⇒ nav **PIXEL-1:1** (gt8 0.000%) AND
    **draw-program 1:1** (0 divergent). See `save-picker-shared-globals` CLOSED below; quirks
    §124/§125. (Originally diagnosed as a deferred-title-render PORT-DEBT — the diff localized
    to ONLY the selected card, proving the nav cursor/scroll was already 1:1.)
  - **M4c — the A-confirm + COMMIT ✅ DONE + PIXEL-1:1 2026-06-14** (`scene_pause.c`
    A-branch + commit_tick + the progress bar; `save_io.c` `save_io_commit_slot`). A on the
    cursor's slot (engine 0x47f889): SE 0x143, then an **EMPTY** slot commits at once
    (`g_pause_save_phase`=1) while an **OCCUPIED** slot pops the **"Overwriting file. Are you
    sure?"** choice box (`FUN_00434def`, exact string — a single row, no `<BR>`); its Yes/No is
    polled (`choice_box_poll` = `FUN_00434ed2`) — Yes → phase=1, No/B → cancel. The phase>=1
    **commit sequence** (engine 0x47f63f): on the first frame, snapshot the card fields
    (0xb381 type + clear the 0xb75a/0xb78e preview blocks — pixel-invisible, the picker render
    reads GAME_MODE/SCORE/… not these), play the streamed save jingle
    (`bin/se/01ti/system/01ti_sys04.bin`), set last_slot, and **`save_io_commit_slot(tslot)`** =
    `FUN_004905a8`: copy the live working bank → save bank `tslot`, re-stamp its checksum, write
    save.dat/_save.dat (the `{savefile}` sandbox keeps replays off the real save); then the
    counter runs 1→0x3c and wraps. The **save-progress bar** (`FUN_004812e4` c89c>0): two
    item_win.tga quads over the selected card under COLOROP=ADDSIGNED — an empty-bar frame +
    a fill quad growing with c89c/30, grey pulsing with the same −128·sin the card uses, alpha
    fading past c89c>0x34 (geometry/the dropped sin amplitude from objdump 0x481358-0x481408).
    **Verified vs the retail v3 cache** (new trace **`house-pause-save-commit`**: ESC→3×down→Z→
    {wait SAVE_PICKER_READY}→A on slot 0→A confirms Yes→commit; `orv3_shot` per-frame):
    **port#N vs retail#N+1 (the 1-frame async-pause seam) is ~0.12% / meanabs ≤0.10 across the
    whole window** — the Overwriting dialog (0.07%), the commit ramp, the progress bar (0.12%),
    and the post-commit resting card all match within M4's accepted breathe/seam phase envelope;
    the only elevated frame is the single dialog-close transition (1.86%, the box text 1 frame
    off at the seam). +6 host tests (the A empty/occupied branches, the Yes/B response, the
    counter wrap, the commit-slot merge+checksum). **PORT-DEBT(save-commit-dungeon):** the
    dungeon "Saving here will save your data<BR>…" warning (`FUN_00434ceb`, gated
    saved_mode==1 && stage_type>0 — inert in the HOUSE) + the `FUN_0047f1a0(0)` town-state swap;
    **PORT-DEBT(save-card-type-modes):** the mode-6 (guild-rank) / mode-0xb (live snapshot copy)
    card-type branches (un-modeled data, unreachable here). Both behind a dungeon/other-mode
    save trace.
- **Options submenu (type 2) ✅ DONE + BIT-EXACT + USER-CONFIRMED 1:1 2026-06-15** (`b3288b8`
  render+nav+tests, `fc14f2d` OPTIONS_READY+nav). The config panel: Music/Sound/Voice volume
  0..9 + Message Speed (SLOW/MED/FAST) + Unread Text Skip (OFF/ON), A/B exit (saving when dirty).
  - **Shared render `settings_panel.{c,h}` = `FUN_0049c050`** (dungeonbord.tga backdrop src
    (0,0,320,360)→dst(slide_x+160,base_y+32) MODULATE; 5 rows: left label @(slide_x+208,
    row*40+base_y+112) + right value @(slide_x+400), selected row 0xff7f7f00 / rest 0xff7f7f7f
    under COLOROP=MODULATE2X; the "Saving" overlay (savewindow.tga + the word) on a dirty exit).
    The engine shares this ONE render with the TITLE settings submenu — `g_scene_state==0 ? 6 : 5`
    rows (title adds a centered "Clear Save Data"). Strings (Music/Sound/Voice/Message Speed/
    Unread Text Skip; SLOW/MED/FAST; OFF/ON; the %d/%s formats) recovered from the PE .data/.rdata
    (Ghidra dropped them). The two SetTextureStageState(COLOROP, ADDSIGNED then MODULATE2X) writes
    are matched (the second wins; font_draw inherits it).
  - **Update `pause_options_submenu_update` = `FUN_0047fc44`** (`scene_pause.c`): U/D move the
    cursor row (%5) + SE 0x146 + the 6-frame cursor slide; L/R adjust the current row's value
    (clamps: audio 0..9, slider3 0..2, slider4 0..1) — Music re-applies the BGM volume
    (`audio_fade_apply`), Voice (SE-B) is silent, the rest play SE 0x146; A/B exit → dirty (a
    slider changed, phase 1) ⇒ exit-save (phase 2, `save_io_commit_slot(-1)` = `FUN_004905a8(-1)`)
    / clean ⇒ exit-no-save (phase 3), then the submenu closes. + the nav-commit type-2 init
    (row 0, cursor snap 168,168) + the update/render dispatch.
  - **Verified** vs the retail v3 cache (new **OPTIONS_READY anchor**, join 239/239, 0 draw-
    divergent): **gt8 0.0000% BIT-EXACT** at every probed offset across `house-pause-options`
    (resting + the slider nav: cursor on each row + numeric AND word value changes); draw program
    **56/56 ALIGNED**. +9 host tests (3290). **OPTIONS_READY** is the robust join anchor — it
    fires AFTER PAUSE_OPEN on both sides, where PAUSE_READY is STRADDLED by PAUSE_OPEN (pre-load
    port / post-load retail) so a PAUSE_READY window mispairs.
- **Items submenu (type 1) ✅ DONE + BIT-EXACT 2026-06-15, AWAITING USER 1:1** (`f639d3d`). The
  in-game inventory grid: ESC → Z (Items is index 0/default in the house list) opens the
  display-menu (`FUN_0046b00a`) sliding in from the right — the player's items grouped by
  category (Swords tab: Worn Sword/Dark Sword), the category banner, the bottom description panel
  (Base Price / Number possessed), over the M3 pause backdrop.
  - **Setup** nav-commit `FUN_00480614` type-1 (L82682-82693): `FUN_004682c5` slide-activate +
    `display_menu_open(mode 5, 1)` (scans the working-bank inventory → category tabs, snaps the
    shared hand cursor to the first item). The dungeon variant picks mode 6 (gated saved_mode==1
    && stage_type>0) ⇒ PORT-DEBT; the house stage_type is 0 so mode 5 is always taken.
  - **Update** `FUN_0047ff40` (house path, `pause_items_submenu_update`): `display_menu_update(1)`
    grid nav → 3 pick-up SE 0x143 / 2 CANCEL (B → close: sub_dir=0, sel_anim=0, hide cursor
    `FUN_00435612`, slide inactive `FUN_004682d0`, SE 0x13d) / 1 CONFIRM (house exits — place-mode
    is dungeon-only). The place / use-medicine / equip paths (DAT_074b28a4!=0) are PORT-DEBT.
  - **Render** `FUN_0048196b` → `FUN_0046b00a(640 - sub_anim*64, 0)`: the grid slides in (slide_x
    640→0 as sub_anim 0→10); the dungeon medicine/equip option loop is gated *DAT_068dd2f0>0
    (house skips it). Threaded a `slide_x` param (FUN_0046b00a param_1) through
    `display_menu_render` — the shop (main.c) + guild (scene_guild.c) call sites pass 0.0f.
  - **Price-label fix (latent SHARED-display_menu bug the Items submenu EXPOSED):** the engine keys
    the description price label off the SCENE (`DAT_0438b1c0`) — guild (scene 6) buy
    (`FUN_00491612`==0)→"Purchase Price-" / sell→"Sell Price-", **every other scene→"Base Price-"**.
    The port keyed off the display-menu MODE, and mode 5 is shared by the guild SELL *and* the
    pause Items, so it mislabeled the pause Items "Sell Price-". Now scene-gated (scene 6 splits
    buy/sell by mode 7/5, in lockstep with FUN_00491612); the guild buy stays "Purchase Price-".
    Caught by the v3 draw-program + pixel diff (the description text diverged), `feedback_verify_1to1`.
  - **ITEMS_READY anchor** (anchor_trace.c + the frida agent) — the rising edge of "Items submenu
    navigable" (scene 9, sub_anim==10, Items selected); fires AFTER PAUSE_OPEN on both sides
    (PAUSE_READY straddle), so the v3 join bridges the +4410-frame async pause-load stretch
    (**199/199**, was 43/199) and the shared hand-cursor bob aligns picker-time-relative.
  - **Verified** vs the retail v3 cache on the new `house-pause-items` trace (ESC → Z →
    {ITEMS_READY} → capture): the grid + item rows + description + the corrected "Base Price-"
    label are **PIXEL-BIT-EXACT** (description panel gt8 0.0000% / 0 px; absolute best resting pair
    gt8 0.0422%). The ONLY residual is the shared hand-cursor's sub-pixel BOB (~330 px, x443-510
    y130-179, = the port-only draw b8b7 vs retail's cursor) at the 1-frame async-pause seam — the
    accepted seam/bob phase pillar (same class as M4c). +3 host tests (3292). Feed "Pause ITEMS
    submenu (type 1) — RETAIL | PORT | diff".
- **Exit-confirm (type 4) — ✅ DONE + PIXEL-BIT-EXACT 2026-06-15, AWAITING USER 1:1** (`b32be5d`
  mechanics + `8303fef` title re-init).
  ESC → 4×down → Z opens the **"Returning to title screen. Are you sure?"** choice box; **No** cancels
  back to the menu, **Yes** quits to the title (fade-out → scene→0 → title load → fade-in). Ported the
  nav-commit type-4 (`FUN_00480614` L82724-82728: `g_pause_exit_confirm=1` + cursor snap, NO submenu —
  the engine gates sub_anim++ on iVar1<4) + the update branch (`FUN_0047fa76` L82059-82102 →
  `pause_exit_confirm_update`): `choice_box_open`/`choice_box_poll` (Yes=1 → quit / No=2 → cancel), the
  quit sequence (`g_pause_exit_phase` 1→0xf → `fade_phase1_start` fade-out → `fade_is_done` →
  `sim_set_mode_9a0(0)` + `d3d_pool_release_type(0xc)` + `g_scene_state=0` + `worker_load_spawn` +
  `fade_phase_out_start` fade-in). The dialog renders via the shared choice box (pause tail); no new
  render dispatch. **The dialog + the fade-out were 1:1 from the first cut** (per-frame brightness
  tracks retail — dialog ~78, black ~0 at the fade bottom, title ~190 — the SAME transition).
  - **The title re-init (worker case-0) — ✅ DONE (`8303fef`):** the Yes→title used to land in the
    WRONG sub-state (the boot Continue-picker left `submenu_state==1` ⇒ the LOAD-GAME card list
    rendered, not the menu) because the engine's title re-init runs on the primary load **worker
    case-0** (`LAB_0045293d` case 0 @ 0x452961 = `FUN_004733d5` asset reload + `FUN_0049a3a3` menu
    reset) which was UNREGISTERED in the port ⇒ `worker_load_spawn` was a no-op + the title resumed
    stale. Ported `FUN_0049a3a3` as **`scene_title_reinit`** (`scene_title.c`, pure-C) + registered
    `worker_load_set_cb(0, scene_title_reinit)` at boot (`main.c`). Body: `FUN_00434ce3`
    (`title_save_dialog_set_active_counter(0)`, DAT_0438b148) · `scene_title_anim_init_fresh` (the
    DAT_09643518..5c zero block + menu_folding_out=1 → submenu_state=0, continue_mode=0) ·
    `save_io_scan_for_title_menu`+`scene_title_menu_init` (= `FUN_0049a43d`) + cursor_pos =
    default_cursor · cursor snap(212,270)+hide (`FUN_00435693`/`FUN_00435612`) ·
    `music_clear_forced_track` (= `FUN_00499560`: forced_track=-1, paused_b=0). `FUN_004733d5` asset
    reload = faithful no-op (title textures persist from boot; the Exit's `d3d_pool_release_type(0xc)`
    frees only pool-tagged assets).
  - **Verified vs the retail v3 cache** (`house-pause-exit` ESC→4×down→Z→Z, re-drove the port over
    PAUSE_READY+180..480): the resting title menu is **PIXEL-BIT-EXACT — gt8 0.0000% / meanabs 0 /
    maxdiff 0** at every settled offset (RECETTEAR logo + NEW/LOAD/Options/Exit; feed "Pause EXIT
    (type 4) → quit-to-title — FIXED"). The studio auto-join is seam-split here (the port emits no
    PAUSE_CLOSE on the exit path) so the compare is by **aligned window-index** (port present_first
    1018 / retail 5292 both land just past their title load ⇒ the whole window is the resting menu),
    per this scenario's known load-seam — verify by content-matched frames, not the auto-join pairs.
    +3 host tests (3298): the reinit clears the stale submenu / rebuilds the menu + hides the cursor /
    runs end-to-end via `worker_load_dispatch_pure(0)`.
  - **PORT-DEBT(exit-house-teardown):** `FUN_00474d92` (the house/shop D3D resource free) — a faithful
    no-op here (the port's resource model differs; the title reloads its own assets).
- **M3+ (later arcs):** the title re-init now closes the Exit (above); remaining: the b1b0==1
  system.bmp fade + action-1/2 pause variants; the unpause cursor restore (already done — see
  `pause-unpause-restore` below).

## PORT-DEBT registry (this arc)
- `pause-status-count` — `DAT_0741bed8` party count stubbed 0 (no Status entry).
- ~~`pause-submenu-*`~~ **all 5 base entries DONE** (Items = type 1, Encyclopedia = type 6, Options =
  type 2, Save = M4, Exit = type 4 — all rendered + navigable + verified). Only the Status entry
  (type 0) stays stubbed, gated on the unported party count (`pause-status-count`).
- `pause-items-dungeon` — the Items submenu dungeon variant: display_menu **mode 6** (vs the
  house's mode 5) + the place-an-item / use-medicine / equip-readout paths (`FUN_0047ff40`
  DAT_074b28a4!=0 branch + the FUN_0048196b dungeon option loop). Gated saved_mode==1 &&
  *DAT_068dd2f0>0 — inert in the house. Needs a dungeon-pause trace.
- `options-config-arena` — the Options exit-save (`save_io_commit_slot(-1)`) writes the save
  arena but the port's live slider values aren't synced into the save-header config region
  (config is module state in audio_fade/settings, not the arena buffer) — pixel-invisible,
  only the written save bytes' config area differs; closes when the config↔arena model unifies.
- `settings-panel-title-adopt` (cleanup) — the TITLE settings render
  (`scene_title.c` `scene_title_settings_render_panel`) is still a SECOND copy of `FUN_0049c050`;
  it should call the verified shared `settings_panel_render`. Needs a title-settings trace to
  verify the title path before swapping (the render is render-only, no host test covers it).
- ~~`pause-exit-confirm`~~ **DONE 2026-06-15** (dialog + Yes/No + No-cancel + fade + scene→0 +
  title re-init, host-tested + pixel-bit-exact) — see the Exit-confirm milestone above.
- ~~`exit-title-reinit`~~ **✅ CLOSED 2026-06-15** (`8303fef`). The Yes→title landed in the WRONG
  sub-state (load-game picker open vs the title menu) because the engine's title re-init runs on the
  worker **case-0** (`FUN_004733d5`+`FUN_0049a3a3`), UNREGISTERED in the port ⇒ `worker_load_spawn`
  was a no-op + the title resumed stale. Ported `FUN_0049a3a3` as `scene_title_reinit` + registered
  `worker_load_set_cb(0, …)` at boot; the resting title menu is now **pixel-bit-exact** vs retail
  (gt8 0.0000% on `house-pause-exit`). +3 host tests.
- `exit-house-teardown` — `FUN_00474d92` (house/shop D3D resource free on quit) — a faithful no-op here.
- ~~`pause-unpause-restore`~~ **✅ CLOSED 2026-06-15** (`scene_pause.c` `pause_dispatch`).
  **USER-OBSERVED SYMPTOM 2026-06-14: exiting the pause menu reseated Recette at the
  scene SPAWN position instead of where she was paused.** **RE correction — the cause
  was NOT a missing player-pose snapshot** (`DAT_06a499ac/b0/b4` are the shared HAND-
  CURSOR snapshot, not the player): the port **re-spawned the load worker on unpause**
  (the old `worker_load_spawn()`), which re-ran the INGAME case-1 load
  (`scene1_preload_house` → `scene1_postload_pose_player`) and re-posed Recette at the
  spawn. **The engine never reloads on unpause** — it FREEZES the underlying scene
  through the open ramp (the per-mode sim dispatch is skipped while `DAT_06a49998 != 0`)
  and RESUMES it in place once the close ramp (dir=0) cycles back to 0. Fix = drop the
  worker re-spawn and run the engine teardown (`FUN_00453384` L50254-50283):
  `stage_load_pulse_set_active(0)` (FUN_004682d0) · cursor hide (FUN_00435612) ·
  `chara_equip_recompute_aggregate` (FUN_004844ef) · `d3d_pool_release_type(0xc)`
  (FUN_00473c03 — frees the pause assets; a faithful no-op in the port since the pause
  sprites are static `sprite_t`, not pool entries, so the close animation keeps them) ·
  dir=0 · the hand-cursor restore (FUN_00435693, gated on the pause-open visibility
  snapshot DAT_06a499ac/b0/b4 — inert in HOUSE free-roam). **Verified 1:1 on the new
  `house-pause-unpause` scenario** (walk RIGHT off spawn → ESC pause → ESC unpause):
  the state panel's player px/py is **bit-identical port==retail at the walked-to
  (+3.10,+6.04) across all 112 aligned pairs** (open→unpause→resume; 0 mismatches),
  never jumping to spawn (−0.30,+9.35); the resumed-scene pixels match retail
  (gt8 0.52% @ mean|abs|=0.00/ch = the pre-existing benign HOUSE 98-vs-125 batching).
  +3 host tests (no-reload / cursor-restore / cursor-stays-hidden). **✅ USER-CONFIRMED
  1:1 2026-06-15** ("can confirm the pause menu behaves correctly now" — parity ledger).
  **Still PORT-DEBT(pause-shop-restore):** the
  unpause shop-grid rebuild (`DAT_06a499b8` → `FUN_00468338` full + `FUN_004681d3`
  DAT_0734b96c reset) — inert outside a shop-display pause; and the guild-pause
  re-init (saved_mode==6 → FUN_00490e15) + actions 1/2 (FUN_00473668/672).
- `playtime-ticker` (NEW, user-observed 2026-06-14 — NOT pause-specific, but noticed via
  the Save submenu) — **✅ CLOSED 2026-06-14.** The in-game playtime never advanced (sit a
  minute, re-save → the SAME `TIME h:mm:ss` on the card). Retail increments the working
  bank's playtime dword each frame at `FUN_004536cb`'s HEAD (L50357-50360, the very first
  thing — before `font_age_tick` AND the worker-busy gate): `working[active].dword[2] += 1`
  (= `SAVE_BANK_FIELD_PLAYTIME`, the frames@60 the card's TIME reads) gated `DAT_0438b1c0 != 0`
  (g_scene_state — any non-title scene, incl. the pause menu mode 9). The port read the field
  but never wrote it. Fixed by porting it to the head of `sim_step_a` (the `FUN_004536cb`
  port): `if (g_scene_state != 0) save_work_dwords_at(save_work_active_slot())[2]++`. Verified
  on `house-pause-save-commit`: the port's post-commit card TIME now ADVANCES (0:03:50 loaded
  → 0:04:03, +13s over the ~780-frame trace) where it was frozen before. **Load-dependent-
  counter caveat:** the playtime is now a load-dependent accumulator like db054 — in a PINNED
  trace the port's card TIME lags retail's (port 0:04:03 vs retail 0:05:14, a ~71s load-seam
  offset: retail's much slower pause-asset load ticks ~4260 more frames before the commit).
  This is the SAME load-seam phase pillar as db054, NOT a logic gap (real un-pinned gameplay
  ticks 1/frame correctly — the user's actual issue is fixed). **Follow-up (clean pinned
  comparison):** fold the working-bank playtime into `{phasepin}` (pin its origin like db054)
  so the card TIME matches frame-for-frame in pinned traces. RNG-neutral; +0 host tests
  needed (sim host build unaffected, 3270 pass).
- ~~`save-picker-shared-globals`~~ **✅ CLOSED 2026-06-14** (`461d873`). The two
  save-picker globals the TITLE Continue picker drives that the port wasn't sharing into
  a later pause Save submenu: (a) the wing-render gate `g_save_picker_hpage_anim` (engine
  `DAT_09643520`) and (b) the breathe counter `g_save_picker_frame` (engine `_DAT_09643574`).
  The port had modeled the engine's ONE render (`FUN_0049b556`) as two divergent copies
  with separate state (`scene_title.c`'s `g_picker_anim_counter` + local `cursor_anim` vs
  `save_picker.c`'s globals). Fix: the title render now increments the shared
  `g_save_picker_frame` (never reset ⇒ carries the Continue history) and mirrors
  `g_save_picker_hpage_anim = cursor_anim` (its 0→10 open-ramp, left at 10 post-Continue).
  Re-drove the port: nav **PIXEL-1:1** (worst gt8 0.000%, was a 22% breathe beat) AND
  **draw-program 1:1** (0 draw-divergent, was 169 — the wings now match). Quirks §124/§125
  updated (the divergence is closed; they remain the retail ground-truth).
- ~~`save-picker-commit`~~ **✅ CLOSED 2026-06-14 (M4c)** — the A-confirm + COMMIT
  (`FUN_0047f5bc` A-branch + `FUN_004905a8` `save_io_commit_slot` + the `FUN_00434def`
  overwrite dialog + the `FUN_004812e4` progress bar), pixel-1:1 on `house-pause-save-commit`.
  See the M4c entry above. Replaced by two narrower debts:
- `save-commit-dungeon` (M4c residual) — the dungeon "Saving here will save your data<BR>…"
  warning (`FUN_00434ceb` + the `FUN_00434d6a` poll + the `DAT_074b28a0` gate) and the
  `FUN_0047f1a0(0)` town-state swap (the `FUN_0047f172(1)`/`FUN_0047f1a0(1)` backup/restore is
  a provable no-op in the non-dungeon house). Gated saved_mode==1 && stage_type>0 — inert in
  the house. Needs a dungeon-save trace.
- `save-card-type-modes` (M4c residual) — the card-snapshot type for game-mode 6 (the
  `DAT_0963c5f0` guild-rank 3/4 split) + mode 0xb (the live `DAT_0438b5ec`/`664` snapshot copy);
  un-modeled data, unreachable from the ported pause Save submenu (house = mode 1, type 0). The
  shared default (1) covers the rest. Pixel-invisible (the picker render doesn't read 0xb381).
