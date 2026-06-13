# Merchant's Guild scene (engine mode 6 / Market) — RE + port plan

Status: **scene shell + first-visit cutscene + post-cutscene MAIN MENU LANDED**
(`a998fb4` 2026-06-10, `06e9fdf`+`6ea5a3a` 2026-06-11) — the menu (panel/options/HUD/
bubble/cursor) renders pixel-identical to retail at rest (see "Main menu UI" below). The
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
- **CENSUS DONE 2026-06-10 — cutscene is frame-by-frame 1:1 (machine-verified, 3 ways);
  the "db054 verdict BLOCKED" was a MISFRAMING.** The earlier note (that retail "probes
  only rng/rngcalls" and we must "RE the retail db054 + pose addresses, extend the probe
  set to mode 6") was WRONG. Reality, from analysing the captured traces:
  - **The cutscene IS richly probed on BOTH sides** — `dialogue_tick` (retail `FUN_0046c320`
    @0x46c320, the shared iv*.ivt updater) emits `box_open/reveal/line_row/st5_x/y/tx/ty/
    st5_active` on **774 retail frames** (port 895; the +121 is the load-seam tail). No probe
    extension was needed; the dialogue runtime is the same code the prologue uses, already
    in `retail_fields.json`.
  - **db054 is the WRONG clock for a cutscene, on BOTH sides** (not "missing on retail"):
    `house_update` (`FUN_0048670f`, the *only* db054 source) fires **0× in retail / 2× in
    port** over the window — db054 is a HOUSE free-roam bob/sparkle counter that simply does
    not advance during a dialogue cutscene. So `--align-field db054` correctly reports "no
    shared values"; it's a knob mismatch, not a capture gap.
  - **Verdict NOW RUNS via `flow_diff --verdict --align-anchor TEXT_ANIM_START --frame-from
    <first-text-anim-frame>`** (new tooling, 2026-06-10): align by a CONSTANT frame offset
    from a shared dialogue anchor, clip the pre-text fade-in. Over the 714-frame cutscene
    [15115..15828] → **✅ PHASE-CLEAN**: `dialogue_tick.*` ALIGNED bit-exact, `fade_tick.*`
    ALIGNED, `rngcalls` ALIGNED (per-frame consumption matches), raw `rng` 714/714 bit-exact.
  - **Three independent frame-by-frame proofs** (single −14100 offset, anchor-rebased):
    (1) **75/75 cutscene anchors** frame-exact (TEXT_ANIM_START/END ×22, DLG_LINE_SHOW/CLEAR
    ×14, EXTRA_SPRITE start/fade/end); (2) **all 8 dialogue fields × 774 common frames = ZERO
    mismatches** (text reveal + line progression + standee tween bit-identical); (3) **rngcalls
    0 per-frame desyncs** across the cutscene.
  - **`triage` auto-handles it now:** `verdict.py` falls back to `--align-anchor
    TEXT_ANIM_START` when db054 yields no shared values → `merchants-guild` triage verdict
    is **exit 0 / PHASE-CLEAN** (session.json refreshed). The render_quad_add/flush DRIFT
    that polluted the raw verdict was a per-draw-pairing artifact (vcount batching differs
    [0,6]); the verdict now defers >1×/frame draw VAs to `render_diff.py` (fixed for ALL
    scenes — the item-display-2 house verdict showed the same false-positive). Recipe:
    `docs/flow-trace-cheatsheet.md` "Cutscene verdict".
  - **Residue = load-suppression seam only** (phase pillar, accept): `kept_count_mismatch`
    port 1058 / retail 936 + 121 port-only dialogue_tick tail frames [15829..15949]; the port
    loads the guild faster (3 sprite_loads vs retail's heavier init) and renders ~121 early
    cutscene frames during retail's load-SUPPRESSED bracket (the triage's first-divergent
    @ordinal 161 fully-white frame). NOT a port bug. `{phasepin: 282}`+`{rngseed [282,19937]}`
    +`{tutloadpin: 8}` are KEPT (canonical pin). `edit.trace.jsonl.bak-preguild` backs up the
    pre-pin trace.
- **Audio: ✅ ALIGNED 2026-06-10** (`172ecc9`). `audio_diff` flagged 2 missing sounds —
  `se_019_id0150` + `00re_sys09.bin` (pre-cutscene, NOT the guild dialogue, whose voice is
  correctly muted under fast-forward — 0 extra). They were mislabelled "worldmap sounds":
  a retail audio-hook **`ret_va` backtrace** (added to the se_play hooks this session) named
  the caller **`FUN_0048670f`** (the HOUSE/shop free-roam update), not the world-map sim.
  They're the **first-shop-door-exit** SE — asm 0x488a95: gated on the first-exit flag
  (`save[0x2bc5f]==0`), starts the dissolve `FUN_004526f5(0,0x11)`, sets the first-exit flags
  (0x2bc5f/61/62), then plays `00re_sys09.bin` (file, string@0x5cefb8 — Ghidra dropped both
  call args, RE'd via objdump) + `0x150` (id). The port's `player_ctrl_worldmap_exit_arm`
  already had the fade+flags but stubbed the SE (`PORT-DEBT(door-SE)`); un-stubbed (file then
  id, RNG-neutral). `audio_diff` merchants-guild: **missing 2→0, track ALIGNED (9 sounds)**.
  (Separately noted PORT-DEBT(door-exit-reset): the asm also zeroes `DAT_056db000` here —
  untested, world-map render already 1:1; mirror if a later door-exit scenario needs it.)

## Main menu UI (FUN_0049404b) — LANDED 2026-06-11 (`06e9fdf` + `6ea5a3a`)
The post-cutscene guild main menu now renders at parity (`src/scene_guild.c` menu
state + update + render, `src/font_draw.c` `font_draw_text_box`).  Verified on the
merchants-guild trace at label 02191 (resting menu): **panel, the 4 options, gold HUD,
the speech bubble + its body text, the chrname nametag, and the guildmaster are all
pixel-identical to retail** (diff ~99.9% black); the only residuals are the hand-cursor
bob phase (~3px) + the "New" sparkle phase (sub-pixel) — both the load-seam phase pillar
(the menu resumes a few frames apart on port/retail; accept).  Cutscene verdict
unchanged (CONST-OFFSET, audio ALIGNED) — the menu changes have zero sim/RNG effect
during the cutscene (only a static counter + the render gate move).

**Gating (the key structural fact, from the retail call-trace):** `FUN_004922c0`
(menu update) + `FUN_0049404b`/`FUN_00494a73`/`FUN_00406d50` (menu render + HUD) fire
**only on the 2 pre-cutscene entry ticks [14963,14964] and post-cutscene [16167+] —
NEVER during the cutscene [14992..16165]**.  The mode-6 dispatch is skipped while the
first-visit dialogue is busy (the dialogue takes over update+render).  Ported by gating
the whole `scene_guild_sim` + the menu render + cursor + HUD on
`!scene1_intro_dialogue_busy()`, so the counters freeze through the cutscene and the
bubble pops in (+ text reveals) after it ends, frame-for-frame as retail.  bg +
guildmaster still draw through the cutscene (they coincide with retail's cutscene-path
bg + guildmaster standee — confirmed 1:1).

**What FUN_0049404b draws (resting main menu):**
- **Panel frame:** `DAT_073d8748` = `bmp/item_win.tga` (a GLOBAL sysasset, not the
  group-7 set), dst (panel_x=256, −8, 400, 320), src (0,0,400,320), MODULATE.
- **Option list** (gated `DAT_09642c00 != 0`): 4 rows drawn bottom→top, each label
  `(&PTR_PTR_005cfaf0)[type]` at x = panel_x+120 (376), y = idx*0x22 + 64 (+2 if not
  selected); **selected scale 1.0769231, others 0.8615385**; color (alpha<<24)|**0x7f7f7f**
  (grey-127 RGB) under **COLOROP = ADDSIGNED** (engine `SetTextureStageState(0,COLOROP,8)`
  at all.c:95943, reset to MODULATE at 96092; vtable 0xfc = SetTextureStageState,
  0xf4 = SetTexture).
- **"New" badge** on Talk (type 2) when any of the 6 talk-dialogue-seen flags (save bank
  byte `0x2bc98+i`) is 0: scale **0.5** (const@0x51935c), pos (row_x−12, row_y+8), sparkle
  color R=`sin(c38·0.1)·64+191`, G=`·32+159`, B=0x7f (all.c:95997-96005).
- **Speech bubble** (gated `DAT_09642c40 > 0`): `DAT_073a9580` = `bmp/shopmode.tga`,
  **H-mirrored** (`FUN_00404e61`), dst from `ive_box_scale`(FUN_0046c86f) — (160,288,416,176)
  fully open — src (0,176,416,352); the chrname (`bmp/ivent/chrname.tga`) **"Guild Master"
  nametag** cell 0xb (col 1, row 4 → src 128,128,256,160) at dst (308,300,128,32); the body
  text via `font_draw_text_box` @ (250,348), variant by `DAT_09642c4c` (<0x78 "Before you
  stock up…" / ≥0x78 "Time to stock up a bit, eh?").  The continue-arrow (`DAT_09642c44`) is
  init-0 / never set ⇒ never drawn.
- **`FUN_0046b00a(0,0)` is a no-op in the guild** — it early-returns when `DAT_0734b98c==0`
  (the shop item-window slide, always 0 here).  Don't port it for the guild.
- **`FUN_00491de0`** (buy/sell qty-confirm) is gated `DAT_09642c50 > 0` — a no-op on the
  plain menu (PORT-DEBT(guild-menu-nav)).
- **Font scale gotcha** (gotcha-worthy): `font_draw_text_box` (FUN_00465db4) passes the BOX
  scale (param_5 = 1.0), NOT param_5*0.76, to the row drawer — the decompile's *0.76 on the
  scale arg is Ghidra FPU mis-grouping (belongs to the line-spacing y).  The bubble text was
  1.317× (=1/0.76) too small until the extra factor was dropped (recapture-confirmed).

**Open (PORT-DEBT, step 4 + beyond):**
- **PORT-DEBT(guild-menu-nav):** the full interactive state machine (`FUN_004922c0`
  94834+: cursor nav/slide `FUN_00435710`, the Talk submenu `DAT_09642c00==2`, the
  Fusion sub-screen `FUN_00493616`, the store-Expansion cost flow, the buy/sell qty-confirm
  `FUN_00491de0`) — deferred to the buy-flow trace (no menu input on this trace).
- `FUN_004922c0` tail: the daily-event probe (`FUN_0045de68`, event system unported), the
  group-6 follow-on cutscenes.
- The mid-transition bg path (`DAT_09642c3c!=0`, alpha 0xff000000) + the variant-1
  (ichiba, dest 1) texture set + render.
- Then: buy flow (Z buy → Z sword → up qty 2 → Z confirm) → tail (tab-switch + single buys).

### Planned follow-on traces (user, for context)
- Leaving the guild triggers a tutorial cutscene (the man gives you bread).
- Returning to Recettear triggers a Tear cutscene.

**Leave dispatch RE — bread-cutscene half DONE 2026-06-12 (`b5ba796`):** the Leave menu option
dispatches to `LAB_00492ad7` (all.c:95108) inside `FUN_004922c0`'s main-menu A-handler. On a
per-location **first-leave flag** (`(&DAT_0450f3f5)[iVar7] == 0`) it fires
**`FUN_0044ba2c(1,9,0)` = `scene1_intro_dialogue_start_single(1, 9)`** — the bread cutscene is
**dialogue group 1 / script 9 (iv1_9)**; the dialogue machinery is already ported (same path as
the first-visit `(1,3)`), so the port is the Leave-option handler + the first-leave gate, not new
dialogue code. **Ported** (`scene_guild.c` mode-1 A-dispatch, `sel==4||sel==5`): read
`save[GUILD_FIRSTLEAVE_OFF=0x2bc5d]` (=`DAT_0450f3f4`+1), iv1_9 if clear, + SE 0x13d
(LAB_00492821, the back beep — plays in BOTH the bread + transition branches). The overlay
freezes the guild tick (the busy gate at `scene_guild_sim` top) and resumes the menu when it ends.
- **The flag is set by the first PURCHASE, not by Leave** (all.c:95560-95564, mode-8 buy-commit):
  `if (DAT_045114fc[loc]!=2 && DAT_0450fb84[loc]==0) { DAT_0450f3f5=1; DAT_0450f3f9=0; }`. So
  *try-leave before buying ⇒ bread cutscene*; after a purchase ⇒ the flag is set ⇒ the else branch.
  (`DAT_0450fb84` is the 0xb7f2-strided guard, like `DAT_045114fc` — the port treats it
  always-true under slot pinning; the buy-commit flag-set is still `PORT-DEBT(first-buy save
  flags)` at `scene_guild.c` `guild_buy_commit`.)
- **Else branch (flag set ⇒ leave-transition):** bumps `c2c` (if `DAT_0450f42a[loc]==0`) or `c28`,
  which the function TOP (all.c:94833-94876) ramps → at `c2c==3` fires a SECOND cutscene
  **`FUN_0044ba2c(1,0x10,4)` = iv1_16** + sets `c28=3`; the `c28` ramp then `DAT_0438b1c0=8` +
  `FUN_0049de0e()` = the world-map scene swap (with a `FUN_004526f5(0,0x11)` fade @ counter==2).
  This is **`PORT-DEBT(guild-leave-transition)`**, the follow-on gap (the actual world-map exit +
  the return-to-Recettear Tear cutscene).
- Option-type for Leave: the guild fresh-visit menu uses **type 4** (`5.60519e-45`, the port's
  `entries[]=4`); type 5 (`7.00649e-45`) also reaches `LAB_00492ad7`. Port handles both (`sel==4||5`).
- **✅ PHASE-CLEAN (data-1:1) 2026-06-12** on `guild-skip-dialogue-talk-leave` (recapture w/
  `caprange [250,4300]`, calltrace `[2600,4300]`): retail arms the bread `FUN_0044ba2c` @ **frame
  17181** (`ret_va 0x4929ec`, the LAB_00492ad7 call), after first-visit @14695 (`0x492353`) + the
  Talk-topic @15858 (`0x492694`) — the exact narrative order. The bread dialogue box runs retail
  17190→17397 (`dialogue_tick` box_open 0→15 @17301, closes @17390; ~89-frame fast-forwarded box).
  **Focused `flow_diff --verdict --align-anchor TEXT_ANIM_START --frame-from 17150 --frame-to
  17430` (281 common frames) = ✅ PHASE-CLEAN — every field bit-exact**: `fade_tick`
  (phase/counter/mode), `dialogue_tick` (box_open/reveal/line_row/st5_*), `rngcalls`, raw `rng`
  (281/281). Same standard the first-visit cutscene was confirmed to. The studio PIXEL diff is
  unusable here (the wide window crosses 8 load-seams ⇒ kept-count seam port=3864/retail=3740 ⇒
  port runs a few frames ahead of retail at each label; label-paired diff mispairs — verify via the
  anchor-aligned flow_diff, NOT the pixel curve). The whole-window verdict (auto-anchored at the
  first-visit's @14890) shows DRIFT because it spans the POST-bread divergence (the port re-fires
  bread on the post-buy leave since the buy-commit flag-set is PORT-DEBT, while retail does the
  iv1_16 transition @A7 retail[18382]) — focus the range on the bread cutscene to see it clean.
- **Menu-backdrop fix (`aa773d0`, caught by VISUAL not flow_diff):** retail keeps the guild main
  menu (Buy/Sell/Talk/Leave + the "come back any time!" option bubble + hand cursor) rendered
  BEHIND the iv1_9 reminder — it's the try-leave-FROM-the-menu dialogue, so the menu stays up
  (frozen) behind the Tear box (retail label 3010). The port hid it: `scene_guild_render` gated the
  whole menu-UI draw on `!scene1_intro_dialogue_busy()` (correct for the first-visit cutscene, which
  fires at entry_tick==2 before the menu is up, but wrong here). Fixed: draw when `!busy() ||
  (s_menu.mode==1 && entry_tick>0xe)` — the first-visit cutscene (mode 1 but entry_tick==2) and the
  mode-2 Talk-topic dialogues stay hidden (both confirmed 1:1). **Lesson: flow_diff PHASE-CLEAN
  proves the dialogue_tick STATE is 1:1 but says nothing about the surrounding scene's RENDER —
  always also content-match a frame.** The port's per-option bubble text was already correct
  ("Well, come back any time!" for Leave types 4/5, variant B @ text_timer≥0x78, snapped on a dir
  press). User-clarified structure: iv1_9 = the try-leave-no-buy *reminder*; the proper *bread
  cutscene* is iv1_16 on the actual leave-after-buy (PORT-DEBT, the follow-on).
**Sibling at `scene_guild.c:329` — the first-BUY tutorial `FUN_0044ba2c(1,0xf,0)` = iv1_15, gated
owns>9 & flag unset.** The Tear (return-to-Recettear) cutscene is in the shop/house scene, not here.

## BUY FLOW — RE + incremental port plan (2026-06-11)
Trace **`merchants-guild-ui-flow-20260611-052747`** (served :8783), windowed `caprange
[330,2600]` / `phasepin 282` / `rngseed 19937` / `tutloadpin 8` (mirrors the previous guild
trace; free-roam-based because the recapture self-heal rebuilds any non-free-roam window —
the auto-rebase syncs the menu at EXTRA_SPRITE_END). Menu appears ~label 1562; resting menu
1:1 (gt8 ≤ ~1340 = cursor-bob/sparkle phase residue); divergence begins ~label 1630 (first Z
on Buy). Audio diff baseline = **51 missing nav/buy SE** (cursor `se_010_id0146` retail×34,
select `se_007_id0143`, purchase `se_000_id013d`/`se_016_id014d`). Trace actions (menu-seg
frames): f99 A=Buy · f179 A=sword · f257 Up f322 Down f376 Right f403 Left (qty wiggle) ·
f475 A=buy1 · f527 A f569 Up f634 A=buy2.

### `FUN_004922c0` mode map (`DAT_09642c00`)
- **1 = main menu** (resting + cursor nav). 94811-94833 = resting counters (PORTED:
  `scene_guild_sim`). 95058-95170 = cursor nav + A-dispatch (UNPORTED).
- **0 = Buy/Sell item list** (the shared item-window grid). 95242-95389 handler. A on item →
  mode 8 (buy: 95368-95377) or sell-confirm.
- **2 = Talk submenu** (94885-95030) — **PORTED 2026-06-11 (`88b666a`), studio-verified 1:1**
  (residue = cursor-bob/New-sparkle phase, accept). **7 rows = 6 topics + "Never mind"**
  (strings @ exe `0x5cfb0c`: "What is the guild?"/"What can I do here?"/"About merchant
  levels"/"About the town"/"About unknown items"/"About fusion"/"Never mind"); no-wrap U/D nav
  over `c10` 0..6 (the render draws 6 visible rows, scroll `c0c` ∈ {0,1}). Entry: mode-1 A on
  Talk sets `c1c=1`, ramps to 0xf → mode 2, slides to `c1c==0x19`. A on a topic → `c20=1`
  confirm-slide; at `c20==0x10` fires `FUN_0044ba2c(1,script,0)` (script = {0a,0b,0c,0e,18,19}
  by `c10`) + sets `save[0x2bc98+c10]=1` (clears that topic's New badge). B / A-on-"Never mind"
  (`c10==6`) → `c14=1` close (c1c slides back <0x10 → mode 1). Render: main list c1c slide/cull
  (selected Talk row slides up-left to head the submenu, others cull at c1c>12); submenu rows
  `rx=panel_x+280−(c1c−0xf)·0x10`, alpha `(c1c−0xf)·0x34`; per-topic New sparkle (topics 0..5);
  up/down scroll arrow (item_win.tga src (448,896,512,944)/(512,896,576,944)). param_3 (0 talk /
  1 first-visit) only gates BGM fades the port omits. **PORT-DEBT(talk-confirm-flash):** the c20
  selected-row brightness pulse (Ghidra-dropped FPU amplitude). Verify trace:
  `guild-skip-dialogue-talk-leave-20260611-204101` `caprange [250,1250]`.
- **3** = sell item-pick (FUN_00469a9f adds gold). **4** = Expansion confirm. **5** = Fusion
  confirm. **6** = post-purchase result anim (`DAT_09642bfc` 0→0x4b). **8 = qty overlay**.
- Transitions: mode1 --A on Buy/Sell--> slide-in (`c24=1,c20=1`; at `c20==0xf`
  `FUN_004682c5`+`FUN_00468338(7=buy/5=sell,1)`+`FUN_004682d8(price-mult)`; at `c24==0x19`)
  --> mode0 --A on item--> `c00=8,c50=1` mode8 --FUN_00491bc0 confirm--> purchase loop
  (`FUN_00468d22`×qty, gold-=qty*price, SE 0x14d) --> mode0.

### Input encoding (CRITICAL gotcha)
`_DAT_073dddd4` is the **32-bit overlap** of `pressed`(low16)|`held`(high16) — `_` warning at
all.c:94729. In the port (`g_sim_buttons[0]`): **actions** A/B/C = `pressed & 0x10/0x20/0x40`
(edge); **directions** R/L/U/D = `held & 0x01/0x02/0x04/0x08` (auto-repeat), i.e.
`_DAT_073dddd4 & 0x10000/0x20000/0x40000/0x80000`. Matches worldmap (dirs←held) + display_menu
(actions←pressed). Option types in `DAT_09640624[c04]` are **denormal-float-encoded ints**
(`type*1.401e-45`; read `*(int*)` = type 0 Buy…6 Expansion) — Ghidra renders the int compares
as float bit-patterns.

### Key globals
Guild menu state (`scene_guild.c` s_menu): `c00`=mode `c04`=main-cursor `c08`=scroll
`c10`=item/talk-cursor `c0c`=item-scroll `c1c`=talk-slide `c20/c24`=submenu slide-in/out
`c30/c34`=fusion-grid cursor/scroll `c50`=qty-overlay-open `c54`=anim-price `c58`=item-id
`c5c`=qty `c60`=unit-price `c64/c68`=qty arrow bob `c2c/c28`=leave/transition anim
`bfc`=result anim. `DAT_005cfae4`=max-qty. `DAT_09640624`=option-type array,
`DAT_005cfab4`=option count. Shared item-window state = `DAT_0734b9xx` (scene1_display_menu's
s_tab_*, s_list).

### Call graph + ported status (verified vs src/ 2026-06-11)
PORTED & reusable: cursor `FUN_00435710/693/61a/612` (title_save_dialog), input gate
`FUN_00434d6a`, item-window `FUN_00468338`/`FUN_00469414`/`FUN_00469a9f`/`FUN_00469abb`/
`FUN_00468d22`/`FUN_004681f6` (scene1_display_menu.c / tables_item.c — but the item-window is
the **shop-display removal subset**; `PORT-DEBT(A3)` defers the inventory-scan POPULATION + the
in-list cursor NAV — exactly what guild buy needs). UNPORTED gaps: **`FUN_00491bc0`** (544B,
qty-overlay input — the bottleneck), `FUN_00491de0` (render — referenced as no-op stub in
scene_guild.c), `FUN_00469a83` (max-buyable 28B), `FUN_00491b16` (owned count 41B),
`FUN_00469a00` (post-add 131B), `FUN_004682d8` (price-mult 11B), `FUN_004682c5` (slide-activate
11B).

### `FUN_00491de0` (qty overlay RENDER, read in full)
Gated `c50>0`. Draws: confirm box (item_win.tga `DAT_073d8748` / `DAT_073d8dc0`) with a
`c50/4` open-scale; "Buying %s, Are you sure?" (DAT_09642c58 item name); qty (`_DAT_09642c54`
anim toward `c60*c5c`) + price; up/down arrows (`c64/c68` bob, drawn only if `c5c<c5cmax` /
`c5c>1`); flash anim `DAT_096405fc/f8`. Logic (qty adjust/confirm) is `FUN_00491bc0(0)` from
mode-8 at all.c:95536 → ret 1=confirm (purchase), 2=cancel (→mode0).

### Incremental port plan (recapture-verify each; `--only port` loop on :8783)
1. **Main-menu cursor nav (mode 1, 95074-95084) — ✅ DONE `c4075dd`:** cursor U/D
   (`c04=(±1+c04)%count`, SE 0x146, slide `FUN_00435710`). **COUPLING FINDING:** the A-press
   Buy/Sell dispatch (`c24=1,c20=1` slide-in) slides the main panel OUT, and the render
   blanks the menu without the item window sliding IN (measured parity dip 381k→429k gt8) — so
   the A-dispatch is NOT separable; it moves into step 2. Step 1 shipped JUST the cursor nav
   (no-op on this trace = non-regressive; the trace holds no main-menu direction).
2. **A-dispatch + Buy slide-in + item-window POPULATION + RENDER (mode 1→0) — THE milestone
   ✅ DONE 2026-06-11.** Landed together (the coupling): the mode-1 A-press (95086-95105:
   Buy/Sell→`c24=1,c20=1`, reset `c10/c0c`, SE 0x143) + the slide-in ramp (95132-95167, at 0xf
   `display_menu_open(7/5,1)`+price-mult, at 0x19 →mode0) in `scene_guild_sim`; the **mode-7
   guild-stock population** in `display_menu_open` (port of `FUN_0049196f` — scan the item DB,
   not inventory: valid·price>0·gi-byte>0·two id-window gates·`k_guild_stock_tier[gi]≤store_lvl`;
   tables `DAT_005cfabc`/`DAT_005c6ef0` extracted from the unpacked .data; mode-aware tabs = NO
   `-1` "Nothing" entry for shop modes; per-item qty-cap → the "N Left" number); the buy-row
   render `"%s - %d Left"` (cap∈(0,100), else just the name); the "Purchase Price-"/"Sell Price-"
   description label (scene-6 mode); `display_menu_render` wired into `scene_guild_render`.
   **VERIFIED 1:1 at the fresh open** (port frozen open vs retail's pre-overlay frame): items
   (Worn Sword/Longsword), order, icons, caps (3 Left/1 Left), description, price (140),
   possessed (0) all match; only the price LABEL needed fixing (was "Base Price-"). The
   port FREEZES at the fresh open (no item-nav/qty yet = steps 3-4), so it correctly never
   shows the post-buy states. **PORT-DEBT(price-trend `FUN_004361b2`):** the buy price's
   daily-market trend factor + the `Out Of Stock`/`Not For Sale`/`Adventurer's Possession`
   status texts (cap-0/special, all post-buy) are deferred. Talk(2)/Leave(4,5)/Expansion(6)/
   Fusion(3) dispatches stay PORT-DEBT. Strings: `%s - %d Left` @0x5c785c, `Out Of Stock`
   @0x5c78a0, `Not For Sale` @0x5c7890, `Adventurer's Possession` @0x5c78b0.
3. **Item-list nav (mode 0) — ✅ DONE 2026-06-11 (`45f5bca`).** The in-list cursor nav was
   already in `display_menu_update` (the header's PORT-DEBT(A3) note was stale); the new
   `scene_guild_sim` mode-0 block dispatches its return: **r==3** (A-edge) → `guild_buy_price_preview`
   (unit price = base·mult, SE 0x143/0x16a), **r==1** (6-frame countdown done) →
   `guild_buy_open_qty_overlay`, **r==2** (B) → mode 1 + the `c18` panel slide-OUT (`FUN_004682d0`).
   Helpers exposed from `scene1_display_menu`: `display_menu_stock_cap` (FUN_00469a83),
   `display_menu_owned_count` (FUN_00491b16), `display_menu_cursor_to_row` (FUN_0046939a).
4. **Qty overlay (mode 8) — ✅ DONE 2026-06-11 (`45f5bca`).** `guild_qty_overlay_input` =
   `FUN_00491bc0(0)`: U/D qty ±1 (held, capped at `max_qty`/floored at 1, error SE 0x16a),
   L/R Yes↔No toggle (pressed edge, cursor ease to 340+yn·96,252), A confirm (Yes→buy flash) /
   B cancel, the `c50` slide-in (1→4) + flash-out, `_c54` price-anim + `c64/c68` arrow bob.
   `guild_buy_open_qty_overlay` caps qty = min(gold/price, 99, stock); `guild_buy_commit` =
   the mode-8 tail (FUN_00468d22×qty + FUN_00469a00 + gold deduct + SE 0x14d). Render
   `scene_guild_qty_overlay_render` = `FUN_00491de0` (savewindow.tga box grow-by-`slide/4` +
   centred title + right-aligned qty "%2d" in the title gap + "Stock Price…%spix" total +
   Yes/No + up/down arrows, all COLOROP=ADDSIGNED grey-127). **Tutorial infinite money**
   (`FUN_004922c0`:94756): gold force-pinned to 10,000,000 while the restricted flag
   (`DAT_0450f3e1`/0x2bc49) is set → qty cap is stock, not affordability; HUD gold never drops.
   Helper `font_measure_text` (`FUN_0047d0ea`) for the qty-number placement.
5. **Verify** full buy flow: ✅ recapture executes A=Buy→select→qty-wiggle→buy1(q1)→buy2(q2),
   2 purchases, stock 3→0; **audio_diff 51→14 missing**; ✅ **USER-CONFIRMED 1:1 2026-06-11**
   ("the panel looks correct other than the slight phase desync that was already there").
   **3 user-flagged polish gaps then ✅ FIXED + verified (`922b5be`):**
   - **Green qty outline + PULSE:** the "%2d" uses a green diffuse `(0x8f-iVar1)<<16 |
     (0xce-iVar1)<<8 | (0x8f-iVar1)` where **`iVar1 = (int)(sin(c54·0.1)·-16.0)`** — the -16.0
     amplitude (const @0x519818) and the 0.1 freq (@0x5193a0) are the FPU multiply Ghidra dropped
     from the decompile (verified in asm 0x492008-0x49207f: `fild c54; fmul 0.1; call sin; fmul
     -16.0; call __ftol`).  So the green THROBS ±16/channel (~±6%, ~63-frame period); under
     COLOROP=ADDSIGNED the body stays white + the anti-aliased edge pulses green.  First shipped
     as the flat midpoint (iVar1=0 → 0x8fce8f, no pulse — the user caught it: "the quantity
     actually seems to pulsate"); the faithful sine now tracks retail's green-mean within ±1
     across the cycle (trough~130 @label1761, peak~145 @1791), phase-aligned because c54
     (`price_anim`) resets to 0 on overlay open + ticks each frame on both sides.
   - **Full-width-space (SJIS 81 40) advance:** the price line collapsed (blank-glyph upload
     `effective_width`=0 → negative advance, "140" overlapping "Price"). `font_alloc` now pins the
     full-width space to 0x0d (the engine pins only ' '→0x18 @FUN_0047cbcb:94732; retail's
     full-width width comes from the glyph cell, the port's blank atlas cell measures 0), and
     `font_upload` no longer clobbers an alloc pin with a 0 measure. Port "140pix"@x230 vs retail
     x228 (label 1750). **GLOBAL but SAFE:** cutscene bit-identical (diff 0px>8 @labels 900/1100)
     — the iv1_3 dialogue uses no full-width spaces.
   - **Gold rolling-counter** (`FUN_00406584` @all.c:4849, ported as `scene1_top_hud_money_tick`):
     the HUD money eases toward bank gold by `rand()%max(|Δ|/25,10)+|Δ|/100` per frame (one
     rng_next15/rolling frame, no-op at rest), wired pre-sim into `scene_guild_sim`. The port
     deducted bank gold but never rolled the displayed mirror. Gold rolls 1000→860→580 across
     buy1(140)/buy2(280) **matching retail at the same labels** (the roll RNG is in sync). The
     restricted-stock flag (`DAT_0450f3e1`/0x2bc49) is **0** for this save — proven by the qty
     caps "3 Left"/"1 Left" (the restricted path returns cap 100 = no "N Left") — so the tutorial
     gold-pin (FUN_004922c0:94756, gold→10M) stays inert and gold decreases normally, as retail.
   **Residual (refinement):** 14 missing SE (7 nav 0146 = qty auto-repeat cadence, 3 select 0143,
   3 cancel 013d, 1 buy 014d — several land in retail-only post-flow regions) + cursor bob phase
   (accept, the pre-existing load-seam residual).
Defer (later traces): Sell (mode 3), Fusion (mode 5 / `FUN_00493616`), Expansion (mode 4),
Talk submenu (mode 2 = window 3), tab-switch (window 2).
Both are almost certainly more `FUN_004922c0`/event-tick branches (same machinery).

## Render-program drill (v3 native trace studio, 2026-06-13)

Driving `guild-ui-flow` through the v3 viewer's **draw-program panel** (which flags
when PIXELS are 1:1 but the RENDER PROGRAM differs — invisible to v2's pixel-only
diff) surfaced two conversation render-program divergences. Probed with
`tools/trace_studio_v3/orv3_draws.py` (per-draw enumeration + cross-side
content-keyed diff) over the cached `guild-ui-flow-ffc2d568` window.

**(1) The guild bg + guildmaster were DOUBLE-DRAWN during every conversation —
✅ FIXED 2026-06-13 (`2a2d84d`).** On the **1076** conversation frames (Talk topics /
first-visit cutscene) the port drew bg_guild (tex `2780`, 1024×512) **twice** — once
from `scene_guild_render`'s slot0, and again from the conversation renderer's own bg
pass (`draw_background`, port of `FUN_0046c9a2`) — plus the guildmaster keeper (slot1,
dst(-64,32,448,448)), all fully overdrawn by the conversation's opaque bg. **Retail
draws bg_guild exactly ONCE** per frame (a full-window scan: retail bg-draws/frame =
`{0:114, 1:2486}`, never 2): its render root **`FUN_004547ab` skips the WHOLE mode-6
scene block** (`FUN_00490e35 → FUN_00494a73` = guild bg + keeper + menu + HUD) when a
full-screen-bg conversation covers the screen — the gate is
`DAT_0438b1c8 != 0 && FUN_0046c869() != 0`, and `FUN_0046c869` returns `DAT_073a3df0`
(= the active script's `bgset:`/n_bg count). So during a full conversation retail
renders ONLY the conversation — its own bg + the **guildmaster AS A STANDEE** (a
separate conversation draw at a *different* dst, NOT the keeper slot) + the box. The
port's `scene_guild_render` already gated the *menu* on the dialogue state but ALWAYS
drew bg + keeper (a deliberate "they coincide with retail's cutscene bg + standee"
note the draw-program trace disproves — the keeper is drawn, fully covered, at a
different position from retail's standee).

**Fix:** gate the bg + keeper on `!scene1_intro_dialogue_covers_screen()` — the port's
*existing* port of that exact `FUN_0046c869` gate (`active() && n_bg>0`, already used
for the INGAME HUD at `main.c:2925`), the same way the menu below is gated. An OVERLAY
dialogue with no bg (the iv1_9 try-leave reminder, n_bg=0) leaves it false, so the bg +
keeper stay the menu backdrop (preserves the confirmed `aa773d0` iv1_9 fix). **Pixel-safe
by construction** (the suppressed draws were fully overdrawn): bg draws/frame went
`{0:106, 1:567, 2:1076}` → `{0:106, 1:1722}` (**zero** double-draws), and the post-fix
port is **pixel-bit-identical to the pre-fix port at all 1749 shared identities** (the
fix changed ZERO pixels — verified by a per-frame fnv64 pixel-hash join, `v3refs.txt`
keyed by `(anchor,offset)` identity via `v3cache.load_meta`). A conversation frame's
cross-side draw diff goes from "matched 6, **port-only 2**, retail-only 1" → "matched 5,
**port-only 0**, retail-only 1".

**(2) Retail lays a SCREEN-BLACKOUT layer first in every conversation (tex `9fd8`) —
SOURCE NAILED 2026-06-13, port deferred to the loading-screen-fidelity pass.** Retail's
conversation render's **FIRST** draw on all 1076 conversation frames is a full-screen
**opaque-black** quad (`0xff000000`, SRCBLEND=SRCALPHA/DESTBLEND=INVSRCALPHA, ZENABLE off),
immediately covered by the opaque bg (net pixel effect 0). **Exact source = `FUN_00453d9c`**
(@0x453d9c): it runs **UNCONDITIONALLY in the render root `FUN_004547ab`**, AFTER the
per-mode scene block and BEFORE the dialogue (`FUN_0046c090`) — so on a conversation frame
(scene block skipped, see (1)) it is the FIRST draw, and on a menu-rest frame it lands after
the scene's bg (still covered). When `DAT_0438bf74 != 0` it blits **`DAT_073aa188` =
`bmp/system.bmp`** (the 128×128 "9fd8" system/fade atlas, sampling a solid 6×6 cell at
src(9,1)-(15,7)) to dst(0,0,640,480) at `0xff000000`. (NOT the `polybg` block / `FUN_00455191`
— that's a 3D sprite-record emit; the earlier guess was wrong. NOT `FUN_00494a73`, whose
mid-transition blit uses the 1024×512 bg texture.) **The gate `DAT_0438bf74` is armed by
`FUN_00452809()`** (a one-line setter) + cleared at render-root points (all.c:49615/50517/
50633) — it is the screen-blackout/fade flag of the transition/load system. **Port status:**
the port ALREADY loads system.bmp (`g_sysassets.system_bmp` / sysassets.h), but `FUN_00453d9c`
+ the `DAT_0438bf74` gate (+ `FUN_00452809`'s call chain) are UNPORTED. Since it is a
fade/transition blackout (invisible whenever the scene/bg is opaque + full-screen, the only
cases traced), **wire it in the LOADING-SCREEN-FIDELITY pass** (the FRONT's deferred user
direction — replicate retail's fades/load screens), where the transition system that arms
`DAT_0438bf74` is RE'd holistically; porting the draw without that gate risks a black flash.
Logged as engine-quirk §122.

**(Separately — HOUSE, NOT this fix's class.)** The `house-loaded-display-pinned` HOUSE
free-roam frame shows a much larger **3D** render-program divergence (port **98** vs
retail **125** draws; 56 port-only + 83 retail-only after content-matching, 42 matched)
— heavy `DrawIndexedPrimitive` batching differences in the 3D scene, NOT the guild's
clean single-layer 2D case. A separate, larger follow-up (the FRONT's "26 batching
splits + 1 extra `ea99` draw" finding); the `ea99` draw is an 80-tri src-alpha-0 first
draw, distinct from the guild's 2-tri black base.
