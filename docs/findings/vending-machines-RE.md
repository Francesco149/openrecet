# Vending machines (FUN_0048670f cc04 1 / 2, merchant level 15+)

> **Status:** ported 2026-11-10 (`src/scene1_shop_display.c` `shop_vender_*`, `src/scene1_player_ctrl.c`,
> render `src/scene1_hud.c` / `src/scene1_walker_pass_init.c`). Retail facts below: IDA `all_ida.c` +
> objdump of `recettear.unpacked.exe`, checked live under rhost / `rd`
> (scenario `vending-level16`, rd runs `vendfix`, `vendfix2`). What no recording reaches is marked **asm only**.

A vending machine is not an object of its own: it is a TABLE whose flag dword is set. Earlier docs call that
dword the "furniture-suppression flag" (`DAT_0450fee8`, `SHOP_DISPLAY_SUPPRESS_FLAGS`): it is the vender's
item handle.

## Data (work bank, VA = 0x44e3798 + slot·0x2dfc8 + offset)

| offset | dword | what |
|---|---|---|
| +0x2c750 + 4·t | 0xb1d4 + t | table t's vender handle: 0 = an ordinary table, else `id << 6` (0x51440 Old Vender 5201, 0x51480 5202, 0x514c0 5203). 20 dwords. |
| +0x2c7a0 + 80·t + 4·k | 0xb1e8 + 20·t + k | machine t's stock, slot k (0..19, −1 empty). One UNIT a slot (a handle, no count). |
| +0x13898 + 4·(col + 20·row) | 0x4e26.. | display grid. The faced cell of a machine's table holds the vender's own handle; the table's other cells are −1. |
| +0x2ce10 + 8·t | 0xb384 + 2t | table origins (unchanged by a machine). |
| +0x2c400 | 0xb100 | merchant level, 0-based (HUD level − 1). |

Menu state (not saved): `DAT_074b2ed4` (0 = the machine's rows, 1 = the item list is picking a slot's item),
`DAT_074b2ed8` (row), `DAT_074b2edc` (first row shown; 6 a page), `DAT_074b2ee0` (last row),
`DAT_0438bf64` (the table), `DAT_0438bf68` (faced table + 1 when it is a machine, else 0 — FUN_0048619f;
the tooltip FUN_00409925 reads it).

Slots offered = `DAT_074b2ee0 − 1`: handle `& ~0x3f` 0x51440 → last row 7 (6 slots), 0x51480 → 9 (8),
anything else → 11 (10). The tooltip's "(n/cap)" uses the same 6 / 8 / 10 (n = non-empty among the 20).
8 / 10 slots: **asm only** (only the Old Vender is sold on the probed days).

## Placing (cc04 1 confirm, asm 0x4891f8..0x4893a0)

FUN_00469414(1) → 1 with an item picked or the faced cell occupied: work +0x2bc60 = 1, `rand() & 1` picks
SE 05a / 05b (both paths). Then the picked item's id (`DAT_095d3804[179·FUN_004681f6(item >> 6)]`):

- outside 5201..5299 → the ordinary swap (cell = item, FUN_00469241, FUN_00468d22(old), FUN_0048439a
  centroid, back-row byte, the all-filled latch).
- 5201..5299 (a vender): count `n` = non-zero flag dwords among the `DAT_0438bfb4` tables; `lvl` = +0x2c400:
  - `lvl < 14` → FUN_00434ceb("Vending machines cannot<BR>be placed at our level.") (0x5cf0f8);
  - `lvl < 21 && n >= 3` → FUN_00434ceb("We are at our level's vending machine limit.") (0x5cf12c) — **asm only**
    (needs a 4th table: shop tier ≥ 1);
  - else `t = FUN_004860c8(cbfc, cc00)`; FUN_004865d5(t) (every item on the table back into the bag, footprint
    order: 2×2 row-major / 4 along x / 4 along z); flag[t] = item; grid[cbfc, cc00] = item; FUN_00469241(item).
    No centroid recompute, no back-row byte, no latch.
- either way cc04 = 0, cursor off, the window slides out; then FUN_004897c6 + FUN_0048a833 (the cc04 1 tail).

Limits: level dword ≥ 14 (HUD 15) to place; at most 3 machines below level dword 21 (HUD 22), then as many as
tables. Retail trace: f5465 of `vending-level16` — bag 33 → 36 (4 back, 1 out), flag[0] 0 → 0x51440.

## Facing a machine (cc04 0 gate, asm 0x488c1d..0x488cc9)

Order of the A (0x10) press: door → Tear in reach → sell counter → `t = FUN_004860c8(cbfc, cc00)`,
flag[t] != 0 → the machine's menu; only then the display-stand menu's gates (+0x2bc5a, +0x2bc68, cbfc != −1).
FUN_004860c8 returns −1 with no cell faced: the flag read is then the dword BEFORE the array (+0x2c74c, 0 in
every save seen). Y (0x40) facing a machine does nothing (its item-status gate wants flag == 0).

Open: cc04 = 2, the cbe8 open-once latch as for the display menu, FUN_00468338(0, first), row 2 (slot 1),
top 0, last 7 / 9 / 11, `DAT_0438bf64 = t`, SE 0x143, hand cursor slides to (16, 40·(row − top) + 124)
(FUN_00435710 — LABEL_673, used after every row move).

## The machine's menu (cc04 2, asm 0x488dda..0x489153)

No FUN_004897c6 / FUN_0048a833 on its frames (the cc04 1 arm has them): straight to LABEL_675.
Rows: 0 "Revert To Table", 1 "Done Stocking", 2.. slot (row − 1) → stock[row − 2].

`DAT_074b2ed4 == 0`, in this order:
1. item detail open (`DAT_0734b96c`): A / B / Y closes it (cursor on, SE 710); nothing else is read.
2. B: cc04 0, cursor off, window out, SE 0x13d.
3. Y on a stocked slot: `DAT_0734b96c = 1`, `DAT_0734b998 = ` the item (no recount), cursor off, SE 710.
4. A: row 0 → FUN_004865d5(`DAT_0438bf64`) (returns the vender itself: its handle is the table's display
   cell), the 20 stock slots back into the bag in slot order, flag[FUN_004860c8(cbfc, cc00)] = 0, close,
   SE 0x143. Row 1 → close, SE 0x143. A slot → `DAT_074b2ed4 = 1`, FUN_0046939a (cursor onto the item
   list), SE 0x143.
5. else the REPEAT mask `DAT_073dddd6`, one else-if chain: up 4 (row−1; top−1 when the row leaves the window),
   down 8 (row+1 while < last; top+1 when row − top > 5), left 2 (−6, both clamped at 0), right 1 (+6, row
   clamped at last, top at last − 5 — the top clamp also runs when the row was already the last). SE 0x146 a
   move. Then FUN_00468286(stock[row − 2], or −1 on rows 0 / 1): highlight + "Number possessed".

`DAT_074b2ed4 == 1`: FUN_00469414(1) → 2: back to the rows (SE 0x13d, cursor slides back); 3: SE 0x143;
1: the slot's item back into the bag (FUN_00468d22), slot = FUN_00469a9f (−1 "Nothing" empties it), the pick
out of the bag (FUN_00469241), FUN_00468338(0, 0), FUN_004682e3 (window shown at once), cursor to the row.
Quirk: the pick is also stored at `DAT_0438b67c[row]` (engine-quirks §166).

## Render

- FUN_0040a765 (asm 0x40ae29..0x40b0a9), scene 1 + house + cc04 2, after the top HUD and before the item
  window: 6 plates item_win src (400,240)-(639,303) at (16 [+16 on the cursor row], 104 + 40r, 240×44), alpha
  255 / 155; arrows FUN_0048edee(16, 92) when top > 0, FUN_0048ee77(16, 352) when top < last − 5; texts at
  y 40r + 118: "Revert To Table" / "Done Stocking" x +64 scale 0.923, "%2d:" x +28 scale 0.769, the item row
  FUN_0046add8 at (x + 56, 40r + 110).
- FUN_00457714: a machine's mesh = `DAT_068dcca0[type + 2·(handle >> 6) − 0x28a0]` = the house stage's map
  meshes 5..10 (xfile/jihanki: 2×2 and 1×4 of venders 5201 / 5202 / 5203), drawn with the table's matrix.
  Type-4 tables of handle 0x514c0 get an extra `RotY(π) × T(0, 0, 2)` — once at rotation 0; at π/2 once,
  and again when the table's x > 5; otherwise only when x > 5 (asm 0x457e8f..0x457fc7) — **asm only**.
- FUN_004161c7 still draws the display-grid cell's icon (the vender's own) inside the machine.
- FUN_0048670f prologue sparkles: a back-row cell on a machine table does not sparkle (flag gate first).

## Evidence (2026-11-10, rhost Windows)

- `vending-level16` (place on the 2×2, open, stock one, B): triage identical 1..5922; portblock IDENTICAL.
- rd run `vendfix` (from the end of it: detail, restock, "Nothing", paging, held repeat, Done Stocking,
  Revert To Table, place on the back-row 1×4, Y facing it, stock, walk off): identical 1..6564; 74 VAs over
  965 frames + 18 portblock frames IDENTICAL; portshots mean abs ≤ 0.01.
- rd run `vendfix2` (crafted: level dword 13 + a vender in the bag): the level refusal box; identical 1..953.
- `vender-sales-l16` (another agent's probe: stock 4 slots, open the shop, two machine sales, night, save):
  identical 1..9596, save IDENTICAL after the FUN_00460f59 fix (engine-quirks §167); gold / stock / sold-list
  VAs + 7 portblock frames IDENTICAL. `vender-revert-sell` (revert, sell the vender at the guild, save):
  identical 1..1869, save IDENTICAL.

## Sales out of a machine (FUN_00461068, `cs_shelf_sales`)

Runs from the counter's idle beat (b524 == 0x14, odd b528, not the scripted sell). Tables in a shuffled order
(FUN_0045e505 over 100 indices, `DAT_0438bfb4` of them used); a table that is a machine draws one candidate
among the flagged customers (`rand() % count`), whose half budget (FUN_0045ecc0 / 2) must exceed the base
price of one of 3 of 10 shuffled slot indices (only slots 0..9 are ever looked at); ids 3000..3099 pass 1 in
10, a falling price trend (FUN_004361b2 ≤ −1) 1 in 10. A sale: gold += the item's BASE price (no haggle),
FUN_00460083(item, 0), FUN_00460f59 (§167), slot = −1. A machine that was visited counts toward the cap of
(flagged customers with score ≥ 65) / 2 visits a call; SE 0x14d if anything sold.
