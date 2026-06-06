# Plan — Shop-display interaction + pause-menu save/load roundtrip

**Goal (user, 2026-06-06):** get the port to reproduce the **save-roundtrip
reference trace** end-to-end: load save → walk to the top-right sword on the
display → **remove it** → **save** (pause menu) → **quit to title** → **reload**
the save → the sword is gone (2 swords left). Reference trace +
ground-truth post-move save bytes: `tests/traces/save-roundtrip/`
(`trace.jsonl`, `_saves/397e13….sav.gz`).

Full port, not MVP — port the whole interaction path, verify each chip 1:1 vs
retail via `scenario-test --target both` + `flow_diff`/pixel-diff.

## Current port state (start line)
- HOUSE free-roam walk = 1:1. The display items + 目玉 sparkle render 1:1
  (`shop-item-display-RE-status.md`).
- `FUN_0048670f` (HOUSE update): only the **cc08==1 walk arm** is ported
  (`scene1_player_ctrl.c`). **Every interaction state (0xa menu, 2 mgmt, pause)
  routes to an unported stub.**
- Pause menu: only **asset preload** ported (`scene_pause.c` = `FUN_00473a3e` +
  FPU init). No open/render/input/save/quit logic.
- Save: **LOAD is done** (`save_io_try_load`, `save_bank_init_all`,
  `save_work_load_slot`). `save_io_write_arena()` (file-write half of
  `FUN_004905a8`) **exists + is sandbox-aware + has a write-notify hook**, called
  only at shutdown (`main.c:2206`). **Missing:** the working-bank → save-bank
  **merge + checksum** (the `FUN_004905a8(slot)` param≠-1 branch) and any
  in-game call site.

## ⚠ CORRECTION 2026-06-06 — the Phase-A RE map below is the WRONG mechanism

A live retail flow-trace of the **actual** removal interaction
(`tests/scenarios/house-display-remove`, `--target retail --call-trace`) proved
the removal does **NOT** use any of: the `cc08` 0xa context-menu, `FUN_0048940e`
grid-swap, `FUN_004850fe` populate, the `DAT_0438cc08` state machine, or the
`DAT_044f7030` grid. During the whole interaction **`cc08` stays `1`** and those
functions are never called. The cc08/0xa material below is the **counter /
customer-sell** menu — a different, later feature.

**The real mechanism = the in-house display menu (`DAT_0734bxxx` subsystem):**
open `FUN_00468338`, update `FUN_00469414`, render `FUN_0046b00a`, slide
`DAT_0734b98c`, removal writes `-1` into grid `DAT_0450ff30[ed8+bf64*0x14]` (item
`FUN_00469a9f()`==-1 = "none") + `FUN_00468d22` inventory return. The HOUSE sim
freezes (`db054` stops) while the menu is up. **Full corrected RE + the rewritten
Phase-A chip breakdown: `docs/findings/shop-display-menu-RE.md` — start there.**

## (SUPERSEDED for Phase A) Original RE map — confirmed against docs/decompiled/all.c

### Interaction state machine — `DAT_0438cc08` inside `FUN_0048670f` (0x48670f)
States: 1 free-roam · **0xa(10) context menu** · **2 display-management** · 4
shop-open · 0x17 furniture-place · 0x32 counter · 0xf/0x10/0x11/0x12/0x1e
sub-anim. Port var `s_cc08` (`scene1_player_ctrl.c`).

- **Open (1→0xa)** @ all.c:88151-88158: `if (9 < DAT_0438b74c)` (proximity
  counter, ++/-- at 86755-86761 in states 0/10/0xf/0x10/0x1e) `&& !(in&0x20) &&
  (in&0x10)` (action button) → `DAT_0438cc08 = 10` + `FUN_00499519()` (sfx).
  The menu is built by **`FUN_004850fe` (0x4850fe)** @ 85374-85409: writes
  `DAT_074b2d98[]` action-type array (idx0=0; +1 each per display-level
  threshold: lvl>4→type1, >6→3, >7→5, >8→4; trailing 6=close), count+1 →
  `DAT_005cd0bc`. Reset `DAT_0438cc0c=0` (cursor) at 87667/87684.
- **Menu tick (state 0xa)** @ 86891-86973: `DAT_0438b754` counts up; at ==0xf
  the selected `iVar7 = DAT_074b2d98[DAT_0438cc0c]` dispatches:
  - 0 → take/pickup (cc08=4, `FUN_0045edaa`)
  - 1/2/3/4 → furniture-place setup (cc08=0x17, copy `DAT_0451057c..` metadata,
    `FUN_00452dc1/d85/dfd/e39`)
  - **5 → MANAGE DISPLAY** @ 86938-86942: `DAT_0438bea4=0; DAT_0438cc08=2;
    FUN_0048940e(0,1);`
  - 6 → close (no-op fallthrough)
- **Cursor nav (state 0xa)** @ 87188-87195: up/down (`DAT_073dddd6 & 8/4`) →
  `DAT_0438cc0c = (idx±1 + n) % n`.

### Display-management mode 2
- **`FUN_0048940e` (0x48940e, 511B)** @ 88179-88289: `(idx, dir)`. dir param
  loads/saves the furniture's item grid ↔ scratch buffer `DAT_074b28d8`.
  Furniture type from `(&DAT_0438bfcc)[DAT_0438bea4]`: 3=2×2, 4=1×4, 6=1×6.
  Grid cell index = `origin_x[idx] + (origin_y[idx]+row)*0x14 + col` into
  `DAT_044f7030` (display grid). **⚠ VERIFY the dir polarity myself** (subagent
  was muddled): one dir copies grid→scratch (enter), the other writes scratch→
  grid **setting the source cell to -1** (the actual item move/removal).
- **Cursor nav `FUN_004862e7` (0x4862e7, 334B)** @ 86294-86383: reads input,
  clamps cursor `DAT_0438cbfc`(col)/`DAT_0438cc00`(row) to the furniture extent,
  recomputes float render pos `_DAT_0438cbf4/8`, sets `DAT_0438bf68` = selected
  cell occupancy.
- **Render `FUN_00485f8c` (0x485f8c, 316B)** @ 86124-86186: gated cc08==2; draws
  each occupied scratch cell via `FUN_00415fab` (already ported as
  `wf_render_display_item`) at the furniture grid origin. + cursor quad.
- **Origins** `DAT_045105a8`/`ac` = working-bank dword 0xb384 (x/y), stride 8,
  indexed by `DAT_0438bea4`. **Display grid** `DAT_044f7030` = working-bank dword
  0x4e26 (15×20, item-id/cell, -1 empty). Scratch `DAT_074b28d8` (≤6 dw).
- Inventory return on removal: `FUN_00468d22(item_id)` (verify).

### Pause menu — `FUN_0047fa76` (0x47fa76, 462B) dispatcher @ 82008
- Selector `DAT_074b2878` indexes menu-type array `(&DAT_074b2844)[]`; ramp
  `DAT_074b2880` (0→10) gates entry to the active option's handler:
  - type 0 → `FUN_0047fe98` (0x47fe98) — **top-level option cursor nav**
    (`DAT_074b288c` selection, in&0x10 confirm, in&0x20 cancel)
  - type 3 → **`FUN_0047f5bc` (0x47f5bc) = SAVE-to-slot** → `FUN_004905a8(slot)`
    @ 81970 (slot from `DAT_074b2834[DAT_074b288c]`) = **merge + write**
  - type 2 → `FUN_0047fc44` (0x47fc44) = **CONFIG** (BGM/SE sliders
    `DAT_056e5778/74/7c/84/82`, `FUN_00499583`); persists via `FUN_004905a8(-1)`
  - type 1 → `FUN_0047ff40` · type 5 → `FUN_004802cf` · type 6 → `FUN_0049f365`
- **Quit-to-title** @ 82060-82100: `DAT_074b2830==1` → confirm box
  `FUN_00434def("Returning to title screen. Are you sure?")`; `FUN_00434ed2(1)`
  Yes → fade → `FUN_00453373` + per-mode teardown (`DAT_06a499a8` switch) +
  `FUN_00473c03`; **`DAT_0438b1c0 = 0`** (title) + `FUN_00452cde` (loading thread)
  + `FUN_0045281c(0,0x11)`. (NOT scene 9 — agent#1 was wrong.)
- Open trigger: START during HOUSE free-roam (find exact gate in `FUN_0048670f`
  prologue / top INGAME tick — `(&DAT_0450f488)[…]` region ~86705).

### Save write — `FUN_004905a8` (0x4905a8, 179B) @ 93126
```
if (slot != -1) {                       // MERGE (port-MISSING)
  copy 0xb7f2 dw: work bank (DAT_044e3798 + slot0*0x2dfc8) → save bank (DAT_056e6280 + slot*0xb7f2)
  checksum: DAT_05714244[slot*0xb7f2] = sum(save bank dw[0..0xb7f0))   // stored at dw 0xb7f0
}
write &DAT_056e5770 size 0x011f7530 → "save.dat", then "_save.dat"   // = save_io_write_arena (DONE)
```
On-disk = raw 18MB arena (header 0x0b10 + 100 banks × 0x2dfc8), no obfuscation,
per-bank XOR checksum at dword 0xb7f0. Port: `save_bank_stamp_checksum` exists;
need a `save_work_to_save_bank(slot)` (copy + stamp) = the merge.

## Chip sequence (each = its own commit, verified vs retail where visible)
**Phase A — interaction** (user said do this first):
- **A1** cc08 0xa context-menu state machine: open trigger (proximity+action),
  `FUN_004850fe` menu populate, countdown + up/down cursor nav, dispatch select
  → set cc08 (esp. type 5 → 2 + `FUN_0048940e(0,1)`). Wire into the port's
  `FUN_0048670f` dispatch (replace the stub for non-1 states, faithfully).
- **A2** context-menu RENDER (the on-screen option ring) — locate renderer, port.
- **A3** mode-2 management logic: `FUN_0048940e` (verify dir polarity), cursor
  nav `FUN_004862e7`, the item move/remove (grid cell→-1) + inventory return,
  exit 2→1.
- **A4** mode-2 RENDER: `FUN_00485f8c` (reuse `wf_render_display_item`) + cursor.
**Phase B — pause menu + save/exit:**
- **B1** pause open (START) + `FUN_0047fa76` dispatcher + menu-type populate +
  selector/option nav (`FUN_0047fe98`).
- **B2** pause RENDER (pause.tga panel + options + portraits; assets preloaded).
- **B3** SAVE: `save_work_to_save_bank(slot)` (merge+checksum = `FUN_004905a8`
  param≠-1) + save-slot picker `FUN_0047f5bc` + wire `save_io_write_arena`.
  **Validate written bytes vs `_saves/397e13….sav.gz`** (host-testable).
- **B4** QUIT-TO-TITLE: confirm box → teardown → `DAT_0438b1c0=0` + loading →
  title.
**Phase C — roundtrip:** drive `save-roundtrip/trace.jsonl` on the port
`--target both`; confirm sword removed after reload + save bytes match.

## Verification assets
- `tests/scenarios/house-loaded-display{,-pinned}` — loads the fa7c82 save into
  HOUSE with 3 swords.
- `tests/traces/save-roundtrip/` — the full reference recording + ground-truth
  post-move save.
- `runs/sr-retail/` — retail captures (display frames, d3d-trace).
