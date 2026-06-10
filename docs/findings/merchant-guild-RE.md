# Merchant's Guild scene (engine mode 6 / Market) — RE + port plan

Status: **scene shell + first-visit cutscene LANDED** (`a998fb4`, 2026-06-10) — the
port now enters mode 6, renders the guild bg, and plays the iv1_3 first-visit cutscene
(visually confirmed on the trace session: bg + guildmaster/Recette standees + text box
+ Esc-skip prompt, matching the user's notes @138 enter / @179 fade-in / @229 standees).
Trace-studio session `merchants-guild-20260608-151902`. The world-map → guild path is
1:1 up to entry (labels 330–490). See `## Port progress` below for what's done + open.

## What the guild IS
The world-map "Merchant's Guild" (dest 3) is **internally the Market scene, engine
mode 6** (`g_scene_state == 6`), labelled "Merchant's Guild" in the UI but driven by
`ivent_bg_ichiba`. Cross-ref `docs/findings/town-map-RE.md:170` (the dest→mode table:
dest 3 → mode 6 via `FUN_00490e16(0)`; dest 1 → mode 6 via `FUN_00490e16(1)`).

`FUN_00490e16(v)` (`all.c:93528`, 14 B): `DAT_0963c5f0 = v; FUN_0049174e();` — sets the
market **variant flag** (0 = guild/"Merchant's Guild" tutorial target, 1 = the other
mode-6 dest) then runs scene-init `FUN_0049174e` (loads the bg textures
`DAT_073da000/010/020`).

## Current port state (the cyan)
Mode 6 is **fully stubbed** in the port:
- **Update:** `src/sim.c:366` — modes 2/3/6/7/0xb/0xd-0x10 fall into a bare
  `scene1_particles_tick()` stub. No event tick.
- **Render:** `src/main.c:3050` — `default: break;`. Mode 6 paints nothing but the
  per-frame Clear ⇒ the blank/cyan the user sees (the worldmap's own cyan clear
  `0xff00ffff` / the Pass-F flat-vertex fallback; the bare mode-6 placeholder clear is
  `0xff17f0ff`).
- **Worker-load:** no `worker_load_set_cb(6, …)` ⇒ no assets load.
- The transition IS wired: `src/scene_worldmap.c` `scene_worldmap_exit_to_dest()` sets
  `g_scene_state = 6` on dest 3 (carries `PORT-DEBT(worldmap-dest-scenes)` — "renders
  blank until that scene ports").

## The first-visit cutscene trigger (empirical, from the retail call-trace)
At session label 580 (abs 14590) retail spawns the cutscene dialogue. Call chain:
`FUN_00490e24` (mode-6 update) → `FUN_004922c0` (per-location event tick) →
**`FUN_0044ba2c(1,3,1)`** → `FUN_00452d07` (dialogue-spawn worker). `FUN_0044bd0d`
(the iv1_5/iv1_6 tutorial dispatcher we already ported) is **NOT** on this path.

`FUN_004922c0` first-visit branch (`all.c:94770-94774`), gated by:
- `DAT_09642c38 == 2` (the per-location entry-tick counter; `++` at fn top, line 94752),
- `(&DAT_045114fc)[loc*0xb7f2] != 2` (location-type check),
- `(&DAT_0450f3f4)[loc*0x2dfc8] == '\0'` (per-location **first-visit seen** flag — unset)
```
(&DAT_0450f3f4)[iVar7] = 1;     // mark seen (fires once)
FUN_0044ba2c(1,3,1);            // play dialogue group=1 index=3 mode=1
FUN_00452809();
return;
```
`FUN_0044ba2c(g,i,m)` (`all.c:45243`, generic "play event dialogue"): early-out if
`DAT_0438b1c8 != 0` (busy); else `DAT_005c7a2c=g; DAT_005c7a30=i; DAT_0438b1c8=2;
FUN_00452d07(m)`.

**Dialogue script (group=1, index=3) → `iv/iv1_3.ivt`** (confirmed present in the data
pack). The path is built by `FUN_0046ddea` = port `scene1_dialogue_load(scene,sub,…)`
("iv/iv%d_%d.ivt" from `DAT_005c7a2c/30`).

`loc` = `DAT_0438b1e0` (current stage index). Port models this as
`save_work_active_slot()` but it is **pinned to 0** (no per-location routing yet).

## Reusable port pieces (NO new dialogue code needed)
- **Spawn:** `scene1_intro_dialogue_start_single(int scene, int sub)`
  (`src/scene1_intro_dialogue.c:257`, decl `.h:53`) = port of `FUN_0044ba2c`/
  `FUN_00452d07`. For the guild: **`scene1_intro_dialogue_start_single(1, 3)`** loads +
  runs `iv1_3.ivt` through the existing interpreter (`scene1_dialogue_run.c`) and draw
  (`scene1_dialogue_draw.c`) + conversation-pose (standees). Busy-guard:
  `scene1_intro_dialogue_busy()`.
- **Save flag** `DAT_0450f3f4` (guild first-visit) = working-arena **byte offset
  `0x2bc5c`** (`0x0450f3f4 − 0x044e3798`), index `+ slot*0x2dfc8`, off
  `save_work_dwords_at(save_work_active_slot())` cast to `uint8_t*`. Siblings already
  defined: `scene1_player_ctrl.c:1004` (`0x2bc5f/61/62`),
  `scene1_tutorial_dispatch.c:16` (`0x2bc63-67`), `scene_worldmap.c:135`.

## Render chain to port (the bg)
- `FUN_00490e35` (`all.c:93554`, 15 B): `FUN_0049b425()` (2D state setup, 207 B
  `all.c:101206`) → **`FUN_00494a73()`** (bg) → `FUN_00406d50()`.
- `FUN_00494a73` (`all.c:96175`, 561 B): a 2D sprite blit. Normal path
  (`DAT_09642c3c==0`): full-screen blit of `DAT_073da000` (640×480), then a 2nd/3rd
  texture (`DAT_073da010`, and `DAT_073da020` only when `DAT_0963c5f0==1`), then
  `FUN_0049404b`, **`FUN_0046b00a(0,0)`** (the item_win drawer — ALREADY ported, see
  shop-display-menu-RE), `FUN_0043537e`, `FUN_00491de0`, `FUN_00435747`, `FUN_00435117`.
  Else (`!=0`, mid-transition): bg blit with alpha `0xff000000`.
- The bg textures are loaded by `FUN_00473769` (texture-group 7, dispatched by the
  worker via `FUN_00471905(7)`). **CORRECTION to the earlier "ivent_bg_ichiba" note:**
  that is the *variant-1* (dest-1) bg. The dest-3 **guild** (variant 0, `DAT_0963c5f0==0`)
  loads (exact paths confirmed in `lnkdatas.bin`):
  - `DAT_073da000` ← **`bmp/ivent/bg_guild.bmp`** (1024×512) — the room bg
  - `DAT_073da010` ← **`bmp/ivent/13syounin_01.tga`** (512×512) — the guildmaster sprite
  - `DAT_073da020` ← `bmp/result/bord01.tga` (512×256) — unused in the variant-0 render
  (`FUN_004918b0`, called by `FUN_0049174e`, is NOT a texture loader — it builds the
  guildmaster idle-anim sequence table `_DAT_09640624` + `DAT_005cfab4`.)

## Port plan (next session) — incremental, recapture-verify each step
Trace: `merchants-guild-20260608-151902` (re-window via `edit.trace.jsonl`; recapture
`--only port` to iterate). **Add the canonical pin first** — the trace is unpinned and
crosses 1 load bracket; lint asks for `{phasepin}` @330 + `{rngseed 19937}` @ the pin +
`{"tutloadpin": 8}` (the guild-load bracket length differs run-to-run, quirk §119,
which is why the port/retail label axes drift after label 490 — `kept_count_mismatch`).

1. **Scene shell:** `sim.c` case 6 → mode-6 update (port `FUN_00490e24`/`FUN_004922c0`
   structure, at least the entry-tick counter + first-visit branch); `main.c` case 6 →
   render (`FUN_0049b425`+`FUN_00494a73`+dialogue-draw); `worker_load_set_cb(6, …)` →
   `FUN_0049174e` asset load. Variant flag `DAT_0963c5f0` from the dest.
2. **Cutscene trigger:** first-visit branch → define `DAT_0450f3f4` flag (0x2bc5c),
   `scene1_intro_dialogue_start_single(1,3)` on the 2nd entry tick when unset.
3. **Wire the dialogue DRAW + TICK into mode 6** (currently INGAME-only:
   `main.c:2949` draw, the tick lives in `scene1_ingame_tick`). Standees/pose/text reuse
   the shop-display chips (text gradient `font_draw_text_fade`, X-hold voice-mute §120).
4. **Verify** the cutscene 1:1 (`trace_studio triage`, anchors, audio_diff — watch for
   the X-hold voice grunts the user flagged: retail mutes voice while fast-forwarding).

Then the user re-windows past the dialogue for the **guild main menu** (note: a "new"
badge appears top-left of Talk when there's unseen dialogue), then the **buy flow**
(Z buy → Z sword → up qty 2 → Z confirm), then the tail (tab-switch + single buys).

## Port progress (2026-06-10, `a998fb4`)
Landed `src/scene_guild.{c,h}` + wiring (`sim.c` case 6, `main.c` render case 6 + init,
`scene_worldmap.c` variant set). **Done (steps 1–3 of the plan):**
- **Scene shell:** `worker_load_set_cb(6, …)` loads the variant-0 texture set;
  `scene_guild_render` = `FUN_00490e35`/`FUN_00494a73` guild path — full-screen bg
  (`bg_guild.bmp`, dst 0,0,640,480 / src 0,0,640,480) + the H-**mirrored** guildmaster
  (`render_quad_add_mirrored`, dst **−64,32,448,448** / src 0,0,512,512 — `FUN_00404e61`).
- **Cutscene trigger:** `scene_guild_sim` = `FUN_00490e24`→`FUN_004922c0` first-visit
  subset — entry-tick counter (`DAT_09642c38`, reset in the load cb per `FUN_0049174e`)
  fires on the **2nd tick**; first-visit flag `DAT_0450f3f4` at working-arena byte
  **0x2bc5c** (fires once, persists). The loc-type `!=2` guard is structurally
  always-true for mode 6 (never a dungeon) — not read literally (loc→slot pinning makes
  the 0xb7f2-stride read unreliable; PORT-DEBT(loc-routing)).
- **Dialogue wiring:** `sim.c`'s dialogue-tick gate extended to mode 6 (loads/advances/
  fast-forwards like the INGAME tutorials); `main.c` draws `scene1_dialogue_draw` on top.
- **Verified (visual, anchor-aligned):** the port emits the iv1_3 anchors it never
  produced pre-fix (TEXT_ANIM_START/END, DLG_LINE_SHOW/CLEAR, EXTRA_SPRITE_END) + renders
  the cutscene. Comparing port↔retail at the SAME dialogue anchor (port TEXT_ANIM_START
  label 1030 ↔ retail label 901), the **scene composition is pixel-identical** (bg + the
  guildmaster/Tear/Recette standees in the same positions). The dialogue *lines* are the
  same script in the same order (iv1_3.ivt through the unchanged shared runtime).
- **Automated pixel-diff verdict BLOCKED — load-suppression seam (phase pillar, accept).**
  `triage` shows `kept_count_mismatch` port=1048 / retail=946 with ~93 contiguous
  port-only labels [510-603]: the port loads the guild FASTER than retail (3 sprite_loads
  vs retail's heavier scene init), so the port goes "active" + renders the early cutscene
  lines ("Guild Master: Sol…") during what is retail's load-SUPPRESSED bracket. Retail's
  first *captured* line is therefore a later one (a capture-boundary artifact, NOT a port
  bug). `{tutloadpin: 8}` (added) only pins tutorial-dialogue brackets, not the guild
  scene-load. **Phasepin pass DONE 2026-06-10** (`{phasepin: 282}` + `{rngseed [282,
  19937]}` in the caprange-opening segment, ~48f before the window): it correctly zeroes
  the PORT db054 (port db054=[0,1]) — but `--align-field db054` still can't run because
  **retail's flow-trace for mode 6 probes ONLY `rng`/`rngcalls`** (db054 count = 0 in
  `retail/call_trace.jsonl`; the pose/px/py/db054 fields the port emits are NOT probed on
  the retail side here). That is the FRONT's open **"run the pinned RETAIL census"** TODO —
  retail rich-field probing isn't set up for mode 6, so the gold-standard db054 verdict is
  blocked by capture-TOOLING, not by the port. Unblocking it needs the retail census /
  agent probe-set extended to mode 6 (RE the retail db054 + pose addresses, add to the
  `{calltrace}` `f` set, recapture) — a separate roadmap task. The phasepin is KEPT (the
  canonical pin the policy wants; the verdict will work once retail mode-6 census exists).
  `edit.trace.jsonl.bak-preguild` backs up the pre-pin trace.
- **Audio:** `audio_diff` flags 2 missing sounds — `se_019_id0150` + `00re_sys09.bin`,
  both at retail abs 14358 = **label ~348** (on the WORLDMAP, ~260 labels BEFORE the
  cutscene). A pre-existing worldmap/records-B-portion gap, NOT the guild cutscene (whose
  voice is correctly muted under fast-forward — 0 extra). Separate arc.

**Open (PORT-DEBT, step 4 + beyond):**
- `FUN_004922c0` tail: guildmaster idle-anim counters (`DAT_09642c40`…), the daily-event
  probe (`FUN_0045de68`, event system unported), the group-6 follow-on cutscenes.
- `FUN_00494a73` UI tail (`FUN_0049404b` fx, `FUN_0046b00a` guild menu frame,
  `FUN_0043537e`/`FUN_00491de0`/cursor/`FUN_00435117`) + the `FUN_00490e35` trailing
  `FUN_00406d50` top-HUD — verify against the diff whether visible behind the cutscene.
- The mid-transition bg path (`DAT_09642c3c!=0`, alpha 0xff000000) + the variant-1
  (ichiba, dest 1) texture set + render.
- Then: guild main menu → buy flow → tail (the post-cutscene re-window).

### Planned follow-on traces (user, for context)
- Leaving the guild triggers a tutorial cutscene (the man gives you bread).
- Returning to Recettear triggers a Tear cutscene.
Both are almost certainly more `FUN_004922c0`/event-tick branches (same machinery).
