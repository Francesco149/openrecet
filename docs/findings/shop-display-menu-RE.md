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
