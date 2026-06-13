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
- **M2c — calendar/gold/portrait + the board background (NEXT).** The retail
  resting-menu draw program is **10 draws** (orv3_draws on `house-pause-f1bf56e7`
  frame 119):
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
- **M3 — the captured-screen BACKDROP [0] + open/close transition (the radial
  blur) — NOW THE ACTIVE TARGET (user: "port [0] now", 2026-06-13).** [0] is the
  captured-screen RT redraw; the transition is a radial-blur/zoom the user flagged.
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
- **M3+ (later arcs):** submenus — Items, Encyclopedia, Options, Save,
  Exit-confirm (the `sub_anim>0` dispatch L83931-83952); the unpause cursor
  restore.

## PORT-DEBT registry (this arc)
- `pause-status-count` — `DAT_0741bed8` party count stubbed 0 (no Status entry).
- `pause-submenu-*` — entering any option (Items/Encyclopedia/Options/Save).
- `pause-exit-confirm` — type-4 return-to-title yes/no + teardown.
- `pause-unpause-restore` — cursor snapshot/restore (`DAT_06a499ac/b0/b4`).
