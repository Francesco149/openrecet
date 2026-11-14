# Probe: lapis-81f-90f

- **Run:** `lapis-90f` · **start (CRAFTED lineage):** `save:8767348923a8` = `lapis-71f-80f`'s end save (shop, day dword 29 = HUD
  Day 30, `stime` 0, Louie L45, slot 020, 73,670 pix). Chain: ← `24ab07c65610` ← `b4766a99d74b` = `lapis-51f-60f`'s end save with
  bank `+0x2bcf7` cleared by `tools/craft_pix_save.py`. So this save descends from a crafted one, not from played state only.
  No task file. Goal: Lapis 81F → 90F in one trip (85F mob floor, 90F black crab), then back and Save.
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`). Normal floors: BFS of the maze grid + one scored search per cell (the
  `lapis-71f-80f` nav script: `-j 2`, 60 s, `--hold 45 --hold-no-goal`, zero HP slack). 85F: 17 chained kill searches (`--hold 30`). 90F:
  a phase script (flip / damage / get-up searches, below).
- **Host:** every session runs natively on the Windows box (`RHOST_WIN=win-wifi`, `RD_HOST=win`). The driver process on maximus was
  OOM-killed at 04:47, during the first `verify --real`. The session survived and `verify --real` was re-run.
- **Movie:** 27,175 frames: Day 30 Market → guild → one trip 81F → 90F → return door → day end → Day 31 morning → Save.
  Filed as `tests/scenarios/lapis-81f-90f/trace.jsonl`. **No retail golden** (next: `rhost.py golden lapis-81f-90f`,
  `rhost.py triage lapis-81f-90f`). Not triaged.
- **Verdict (2026-11-14):** `rd verify lapis-90f` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 27175 rng a734c0e7 sem be6e42ca9adc3f72 dev 65a42a308b05989c  (unskipped retail)  [win]
  live session:          frame 27175 rng a734c0e7 sem be6e42ca9adc3f72 dev 65a42a308b05989c  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 27175 rng a734c0e7 scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:6809401b79a9` (slot 020 overwritten from the shop's pause menu at f27175: day dword 30 = HUD Day 31, `stime` 0,
  58,580 pix, SCORE 7942 → 8414, TIME 4:31:46 → 4:39:16).
- **Damage taken: none.** `rd scan` with `--where "hp < 219"` over f3114..21331 shows 0 rows; HP 224 throughout the crab fight.
  **Level-up L45 → L46 at f10652** (85F): HP 219 → 224 (refilled), ATK 63 → 64, DEF 83 → 84. No food eaten. SP 49 → 4 from spins on 85F,
  back to 50 at the level-up.

## Before the dungeon

| frame | what |
|---|---|
| 137 | shop, Day 30 morning (`.*60 A .*30 A`); control |
| 451 | door by search → town map; `L` → `wm_cur` 1 → Market f549 |
| 589..1195 | **Market Food: Apple, Cherry (2 left), Watermelon (2), Walnut Bread, Beef Bowl (2), Candy, Taiyaki; Medicines: Ferromin / Ferromin G Out Of Stock.** Bought Cherry ×2 (1,260), Watermelon ×2, Beef Bowl ×2 (1,610 each), Taiyaki ×4 (3,080): 73,670 → 63,030. Category L / R order (from Necklaces, R): Capes, Scarves, Shoes, Rings, Treasures, Books, Food, Medicines, Flooring, Wallpapers, Counters, Carpets |
| 1257..1362 | `B B` → map; `D R U` → `wm_cur` 6 → Adventurers' Guild: Louie **L45**; Charme / Caillou / Tielle L40 |
| 1402..2667 | hire `A A`; `A` enters slot 1's category list (Spears); `R` ×4 → Armor. **The first `A` picked "Nothing"**: the row cursor opened on row 0 here. Per item: `D` (next slot) `A` (enter, remembered row) `A` (pick). Packed **Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, Watermelon ×2, Beef Bowl ×2, Taiyaki ×3, Cherry ×2, Baked Yam, Apple+3, Apple ×5** (20/20). The Food list's selected row = the description block at y 368 in `rd text` (no block = "Nothing") |
| 2667..2835 | `U` ×20 + `A` → plates → `A` → rows (window 41～75, cursor on the remembered "71～75"); `D D` → **"81～85"** (last row) |
| 2852 | **81F**, fee 4,450 (63,030 → 58,580), `stime` 0 → 1 |
| 3052 / 3114 | "Equip items in bag?" (`equipq` 1). The first `A` (f3052) did not take; a second `A` did → **ATK 63, DEF 83**, HP 219, SP 49 |

## Per floor

Level `floor/5 + 25`: L41 (81F–85F: table HP 15 → 138), L42 (86F–90F: 15 → 141). Enemy lists come from the actor table at nav start
(`type × count (HP)`; total). Banner: 81F "Adventurer defense power doubled!" (`pdef` still read 83). Others not captured.

| floor (`floor`) | frames | enemies | grid, start → exit, legs |
|---|---|---|---|
| 81F (80) | 2852 → 5325 | 8, 9×4 (184), 10×4 (46), 11, 12, 14×3, 15×6 (138), 16×5, 17×3, 18×2, 19×2, 20×2, 22×2, 29, 36×4, 39, 40×4, 44×5 (110), 45×2 (129), 46×2 (166), 47×2 (322), 50×3, 51×3, 53×4; 67 | 11 × 9, (3, 5) → (6, 3), 5 legs |
| 82F (81) | 5325 → 6634 | 8×4, 9×3, 10×2, 11×2, 12×2, 14×6, 15×8, 16×11, 17×4, 18×2, 19×3, 20×3, 22×2, 36×4, 39×5, 40×3, 44×4, 45×4, 46×2, 47×4, 50×5, 51×2, 53×3; 88 | 11 × 11, (3, 6) → (5, 7), 3 legs |
| 83F (82) | 6634 → 8383 | 8×3, 9×3, 10×3, 11×2, 12×4, 14×6, 15×5, 16×8, 17×5, 18×2, 19×6, 20×4, 22×2, 36×2, 39×2, 40×4, 44×2, 45×2, 46×5, 47×2, 50×3, 51×2, 53×2; 79 | 11 × 11, (3, 7) → (6, 8), 4 legs (1 backtrack) |
| 84F (83) | 8383 → 9768 | 8×2, 9, 10, 11×3, 12, 14×6, 15×5, 16×4, 17×2, 18×4, 19×5, 20×2, 22, 36×3, 39, 40, 44×2, 45×3, 46, 47×2, 50×3, 51, 53×3; 57 | 11 × 9, (3, 2) → (1, 2), 4 legs |
| **85F (84)** | 9768 → 11935 | **19 ×11, 20 ×6, 21 ×5 (230), 18 ×4, 17 ×4** (138 unless noted); 30 | mob floor (below); 5 × 7, start (1, 3), exit cell (1, 1) |
| 86F (85) | 11935 → 13104 | 8×2 (169), 9×2 (188), 10×2 (47), 11, 12×3, 14×3, 15×4 (141), 16×8, 17×4, 18×2, 19×2, 20×4, 22×2, 36×4, 39×3, 40×3, 44 (113), 45×5 (132), 46×3, 47×3 (329), 50, 51×2, 53×2; 66 | 11 × 9, (3, 1) → (2, 3), 3 legs |
| 87F (86) | 13104 → 15323 | 8×2, 9×2, 10×2, 11×4, 12×3, 14×7, 15×6, 16×9, 17×4, 18×2, 19×2, 20×3, 22×5, 29, 36×2, 39×4, 40×2, 44×2, 45×2, 46×3, 47×3, 50×3, 51×2, 53×2; 77 | 11 × 11, (1, 1) → (6, 1), 5 legs |
| 88F (87) | 15323 → 18208 | 8×2, 9×2, 10×4, 11×5, 12×4, 14×4, 15×4, 16×5, 17×4, 18×2, 19×4, 20×3, 22×2, 36×2, 39×2, 40×3, 44×3, 45×2, 46×3, 47×3, 50×2, 51×2, 53×2; 69 | 11 × 9, (4, 1) → (1, 2), 6 legs |
| 89F (88) | 18208 → 21331 | 8×3, 9×2, 10×3, 11×4, 12×4, 14×9, 15×8, 16×8, 17×3, 18×3, 19×4, 20×6, 22×4, 36×4, 39×3, 40×3, 44×2, 45×3, 46×5, 47×3, 50×3, 51×4, 53×3; 94 | 11 × 11, (3, 6) → (8, 5), 8 legs |
| **90F (89)** | 21331 → 25784 (results) | **69 black crab** + bombs | boss floor, below |

## 85F: the mob floor (roper list)

ROSTER said "none (roper list)". Live: 30 actors at arrival, all in state 0: type 19 ×11, 20 ×6, 21 ×5 (HP 230 = 25 · 9.2), 18 ×4, 17 ×4
(HP 138). `gate` 3; exit cell (1, 1), start (1, 3) at (−40, −120).
- 17 chained kill searches (nearest first, `--avoid "hp < H"`, `--hold 30`), all found, **zero damage**. The other 13 slots were freed
  without being the search target (spins / adjacent swings; not attributed).
- **f11652: last actor freed → `estage` 1, `gate` 0.** The portals are the special objects at `0x695ef78 + i·0x2a0`. Next-floor circle
  at **(−76.5, −120)**, return door at **(−83.5, −120)**: in the START cell row (1, 3) side, not at the exit cell, as on 75F.
  `portal` 1 at f11897 (−75.65, −119.65) → "Go to the next floor?" → `A` → 86F f11935.

## 90F: the black crab (type 69)

**Live stats** (`0x5c23f0 + 69·0x68`, +0x2c..+0x40): **HP 150, (+0x30) 25, AT 40, DF 30, MA 20, MD 30**. The card's AT 20 / MA 0 / MD 10
were wrong. +0x44 = 1.3, then 4, 3, 4.5 (body radius base), 0.45, 1, 3.6. Live HP **1410** (150 · 9.4, L42). Bomb row (type 37):
HP 15, 10, AT 16, **DF 16** (the card said 20), MA 18, MD 10. Live bomb HP 141.
By the card's formula, a jab = `ftol(40·9.4/2 − 84/4)` ≈ **167**. Two jabs would kill Louie (224). None landed.

Setup: crab at (−40, 0, −34), yaw π (facing the party), state 8. Party at (−40, −45), 11 in front of it. `lock` 0 for the intro;
**state 8 → 0 at f21572 = etick 240** (arrival f21331).

Fight f21572 → f25309 (3,737 frames). Columns: crab state / sub, crab HP, live bombs (slots 1..8), the bomb types.

| frame | `a0st`/`sub` | crab HP | what |
|---|---|---|---|
| 21632 | 0/0 | 1410 | bomb 1 = **type 39 (green)** at state-0 tick 60 (`+0xa64` started at 0 here, not −60) |
| 21696 / 21904 | 5 → 0 | 1410 | bubbles (200 ticks) |
| 21972 | 0 | 1410 | bomb 2 = type 39 (2 bombs + crab = the cap of 3) |
| 22036 | 3 | 1410 | turn + slam |
| 22084 | 3 | 1410 | bomb 1 killed (lit, `+0x428` 2) |
| 22195 | 1 | 1410 | jab (pincer 6) |
| **22269** | **6/0** | 1410 | **green blast (185 ticks after the kill) flips it**; `unhit` 1 from 22274 (launch, 58 ticks) |
| 22327..22486 | 6/1 | 1410 → 1347 | on its back; hits from the FRONT (`a0rel` 0.00): **−32, −31** |
| 22486..22581 | 6/2 | 1347 | get-up, `unhit` 1, 95 ticks → state 0 (`sub` stays 2 until the next flip) |
| 22581 | 0/2 | 1347 → 1320 | a hit on the first state-0 tick: −27 (DF scale not yet back to 5) |
| 22645 / 22827 | 1 / 2 | 1320 → 1316 | jab 6, then jab 7; front-arc body hits in normal states: −1, −1, −2 |
| 22827 | 2 | 1316 | bomb 3 = **type 37 (red)** |
| **22858** | **6/0** | 1316 | bomb 2 (green, lit 22665) blows → flip 2, **0 blast damage** |
| 22917..23083 | 6/1 | 1316 → 1179 | side / back hits: −32, −31, −36, −38 |
| 23294 / 23626 | 5 / 3 | 1179 | bubbles; bomb 4 = **type 38 (blue)**; turn + slam |
| 23764 | 0 | 1179 → 1178 | red bomb lit |
| 23789 | 1 | 1178 | jab 6 |
| **23950** | **6/0** | 1178 → **999** | red blast (186 ticks after the lighting, not 300: pushed into contact?) → flip 3, **−179 blast damage**; the blast lit the blue bomb |
| **24015** | 6/1 | 999 → **789** | **blue blast (65 ticks ≈ its 60-tick fuse) on the FLIPPED crab: −210**. No re-flip, but damage |
| 24063..24209 | 6/1 | 789 → 647 | hits −36, −29, −77 (2 hits) |
| 24373 / 24420 | 0 / 3 | 647 | bomb 5 = **type 40 (yellow)**; turn + slam |
| 24583 / 24705 | 1 → 2 | 647 → 642 | bomb 6 = type 40; **jab chain 1 → 2** at 24705; front-arc chips −1, −2, −1, −1 |
| 24973 | 5 | 642 | bubbles |
| **25034** | **6/0** | 642 → **352** | yellow blast (lit 24691, 343 ticks) → flip 4, **−290 blast damage** |
| 25093..25268 | 6/1 | 352 → 119 | back hits (`a0rel` 2.66..2.93): −47, −92 (2 hits), −49, −45 |
| **25279** | 6/1 | 119 → **0** | bomb 6 (lit 25268) blows on contact 11 ticks later; the blast kills it. `a0use` 2 |
| 25454 | | | slot freed (death + 175) → `estage` 1; circle (−35, −40), return door (−45, −40) |

- **Arms hit:** intro, walk, tick-60 / tick-120 decisions, jab 6 (×4), jab 7 (×2), jab chain 1 → 2, turn + slam (×3), bubbles (×3),
  bombs called (types 39, 39, 37, 38, 40, 40: all four colours), the 3-actor bomb cap, flips by blast (×4: launch 58 ticks, then
  on its back 150..200 ticks, get-up 95), front / side / back damage while flipped, 1–2 chips on front-arc hits in normal states, a
  blast lighting another bomb, a contact blow (pushed dying bomb), death sweep.
- **Not hit:** spray (state 4), state 7, any crab attack landing on the player (0 hits taken), a blast hitting the player, player death.
- **Blast damage to the crab is new** (the card says a blast only flips it). 4 of 6 blasts damaged it: −179, −210, −290, −119+ (the
  kill). The two that flipped it during a jab (states 1 / 2) did 0; the ones in states 0 / 5 / 6 did 179..290. Not read in code.
- Damage per sword hit while flipped (ATK 64): front 31–32, side 36–38, back 45–49. In normal states: front arc 1–2, outside 0.

**Fight method** (`crab.py`, one search per phase, `-j 2`, `--avoid "hp < H − 6"`):
- **flip:** goal = `a0st == 6` and within 8 of the crab; target = the bomb nearest the crab. Cell = (crab state, bomb `+0x428`,
  bomb HP / 30, bomb − crab / 3, player − crab / 3). Score = 30·lit − bomb-to-crab distance + 60·flipped − 3·flipped·distance.
  `--hold 40 --hold-no-goal`.
- **dmg:** score `-a0hp`, goal = −130, `--hold 30 --hold-no-goal`.
- **get-up:** goal `a0st != 6`, score = distance to a point 9 behind the crab.

13 searches. The first damage pass (goal "or `sub` 2") was satisfied by waiting; it was re-done from its checkpoint without the get-up
clause. Without a closeness term in the flip goal, the first flip found the player 16 away, and only 2 hits landed.

## The return and the day end

| frame | what |
|---|---|
| 25454..25579 | `estage` 1; waited 125 ticks (pad dead after the clear) |
| 25592 / 25595 | walk to (−47.7, −39.8); `L . A` → sword on the door: **`s32(0x695f218 + 0x268)` 0 → 1** |
| 25637 | `L` → `portal` 2 at (−45.84, −39.97) → "Return to town now?" → `A` |
| 25784 | results (`scene` 9; bag 20/20, nothing eaten); `B` → town map f25861, **`stime` 1 → 3** (not 2: a long trip spends two periods) |
| 25966 | `D L D` → `wm_cur` 0 → home (`stime` 3) |
| 26084..26323 | shop door by search → map → `R` (3) → Merchants' Guild → `B` → `D` (0) → home: **`stime` 3 → 4** |
| 26403..26806 | "Thanks for all your help today, Tear!" → Daily Profits (Profits −15,090, score 7942 → 8414 (+472)) → **day dword 30** (f26789) → "Day 31" |
| 26976 / 27073 / 27175 | `(~ .*20)` ×n → pause / `D` ×4 → `pm_sel` 4, `A` → slots (cursor on 020) / `A` "Overwriting file. Are you sure?" `A` → saved; `rd endsave` → `save:6809401b79a9` |

## Card corrections

- `crabs.md`: type 69's live table row is HP 150, AT **40**, DF 30, MA **20**, MD **30** (card: AT 20, MA 0, MD 10, "both types"). Bomb DF
  is **16**, not 20. Blasts DAMAGE the crab (−179..−290 live), including while it is already flipped (no re-flip). A 69 bomb drop came
  at state-0 tick 60 of the first walk (clock started at 0 after the intro). Flips last: launch 58, on its back 150..200, get-up 95. `+0xa5c`
  stays 2 after the get-up.
- ROSTER d4 84 (85F): mob floor, 30 actors (19 ×11, 20 ×6, 21 ×5, 18 ×4, 17 ×4 at L41), exit hidden until all freed, portals at
  (−76.5 / −83.5, −120).
- ROSTER d4 89 (90F): live 1410 confirmed; adds = 37..40, all four seen, at most 2 at once.

## Tool gaps

1. `actors(T >= 37 and T <= 40)` over all 64 slots is 833 postfix tokens: refused as a goal / score. Slot ranges work
   (`actors(.., 1, 8)`). A built-in "bombs alive" name would help on bomb floors.
2. A search goal that a wait can satisfy (`sub == 2`) wastes the phase. Damage phases need goal = damage only, with `--apply-best`.
3. `rd verify --real` runs its client on maximus. A maximus OOM kills it (exit 137) while the Windows session lives on.
4. Facing-relative positions (a point behind the boss) need `sin(yaw)` / `cos(yaw)` by hand. An `aNbehind(d)` helper would help.
