# Probe: pause-status-items

- **Run:** `pause-status-items` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/pause-status-items.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 42 turns, $0.77, 4 min
- **Scenario:** `tests/scenarios/pause-status-items/` (566 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/pause-status-items.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical pause-status-items                   ticks 1..866

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

I recorded the run **`pause-status-items`** from `save:64d43650865f`. It is **566 frames** long, `rd check` gives **PASS** and `rd verify` gives **MATCH**. I looked at two strips from a real-device replay:
- `runs/pause-status-items/view/strip_222_258.png` (the Status page)
- `runs/pause-status-items/view/strip_318_444.png` (the Items page: its tabs, the scroll and the detail box)

Both pages are view-only in the house. Status has no equipment or skill options, and A on an item offers no Use / Equip menu.

**Inputs and step frames**
- **Load (f0–137):** `.*60 A .*30 A` loads the save. The house is up at f137 with `dlg` 0 and no morning event. Recette is at (−0.3, 9.35), it is day 15 and gold is 470000.
- **Step 1, pause menu up (f182):** `~` at f178.
- The entry list (`0x74b2844`, count at `0x73e154c` = 6) reads [0, 1, 6, 2, 3, 4]: Status, Items, Encyclopedia, Options, Save, Exit Game. Status is listed because the party count (`0x741bed8`) is 2.
- **Step 2, Status open (f220):** A with `pm_sel` 0. `pm_state` counts 1→10 over f211–220.
- **Status probe (f221–258):**
  - R at f221 moves from Louie to Charme.
  - A at f234 opens the controls page; L at f247 moves back to Louie.
  - B at f258 closes the controls page; a second B closes Status (`pm_state` 9→0 over f270–279).
- **Items open (f315):** D, then A, with `pm_sel` 1.
- **Items probe (f316–562):**
  - R×8 steps through tabs 1–8 (f324–387).
  - D×10 goes to the end of the list (f391–427). An extra D at the bottom does nothing: the list stops there and does not wrap.
  - C at f434 opens the detail box; A at f445 closes it.
  - A on an item at f456 starts a 6-tick select animation, then nothing happens.
  - U×10 scrolls back to the top (f477–516).
  - R goes to tab 9 (f524), and R again wraps to tab 0. B closes Items (`pm_state` reaches 0 by about f558).
- **Step 3, back in the game (f566):** B closes the pause menu, giving `scene == 1` and `dlg == 0`.
- **Checkpoints:** `status_open` (f220) and `items_end` (f433: Ingredients tab, cursor on the last item).

**Variables I named (this run's vars.json; worth sharing)**
- `st_chara` = s32 at 0x74b288c: the character shown on the Status page (index into the party).
- `st_sub` = s32 at 0x74b2888: 1 while the controls page is up.
- `it_tab` = s32 at 0x734b968: the Items tab. The tab count is at `0x731f404` (10 here).
- `it_cur` = `s32(0x7337850 + 4*it_tab)`: the cursor row in the current tab.
- `it_top` = `s32(0x73376c0 + 4*it_tab)`: the top row of the scroll window (7 rows are visible).
- `it_n` = `s32(0x7337210 + 4*it_tab)`: the number of rows in the current tab.
- `it_detail` = s32 at 0x734b96c: 1 while the detail box is up.
- Other values worth knowing:
  - `0x734b994`: the A-select animation, 1..6.
  - `0x74b28a4`: the Use / Throw away window flag (it stayed 0).
  - `0x74b28ac`: the Use / Throw away cursor.

**Status page (`sub_47FE98`; text drawn by `FUN_0047f1ce`)**
- **What it shows:**
  - The character's name between ◀ ▶ arrows.
  - Class, Level, Adventures and Defeats.
  - Attack, Defense, Magic and MDef.
  - Four equipment slots.
  - HP and SP gauges, and the footer "Button 1: Check controls and skills".
- **Louie:** Swordsman, Level 15, Adventures 5, Defeats 0. Attack 35, Defense 36, Magic 10, MDef 16, HP 87/87, SP 22/22. He wears a Longsword, Scrap Plate, Wooden Shield and Old Muffler; the Longsword and Wooden Shield are drawn in a blue-ish colour (ff4d4dff).
- **Charme:** Thief, Level 8, Adventures 0, Defeats 0. Attack 22, Defense 30, Magic 12, MDef 17, HP 38/38, SP 31/31. She wears a Rusty Dagger, Scrap Plate, Wooden Armband and Leather Boots.
- **Buttons:**
  - **L / R:** cycle through the party (wrapping, SE 326). They only work when there are at least 2 characters, and they also work while the controls page is up.
  - **U / D:** do nothing.
  - **A:** opens or closes "Character Controls (<name>)" (SE 323). This is a full-screen 640×480 picture with Movement, Defense, Attack and Skill panels; for Charme it also shows the shadow-image skill.
  - **B:** closes the controls page if it is up; otherwise it closes Status (SE 317).
- There is no equipment changing and no skill menu on this page.

**Items page (`sub_47FF40` → display list `sub_469414(1)`)**
- **The 10 tabs (rows in each):**
  - 0 Clothes (1)
  - 1 Robes (1)
  - 2 Breastplates (1)
  - 3 Armor (1)
  - 4 Bracelets (1)
  - 5 Helms (1)
  - 6 Shoes (1)
  - 7 Treasures (2)
  - 8 Ingredients (11)
  - 9 Food (3)
- **What it shows:** the tab name between arrows, then up to 7 rows with an icon, the name and "xN". Below is a help box with two lines of flavour text, "Base Price- N" and "Number possessed- N", and the footer "Button 3: Item Details".
- **L / R:** change tab, wrapping round (SE 326). Each tab keeps its own cursor and scroll. If there were only one tab, L / R would jump 6 rows instead; I read that in the decompile and did not see it.
- **U / D:** move the cursor and do not wrap. The window scrolls once the cursor passes row 6: at row 10, the top row is 4. Green arrows show that the list goes on above or below.
- **C (Button 3, bit 0x40):** opens the detail box, unless the item is flagged `&0x10`. The box shows the name, Type, Effect, Base Price, Highest Sale Price and Lowest Sale Price. For Chestnut it read "Ingredients / ---- / 200 pix / ---- / ----". A, B or C closes it (SE 710).
- **A on an item:** starts a 6-tick select animation, then the list returns 1. `sub_47FF40` only opens the Use / Throw away window when `saved_mode == 1` and the stage type is ≥ 1 (a dungeon). In the house nothing opens, and A only plays SE 323. The code also shows Food and Medicines going through `sub_4848C4` / `sub_484BCB` (HP / SP), which is the dungeon "Use" path. So Use / Throw away exists only in a dungeon.
- The "pause Equip variant" is entry type 5 (`sub_4802CF`). It is listed only in a dungeon (`saved_mode == 1` and stage type > 0), so it does not appear in the house.
- **B:** closes Items (SE 317).

**Tool gaps**
- `rd eval` cannot evaluate list comprehensions, so I read arrays with `ram dwords`.
- `rd text` only reads the current frame, so I took the tab names from the strip. A `text --at F` option would save a picture.
