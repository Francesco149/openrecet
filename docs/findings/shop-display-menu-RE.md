# Shop-display "remove item" menu — RE (ground-truth corrected 2026-06-06)

**Status:** RE re-derived from a live retail flow-trace of the actual
save-roundtrip removal interaction. **This supersedes the original
`plans/shop-display-roundtrip.md` RE map for Phase A — that map described the
WRONG mechanism** (the `cc08` 0xa context-menu + `FUN_0048940e` grid-swap +
`FUN_004850fe` populate). None of those run during the real removal.

## How it was found
- New bench `tests/scenarios/house-display-remove` — the save-roundtrip inputs
  verbatim through the **first** sword removal (load fa7c82 → walk to the
  back-stand swords → Z → "select none" → Z → close). Anchors: `LOADING_END`
  → `PAUSE_OPEN` (menu open) → `PAUSE_CLOSE` (menu close).
- Added interaction-state globals to the retail `0x48670f` Frida hook
  (`tools/flow/retail_fields.json`): `cc08 b74c b754 cc0c cd0bc bea4 cbfc cc00`.
- `scenario-test house-display-remove --target retail --call-trace` →
  `runs/scenarios/house-display-remove-retail-*/call_trace.jsonl`. Read the
  per-frame `0x48670f` field rows + the called-VA set during the menu window.

## Ground-truth findings (authoritative — from the live trace, not decompile)
1. **`DAT_0438cc08` stays `1` (free-roam) for the ENTIRE interaction.** The
   removal is NOT a `cc08` state transition. `b74c/b754/cc0c` stay `0`; `cd0bc`
   is a constant `7` (already populated, irrelevant here).
2. **`FUN_0048670f` is called every frame throughout** (no gaps 14586→14848) —
   the HOUSE controller keeps running; the player is just frozen in an
   interact-pose (`panim=3`) and **`db054` (the §85 phase clock) FREEZES** at
   157 for the whole menu (14703→14778): the menu pauses the HOUSE sim while
   `FUN_0048670f` itself still ticks.
3. **The `DAT_074b2ec4=1` / scene-8 path is NOT taken** (`FUN_004528b3` never
   called) — that's the *counter / customer-sell* shop, a different thing.
4. **`FUN_0048940e` and `FUN_004850fe` are NEVER called** — the plan's grid-swap
   + context-menu-populate are not part of this interaction at all.

## The real mechanism — the in-house display menu (`DAT_0734bxxx` subsystem)
All call sites are **inside `FUN_0048670f`** (it ends at `0x48940e`), reached on
the free-roam path while `cc08==1` (the precise gating goto is in the
`0x488xxx–0x4894xx` tail; Ghidra labels the enclosing region `cc08==0x32` but
the live `cc08==1` read says that label/branch attribution is not what actually
gates it — **pin the exact gate at the address when porting**, don't trust the
decompile's `cc08==` attribution here).

| addr | func | role |
|------|------|------|
| `0x488d8a` (in `0x48670f`) | calls **`FUN_00468338`** (0x468338, 2490B) | menu OPEN/dispatch — fired ONCE on the Z-press frame (14703) |
| `0x48915f` (in `0x48670f`) | calls **`FUN_00469414`** (0x469414, 1516B) | menu UPDATE — every menu frame; returns a code |
| — | **`FUN_004693e3`** (0x4693e3, 41B) | open/close slide: `DAT_0734b98c` ramps 0↔5 |
| — | **`FUN_0046b00a`** (0x46b00a, 3640B) | menu RENDER (2D quads via `FUN_00404efc`); early-outs on `DAT_0734b98c==0` |
| — | **`FUN_00468ddc`** (0x468ddc, 303B), `FUN_00469a9f`, `FUN_00468d22`, `FUN_00469241` | inventory query / selected-item / inventory-return / place helpers |

### State globals (`DAT_0734bxxx` + a few `DAT_0438bxxx`)
- `DAT_0734b98c` — open-slide counter (0=closed … 5=fully open). Gates render.
- `DAT_0734b968` — menu cursor (nav: `b968 = (b968 ± 1) % DAT_0731f404`, FUN_00469414).
- `DAT_0734b9a8` — submenu/mode id (`==6` is a distinguished mode).
- `DAT_0731f404` — current item/option count.
- `DAT_074b2ed8` — selected display SLOT; `DAT_0438bf64` — furniture/row base.
- **Display grid** = `DAT_0450ff30`, cell index `DAT_074b2ed8 + DAT_0438bf64*0x14`
  (15-wide rows). **NOT `DAT_044f7030`** (that was the plan's wrong array).

### The removal ("select none") — `FUN_0048670f` @ ~all.c:87879
```
iVar7 = FUN_00469414();              // menu update; 1=confirm-select, 2=stay, 3=cancel
if (iVar7 == 1) {                    // player confirmed a slot
  DAT_074b2ed4 = 0;
  item = FUN_00469a9f();             // the chosen item id; -1 == "none"
  if (grid[ed8 + bf64*0x14] != -1)   // slot currently occupied
    FUN_00468d22();                  //   → return the displaced item to inventory
  DAT_0438b67c[ed8] = item;
  grid[ed8 + bf64*0x14] = item;      // ← write -1 here == REMOVE the sword
  if (item != -1) FUN_00469241();    // (placing a real item)
  FUN_00468338(0,0); FUN_004682e3(); // close + re-open the menu
}
```
So **"select none" = `FUN_00469a9f()` returns -1 → the display grid cell is set
to -1 (item removed) and the previous item is returned to inventory via
`FUN_00468d22`.** This is the save-relevant state change the roundtrip persists.

## Entry trigger (free-roam → menu)
Z (action, input `0x10`) while facing/adjacent to a stocked display opens the
menu: `FUN_00468338` runs once (14703) from `FUN_0048670f@0x488d8a`, the slide
`DAT_0734b98c` ramps to 5, and the `PAUSE_OPEN` anchor fires (the menu sets the
same modal flag `DAT_0438b150` the recorder watches — that's why the recorder
labels the display menu `PAUSE_OPEN`, NOT the START pause). **The exact
free-roam affordance/gate that routes the Z-press to `0x488d8a` is the one
remaining thing to pin at port time** (read the decompile at `0x4886xx–0x488d8a`).

## Implications for the plan
Phase A chips must be rewritten around this subsystem:
- **A1** open trigger (free-roam Z near a stocked display → `FUN_00468338`) +
  `DAT_0734b98c` slide + the menu-active freeze of the HOUSE sim.
- **A2** `FUN_00469414` update (cursor nav `b968`, the 1/2/3 return code) +
  `FUN_00469a9f` selected-item + the grid write / `FUN_00468d22` inventory
  return (the actual removal).
- **A3** `FUN_0046b00a` render (the menu panel + item list + cursor).
The `cc08` 0xa / `FUN_0048940e` / `FUN_004850fe` items in the old plan are the
**counter/customer-sell** menu — a different, later feature; keep them for that.
