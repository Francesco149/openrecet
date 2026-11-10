# Probe: item-storage-l17

- **Run:** `item-storage-l17` · **start:** save:352c146ab865 · **task:** `tools/drive/tasks/item-storage-l17.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 74 turns, $1.03, 6 min
- **Scenario:** `tests/scenarios/item-storage-l17/` (2207 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/item-storage-l17.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical item-storage-l17                     ticks 1..2507, save IDENTICAL

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `item-storage-l17`** (2,207 frames, start save:352c146ab865). `rd check` gives PASS and `rd verify` gives MATCH (rng ac4f63aa, sem 86d83705e925aa94). The end save is **cbb3056eeb1e**: an in-game save over slot 020 at about f2044.

**Main finding:** "Item Storage" is not a storage place. It makes the dungeon bag bigger: at level 17 you can take 25 items into a dungeon instead of 20. You use it at the Adventurers' Guild, in the "Pick the items you wish to bring" bag.

**Step frames**
- **Step 1, in the shop with control:** `check` judges it at f137, when the save loads. Real control comes at f725.
- **Step 2, back in the shop at the end:** f1788, when I came home from the guild.
- **End:** f2207, in free roam at the door (2, −3.24). I looked at a picture of it.

**Timeline**
- f138–200: the level-up event. One `~ .*45 A` skipped it; `dlg` went to 0 for one tick at f200.
- f201: Tear's vending-machine tutorial starts at once ("Ah, Recette. A moment, if you would." … "how to place and use vending machines"). Skipped with `~ .*45 A` at f725. No storage tutorial followed (I waited 100 ticks).
- f857: Tear's menu (`cc08` 15). f899: Merchant Level page (`cc08` 18).
- f1113: counter menu.
- f1290: town map. f1349: Adventurers' Guild (`scene` 11, `wm_cur` 6, reached from the map with `D R`).
- f1451: the bag opens.
- f1574: stored Tough Clothes. f1655: stored Shell Chestpiece. f1711: took the Shell Chestpiece back.
- f1753: Leave, back to the map.
- f1788: home (map cursor 0).
- f1853–2187: pause menu → Save → slot 020 → Yes.

**Game facts**
1. **Ability texts** (Merchant Level page; the current level, 17, is drawn in yellow):
   - **L16 "Two Items":** "if you are defeated in a dungeon, you may bring back two items instead of one."
   - **L17 "Item Storage":** "At this level, your overall inventory capacity in a dungeon is increased to 25 items."
   - **L18 is now named "More Items"** (greyed; it shows no help text). L19 and L20 show "???".
   - This agrees with adventurers-guild-RE.md: bag size `DAT_005c5a98` (s32 0x5c5a98) = 20 / 25 / 30 / 35 at merchant level dword < 16 / ≥ 16 / ≥ 23 / ≥ 39. Here the level dword (bank 0xb100) reads 16, the HUD shows 17, and 0x5c5a98 reads 25 straight after Continue. So "More Items" (L24, dword 23) is probably the 30-item step.
2. **Nothing new anywhere else:**
   - Counter menu: still 7 entries (`s32(0x5cd0bc)` = 7): Open Store, Change Walls, Change Flooring, Change Carpet, Move Tables, Change Tables, Cancel.
   - Tear's menu: still Store Atmosphere, Selling, Buying and Orders Lectures, Merchant Level, Tear's Lectures, News Summary, Outstanding Orders, Time Lecture, Never Mind.
   - Pause menu entry types (`0x74b2844`…): 0, 1 Items, 6 Encyclopedia, 2 Options, 3 Save, 4 Exit.
   - The pause Items page opens on Clothes / Tough Clothes as usual.
   - I did not open a table's display menu this run.
3. **Where storage is reached:** Adventurers' Guild (`scene` 11, `cc08` stays 1) → adventurer list (`s32(0x6a49b5c)`; Louie, fee 1450) → `A` → the "Pick the items you wish to bring with you…" box → `A` → bag mode (`s32(0x6a49b60)` 1).
   - The bag has rows "Depart!" and slots " 1:" to "25:"; slot 25 is the last row (picture f1527).
   - Buttons in the bag: `U`/`D` ±1 row, `L`/`R` ±6 rows, `A` on a slot opens the item picker (`0x6a49b60` 2), `C` shows item details, `B` goes back to the adventurer list.
   - Buttons in the picker: `L`/`R` change category (Clothes, Breastplates/Armor, …), `A` swaps the item into the slot, and `A` on "Nothing" takes the slot's item back to the inventory.
   - In the adventurer list, `U` to "Leave" then `A` returns to the town map with the cursor on the guild. The bag goes back into the inventory (count 32 again). Leaving without departing cost no time period (`stime` stayed 0).
4. **Memory layout from one store action** (`ram mark` / `diff` over 0x4380000..0x4511760):
   - The bag is not in the save bank. It is `DAT_0438b5ec[i]` (slot i+1 at 0x438b5ec + 4i), with a twin copy at 0x438b684 + 4i. Each entry is an item code, id<<6, or −1 when empty.
   - Tough Clothes 0xfac0 (id 1003) went to slot 25 (0x438b64c and 0x438b6e4).
   - It came out of the inventory list in the bank (the list starts at bank dword 0x18, VA 0x44e37f8). The list shifted left one place and its last entry became −1. The inventory count 0x450f2b0 went 32 → 31.
   - Shell Chestpiece 0x12cc0 (id 1203) went to slot 24 (count 30). After taking it back, slot 24 reads −1 again and the count is 31.
   - After Leave, the count is 32 but the bag array still holds 0xfac0. The memory is not cleared; Continue / the guild's load clears it.

**Pictures (real device):** f308 (the vending event), f951 (level page with the L17 text), f1527 (bag rows 20–25), f1655 (two items stored, the picker on Armor), f1711 (one taken back), f2207 (end in the shop).

**Named state** (not added to vars.json): `bagsize` = s32 0x5c5a98; `bag` = 0x438b5ec + 4i; `bagmode` = s32 0x6a49b60 (1 bag, 2 picker); `advlist` = s32 0x6a49b5c; `invcount` = s32 0x450f2b0.

**Tool gaps**
- `rd text` only shows one tick, so I needed several step-and-read calls.
- `search --apply-best` did not apply a found path (only `--apply` does), so I had to replay it by hand.
- The pause main page is a sprite menu; I had to read its entries from the type table at 0x74b2844.
