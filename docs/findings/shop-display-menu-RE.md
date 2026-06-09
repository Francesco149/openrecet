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

## ⚠ CORRECTION 3 (2026-06-06 PM) — frame-by-frame retrace; the A2/A2.1 "fixed" claims were WRONG

User flagged (again) that the interaction is still badly broken and asked for a
**frame-by-frame** retrace of retail from just before the Z-press.  New bench
`tests/scenarios/house-display-remove-opentrace` (contiguous `{caprange}` over the
open) drove `--target both --call-trace --d3d-trace --d3d-trace-verts`
(`runs/scenarios/house-display-remove-opentrace-both-20260606T114216Z`, 47
aligned frames each side; open = index 8 = port f889 / retail f13872).  Findings,
each corrected against the FRONT's prior claims:

1. **Player pose/facing — actually 1:1 (USER-CONFIRMED visually).**  The FRONT's
   A2.1 "faces camera was a frame-mismatch artifact" was right in conclusion;
   user verified the pose is correct.  NOT a bug.  (She is visible top-left
   because the menu doesn't cover that quadrant — see #4.)

2. **White HUD — NOT fixed by A2's b150 decouple.  Root = `item_win.tga` bind
   desync (the §108 cache class).**  At PAUSE_OPEN+29 (port f918+) the top HUD
   (money panel + "Exchange with client" badge) goes flat grey **(157,157,157)**
   and STAYS grey.  Proven from the port d3d-trace: the HUD `item_win.tga` draws
   on the grey frame are **byte-identical** to the gold frame — same texture
   pointer (0x6c82950), same vertex bytes (pos + diffuse 0xff7f7f7f + UVs), same
   COLOROP(8→4), same MIN/MAG/MIP filter states, same bind order.  The ONLY
   stream delta is the walk-dust (`effect.bmp`) decaying to 0 draws at exactly
   f918.  So the texture *content* being sampled is wrong = item_win's bound
   D3D texture is desynced (flat → samples a constant).  **Retail avoids it
   because `FUN_0046b00a` rebinds item_win every menu frame.**  ⇒ porting the
   menu render (#4) is expected to fix this; if not, add a `SetTexture`
   invalidate on the cc04 path mirroring the §108 ee1e1c2 sparkle fix.

3. **Slot "highlight" = TWO unported renderers (user crops 20260606T120932_bc07):**
   - **(3a) Orange glow behind the faced sword** = `FUN_0045aa36` **Block G**
     (decompile L55089-L55108), currently the documented STUB in
     `src/scene1_chr_shadow.c` ("L347 Block G — cc08==1 ground-decal special").
     A 3D world quad at the cell (`_DAT_0438cbf4`, 1.9, `_DAT_0438cbf8` =
     `2*cbfc-9`, `2*cc00-7`), texture `DAT_073d8748`=item_win, pulsing alpha
     `alpha = ftol(sin(DAT_0438b8cc*0.05))`, color `alpha<<24 | 0xffffff`,
     DrawPrimitive(TRIANGLESTRIP,2,&DAT_0064c388,stride 0x18).  **TODO:** read the
     DAT_0064c388 vertex UV + world-size setup (objdump 0x45aa36) for the exact
     item_win sub-rect + quad size, then add the block to chr_shadow (reuse its
     existing 3D shadow-quad path).
   - **(3b) "Worn Sword" parchment name bubble + text** = `FUN_00409925`
     (decompile L6425-L6505), the free-roam shop-HUD's faced-cell block (the rest
     of FUN_00409925 — money/day-progress bars — is a different already-rendered
     HUD).  Gated `b1c0==1 && cc08==1 && cbfc!=-1 && cc00!=-1 && grid[cell]!=-1`.
     Recipe: project the cell world point `(2*cbfc-9, 1.9, 2*cc00-6.5)` to screen
     via `FUN_00490c78`→`FUN_00490d29` (transform by view `g_scene1_view`, then
     `sx = 320 - fx*vx/vz`, `sy = 240 + fy*vy/vz`; `fx = proj[0]*320`,
     `fy = proj[5]*240` from `g_scene1_proj` per `FUN_00490cc6`).  Then:
       • bubble quad: item_win src(832,480,959,559) dst((sx-26),(sy-16),164,80)
         [verify color/arg via objdump 0x409925 — Ghidra dropped the 4th arg].
       • name: `font_draw_text_centered(sx+52, sy+26, name, color, 0.6)` where
         name = item DB record `FUN_004681f6(grid[cell]>>6)` (ported in
         tables_item.c) name field; the count/qty formatting via FUN_005038ff.
     `font_draw_text_centered` (FUN_0047d14c) is text-only in the port, so the
     bubble is the item_win quad above (NOT inside the label fn).

4. **Menu UI (FUN_0046b00a render + FUN_00468338 population) — wholly unported.**
   The port draws NO menu (0 menu textures in the f918 d3d-trace).  Retail slides
   the panel in from the right over ~5 frames (`DAT_0734b98c<<7` = 128px/step) and
   it occupies the right + bottom, leaving the top-left scene+HUD visible.
   - **`DAT_073d8748 = item_win.tga`** (1024², loaded `FUN_0047193c` @ L71643);
     **`DAT_073d8678 = data_win.tga`** (512², L71644 — the menu cursor base);
     item icons = `DAT_073d8778[]` table (item00.bmp, FUN_0047193c @ L71706).
   - **Population `FUN_00468338(0, first)`** (L64137): param_1==0 ⇒ the else path
     scans the working-bank inventory `DAT_044e37b0` (base+0xb7f2 dwords), for each
     item finds its DB record (`DAT_095d3804` stride 0xb3, match `id>>6`), builds a
     sort key into DAT_0730b60c, `FUN_0045526a` sorts, then groups into category
     tabs (`DAT_07337210[]` counts, `DAT_0731f598[]` stride-2 list, `DAT_0731f404`
     num tabs).  Replaces the A2 single-"none"-entry stub in
     `display_menu_open`.  Item DB + names already in `tables_item.c`.
   - **Render `FUN_0046b00a(0,0)`** (L66457, called from the HOUSE render tail
     FUN_0045cc85 @ L56226): slide gate `if(DAT_0734b98c==0) return;`
     `local_18 = (640 - slide*128) + 240`, `local_40 = 80`.  Panels (all
     `FUN_00404efc(dst{x,y,w,h}, src{l,t,r,b}, item_win, color)` = port
     `render_quad_add`): main panel src(0,0,400,320) dst(local_18,40,400,320);
     category tab src(448, 736|813, 688, 813|890) dst(local_18+80,10,240,77);
     scroll-up src(448,896,512,944) dst(local_18+56,40,64,32) if scroll>0;
     scroll-down src(512,896,576,944) dst(local_18+56,312,64,32) if more-below;
     then per-row { item00 icon (src from DB icon idx % / 8 *32) + font name/count
     via FUN_005038ff+FUN_0047ca05 }, the description window (DAT_0734b990), the
     selected-row pulse, and the cursor (DAT_073d8678 src(288,320,488,352)
     dst(440,440,...)).  Build incrementally + validate each group quad-by-quad
     against `--d3d-trace-verts` (render_diff.py --explain).

### Chip plan (each its own commit, verify vs the opentrace both-run)
- **B-white**: confirm #4 fixes the white HUD; else add the cc04 SetTexture invalidate.
- **C3a**: port FUN_0045aa36 Block G (orange cell glow) — needs the 0x45aa36 vert UVs.
- **C3b**: port FUN_00409925 L6425-6505 (name bubble + text) + the world→screen helper.
- **C4a**: port FUN_00468338 param_1==0 population (real inventory list).
- **C4b**: port FUN_0046b00a panels → rows/icons/text → description → cursor.

## SESSION HANDOFF (2026-06-06 PM) — menu renders + white-UI fixed; next-session chips

**Landed this session** (commits f625b36→4013fbc): the contiguous open-trace bench
`house-display-remove-opentrace`; CORRECTION 3 diagnosis; C4a population
(`FUN_00468338(0)` — inventory scan + item-DB category tabs + `chr_prepass_sort`,
each tab led by a -1 "Nothing" entry, deduped counts); C4b-1 panels
(`FUN_0046b00a` item_win main/frame/scroll, **user-confirmed 1:1**); C4b-2 rows
(icons `item_icons[cat]` at xL+72,row*0x22+86 + name/count text at xL+120,
row*0x22+92, scale 0.8); C4b-3 "Swords" header (`FUN_0047d14c`, center_x=xL+204,
y=40); white menu text; and the **white-UI COLORARG fix** (see below). New tool
`tools/d3d_state_at_draw.py` (reliable state-accumulating trace inspector).

**WHITE-UI ROOT CAUSE (fixed, commit c4be7d5):** the d3d COLORARG state is
persistent across frames; the port's 3D renderers clobber COLORARG1=DIFFUSE /
COLORARG2=CURRENT|TEXTURE and never restore. **Retail NEVER sets COLORARG** (zero
sets in a whole menu frame — `d3d_state_at_draw.py` proved it). Walk-dust masked
it (re-paired COLORARG2=TEXTURE); the menu freezes the walk → dust stops → the
leak (COLORARG1=DIFFUSE+COLORARG2=CURRENT) hits the 2D UI → MODULATE = diffuse·
current = white (alpha intact → silhouette survives). Fix: restore COLORARG1=
TEXTURE, COLORARG2=CURRENT in `render_quad_state_setup`. ENGINE-QUIRK to log:
retail leaves COLORARG at the D3D defaults for ALL UI.

## SESSION (2026-06-06 PM #2) — C4b-4 cursor + nav + description + tip LANDED, user-confirmed 1:1

Commits: cursor-init+nav+description; dash-string fix; "Button 3: Item Details" tip.
**User-confirmed the settled menu is 1:1.**  What landed (`scene1_display_menu.c`):
- **Cursor-init (FUN_00468338 tail, all.c:64637):** first-open starts the cursor on
  the first REAL item (skip the -1 "Nothing" lead) → retail highlights the displayed
  sword + its description, not "Nothing".  A cursor-UP selects "Nothing" (the removal).
- **Nav (faithful FUN_00469414):** cursor up/down + scroll, tab switch, single-tab page
  jump, the held auto-repeat mask (`g_sim_buttons[0].held`), slide the shared cursor on
  every move, the "Number possessed" recount (`DAT_005c6ee4`).
- **Hand cursor:** the SHARED `title_save_dialog` cursor (FUN_00435693/710/747 — same as
  title/options/skip-prompt), snapped at open (x=280, y=(cursor-scroll)·36+96), hidden on
  close, drawn at the menu tail as the engine's FUN_0048fdaf does (FUN_0046b00a→FUN_00435747).
- **Description panel (FUN_00469b3a):** item_win bottom panel bg src(0,320,640,480)
  dst(0,332,640,160) + desc_line1/2 + **"Base Price- %s"** (comma fmt, FUN_00469abb) +
  **"Number possessed- %d"** — note the **DASH** (`s_…_005c75f0`/`005c7638`), retail EN
  shows "Base Price- 200" / "Number possessed- 1".  White text scale 0.8 (price is
  data-driven from the DB = 200).
- **"Button 3: Item Details" tip (FUN_0046b00a tail, all.c:66843):** a BAKED data_win.tga
  strip src(288,320,488,352) at fixed dst(440,440,200,32), bottom-right (the text is in
  the texture, not font-drawn).

**STILL MISSING (user-flagged 2026-06-06):**
- **"Exchange with what?" world-projected prompt bubble** over the stand during the menu.
  Separate render off a **localized message-table** string — NOT an .exe literal (not found
  by string-scan of vendor/unpacked).  Shares the world→screen projection (FUN_00490c78)
  with the deferred C3b tooltip.  Needs: locate the drawer (a FUN_00490c78 caller in the
  cc04 render path — candidates near all.c:6902/6936/6963) + how the port loads the UI
  message string.
- **per-row type colours + price-status line (C4b-4c)** — `FUN_004361b2` (item price-trend)
  reads the daily-market region pricing tables (unported); type-0 items render white, which
  matches retail for the displayed (no-trend) swords.

**NEXT-SESSION CHIPS (recipes ready — pure execution):**
- **C3a orange cell glow** = `FUN_0045aa36` Block G (all.c L55089, the STUB in
  `scene1_chr_shadow.c`). 3D quad at the cell (`_DAT_0438cbf4`,1.9,`_DAT_0438cbf8`),
  item_win, pulsing alpha `ftol(sin(DAT_0438b8cc*0.05))`. **TODO:** objdump 0x45aa36
  for the `DAT_0064c388` vert UVs + world size.
- **C3b "Worn Sword" free-roam tooltip** = `FUN_00409925` L6425-6505 (the unported
  front of scene1_merchant_hud's source fn). Project the cell to screen
  (`FUN_00490c78`→`FUN_00490d29`: transform `(2*cbfc-9,1.9,2*cc00-6.5)` by
  `g_scene1_view`, then sx=320-fx·vx/vz, sy=240+fy·vy/vz, fx=proj[0]·320,
  fy=proj[5]·240 per `FUN_00490cc6`), draw the parchment bubble (item_win
  src(832,480,959,559) dst((sx-26),(sy-16),164,80)) + name via
  `font_draw_text_centered(sx+52, sy+26, name, …, 0.6)`.

#### C3b SCOPE — verified from the decompile/grep 2026-06-09 (NOT a quick port like C3a)
A multi-part chip with **two unported prerequisites** + objdump-still-needed colors:
- **(P1) world→screen projection `FUN_00490c78`→`FUN_00490d29` is UNPORTED.** The port has
  the `g_scene1_view` / `g_scene1_proj` matrices (`scene1_render.c`) but **no** project-point
  helper. Need a `scene1_project_world` (host-testable): transform the world point by
  `g_scene1_view` → (vx,vy,vz), then `sx = 320 - (proj[0]·320)·vx/vz`,
  `sy = 240 + (proj[5]·240)·vy/vz` (`FUN_00490cc6` derives fx/fy from proj). Reusable —
  other world-anchored HUD wants it. **Confirm the exact view/proj indices + the vx/vz sign
  via objdump 0x490c78/0x490d29/0x490cc6.**
- **(P2) name-text COLOR `local_20` = `FUN_004361b2(itemid)` is UNPORTED** (an existing
  PORT-DEBT, `scene1_display_menu.c:462/690` — "item price-trend, reads the daily-market
  region"). The classifier returns a level → one of 4–5 packed color dwords (the decompile
  shows them as reinterpreted floats `-3.39e38`/`-NAN`/`-1.70e38`/`-2.72e38`; **objdump
  0x409925 for the real dword immediates**). A faithful tooltip needs the right color, so
  either port `FUN_004361b2` or PORT-DEBT a default and expect the user to flag the colour.
- **Gate is RICHER than the one-liner above:** `b1c0==1 && *DAT_068dd2f0==0 &&
  (cc08==1 || cc08==0x32) && cbfc!=-1 && cc00!=-1`, then **`grid[cell]!=-1 || bf68!=0`**,
  splitting into an **item branch** (`bf68==0`: name text, COLOROP **ADDSIGNED**=8) and a
  **furniture branch** (`bf68!=0`: name + "%d/%d" slot count). `grid[cell]` =
  `*(DAT_044f7030 + (cbfc+cc00·20)·4 + slot·0x2dfc8)`. Bubble drawn by `FUN_00404efc`
  (=`render_quad_add`) then `FUN_00405354` (flush); **the bubble's 4th color arg was dropped
  by Ghidra → objdump 0x409925.** Name string built by `FUN_005038ff` (ported, scene_floor.c)
  from the item DB record `FUN_004681f6(id>>6)` (ported, tables_item.c); text via
  `FUN_0047d14c`=`font_draw_text_centered` (ported, font_draw.c), scale 0x3f19999a=0.6.
  For the `item-display-2` bench the player faces a back-row sword cell (cc08==1, bf68==0) →
  the **item branch** is what to port + verify first.

## SESSION 2026-06-09 — item-display-2 bench (pinned + call-traced) + 4-gap board + C3a FULLY spec'd

**Bench:** trace-studio session **`item-display-2`** (`http://localhost:8778/?session=item-display-2`)
— a user recording (load slot 2 → place 3 items → 2 back-to-back Tear tutorial dialogues).
Now **phase+RNG-pinned** (`{phasepin:0}`+`{rngseed:[0,19937]}` at the free-roam entry) and
**call-traced** (`{calltrace:[0,1850]}`); both sides cover the full window (port 1849 / retail
1842). With the pin the free-roam diff is **black except the real gaps** (bg-NPCs/sparkle 1:1).
Trigger CONFIRMED: tutorial dialogue #1 fires the instant the 3rd item is placed (3rd
`PAUSE_CLOSE`→`CONV_POSE_START` consecutive). The window inputs put the 3 display-stand
interactions at caprange frames ~121/381/587 on BOTH sides.

**User-flagged gap board (free-roam region, all confirmed from the clean diff):**
| # | gap | frame | chip / status |
|---|-----|-------|---------------|
| 1 | slot-highlight glow | f107 | **C3a — ✅ DONE 2026-06-09** (commit e25587c; verified below) |
| 2 | "What will you place?" prompt | f147/f391 | placement-MENU prompt bubble — **OPEN** (≠ C3b; the menu's own world-projected prompt, drawer + localized UI-string RE pending) |
| 3 | item name tooltip ("Worn Sword") | f183/f257 | **C3b — ✅ DONE + user-confirmed 1:1 2026-06-09** (tooltip band max 1/ch, 0 px>8; see below) |
| 4 | hands-up Recette anim / carry pose | f183+/f441 | placement-reaction + carried-item sprite — **OPEN** (the verdict's px/py/pz drift; held item red-vs-gold + pose) |

**Expanded interaction-flow board (user, 2026-06-09 PM — recapture of `item-display-2`):**
the C3b name-tooltip was only one slice; the full placement→tutorial flow has these OPEN gaps:
| gap | frame | what / panel | status |
|-----|-------|------|--------|
| **Tear tutorial dialogue ×2** | post-3rd-place + … | the headline — **PRIORITY**. #1 fires when the 3rd item is displayed; #2 fires when ALL items in possession are displayed (user "possibly — check retail to make it faithful"). Port shows NEITHER. | RE-from-retail next |
| "What will you place?" prompt | f391 (retail) | the placement-menu prompt speech-bubble (tail down-right); ≠ C3b | OPEN (gap #2) |
| menu panel slide-in anim | f122 (retail) | the placement menu slides up from the bottom; port pops/positions it without the slide | OPEN |
| selection flash | f172 (diff) | the menu's selected-row highlight bar diverges (a flash on select) | OPEN |
| placement dust desync | f272 (diff) | foot/placement dust particles desync after an item is placed | OPEN |
| carry pose / held item | f183+ (diff) | gap #4 above — carried-item sprite (red vs gold) + reaction pose | OPEN |

**Verdict finding:** `house_update.px/py/pz` DRIFT from f122 (the 1st interaction). db054 freezes
during the menu so part may be a pairing artifact, but it most likely IS the carry/reaction pose
(gap #4) moving the actor. Confirm via the captured call-trace before treating as a logic bug.

### C3b (item-name tooltip) — ✅ PORTED & USER-CONFIRMED 1:1 2026-06-09
Ported FUN_00409925's front (asm 0x409925-0x409cf0) as `merchant_hud_item_tooltip` at the top of
`scene1_merchant_hud_render` (it IS the front of that fn; the body = the level badge). New reusable
host-tested helper **`scene1_project_world[_mat]`** (`scene1_render.c`, port of FUN_00490c78→490d29:
`fx=proj[0]·320, fy=proj[5]·240` via TransformNormal((1,1,1),proj); `sx=320−fx·vx/vz, sy=240+fy·vy/vz`,
view-point via affine TransformCoord). Bubble = item_win src(832,480)-(959,559) dst(sx-26,sy-16,164,80)
diffuse 0xffffffff (the dropped 4th arg, objdump 0x409a32); name via `font_draw_text_centered(sx+52,
sy+26, name, color, 0.6)` under COLOROP ADDSIGNED→MODULATE; "%s" when `itemid&0xf==0` else "%s+%d".
**Verified** on `item-display-2` (port recapture): the "Worn Sword" tooltip band is **max 1/ch, mean
0.000, 0 px>8/ch** vs retail at f183 AND f257 — bit-clean; the only frame divergence is the carry-pose
(gap #4). **The key gotcha:** the faced cell's itemid is the **working save-bank DISPLAY grid**
(`save_work_dwords_at(slot)[SAVE_BANK_FIELD_DISPLAY_GRID + cbfc + cc00·20]` — what the sparkle/A2-removal
read), **NOT** `shop_display_grid_cell` (that's the FURNITURE-LAYOUT grid — reading it rendered a bogus
"Sword+2"). **PORT-DEBT(C3b-colour, FUN_004361b2):** the name colour (market price-trend level) is the
unported daily-market classifier (same debt the menu rows carry) → defaulted to level-0 neutral
0x7f7f7f. **PORT-DEBT(C3b-furn, FUN_00409925):** the bf68≠0 furniture-stand tooltip branch (name +
"%d/%d" slot count) is deferred — the bench faces item cells (bf68==0).

### C3a (slot-highlight orange glow) — ✅ PORTED & VERIFIED 2026-06-09 (commit e25587c)
Ported as `chr_shadow_build_display_glow` (pure, host-tested) + the Block-G draw in
`scene1_chr_shadow_render` (after the bg-NPC shadows, before the teardown). New accessors
`shop_display_render_x/render_z/bf68`. **Verified on `item-display-2` (port recapture --only
port):** the faced cell (4,4, holding the armor) now shows the orange glow; the studio diff
@ord107 glow-region (y380:475,x455:560) is **max 6/ch, mean 0.00, 0 px >8/ch** = bit-clean
vs retail (whole-frame residual 958 px>8 = pre-existing player/HUD, not the glow).
**Two constant corrections vs the original spec below (both verified by round-trip + the
asm immediates):** (1) the scale is **0.0036799998** (`0x3b712c27`), NOT 0.003685 — that
literal rounds to `0x3b712c28` AND `0.003685f`→`0x3b7180ca` (wrong); (2) the U texels are
**224.5 / 287.5** (centres), NOT 225 / 288 — `0x3e608000`=224.5/1024, `0x3e8fc000`=287.5/1024
(the V texels 480.5/543.5 were already right). Alpha consts 0.05/32.0/159.0 are all **float**
(`fmul/fadd DWORD PTR` @ 0x45b910/92a/930). The original spec follows (kept for the asm refs):

`FUN_0045aa36` Block G @ asm 0x45b8e0–0x45b94f (decompile L55089). A flat alpha-blended
`item_win` decal on the display surface over the faced cell. Port into the
`scene1_chr_shadow.c` Block-G stub (L347/L234), reusing its 3D-quad path.
- **Gate:** `cc08==1 && bf68==0 && cbfc!=-1 && cc00!=-1`.
- **Verts** (runtime-init all.c:9086–9105; .bss, stride 0x18 = pos[12]+diffuse[4]+uv[8],
  TRIANGLESTRIP 4 verts): XZ-plane y=0, local corners (±256). UV in item_win(1024²):
  V0(-256,0,+256)=(225,480.5) V1(-256,0,-256)=(225,543.5) V2(+256,0,+256)=(288,480.5)
  V3(+256,0,-256)=(288,543.5) — i.e. src rect (225,480.5)–(288,543.5), a 63² patch.
- **WORLD xform:** scale(**-0.003685, 0.003685, 0.003685**) then translate to
  (`_DAT_0438cbf4`, **1.9**, `_DAT_0438cbf8`) (the pre-computed cell render X/Z). 512·0.003685
  ≈ 1.887 world units; X is mirrored.
- **Texture** `DAT_073d8748`=item_win; **blend** SRCBLEND=SRCALPHA(5)/DESTBLEND=INVSRCALPHA(6).
- **Alpha (exact, objdump 0x45b902–0x45b94a):** `alpha = ftol( sinf(g_sim_frame·0.05)·32.0
  + 159.0 )`; diffuse = `(alpha<<24) | 0xffffff`. Pulses alpha **127↔191**. (The earlier doc's
  bare `ftol(sin(b8cc·0.05))` was missing the `·32+159`.) `g_sim_frame` = `DAT_0438b8cc` (the
  `sim_phasepin`-pinned counter — so the pulse phase is 1:1 on the pinned bench).
- **Draw:** `DrawPrimitiveUP(TRIANGLESTRIP, 2 prims, verts, stride 0x18)`.

## SESSION 2026-06-09 PM — Tear tutorial dialogues (RE COMPLETE, port pending)

The two back-to-back Tear dialogues the `item-display-2` bench was recorded for — **the
headline gap (user priority).** Probed entirely from the **retail call-trace** + the
**already-ported prologue dialogue** (per user steer: "probe retail with our tracing tools +
use what you already ported of previous dialogue"). The dialogue text was extracted from the
real game archive via `tools/extract/data-bin.py vendor/original/ <out>` (grep needs `-a` —
the `.ivt` are binary).

**Both dialogues run through the SAME interpreter the port already has** — `FUN_0046c320`
(dialogue_tick, ported as `scene1_dialogue_run` / driven by `scene1_intro_dialogue`). On the
retail trace `0x46c320` fires abs **15216→16397** (ord 649→1830), carrying box_open/reveal/
line_row; **two** line-row-0 starts (ord **770** + ord **1483**) = the two scripts.

**The two scripts (loaded `iv/iv%d_%d.ivt` by `(DAT_005c7a2c, DAT_005c7a30)` = scene/sub, the
exact path `scene1_dialogue_load` already builds):**
- **#1 = `iv1_5.ivt`** (scene 1, sub 5): *"Recette. May I speak with you a moment? … Those
  counters by the window… Items you place there are visible to anyone passing by on the street."*
- **#2 = `iv1_6.ivt`** (scene 1, sub 6): *"Alright. That should do for displaying our wares. …
  Are you sure it doesn't look dumb?"*
- (Related, already-fired-earlier: `iv1_4.ivt` (1,4) = the "crash course… put items on display"
  instruction.)

**The trigger chain (all unported):**
1. **Condition flags set at item PLACEMENT** — `house_update` `FUN_0048670f`, the display-menu
   confirm path `FUN_00469414()==1` (all.c:87932-87976, the place-item branch; the port already
   populates the grid so placement works, but does NOT set these flags):
   - **`DAT_0450f3fb = 1`** iff the placed cell is **row 0** (`DAT_0438cc00 == 0`, the window
     counters) → arms **iv1_5**.
   - **`DAT_0450f3fd = 1`** iff **all display cells are filled** — the 0x14-stride double-loop at
     87954-87972 counts stand cells (`DAT_074b28e8` value in (1,5)) vs occupied item-grid cells
     (`DAT_044f7030 != -1`); equal ⇒ every counter full → arms **iv1_6** (+ clears `DAT_0450f3f2`).
2. **Story-event dispatcher `FUN_0044bd0d`** (all.c:45406, 2723 B — the master tutorial/scenario
   scheduler; the port currently only realises the prologue (1,1)/(1,2) via `scene1_intro_dialogue`).
   Per-slot, when a condition flag is set and its done-flag is clear and `DAT_0438b1c8==0`
   (no dialogue active), it sets scene/sub + `DAT_0438b1c8=2` + `FUN_00452d07` and marks done:
   - **iv1_5**: `DAT_0450f3fb==1 && DAT_0450f3fc==0` (45666) → `(1,5)`, done `DAT_0450f3fc`.
   - **iv1_6**: `DAT_0450f3fd==1 && DAT_0450f3fe==0` (45677) → `(1,6)`, done `DAT_0450f3fe`(+`f3ff`).
3. **Activation** sets `DAT_005c7a2c/30` (scene/sub) + `DAT_0438b1c8=2`; the interpreter
   (ported) loads `iv/iv1_5.ivt` / `iv/iv1_6.ivt` and runs. The conversation also poses the
   chibis via the already-RE'd `FUN_0048407f` conversation-pose driver (talk flag `DAT_0450f470`,
   beat-driver `FUN_00470a46`).

**Port plan (chips):** (D1) set `DAT_0450f3fb`/`f3fd` at the port's placement confirm (mirror
87949-87975 incl. the all-cells-filled count); (D2) a focused event-dispatcher (the iv1_5/iv1_6
branches of `FUN_0044bd0d` + done-flags, persisted per-slot) that activates the dialogue; (D3)
extend `scene1_intro_dialogue` (or a sibling tutorial-dialogue driver) to load+run `(1,5)`/`(1,6)`
on activation — it already does `(1,1)`/`(1,2)`. Verify on `item-display-2`: the dialogue box +
Tear/Recette should appear at ord ~770 (iv1_5) and ~1483 (iv1_6), matching retail.

### PORTED & VERIFIED 2026-06-09 (D1–D3 landed)

Both Tear dialogues now fire AND render on `item-display-2` — the port's dialogue-anchor
structure is **identical to retail**: `CONV_POSE_START` 2=2, `LOADING_START` 3=3 (initial +
iv1_5 + iv1_6), `TEXT_ANIM_START` 18=18 (iv1_5 12 lines + iv1_6 6), `DLG_LINE_CLEAR` 8=8.
iv1_5's first text line is at **port ord 769 vs retail 770** (1:1 trigger). The studio frame
shows the box + Tear/Recette portraits + "Tear" nameplate + ESC-skip prompt rendering.

- **D1** (`scene1_player_ctrl.c`, `player_ctrl_cc04_menu_arm` confirm path): the
  `DAT_0450f3fd` all-displayed latch (all.c:87952-87976) — count layout-grid stands {2,3,4}
  (`shop_display_grid_cell`) vs occupied item cells; gated on a non-empty inventory
  (`SAVE_BANK_FIELD_ITEM_COUNT`=`DAT_0450f2b0`), clears `f3f2`. `f3fb` (row 0) was already set.
  **Engine-quirk:** on the bench `f3fd` fires via the **`item_count==0` gate** (the player
  places their *last* item → inventory empty → the count is skipped and the latch set
  trivially), NOT the all-filled stands==occupied count (the shop has 12 stand cells, only 3
  items). Retail's gate is identical, so this matches.
- **D3** (`scene1_intro_dialogue.c/.h`): `scene1_intro_dialogue_start_single(scene,sub)` runs a
  single arbitrary script through the **shared `g_rt`** (retail's one dialogue runtime) via a new
  `D_TUT` state, preceded by a `D_TUT_LOAD` **load bracket**. The bracket is REQUIRED:
  retail's `FUN_00452d07` spawns the `LAB_00452aab` worker → `LOADING_START`/`CONV_POSE_START`/
  `LOADING_END` (iv1_5 abs 15213→15215), and the trace's per-line-advance Z inputs are gated
  *after* `{wait LOADING_END}` — without the bracket those waits never match, the Z inputs
  starve, and the dialogue stalls on line 0. `_done()` is now a sticky `g_freeroam_started`
  latch (post-prologue tutorials must not re-fire `FREEROAM_START`). New `_busy()` (retail
  `DAT_0438b1c8 != 0`) and `_posing()` accessors cover the `D_TUT_LOAD`→`D_TUT` lazy-load seam.
- **D2** (`scene1_tutorial_dispatch.c/.h`): focused port of `FUN_0044bd0d`'s iv1_5/iv1_6 branches
  (all.c:45664-45688) — **RNG-neutral** (the scheduler consumes zero shared LCG; `FUN_00452d07`
  only sets the gate + spawns the worker). Pumped after the player ctrl in the INGAME default
  arm (`scene1_ingame_default_arm_tick`, retail 0x40849). **Gated on `_busy()`** — the obvious
  `_active()||_loading()` gate has a 1-frame hole at the lazy-load seam (loading already false,
  active not yet true) through which iv1_6 fired and **clobbered iv1_5** (only one dialogue
  played, both done-flags set). `PORT-DEBT(focused, FUN_0044bd0d)`: the outer
  `f454`/`f455`/`fb88` gates + every other scenario branch are not ported.

**Open follow-ups (NOT the dialogue gap, which is closed — user-flagged 2026-06-09 PM on the
recapture; the dialogues "play out correctly… huge progress"):**
1. **Per-line advance cadence — ✅ FIXED 2026-06-09 (commit a8269f6), now frame-EXACT 1:1.** The
   port cleared a line early (ord 834 vs retail 874, ~40f over the first line), desyncing the
   subsequent lines under the held-Z (0x20) fast-forward. **The dialogue cadence itself was never
   the bug** — all of it is faithful: the internal-step count (`DAT_005c78ec=2` for held 0x20),
   the per-step reveal++/dwell++, the slam conditions, AND the **waitkey gate** `0x46d93c`
   (disassembled: `dwell≥0xf` AND (`held&0x60` → ret 2, OR `edge&0x10` → SE 0x144 + ret 2); the
   port's `op_msg_waitkey` matches exactly — the opening-prologue.md note that omitted the
   held-0x60 path was just incomplete). The ~40f came from the **CONV_POSE_BLINK anchor that gates
   the trace's advance input**: the port blinked every **40** frames, retail every **64**, so the
   `{wait CONV_POSE_BLINK}`-gated held-Z engaged ~40f early.
   - **Root cause (call-trace probe at the pose tick, VA 0x484080).** The blink rides the player
     actor's **anim 6** (look-up-at-Tear, `FUN_0048407f`; cycle `38·d20 39·d6 38·d32 39·d6` =
     **64**). The LUT durations were correct (probe: d0=20 d1=6 d2=32 d3=6, marker −1). But the
     player was animating **anim 0 (idle, 4 frames × 10 ticks = 40)** — `panim==6` survived in only
     **2 of 1084** pose frames. Those 2 frames are the `D_TUT_LOAD→D_TUT` **lazy-load seam**
     (`_loading()` just dropped, `_active()` not yet up — only `_posing()` spans it): the free-roam
     **walk arm ran on that seam frame** (gated `!_active() && !_loading()`, both false) and reset
     `anim 6→0` (its idle↔walk transition branch); `conv_pose_enter` keys its restore on **STATE**
     (still 6), so the anim never recovered → idle loop for the whole dialogue.
   - **Fix:** gate the walk arm (and its 0x48670f flow-trace emit) on `!scene1_intro_dialogue_posing()`
     too, so the walk never runs while the conversation pose owns the player. Retail has no such
     seam — its walk gate tracks the cc08 event state (= the talk flag), which transitions with the
     pose. **Verified:** port blink period → 64, blink times bit-identical to retail
     (`-102,-38,26,90,154,…`), **all 44 per-line anchor pairs (TEXT_ANIM_START/END/DLG_LINE_CLEAR)
     Δ=0** vs retail on item-display-2. Recette now plays the look-up blink, not her idle loop. No
     prologue/free-roam regression (`_posing()` is already covered by `!_active()` there; the walk
     gate is unchanged outside a pose). Host suite 3222✓.
2. **bg-window NPCs desync after the dialogue starts — ✅ FIXED 2026-06-09 (commit 843b6f1),
   rngcalls net +1375 → +31.** NOT the standee shake. Root: **the port ran the DEFAULT arm
   (FUN_00442cef) during dialogues; retail dispatches every `DAT_0438b1c8`-busy frame to the
   EVENT arm (FUN_004427d3)** — verified on the retail call-trace (0x4427d3 + 0x48407f + 0x46f621
   once per dialogue frame, zero 0x442cef/0x48670f rows). The default arm's dev-overlay LCG step
   (+1/frame, §95) and the 目玉-sparkle emitter (+6 per 8 frames over the 6 occupied cells) were
   shared-LCG consumption retail doesn't have during a dialogue ⇒ +3245 over the two dialogue
   windows, net +1375 after the retail-lead from its real D_TUT load. Fix: `scene1_ingame_tick`
   ORs `scene1_intro_dialogue_busy()` into the paused-arm condition (the port had modeled b1c8
   TWICE — a never-written `g_scene1_ingame_paused_flag` + the dialogue lifecycle); the event arm
   is now pose tick + `scene1_event_actor_tail_tick` (bg-NPC pump + companion spring/wing +
   **unconditional db054++**, retail ground truth db054=1205 at the inter-dialogue gap) +
   records-B + particles. Port dialogue-window consumption now **+6 every 4th frame (wing), else
   0 = 1.52/frame ≡ retail's 1.51**. Same chip fixed the **db054 menu-close off-by-one** (the
   companion fallback re-read cc04 AFTER the menu arm cleared it mid-frame → +1 db054 per pause;
   retail only advances when the frame *dispatched* outside the menu) — that skew was the entire
   phantom `house_update.px/py DRIFT @202` (gap F's sim half: px bit-identical under 1-frame
   shift) AND the dust-field DRIFT @209 (gap E): post-fix px/py/dust/db054/cbfc all
   ALIGNED/bit-exact. **Residual ±31:** boundary-frame artifacts at menu open/close + the load
   brackets — +6 per pause whose freeze lands on db054%4==0 (one extra wing emit on a boundary
   frame; both sides fire the wing EVERY menu frame when frozen at %4==0 — retail's `0xcf05d33`
   LCG attribution during pause2 = its wing through the hooked thunk) and +25 across the
   D_TUT-load/dialogue seams (inter-dialogue default frames + pause-3 boundary). Chase with a
   dense per-frame drill over one menu open/close if it ever matters. Companion
   `cx/cz (spread ~0.003)/canim/cframe/pcnt` micro-DRIFT around menu boundaries is the same
   boundary-frame family (who ticks the companion/interact-pose anim on the open/close frame) —
   menu-arm chip scope.
3. **Text reveal GRADIENT-to-transparent — ✅ PORTED 2026-06-09** (user: "long overdue").
   Mechanism: `FUN_0047d464` (the glyph-row drawer, port `dialogue_draw_row`) fades EACH ROW's
   glyph alpha by `input_alpha * clamp(param_6 * DAT_5198d8, DAT_519364)` where `param_6` = that
   row's char budget, `DAT_5198d8` = **0.2** (`0x3e4ccccd`), `DAT_519364` = **1.0** (clamp ceiling)
   — verified by objdump (`0x47d528 fildl -0xc` = `param_6`, set once at `0x47d4d4`, so it's
   **per-row, not per-char**). So a newly-revealing row ramps transparent→opaque over its first
   ~5 revealed chars; a settled row's budget is large (reveal climbs to 0x800) ⇒ full alpha.
   The "edge sinf" path I first guessed (`FUN_0046c9a2` `FUN_00503a44`) is a *full-screen* fade
   effect (`DAT_073a3df4`-gated), NOT the text. Ported into the `scene1_dialogue_draw.c` row loop
   (`fade = min((int)budget·0.2, 1.0)`, alpha = `(color>>24)·fade`). Verified on item-display-2:
   port f1196 line-start text dim, f1260 settled text full alpha.
4. **Placed-item ids wrong on the place path.** `display_menu_selected()` (`FUN_00469a9f`)
   returned 64 / 64064 / 256512 on the 3 placements — the cc04 confirm path was written/tested for
   the `sel==-1` *removal* roundtrip; the *placement* selection isn't fully ported, so the
   newly-placed display items render wrong. Doesn't affect the dialogue triggers.
5. **Missing "bread" item tooltip during the dialogue** (user, retail ord 854 box
   467,276,726,448). Likely the C3b `merchant_hud_item_tooltip` (faced-cell item name) — either
   suppressed while a dialogue is active, or showing the wrong/garbage placed item (#4). Compare
   the port vs retail frame.
6. **Dialogue box/portrait pixel-parity** vs retail (filtering/colour) — a visual pass once the
   cadence (#1) is settled.
