# Shop "items on display" renderer — MAPPED (porting spec)

**Date:** 2026-06-04 · **Status:** ✅ MAPPED via d3d-trace caller analysis on
retail. Ready to port. (Earlier this doc said UNMAPPED + killed two wrong leads;
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
