# Dungeon roster — floors, bosses, setup / event / AI functions, port status

**code-read** (IDA `all_ida.c` + `.data` of `recettear.unpacked.exe` + `data/enemylist.txt` / `enemy.txt`); nothing run.
Shared boss-floor rules: `boss-floor-common.md`. Unlock state: `UNLOCKS.md`. Cards: `golem.md`, `griff.md`,
`demon-hand.md`, `jellyfish.md`, `arma.md`, `arma-golem.md`, `pumpkin-ghost.md`, `black-knight.md` (this author);
types 23 / 24 / 25 / 27 / 54 / 67 / 68 / 69 = other authors' cards.

Globals: dungeon `s32(0x438b4c8)`, floor (0-based) `s32(0x438b4cc)`, boss-run dungeon `s32(0x438b4d0)` (−1 = none),
run stage `s32(0x438b4d4)`, start floor (dungeon 5) `s32(0x438b4d8)`, event stage `s32(0x438b8a0)`, event tick
`s32(0x438b8a4)`, hired id `s32(0x438b7d8)`.

## 1. Dispatch (where type → function is chosen)

| what | function | notes |
|---|---|---|
| floor setup | `FUN_00436f97` (IDA 37404: stage type > 4) → d0 `FUN_0044c88f`, d1 `FUN_0044d1e4`, d2 `FUN_0044d9d3`, d3 `FUN_0044e08f`, d4 `FUN_00450443` (floor 99 → `FUN_0044ecc4`), else `FUN_00450ee0` | |
| floor events (per tick) | `FUN_004427f1` → stage type ≤ 4 `FUN_0044bd0d` (house), d0 `FUN_0044c9ba`, d1 `FUN_0044d47d`, d2 inline (floor 29 `FUN_0044db3c`, else `%5 == 4` → `FUN_0044d0a4(1)`), **d3 inline**, d4 `FUN_00450877` (floor 99 → `FUN_0044f13d`), else `FUN_00451546` | |
| generic boss-floor setup / event | `FUN_0044cc80(kind)` / `FUN_0044d0a4(portals)` | see `boss-floor-common.md` |
| actor tick | `FUN_00430c6d`: `+0x428 == 1` → `FUN_0042f28c` (alive AI switch on `+0x424`), `== 2` → `FUN_0042f7ad` (death arms); types 62 / 63 / 65 / 66 never reach either (own branch: follow parent `+0xa04`, detach, fall) | |
| stage static spawn list | `FUN_0042df55`: `DAT_0052c068[stage·1000 + i·10]`, 100 × 40-byte rows per stage, 20 stages in `.data` (all rows −2 / −1 = empty). Stage index ≥ 20 reads past the table into `DAT_0053f8e8` = the parsed enemylist sections (0x52c068 + 20·4000 = 0x53f8e8) | which stage INDEX the type-20 cutscene stage has: **not resolved** (needs `s32(0x438b4dc)` at runtime) |
| level | `FUN_0041f319(type)`: d0 `floor`; d1 `floor/2+2`; d2 `floor/3+6`; d3 `floor/4+16`; d4 `floor/5+25`; d5 `ftol((floor<30 ? floor : floor−30)·1.7+50)`; boss run d: the same formula on `5·floor−145` | HP = table HP·(1+0.2·lv) (`FUN_0041f903`) |
| stage tileset | `FUN_0045bc7f`: `DAT_005c5ae8[30·dungeon + floor/5 (+1 on the last floor)]`; d5 floors ≥ 30: `DAT_005c64d8[floor−30]`, overrides 37 → 15, 45 → 19, 58 → 17, 59 → 5; `FUN_0045bcea`: bank+0x2bce2 or +0x2bcf8 set → stage type 20 | |

### `FUN_0042f28c` alive-AI switch (complete)

| type(s) | enemy.txt name | AI | port |
|---|---|---|---|
| 0–5 slimes | スライム* | `FUN_0042c9b3` | ported, verified |
| 6–9 bats | アーリマン* | `FUN_0042ce17` (`+0xae4 == 3`: `FUN_00424509`) | ported, verified |
| 10–12 bees | キラービー* | `FUN_00428f0a` | ported, verified |
| 13 / 14 mushrooms | キノコ* | `FUN_00428ae4` (`+0xae4` 1 / 2: `FUN_00428718` / `FUN_004284e1`) | ported, verified |
| 15 pumpkin | かぼちゃ | `+0xae4` 0 `FUN_0042a33e`; 1 `FUN_0042a23c`; else `FUN_00429e6f` | 0 ported; 2 (`FUN_00429e6f`) ported, verified `obsidian-3f-5f-pumpkin`; 1 stub |
| 16 kobold | コボルド | `FUN_004262b7` (`+0xae4 == 3`: `FUN_00423686`) | ported |
| 17–21, 56–58 ropers | ローパー* | `FUN_0042a74c` | ported |
| 22 rabbit | 兎 | `FUN_00429962` | ported |
| 23 crown slime | クラウンスライム | `FUN_0042aef7` | ported, verified |
| 24 / 59 / 60 rats | ねずみバール / ハリセン / マグロ | `FUN_0042b6b7` | ported (24 verified; 59 verified: `lapis-1f-10f`; 60 recorded: `lapis-51f-60f`, not triaged) |
| 25 Charme | シャルム | `FUN_0041fa6e` | ported, verified |
| 27 Tielle | ティエール | `FUN_00423b58` | ported, unverified |
| 28 Griff | グリフ | `FUN_00424b42` | ported, verified `obsidian-21f-30f-griff` (identical + save; phase 0 / 1, all five states, the counter) and `obsidian-41f-50f-finale` (floor 49: phase 2, record 23, the kept bats) |
| 29 lost girl | (placeholder) | `FUN_0042da1d` | ported |
| 30 Alouette | アルエット | `FUN_0042d676`; floor 99: `FUN_0044f009` (prop) | ported unverified; floor-99 arm stub |
| 31 | (placeholder) | `FUN_0042d392` | ported, unverified |
| 34 wisp | ウィスプ | `FUN_0042ac16` | stub |
| 35 / 36 moai | モアイ金 / 銀 | `FUN_0042cfd0` | ported (`actor_ai_turret`), 35 variant 0 verified `obsidian-11f-15f-ahrimans`, 36 (golem adds) `obsidian-16f-20f-golem` (identical + save); variant > 0 (octant trio) no recording |
| 37–40 bombs | ボム* | `FUN_004293f0` | ported |
| **41 Arma** | アルマ | **`FUN_00425c3f`**; floor 99: `FUN_0044eff5` (prop) | ported (`actor_ai_arma`), verified `lapis-41f-50f` (identical + save); prop stub |
| 42 / 43 | ヒドラ / 親父 | `FUN_004284ba` (prop: `+0xb08 = +0xb04 = 1`) | stub |
| 44–48 knights | *ナイト | `FUN_00427f59` | ported |
| 49 black knight | ブラックナイト | `FUN_004279cf` | ported, verified `obsidian-41f-50f-finale` (identical + save) |
| 50 / 51 skeletons | スケルトン* | default `FUN_00428213` (after `+0xb8c` → `+0xadc = 2`) | ported (`actor_ai_skeleton`), verified `obsidian-31f-40f-ohms` (drift, lunge record 12, two revives) |
| 52 / 53 ghosts | ゴーストＯ / Ｎ | `+0xae4 == 0` `FUN_004275a7`, else `FUN_004278de` | ported (53 variant 0 verified: `obsidian-1f`; 52 + 53 walk variant verified: `obsidian-3f-5f-pumpkin`; 52 variant 0 no recording) |
| 54 boss ahriman | ボスアーリマン | `FUN_00426811` | ported, verified (the 15F pair: `obsidian-11f-15f-ahrimans` identical + save) |
| **55 pumpkin ghost** | かぼちゃゴースト | **`FUN_00426eab`** | ported (`actor_ai_t55`), verified `obsidian-3f-5f-pumpkin`, the pair `lapis-1f-10f` (identical + save) |
| **61 / 64 golem bodies** | ゴーレム / ダークゴーレム | **`FUN_00420f4d`** | ported; 61 verified `obsidian-16f-20f-golem` (identical + save; states 8 / 0 / 1 / 2 / 3, adds, death); 64 (dark) no recording |
| 62 / 63, 65 / 66 golem arms | ゴーレム右 / 左, ダーク… | none (`FUN_00430c6d` part branch) | ported; attached branch verified `obsidian-16f-20f-golem`; detached (arm lost) unreached |
| 67 Ohm | オーム | `FUN_00422ed6` | ported, verified (40F pair: `obsidian-31f-40f-ohms` identical + save) |
| 68 / 69 crabs | カニ / 黒カニ | `FUN_00422092` | ported (68 verified) |
| **70 / 71 jellyfish** | クラゲ / 赤クラゲ | `+0x3dc = DAT_0438ccc4`, **`FUN_004219f2`** | ported (`actor_ai_jelly`; 4 anim layers `FUN_004218e1`), 70 verified `obsidian-6f-10f-jellyfish` (identical + save; the 2 hit parts in `FUN_0043865e` with it); 71 no recording |
| 72 hand | 魔王の手 | `FUN_00424641` | ported, verified `obsidian-41f-50f-finale` (d3 floor 49; the d5 release unverified) |
| **73 / 74 Arma golem body / core** | アルマゴーレム / コア | **`FUN_004205e5`** | **stub** |
| **75 / 76 Arma golem hands** | アルマゴーレム右手 / 左手 | **`FUN_00420bd0`** | **stub** |
| 77 Prime body | プリム本体 | `FUN_0044f01d` (prop) | stub |
| 78 Prime wing, 26, 32, 33 | プリム羽, 岩とマグロ | default `FUN_00428213` | stub |

### `FUN_0042f7ad` death arms (`+0xaf0` = death tick)

| type | arm | port |
|---|---|---|
| 25 | Charme | ported |
| 27 | Tielle (`FUN_0042439e`) | ported, unverified |
| 28, 41 | tick 1 slow-mo 60 + player invulnerable 1200; tick 60 `FUN_0042f746` (no other live one of the type → every live actor → dying); d5: from tick 60 alpha −16 → free; else event stage 5: placed beside the party, stage 7: shrinks, y = event tick·0.25 → free | ported (28 verified `obsidian-21f-30f-griff`; 41 and the d5 fade unverified) |
| 70 / 71 | tick 60 `FUN_0042f746`; explosions (`FUN_0042f685`) ticks 1..119; from 100 alpha −8 → free; anim 13 | ported, verified `obsidian-6f-10f-jellyfish` (no add alive at tick 60) |
| 61 / 64 (+ 67, 68 / 69) | same + arms 62 / 63 / 65 / 66 copy alpha and `+0x428`; anim 10 | ported unverified (61 / 64) |
| 73 | y = 1; d5 only: from tick 60 alpha −4 → free, tick 60 `FUN_0042f746`; explosions 61..239; anim 12 ticks 2..301, then 13. d4: stays dying (the event reads `+0x428 == 2`) | **stub** |
| 74 | y = 1, anim 12; d5: from tick 60 alpha −16 → free | **stub** |
| 23, 49, 54, 55 | tick 1: slow-mo, explosion, alpha 0, player invulnerable 1200 (60 if another live one of the type); tick 60 `FUN_0042f746`; tick 120 free | 23 / 49 / 54 / 55 in the port's arm (55 verified: `obsidian-3f-5f-pumpkin`, the pair `lapis-1f-10f`; 49: `obsidian-41f-50f-finale`) |
| 24 / 59 / 60 | rat collapse (500 ticks) | ported |
| 37–40 | bomb fuse | ported |
| 72 and the rest | default: tick 2 free + puff (`+0xadc` revive count first) | default ported (72 verified: `obsidian-41f-50f-finale`) |

## 2. Dungeons

Floor counts = `DAT_005c64c0[d]`·5 = {1, 3, 6, 10, 20, 12}·5. Guild rows: d0..d4 "Floor 5g+1～5g+5" for g < cap, then
"Boss Rush" (row cap); d5 "Floor 10g+1～10g+10" for g < 3, row 3 "Boss Rush" (start floor 30). Names: guild plate strings.

| d | name | floors | rows | setup | events |
|---|---|---|---|---|---|
| 0 | Hall of Trials | 5 | 1 | `FUN_0044c88f` | `FUN_0044c9ba` |
| 1 | Jade Way | 15 | 3 + Boss Rush | `FUN_0044d1e4` | `FUN_0044d47d` |
| 2 | Amber Garden | 30 | 6 + Boss Rush | `FUN_0044d9d3` | inline; 29: `FUN_0044db3c` |
| 3 | Obsidian Tower | 50 | 10 + Boss Rush | `FUN_0044e08f` | inline in `FUN_004427f1` |
| 4 | Lapis Ruins | 100 | 20 + Boss Rush | `FUN_00450443` (ported: `dungeon4_stage_setup`), 99: `FUN_0044ecc4` (stub) | `FUN_00450877` (ported: `dungeon4_event_tick`; floors 4 / 9 verified `lapis-1f-10f`), 99: `FUN_0044f13d` (stub) |
| 5 | Crystal Nightmare | 60 (30 + 30 boss floors) | 3 + Boss Rush | `FUN_00450ee0` | `FUN_00451546` (37: `FUN_0044db3c(1)`) |

Boss Rush of d = 1..4: `0x438b4c8 = 5`, `0x438b4d0 = d`, start floor 30 / 33 / 38 / 46, 3 / 5 / 8 / 13 floors
(`FUN_0045c025`); runs on dungeon 5's setup / events and enemylist `ダンジョン６` (`f:N` = floor N−1).

### Checkpoint floors (0-based `floor`; "generic" = `FUN_0044cc80(1)` + `FUN_0044d0a4`, boss from the list's first line)

**d0** — no boss. Floor 4 (`FUN_00436f97`, IDA 37443..37462): bank+0x2bc82 = 0, then fee flag bank+0x2bc81 ≠ 0 or
mode 2 → portal kind 6 at the start + (0, 2, 10); else bank+0x2bc82 = 1 and portal kind 4 (the guild-intro run).

**d1** — 4: 23 ×1 (+ adds 2); 9: 24 (+ 13 / 14); 14: 25 Charme (own setup). Other authors.

**d2** — 4: 54 (+ 6 / 7); 9: 67 (+ 10); 14: mob list (no boss id); 19: 68 (+ 37); 24: 23 ×2; 29: 27 Tielle (own). Other
authors.

**d3 Obsidian Tower** (level `floor/4 + 16`)

| floor | list `f:` | boss (HP = table·mult) | adds the AI spawns | setup / event | card |
|---|---|---|---|---|---|
| 4 | 5 | 55 ×1 (15·4.4 = 66) | 52 / 53 / 15, cap 4 | generic | `pumpkin-ghost.md` |
| 9 | 10 | 70 (100·4.6 = 460, live) | 17 ×(phase+1) per cycle | generic (placed z −40) | `jellyfish.md` |
| 14 | 15 | 54 ×2 (15·4.8 = 72 each) | 6 / 7 / 8 | generic | other author |
| 19 | 20 | 61 + 62 + 63 (150·5.0 = 750 each) | 36, ≤ 4 over the fight | own: `FUN_0044e08f` case 19 | `golem.md` |
| 24 | 25 | none (green mob list) | — | generic, no boss | — |
| 29 | 30 | 28 Griff (150·5.6 = 840) | 6 ×48 per summon | own: case 29 + inline timeline | `griff.md` |
| 34 | 35 | none (slime list) | — | generic | — |
| 39 | 40 | 67 ×2 (150·6.0 = 900 each) | 10 | generic | other author |
| 44 | 45 | 49 (15·6.4 = 96) | 44..48, kept at 3 | generic | `black-knight.md` |
| 49 | 50 | 28 Griff (150·6.6 = 990) + 72 hand (990, takes 0 damage) | 6 | own: case 49 + inline timeline | `griff.md`, `demon-hand.md` |

Extra d3 state: first trip to row 0 runs a stage-type-20 cutscene (event 7/6); floor-49 clear → stage 20 again
(event 7/14) and out. See `UNLOCKS.md`.

**d4 Lapis Ruins** (level `floor/5 + 25`)

| floor | list | boss | adds | setup / event | card |
|---|---|---|---|---|---|
| 4 | 5 | 55 ×2 (150·6.0 = 900 each, retail) | 52 / 53 / 15, cap 6 | generic | `pumpkin-ghost.md` |
| 9 | 10 | 59 rat (150·6.2 = 930) | 13 / 14 | generic; after: event 10/4 once (+0x2bcf4) | other author (24) |
| 14 | 15 | 54 ×3 | bats | generic | other author |
| 19 | 20 | 67 ×3 | 10 | generic | other author |
| 24 | 25 | 23 ×3 (100·6.8 = 680 each) | 2, cap 5 | generic | `crown-slime.md` § Lapis 25F trio (port identical + save: `lapis-21f-30f`) |
| 29 | 30 | none: 20 bombs `37 + (j&3)` at (−40 ± 10, −40 ± 10) | — | own: `FUN_00450443` case 29; after: story exit once (+0x2bcf5): fade 120 → +0x2bcfd → the shop stage, `FUN_0048670f`'s +0x2bcfd arm (iv10_6 at b924 301, fade 80) → +0x2bcfe → back on 30F, the portals at etick 60 | — (port identical + save: `lapis-21f-30f` (flag set), `lapis-30f-story` (the story exit, the town scene, the return)) |
| 34 | 35 | 55 ×3 (15·7.2 = 108) | cap 5 | generic | `pumpkin-ghost.md` |
| 39 | 40 | 71 red jellyfish (150·7.4 = 1110) | 17 | generic | `jellyfish.md` |
| 44 | 45 | none (red mob list) | — | generic | — |
| 49 | 50 | 41 Arma (250·7.8 = 1950, observed) | — | own: default arm + `FUN_00450877` | `arma.md` |
| 54 | 55 | 49 ×2 (15·8.0 = 120; **live 100·8.0 = 800 each**, `lapis-51f-60f`) | 44..48, kept at 5 (2 spawned per tick: both BKs run the test) | generic | `black-knight.md` § 11 |
| 59 | 60 | 60 rat (150·8.2 = 1230; live 1230) | 13 / 14 | generic; after: event 12/4 once (+0x2bcf7; flag already set in the `lapis-51f-60f` lineage: skipped, `estage` stays 1) | `mushroom-king.md` § 9 |
| 64 | 65 | none (skeleton / ghost list) | — | generic | — |
| 69 | 70 | 64 + 65 + 66 dark golem (150·8.6 = 1290 each) | 35, ≤ 9 over the fight | own: case 69 | `golem.md` |
| 74 | 75 | none (yellow mob list) | — | generic | — |
| 79 | 80 | no fight: 42 ヒドラ + 43 親父 props, events 12/10..12/12 | — | own: case 79 + `FUN_00450877` | — (prop AI `FUN_004284ba`) |
| 84 | 85 | none (roper list) | — | generic | — |
| 89 | 90 | 69 black crab (150·9.4 = 1410) | 37..40 | generic | other author |
| 94 | 95 | none (knight list) | — | generic | — |
| 99 | 100 | 73 body (1·9.8), 74 core (9.8), 75 / 76 hands (150·9.8 = 1470); props 30, 41, 77 | — | `FUN_0044ecc4` + `FUN_0044f13d` | `arma-golem.md` |

**d5 Crystal Nightmare** — floors 0..28 mob lists (`f:1-5` … `f:26-29`), floor 29 a rest floor (return portal +
1..3 warp plates by start floor; `FUN_00450ee0` case 29), floors 30..58 one boss floor each, floor 59 the end floor
(case 59; no enemies). Level on floors ≥ 30: `ftol((floor − 30)·1.7 + 50)` = 50..97.

| floor | boss | setup | note |
|---|---|---|---|
| 30 / 31 / 32 | 23 / 24 / 25 | generic (`FUN_0044cc80(2)`) | = d1's Boss Rush |
| 33 / 34 / 35 / 36 | 54 / 67 / 68 / 23 ×2 | generic | d2's Boss Rush starts at 33 |
| 37 | 27 Tielle | own (default arm tail), event `FUN_0044db3c(1)` | |
| 38 | 55 | generic | d3's Boss Rush starts at 38 |
| 39 | 70 | generic | |
| 40 | 54 ×2 | generic | |
| 41 | 61 + 62 + 63 | own: case 41 | |
| 42 | 28 Griff | generic (list first line) | Griff's phase rule: 1 below 30 % HP |
| 43 | 67 ×2 | generic | |
| 44 | 49 | generic | |
| 45 | 28 Griff + 72 hand | generic + case 45 spawns the hand at (−35, 20, −25) | Griff's floor-49 rule does NOT apply (floor ≠ 49) |
| 46 | 55 ×2 | generic | d4's Boss Rush starts at 46 |
| 47 | 59 | generic | |
| 48 | 54 ×3 | generic | |
| 49 | 67 ×3 | generic | |
| 50 | 23 ×3 | generic | |
| 51 | 55 ×3 | generic | |
| 52 | 71 | generic | |
| 53 | 41 Arma | generic | |
| 54 | 49 ×2 | generic | |
| 55 | 60 | generic | |
| 56 | 64 + 65 + 66 | own: case 56 | |
| 57 | 69 | generic | |
| 58 | 73 + 74 + 75 + 76 | own: case 58 (5 pillars, `DAT_0438c150 = 5`); event `FUN_0044d0a4(1)` | |
| 59 | — | case 59 | end of the run / dungeon |

Every placed boss starts in AI state 8 (`+0xa54`); in dungeon 5 state 8 lasts 240 state ticks for 28 / 72 / 73 / 75 /
76 (each AI tests `0x438b4c8 == 5`), elsewhere the floor's event releases it.

## 3. Table stats of the boss-class types (enemy.txt over the `.data` rows at 0x5c23f0, stride 0x68)

HP / EXP / AT / DF / MA / MD; `rf[]` = the 7 floats at +0x44 (rf0 scale, rf2 half-height factor, rf3 radius factor:
hit radius = `+0xabc · rf3 · rf0`, half-height = `+0xabc · rf2 · rf0`, `FUN_0043865e` IDA 38950). "baked" = no
enemy.txt line, the `.data` values stand.

| type | name | HP | EXP | AT | DF | MA | MD | rf0 / rf2 / rf3 | boss flag (+0x20) | src |
|---|---|---|---|---|---|---|---|---|---|---|
| 28 | グリフ | 150 | 30 | 20 | 20 | 0 | 10 | 0.6 / 2.0 / 2.0 (rf6 1.2) | 0 | baked |
| 41 | アルマ | 250 | 25 | 50 | 35 | 40 | 35 | 0.6 / 1.5 / 2.0 | 0 | live (`lapis-50f`; the baked read said 305 / 30 / 20 / 20 / 0 / 10) |
| 42 | ヒドラ | 15 | 30 | 20 | 20 | 0 | 10 | 1.0 / 2.0 / 2.0 | 0 | baked |
| 43 | 親父 | 15 | 30 | 20 | 20 | 0 | 10 | 0.6 / 2.0 / 2.0 | 0 | baked |
| 49 | ブラックナイト | 15 | 30 | 2 | 20 | 0 | 10 | 1.2 / 2.0 / 2.0 | 0 | txt |
| 55 | かぼちゃゴースト | 15 | 30 | 20 | 20 | 0 | 10 | 0.9 / 2.0 / 2.0 | 0 | txt |
| 61 | ゴーレム | 150 | 25 | 20 | 30 | 0 | 10 | 0.9 / 3.0 / 4.5 (rf4 0.65) | 1 | txt |
| 62 / 63 | ゴーレム右 / 左 | 150 | 25 | 1 | 30 | 0 | 10 | 0.9 / 3.0 / 4.5 | 1 | baked |
| 64 | ダークゴーレム | 150 | 25 | 20 | 30 | 0 | 10 | 0.8 / 3.0 / 4.5 (rf4 0.65) | 1 | txt |
| 65 / 66 | ダークゴーレム右 / 左 | 150 | 25 | 1 | 30 | 0 | 10 | 0.8 / 3.0 / 4.5 | 1 | baked |
| 70 | クラゲ | 150 | 25 | 20 | 30 | 0 | 10 | 1.0 / 3.0 / 4.5 | 1 | txt |
| 71 | 赤クラゲ | 150 | 25 | 20 | 30 | 0 | 10 | 0.8 / 3.0 / 4.5 | 1 | txt |
| 72 | 魔王の手 | 150 | 25 | 20 | 30 | 0 | 10 | 0.8 / 3.0 / 4.5 | 1 | baked |
| 73 | アルマゴーレム | **1** | 0 | 20 | 30 | 0 | 10 | 1.1 / 3.0 / 4.5 | 1 | baked |
| 74 | アルマゴーレムコア | **1** | 25 | 0 | 30 | 0 | 10 | 1.1 / 3.0 / 4.5 | 1 | baked |
| 75 / 76 | アルマゴーレム右手 / 左手 | 150 | 25 | 20 | 30 | 0 | 10 | 1.1 / 3.0 / 4.5 | 1 | baked |
| 77 / 78 | プリム本体 / 羽 | 15 | 30 | 20 | 20 | 0 | 10 | 0.6 / 2.0 / 2.0 | 0 | baked |
| 35 / 36 | モアイ金 / 銀 (golem adds) | 15 | 0 | 20 | 20 | 0 | 10 | 0.6 / 2.0 / 2.0 | 0 | txt |

Boss flag (+0x20 == 1), `FUN_00430c6d`: the distance sleep `+0xaf4` is forced 0 (IDA 31880), the anim driver
`FUN_00482a71` is replaced by the +0x2c blend (IDA 31926..), and the fall-out-of-the-map kill (IDA 32163) is skipped.
MA = 0 on every row (see `boss-floor-common.md`).

## 4. Not resolved

- `FUN_0042df55` for stage index ≥ 20: the rows alias the parsed enemylist; whether the type-20 cutscene stage has
  index ≥ 20 at runtime (and what then spawns) needs `s32(0x438b4dc)` on that stage.
- Mob checkpoint floors (d3 24 / 34, d4 44 / 64 / 74 / 84 / 94): which placement fills them — not traced.
- d0's floor-4 events (2/9, 2/11 in `FUN_0044c9ba`) not read beyond the fee-flag test.

Re-read here: `FUN_00431990` boss-class ids = 23..25, 27, 28, 41, 43, 49, 54, 55, 59..73 (IDA `sub_431990`) — so 72
(hand) counts as a boss id, 74..78 do not.
