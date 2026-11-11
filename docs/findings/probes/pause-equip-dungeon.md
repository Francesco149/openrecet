# Probe: pause-equip-dungeon

- **Run:** `pause-equip-dungeon` · **start:** save:824053720e01 · **task:** `tools/drive/tasks/pause-equip-dungeon.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 75 turns, $1.12, 9 min
- **Scenario:** `tests/scenarios/pause-equip-dungeon/` (2890 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/pause-equip-dungeon.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical pause-equip-dungeon                  ticks 1..3190

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run pause-equip-dungeon**: from save:824053720e01, 2890 frames. `rd check` gives PASS and `rd verify` gives MATCH: a fresh replay reads frame 2890, rng 5f97cb7c. I looked at a strip of the Equip page, f2390–2570. Checkpoints: `floor1` at f1981 (real control on 1F) and `back1` at f2608 (after the first equip change).

**Step frames (`rd check`)**
1. Dungeon floor with control: **f1240**. This is too early. As in hire-caillou, `lock` reads −1 on the arrival tick. Then come the "The Hall of Trials" title card and the "Equip items in bag?" box (`lock == 0`). Real control starts at **f1981**. The step predicate should be `lock == -1 and frame > arrival + ~30`, or also require that the bag prompt is gone.
2. Pause menu on the floor: **f2040**.
3. Back on the floor with control: **f2568** (scene 9 → 1 is shown as f2569 in the watch). I then paused a second time (f2608–2827) to swap the armour back. The movie ends on the floor at f2890 with control.

**Route facts**
- **Load and town map:** `.*60 A .*30 A .*120` puts you in the house at f137. A search reached the town map (`scene == 8`) at f335.
- **Map cursor:** `D` ×4 + `U` moved `wm_cur` 0→5→2→0→2. Then `R` gave 3 and `U` gave 6. `A` reached the guild (`scene == 11`) at f428.
- **Guild list:** the cursor opens on Louie, and `A` hires him. His fee is **3,950 G**, taken on Depart (470000 → 466050 at f1240).
- **Bag screen:**
  - `R`/`L` pages the 20-slot list (6..20) when the cursor is on the slot list. They only change the item category once a slot's picker is open (`A` on the slot).
  - The picker opens on "Nothing" or on the first item, depending on the category, so read `rd text` before pressing `A`. After a pick the picker can open on a different category.
- **Shop stock categories:** Clothes, Robes, Breastplates, Armor, Bracelets, Helms, Shoes, Treasures, Ingredients, Food. There are **no weapons in stock**, so no spare weapon could be packed.
- **Packed:** slot 1 Tough Clothes, slot 2 empty, slot 3 Iron Armor, slot 4 Beef Bowl, slot 5 Candy. That is a spare armour and two foods.
- **Dungeon picker:** `B .*10 L .*10 A .*30` left the cursor on Jade Way. Then `L .*12 A .*30 A` departed to the Hall of Trials.
- **Equip prompt:** the box appears about 600 ticks after arrival (it was still waiting at f1961). I answered **No** with `R .*8 A` at f1981 (atk/pdef stayed 56/58).

**Louie before pausing**
- HP 197/197, SP 45, `atk` 56, `pdef` 58.
- I spent SP with one `B` skill at f1985: SP 45 → 40. I took no hit.

**Pause menu in the dungeon (`scene == 9`)**
- Entry type table at 0x74b2844: `[0, 1, 5, 6, 2, 3, 4, 7]`. The picture shows Status (0), Items (1), Equip (5), Encyclopedia (6), Options (2), Save (3), Exit Game (4). Slot 7 is not visible.
- `pm_sel` opens at 0 (Status) with `pm_type` 0.
- `pm_state` runs 0 → 6 or 2 → 10 while a page opens (10 = open), and 10 → 8 → 2 → 0 while it closes (about 25 ticks). `B` from the main page closes the pause about 30 ticks later.
- The main page is a sprite (no text): Merchant Level 6, a calendar with Day 16 and "80,000 pix Payment Due!" on day 22, and 466,050 pix.

**Status page** (`A` at f2070, open at f2100). `rd text`:
- Name and class: "Louie", "Class Swordsman".
- Record: "Level 40", "Adventures 6", "Defeats 0".
- Stats: "Attack 56", "Defense 58", "Magic 21", "MDef 32".
- Equipment: "Longsword", "Scrap Plate", "Wooden Shield", "Old Muffler".

**Items page** (`pm_sel` 1, `pm_type` 1, open at f2181). `rd text`:
- Actions: "Use", "Throw Away".
- Count and list: "4/20", then "Rearrange", "Tough Clothes", "Iron Armor", "Beef Bowl", "Candy". Each row's help shows Base Price and "Number possessed".
- On the Candy row, `A` highlights "Use" and a second `A` uses it (keys `D D D A .*20 A`).
- Result: "Used Candy." / "Louie recovered 5 SP.". SP went 40 → 45 by f2249 (HP stayed 197) and the count went 4/20 → 3/20. `items` stayed 28 (the bag is separate from the shop inventory).
- `B` returns from the action box to the list, and `B` again to the main page.

**Equip page** (`pm_sel` 2, `pm_type` 5, open at f2376). This is the dungeon-only variant. `rd text`:
- Left list: "Optimum Equipment", then the 4 equipped items. The help box reads "Equip Optimum Items".
- Right list: the bag ("3/20", "Rearrange", items). Food is greyed (colour 505050) when an armour slot is picked.
- Keys: `D D A` picks the armour slot. Moving in the bag previews "Defense 58 → 59" for Tough Clothes and "58 → 64" for Iron Armor, with the MDef line below.
- **First swap:** `A` equipped Iron Armor at about f2442. The page then showed "Defense 64", and Scrap Plate went into the bag tagged "Adventurer's Possession" in green.
  - **The leader totals at 0x56db0ac/0x56db0b0 do not change while paused.** `pdef` stayed 58 until the pause closed, then read **64 at f2569**. `atk` stayed 56.
- **Swap back** (second pause, f2608): `D D A D A` put Scrap Plate back. The page showed Defense 58, and Iron Armor returned to the bag without the tag. `pdef` went 64 → **58 at f2827** on unpausing. `atk` stayed 56.

**Compared with the shop's pause menu**
- The dungeon menu adds **Status** (type 0, first entry, default cursor) and **Equip** (type 5). It also has Exit Game (type 4).
- Save and Encyclopedia are still present.
- The Items page acts on the dungeon bag (x/20), not the shop inventory.
- Equipment changes apply to the dungeon leader's stats only when the pause closes.

**Tool gaps**
- The bag-screen picker, the Equip prompt and the pause main page are sprites with no `rd text`, so I needed pictures to find cursor positions. Named vars for the bag picker's open state, category and cursor, and for the Yes/No cursor, would help.
- I suggest naming `pm_types` as the list at 0x74b2844 (8 dwords).
