# Probe: market-buy-display

- **Run:** `market-buy-display` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/market-buy-display.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 87 turns, $1.42, 11 min
- **Scenario:** `tests/scenarios/market-buy-display/` (1647 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/market-buy-display.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `market-buy-display`: 1,647 frames. `rd check` gives PASS and `rd verify` gives MATCH** (frame 1647, rng 149bcc55, sem d9311c0979953294). I looked at strips of the Market list (470..770) and of the display menu (1240..1640).

**Step frames (from `rd check`)**
1. **In the Market:** f303. The Market is town-map entry 1 (`wm_cur == 1`, reached with `L .*5` three times). It is internally scene 6 variant 1 (`s32(0x963c5f0) == 1`).
2. **Something was bought:** f509, two Treeseed Necklaces for 1,400 total.
3. **Display menu open over a table cell:** f1186, on the back shelf at (col 1, row 0).
4. **An item was on display and control was back:** f1253, the Apple placed.

**Other frames**
- **Load:** the save loads with `.*60 A .*30 A`; I was in the house at f137, `dlg 0`, with no event.
- **Buys:** Treeseed Necklace ×2 (f509, −1,400), Apple ×1 (f656, −210), Old Muffler ×1 (f773, −35). Gold went 470,000 → 468,355 and `items` 32 → 36.
- **Leaving:** Leave at f856 (back to the town map); home at f1034.

**Display actions** (grid = bank dword 0x4e26, VA 0x44f7030 for slot 0; cell = col + row·20; values are `id << 6`)
- **f1253, place on an empty cell:** Apple onto the back shelf (1,0), cell 1 (0x44f7034): −1 → 0x3e840. Inventory list at bank+0xa0 lost one slot (the later slots move up one), and `items` went 36 → 35. A was pressed at f1246; the action lands 7 ticks later.
- **f1410, place on an empty cell of a different table:** Treeseed Necklace onto the left shelf (0,2), cell 40: −1 → 0x20d40. `items` 35 → 34.
- **f1502, take an item off:** chose "Nothing" on the left shelf (0,4), cell 80: 0x4b80 (the Longbow) → −1. The Longbow went back to the inventory; `items` 34 → 35.
- **f1627, swap:** Old Muffler onto the centre table (3,3), cell 63 (0x44f712c): 0x6480 (Iron Spear) → 0x23f40. In the inventory the muffler left and the spear was added at the end of the list; `items` stayed 35.
- Bank dword 2 (0x44e37a0) also rises steadily; it looks like a play-time counter, not part of the display action.

**Shop layout**
- There are 3 tables, at grid (3,3), (1,0) and (0,1); the count is at 0x438bfb4.
  - The back shelf is cells (1..4, 0).
  - The left shelf is (0, 1..4).
  - The centre table is 2×2 at (3..4, 3..4).
- Occupied cells at the start: 2, 3, 4, 20, 60, 63, 64, 80, 83, 84.

**Market options and stock**
- Options are Buy, Sell, Leave (no Fusion or Talk); the greeting is "Let's pick up some things here!".
- 13 tabs, in this order, wrapping round. A price is shown only for the item under the cursor; items without "N Left" have unlimited stock.
  - **Necklaces:** Treeseed Necklace 700, Shell Necklace (1 left) 1,680.
  - **Capes:** Cloth Cape 1,400, Adventurer's Cape (1).
  - **Scarves:** Old Muffler 35, Handknit Muffler (1).
  - **Shoes:** Summer Sandals 560, Leather Boots (1).
  - **Rings:** Willpower Ring 4,200, Evoker's Ring (2), Purity Ring (2), Healing Ring (1).
  - **Treasures:** Unthankful Statue 98, Thankful Statue (1).
  - **Books:** Les Infortunes 350, Le Penseur, Wings of Eldon, Wings of Eldon 2 (1), Deux Infortunes (Out Of Stock), Booze of the World (1), Polite Children (1); the list scrolls further.
  - **Food:** Apple 210, Cherry (6), Watermelon (5), Walnut Bread, Beef Bowl (4), Candy, Taiyaki; the list scrolls further.
  - **Medicines:** Ferromin 700 (Out Of Stock), Ferromin G (Out Of Stock).
  - **Flooring:** Plank Floor (4) 350, Tile, Stone, Brick (1 each).
  - **Wallpapers:** Lacquer Walls (5) 350, Plank Walls, Starry Wallpaper, Resort Wallpaper (1 each).
  - **Counters:** Wooden Counter (4) 350, Legendary Counter (1), Barrel Counter (1).
  - **Carpets:** Red Carpet (4) 350, Green Carpet (1).

**Buy price rule:** purchase price = base × 0.7 (the price trend is 1.0 today). Two base prices were read from the display menu: Treeseed Necklace 1,000 → 700, Apple 300 → 210.

**Buy flow:**
- **List:** A on an item goes to `gmode` 8 after about 7 ticks, showing "Buying X. Are you sure?", the quantity, "Stock Price", and Yes / No.
- **Quantity:** U raises it (`s32(0x9642c5c)`).
- **Confirm:** A pays and returns to `gmode` 0 about 10 ticks later.
- **Back out:** B returns to `gmode` 1.

**How the display menu is organised**
- **Prompt:** "What will you place?" on an empty cell, "Exchange with what?" on an occupied one; the occupied cell's item name floats as a label.
- **Tabs:** L/R switches between categories, and only categories you hold appear. Seen: Clothes, Robes, Breastplates, … Necklaces, Scarves, Shoes, Treasures, Ingredients, Food.
- **Rows:** entry 0 is always "Nothing" (take the item off); stacks show "xN"; each item shows a Base Price and Number possessed.
- **Order:** within a tab the order looks like inventory or database order. It is not clearly alphabetical; I didn't settle this.
- **Remembered state:** the tab is remembered between openings (Necklaces stayed). The cursor resets to 0 when you change tab; once it re-opened on entry 1.
- **Input lock:** an R pressed about 25 ticks after A opened the menu was ignored, so allow more than 25 ticks before switching tabs.

**Tool gaps**
- `rd eval` has no `hex()`.
- `rd text` can't tell which list row is highlighted: there is no cursor variable for the shared item window, and the description box (the only cursor hint) is missing during tab slides.
- A per-tab browse costs two calls (play, then text).
- The search back to the shop used a long wandering path on the town map (about 170 frames); it is harmless, but the movie could be shorter.
