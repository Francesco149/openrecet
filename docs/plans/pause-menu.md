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
    - **[0]** tex `3e66`, full-screen 640×480, MODULATE, white — the **static
      board background** behind the whole menu (the dark panel the calendar
      sits on; the cyan-vs-retail diff). Hash is STABLE across all resting-menu
      frames (80/100/110/119 — the menu is already at rest by f80) ⇒ a static
      layer, not a fade. **CORRECTION (was mis-attributed to `FUN_0045404b`):**
      reading `FUN_0045404b` (0x45404b) shows it is the OPEN/CLOSE
      **captured-screen cross-fade** — CopyRects the screen → redraw with alpha
      `0xff − sin(DAT_06a49994·π/DAT_005c5938)`, gated `0 < DAT_06a49994`, tex
      `DAT_073cb900` → that belongs to **M3 (the fade transition)**, NOT this
      board. [0]'s real source is still OPEN: candidate assets are `dungeonbord`
      (`DAT_073a9b08`) / `result_bord01` (`DAT_073d86c8`) — both pause-loaded,
      but neither has an obvious single full-screen draw (result_bord01 is drawn
      ×3 near all.c:82959, dungeonbord near 101624). **RESOLVE FIRST in M2c via
      the viewer pixel-pick** (`orv3_window … --launch`, click the board bg →
      the draw index → its source via `call_trace.jsonl`) — the canonical v3
      method; don't keep guessing from static analysis.
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
  Save-arena fields are addressable (`save_work` bank base + `DAT_0438b1e0`
  slot, the same `bank*0x2dfc8 + offset` geometry chara_equip uses): gold `+0xc`,
  calendar period `+0x2c3f8`/`+0x2c3fc`, level `+0x2c400`, a mode `+0x2dd64`,
  current day `_DAT_0438b91c`. Portrait = `FUN_0048d997` (TBD). Once [0]+[4-9]
  land, the WHOLE resting menu is cleanly pixel-1:1-verifiable in one diff.
- **M3+ (later arcs):** submenus — Items, Encyclopedia, Options, Save,
  Exit-confirm (the `sub_anim>0` dispatch L83931-83952); the unpause cursor
  restore; the open/close fade transition (`FUN_00454191` body).

## PORT-DEBT registry (this arc)
- `pause-status-count` — `DAT_0741bed8` party count stubbed 0 (no Status entry).
- `pause-submenu-*` — entering any option (Items/Encyclopedia/Options/Save).
- `pause-exit-confirm` — type-4 return-to-title yes/no + teardown.
- `pause-unpause-restore` — cursor snapshot/restore (`DAT_06a499ac/b0/b4`).
