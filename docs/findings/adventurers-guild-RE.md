# The Adventurers' Guild (engine mode 11) — RE + port

World-map dest 6 → `FUN_0049e163` sets `DAT_0438b1c0 = 11` (no scene-init call; the
world-map exit's FUN_00436f97 / fade / load spawn are shared with every dest). Port:
`src/scene_adv_guild.c` (2026-10-14).

## Load (worker case 11)
`FUN_0045bdc2` + `FUN_00473874` (textures, group 7: `bmp/boukenguild_bg.bmp`,
`brave_sel.tga`, `dungeonmap.tga`, `dungeonbord.tga`, the 8 `st_*.tga` portraits).
`FUN_0045bdc2`:
- `FUN_00434ce3` (message box shut), `DAT_06a49b6c` (ticks) = 0;
- `FUN_0047f172(0)`: the whole work bank → backup bank 0 (`DAT_0741bee0`), the expedition's
  restore point (unported: only the dungeon reads it back);
- work `0xb381` = 1; `DAT_0438b8a4` = 0 (`FUN_0044c7bb`);
- bag size `DAT_005c5a98` = 20 / 25 / 30 / 35 at merchant level < 16 / ≥ 16 / ≥ 23 / ≥ 39
  (`.data` value 38 before the first load);
- the bag `DAT_0438b5ec[38]` and its twin `DAT_0438b684[38]` = −1;
- cursors from the last expedition: dungeon `DAT_06a49b50` = work `0xb37f`, adventurer row
  `DAT_005c5a9c` = the row of work `0xb37e` in the party list (`FUN_0047f1ce`; the last
  match wins, 0 = "Leave" if none), list scroll = row − 4 when row > 4; bag row 1;
- start floors per dungeon `DAT_06a49b2c[i]` = (deepest floor `0xb101+i` + 1) / 5 + 1,
  capped by `DAT_005c64c0` = {1,3,6,10,20,12} (dungeon 5: cap 4), +1 when the
  floor-group flag `0xb10a + 20·i + n` is set (i > 0);
- `DAT_0438b4c8` = 0; dungeon count `DAT_06a49b74` = 2..6 from bytes `0x2bcb9`, `0x2bce1`,
  `0x2bcf3`, (`0x2bc53` && `0x2bd00 == 1`).

## The party list `FUN_0047f1ce`
`DAT_0741bed8` = count, `DAT_07477e74[]` = ids 0..7 in id order: adventurer k is listed
when byte `0x2bc4c+k` or `0x2bd18+k` is set. `DAT_07477e70` (never written, 0) is read as
the 1-based view: row 0 = "Leave". The pause build `FUN_0047f2f6` calls it and lists
"Status" iff count > 0 — after the guild intro (Louie joins) the pause menu has one more
row. Port: `src/party_roster.c`, called by the pause build, the item window open and here.

## Update `FUN_0045c051` (LABEL_106, only while `DAT_0438b1c8 == 0`)
Order: the intro `FUN_0044c7c3` → the "Pick the items" box (`DAT_06a49b70` 1 → open via
`FUN_00434ceb`, 2 → wait for `FUN_00434d6a` == 1) → `FUN_00434d6a` == −1 returns → on
tick 0 the too-many-items box (`FUN_00491b16` > 14970), on tick 1 a forced Leave → then
the first matching arm:
- **commit** `DAT_06a49b44` (1 depart, 2 leave), once `FUN_004528b3` (fade done): the
  hired id → `DAT_0438b7d8` + `FUN_004844ef`; `DAT_0438b4c8/4cc/4d0/4d4/4d8` from dungeon +
  floor (past the regular start floors: dungeon 1..4 → floor 30/33/38/46 with
  `DAT_0438b4c8 = 5`, `DAT_0438b4d0 = dungeon`); work `0xb37f/0xb380/0xb37e` = dungeon /
  floor / id; `DAT_0438b928 = 0`. Leave: the bag goes back to the inventory, mode 8, map
  cursor on dest 6. Depart: `0x2bce2` = 1 on the first dungeon-3 top-floor trip, the bag
  compacted (bag-size passes of a shift-left), `FUN_00435c98`, period `0xb0fc` +1, mode 1,
  `FUN_0045e028`, `0xb37d` = 1, the fee off the gold (when byte `0x2bc81`), trips
  (`rec[20]`) +1. Both: `FUN_0045bcea` (`DAT_0438b4dc`), `FUN_0044bce7`, fade-in, load.
- **floor list** `DAT_06a49b4c`: X back, Z depart, up/down (7 visible).
- **dungeon map** `DAT_06a49b48`: X back to the bag, Z opens the floor list (on the last
  expedition's start floor when it is the last expedition's dungeon), left/right walk 0..min(count−1, 4), down → dungeon 5
  when 6 are open; from 5, up/left/right → 2/1/3.
- **item picker** `DAT_06a49b60 == 2`: `FUN_00469414(1)`; a pick swaps the bag slot with
  the inventory (`FUN_00468d22` / `FUN_00469241`) and re-opens the list `FUN_00468338(0,0)`.
- **adventurer list** `DAT_06a49b5c`: X or Z on row 0 → Leave; Z on an adventurer checks
  the fee (gold < fee and fees on → SE 362, except row 1), then opens the bag
  (`FUN_00468338(0,1)`, `FUN_00468233(id,−1)`, `FUN_004682ee(id)`) and the "Pick the items"
  box.
- **the bag** `DAT_06a49b60 == 1`: the item-detail popup (`DAT_0734b96c`) eats 0x70; X back
  to the list; C opens the detail; Z on row 0 departs (fees off: straight into dungeon 0;
  else the map), Z on a slot opens the picker; up/down ±1, left/right ±6 with retail's
  scroll clamps.
Tail: `DAT_06a49b78` = the hired adventurer's filled equipment slots (`rec[1..10]`), ticks +1.

Fee: `DAT_005cc680[id]` (128 B records; IDA labels it `aLouie`, the record's name field):
`+0x28` base {50,100,200,300,500,500,1000,2000} + `+0x2c` 100 · level, halved (signed) at
merchant level ≥ 20.

## The intro `FUN_0044c7c3`
Byte `0x2bc7c` == 1 (set by the story) arms it: `0x2bc7d` → iv2_7, then `0x2bc7e` (+ `0x2bc7f`
exit latch + `0x2bc4c` Louie joins) → iv2_8, then clear `0x2bc7c`/`0x2bc7f` and return 2: mode
8, map cursor dest 6, `FUN_00473969`, fade-in, load. Both scripts start with
`FUN_00452d07(1)` and `FUN_00452809` (blackout). rhost `buy-from-seller-guild-intro`:
scene 11 at f11002, iv2_7 armed f11003 (gate 2), runs f11004–f12802, gate 0 f12803, iv2_8
armed f12804, ends f17013, mode 8 at f17014.

## Render `FUN_0045cc85` + `FUN_0040a765` + `FUN_0045cc1e`
Skipped while a dialogue with a `bgset:` runs (iv2_7 and iv2_8 both paint `bg_guild.bmp`):
in the intro retail draws the guild scene only on the gate-2 / gate-0 frames. Layers: bg,
`FUN_0040a765` (HUD + cursor; called again after the scene), the hired one's portrait, the
adventurer panel (band, list frame, equipment plates, fee, 4-line profile, equipment rows
via `FUN_0046add8`, the list with greyed unaffordable rows), the bag (6 plates, "Depart!",
rows, `FUN_0046b00a`), the dungeon map (paths, markers under ADDSIGNED, name plates), the
floor list ("Floor N～M" / "Boss Rush", best times `0xb10b + 20·d + g` as h:m:s from ticks).
Engine quirk (§139): the adventurer panel ORs 0x20 into each shown equipment slot of the
WORK bank — a render-side save write.

The item window is drawn **three times** a frame: `FUN_0040a765` calls `FUN_0046b00a`
(+ the choice box `FUN_0043537e`, the hand `FUN_00435747`, the message box `FUN_00435117`)
unconditionally, it runs twice (inside `FUN_0045cc85` and after it), and `FUN_0045cc85`
calls `FUN_0046b00a` once more after the bag rows. The parchment's texel alpha is 203, so
one layer (the Merchants' Guild, mode 6) is see-through and the stacked guild layers read
as opaque. Device state is identical at every one of those draws (v3 capture, f5600 vs
f8600). Row tint: with an adventurer compared (`DAT_005c6ee0`), a row it can't wear
(`FUN_00468ddc`, or unappraised) draws its icon at `0xff4d4d4d` and its name at
`0xff505050` / the half-strength trend tints.

## Pause from modes 6 / 11 (`FUN_00453384`)
Entry: `FUN_004681d3` (detail popup shut) and, in 6 / 11, `FUN_004681a6` copies the item
window's per-tab scroll / cursor and tab aside. Unpause: when a window was up
(`DAT_06a499b8`) `FUN_00468338(DAT_0734b9a4, 1)` rebuilds it (cursor / scroll zeroed) and
`FUN_004682e3` slides it fully in; then mode 6 `FUN_0049161d` / mode 11 `FUN_0045bd36` put
the copy back (`FUN_00468131`, cursor snap, row re-highlight) — mode 11 also re-highlights
the bag slot and the compared adventurer, mode 6 the qty-overlay cursor and the price
multiplier. Without this the port's guild Buy list went dead after an unpause and the
`adv-guild-pack-leave` player never left the Merchants' Guild (f9609).

## Continue (`FUN_0049a59e`)
The Continue arm sets the bag size from the merchant level and empties the bag
(`memset(DAT_0438b5ec, 0xff, 0x98)`) before any guild visit — retail shows 20 from the
load frame, the port kept the `.data` 38 until the guild (fixed).

## Open
- `PORT-DEBT(stub, FUN_004682ee)`: the bag picker's first-equippable-tab preselect
  (`FUN_00468ddc` equip check); the display menu's mode-0 population is itself partial.
- `PORT-DEBT(stub, FUN_0047f172)`: the backup bank.
- `PORT-DEBT(stub, NONE)`: Depart loads the dungeon (no dungeon port);
  `PORT-DEBT(stub, FUN_0045bcea)`: `DAT_0438b4dc` has no port mirror.
- `PORT-DEBT(stub, FUN_00452809)`: the intro's blackout + mode-1 load fades (as at the guild).
- Evidence: `adv-guild-pack-leave` (slot 9, native) — portstate on the mode / guild /
  bag VAs identical f1–f17745 (two guild visits, the Merchants' Guild with a pause,
  packing, Depart); portshots at 7 guild/Merchants' Guild frames gt8 ≤ 0.04%, the
  rest is the hand cursor's anti-aliased outline (position identical).
