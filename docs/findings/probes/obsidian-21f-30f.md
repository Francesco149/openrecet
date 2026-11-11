# Probe: obsidian-21f-30f

- **Run:** `griff-30f` · **start:** `save:671c5819784b` (end save of `golem-20f`: shop, day 17 morning, crafted lineage) · no task
  file (goal: Obsidian Tower 21F → 30F in one trip, the 25F checkpoint floor, Griff's arms on 30F, back to town, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`), `rd search -j 2`
- **Side run:** `griff-scan` (reads only: `--from griff-30f@F` at f36990 / f37030 / f37104, bat counts; stopped; `rd verify` MATCH at f37040)
- **Movie:** 39676 frames (one trip; no in-dungeon save point exists, see 25F). **Not filed.**
- **Verdict (2026-11-11):** `rd verify griff-30f` (headless):
  ```
  fresh headless replay: frame 39676 rng 04dbfdce sem 84821c7c0f5c2185 dev bbf044248673d76a  (unskipped retail)
  live session:          frame 39676 rng 04dbfdce sem 84821c7c0f5c2185 dev bbf044248673d76a  (skips 414ee2: dev not compared)
  MATCH
  ```
- **End save:** `save:b7eae73ba6be` (slot 020 overwritten from the town map's pause menu, day 18 `stime` 2, SCORE 5488;
  bank+0x2bce5 = 1 = Griff's 30F beaten).
- **Card:** `docs/findings/bosses/griff.md` (§2 stats corrected, §6 attacks, §9 arms marked, §10 retail facts).
- **Dialogues:** none on the load, Market, guild, 21F–29F, nor before the fight (0x2bce4 = 1). Night of day 17: "Thanks for all your
  help today, Tear!" (f2487). **After Griff: event 7/12** (f38171..38741), text in the card §10.

## Before the dungeon

- Load `.*60 A .*30 A .*120` → shop f137, no event. Door by search → town map f683: `wms` 1121010, **the Adventurers' Guild is closed
  on day 17** (`s32(0x9643644)` = 1, the 7th digit 0); Town Square shows an event (2) — entered once by mistake (scene 14, `dlg` 1,
  f1836) and undone, not part of the movie.
- **Market** (wm 1, f724): Food tab = 7 × `R`/`L` from Necklaces (`L` walks the tabs backwards). Bought **Beef Bowl ×4** (1,610 each,
  "4 Left" = all) f1082 and **Baked Yam ×3** (1,680 each, "Price Up") f1264; gold 454,200 → 442,720. Food list: Apple 252, Cherry (6),
  Watermelon (5), Walnut Bread 70, Beef Bowl (4), Candy, Taiyaki, Candy Apple (5), Baked Yam (5).
- **Baked Yam is an SP item** (+14 SP, f≈35400). Beef Bowl +55 / +58 / +56 HP; Chocolate Bar +21 SP; Candy +10 SP.
- Burning day 17: a period passes only when you come HOME after visiting a place (home → `stime` 0 → 1 f1410 after the Market; a bare
  map → home round trip changes nothing, f1589). Market + home → 2 (f1875); evening: Market closed (`wms` 1011200), Merchants' Guild +
  home → 3 (f2174); evening + home → 4 (f2487, night lines) → Daily Profits "Day 18" (profits −11,480 = the two purchases) → day 18
  morning with control f2856. The shop door at night / noon is `A` within reach facing +x (`R*12 . A`), not a walk-through.
- Day 18: map f3255 (`wms` 1121011), map cursor from Home: `D D D R U` → 6 (moves: 0 −D→ 5 −D→ 2 −D→ 0 −R→ 3 −U→ 6), guild f3337.
- **Hired Louie** (L40: ATK 56, DEF 58, MDEF 32, HP 197, SP 45). Against Griff's retail DF 30 his sword does `ftol(28 − 7.5)` = 20
  (+0..4), the spin 40 — enough (1120 HP). Not Caillou (HP 80 against hits of 43–66).
- Pack screen: bag has ≥ 9 slots (a second page `6:`…); `A` on a slot opens the category list after ~25 ticks, `L` there → Food.
  Packed Beef Bowl ×4, Baked Yam ×3, Chocolate Bar, Candy. `U`×10 → Depart!, dungeon cursor already on Obsidian, `A` → floor rows
  (cursor remembered 3 = "16~20"; rows listed to **"31~35"**), `D` → 4 = "21~25", `A` departs f4506: fee 3,950, `stime` 0 → 1.
  Equip prompt: `rd tap A --gap 10 --until "lock == -1"` → control f4731.

## Per floor

Enemy lists = the actor table after entry (`type × count (HP)`). Route = BFS of the maze grid + one scored search per cell
(`-j 2`, 60 s): 49 legs, all found first try. Levels: `floor/4 + 16`.

| floor (`floor`) | frames | banner | enemies | grid / route | HP |
|---|---|---|---|---|---|
| 21F (20) | 4731 → 9418 | none | 1, 3, 8, 9, 18, 19, 35, 46, 47, 52, 53 (L21: 104 / 130 / 94 / 104 / 78 / 78 / 78 / 94 / 182 / 104 / 78); kills below | 9 × 9, (5, 3) → (2, 3) + detours | 197 flat |
| 22F (21) | 9576 → 11377 | "Adventurer attack power doubled!" — `ticker` counted UP (201 at f9777) until an `A` (f9798) | 59: 1×5, 3×5, 8×4, 9×6, 18×9, 19×4, 35×7, 46×4, 47×3, 52×5, 53×7 (L21) | 11 × 9, (7, 5) → (5, 2), 5 legs | 197 |
| 23F (22) | 11377 → 12981 | none | 63: 1×7, 3×7, 8×4, 9×5, 18×6, 19×7, 35×7, 46×5, 47×7, 52×4, 53×4 | 9 × 9, (3, 2) → (2, 6), 5 legs | 197 |
| 24F (23) | 12981 → 14039 | none | 50: 1×6, 3×5, 8×7, 9×5, 18×5, 19×4, 35×3, 46×4, 47×5, 52×3, 53×3 | 9 × 9, (4, 1) → (6, 2), 3 legs | 197 |
| **25F (24)** | 14039 → 21760 | none | **30: 0×6 (81), 7×5 (92), 11×4 (27), 17×5 (81), 39×4 (81), 47×6 (189)** (L22) | 5 × 7, below | 197 → 186 |
| 26F (25) | 21760 → 22752 | none | 50: 1×5, 3×5, 8×3, 9×5, 18×5, 19×4, 35×3, 46×3, 47×5, 52×7, 53×5 (L22) | 9 × 9, (5, 4) → (6, 6), 3 legs | 186 |
| 27F (26) | 22752 → 27462 | "Defense power doubled for everyone!" | 52: 1×5, 3×4, 8×6, 9×4, 18×4, 19×5, 35×4, 46×5, 47×6, 52×5, 53×4 | (1, 3) → (1, 5) the long way, 14 legs | 186 |
| 28F (27) | 27462 → 29395 | "Consumables now twice as effective!" | 55: 1×4, 3×7, 8×4, 9×4, 18×4, 19×4, 35×10, 46×4, 47×3, 52×4, 53×7 | (1, 1) → (5, 2), 5 legs | 186 |
| 29F (28) | 29395 → 31614 | none | 70: 1×4, 3×6, 8×9, 9×7, 18×5, 19×7, 35×10, 46×4, 47×5, 52×6, 53×7 (L23) | 9 × 11, (3, 4) → (6, 8), 7 legs | 186 |
| 30F (29) | 31614 → 39284 (results) | none | Griff (type 28, L23, HP 1120); swarm bats type 6 | boss floor | 186 → 115 |

Floor exit: the "Go to the next floor?" box fades in (alpha 0x3f); an `A` in its first ~10 ticks is dropped — `rd tap A --gap 10
--until "floor == N"` works. 22F's banner `ticker` counts up and play stays frozen until `A`.

## New enemy types (not in `obsidian-1f` … `16f-20f`)

Stats at `0x5c23f0 + type*0x68` (HP, EXP, AT, DF, MA, MD; rf[0..3]); one of each killed by search, no damage taken.

| type | name (ROSTER family) | HP | EXP | AT | DF | MA | MD | rf | kill |
|---|---|---|---|---|---|---|---|---|---|
| 3 | slime | 25 | 12 | 22 | 4 | 0 | 30 | 0.8, 1.5, 2, 2 | 21F idx 10, f7661 (130 HP) |
| 9 | bat (ahriman) | 20 | 22 | 20 | 14 | 24 | 25 | 0.8, 1.5, 4, 2 | 21F idx 19, f5373 (5 swings, 24 each) |
| 19 | roper | 15 | 20 | 20 | 4 | 0 | 0 | 0.8, 1.5, 2, 2 | 21F idx 8, f8778 |
| 47 | knight | 35 | 25 | 24 | 27 | 0 | 0 | 0.9, 1.5, 2, 2 | 21F idx 15, f6851 (1,478 frames) |
| 11 | bee | 5 | 12 | 19 | 4 | 0 | 0 | 0.5, 1.5, 4, 4 | 25F idx 10 / 11, f14351 / f14356 (a spin) |
| 39 | bomb (green) | 15 | 20 | 16 | 20 | 22 | 10 | 0.9, 1.5, 2, 2 | 25F idx 17, f15763 (spin; no damage) |

Attacks of 3 / 9 / 19 / 47 / 11: not seen (no hit taken). Type 0 / 7 / 17 seen on 1F–15F already.

## 25F — the checkpoint gauntlet

- `gate` 3 (exit hidden), `estage` 0 at entry; all 30 enemies awake at entry (state timers running, no sleep), grouped by type
  along the 5 × 7 maze: bats (1, 1)–(1, 2), ropers (1, 2), bees (2, 3), bombs (3, 3), knights (2, 4), slimes (2, 5). They converge on
  the player.
- Cleared by repeated "kill the nearest" searches (`--avoid "hp < H"`, goal = the slot freed `+0x428 == 0`). **All 30 gone →
  `estage` 1 at f20627**, then the player is frozen ~140 ticks (`p_act` 2 held at `p_actt` 11 until f20753; `lock` −1), the exit
  portal object (−116.5, −120) rises from y −5 to 0 (cell (3, 3); the grid's own exit cell (1, 1) has nothing).
- Only **"Go to the next floor?"** — no return door: object list `0x695f210 + k*0x2a0` = (−123.5, −120), (−200, −40), (−200, −80),
  (−40, −160); `A` facing −x at (−121.5, −119.7) does nothing; cell (1, 1) gives no prompt. So the trip went on to 26F.
- **Bomb (type 39):** a kill leaves a lit corpse (`+0x428` 2, `+0xaf0` counting; type 39's fuse starts at 360, blows at 540 —
  corpse idx 14: 405 at f15813 → gone ≈f15946, ~10 away, no damage). Knocked into another actor it blows at once: on an undone
  branch a sword kill at 2.6 away blew 10 ticks later, **186 → 103 (83)** at f15864 (the record was not in table B on any tick read).
  Killing it with the spin (`p_inv` 30) avoids that (f15763). Bombs 15 / 16 vanished during the knight fights (not traced).
- Damage taken on the kept line: 197 → 186 (−11, the roper group, f≈14420). On an undone branch the knights took 103 → 20.
- SP: spins 45 → 0 over the floor.

## 30F timeline (Griff; names in the card §9)

| frame | what |
|---|---|
| 31614 | on 30F: Griff (−40, −35) state 8, HP 1120, L23; party (−40, −48); `est` 0, `gate` 3 |
| 31704 | `lock` 4 (scripted walk, `etk` 60..99) → party z −40.28 |
| 31885 | `est` 3 (`etk` 270), `lock` −1, no dialogue |
| 31886..32223 | pause: Chocolate Bar (SP 0 → 21), Candy (→ 31) |
| 32248 / **32260** | slash: state 1 (player 3.8 below him on his axis); 3 records 22 (reach 1.5); **186 → 141 (45)**, `p_hurt` 30 |
| 32321 | first hit, fresh (`ghc` 280 → 0): 1120 → 1097 |
| 32627 | second hit inside 60 (`ghc` 32): `gstag` 40, `gctr` 1; HP 996 |
| 32672 / 32692 / 32757 | counter t 1 (`gguard` 60) / counter over / guard over (`gunt` 0) — burst missed (player ~3 away after the knock-back) |
| **32703** | fan rings (5 × record 152, reach 0.7, age 120) erupt on the player: **141 → 75 (66)** |
| 32708..32950 | pause: Beef Bowl ×2 → 183 |
| 32964 → 34847 | 6 searches of 60 HP: 996 → 580, no damage |
| 35001 | **dash** state 2 (`gdash` 185 → 0); `gunt` 1 to t 84 |
| **35051** | dash t 50: **183 → 139 (44)**; Griff slides 11.6 units −z |
| 35091 / **35101** | state 0 (recovery) / sword in it: 580 → 553 |
| 35149 | second hit (`ghc` 44): 528, stagger |
| 35193 / 35196 / **35198** | counter t 1 / record 14 at Griff (reach 2.0) / **139 → 96 (43)**, `p_hurt` 70 |
| 35400..35487 | pause: Baked Yam (SP 31 → 45), Beef Bowl → 154 |
| 35661 → ≈35700 | second fan eruption while Griff was staggered: **154 → 93 (61)** (a search with `--avoid "hp < 154"` found 6 cells for 300 s: no escape left) |
| 35883 | Beef Bowl → 149 |
| 36491 | 340 → 317: phase 1 |
| **36820** | swarm state 5, side 2; bats from f≈36892, contact hit 149 → 133 (16) f≈36928; state 0 f36955 |
| 36999 | first bats freed at age 100 (47 of 48 gone by f37104) |
| 37059 → 37318 | 317 → 123 (sword + spins; spin + sword inside 60 → counters) |
| **37613** | detonate state 4 (`gpc` 660), 30 ticks, one record 151 |
| **37798** | Griff HP 0 (`guse` 2), `est` 4, bank+0x2bce5 = 1 |
| 38140 / 38171..38741 | stage 5 (party (−40, −45)) / event 7/12 dialogue |
| 38771 / 38801 | `est` 7 / Griff freed, `gate` 0 |
| 39132 | "Return to town now?" (`portal` 22) at (−44.29, −39.48) |
| 39284 / 39361 | results (bag 3/20: Baked Yam ×2, Gunpowder) / town map, `stime` 1 → 2, `wms` 1121211 |
| 39656 / 39676 | Save slot 020 / `rd endsave` → `save:b7eae73ba6be` |

## Tool gaps

1. A movie segment turned into `{mask}*n` tokens (+ `~` for its `esc` lines) and replayed in a side run diverged within ~100 frames
   (the first 92 frames matched); `--from RUN@F` is exact but one session per frame. A `rd scan R --from F --to T --watch …` (replay
   the run's own movie with per-tick rows) would have given every Griff state change without the binary search.
2. `rd ram R dwords <decimal>` (an unconverted shell number) crashed the live session (`c0000005`, f20627); the next command replayed.
3. A long `--expr` define (49 summed `and` terms) evaluated to 0 where the sum of `==` terms worked.
4. After `rd search` re-anchors, `rd eval --at CK` refuses every older checkpoint ("no savestate in this session").
5. Damage searches end on the goal frame: a fan cast during the search erupted 76 ticks after it (f32703); a later search then found
   no escape at all (6 cells, 2 × 150 s). Goal tails (hp re-checked N ticks on) or "no record 152 aimed at the player" would help.
6. No reader for pause-menu list rows (which item is under the cursor) besides the description box text.
