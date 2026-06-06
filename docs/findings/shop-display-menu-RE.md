# Shop-display "remove item" menu — RE (ground-truth, cc04 correction 2026-06-06)

**Status:** RE re-derived TWICE from the same live retail flow-trace of the
save-roundtrip removal. Read this section first; the two boxes below record the
correction history so the wrong mechanisms are never re-suspected.

> **CORRECTION 2 (2026-06-06 PM) — the menu is the `cc04==1` sub-state, NOT
> `cc04==2`.** The first rewrite (below) correctly threw out the `cc08` 0xa
> context-menu, but then mapped the two live-traced call sites (open ret
> `0x488d8a`, update ret `0x48915f`) onto the WRONG decompile branch — the
> `cc04==2` furniture-grid path (`DAT_074b2ed8` / `DAT_0450ff30`). Disassembling
> the two ret addresses shows they are the **`cc04==1`** in-house display-stand
> menu: the open at `0x488d85` sets `DAT_0438cc04 = 1`, and the update at
> `0x48915a` is `FUN_00469414(1)` inside the `cc04 != 2` else-branch
> (all.c:87905+). The removal grid is **`DAT_044f7030`** (= save dword 0x4e26 =
> `SAVE_BANK_FIELD_DISPLAY_GRID`, the very grid the 目玉 sparkle already reads),
> cell `DAT_0438cbfc + DAT_0438cc00*0x14` — **NOT** `DAT_0450ff30` /
> `DAT_074b2ed8`. The original RE missed this because the retail `0x48670f`
> Frida hook watched `cc08` (stays 1) but **never watched `cc04`** (now added).

> **CORRECTION 1 (2026-06-06 AM) — still valid:** the removal does NOT use the
> `cc08` 0xa context-menu, `FUN_0048940e` grid-swap, `FUN_004850fe` populate, or
> the `DAT_0438cc08` state machine. `cc08` stays `1` the whole interaction;
> those functions never run. That material is the **counter / customer-sell**
> menu, a different later feature — keep it for that.

## How it was found
- Bench `tests/scenarios/house-display-remove` — the save-roundtrip inputs
  verbatim through the first sword removal (load fa7c82 → walk to the back stand
  → Z → "select none" → Z → close). Anchors `LOADING_END` → `PAUSE_OPEN` (menu
  open) → `PAUSE_CLOSE`.
- `scenario-test house-display-remove --target retail --call-trace` →
  `runs/scenarios/house-display-remove-retail-20260606T023339Z/call_trace.jsonl`.
- The two menu call sites were pinned by their **ret_va** (image base 0x400000):
  `0x468338` open → ret `0x88d8a` (×1, frame 14135); `0x469414` update → ret
  `0x8915f` (×75). Both disassembled in `vendor/unpacked/` to identify the
  enclosing branch.

## Ground-truth findings (authoritative — from the live trace + asm)
1. **`cc08` stays `1` (free-roam); the menu is gated by `cc04`.** `cc04` goes
   `0 → 1` on the Z-press (open) and back to `0` on close. `b74c/b754/cc0c` stay
   `0`; `cd0bc` is a constant `7` (irrelevant here).
2. **`db054` (the §85 HOUSE phase clock) FREEZES for the whole menu.** It holds
   at **157** from the open frame 14135 through 14211 (76 frames), then resumes
   at 158 on close. `FUN_0048670f` itself keeps ticking every frame (no gap in
   the `0x48670f` rows) — the menu just routes away from the free-roam walk arm
   that advances `db054`. The player is frozen in the interact pose.
3. **The cc04==2 / `FUN_0048940e` / `FUN_004850fe` / `DAT_0450ff30` paths are
   NEVER called.** Authoritative menu-window call set (open frame 14135 .. +90,
   the funcs that matter):

   | VA | ×calls/window | role |
   |----|------|------|
   | `0x468338` | 1 | menu OPEN (ret `0x88d8a`, cc04→1) |
   | `0x4681ec` | 1 | post-open setup (FUN_004681ec) |
   | `0x4693e3` | 91 | open/close SLIDE (`DAT_0734b98c` ramp) — every frame |
   | `0x469414` | 75 | menu UPDATE `FUN_00469414(1)` — returns 1/2/3 |
   | `0x469a9f` | 2 | selected-item query (−1 = "none") |
   | `0x468d22` | 1 | inventory RETURN of the removed sword |
   | `0x469241` | 1 | place/refresh helper |
   | `0x468ddc` | 237 | inventory query (per-slot) |
   | `0x46b00a` | 91 | menu RENDER — every frame |
   | `0x485f8c` | 91 | display-cell RENDER (occupied items + cursor) — every frame |
   | `0x4682d0`,`0x435612` | 1 | close cleanup |

## The real mechanism — the in-house display-stand menu (`cc04==1`)

### Open gate (free-roam → menu), asm `0x488cce`–`0x488d85` in `FUN_0048670f`
Reached on the Z-press (`DAT_073dddd4 & 0x10`) free-roam path, after the
door/exit (`bVar17`), counter (`DAT_0438be7c`), and customer (`bVar3`) guards all
fall through. Then `iVar7 = FUN_004860c8()` (the furniture index for the faced
cell) and the branch split on the **furniture-suppression flag**:
```
if (DAT_0450fee8[fidx] == 0) {            // == 0 → a normal visible display STAND
  if (DAT_0450f3f2[slot] != 0 &&          // shop-is-open-ish gate
      DAT_0450f400[slot] == 0 &&          // not suppressed
      DAT_0438cbfc != -1) {               // a display cell is highlighted (col, here 4)
    FUN_0049933c(rand()%3 → 00re_sys04a/b/c.bin)   // open SE
    FUN_00482a71(&DAT_056daae8, DAT_056da1cc, 1.0)  // actor → interact pose
    bVar17 = (DAT_0438cbe8 == 0)
    DAT_0438cc04 = 1                       // ← ENTER the display-stand menu
    if (bVar17) DAT_0438cbe8 = 1
    FUN_00468338(0, bVar17)                // ← OPEN (ret 0x488d8a)
    FUN_004681ec()
  }
  goto tail;
}
DAT_0438cc04 = 2; ... FUN_00468338(0,bVar17); ...   // flag != 0 → the OTHER (cc04==2) furniture-grid mode
```
- `DAT_0450fee8` / `DAT_0450f3f2` / `DAT_0450f400` are base `DAT_044e3798`
  (working bank) + 0x2bc.. offsets.
- For the 目玉 sword STAND the flag is `0`, so the trace takes **cc04==1**.

### Update + removal — `cc04 != 2` else-branch (all.c:87905+, asm `0x48915a`+)
```
iVar6 = FUN_00469414(1);                  // update; returns 1=confirm, 2=cancel, 3=pick-up
if (iVar6 == 2) {                         // CANCEL
  DAT_0438cc04 = 0; FUN_00435612(); FUN_004682d0(); FUN_00499519();
} else if (iVar6 == 3) {                  // PICK-UP (carry) — sets db048=0xc carry pose
  ...
} else if (iVar6 == 1) {                  // CONFIRM (place / remove)
  item = FUN_00469a9f();                  // selected item; −1 == "select none" == REMOVE
  cell = DAT_0438cbfc + DAT_0438cc00*0x14; // (col 4, row 0) → cell 4
  if (item != -1 || grid[cell] != -1) {   // proceed if placing OR slot occupied
    ... SE / rng ...
    if (item_category < 0x1451 || item_category > 0x14b3) {   // (sword stand path)
      DAT_044f7030[cell] = item;          // ← write −1 == REMOVE the sword (save dword 0x4e26)
      FUN_00469241();                     // place/refresh
      FUN_00468d22();                     // RETURN the removed sword to inventory
      FUN_0044bd0b(); FUN_0048439a();
      ...
    } else { ... counter-display variant ... }
  }
  DAT_0438cc04 = 0; FUN_00435612(); FUN_004682d0();   // CLOSE
}
```
**"Select none" = `FUN_00469a9f()` returns −1 → `DAT_044f7030[cbfc + cc00*0x14] =
−1` (item removed) + `FUN_00468d22` returns the sword to inventory.** That is the
save-relevant state change the roundtrip persists. The removed cell (4) is the
back-row col-4 stand = the sparkle's x=−1 sword.

### State globals
- `DAT_0438cc04` — the cc08==1 interaction sub-state (0 walk / 1 display-stand
  menu / 2 furniture-grid). **THE gate.** Port `s_cc04`.
- `DAT_0438cbfc` / `DAT_0438cc00` — highlighted display cell col / row (−1 none).
- `DAT_044f7030` — display grid (= save dword 0x4e26 = `SAVE_BANK_FIELD_DISPLAY_GRID`).
  Cell index `cbfc + cc00*0x14` (15-wide rows; back row = row 0).
- `DAT_0734b98c` — open/close slide counter (`FUN_004693e3`, 0↔5), gates render.
- `DAT_0438cbe8` — open-once latch (set on first open, arg2 `bVar17` to `FUN_00468338`).
- `DAT_0450fee8[fidx]` — furniture-suppression flag: ==0 chooses cc04==1, !=0 cc04==2.

## ⚠ A1 PREREQUISITE discovered 2026-06-06 — the open gate needs `cbfc`, which needs furniture placement (UNPORTED for loaded saves)

Tracing the open gate's inputs end-to-end, the planned A1 ("just wire the Z-press
open") has a deep unported dependency chain:

1. **Open gate** (asm `0x488cce`–`0x488d85`) fires only when `DAT_0438cbfc != -1`
   — i.e. the player is FACING a highlighted display cell.
2. **`cbfc`/`cc00`** are set every walk frame by **`FUN_0048619f`** (cell-highlight
   detector, asm decoded below), which gates on the **furniture-layout grid**
   `DAT_074b28e8[col + row*0x14] ∈ [2,8]`.
3. **`DAT_074b28e8`** is rebuilt every frame by **`FUN_0048960d`** (0x48960d) in the
   `FUN_0048670f` prologue (`if (DAT_0438b924==0) FUN_0048960d()`, all.c:86728): copy
   the per-stage base template `DAT_005cd104[stagetype*0x4b0]` (300 dwords) into the
   grid, then STAMP each placed-furniture footprint with 2 (2×2) / 3 (1×4) / 4 (4×1).
   The base template's row 0 has NO stand cells (cols 1-4/11-13 = 0); the back-row
   sword STANDS come entirely from the **placed-furniture stamp**.
4. **Placed furniture** (`DAT_0438bfb4` count, footprint-class `DAT_0438bfcc`,
   rotation `DAT_0438c01c`, origins `DAT_045105a8/ac` = bank dword 0xb384/5) is written
   by **`FUN_00436f97` block-21** (= the port's `scene1_postload_walker_phase2_init`).
   **In the port this writer is GATED OFF in production** (`g_walker_scene_type` default
   -1 → early-return; only the `--force-walker-phase2` debug flag fires it for new-game).
   So a CONTINUE/loaded save has **`g_scene1_walker_phase2_count == 0`** → empty layout
   grid → `cbfc` always -1 → **the open gate can never fire.** This is the same
   long-deferred W4/Cf.* furniture-placement blocker.

**`FUN_0048619f` (cell detector) — asm-decoded (0x48619f), constants verified:**
```
eax = ftol(pang / π * 10.0)            ; facing-octant index (C1=π@0x51943c, C2=10@0x5194f0)
xoff=0; zoff=0
if eax==0:            zoff =  2.0
elif -11<=eax<=-9:    zoff = -2.0
elif eax==4:          xoff =  2.0       ; (0x519314=2.0, 0x519908=-2.0)
else:                 xoff = -2.0
col = ftol((px + xoff + 10.0) * 0.5)   ; (+10@0x5194f0, *0.5@0x51935c)
row = ftol((pz + zoff +  8.0) * 0.5)   ; (+8@0x519378)
clamp col>=0, row>=0
cell = DAT_074b28e8[col + row*0x14]
if 2 <= cell <= 8:   cbfc=col; cc00=row
                     _DAT_0438cbf4 = col*2 - 9.0   ; render pos (0x5196b4=9)
                     _DAT_0438cbf8 = row*2 - 7.0   ; (0x5193f8=7)
                     if DAT_0450fee8[FUN_004860c8(col,row)] != 0: DAT_0438bf68 = idx+1
else:                cbfc=-1; cc00=-1
```
Validated against the live open frame (pang=-π → eax=-10 → zoff=-2; px=-0.787 →
col=4; pz=-5.32 → row=0): **cbfc=4, cc00=0** = back-row col-4 sword. ✓

**Revised Phase-A chip order** (A1 now has a furniture prerequisite, A0):
- **A0 — furniture placement + layout grid (the prerequisite).** Wire
  `scene1_postload_walker_phase2_init` to fire on the loaded-save HOUSE entry
  (source its runtime inputs — scene_type / stage_positions — from the loaded bank
  origins; the new-game count=3 layout is the likely loaded-save layout for the
  early fa7c82 save). Port `FUN_0048960d` (grid rebuild) into the prologue + the
  `FUN_004860c8` furniture-index helper. Verify: `DAT_074b28e8` row-0 stand cells +
  the round-table footprint match a live retail `DAT_074b28e8` dump. Also unblocks W4
  collision furniture + PII.3b furniture render.
- **A0b — cell-highlight detector `FUN_0048619f`.** Wire into the walk tail
  (all.c:87750-87757 — runs when `DAT_0450f3f2 != 0`). Add `cc04/cbfc/cc00` to the
  port's `0x48670f` flow-trace payload. Verify: `cbfc/cc00` track retail frame-for-
  frame as the player walks to the stand.

## Implications for the plan — rewritten Phase-A chips
- **A1 — open + slide + sim-freeze.** Wire the cc04==1 open gate into the port's
  `FUN_0048670f` Z-press path: furniture-flag==0 stand + `cbfc != -1` + the two
  shop gates → `s_cc04 = 1`, `FUN_00468338` open + the `DAT_0734b98c` slide, and
  **freeze the HOUSE sim while `s_cc04 != 0`** (stop advancing `db054` / the
  free-roam walk arm — the port already skips the walk arm for `s_cc04 != 0`, so
  this is mostly confirming `db054` rides the same gate). Verify vs retail:
  `db054` frozen for the menu window, `cc04` 0→1 on Z, slide ramp.
- **A2 — update + removal.** `FUN_00469414(1)` (cursor nav + the 1/2/3 return) +
  `FUN_00469a9f` selected-item + the grid write `DAT_044f7030[cbfc+cc00*0x14] =
  item` (−1 on "select none") + `FUN_00468d22` inventory return + close to
  `s_cc04 = 0`. The grid is the one the port already maps
  (`SAVE_BANK_FIELD_DISPLAY_GRID`) — the sparkle stops over the removed cell for
  free once the write lands.
- **A3 — render.** `FUN_0046b00a` (menu panel + option list + cursor) +
  `FUN_00485f8c` (occupied display-cell items, reuse `wf_render_display_item`).

The `cc08` 0xa / `FUN_0048940e` / `FUN_004850fe` / `DAT_0450ff30` items in the old
plan are the **counter / customer-sell** menu — a different, later feature.

## A3 — menu RENDER (FUN_0046b00a) ground truth from the d3d-trace (2026-06-06)

The user wanted the **whole** interaction rendered 1:1.  A2 (removal) +
A2.1 (interact-pose anim tick) landed & verified; the menu RENDER is the next
chip.  A first single-texture panel attempt (item_win.tga (0,0)-(400,320))
rendered a **white blob** — the decompile's `DAT_073d8748 = item_win.tga` label
is misleading.  The retail d3d-trace (`runs/scenarios/house-display-remove-both-*`,
`--d3d-trace`) of a menu frame shows the panel is **multi-texture**, drawn at the
END of the HOUSE render (FUN_0045cc85), in this texture order:

```
item_win.tga ×2          (frame/border pieces)
data_win.tga             (the parchment PANEL background)
hpmp_base.tga            (a bar — the item stat/gauge?)
item_win.tga ×5          (list frame / scroll / decorations)
<font text ×~30>         (SetTexture None = the bitmap-font glyph draws — item names/counts/Details)
item/item00.bmp          (the item ICON sheet — per-row item icons)
item_win.tga
<font text ×~120>        (the Details panel text + per-row names)
data_win.tga + nowloading.tga   (the menu CURSOR — nowloading.tga holds the hand)
```

So the render layers are: **data_win.tga panel bg → item_win.tga frame/scroll →
hpmp_base.tga bar → per-row { item00.bmp icon + font name/count } → font Details
panel → data_win.tga+nowloading.tga cursor.**

### Next-chip checklist
1. **Re-capture with `--d3d-trace-verts`** to get the exact per-quad dst/src/UVs
   + diffuse for every panel/frame/row quad (the `--d3d-trace` run has no vertex
   data) — then `render_diff.py --explain` names any divergent vertex once the
   port draws.
2. **Port FUN_00468338 param_1=0 population** first (the list the rows iterate).
   Intricate but deterministic + host-testable: scans inventory DAT_044e37b0,
   item-DB category grouping (DAT_095d3808 = id/100), stacking duplicates,
   per-category tabs, FUN_0045526a sort.  Module: `scene1_display_menu.c`.
3. **Port FUN_0046b00a** quad-by-quad against the verts trace, using
   `render_quad_bind/_add/_flush` + the correct sysasset textures (data_win,
   item_win, hpmp_base, item00, nowloading).  Wire after `scene1_render_overlay`
   in main.c (the engine calls it at the tail of FUN_0045cc85).  Reuse the
   load-picker render (`title_continue_picker_render` in scene_title.c) as the
   item_win/data_win quad-draw pattern.
4. **Port FUN_00485f8c** (the occupied display-cell items + cursor over the stand).
5. **Pose:** retail also switches the player anim 3→4 at the confirm (the pick-up
   pose tied to db048=0xc); land it with the cursor/carry chip.

Reverted the white-blob panel attempt (commit kept master clean); the scaffolding
(wiring point + the item_win pattern) is captured above.
