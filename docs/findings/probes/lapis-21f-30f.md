# Probe: lapis-21f-30f

- **Run:** `lapis-30f` · **start:** `save:303f7bcf95ea` (end save of `lapis-20f`: town map, day dword 21 = HUD Day 22, `stime` 3) · no task
  file (goal: Lapis Ruins 21F → 30F in one trip, the 25F crown-slime trio, the 30F bomb floor, back, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`); legs `rd search -j 2 --hold 30`; fights `-j 3`, every fight search `--hold 30`
- **Host:** every session on the Windows box (`win`, `RHOST_WIN=win-wifi`); verify on `win`.
- **Movie:** 32,205 frames (night of day 22 → day 23 morning, Market, guild, one trip 21F → 30F, return door → town map, Save). **Not filed.**
- **Verdict (2026-11-12):** `rd verify lapis-30f` (headless, win):
  ```
  fresh headless replay: frame 32205 rng 36779b64 sem d6a342bf78a344bd dev d15c299f5d34874a  (unskipped retail)  [win]
  live session:          frame 32205 rng 36779b64 sem d6a342bf78a344bd dev d15c299f5d34874a  (skips 414ee2: dev not compared)  [win]
  MATCH
  ```
- **End save:** `save:3c2a6b9c8b1d` (slot 020 overwritten from the town map's pause menu at f32188: day dword 22 = HUD Day 23, `stime` 3,
  SCORE 6104, TIME 3:28:39 → 3:37:32).
- **Card:** `docs/findings/bosses/crown-slime.md` § "Lapis 25F trio".
- **No story exit / no 30F town scene on this lineage:** `u8(bank + 0x2bcf5)` = 1 at the start (the crafted lineage already has it; see
  `lapis-1f-10f`, "0x2bcf5 = 1"). `FUN_00450877`'s floor-29 branch therefore took `*flag != 0` → event stage 2 (portals) — the fade /
  teardown / `DAT_0438b1c0 = 1` arm (`PORT-DEBT(stub, FUN_00450877)`) is **not reached** by this movie. Reaching it needs a save with
  bank+0x2bcf5 = 0 (crafted: `tools/craft_pix_save.py --set`) and a 30F bomb clear.

## Before the dungeon

- Load `.*60 A .*30 A .*120` → **town map** f137 (not the shop): `wms` 1011200 (evening: Market 0, Adventurers' Guild 0, Pub 2, Chapel 0),
  gold 392,300. The guild is closed in the evening → the day had to end first.
- Home at `stime` 3 with no visit since the return does NOT end the day (shop, `stime` 3; f261). Merchants' Guild (`wm_cur` 3 via `R` from Home)
  → its Buy list has **no Food category** (17 categories, Swords … Charms; Swords: Worn Sword 112, Longsword 672, Steel Sword 1,680 "1 Left").
  Leave → Home (`D` from 3 = 0; `L` from 3 = 2 = Town Square, entered once by mistake — "It looks like nobody's here…" — undone) →
  "You look tired; get some rest." → Daily Profits Day 22 → **weekly payment** ("So, this is the payment for this week. Let me see…",
  "Congratulations. Well done.", "I have officially received your payment for this week.", "Let us continue to work hard next week, as
  well!", "YAY! I did it!", "I'm counting on you next week too, Tear!"; gold 392,300 → 312,300) → Day 23 morning, control f2551.
- Shop: the pad did nothing for ~350 ticks after control (f2551..≈2900, no text); then a search found the door (f3114). Map `wms` 1121011
  (Merchants' Guild 2 = an event waits; not entered).
- **Market Food** (day 23): Apple 210, Walnut Bread, Candy, Taiyaki 770 on sale; **Cherry, Watermelon, Beef Bowl, Candy Apple, Baked Yam
  Out Of Stock**. Bought Apple ×12 (2,520) and Taiyaki ×2 (1,540): 312,300 → 308,240. No HP food better than Apple (15) existed.
- Guild (`D R U` from Market → `wm_cur` 6): Louie **L42** (Longsword, Scrap Plate, Wooden Shield, Old Muffler), hired with one `A`.
  Pack: Iron Armor, Spiked Armband, Apple ×13, Taiyaki ×2, Candy Apple ×2, Baked Yam (20/20).
  Pack-screen facts: a `R`/`L` category change while the category list has focus leaves the item cursor on row 0 ("Nothing") — `D` first;
  `A` on "Nothing" picks nothing. After a pick the item cursor keeps its row index (picking the same row repeatedly works).
- Depart → rows: cursor remembered on **"Floor 11～15"** (picture `view/f006313.png`); `D D` → "21～25"; depart f6340, fee 4,150
  (308,240 → 304,090), `stime` 0 → 1. Equip box → `A`: ATK 61, DEF 68, MATK 22, MDEF 42, HP 206 / 206, SP 46. Control f6579.

## Per floor

Level `floor/5 + 25`: L29 (21F–25F), L30 (26F–30F). Enemy lists = types in the actor table at the floor's start (`type × count (HP)`).
Route = BFS of the maze grid + one scored search per 40-unit cell (`-j 2`, `--hold 30`, 60 s): **every leg found on the first try, no
damage taken on any floor (HP 206 → 206, 210 after the level-up)**.

| floor (`floor`) | frames | banner | enemies | grid, start → exit, legs |
|---|---|---|---|---|
| 21F (20) | 6579 → 10201 | "Will-o'-Wisps will appear soon. Enjoy!" — `ticker` counts UP (60 at f6639) and play is frozen until an `A`; **no type 34 appeared** (3,600 ticks) | 0×3 (102), 1, 2, 4×3 (238), 6×4, 8 (122), 10 (34), 11×2 (34), 13×4 (68), 15×3, 16×3 (136), 17×4, 18×3, 22×2, 35, 37, 44×2 (82), 50, 52×4 (136); 44 | 9 × 9, (1, 5) → (7, 2), 9 legs |
| 22F (21) | 10201 → 12397 | "Adventurer movement speed increased!" | 0×2, 1×3, 2×5, 4, 6×2, 8×3, 10×2, 11, 13×4, 15×6, 16×4, 17×2, 18×2, 22×2, 35×2, 37, 44×3, 50, 52×2; 48 | 9 × 9, (5, 5) → (1, 4), 7 legs |
| 23F (22) | 12397 → 15452 | "EXP gain has doubled!" | 0×2, 1×2, 2×2, 4×5, 6×2, 8×4, 10×2, 11×3, 13×6, 15×6, 16×10, 17×4, 18×4, 22×2, 35×2, 37×3, 44×2, 50×4, 52×4; 69 | 9 × 11, (3, 4) → (7, 8), 8 legs |
| 24F (23) | 15452 → 18385 | none | 0×4, 1×2, 2×2, 4×2, 6×4, 8×2, 10×3, 11×3, 13×3, 15×3, 16×6, 17×3, 18, 22×3, 35×2, 37×3, 44×2, 50, 52×3; 52 | 9 × 9, (3, 3) → (3, 7), 8 legs |
| **25F (24)** | 18385 → 24039 | none | **23 ×3 (680 each, L29)** + blue slimes type 2 (102) | boss floor — `crown-slime.md` § Lapis 25F trio |
| 26F (25) | 24039 → ≈26053 | "EXP gain has doubled!" | 0×3 (105), 1×2 (140), 2×4, 4×2 (245), 6×2, 8×2 (126), 10×4 (35), 11×2, 13×8 (70), 15×7, 16×6 (140), 17×4, 18×2, 22×2, 35×2, 37×2, 44×3 (84), 50×3, 52×2; 62 | 11 × 9, (7, 3) → (5, 6), 5 legs; **object 0 is NOT the exit here** (it sat at (−28.9, −60.4)): the portal is the exit cell's centre (−200, −240) |
| 27F (26) | ≈26053 → 26741 | "Movement speed increased for everyone!" | 0×3, 1×2, 2×3, 4×2, 6×3, 8×3, 10×2, 11×2, 13×6, 15×5, 16×6, 17, 18×2, 22, 35, 37×2, 44×2, 50×2, 52×4; 52 | 9 × 9, (4, 5) → (6, 5), 2 legs |
| 28F (27) | 26741 → 28132 | "Will-o'-Wisps will appear soon. Enjoy!" — again no type 34 | 0×2, 1, 2×2, 4×4, 6, 8, 10, 11×4, 13×4, 15×2, 16×6, 17×6, 18×2, 22×2, 35×3, 37×3, 44×2, 50×2, 52×4; 52 | 9 × 9, (5, 5) → (3, 6), 3 legs. **Level-up f27373** (Louie level dword 41 → 42 = L43; HP 206 → 210 / 210, SP 22 → 47 / 47, DEF 68 → 69) |
| 29F (28) | 28132 → 30619 | **"Adventurers now nearly blind!"** | 0×2, 1×3, 2×2, 4×3, 6×5, 8×3, 10×2, 11×3, 13×6, 15×6, 16×10, 17×2, 18×3, 22×4, 35×3, 37×2, 44×3, 50×2, 52×2; 66 | 9 × 11, (2, 3) → (5, 6), 6 legs |
| **30F (29)** | 30619 → 31828 (town map) | none | **37 ×5, 38 ×5, 39 ×5, 40 ×5 (105 each, L30)** | arena, below |

Floor change: walk onto the exit cell's centre `{dpad(cx - px, cz - pz)}*200 --until "portal > 0"`, then `rd tap A --gap 10 --until
"floor == N"`: 38 ticks every time. The floor banner can appear ~10–40 ticks after arrival; while `ticker` ≠ 0 the pad does nothing.

## New enemy types (not killed in any earlier `docs/findings/probes/*.md`)

Stats at `0x5c23f0 + type*0x68` (+0x2c HP, EXP, AT, DF, MA, MD; rf[0..3] at +0x44; +0x20 flags):

| type | HP | EXP | AT | DF | MA | MD | rf | flags | kill |
|---|---|---|---|---|---|---|---|---|---|
| 37 bomb | 15 | 10 | 16 | 16 | 18 | 10 | 0.9, 1.5, 2, 2 | 0 | (killed before) 30F: idx 0 by the spin f30907 (105 → 42) then the blast f30920 |
| **38 bomb** | 15 | 15 | 16 | 18 | 20 | 10 | 0.9, 1.5, 2, 2 | 0 | **30F idx 1: 105 → 0 by the blast of idx 19 at f30920** (no hit of the player's on it) |
| 39 bomb | 15 | 20 | 16 | 20 | 22 | 10 | 0.9, 1.5, 2, 2 | 0 | (killed before: obsidian 25F) 30F: idx 6 by the blast f30920 |
| **40 bomb** | 15 | 25 | 16 | 22 | 24 | 15 | 0.9, 1.5, 2, 2 | 0 | **30F idx 19: sword 105 → 66 (39, back) f30892, spin 66 → 0 f30907** |
| 23 crown slime | 100 | 20 | 30 | 8 | 0 | 10 | 0.8, 4, 3, 4.5 | 0 | 25F, all three (card) |
| 2 blue slime | 15 | 8 | 16 | 4 | 0 | 10 | 0.8, 1.5, 2, 2 | 0 | 25F (boss-spawned, 102 HP at L29) |

Not killed here (killed before): 0, 1, 4, 6, 8, 10, 11, 13, 15, 16, 17, 18, 22, 35, 44, 50, 52. **Type 34 wisp: not met** (21F and 28F
banners announced it; none in the actor table on either floor).

## 30F — twenty bombs (`FUN_00450443` case 29)

| frame | what |
|---|---|
| 30619 | on 30F: 20 bombs at (−40 ± 10, −40 ± 10), types `37 + (j & 3)` in slot order (0: 37, 1: 38, 2: 39, 3: 40, …), all state 8, y 0, HP 105; `gate` 3, `estage` 0, `etick` 0; party (−40, −45); `u8(bank + 0x2bcf5)` 1, `u8(bank + 0x2bcfe)` 0 |
| 30619..30858 | `lock` 0 (no control), bombs state 8 count 0..239; no banner, no dialogue |
| 30859 / 30869 | bombs state 0 (t 240) / `lock` −1 (control); bombs bob (y 1.28) and drift around the player |
| (undone branch 30869..31269, standing still) | bombs enter state 1 (up to 6 at once); their lobs (record 90) hit **37 (f≈31069), 37 (f≈31189), 27 (f≈31269)**: 210 → 109 in 400 ticks — standing still is not an option |
| 30892 | sword on idx 19 (type 40): 105 → 66 |
| **30907** | **spin kills idx 19** (66 → 0) and hits idx 0 (type 37, 105 → 42); `p_inv` 21 |
| 30912 | idx 19 death t 1: fuse `+0xaf0` set to **240** (type 40); it is still sliding (spin knock-back) |
| **30920** | **idx 19 (fuse 244, moving) comes within 1.5 of another bomb → explodes at once** (slot freed); the blast kills **8 bombs in one tick** (105 / 42 → 0: idx 0, 1, 3, 6, 8, 12, 15, 16 / 18), each a lit corpse (`+0x428` 2, stagger 27 = knocked back). The player (spin, `p_inv` 16) takes nothing |
| 30930 | fuses read 5 ticks after the kill: **type 37 → 245, 38 → 485, 39 → 365, 40 → 245** (= start 240 / 480 / 360 / 240, blow at 540: 300 / 60 / 180 / 300 ticks) |
| 30985 | a second chain: live 10 → 3 (another corpse blew while moving) |
| 31020..31058 | spins finish the last 3; live 0 at f31058, 14 corpses left |
| 31101..31391 | corpses blow on their fuses (13 → 0); the player kept away by a survival search (goal "all gone and `hp >= 210`", `--hold 20`). On an undone branch the LAST explosion hit for **119** (210 → 91) on the very frame the goal turned true (goal checked before the avoid) |
| **31391** | no actor left → **`estage` 0 → 2 in one tick** (stage 1 → flag `0x2bcf5` already 1 → 2), `gate` 3 → 0 at f31392; `lock` stays −1, **no fade, no dialogue, no town scene** |
| 31411..31551 | the pad is dead ~120 ticks after the clear |
| 31636 | return door "Return to town now?" at (−45.32, −40.94) (both portals up: next-floor (−35, −40), return (−45, −40)) |
| 31811..31828 | results bag 18/20 (Iron Armor, Spiked Armband, Apple ×13, Candy Apple ×2, Baked Yam) → town map f31828, `stime` 1 → 3 |
| 32188 / 32205 | Save slot 020 / `rd endsave` → `save:3c2a6b9c8b1d` |

How the floor is meant to be cleared: kill one bomb inside the cluster with a knock-back hit — its corpse slides into a neighbour and
blows at once; the blast (≥ 105 to each bomb in range) kills most of the cluster, each victim a lit, sliding corpse that chains again.
Then stay away from the corpses until their fuses run out (a fuse explosion still hurts: 119 at L30, MDEF 42). Kill with the spin
(`p_inv` 30) so the chain's first blasts cannot touch you.

Bomb AI facts seen (port `actor_ai_bomb` / `actor_bomb_fuse` agree): state 8 lasts 240 on this floor; fuse starts per type as above;
moving corpse + another actor within 1.5 → instant explosion.

## Story / events

- **None in the dungeon:** `dlg` 0 from f6340 to f31828 (no first-entry event on 21F, none on 25F / 30F, no story exit — see the top).
- Dialogue before the dungeon: the night / payment lines above (f1166..2551).

## Food

Taiyaki ×2 on 25F: +31 SP (f21850), +30 SP (f23395). Nothing else eaten: HP never dropped on the kept movie (206 → 206 → 210).
Left: Apple ×13, Candy Apple ×2, Baked Yam.

## Tool gaps

1. Object 0 (`ex ez exitd`) is not the exit portal on every floor (26F: a decoration at (−28.9, −60.4)); the exit cell's centre from
   `0x438b43c / 440` was right on every floor. A named `exitc` (cell-centre distance) would avoid it.
2. `--hold` with a goal that only lasts 2 ticks (`aNuse == 2` on a blue slime: dying lasts 2 world ticks) rejects every arrival; and a
   goal "slot not in use" is also satisfied by an ABSORB. The first trio search applied a 1,009-frame best path (absorbs included).
   Goals over "the count of old slimes" (`actors(T == 2 and U == 1 and AGE > A, 3, 20) == 0`) plus an avoid on each boss's power rising
   worked.
3. `actors(..)` over all 64 slots inside `--goal` exceeds the 598-token limit (961); `actors(COND, 3, 20)` fits.
4. A goal and the avoid true on the same frame apply the path ("goal holds, avoid TRUE"): a bomb's fuse explosion landed on the goal
   frame. Put the avoid's condition into the goal (`and hp >= H`) or hold.
5. The `rd eval` parse trap (`name = value`) again: my own helper's regex skipped the `frame = N` line.
