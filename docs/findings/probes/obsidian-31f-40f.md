# Probe: obsidian-31f-40f

- **Run:** `obs-40f` · **start:** `save:b7eae73ba6be` (end save of `griff-30f`: town map, day 18 `stime` 2, crafted lineage) · no task
  file (goal: Obsidian Tower 31F → 40F in one trip, the 35F checkpoint floor, the 40F Ohm pair, back to town, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`), `rd search -j 2` (legs), `-j 3` (35F kills), `-j 4` (40F)
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`; `rd ls` → `win`).
- **Side run:** `obs-40f-scan` (reads only: `--from obs-40f@F`, idle ticks; stopped)
- **Movie:** 44286 frames (one trip). **Not filed.**
- **Verdict (2026-11-11):** `rd verify obs-40f` (headless, Windows):
  ```
  fresh headless replay: frame 44286 rng e9beba84 sem c1dec14e705a8b44 dev 5f4945d532b58c61  (unskipped retail)  [win]
  live session:          frame 44286 rng e9beba84 sem c1dec14e705a8b44 dev 5f4945d532b58c61  (skips 414ee2: dev not compared)  [win]
  MATCH
  ```
  Session rebuild = 44,286 frames replayed in 23.7 s (≈1,870 frames/s); leg searches 1,400–2,700 ticks/s per session.
- **End save:** `save:8d209cc3acd7` (slot 020 overwritten from the town map's pause menu, day 19 `stime` 2, SCORE 5488 → TIME 2:58:14).
- **Card:** `docs/findings/bosses/ohm.md` §7 "40F pair" (arms marked, stats corrected).
- **Dialogues:** none on the load, Market, guild, 31F–40F, nor after the pair (the return tap transcript f43735..43934 shows only the
  prompt and the results bag). Night of day 18: "Thanks for all your help today, Tear!" (f1552); day-19 news lines f2173.

## Before the dungeon

- Load `.*60 A .*30 A .*120` → town map f137, `wms` 1121211 (guild open at `stime` 2). Map cursor from Home: `U` → 2, `L` → 1 (Market),
  `R` → 3; from Market (1): `U` 4, `D` 0, `L` 3, `R` 2; from 3: `D` 0, `R` 1, `U` 6.
- **Market** (f240): **Beef Bowl Out Of Stock** (bought out the day before). Food recovery read from the item table
  (`0x95d37d0 + i*0x2cc`: +0x08 HP, +0x0c SP, name +0x4a; food = records 468..504, ids 4000..4036): Apple 15, Orange 25, Cherry 30,
  Grapes 40, **Watermelon 60**, Melon 250, Durian 999/−500 SP, Walnut Bread 10, Egg Toast 20, Nut Mix 30, Apple Pie SP 300, Beef Bowl 50,
  Roast Chicken 80, Ham Sandwich 120, Curry 200, … Candy SP 10, Chocolate Bar SP 20, Taiyaki SP 30, **Candy Apple SP 40**, **Baked Yam SP
  50**, Honey SP 60. Bought Watermelon ×5 (1,540, all), Cherry ×6 (630, all), Candy Apple ×2 (1,120): 438,770 → 425,050. In the buy box
  `U` raises the count (1 → 5) before `A`.
- **After the Market the guild closed** (`wms` 1121211 → 1121210 at f914, `stime` still 2). Burned day 18: home → `stime` 3 (f987);
  Merchants' Guild (f1293) + home → 4 (f1509), night line, Daily Profits "Day 19" (−17,670), day 19 morning f1966. The shop gave control
  only after the news ticker (f2173..2194); a search found the door then (f2441).
- Day 19: map `wms` 1121011, `R U` → 6, guild f2520. **Hired Louie** (L40, cursor default; ATK 56 DEF 58). Pack: bag 20 slots over
  pages of 5; `A` on a slot, then (category list focused) `L` → Food; the list cursor stays on the item while it lasts. Packed Watermelon ×5,
  Cherry ×6, Candy Apple ×2, Baked Yam ×2 (15/20). Depart → Obsidian → rows, cursor remembered 4 ("21~25"), `D D` → **"31~35"**, departs
  f4137 (fee 3,950, `stime` 0 → 1). "Equip items in bag?" box f4367 → `A` → control f4378.

## Per floor

Enemy lists = the actor table after entry (`type × count (HP)`). Route = BFS of the maze grid + one scored search per cell (`-j 2`, 60 s):
**49 legs, all found first try, no damage on any leg**. Levels `floor/4 + 16` (L23 on 31F–32F, L24 33F–36F, L25 37F–40F).

| floor (`floor`) | frames | banner | enemies | grid / route | HP |
|---|---|---|---|---|---|
| 31F (30) | 4137 → 7362 | none | 68: 4×7 (196), 8×6 (101), 9×5 (112), 18×8 (84), 19×4 (84), 35×4 (84), 45×5 (78), 46×4 (101), 47×7 (196), **50×6 (84)**, 52×5 (112), 53×7 (84) | 11 × 9, (6, 1) → (8, 6), 7 legs | 197 → 202 (level 41, f≈5200) |
| 32F (31) | 7362 → 10031 | "Adventurer attack power doubled!" (`ticker` 45 at f7407; ended by itself, control f7708) | 61: 4×4, 8×3, 9×4, 18×4, 19×4, 35×7, 45×4, 46×7, 47×8, 50×4, 52×6, 53×6 | 11 × 9, (5, 5) → (8, 4), 4 legs | 202 |
| 33F (32) | 10031 → 12372 | none | 74: 4×6 (203), 8×4, 9×7, 18×6, 19×7, 35×4, 45×6, 46×6, 47×6, 50×7 (87), 52×5, 53×10 (L24) | 9 × 11, (1, 4) → (7, 5), 7 legs | 202 |
| 34F (33) | 12372 → 14328 | "Attack power doubled for everyone!" (`ticker` 45 at f12417; control f12724) | 94: 4×5, 8×8, 9×9, 18×6, 19×6, 35×6, 45×11, 46×12, 47×9, 50×6, 52×9, 53×7 | (1, 5) → (5, 4), 5 legs | 202 |
| **35F (34)** | 14328 → 30547 | none | **45 slimes: 0×4 (87), 1×9 (116), 2×11 (87), 3×5 (145), 4×12 (203), 5×4 (58)** | 5 × 7, below | 202 |
| 36F (35) | 30547 → 30966 | none | 74: 4×10 (203), 8×5, 9×5, 18×5, 19×8, 35×7, 45×5, 46×5, 47×7, 50×8, 52×4, 53×5 | 11 × 11, (1, 3) → (2, 3), 1 leg | 202 |
| 37F (36) | 30966 → 34517 | "Defense power doubled for everyone!" | 72: 4×4 (210), 8×4, 9×7, 18×6, 19×7, 35×8, 45×8, 46×8, 47×5, 50×5, 52×4, 53×6 (L25) | 9 × 11, (1, 3) → (6, 6), 10 legs | 202 |
| 38F (37) | 34517 → 36554 | "Attack power doubled for everyone!" | 39: 4×3, 8×2, 9×3, 18×2, 19×5, 35×4, 45×3, 46×3, 47×4, 50×5, 52×2, 53×3 | 9 × 9, (5, 3) → (1, 2), 5 legs | 202 |
| 39F (38) | 36554 → 40261 | "Attack power doubled for everyone!" | 74: 4×6, 8×8, 9×7, 18×10, 19×4, 35×6, 45×6, 46×7, 47×5, 50×4, 52×5, 53×6 | 9 × 11, (2, 1) → (2, 9), 10 legs | 202 |
| 40F (39) | 40261 → 43735 (return prompt) | none | Ohm ×2 (type 67, L25, HP 900 each); bees type 10 (HP 30) | boss floor, below | 202 → 187 |

Floor change = `portal > 0` by a scored search onto `(ex, ez)`, then `rd tap A --gap 10 --until "floor == N"`: ~38 ticks.

## New enemy types (not in `obsidian-1f` … `21f-30f`)

Stats at `0x5c23f0 + type*0x68` (HP, EXP, AT, DF, MA, MD; rf[0..3]); kills by search, no damage taken.

| type | name (ROSTER family) | HP | EXP | AT | DF | MA | MD | rf | kill |
|---|---|---|---|---|---|---|---|---|---|
| 50 | skeleton (スケルトン, port **stub**) | 15 | 10 | 20 | 14 | 0 | 15 | 0.9, 1.5, 2, 2 | 31F idx 5: killed **three times** f4698 / f4915 / f5171, freed f≈5271 |
| 4 | slime | 35 | 15 | 20 | 10 | 0 | 40 | 0.8, 1.5, 2, 2 | 32F idx 46, f8335 (627 frames, sword only) |
| 5 | slime (metal: `+0xb54`, min damage 0) | 10 | 50 | 20 | **50** | 0 | 80 | 0.8, 1.5, 2, 2 | 35F idx 21, f17303; all four by f19120 |
| 10 | bee (Ohm's) | 5 | 10 | 17 | 4 | 0 | 0 | 0.4, 1.5, 4, 4 | 40F, by spins (several) |

- **Skeleton (type 50) revives.** Death arm default case (`FUN_0042f7ad`, IDA all_ida.c ≈31305): while `+0xadc` (revive count) ≠ 0, death
  tick 1 anim 4, tick 60 anim 5, **tick 90: `+0xadc` −1, `+0x428` back to 1** at full HP. Observed idx 5: dying f4698 (`+0xadc` 2 → 1,
  `+0xaf0` 6 at f4708 … 90 at f4798, `+0x428` 1, HP 84), killed again f4915 (→ 0 at revive f≈5005), third kill f5171 → freed (`+0x428` 0
  at f5271). At entry `+0xadc` read 2 on idx 4, 1 after a revive, 0 with `+0xb8c` 1 on idx 6 (not yet initialised: the AI sets
  `+0xadc = 2` from `+0xb8c`). AI `FUN_00428213`: state 0 = drift toward a random point 8 around the player, then state 1 = lunge
  (accel 0.04 × 40 ticks along the yaw, **record type 12 at tick 13**, 90 ticks); state 1 seen on idx 5 f4498. No skeleton hit taken.
- Type 4 / 5 attacks: not seen (no hit taken). Type 45 (knight; HP 14, AT 19, DF 20) is on every floor 31F–39F and is still never killed.

## 35F — the slime checkpoint

- `gate` **0** at entry (not 3 as on 25F) but no exit object: object 0 (`ex ez`) is kind 1 (a drop), the portals are hidden objects
  46 / 47 (`+0x268` 3). `estage` 0, `etick` running; all 45 slimes awake at entry (state timers running), grouped by type along the
  5 × 7 maze: type 4 by the start, 1, 2, 0 / 5 / 3 further in, a second 4 / 1 / 2 group to the south.
- Cleared by 45 "kill the nearest" searches (`-j 3`, 60 s each, `--avoid "hp < H or sp < 20"`, goal `+0x428 != 1`): f14374 → f28928,
  **no damage taken**. SP 46 → 21 (spins only while SP ≥ 25).
- **All gone → `estage` 1 at f28934**; the player is held in `p_act` 2 to ≈f29070 (as on 25F). Objects 46 / 47 go `+0x268` 3 → 0 and
  rise **at the start cell**: **kind 8 "Go to the next floor?" at (−36.5, −40)** and **kind 6 "Return to town now?" at (−43.5, −40)**
  (prompt read f30579 on an undone branch) — **35F has a return portal; 25F had none**. The grid's exit cell (1, 3) and 25F's portal
  spot (−116.5, −120) give nothing. The trip went on (f30509 → 36F).

## 40F — the Ohm pair (card: `docs/findings/bosses/ohm.md` §7)

Names (this run's `vars.json`): `oNst oNt oNbt oNbn oNuse` = s32 `B+0xa54 / a58 / a64 / a68 / 428`, `oNx oNz oNyaw oNhp` = f32
`B+0x3f0 / 3f8 / 420 / 42c`, `B = 0x76b970 + N*0xba4`; `bees` = the live type-10 count in slots 2..21. States along the fight were
sampled from side runs every 59 frames (f40561..43334).

| frame | what |
|---|---|
| 40261 | on 40F: `gate` 3; Ohm 0 at (−45, −34), Ohm 1 at (−36.0, −34) (`o1x` −36.0021 when control came), both state 8, **HP 900, L25** (`+0xae0` 25) |
| 40502 | `etick` 241, `lock` −1, both state 0 t 1 (intro 240), `bees` 0, both `bt` counting from 1 |
| 40515 / 40532 | Ohm 0 → state 2 (charge, t 1); Ohm 1 → state 2 t 1 (both facing the player at (−40, −45)) |
| **40597** | **Ohm 0's charge hits: 202 → 163 (39)**, Ohm t 83, `p_hurt` 70, `p_inv` 30. Disc: point 2 ahead at f40597 = (−43.54, −40.93), player (−40, −45): **5.39 < 5.5** → hit; at f40596 the point was 5.66 away → no hit |
| 40625 | **both** `bt` reach 120 on the same tick → 2 bees (`bn` 1 / 1) at points 2..10 from (−40, −40) |
| 40745 | 2 more (4 alive, `bn` 2 / 2) |
| 40813 | (side run, idle) bee sting: record type **17**, side 1, reach 0, 1.9 from the player: 163 → 146 (17) |
| **40865** | (side run, idle) `bees` 4 < 5 → Ohm 0 spawns the 5th (`bn` 3); **Ohm 1's `bt` freezes at 119** while 5 live (f40877..41157): **the cap 5 is shared** (one count over the whole table, `FUN_00422ed6`) |
| 41033 / 41092 | Ohm 0 in state 1 (walk) t 7 / Ohm 1 state 2 |
| **41076** | **Ohm 1's charge hits: 163 → 123 (40)** at its t 70 (the record's first tick); point 2 ahead (−44.90, −49.10), player (−47.72, −51.23): 3.54 |
| 41080..41407 | pause: Watermelon +68 (→ 191), Candy Apple (SP 16 → 46) |
| 41564..41918 | damage searches: spins hit **both** Ohms at once (848 → 801 and 726 → 681 in one search); `bn` 5 / 5 → 6 / 6 |
| 41703 | Ohm 0's charge (t ≈ 83): 191 → 151 (40) — the search's goal frame left no escape (one cell for 145 s ×2) |
| 41965 | Ohm 0's charge: 151 → 109 (42) |
| **42213** | **Ohm 0 burst** (state 3 t 16); records 33 at t 20 / 24: 8 each, reach 0.10, power 1, spread 6–10 behind / beside it; none reached the player (z −36 vs −42..−46) |
| 42227 | bee sting (record 17 at (−51.6, −34.3), age 5): 109 → 92 (17) |
| 42286 | **Ohm 1's charge: 92 → 50 (42)** |
| 42306..42710 | pause: Watermelon ×2 (→ 119 → 187), Baked Yam (SP 6 → 46) |
| 42803 / 42862 | Ohm 0 state 1 (walk) t 53 / 108; `bn` 8 / 8 |
| **42875** | **Ohm 1 dies** (HP 0, `+0x428` 2): death tick 1 → slow-mo 60 (`0x438becc` 55 at f42885), **`p_inv` 1200** (1199 at f42885) |
| 42978 (tick 60) | **no sweep**: `bees` stays 2 (Ohm 0 alive) |
| 43025..43045 | alpha 255 → 199 → 119 → 39 (−8 / tick from tick 100) |
| 43050 | Ohm 1 freed (`+0x428` 0, death timer 131) |
| 43105..43338 | Ohm 0: 153 → 0 under `p_inv` (search with `--avoid "bees < 2"` to keep the bees alive); Ohm 0 `bt` −136 at f42885 (`bn` > 2·cap → gap 520), `bn` 9 at f43216 |
| **43338** | **Ohm 0 dies**: `p_inv` 1200 again, slow-mo 60; 2 bees alive (idx 16 / 17, HP 30) |
| **43440** (tick 60) | **sweep**: no other live 67 → `bees` 2 → 0 (all live actors set dying) |
| 43538 / **43539** | Ohm 0 freed / **`estage` 1**, `gate` 0, `0x9643128` 0; objects 0 / 1 = kind 8 (−35, −40) next floor, kind 6 (−45, −40) return |
| 43735 | "Return to town now?" (`portal` 22) at (−45.94, −39.79) → `A` |
| 43917 / 43934 | results (bag 11/20) / town map, `stime` 1 → 2 |
| 44077..44286 | pause → Save (`pm_sel` 4, `pm_type` 3) → slot 020 → "Overwriting file. Are you sure?" → Yes (TIME 2:58:14); `rd endsave` → `save:8d209cc3acd7` |

Damage taken on 40F: 5 charge hits (39 / 40 / 40 / 42 / 42 = `ftol(30·3.5/2 − 59/4)` = 37, +0..6) and 2 bee stings (17 = 15 +0..2);
food: Watermelon ×3 (+68 / +69 / +68), Candy Apple, Baked Yam. Bag left: Watermelon ×2, Cherry ×6, Candy Apple, Baked Yam.

## Tool gaps

1. No reader of the run's own movie with per-tick rows: state transitions of the 40F fight were sampled by 48 side runs
   (`rd new obs-40f-scan --from obs-40f@F`, ~20 s each, every 59 frames). A `rd scan R --from F --to T --watch …` would be one command.
2. Damage searches applied on their goal frame left states where a charge record was already unavoidable (4 times on 40F: a search from
   there finds one cell for 90 s). Handled by playing 20 idle ticks; a goal tail ("hp unchanged N ticks later") would avoid it.
3. `rd ram R define … --expr "sum(...)"`: no `sum` / comprehension; the bee count needed 20 product terms.
4. `rd eval` with ~600 expressions errors ("undefined program"); batches of 40 work.
5. `--apply-best` alone does not apply a found path (`--apply` needed as well).
6. Waiting in `rd tap A` for `lock == -1` at a dungeon entry stops on the first tick (lock is −1 for one tick before the equip box).
