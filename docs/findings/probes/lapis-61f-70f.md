# Probe: lapis-61f-70f

- **Run:** `lapis-70f` · **start:** `save:78c391141547` (end save of `lapis-51f-60f`: shop, day dword 27 = HUD Day 28, `stime` 0,
  Louie L44) · no task file (goal: Lapis Ruins 61F → 70F in one trip, the 65F mob floor, the 70F dark golem, back, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`); normal floors = BFS of the maze grid + one scored search per cell
  (`-j 2`, 60 s); 65F = one chained kill search per enemy down (57 searches, `--hold 30`); 70F = chained −80 HP body searches + add
  kills + two "survive the pattern" searches (`--hold 60`).
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`, `RD_HOST=win`).
- **Movie:** 50,597 frames (Day 28 guild → one trip 61F → 70F → return door → day end → Day 29 morning → Save). Filed as
  `tests/scenarios/lapis-61f-70f/trace.jsonl`. **No retail golden** (next: `rhost.py golden lapis-61f-70f`,
  `rhost.py triage lapis-61f-70f`). Not triaged.
- **Verdict (2026-11-14):** `rd verify lapis-70f` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 50597 rng ac232600 sem 7f0153d27f5ee005 dev a2e416ec55267a24  (unskipped retail)  [win]
  live session:          frame 50597 rng ac232600 sem 7f0153d27f5ee005 dev a2e416ec55267a24  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 50597 rng ac232600 scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:535305f4273b` (slot 020 overwritten from the shop's pause menu: day dword 28 = HUD Day 29, `stime` 0,
  278,020 pix, SCORE 7221 → 7582, TIME 4:11:17 → 4:25:16).
- **Zero damage taken on the whole trip** (HP 215 → 219 by the level-up only). One level-up: **L44 → L45 at f12814** (65F; HP 215 → 219,
  ATK 62 → 63, DEF 82 → 83). Food: 2 Taiyaki on 70F (SP 4 → 34 → 49, max 49).

## Before the dungeon

| frame | what |
|---|---|
| 137 / 138 | shop, Day 28 morning (day dword 27); control |
| 471 | door by search → town map, `wms` 1121011 (guild open on dword 27), `wm_cur` 0 |
| 585 / 602 | `R U` → `wm_cur` 6 (the first `R U` at f471..476 was eaten: the map ignores input for ~50 ticks after arrival), `A` → Adventurers' Guild: Louie **L44**; Charme / Caillou / Tielle L40 |
| 664..2169 | hire `A A`; the 3rd `A` enters slot 1's category list (opened on Spears). Categories (R): Spears, Clothes, Robes, Breastplates, Armor, Bracelets, Helms, Shoes, Treasures, Ingredients, Food (no Swords). **Pack flow: after a pick the cursor returns to the SLOT list on the same slot** (it does not advance); per item `D` (next slot) `A` (enter its category, remembered row) [`D`/`U` row] `A` (pick). Packed **Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, Taiyaki ×4, Baked Yam, Apple ×11** (20/20). Category per new slot = the previous pick's; food row index kept |
| 2320 / 2361 / 2397 | `U` ×20 + `A` → "Depart!" → plates (cursor on Lapis) → `A` → rows, cursor on the remembered **"51～55"**; `D D` → **"61～65"** (best `--:--:--`) |
| 2414 | **61F**, fee 4,350 (282,370 → 278,020), `stime` 0 → 1 |
| 2614 / 2676 | **"Equip items in bag?" appears 200 ticks after arrival** (`equipq` 1 at f2614 and counts up per tick); an `A` on its first tick is ignored, `A` at f2676 → **ATK 62, DEF 82**, HP 215, SP 48 |

## Per floor

Level `floor/5 + 25`: L37 (61F–65F: table HP 15 → 126), L38 (66F–70F: 15 → 129, 150 → 1290). Enemy lists = actor table at arrival (`type × count
(HP)`; total after `;`). Normal floor = wait `ticker == 0`, BFS, one scored search per cell (`--hold 30 --hold-no-goal` added from 69F,
below), a search for `portal > 0` at the exit cell, `rd tap A --gap 10 --until floor == N` (38 ticks).

| floor (`floor`) | frames | banner | enemies | grid, start → exit, legs |
|---|---|---|---|---|
| 61F (60) | 2414 → 5540 | not read | 8, 9, 10×3, 11, 12×3, 14×4, 15×4, 16×8, 17×3, 18×2, 19×3, 20, 22×3, 36×3, 38×2, 39×4, 44×4, 45×3, 46×4, 47×2, 50×2, 51, 53×2; 64 | 9 × 11, (1, 7) → (7, 4), 9 legs |
| 62F (61) | 5540 → 7178 | "Adventurers now nearly blind!" | 8×6 (151), 9×4 (168), 10×2 (42), 11×4, 12×3, 14×7 (168), 15×7 (126), 16×11 (168), 17×5, 18×2, 19×2, 20×5, 22×2, 29, 36×2, 38×2, 39×2, 44×2 (101), 45×2 (118), 46×2 (151), 47×5 (294), 50×2, 51×3, 53×6; 89 | 11 × 11, (6, 1) → (7, 4), 4 legs |
| 63F (62) | 7178 → 8483 | "Enemy attack power doubled!" | 8×4, 9, 10×3, 11×3, 12×2, 14×5, 15×5, 16×5, 17, 18×2, 19, 20, 22×4, 36×4, 38×2, 39, 44, 45×3, 46×3, 47, 50×2, 51, 53×4; 59 | 11 × 9, (3, 5) → (5, 6), 3 legs |
| 64F (63) | 8483 → 9720 | "Defense power doubled for everyone!" | 8×2, 9×2, 10×3, 11×3, 12×3, 14×8, 15×4, 16×4, 17×2, 18×2, 19×2, 20×4, 22×5, 36×2, 38×2, 39, 44×2, 45×2, 46×3, 47×2, 50×5, 51×4, 53×3; 70 | 9 × 11, (4, 7) → (5, 9), 3 legs |
| **65F (64)** | 9720 → 30407 | none | **50 ×10, 51 ×10 (126), 52 ×10 (168), 53 ×9 (126)**; 39 | mob floor (below); 5 × 7, (1, 3), exit cell (1, 1) |
| 66F (65) | 30407 → 33106 | "This level is full of traps. Enjoy!" | 8, 9×3, 10, 11, 12×2, 14×6, 15×5, 16×3, 17, 18, 19, 20, 22, 36, 38, 39, 44×2 (103), 45 (120), 46×4 (155), 47×2 (301), 50×4, 51, 53; 45 | 9 × 9, (1, 3) → (4, 5), 7 legs (no trap hit) |
| 67F (66) | 33106 → 35830 | not read (none at arrival +3) | 8×2, 9×2, 10×4, 11×4, 12×4, 14×3, 15×2, 16×8, 17×2, 18×3, 19×2, 20×3, 22×2, 36×3, 38, 39×3, 44×2, 45×4, 46×4, 47×2, 50×3, 51, 53×2; 66 | 11 × 9, (4, 1) → (2, 6), 7 legs |
| 68F (67) | 35830 → 37159 | "Enemy defense power doubled!" | 8, 9×2, 10×2, 11, 12, 14×2, 15×3, 16×7, 17×2, 18, 19×3, 20×4, 22×4, 36, 38, 39, 44×4, 45×3, 46, 47, 50, 51×2, 53×2; 50 | 9 × 9, (5, 3) → (3, 4), 3 legs |
| 69F (68) | 37159 → 39447 | "Enemy movement speed increased!" | 8×6, 9×3, 10×3, 11×3, 12×4, 14×8, 15×6, 16×12, 17×3, 18×5, 19×4, 20×2, 22×4, 36×2, 38×3, 39×5, 44×3, 45×3, 46×4, 47×3, 50×4, 51×3, 53×5; 98 | 11 × 11, (4, 7) → (8, 8), 5 legs (see below) |
| **70F (69)** | 39447 → 49017 (results) | none | **64 + 65 + 66 (1290 each, L38)**; adds 35 | boss floor, below |

- **69F, leg (7, 7) → (8, 7):** a scored leg without `--hold` applied a path that ended 1.8 from a type 20 with its attack committed:
  from f38837 every branch was hit within 4 ticks (−33, 219 → 186). Rewound to f38428 and re-ran the legs with `--hold 30
  --hold-no-goal --hold-actions . U D L R`: clean. The (8, 7) → (8, 8) corridor had 4 type-16 kobolds (`+0xae4` 2) standing in a
  row at z −290, x −317..−323 (age 172: spawned there); the held search slipped past them.
- 62F's exit was `(7, 4)` like 61F's (different grids). No kills on the normal floors.

## 65F — the mob floor (`gate` 3, generic boss-floor event)

39 actors at arrival, all in state 0: skeletons 50 ×10 and 51 ×10 (HP 126, `unhit` 0), ghosts 52 ×10 (HP 168) and 53 ×9 (HP 126),
**`unhit` 1** at arrival (they become hittable when they come out; the kill searches reached each one). Live table stats: 50 HP 15 /
AT 20 / DF 14 / MA 0 / MD 15; 51: 15 / 22 / 18 / 0 / 20; 52: 20 / 0 / 16 / 20 / 10; 53: 15 / 0 / 20 / 24 / 10. **The exit is hidden
until every actor is freed** (the next-floor circle at the exit cell centre (−40, −40) gave nothing at `gate` 3: 1,300 frames of a
walk-and-tap loop, cut from the movie).

- **Skeletons revive twice** (`engine-quirks` / obsidian-31f-40f): each goes `+0x428` 2 with HP back to 126 and stands up ~100 ticks
  later; 3 downs each. Downs counted over f9720..29250: **type 50: 30, 51: 30, 52: 10, 53: 9** (= 10 / 10 / 10 / 9 actors).
- 57 chained searches (goal: the nearest live actor's `+0x428 != 1` or HP ≤ 0; cell = its HP/8, the relative position /1.5, `p_act`,
  `hp/10`; actions = aimed swing, aimed spin, approach, back off, two strafes, 4 walks, idle; `--avoid "hp < H" --hold 30`): **57/57
  found, zero damage**; 47 .. 900 frames each (ghosts the slowest: 500–900).
- f29245 the last actor freed → **`estage` 1, `gate` 0 the same tick**; the pad stays dead until ≈ etick 120+ (searches "nothing scored"
  at etick 54); f30367 `portal` 2 at (−40.25, −40.95) → `A` → **"Go to the next floor?"** (`portal` 15) → `A` → 66F (f30407).

## 70F — the dark golem (card `golem.md`)

Run-local names: `gidx` = `s32(0x76c3d0)` (B+0xa60), `gadd` = `s32(0x76c3d8)` (+0xa68), `gtim` = `s32(0x76c3d4)` (+0xa64).

| frame | what |
|---|---|
| 39447 | arrival: party (−40, −45); **64 / 65 / 66 all at (−40, −12, −35)**, state 8, **HP 1290 each** (= 150 · 8.6, L38); `gate` 3; arms `+0xb08` (unhit) 1 from the next tick (f39448) |
| 39688 | state 8 → 0 after 240 ticks, `lock` −1 |
| 39692..39975 | pause: Items is `D` from the main page (`pm_sel` 1; `A` on `pm_sel` 0 opens Louie's status / controls pages). Bag: Scrap Plate, Wooden Shield, Old Muffler (Adventurer's Possession), Taiyaki ×4, … ; `D D D A A` = eat: **SP 4 → 34 → 49** (Taiyaki +30, cap 49) |
| **40011 / 40049** | **add #1 / #2 (type 35)**: `need` 2 — the second follows ≈ 38 ticks later (`gtim` counts 30 per spawn) |
| 40049 / 40246 | idx 0 → right punch (`a0sub` 0), idx 1 → right punch (act 1 = side 0 again) |
| 40459 | idx 2 (act 2, RNG) → **left punch** (`a0sub` 1) |
| 40728 | idx 3 → **double** (state 2) |
| 40937 / 41114 | idx 4 → **jump at the player** (state 3, sub 0), idx 5 → **jump home** (state 3, sub 1) |
| 41283 / 41556 / 41829 | idx 6 / 7 / 8 (act 2): right, left, left |
| 42074 / 42251 / 42454 | idx 9 / 10 → jump at (act 4) ×2, idx 11 → jump home (act 5); `gidx` wraps 11 → 0 (f42454) |
| 42044..46700 | **adds killed** (7 kills, all type 35, HP 129, ~430–800 frames each): `gadd` 3 (f42044), 4 (f42613), **5 (f43265): `gtim` −60** (> 2·need), 6, 7, 8, **9 (f45886): `gtim` −120** (> 4·need), **10 (f46700): no spawn** (≥ 5·need) — **9 adds over the fight**; `gadd` keeps counting (11 at f47023) with nothing spawned |
| 43057 / 43919 / 44192 / 44461 / 45669 | 2nd / 3rd cycle act-2 picks: left, left, right, right, right |
| **47301** | **body dies** (`+0x428` 2) in state 3 (jump at, idx 10), `p_inv` 1200 |
| 47405 | arms `+0x428` → 2 (≈104 frames after the body, slow-mo) |
| **47476** | all three freed → **`estage` 1, `gate` 0 the same tick**; next-floor circle (−35, −40) = object 0, return door (−45, −40) = object 1 |

**Damage dealt** (Louie ATK 62 → 63, body DF 30): sword front state 0: **25–27**; sword counter (state 1 / 3, front): **29–32**; sword
side/back state 0 (`a0rel` 2.77): **36**; spin (5 SP) counter state 1: **56–67** (swords + spins f40011..40481 took the body 1290 → 549
in 470 frames). **Damage taken:** none applied. Measured in an undone branch (f41970, right fist at state-1 t 100, player 7.3 from the
body centre): **219 → 166 = 53** (the card's 20F fist was 30–34 at L20).

**Arms (golem.md § 7) hit:** rise, walk + pattern advance (idx 0..11, wrapped twice), right punch (act 0), act 1 (right), left punch
(act 2), random side both ways (idx 2 / 6 / 7 / 8 in three cycles), double (act 3: f40728, f43372, f45950), jump at (act 4), jump home
(act 5), add spawn (**dark variant: `need` 2, add type 35** — first live record), add back-off **−60 and −120**, the spawn cap
(`gadd` ≥ 10 → none), death (arms follow). **Not hit:** † arm lost / state 4 / act 3 fallback (arm HP 1290, `+0xb08` 1 throughout:
unreachable, as on 20F); state 7 / act 6 (no writer).

Hits taken: none on the applied path. The two "survive the pattern" searches (goal `gidx == 6`, then `gadd == 10`, score progress,
`--hold 60`, no attack actions) rejected 793 arrivals in the first (the goal state lost within the hold), applying the best path —
standing within ~3.6 of the body centre between attacks never got hit.

## The return and the day end

| frame | what |
|---|---|
| 47476 | **the return door (object 1, kind 6, base 0x695f218): state `+0x268` 3 → 0 at the floor open, hit counter `+0x264` = 1** |
| 47606..48704 | walking into it from every side (search: 17,232 cells, both sides, also after `p_inv` ran out at f48666) raised nothing |
| **48776** | **a sword swing on the door: `+0x264` 1 → 0, state 0 → 1** (`record_hit.c` kind-6 arm: a door must be HIT before `stage_portal_scan` sees it) |
| 48864 / 48885 / 48896 | `portal` 2 at (−45.42, −40.83) → `A` → "Return to town now?" (`portal` 23) → `A` (Yes) |
| 49017 | results bag (`scene` 9) "19/20" |
| 49184 | `B` → town map, **`stime` 1 → 2**, `wm_cur` 6, `wms` 1121211 (Town Square / Pub events waiting, not taken) |
| 49237..49393 | `D` → 3 → Merchants' Guild (`B` ×n leaves) → `D` → 0 → home: **`stime` 2 → 3** |
| 49483..49672 | shop door `R*12 . A` from (2, −3.24) → map → `R` → 3 → Merchants' Guild → `D` → 0 → home: **`stime` 3 → 4** |
| 49702..50149 | "Thanks for all your help today, Tear!" → Daily Profits (−4,350, score 7221 → 7582) → **day dword 28** (f≈50132) → shop morning f50149 (`stime` 0) |
| 50300 | `(~ .*20)` → pause (ESC ignored ≈ 150 ticks) |
| 50364 / 50466 / 50537 / 50597 | `D` ×4 → `pm_sel` 4 / `A` → slots (cursor on 020) / `A` → "Overwriting file. Are you sure?" / `A` → "NOW SAVING", SCORE 7582, TIME 4:25:16; `rd endsave` → `save:535305f4273b` |

## Story / events

- No `dlg` 1 on the whole trip. Guild open on day dword 27.
- Town map after the trip: `wms` 1121211 (events at the Town Square and the Pub) — not visited.

## Tool gaps

1. **The return door needs a hit** — not in DRIVER.md: earlier reports' "walk into it from side X" worked because an `A` came first
   (50F) or the search's default actions include `A .*7` (60F). A var for object 1's state (`s32(0x695f218 + 0x268)`, 1 = open) and a
   DRIVER line would save the next driver ~1,200 frames / 4 searches.
2. `rd text` still has no `--at-frame`: 61F / 67F banners missed.
3. A scored nav leg without `--hold` can end in a committed enemy attack (69F); the nav recipe should carry `--hold 30 --hold-no-goal`.
4. `rd ram R dwords` on the dungeon-object block (0x695ef68, 12 dwords) crashed the live session (`c0000005` read ffffffff at
   eip 00217514, f48704); the session rebuilt cleanly and the run was unchanged.
5. The kill-loop / nav-loop wrappers (Python around `rd search`) worked unattended for 57 + 33 searches; an `rd` "kill nearest of
   types T" / "walk the BFS route" macro would make them one command.
