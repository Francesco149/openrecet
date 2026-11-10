# Probe: endless-first-days

- **Run:** `endless-first-days` · **start:** save:c062daff8848 · **task:** `tools/drive/tasks/endless-first-days.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 69 turns, $1.07, 5 min
- **Scenario:** `tests/scenarios/endless-first-days/` (10659 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/endless-first-days.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical endless-first-days                   ticks 1..10959, save IDENTICAL

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

## endless-first-days: the first two days of Endless Mode after loading the Endless save

**Result:** the run is `endless-first-days`, starting from save:c062daff8848. The movie is 10,659 frames. `rd check` gives PASS and `rd verify` gives MATCH (frame 10659, rng 7a165bf2). There was no game over and no return to the title.

**Frame of each step (from `rd check`):**
1. Loaded with control: f137. The Endless title card (script 12/1) only starts after this frame; real control is at f2599.
2. The next morning (day dword 37): f6463.
3. Two days on (day 38): f9842 (the day dword went 37 → 38).

**Pictures I looked at:** the pause menu calendar (f2940) and the Daily Profits screen for "Day 37" (f6357).

### Loading
- `.*60 A .*30 A` loads slot 001. The game is in the shop at f137 with dlg 0, then script 12/1 (sid 12001) holds dlg 1 from about f138 to f2598, about 2,460 ticks. Control comes at f2599, at the morning spot (−0.3, 9.35).
- There was no morning event on either day: I waited 300 ticks each morning and saw no dialogue.

### What is different from story mode
- **The calendar has no debt.** It shows Merchant Level 6 and 400,000 pix. The grid is a fresh 35-day calendar (1–35, with Sundays 4, 11, 18, 25, 32 in red), "Today" is on **1**, and there are no payment-day marks. So Endless Mode restarts the calendar display at day 1 even though the day dword reads 36.
- **The Daily Profits screen** after day 36 reads "Day 37" with empty Sold, Bought and Found panels, Profits 0 and Score Difference 7308 (0). It looks the same as in story mode.
- **No collection form.** After the Profits screen the game goes straight to the morning. No form appeared on either night.
- **Tear's day-end lines:** Recette says "Thanks for all your help today, Tear!" and Tear answers "You also did well. You look tired; get some rest." Then the Profits screen.

### Map states (`wms`, digits in the order Home, Market, Town Square, Merchants' Guild, Pub, Chapel, Adventurers' Guild)
| Day dword | stime 0 | stime 1 | stime 2 | stime 3 (night) |
|---|---|---|---|---|
| 36 | 1111011 | 1111011 | 1111111 | 1011100 |
| 37 | 1111011 | 1111011 | 1111111 | 1011100 |

- No destination was marked 2 at any period, so there were no new town events in these two days.
- I burned every period with an empty Town Square visit: script 16/1 (sid 16001), "It looks like nobody's here...".
- I did not visit the Pub or the Chapel, since neither was marked 2.

### Guild and shop lists
- **Adventurers' Guild "Adventurer Selection"** (read on days 36 and 37): Leave, "Level:15 Swordsman Louie", "Level:8 Thief Charme". The highlighted figure is 1450, which I take to be Louie's fee (not checked). **No new adventurers after the story.**
- **Mashing A in the Adventurers' Guild** goes through selection into the item-preparation screen ("Pick the items you wish to bring with you… Depart!"). I undid that.
- **Merchants' Guild main list** (days 36 and 37): Buy, Sell, Fusion, Talk (tagged "New"), Leave. Greeting: "Before you stock up, make sure to read up on everything you can!".
- **Merchants' Guild Buy has 15 tabs:** Swords, Daggers, Staves, Bows, Spears, Gloves, Clothes, Robes, Breastplates, Armor, Bracelets, Shields, Hats, Helms, Charms. Each has 2–3 entries, the last "1 Left" (for example Worn Sword 168 / Longsword / Steel Sword; Charms: Souvenir Charm 210 / Standard / Popular). There is nothing that looks like an Endless-only tab.
- **Market** (Buy, Sell, Leave; "We came to the market... What do we do now?"): the same 13 tabs as `market-buy-display.md`. Necklaces, Capes, Scarves, Shoes, Rings, Treasures, Books, Food, Medicines, Flooring, Wallpapers, Counters, Carpets.
  - Prices are the same (for example Treeseed 700, Apple 210, Willpower Ring 4,200).
  - Stock counts differ: Cherry 10, Watermelon 9, Beef Bowl 8, Plank Floor 8, Lacquer Walls 9, Wooden Counter 8, Red Carpet 8. Evoker's, Purity and Healing Rings are now Out Of Stock. Ferromin and Ferromin G are still Out Of Stock.
  - **No new categories.**

### Saves (pause → Save → picker; checkpoints)
| Day dword | Slot | Save sha | Checkpoint |
|---|---|---|---|
| 36 | 001 (start save, not re-saved) | c062daff8848 | day36 f2971 |
| 37 | 007 (was NO-DATA) | 71c8e3d35d69 | day37 f7073 (save filed at f7043) |
| 38 | 008 (overwrote an old non-Endless save) | **1e52630934d5** | day38saved f10659 |

- Slot 007 read back on the picker on day 38: SCORE 7308, TIME 2:57:52, "Endless".
- The checkpoint `day38` (f10374) was taken after my first save attempt, which failed (see below), so it is not a saved state.
- **The picker opens on the last-used slot,** not on the next empty one. On this save file, slots 002–006 and 008–012 hold old saves; 007 was the only empty slot I saw.
- **Overwriting an occupied slot** asks "Overwriting file. Are you sure?" Yes / No. My first day-38 attempt was cancelled by the B presses and wrote nothing (the endsave sha stayed 71c8e3d35d69). `L .*8 A` picks Yes.

### Route facts
- **From the morning spot to the town map** (161 ticks, found by search): `DL*16 U*16 R*8 DR*16 UL*32 U*40 UR*8 R*8 A .*7 A .*7 A`.
- **Town map moves:**
  - Market: `L .*10 R` → Town Square.
  - Merchants' Guild: D → Market (the cursor read 1 after D then L).
- **Period burn** (531 ticks): the calendar-week4 macro works unchanged in Endless Mode.
- **At night**, `.*30 U .*10 A .*40 (A .*6)x28 .*80 D .*10 A .*200` reaches stime 4. Then A taps go through Tear's lines and the Profits screen to the morning, about 360 ticks.

### Tool gaps
- **`rd tap --text`:** it lists every repeated draw, so mashing A in a menu floods the output.
- **No shared vars for:**
  - the save picker's cursor or slot state, or an "Overwrite?" prompt;
  - the shop or guild Buy tab name;
  - the guild list cursor.
- **`--watch` prints every tick** unless you pass `--every`, which is easy to forget.
