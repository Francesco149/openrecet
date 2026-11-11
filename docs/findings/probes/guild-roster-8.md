# Probe: guild-roster-8

- **Run:** `guild-roster-8` · **start:** save:824053720e01 · **task:** `tools/drive/tasks/guild-roster-8.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 55 turns, $0.89, 4 min
- **Scenario:** `tests/scenarios/guild-roster-8/` (1784 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/guild-roster-8.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

Run `guild-roster-8` is finished: the movie is 1,784 frames, `rd check` gives PASS (step 1 at f418, step 2 at f1763) and `rd verify` gives MATCH. The roster was walked top to bottom and back up. Caillou and Griff were each taken up to the floor lists and backed out without departing, then "Leave" returned to the town map. Gold stayed at 470,000 throughout, so no one was hired. I looked at pictures of frame 643 (Arma's row) and frame 1121 (the Obsidian Tower floor list).

**Frames**
- Save loaded and control given at f137. A search walked to the shop door: town map at f335.
- The card's map route `D D D R` did not work: it moved the cursor 0→5→2→3→1. A search found `L U`, which put the cursor on the Guild (`wm_cur == 6`) at f394.
- `A` entered the Guild: step 1, `scene == 11`, at f418.
- Walking down the list: Leave f539, Louie f552, Charme f565, Caillou f578, Tielle f591, Elan f604, Nagi f617, Griff f630, Arma f643.
- Walking back up: Griff f669, Nagi f682, Elan f695, Tielle f708, Caillou f721, Charme f735, Leave f748.
- Caillou: `A` opened the bag and the "Pick the items" box (checkpoint `caillou_bag`, f818). Dungeon map at f881. Floor lists:
  - Hall of Trials f915
  - Jade Way f962
  - Amber Garden f1009
  - Obsidian Tower f1056, scrolled to the bottom at f1121
  - Lapis Ruins f1168, bottom at f1293
  - `B B B` back to the roster at f1335.
- Griff: dungeon map at f1466, Jade Way f1487, Lapis Ruins rows 36–70 at f1643. `B B B` back to the roster at f1685.
- Up to "Leave" (f1734), `A` → step 2, town map (`scene == 8`) at f1763. The movie ends at f1784.

**Text per adventurer** (every list row reads "Level:40 <class> <name>"; Leave shows no panel)
- **Louie** (Swordsman): "Brave but poor adventurer." / "Balanced and easy to control." / "Uses swords, shields and armor." Longsword, Scrap Plate, Wooden Shield, Old Muffler. Fee 3950.
- **Charme** (Thief): "Mysterious, pretty thief." / "Incredibly fast and deadly." / "Uses knives exclusively." Rusty Dagger, Scrap Plate, Wooden Armband, Leather Boots. Fee 4000.
- **Caillou** (Magician): "Magician. Bit of a brat, really." / "Frail, but possesses very" / "powerful magic. Uses staves." Focus Staff, Raincoat, Wooden Armband. Fee 4100.
- **Tielle** (Archer): "Young, kind elven archer." / "Able to fight well at range." / "Uses bows, naturally." Wooden Bow, Scrap Plate, Wooden Armband. Fee 4200.
- **Elan** (Black Belt): "Well-meaning martial artist." / "Capable of incredible hits" / "with his fists and gloves." Leather Glove, Clothes, Wooden Armband, Souvenir Charm. Fee 4400.
- **Nagi** (Lancer): "Eastern lance-wielder. Her lance" / "gives her significant reach." / "Bit of an airhead, however." Laundry Pole, Clothes, Wooden Armband, Treeseed Necklace. Fee 4400.
- **Griff** (Assassin): "A demonic assassin. His claw-" / "based fighting style is" / "sinister... and very effective." Evil Claw, Scrap Plate, Cloth Cape, Summer Sandals. Fee 4900.
- **Arma** (Golem): "A golem created by forgotten" / "arts. The "parts" equipped on" / "her arms change how she fights." Punch Unit, Scrap Plate, Wool Hat. Fee 5900.

Two layout notes:
- The equipment rows are drawn at x48, y289/329/369/409. A second set of draws at y489/529/569 repeats some of the first rows (Arma, Tielle and Caillou repeat only the weapon). The picture shows one equipment column with "equipment" placeholders for empty rows.
- These fees do not match the formula in `docs/findings/adventurers-guild-RE.md` (base + 100·level, halved at merchant level ≥ 20). Louie would be 4050 or 2025, but the screen shows 3950. Worth a look.

**What the game asks between the roster and the floor list**
1. `A` on an adventurer opens the bag (`0x6a49b60 = 1`) with the box (`0x6a49b70 = 2`): "Pick the items you wish to bring with you. / When finished, select the "Depart!" option / at the top of the list." Rows: Depart!, 1: … 5:. A side panel reads Clothes / Nothing / Tough Clothes.
2. `A` closes the box. The first time the bag cursor was on row 1; the second time it was already on Depart!.
3. `A` on Depart! opens the dungeon map (`0x6a49b48 = 1`). This works because the fees byte `bank+0x2bc81` is 1; with fees off it would depart straight into dungeon 0. The map cursor `0x6a49b50` starts at 1.
4. `A` on a dungeon opens its floor list (`0x6a49b4c = 1`).
5. `B` backs out one level at a time: floor list → map → bag → roster.

**Pressing `A` in a floor list departs at once** (IDA `sub_45C051`, the floor-list branch, sets `dword_6A49B44 = 1`), so I only used `B` there.

**Dungeon list and floor rows** (the same for both adventurers; 7 rows visible; row counts `0x6a49b2c[d]` = 1, 4, 7, 11, 21; dungeon count `0x6a49b74 = 5`, so no sixth dungeon)
- **Hall of Trials:** Floor 1～5, record 00:09:09.
- **Jade Way:** 1～5 00:07:17, 6～10 00:15:49, 11～15 00:13:26, Boss Rush 00:02:53.
- **Amber Garden:** 1～5 through 26～30, plus Boss Rush. All times are --:--:-- except 26～30, which reads 00:00:00.
- **Obsidian Tower:** 1～5 through 46～50, plus Boss Rush. Only 46～50 reads 00:00:00.
- **Lapis Ruins:** 1～5 through 96～100, plus Boss Rush. Only 96～100 reads 00:00:00.

**Scroll behaviour**
- Roster cursor `gcur` = s32 `0x5c5a9c` (0 = Leave); roster scroll `gscroll` = s32 `0x6a49b64`. The list holds 9 rows (count `0x741bed8 = 8`) and shows 5.
- Going down, the list scrolls once the cursor would pass the 5th visible row. Moving from row 4 to 5 sets scroll 0→1, and it ends at cursor 8, scroll 4.
- Going up, the scroll only moves when the cursor goes above the top visible row. At cursor 7–4 the scroll stays 4, then the cursor and scroll fall together to 3/3 … 0/0.
- From cursor 3 with scroll 0, the scroll became 1, 2, 3 at cursor 5, 6, 7.
- Neither end wraps, and the cursor moves on the frame of the press (edge mask `0x73dddd4`).
- The floor list works the same way: cursor `0x6a49b54`, scroll `0x6a49b58`, 7 visible. The bottom reads 10/4 in Obsidian Tower and 20/14 in Lapis Ruins.
- `gcur` and `gscroll` are defined in this run's vars and worth sharing.

**What the tools lacked:** `rd text` has no `--at`. `tap --text` prints only rows that changed, so identical rows (a repeated fee or equipment slot) disappear from the transcript, and each row needed its own play + text call.
