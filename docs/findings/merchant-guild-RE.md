# Merchant's Guild scene (engine mode 6 / Market) — RE + port plan

Status: **investigation complete, port NOT started.** Trace-studio session
`merchants-guild-20260608-151902` (served 8782 during the RE session). The world-map
→ guild path is 1:1 up to entry (labels 330–490); from label ~490 retail loads + plays
the first-visit cutscene while the port shows a blank/cyan placeholder.

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
- The bg textures are loaded by scene-init `FUN_0049174e` (the worker-load piece — the
  main new asset path). `ivent_bg_ichiba`.

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

### Planned follow-on traces (user, for context)
- Leaving the guild triggers a tutorial cutscene (the man gives you bread).
- Returning to Recettear triggers a Tear cutscene.
Both are almost certainly more `FUN_004922c0`/event-tick branches (same machinery).
