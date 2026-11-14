# Probe: lapis-71f-80f

- **Run:** `lapis-80f` · **start (CRAFTED lineage):** `save:24ab07c65610` = `lapis-60f-event-12-4`'s end save (shop, day dword 28 =
  HUD Day 29, `stime` 0, Louie L44, slot 020). That run started from `b4766a99d74b`, itself `lapis-51f-60f`'s end save with bank
  `+0x2bcf7` cleared by `tools/craft_pix_save.py`: this save descends from a crafted one, not from played state only. At start:
  `+0x2bd14` (80F seen) 0, `+0x2bd13` 1, `+0x2bcf7` 1.
  No task file (goal: Lapis 71F → 80F in one trip, the 75F mob floor, the 80F events 12/10..12/12, back, Save).
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`). Normal floors: BFS of the maze grid + one scored search per cell
  (`-j 2`, 60 s, `--hold 45 --hold-no-goal`, 8 hold directions, a random `--seed` each, backtrack 2 legs on a failed leg). 75F: 36
  chained kill searches (`--hold 30`, bombs: goal = slot freed). 80F: `rd play` / `rd tap --text`.
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`, `RD_HOST=win`).
- **Movie:** 47,087 frames (Day 29 guild → one trip 71F → 80F → return door → day end → Week-4 repayment → Day 30 morning → Save).
  Filed as `tests/scenarios/lapis-71f-80f/trace.jsonl`. **No retail golden** (next: `rhost.py golden lapis-71f-80f`,
  `rhost.py triage lapis-71f-80f`). Not triaged.
- **Verdict (2026-11-14):** `rd verify lapis-80f` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 47087 rng 71ee8e26 sem 3a115a8c17707d6c dev 43415ac8a23fa1d5  (unskipped retail)  [win]
  live session:          frame 47087 rng 71ee8e26 sem 3a115a8c17707d6c dev 43415ac8a23fa1d5  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 47087 rng 71ee8e26 scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:8767348923a8` (slot 020 overwritten from the shop's pause menu at f47087: day dword 29 = HUD Day 30, `stime` 0,
  73,670 pix after the automatic Week-4 repayment of 200,000, SCORE 7531 → 7942, TIME 4:18:44 → 4:31:46). `+0x2bd14` still 0.
- **Damage taken:** one hit, 215 → 191 at f6586 (71F, arriving at cell (5, 2) between a type 18 and a type 20). **Level-up L44 → L45
  at f20635** (75F: HP max 215 → 219 and refilled, ATK 62 → 63, DEF 82 → 83). No food eaten (SP 48 → 4 by spins on 75F).

## Before the dungeon

| frame | what |
|---|---|
| 137 / 138 | shop, Day 29 morning (day dword 28); control |
| 471 | door by search → town map (`wms` 1121011), `.*60 R U` → `wm_cur` 6 |
| 570 | `A` → Adventurers' Guild: Louie **L44**; Charme / Caillou / Tielle L40 |
| 614..1875 | hire `A A`, 3rd `A` → slot 1's category list (Spears). `R`×4 → Armor. **After a pick the next slot's list opens on the NEXT category** (Armor → Bracelets → Helms → Shoes → Treasures; the row cursor sits on the remembered item). Packed **Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, Taiyaki ×2, Baked Yam, Apple ×13** (20/20; only 2 Taiyaki left in stock) |
| 2027 / 2060 | `U` ×20 + `A` → plates (cursor on Lapis) → `A` → rows on the remembered "56～60"; `D` ×3 → **"71～75"** (last row, best `--:--:--`) |
| 2077 | **71F**, fee 4,350 (278,020 → 273,670), `stime` 0 → 1 |
| 2277 / 2313 | "Equip items in bag?" (`equipq` 1 at arrival + 200) → `A` → **ATK 62, DEF 82**, HP 215, SP 48 |

## Per floor

Level `floor/5 + 25`: L39 (71F–75F: table HP 15 → 132), L40 (76F–80F: 15 → 135). Enemy lists = actor table at the nav start
(`type × count (HP)`; total). Banners: only 71F read ("The floor is now slick!"); the others were drawn after the arrival tick
and not captured.

| floor (`floor`) | frames | enemies | grid, start → exit, legs |
|---|---|---|---|
| 71F (70) | 2077 → 7421 | 8×2 (158), 9×5 (176), 10×2 (44), 11×2, 12×2, 14×8 (176), 15×5 (132), 16×5, 17×3, 18×4, 19×2, 20×2, 22×4, 29, 36, 38×2, 39, 44×2 (106), 45×4 (123), 46×2 (158), 47×4 (308), 50, 51×2, 53; 67 | 9 × 11, (2, 5) → (5, 5); a kobold pack (type 16 ×4, age 195) and two type 20 sat in (4, 4): BFS rerouted west (`--block`), then (3, 1) → (5, 5) via the east column; one hit (−24) |
| 72F (71) | 7421 → 11159 | 8×4, 9×2, 10×4, 11×3, 12×2, 14×6, 15×5, 16×9, 17×3, 18×4, 19×3, 20×4, 22×3, 36×2, 38×2, 39×2, 44×2, 45×2, 46×2, 47×2, 50×2, 51×2, 53×2; 72 | 11 × 9, (1, 4) → (9, 5), 9 legs |
| 73F (72) | 11159 → 14470 | 8×2, 9×4, 10×3, 11×4, 12×4, 14×5, 15×6, 16×6, 17×4, 18×2, 19×3, 20×2, 22×6, 36×3, 38×4, 39×2, 44×6, 45×3, 46×2, 47×3, 50×2, 51×2, 53×3; 81 | 11 × 11, (7, 1) → (5, 8), 9 legs (1 backtrack) |
| 74F (73) | 14470 → 17723 | 8×3, 9×3, 10×2, 11×2, 12×3, 14×6, 15×4, 16×7, 17, 18×2, 19×6, 20×2, 22×3, 36×6, 38, 39×3, 44×2, 45×2, 46×2, 47×2, 50×2, 51×2, 53×2; 68 | 9 × 11, (1, 7) → (7, 6), 9 legs (1 backtrack) |
| **75F (74)** | 17723 → 31562 | **3 ×6 (220), 6 ×5 (132), 10 ×4 (44), 20 ×5 (132), 35 ×4 (132), 40 ×6 (132), 46 ×5 (158), 51 ×4 (132)**; 39 | mob floor (below); 5 × 7, start (1, 1), exit cell (1, 3) |
| 76F (75) | 31562 → 34968 | 8×4 (162), 9 (180), 10×3 (45), 11×2, 12×2, 14×9 (180), 15×5 (135), 16×7, 17×2, 18×2, 19×3, 20×5, 22×2, 36×3, 39×2, 40×3, 44×2 (108), 45×2 (126), 46×2 (162), 47×4 (315), 50, 51×2, 53×2; 70 | 11 × 11, (1, 2) → (5, 4), 8 legs |
| 77F (76) | 34968 → 36161 | 8×4, 9, 10×2, 11, 12×3, 14×4, 15×3, 16×12, 17, 18×2, 19×3, 20×4, 22×2, 36×2, 39×4, 40×3, 44×2, 45×3, 46×3, 47×2, 50×2, 51×2, 53×3; 68 | 9 × 11, (5, 7) → (4, 8), 2 legs |
| 78F (77) | 36161 → 37911 | 8, 9×2, 10, 11, 12, 14×2, 15×2, 16×3, 17, 18, 19, 20×2, 22×2, 36, 39, 40×3, 44×3, 45×3, 46, 47×5, 50, 51×3, 53×3; 44 | 9 × 9, (3, 5) → (5, 2), 5 legs |
| 79F (78) | 37911 → 39702 | 8, 9×2, 10×2, 11, 12, 14×3, 15×4, 16×7, 17×3, 18×3, 19×5, 20×3, 22, 36×3, 39, 40×2, 44, 45, 46×2, 47×2, 50, 51×2, 53; 52 | 9 × 9, (2, 1) → (6, 2), 5 legs |
| **80F (79)** | 39702 → 44486 (results) | **none** (props 42 + 43 from f39704) | event floor, below |

- **`ex` / `ez` (object 0) is not always the exit:** on 71F it read (−133.7, −54.3) (a wall-side object in cell (3, 1)) while the exit
  was cell (5, 5) = `s32(0x438b43c)` / `s32(0x438b440)`. 1,500 frames of portal searches at `exitd` 10.3 went nowhere before the
  start/exit cell vars were used (cut from the movie). On 76F..79F `ex/ez` = the exit cell centre.
- 71F: the first try with a damage slack (avoid `hp < H − 40` per leg) compounded to HP 0 at f5710 (24–38 a hit, 7 hits): loaded
  back to f2313 and redone with zero slack + reroute (cut from the movie).

## 75F — the mob floor (yellow list)

39 actors at arrival, all state 0 at age 5: type 3 ×6 (HP 220), 6 ×5, 10 ×4 (44), 20 ×5, 35 ×4 (moai, HP 132), 40 ×6 (yellow bombs,
132), 46 ×5 (158), 51 ×4 (skeletons, 132). **`gate` (object 0) reads 0 on this floor**, yet the exit is hidden: walking onto the exit
cell (1, 3) centre (−40, −120) raised nothing (f18655, 4 × 78 s of portal searches at distance < 0.02, cut).

- The run first walked to the exit cell, into the middle of the mob; the kill searches from there started doomed (a bomb at 1.5,
  lit by a swing at a type 20 next to it: every branch failed `hp < H` — "1 cells"). Loaded back to the arrival (f17723) and fought
  from the start room (1, 1), a dead end: the enemies came in one or two at a time.
- **Bombs (type 40):** a hit lights them (`+0x428` 2) and they blow later (`engine-quirks` § 148); a kill goal of `+0x428 != 1`
  succeeds on the lit tick and the blast lands after any 30-tick hold. Goal for bombs = the slot freed (`+0x428 == 0`), cell =
  (`+0x428`, relative position / 3): 5 found (206..1103 frames each, 125 s searches).
- 36 chained searches, all found, **zero damage**: kills by target type 3 ×6, 6 ×5, 10 ×3, 20 ×1, 35 ×4, 40 ×5, 46 ×4, 51 ×8
  (skeleton downs); the remaining slots were freed without being the search target (blasts / adjacent swings, not attributed).
- **f29318 last actor freed → `estage` 1.** The portals are the special objects `DAT_06a47054` = 20 / 21: the next-floor circle at
  **(−36.5, −40)**, the return door at **(−43.5, −40)** — the START room (1, 1), not the exit cell. `portal` 2 at f31524 (−36.8,
  −40.8) → "Go to the next floor?" → `A` → 76F f31562.

## 80F — no fight: events 12/10, 12/11, 12/12

Port timeline (`src/dungeon_flow.c` `d4_floor79_event`, FUN_00450877 asm 0x4508b6..0x450a69) confirmed tick for tick. Party arrives
at (−40, −55) (the column, facing 0); `gate` 3, `estage` 0, `lock` −1 on the arrival tick.

| frame | `estage` | `etick` | `lock` | `dlg` | `sid` | what |
|---|---|---|---|---|---|---|
| 39702 | 0 | 0 | −1 | 0 | 1001 | arrival; `+0x2bd14` 0, `DAT_09643128` 0, no actors |
| 39703 | 0 | 1 | **0** | 0 | 1001 | stage 0 holds the pad |
| **39704** | **1** | 1 | 0 | 0 | 1001 | **props spawned: type 42 (Hydra) at (−29, 0, −34) slot 0, type 43 (Papa) at (−31, 0, −34) slot 1, HP 135 each (15 · 9.0, L40), `unhit` 1**; `DAT_09643128` 0 → 1 |
| **39824** | **2** | 1 | 0 | **1** | **12010** | etick 120 → `FUN_0044ba2c(12, 10, 0)` |
| 40613 | 2 | 1 | 0 | 0 | 12010 | dialogue over; **bank `+0x2d67a` 0 → 1** (etick held at 1 while `dlg` 1) |
| 40643..40703 | 2 | 31..90 | **4** | 0 | 12010 | **auto-walk: `lock` 4 for etick 30..89 walks the party +z, pz −55 → −43.79** |
| **40723** | **3** | 1 | 0 | **1** | **12011** | etick 110 → iv12_11 |
| 43768 | 3 | 1 | 0 | 0 | 12011 | dialogue over; **`+0x2d67b` 0 → 1** |
| **43769** | 3 | 2 | **−1** | 0 | 12011 | **the 42 freed (`+0x428` 0), the 43 anim 2 (lying)**; control |
| 43769..43825 | 3 | → 57 | −1 | 0 | 12011 | walked `{dpad(-31 - px, -34 - pz)}` 13.3 → < 4 |
| **43825** | **4** | 1 | 0 | **1** | **12012** | distance to the 43 < 4 (4.64 at f43821; reached at (−33.37, −37.15)) → iv12_12 |
| 44068 | 4 | 1 | −1 | 0 | 12012 | dialogue over; **`+0x2d67c` 0 → 1** |
| 44078 | 4 | 11 | −1 | 0 | 12012 | etick ≥ 10: both special objects rise (circle (−35, −40), return door (−45, −40); state 3 → 0) |

**Props:** neither moves; `FUN_004284ba` only sets `+0xb08` / `+0xb04` = 1 and anim 0 while `estage` < 3. Their age counts only
outside dialogue (230 at f43768). Pictures (`view/f041300.png`: Hydra + Papa fighting top-left; `f043900.png`: Papa lying, the
Hydra gone).

**Story bytes:** `rd ram mark` at f39702 → `diff` over the whole bank at f44088: only **`+0x2d67a`, `+0x2d67b`, `+0x2d67c`** (=
`+0x2d670 + event index`, set when each dialogue closes) and the play-time dword `+0x8`. **`+0x2bd14` is NOT set by the events, the
return or the day end** (still 0 in the end save; `+0x2bcf0..+0x2bd20` unchanged: `00 00 01 01 | 01 01 01 01 | 0 | 00 00 00 01 | 0 … |
00 00 00 01 (+0x2bd13) | 00`). So a second 80F visit replays the events unless something else (a town event?) sets it — not
checked.

**Full text** (first-tick frame of each line; speaker by name plate / box position):

iv12_10 (Recette x 144, Tear x 236):

| frame | text |
|---|---|
| 39902 | (Recette) Wow, we've come a long way. I hope there isn't a lot left... |
| 39967 | (Tear) I do not think it is much further. Courage, Recette. |
| 40110 | (Recette) Wait... |
| 40175 | (Tear) Is something wrong? |
| 40240 | (Recette) Is it me or is there someone ahead of us? |
| 40305 | (Tear) This deep in? I rather doubt... |
| 40422 | (Tear) Wait... I believe you are correct. |
| 40461 | (Tear) There is someone just ahead, fighting a monster, I think. |
| 40513 | (Recette) Huh... |

iv12_11 (top box = Papa (x 112 / 66) or the Hydra (x 84); Tear x 80; Recette x 200):

| frame | text |
|---|---|
| 40801 | (Papa) Heh heh heh... not too bad, you foul beast. |
| 40840 | (Papa) However... I'm not backing down, either! |
| 40905 | (Hydra) RRROOOOOOAR!!! |
| 40970 | (Papa) Take THIS, monster! |
| 41204 | (Papa) HOOOORAAAAH! |
| 41269 | (Tear) Hmm. He is fighting hard, but... to be honest, he is awful at this. |
| 41308 | (Tear) Shall we aid him? |
| 41425 | (Recette) ... Um... |
| 41490 | (Tear) Is something wrong? |
| 41581 | (Papa) HaHA! That's right! Once I prove how great a hero I am, my dear, sweet |
| 41620 | (Papa) daughter shall appreciate her Papa once again! |
| 41672 | (Recette) ... |
| 41737 | (Papa) Papa is doing his best, Recette! |
| 41867 | (Tear) Wh-wh-what?! |
| 41919 | (Recette) ... Oi... |
| 42023 | (Recette) Hey. Tear. We need to move on, right? |
| 42062 | (Recette) Tear, let's go. You know, like, quick. Ish. |
| 42127 | (Tear) Wh-WHAT? Are... are you sure?... |
| 42192 | (Recette) I-see-no-rea-son-to-stay-here-at-all. |
| 42231 | (Recette) And I REALLY don't see a guy wearing a pair of pants as a mask. |
| 42283 | (Tear) Well... |
| 42413 | (Papa) HUAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA |
| 42439 | (Papa) ... |
| 42478 | (Papa) ... AAAAAAAAAAAAAAAGH! |
| 42543 | (Papa) Our hero... is in quite a pickle! |
| 42608 | (Papa) With the fire! And the claws! And the... lack of healing items! |
| 42647 | (Papa) And the dying, that's not good either! |
| 42712 | (Tear) You are... QUITE certain we do not need to aid him? |
| 42777 | (Recette) Yeahyeahyeahyeah, it's fine! So long as he, uh, doesn't die, right? |
| 42842 | (Tear) I... would not be too sure about that... |
| 42959 | (Papa) HUAAAAAAAAAAAAAAAAA ... ... agh. |
| 42998 | (Papa) If... if someone is out there... tell... |
| 43037 | (Papa) tell... my daughter... that... her... ... father... ... ... ... ... ... |
| 43076 | (Papa) ... ... ... ... ... ... ... ... ... ... ... ... died a hero. |
| 43167 | (Tear) It seems to be over. I... think, at least. |
| 43232 | (Recette) I'm gonna go crawl into a hole and die of embarrassment, 'kay? |
| 43284 | (Papa) ... |
| 43310 | (Papa) *Snore* |
| 43440 | (Recette) Okay we are leaving no more wasting time FEET FORWARD. |
| 43505 | (Tear) That would be for the best, I suppose. |
| 43648 | (Papa) *Snerk, Snore, Huagh* |

iv12_12 (Recette x 144, Tear x 234):

| frame | text |
|---|---|
| 43903 | (Recette) Well no response looks like he's gone let's keep going. |
| 43968 | (Tear) ... He IS still alive, remember. |

`A` every 12 ticks throughout; "ESC Key: Event Skip" shown on all three (not used).

## The return and the day end

| frame | what |
|---|---|
| 44168..44300 | walk to (−47.6, −39.9) (the first walk attempt at f44088..44168 did not move: the pad was dead during the 120-tick rise) |
| 44302 / **44312** | `L . A` → sword on the door: **`door` = `s32(0x695ef78 + 0x2a0 + 0x268)` (object 1 = special 1 on this floor) 0 → 1** |
| 44344 | `L` → `portal` 2 at (−45.93, −40.01) → "Return to town now?" → `A` (Yes) |
| 44486 | results (`scene` 9); `B` → town map f44534, **`stime` 1 → 2**, `wm_cur` 6, `wms` 1121211 |
| 44627..44783 | `D` → 3 → Merchants' Guild → `B` → `D` → 0 → home: **`stime` 2 → 3** |
| 44851..45257 | shop door by search (f45028) → map → `R` 3 → Merchants' Guild → `D` → 0 → home: **`stime` 3 → 4** |
| 45417..45714 | day end, Daily Profits → **day dword 29** (f45714) |
| 45775..46036 | **Week #4 debt repayment, 200,000 pix** (TERME Finance collection form; 273,670 → 73,670, counted down over 262 ticks) |
| 46290..46723 | "So, this is the payment for this week. Let me see..." / "Congratulations. Well done." / "I have officially received your payment for this week." / "Let us continue to work hard next week, as well!" / (Recette) "YAY! I did it!" / "I'm counting on you next week too, Tear!" / "Day 30" |
| 46892 / 46974 / 47015 / 47056 / 47087 | pause (`(~ .*20)` ×n: ESC ignored ≈ 110 ticks) / `D` ×4 → `pm_sel` 4 / `A` → slots (cursor on 020) / `A` → "Overwriting file. Are you sure?" / `A` → saved; `rd endsave` → `save:8767348923a8` |

## Card corrections

- ROSTER d4 74 (75F): not a generic "no gate" floor in practice — the exit is hidden until every actor is freed (as on 65F), the
  object-0 `gate` var reads 0 here, and the portals rise in the START room (−36.5 / −43.5, −40), not at the exit cell.
- ROSTER d4 79 (80F): confirmed live; adds the `lock` 4 auto-walk, the trigger distance 4 from the 43 (Papa), the per-event seen
  bytes `+0x2d67a..c`, and that `+0x2bd14` stays 0 through the whole trip.
- DRIVER.md "BFS" recipe: the exit cell is `s32(0x438b43c)` / `s32(0x438b440)`; `ex/ez` (object 0) is not always the exit (71F).

## Tool gaps

1. **`ex` / `ez` / `exitd` / `gate` name object 0**, which is not always the exit portal (71F: a different object; 75F: `gate` 0 on a
   hidden-exit floor). A var pair for the special objects (`DAT_06a47054` → `0x695ef78 + i·0x2a0`, state `+0x268`) and for the exit
   cell would have saved ~1,500 frames and 4 failed searches.
2. Bombs as kill targets: `+0x428 != 1` is a trap (lit ≠ gone). A kill macro should know types 37..40.
3. `rd search` with the same seed replays the same exploration: a backtrack loop re-applied the identical path into the same trap 3×
   until `--seed` was randomised.
4. A per-leg damage slack compounds (7 hits to HP 0 on 71F); the slack must be against the floor's entry HP, not the leg's.
5. `rd text` has no `--at-frame` (banners for 72F..79F not read).
