# Shop "items on display" renderer — PORTED & 1:1

**Date:** 2026-06-04 · **Status:** ✅ **LANDED & human-verified 1:1 vs retail.**
The merchandise on shop displays now renders (Worn Swords on the back stand of
the user's loaded save). The user confirmed it is **1:1 with retail; the only
residual is the item sparkle/glint effect** (deferred — see "Remaining" below).

## What landed
- `FUN_00415fab` ported as `wf_render_display_item` (per-cell icon billboard) in
  `scene1_wide_followup.c`; args recovered via objdump @0x416a02 (col, row, item,
  z=0). Resolver `tables_item_find_slot_by_id(item>>6)` → record `.category`
  (+0x38) + `.subindex` (+0x3c); texture `g_sysassets.item_icons[cat]`. UV +
  world helpers are host-tested in `scene1_wide_followup_helpers.c`.
- The `FUN_004161c7` mid-block-2 grid walk un-gated: 15×20 cells from the working
  arena (`save_work_dwords_at(slot) + SAVE_BANK_FIELD_DISPLAY_GRID` = dword
  0x4e26), gated on `g_scene_state == 1` (free-roam).
- **Key fix:** the per-item quad left-multiplies engine `DAT_0438cdf8` (the
  camera-facing billboard matrix). The walker had been using an identity
  pre-matrix stand-in, so items first rendered edge-on (thin sliver). Wiring
  `wf_pass_c_set_pre_matrix(g_scene1_camera_orient)` at the walker entry (the
  port already computes that matrix via `scene1_camera_angle_compute`) turned the
  swords to face the camera → 1:1.

## Item sparkle/glint — EMITTER CRACKED (2026-06-04, live call-graph)

The sparkle emitter was found by a Frida **call-trace on the overlay spawn**
(hook `0x414345` + its wrappers, read the caller `ret_va` at a loaded-shop
free-roam frame — `runs/sr-sparkle`, `tests/traces/save-roundtrip/sr_sparkle.jsonl`,
`/tmp/sparkle_vas.json`). Three prior **static** guesses all misfired
(`FUN_0042353c`/`FUN_0042439e` = a scripted event sequence, NOT this), so the
live ret_va was decisive — same technique that found the renderer.

- **Call chain:** spawn `FUN_00414345` ← wrapper `FUN_004147d5` ← **emitter at
  `0x4867ee`, inside `FUN_0048670f`** (the big HOUSE free-roam update / Cpop
  caller). 81 spawn calls steady across the window, every 8th frame.
- **The emitter** (top of `FUN_0048670f`, all.c:86579-86598; asm 0x486737-0x4867fa):
  ```c
  if (DAT_0438b8cc % 8 == 3) {                 // fires once every 8 frames
      for (piVar16 = &DAT_005ce3c4;            // 7 candidate back-row COLUMNS
           piVar16 != &DAT_005ce3e0; piVar16++) {  // = {1,2,3,4,11,12,13}
          int f = FUN_004860c8(*piVar16, 0);   // furniture index at cell (col,0)
          if (DAT_0450fee8[f] == 0 &&          // bank dword 0xb1d4 — furniture gate
              DAT_044f7030[*piVar16] != -1) {  // working-bank dword 0x4e26 (display grid) occupied
              FUN_004147d5(&DAT_056da1b8,
                  2*col - 9 + (rng-0.5),        // x  (col 1→-7, 2→-5, 4→-1 = the 3 swords' X)
                  rng + 2.0,                    // y  (just above the item)
                  (rng-0.5) - 7.0,              // z  (≈ -7, the back stand)
                  0x3b, 0x3e99999a /*scale 0.3*/, 0xffffffff /*white*/);
          }
      }
  }
  ```
  Three `FUN_00471089()` (RNG 0..1) per occupied cell. `&DAT_056da1b8` = owner.
- **Template `0x3b` (59) = `目玉商品`** ("featured display item") in `ef/effect1.dat`
  record 59 — DECISIVE confirmation. Numeric fields (record byte 0x64, 18 dwords):
  texture_type **19**, type_shape 0, spawn_count 1, scale_base_mul 1.0,
  tpl5 0.99, age_stagger 1.0, **fade_out_def 24** (→ lifetime 24 frames; spawned
  every 8 ⇒ ~3 alive per item = the doc's "3 per item"), scale_x 1.0,
  scale_y_ratio/blend_off/blend_mix 0.5, fade_in 8, fade_out 16, layer_pair 0x100.
- `FUN_004860c8(col,row)` (all.c:86191) maps a grid cell → the furniture index
  whose origin (per-furniture grid origins `DAT_045105a8/ac` = working-bank dword
  0xb384) covers it; returns -1 if none. Gates so only cells on a real display
  stand sparkle.

### Port pipeline gap (what blocks rendering today)
The overlay particle SYSTEM is ported (`scene1_overlay_spawn`=FUN_00414345,
`scene1_overlay_render`=FUN_00414ee2) but its **template table
`g_scene1_overlay_templates` is empty** (memset-0; nothing populates it). The
real loader is engine **`FUN_00412a89`** (all.c:11262): default-inits the table,
then reads `ef/effect{1..4}.dat` copying each file's **first 0x4330 bytes** (100
records × 0xac) into the overlay template table `DAT_00733820` (`DAT_00733884` =
base+0x64 = the numeric fields) and the next 38000 into the parent table. The
port's **PFO.7** (`scene1_pfo_parent_table_load_all`) ported ONLY the 38000-byte
parent chunk — the **0x4330 overlay-template chunk is unported**. Shapes + layer
textures load via **O.10** (`FUN_00474f4f`, `scene1_overlay_table_load_all`) +
sysassets. So the work is: (1) load `effect1.dat` bytes 0..0x4330 →
`g_scene1_overlay_templates` (port stride 43 dw; field k = file byte rec*0xac +
0x64 + k*4), (2) port the emitter into the `FUN_0048670f` prologue (currently a
no-op in `scene1_player_ctrl.c`), porting `FUN_004860c8` too. RNG: 3 LCG draws
per occupied cell every 8 frames — must stay phase-aligned (`phase_probe`).

The `.idx`/`effect_set.idx` text files are the effect-EDITOR's export (writers
`FUN_00411ac7`/`FUN_00411e3f`/`FUN_00412175`), NOT runtime loaders — `ef/` has
only the binary `.dat`. Ignore them for the port.

### Older d3d-trace scoping (superseded by the call-graph above, kept for record)
- **Item sparkle/glint** — the yellow 4-point twinkle stars retail draws over
  each display item (the only residual the user flagged after confirming the
  swords 1:1). **Scoped via the retail d3d-trace** (`runs/sr-retail/
  d3d_trace.jsonl`, free-roam frame 817, 3 swords on display):
  - The sparkles are **2D-overlay quads** drawn by `FUN_00414ee2` (the 2D overlay
    dispatcher) at ret_va `0x415e61` — **9 draws = 3 per item** — each a
    TRIANGLESTRIP quad (FVF 0x142, stride 24) with `SetTransform(WORLD)` at
    ret_va `0x4159ca` and one shared star texture (handle `0x172fcf30`).
    NOT Pass C and NOT `FUN_00415fab` (those are the item billboards at
    `0x4161c3`).
  - **Positions are 3D WORLD, not screen-space** — `SetTransform(WORLD)` per
    draw. Verified against the item draws in the same frame: the 9 sparkle
    transforms cluster into 3 groups of 3, each at one item (item X = -7/-5/-1,
    Z=-6.5, Y=1.9; sparkles X≈item±0.4, Y≈2.0-2.7 just above, Z≈-6.5..-7.4 with a
    small per-spark scatter + Y jitter = the twinkle animation).
  - **The port already has BOTH halves of this system:** the renderer
    `FUN_00414ee2` (`scene1_overlay_render`) AND the spawn API `FUN_00414345`
    (`scene1_overlay_spawn`, template-driven). It's dormant in HOUSE only because
    nothing spawns the sparkle records.
  - **NOT the dust/wing particle system** (checked + ruled out 2026-06-04): dust
    (records-A type 0xe, draw 0x41e97b) and wing-glow (records-A type 0x1f, draw
    0x41e165) are arms of `FUN_004176ff`. The sparkles are the `FUN_00414345`/
    `FUN_00414ee2` overlay-template system instead.
  - **So the one missing piece is the EMITTER + the sparkle template:** find the
    HOUSE free-roam tick code that loops occupied display cells and calls the
    overlay spawn (`FUN_00414345`) ~3× per item with the twinkle template (star
    texture `0x172fcf30`, randomized scatter), then port it and wire it into the
    HOUSE tick — `scene1_overlay_render` already draws whatever it spawns.
    (`FUN_0045ed12`, a grid+resolver predicate for special item-ids
    `4000<id<0xfa7 || ==0xfab || ==0xfb0 || 0xfb8<id<0xfc3`, is NOT it.)
- `FUN_00485f8c` (display-management overlay, editing mode `DAT_0438cc08==2`).

---
*Original mapping notes (kept for the record):*

**Status:** ✅ MAPPED via d3d-trace caller analysis on retail.
(Earlier this doc said UNMAPPED + killed two wrong leads;
those corrections still stand — see the bottom.)

## How it was found
Booted retail with the user's save (the save-roundtrip trace's load prefix,
`trace-to-picker`/`retail_load`), reached HOUSE free-roam with 3 swords on
display, and ran `frida_capture.py --d3d-trace`. The d3d-trace records `ret_va`
(caller) per draw. Grouping `DrawPrimitiveUP` by caller at the captured
free-roam frame showed **`0x004161c3 ×3`** — exactly the 3 swords. That return
address is the tail `DrawPrimitiveUP` of `FUN_00415fab`. (Method: the per-draw
caller VA is the reliable way to find an unknown renderer — far better than the
curated call-trace VA list or static guessing.)

## The subsystem (3 functions + 1 grid + the item table)

### `FUN_00415fab` @ 0x415fab (540 B) — per-item billboard renderer
`FUN_00415fab(int gx, int gy, uint item, float z)`:
- World pos from the **display-grid cell**: `x = gx*2 - 9`, `z = gy*2 - 6.5`,
  `y = z_param + 1.9` (`thunk_FUN_004a3462` builds the transform).
- `iVar3 = FUN_004681f6(item >> 6)` — resolve the item record index.
- **Texture**: `DAT_073d8778[ DAT_095d3808[iVar3*0xb3] * 0x10 ]` → `SetTexture`
  (cached against `DAT_0076b95c`).
- **Icon UV**: icon index `DAT_095d380c[iVar3*0xb3]`; 32×32 cell in an 8-wide
  atlas → the 4 UVs at `DAT_0064e5d8..`. (`item & 0x10` forces icon 0.)
- `SetTransform(WORLD)` + `DrawPrimitiveUP(TRIANGLESTRIP, 2, &DAT_0064e5d8, 0x18)`
  — one textured quad. (`0x18` stride = pos+diffuse+uv vertex.)

### `FUN_004161c7` @ 0x4161c7 (4925 B) — the HOUSE free-roam shop render (DRIVER)
Called from the scene render (51179 / 54533 / 92170). The ambient-merchandise
block (all.c ~L13622):
```c
if (DAT_0438b1c0 == 1 && *DAT_068dd2f0 == 0) {        // free-roam && stage-palette gate
    piVar11 = &DAT_044f7030 + iVar10;                  // display grid base (see below)
    for (row = 0; row != 0xf; row++)                   // 15 rows
        for (col = 0; col != 0x14; col++, piVar11++)   // 20 cols  (= 300 cells)
            if (*piVar11 != -1) FUN_00415fab(...);      // draw each occupied cell
    FUN_00485f8c();                                     // + interaction-mode overlay
}
```

### `FUN_00485f8c` @ 0x485f8c (316 B) — display-management overlay (secondary)
Draws the items on the ONE stand the player is editing (gated `DAT_0438cc08==2`,
furniture index `DAT_0438bea4`, item-id row `DAT_074b28d8`, furniture type 3 =
2×2 grid / 4 = 1×4 row). Grid origin per furniture in the working bank (next).
Not needed for the ambient free-roam render; port after the main grid.

## The data (in the working arena — already built, `save_work.c`)
- **Display grid `DAT_044f7030` = working-bank dword `0x4e26`** — a 15×20 = 300
  cell grid, one **item ID per cell** (`-1` = empty). This is exactly the
  `save_bank.h` entry "`[0x4e26..+299] 0xFFFFFFFF × 300`" (was unlabeled). THIS
  is the shop display state — **NOT** 0x9e76 (that's the ranking summary).
  Indexed `&DAT_044f7030 + DAT_0438b1e0*0x2dfc8` (active working slot).
- **Per-furniture grid origins `DAT_045105a8`/`DAT_045105ac` = working-bank dword
  `0xb384`** (x/y), stride 8 bytes, indexed by furniture index `DAT_0438bea4`.
  (Used by the FUN_00485f8c overlay; the main grid uses cell col/row directly.)
- **Item table `DAT_095d3808`** (stride `0xb3` dwords = 179, 100 entries) — per
  item: `[0]` → texture-handle index into `DAT_073d8778`; `DAT_095d380c[i*0xb3]`
  → 32×32 icon index. Loaded by the gameplay-table loader.

## The port hook ALREADY EXISTS
`src/scene1_wide_followup.c` is the port of `FUN_004161c7` (C8f.1). It already
documents the merchandise block (L325-360) — the `DAT_044f7030` grid loop +
"Both FUN_00415fab and FUN_00485f8c are unported … the outer gate keeps the loop
[skipped] until DAT_044f7030 ports." That data now ports (working arena, dword
0x4e26, loaded by `save_work_load_slot`). So the chip is: port `FUN_00415fab`,
read the grid from `save_work`, and un-gate the existing loop. The driver
plumbing is done.

## Port plan (task D)
1. Expose the working-bank display grid (dword 0x4e26, 300 cells) — trivial
   accessor on `save_work` (the array is already loaded by `save_work_load_slot`).
2. Port `FUN_00415fab` (per-item quad: world pos from cell, item texture +
   32×32 icon UV, one DrawPrimitiveUP). Needs the item-table texture/icon lookup
   (`DAT_095d3808`/`DAT_073d8778`/`DAT_095d380c`) — check what's already loaded
   by the tables loader.
3. Port the `FUN_004161c7` ambient block: the 15×20 grid loop calling the
   renderer per occupied cell, gated on free-roam. Wire into the HOUSE render
   chain (`scene1_render`) in the right draw-order slot.
4. Verify vs the retail capture: `runs/sr-retail/` (frame 922 = 3 swords on the
   back table; `d3d_trace.jsonl` has the exact per-item draws @ ret_va 0x4161c3).
5. Defer `FUN_00485f8c` (display-management overlay) until the editing UI lands.

## Verification artifacts
- `runs/sr-retail/frames/frame_00922.png` — retail loaded shop, 3 swords visible.
- `runs/sr-retail/d3d_trace.jsonl` — the draws; `DrawPrimitiveUP @ ret_va
  0x4161c3 ×3` = the merchandise.
- Port comparison (current gap): `runs/sr-load/` — same load on the port, back
  table EMPTY (renderer unported).

## The two corrected wrong leads (kept for the record)
- `FUN_00456f56` is the dormant CHARACTER-sprite walker (`DAT_056dacc0` actor
  array), NOT item display.
- Bank dword `0x9e76` (100×18) is the per-bank RANKING summary (`FUN_0049f012`),
  NOT the shop display. (The display is `0x4e26`, above.)

## Prerequisites — already present in the port (scoped 2026-06-04)
- `FUN_004681f6` (item-id → record slot): **ported** (`tables_item.c`).
- Per-category item icon textures: **loaded** (`sysassets.item_icons[33]`).
- 32×32 tile-UV math + the resolver pattern (`DAT_095d3808`→`DAT_073d8778`
  texture bank, `DAT_095d380c` icon index): scaffolded in
  `scene1_wide_followup_helpers.c` (the wide-followup's own "Pass D" resolver —
  reusable shape, though a separate pass from the 0x4e26 display grid).
- Display grid data (working-bank 0x4e26): **loaded** by `save_work_load_slot`.
So the remaining work is just `FUN_00415fab` (the quad build + its specific
texture/icon lookup) + un-gating the `scene1_wide_followup.c` grid loop to read
`save_work`. A clean, self-contained chip for a fresh session.

---

## Sparkle render-side findings (2026-06-04/05) — durable; the WIP code was stashed

The sparkle emitter was fully RE'd (above) and a WIP implementation built, but the WIP
render is NOT yet 1:1 and was **stashed outside the repo** (`../openrecet-sparkle-wip.patch`)
pending the frame-0-forward 1:1 sweep (it will likely be redone from scratch once
everything upstream of the sparkle frame is structurally 1:1 — see `docs/FRONT.md`). These
render-side facts are hard-won and durable — re-use them, don't re-derive:

**Verified CORRECT (bit-exact vs retail):**
- Template `0x3b` = `目玉商品` from `ef/effect1.dat` (now loaded by `scene1_overlay_templates_load_chunk`, committed): texture_type 19, type_shape 0, spawn_count 1, fade_out_default 24, layer_pair 0x100.
- The port's effect00.bmp GPU texture cell (64,64,32,32) IS the yellow 4-point star (verified via a `LockRect` readback: pixel (80,80) BGRA matches the file). texture / UV / decode all correct.
- The composed sparkle **WORLD matrix is bit-identical to retail's** traced matrix: scale ≈ 1.125e-4 (3×3 = `[-1.125e-4,0,~0; 0,6.24e-5,-9.36e-5; 0,-9.36e-5,-6.24e-5]`), translations at the 3 sword columns (x=2·col-9 = -7/-5/-1, y≈2.3, z≈-7.2). Emitter positions + RNG verified.

**The render is one of the FUN_004176ff `FUN_00414ee2` calls, NOT the shop driver's.** The
sparkle slot is **layer 0, mode 0** (slot[5]=template layer byte=0, slot[7]=param_10=0 via
the `FUN_004147d5` wrapper). `FUN_004161c7` (wide_followup) only calls `FUN_00414ee2(1,0)`
— it does NOT draw the sparkle. The sparkle draws via a `FUN_00414ee2(0,0)` call inside
`FUN_004176ff` (the records-A dust/wing pass, port `scene1_render.c` ~L868 under
`scene1_push_projection(dev, 500.0f)`; calls at objdump 0x417885/0x4178b0).

**Exact render state at the sparkle draw** (retail d3d-trace `runs/sr-retail/d3d_trace.jsonl`,
free-roam frame 707, `DrawPrimitiveUP` ret_va 0x415e61, `SetTexture 0x172fcf30`):
SRCBLEND=ONE / DESTBLEND=ONE (additive); COLOROP=MODULATE, **COLORARG1=DIFFUSE(0),
COLORARG2=TEXTURE(2)** (⇒ vertex-colour × texel = the yellow star; the port's default
COLORARG2=CURRENT gave flat white — this was the "white blob" bug); ALPHAOP=MODULATE,
ALPHAARG1=TEXTURE, ALPHAARG2=DIFFUSE; ZENABLE=1, ZWRITE=0, ALPHABLENDENABLE=1; MIN/MAG
filter=LINEAR. `SetTransform(WORLD)` per draw = the billboard matrix above.

**The open render problem (for the 1:1 sweep, not now):** placing the `(0,0)` render so it
(a) has the camera-facing billboard pre-matrix live, (b) uses the additive+MODULATE+TEXTURE
state, and (c) does NOT corrupt the shop-item pass. WIP attempts: at the wide_followup site
the position was correct but the state writes leaked and broke the swords; at
`scene1_render_overlay` (FUN_00417504, the 2D-overlay/HUD pass) the state was clean but the
2D transforms put the 3D-world sparkle off-position; in the `FUN_004176ff` records-A pass
(z_far=500) the state+projection were right but the sparkle still landed off the stand —
likely a view/pre-matrix interaction to resolve once the upstream HOUSE render is verified
1:1 (so the divergence isolates instead of compounding). Diagnose with the Phase-1
render-diff engine, pinned.
