# Probe: crystal-1f-20f

- **Run:** `crystal-10f` · **start (CRAFTED lineage):** `save:7874cefc5cb5` = `lapis-91f-100f`'s end save (shop, day dword 31 = HUD
  Day 32 morning, `stime` 0, Louie L47, slot 020, **49,410 pix** (not 58,580: that was the 91F leg's start)). Chain ← `6809401b79a9`
  ← … ← `b4766a99d74b` = `lapis-51f-60f`'s end save with bank `+0x2bcf7` cleared by `tools/craft_pix_save.py`: this save descends
  from a crafted one. Dungeon 5 was opened by event 12/20 in `lapis-91f-100f`. No task file.
- **Goal as given:** Crystal Nightmare 1F → 10F in one trip, then the return door. **What the game has instead:** d5 floors 0..28 are
  plain random floors (`FUN_0043195d` is false for them, no setup / event arm), and the 10F exit cell holds TWO portals: the
  next-floor circle (object kind 8) and a **return portal (kind 6) 5 units +z of it**. My 10F step took the kind-8 one ("Go to the
  next floor?" → Yes) and the trip went on; the nav script's 20F exit search then landed on the kind-6 one ("Return to town now?",
  tapped Yes). So the movie covers **1F → 20F** in one trip and leaves from 20F. Zero damage the whole way.
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`). Floors: the `lapis-71f-80f` nav script (BFS of `0x73dfd08` + one scored
  search per cell, `-j 2`, `--hold 45 --hold-no-goal`, `--avoid "hp < 228"`); every floor found on the first pass.
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`, `RD_HOST=win`). Nothing ran under wine on maximus.
- **Movie:** 49,539 frames: Day 32 (guild closed) burned → Day 33 guild → one trip 1F → 20F → return portal → town → day end → Day 34
  morning → Save. Filed as `tests/scenarios/crystal-1f-20f/trace.jsonl`. **No retail golden** (next: `rhost.py golden crystal-1f-20f`,
  `rhost.py triage crystal-1f-20f`). Not triaged.
- **Verdict (2026-11-14):** `rd verify crystal-10f` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 49539 rng 6694c291 sem 14497fe56c33e6ab dev 269aa3619a2596a3  (unskipped retail)  [win]
  live session:          frame 49539 rng 6694c291 sem 14497fe56c33e6ab dev 269aa3619a2596a3  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 49539 rng 6694c291 scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:2b9e8df3acad` (slot 020 overwritten from the shop's pause menu at f49539: day dword 33 = HUD Day 34, `stime` 0,
  44,760 pix, SCORE 8948 → 9061, TIME 4:59:34 → 5:13:15).
- **Payment state:** the final payment (500,000, `week5-gameover`) is collected on the night of day dword 35 (the form shows "Day 36");
  the end save has 44,760 pix and two days left — this lineage cannot pay it. No morning event on HUD Day 33 or Day 34.
- **Damage taken: none** (`rd scan` 5334..47700 `--where "hp < 228 or sp != 51"`: 0 rows). No kills that mattered, no level-up
  (Louie stays L47, HP 228, SP 51), no food eaten.

## Before the dungeon

| frame | what |
|---|---|
| 172 | shop, HUD Day 32 morning (`.*60 A .*30 A .*80`); control at once |
| 461..741 | door by search → town map; map cursor from Home: `U` → 2, `U` → 5, `R` → 6. **Adventurers' Guild "Closed"** (day dword 31; the 7-day rule, as on dword 24 / 31 in earlier probes): A does nothing |
| 797..1226 | Market (wm 1). Food (`L` ×2 from Necklaces… tab order Necklaces → … → Medicines → Food): Apple 168, Cherry / Watermelon / Beef Bowl Out Of Stock, Walnut Bread 70, Candy 168, Taiyaki 616 (Price Down). Owned: Apple 14, Taiyaki 12, Watermelon 2, Beef Bowl 2, Cherry 2. Bought nothing |
| 1303..2551 | day burned: home (`stime` 1) → door → Merchants' Guild → home ×3 (`stime` 2 on leaving home, 3, then 4 on entering home in the evening, f2049) → "G'night" → Daily Profits → **day dword 32** (HUD Day 33). The door at noon / evening: `L*4 DL*8 R*40 A .*7 A .*7 A .*7` from (2, −3.24) |
| 2551..2759 | morning, no event; pad dead 208 ticks |
| 3002 / 3078 | door by search → map `wms` 1121011 (guild open) → `U U R` → guild |
| 3118 | **Roster:** Louie **L47** (fee 4,650), Charme, Caillou, Tielle, Elan, Nagi, Griff, **Arma (Golem)** — all L40. Arma is hirable now |
| 3139..4842 | hire Louie; help scroll (`A` dismisses), `A` enters slot 1. Category ring (`R`): Spears → Clothes → Robes → Breastplates → Armor → Bracelets → Helms → Shoes → Treasures → Ingredients → Cards → Food. After a pick the list drifts to the next category. Packed **Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, Taiyaki ×12, Baked Yam, Beef Bowl ×2, Watermelon** (20/20) |
| 4983 / 5045 | Depart → plates (cursor on Lapis) → `D` → **Crystal Nightmare** rows: "Floor 1～10", "Floor 11～20", "Floor 21～30", "Boss Rush", all `--:--:--`, cursor on row 0 |
| 5062 | **1F**, fee 4,650 (49,410 → 44,760), `stime` 0 → 1; `0x438b4c8` 5, start floor `0x438b4d8` 0 |
| 5293 / 5334 | "Equip items in bag?" → `A`; HP 228, SP 51 |

## Per floor

Level `ftol(floor·1.7 + 50)`: L50 (1F) … L65 (10F) … L82 (20F); HP = table HP·(1 + 0.2·L) — 15 → 165 at 1F, 210 at 10F, 261 at
20F (live). Enemy lists = the actor table at arrival (`type × count (HP)`). Banner = `rd text --at-frame` arrival + 60.

| floor (`floor`) | frames | banner | enemies (arrival) | grid, start → exit, legs |
|---|---|---|---|---|
| 1F (0) | 5062 → 7532 | This level is full of traps. | 0×2 (165), 1, 2×3, 4×4 (385), 6×2, 8 (198), 10×2 (55), 11, 13×2 (110), 15×4, 16×5 (220), 17×4, 18, 22×3, 35×2, 37×2, 44×4 (132), 50×2, 52×3 (220); 48 | 7 × 11, (1, 3) → (5, 4), 5 |
| 2F (1) | 7532 → 10057 | The floor is now slick! | same 19 types; 46 | 7 × 11, (3, 4) → (2, 8), 7 |
| 3F (2) | 10057 → 11135 | The automapper will not work here. | same; 35 | 7 × 11, (3, 4) → (4, 5), 2 |
| 4F (3) | 11135 → 12780 | Will-o'-Wisps will appear soon. | same + 29 ×1; 66 | 9 × 11, (5, 7) → (6, 4), 4 |
| 5F (4) | 12780 → 13796 | Will-o'-Wisps will appear soon. | same; 34 | 11 × 7, (7, 2) → (9, 2), 2 |
| 6F (5) | 13796 → 14911 | Enemy movement speed increased! | **list 2:** 6, 8, 10, 11, 12, 13, 15, 16, 17, 18, 19, 22, 35, 37, 38, 44, 45, 50, 51, 52; 48 | 9 × 9, (5, 3) → (6, 4), 2 |
| 7F (6) | 14911 → 17379 | Enemy attack power doubled! | list 2; 41 | 9 × 9, (4, 3) → (1, 5), 5 |
| 8F (7) | 17379 → 20428 | This level is full of traps. | list 2; 39 | 11 × 7, (3, 3) → (7, 2), 7 |
| 9F (8) | 20428 → 21456 | The automapper will not work here. | list 2; 58 | 9 × 9, (5, 1) → (6, 2), 2 |
| **10F (9)** | 21456 → 24335 | The automapper will not work here. | list 2 (210 for HP 15); 66 | 9 × 11, (2, 5) → (6, 8), 7; **exit cell: kind 8 at (−240, −320) + kind 6 return at (−240, −315)**; f24308 "Go to the next floor?" → Yes |
| 11F (10) | 24335 → 27219 | Enemy movement speed increased! | list 2 + 29; 48 | 7 × 11, (1, 6) → (4, 3), 6 |
| 12F (11) | 27219 → 28415 | Enemy attack power doubled! | list 2; 44 | 9 × 9, (5, 3) → (5, 1), 2 |
| 13F (12) | 28415 → 30114 | The automapper will not work here. | list 2 + 29; 40 | 9 × 9, (3, 3) → (5, 1), 4 |
| 14F (13) | 30114 → 33037 | Will-o'-Wisps will appear soon. | list 2; 44 | 7 × 11, (3, 3) → (2, 8), 8 |
| 15F (14) | 33037 → 34945 | The automapper will not work here. | list 2; 53 | 7 × 11, (3, 5) → (5, 3), 4 |
| 16F (15) | 34945 → 37102 | Enemy defense power doubled! | **list 3:** 8, 9 (320), 10, 11, 12, 14 (320), 15, 16, 17, 18, 19, 20, 22, 36, 38, 39, 44, 45, 46 (288), 47 (560), 50, 51, 53; 46 | 9 × 9, (1, 4) → (4, 4), 5 |
| 17F (16) | 37102 → 39208 | This level is full of traps. | list 3; 50 | 9 × 9, (4, 5) → (7, 2), 6 |
| 18F (17) | 39208 → 41818 | Adventurers now nearly blind! | list 3; 37 | 11 × 7, (3, 1) → (2, 5), 7 |
| 19F (18) | 41818 → 46302 | Adventurers now nearly blind! | list 3; 59 | 9 × 9, (2, 1) → (3, 7), 11 |
| **20F (19)** | 46302 → 47700 | The floor is now slick! | list 3 (261 for HP 15, 47 = 609); 70 | 9 × 11, (4, 1) → (3, 4), 4; **exit cell: kind 8 at (−120, −160) + kind 6 return at (−120, −155)**; ≈f47644 the search's `portal > 0` landed on the return one: "Return to town now?" → Yes |

**Live table stats** (`0x5c23f0 + type·0x68`, s32, HP / AT / DF / MA / MD): 0 15/14/4/0/5 · 1 20/18/14/0/0 · 2 15/16/4/0/10 ·
4 35/20/10/0/40 · 6 15/20/4/15/10 · 8 18/20/10/20/20 · 9 20/20/14/24/25 · 10 5/17/4/0/0 · 11 5/19/4/0/0 · 12 5/21/4/0/0 · 13 10/14/0/0/0 ·
14 20/20/0/0/0 · 15 15/20/4/0/0 · 16 20/17/4/0/0 · 17 15/16/4/0/0 · 18 15/18/4/0/0 · 19 15/20/4/0/0 · 20 15/22/4/0/0 · 22 15/16/8/0/0 ·
29 15/20/20/0/10 · 35 15/16/30/16/10 · 36 15/16/20/16/20 · 37 15/16/16/18/10 · 38 15/16/18/20/10 · 39 15/16/20/22/10 · 44 12/17/18/0/0 ·
45 14/19/20/0/0 · 46 18/21/23/0/0 · 47 35/24/27/0/0 · 50 15/20/14/0/15 · 51 15/22/18/0/20 · 52 20/0/16/20/10 · 53 15/0/20/24/10.

## The 10F / 20F exits (the "Floor N～N+9" checkpoints)

- Objects (`0x695ef78 + k·0x2a0`: x +0, z +8, kind +0x8c, state +0x268): on 10F and 20F object 0 = kind 8 (next floor) at the exit cell
  centre, object 1 = **kind 6 (return)** at the same x, z + 5. Other objects kinds 4 / 5. `FUN_00442cef` (IDA 44683): within 3 of an
  object, `0x438bea8 = (kind == 6)`; the prompt is "Return to town now?" when it is 1, else "Go to the next floor?".
- At the prompt (same function, IDA 44850): d5, floor < 30, `floor % 10 == 9` → the row's best time `bank+181692 + 4·(floor/10)` is set from the trip
  counter `0x438b1e8` if empty or larger. Live: **row 0 = 15591** (10F prompt f24301), **row 1 = 19822** (written before f47680 — the
  20F prompt). The counter only runs while `0x438be98 == 0`; it is not frames since departure (15549 at f24235 vs 19173 frames).
- Return: f47700 `0x438be98` 2 (= returned; 1 = fell, 3 = cleared the last floor), results "CAME BACK ALIVE!" (sprite, picture
  `view/f047840.png`) at scene 9 f47821, town map f47871 (`stime` 2, `wms` 1121211). A queued `A` from the tap entered the guild
  (scene 11, f47915); `B` → map → `D D` → home.

## The return and the day end

| frame | what |
|---|---|
| 47992..48055 | guild → map (`wm_cur` 6 → 3 → 0) → home, `stime` 2 |
| 48055..48574 | door → Merchants' Guild → home ×2: `stime` 2 → 3 (leaving home), → **4** on the evening door attempt (f48574) |
| 48600..49021 | "G'night!" / "Good night." → Daily Profits **Day 33**: Profits −4,650 (the hire), Found **Lobster**, Score 8948 → 9061 (+113) → day dword 33, "Day 34" |
| 49021..49229 | morning, no event; pad dead 208 ticks |
| 49233 / 49347 / 49418 / 49539 | `~` → pause, `D` ×4 → `pm_sel` 4, `A` → slots (cursor on 020) → `A` "Overwriting file. Are you sure?" → `A` → saved; `rd endsave` → `save:2b9e8df3acad` |

## Card corrections

- ROSTER d5: floors 0..28 are plain random floors (no setup / event arm; `FUN_0043195d` false), NOT mob floors — the exit shows from
  the start. Floors 9 and 19 (and by the same rule 29 is the rest floor) carry a return portal (kind 6) beside the next-floor circle; the
  prompt there writes the row time. Enemy lists by floor band (live): 1F–5F list 1 (19 types, slimes 0..4, 22, 35, 37, 44, 50, 52),
  6F–15F list 2, 16F–20F list 3 (knights 44..47, 53, golem adds 36 / 39). 29 appears on 4F, 11F, 13F.
- Arma (Golem, L40) is on the guild roster after `lapis-91f-100f` (event 12/20).

## Tool gaps

1. The nav script's exit goal `portal > 0` does not say WHICH portal: on a d5 checkpoint floor the return and next-floor portals sit
   5 units apart and `portal` is shared. A named `ptype` var (kind of the object within 3, i.e. `0x438bea8`) would let a goal say
   `portal > 0 and retp == 1` / `== 0`. This is why the movie went to 20F instead of returning from 10F.
2. Floor banners are not on screen at the arrival tick (`ticker` 30 but text empty); `rd text --at-frame arrival+60` per floor costs
   ~30 s each. A `rd scan --text` column (first text row at y 52) would collect them in one replay.
3. `rd tap A --until floor != N` keeps pressing after the trip ended (scene 8): one press leaked into the guild. A `--until` that also
   stops on `scene != 1` would be safer in nav scripts.
