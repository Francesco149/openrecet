# Probe: vending-level16

- **Run:** `vending-level16` · **start:** save:2afe7fb42617 · **task:** `tools/drive/tasks/vending-level16.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 105 turns, $1.49, 27 min
- **Scenario:** `tests/scenarios/vending-level16/` (5622 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/vending-level16.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `vending-level16`**: 5,622 frames. `rd check` gives PASS and `rd verify` gives MATCH (sem d950c9202c029553). Vending machines can be obtained and placed with this save. I bought one, placed it, stocked one item and stood facing it, and I looked at strips of every new screen.

**Step frames**
- **Step 1, in the shop with control:** `check` judges it at f137 (the save loads at f137, before the level-up event starts at f138). Real control comes at f235, after the level-up event is skipped (`~`, wait, `A` on Yes).
- **Step 2, back in the shop at the end:** `check` judges it at f3028. That is the end of a second, unskippable event that plays at once (see below).
- **End state at f5622:** `scene 1`, `cc08 1`, `dlg 0`, standing at (0.635, −1.775) facing the vending machine, with its tooltip on screen.

**Timeline**
- f235–3028: Tear's vending-machine tutorial.
- f3066: Tear's menu opens (`cc08 15`).
- f3109: Merchant Level page (`cc08 18`).
- f3244: counter menu (here it is `cc08 0` with the list showing).
- f3603: town map. f3677: Merchants' Guild. f4408: Market.
- f4913: bought the "Old Vender" (`gold` 470000 → 449000).
- f5046: back in the shop.
- f5102: opened the menu over a table (`cc04 1`).
- f5467: placed the machine.
- f5509: opened the machine's own menu (`cc04 2`).
- ~f5571: stocked a Beef Bowl.
- f5602: closed the menu with `B`.

**Game facts**

1. **Level 14–16 abilities** (Tear → Merchant Level, `rd text`):
   - **L14 "Change Counters":** "At this level, you may change the kind of counters in the store if you have a new one. Check the market and elsewhere for new counters!"
   - **L15 "Vending Machines":** "At this level, you may place vending machines! You may only place a few, however, and you must possess vending machines to place."
   - **L16 "Two Items":** "At this level, if you are defeated in a dungeon, you may bring back two items instead of one."
   - L17 is "Item Storage"; L18 and L19 show "???".

2. **The level-up event is followed by a second event.** About 30 ticks after control returns, an event starts by itself ("Ah, Recette. A moment, if you would."). It shows "ESC Key: Event Skip", but I clicked through it with `A` (about 2,800 ticks). It is Tear's vending tutorial:
   - To place one, choose it from the item placement menu at a counter, as if it were a normal item; the counter becomes that machine.
   - You must keep it stocked yourself, and there is a limit to how much fits.
   - Machine prices cannot be haggled: items sell at base price, so they suit cheap items in volume.
   - You cannot sell machines off yourself; to get rid of one, sell it back to the Merchant's Guild or market at a steep loss.
   - In this game "counter" means a display table.

3. **Counter menu at level 16:** 7 entries (`s32(0x5cd0bc) == 7`), entry codes 0, 2, 1, 3, 5, 4, 6: Open Store, Change Walls, Change Flooring, Change Carpet, Move Tables, Change Tables, Cancel. There is no vending entry. There is also no "Change Counters" entry, probably because no new counter is owned yet.

4. **Where vending machines come from:** the **Market's** Buy list has a "Venders" category with "Old Vender – 1 Left" at 21,000 pix (base price 30,000). Its description: "An old, refurbished vending machine. Still usable, although its cooling unit cools the entire SHOP." The Market also has a "Counters" category: Wooden, Legendary, Barrel, Draped, and one out of stock. The Merchants' Guild Buy list on this day has only equipment (15 categories, Daggers through Charms), with no venders.

5. **How one is placed:**
   - Walk to a display table and press `A`; the table's display menu opens (`cc04 = 1`).
   - Press `L`/`R` to the "Venders" category; its help label reads "Place Vending Machine". Pick "Old Vender" and press `A`.
   - The whole 4-cell centre table becomes the machine, and its 4 displayed items go back to the inventory (`items` 33 → 36).
   - Bank changes in the diff:
     - Dword 0xb1d4 (VA 0x450fee8): 0 → 0x51440. 0x51440 is the vender's item code; I defined it as `vend0`.
     - Dwords 0x26..0x29 (VA 0x44e3830..0x83c): from (0x51440, −1, −1, −1) to (0x6480, 0x64c0, 0x30882, 0x2ee80).
     - Around dword 0x4e4b (VA 0x44f712c..0x180): the reverse change.
     - Inventory count, dword 0xaec6: 33 → 36.
   - I did not work out the exact meaning of the 0x26 and 0x4e4b blocks.

6. **What facing it shows:** a tooltip reading "Old Vender (0/6)". After stocking one item it reads "(1/6)". `A` while facing it opens the vender menu (`cc04 = 2`), whose rows are "Revert To Table", "Done Stocking", and slots "1:" to "4:" (more below a scroll arrow; 6 in all). Picking a slot opens the item list and `A` stocks one unit; slot 2 then read "2: Beef Bowl" and the inventory went from Beef Bowl x3 to x2. `B` closes the menu and the stock stays. Pictures: f5499, f5570, and strip 5450–5622.

**Tool gaps**
- `rd text` only captures the text drawn on the current tick, so reading a whole dialogue took strips timed just before each `A`. A "collect text over a range" command would save a lot.
- A session rebuild (which happened after a binary rebuild during `rd text`) silently dropped the `ram mark`.
- `wm_cur` changes with `L`/`R`/`D` in ways that do not follow the map layout, so I had to find the right press by trial.
- The list syntax `[... for i in range(n)]` is not supported in eval.
- The task's step 2 (`frame > 1500`) is met by the end of the unskippable vending event, so the step frames say little; the findings above are the result.
