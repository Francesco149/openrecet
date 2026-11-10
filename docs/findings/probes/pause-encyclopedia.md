# Probe: pause-encyclopedia

- **Run:** `pause-encyclopedia` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/pause-encyclopedia.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 41 turns, $0.88, 5 min
- **Scenario:** `tests/scenarios/pause-encyclopedia/` (2038 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/pause-encyclopedia.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `pause-encyclopedia`** (start save:64d43650865f): 2038 frames. `rd check` gives PASS and `rd verify` gives MATCH. I looked at real-device pictures of four moments: f258 (Swords grid), f1794 (Food scrolled to the bottom), f1910 (an empty "???" category) and f1996 (the Longsword detail). A fifth picture, f1671 (the Slime Fluid detail), was made but I did not open it.

**Steps and their frames**
- Load with `.*60 A .*30 A`; the house is up at f137 with dlg 0.
- Step 1, pause menu up: `~` pressed at f171, `scene == 9` at **f172**.
- Step 2, Encyclopedia open: D D then A at f228 (`pm_sel` 2, `pm_type` 6). `pm_state` counts 1→10 over f242–251, so the step holds at **f251**.
- Step 3, back in the game: at f1997 B closes the page (`pm_state` 9→0 by f2008), and at f2018 B closes the pause menu, giving `scene == 1` and `dlg == 0` at **f2018**.
- Checkpoint: `enc_open` (f258).

**What happens in between**
- f274–1012: R walks every category, Swords through Carpets.
- X (button D) goes back to category 9; then R with `--text` walks categories 9–23 again (f1311–1656). This repeat is only there because the first transcript was cut off in the middle.
- f1656: C opens the Slime Fluid detail.
- f1672: C closes it. A goes to the next category (I did not expect A to do that).
- f1728: on Food, D×6 goes to the bottom row (row 4, `enc_top` 2). The 6th D does nothing: the list stops and does not wrap.
- f1795: C opens the Shrimp Doria detail.
- f1811: C closes it; U×5 goes back to the top.
- A A goes to Flooring ("???"). C there does nothing, so empty categories cannot be selected.
- f1910–1996: A×5 wraps round to Swords; R moves to Longsword; C opens its detail at f1982.

**Variables I named (this run's vars.json; worth sharing)**
- `enc_cat` = s32 0x964C428: the category. The category count is s32 0x964C42C (33).
- `enc_row` = s32 0x964C410: the cursor row.
- `enc_col` = s32 0x964C430: the cursor column (0..2).
- `enc_top` = s32 0x9646640: the scroll window's top row. 3 rows of 3 icons are visible.
- `enc_slide` = s32 0x964C424: the category-change slide. It counts ±1..±10 and blocks input while it runs.
- `enc_detail` = s32 0x964C434: 1 while the detail box is up.
- `enc_rows` = s32 0x9643690: the number of rows in the current category. 0x964368C and 0x9643694 hold the same for the previous and next category.
- Other addresses:
  - Completion: seen count s32 0x964C420 (65) ÷ total s32 0x9643688 (538) = 12%. Below 1% but above 0 it shows 1%.
  - The entry slots are word_9643698: 60 words per category (20 rows × 3). Each holds an item id, −1 (empty), or −2 (the single placeholder in an empty category).
  - word_9646578[cat] is the category's base item id. Category names are at byte_963E5F8 + 32·(id/100).
- The page code is the function at about all_ida.c:115100; the drawing is sub_49F8B8.

**Buttons**
- **L / R:** move the column. At the end of the row they go to the previous / next category (wrapping, SE 326).
- **U / D:** move the row and scroll. They stop at the ends without wrapping.
- **A:** next category (input bit 0x100000).
- **X (button D):** previous category (bit 0x800000).
- **C (button 3):** opens or closes the detail box (SE 710). It does nothing on a −2 "???" slot.
- **B:** closes the Encyclopedia at once, even while the detail box is up; `enc_detail` stays 1 afterwards (stale).
- **Y (button E):** closes the whole pause menu in one press (`scene` 9→1, with `pm_state` still 10). A second Y re-opened it. I undid this, so it is not in the movie.

**"???" entries**
- Items not yet seen are not listed at all.
- A category with nothing seen shows the heading "???" and one empty frame that cannot be selected.

**Detail page fields** (left box)
- "Name (Equippable / Consumable)" — Slime Fluid's title has no tag.
- Type; Effect; Base Price; Highest Sale Price; Lowest Sale Price ("----" if never sold).
- For equipment, a right box "Characters who can equip/current equipment" shows the portraits.
- The examples:
  - Longsword (Equippable): Swords, ATK+14, 1200 / 1450 / 900 pix.
  - Shrimp Doria (Consumable): Food, "Recovers 50HP, Recovers 50SP", 4800 / 6800 / 6800 pix.
  - Slime Fluid: Ingredients, ----, 100 pix, ---- / ----.

**The list page:** a 3×3 icon grid with names, a help box (two lines of flavour text plus "Price- N") and the Completion Rate panel.

**Categories** (33, in `enc_cat` order; how many entries are seen in each; the first / last entry)
- 0 Swords: 3 (Worn Sword, Longsword, Steel Sword)
- 1 Daggers: 3 (Rusty Dagger, Crafter's Knife, Thief's Knife)
- 2 Staves: 1 (Magician's Staff)
- 3 Bows: 1 (Longbow)
- 4 Spears: 2 (Iron Spear, Practice Lance)
- 5 Gloves: 2 (Leather Glove, Sturdy Glove)
- 6 Claws: ???
- 7 Arm Parts: ???
- 8 Clothes: 2 (Clothes, Tough Clothes)
- 9 Robes: 2 (Raincoat, Thick Robe)
- 10 Breastplates: 2 (Scrap Plate, Shell Chestpiece)
- 11 Armor: 3 (Leather Armor, Iron Armor, Parade Armor)
- 12 Bracelets: 2 (Wooden Armband, Spiked Armband)
- 13 Shields: 3 (Wooden Shield, Common Shield, Big Shield)
- 14 Hats: 3 (Wool Hat, Cloth Hat, Warm Hat)
- 15 Helms: 2 (Wooden Helm, Warrior's Helm)
- 16 Charms: 1 (Standard Charm)
- 17 Necklaces: 2 (Treeseed Necklace, Shell Necklace)
- 18 Capes: 1 (Cloth Cape)
- 19 Scarves: 1 (Old Muffler)
- 20 Shoes: 2 (Summer Sandals, Leather Boots)
- 21 Rings: ???
- 22 Treasures: 3 (Unthankful Statue, Thankful Statue, Very Odd Vase)
- 23 Ingredients: 8 (Slime Fluid … Charred Lizard)
- 24 Books: ???
- 25 Cards: ???
- 26 Food: 13 (Apple … Shrimp Doria)
- 27 Medicines: 2 (Ferromin, Ferromin G)
- 28 Flooring: ???
- 29 Wallpapers: 1 (Lacquer Walls)
- 30 Venders: ???
- 31 Counters: ???
- 32 Carpets: ???

That is 65 seen.

**Transcripts, trimmed**

Food (the only category that scrolls; 5 rows):
- Apple ("This apple is a bit sour, so it's best used in jam or a pie. Still perfectly edible, though!" Price- 300)
- Orange (700)
- Cherry (900)
- Grapes ("Given their use in snacking, jelly, and, uh, alcohol, grapes are always welcome as a gift." 1,400)
- Walnut Bread
- Beef Bowl
- Candy ("A popular fruit-flavored candy. People keep saying you can find gold prize wrappers, but no luck today..." 300)
- Chocolate Bar
- Taiyaki
- Candy Apple ("This rare treat is one part sour apple, one part hardened caramel, one part stick and all parts tasty." 1,600)
- Baked Yam
- Honey
- Shrimp Doria ("A rice dish made with shrimp, melted cheese and a special white sauce. Is your mouth watering?" 4,800)

Ingredients:
- Slime Fluid (100)
- Slime Liver (1,800)
- Fur Ball (110)
- Water Shroom
- Chestnut
- Insect Trap
- Powerful Antivenom
- Charred Lizard

Other prices seen:
- Weapons: Worn Sword 200, Longsword 1,200, Steel Sword 3,000; Rusty Dagger 400, Crafter's Knife 1,400, Thief's Knife 3,400; Magician's Staff 1,200; Longbow 1,400; Iron Spear 1,800, Practice Lance 4,200; Leather Glove 200, Sturdy Glove 1,000.
- Body: Clothes 100, Tough Clothes 1,400; Thick Robe 2,100; Scrap Plate 200, Shell Chestpiece 2,800; Leather Armor 500, Iron Armor 1,900, Parade Armor 5,600.
- Arms and head: Wooden Armband 150, Spiked Armband 1,200; Wooden Shield 350, Common Shield 1,200, Big Shield 2,800; Wool Hat 250, Cloth Hat 800, Warm Hat 1,600; Wooden Helm 400, Warrior's Helm 1,600.
- Accessories: Standard Charm 1,400; Treeseed Necklace 1,000, Shell Necklace 2,400; Cloth Cape 2,000; Old Muffler 50; Summer Sandals 800, Leather Boots 1,600.
- Other: Unthankful Statue 140, Thankful Statue 4,550, Very Odd Vase 21,400; Ferromin 1,000 ("Increases max HP."), Ferromin G 2,500; Lacquer Walls 500.
- Not captured: Raincoat (its line was cut from the output), and Orange's to Shrimp Doria's remaining prices.

**Tool gaps**
- `rd tap --text` output over about 6 KB is cut in the middle, so long walks have to be done in chunks.
- There is no sum or loop in expressions, so counting the slots of 33 categories needed a raw hex dump.
- Even a `look` at a single frame costs a full replay.
