# Boss Rush — dungeon 5's floors 30.. run as "boss runs" of dungeons 1..4

**code-read**: entry = the Adventurers' Guild departure commit (port `src/scene_adv_guild.c:311..341`), floor count FUN_0045c025
(port `src/dungeon_flow.c:454`), setup FUN_00450ee0 (port `:1110`, IDA `sub_450EE0` all_ida.c:52562), events
FUN_00451546 (port `:1252`, IDA :52823), level FUN_0041f319 (port `src/actor_table.c`), drop tables
(port `src/record_hit.c:165`), enemylist.txt section `ダンジョン６` (user data). Shared rules: `boss-floor-common.md`.

## 1. Entry and structure

- Adventurers' Guild: picking the row past a dungeon's regular start floors (dungeons 1..4) sets
  `dungeon = 5`, **`s32(0x438b4d0)` = the source dungeon (mode)**, `s32(0x438b4d4) = 0` (run stage), and the floor:

| mode | source dungeon | first floor | floors (FUN_0045c025) | `floor` range |
|---|---|---|---|---|
| 1 | dungeon 1 | 30 | 3 | 30..32 |
| 2 | dungeon 2 | 33 | 5 | 33..37 |
| 3 | dungeon 3 | 38 | 8 | 38..45 |
| 4 | dungeon 4 | 46 | 13 | 46..58 |

- Every floor ≥ 29 of dungeon 5 is a checkpoint (FUN_0043195d) → no floor events, no respawns. Grid 3 × 3, arena
  centre (−40, −40) (floor 31: room type 12, the king's arena; 37: Tielle's corridor).
- Per floor (except 37 / 41 / 45 / 56 / 58): FUN_0044cc80(2) — **one** portal at (−40, 0, −40), sunk; next-floor
  (kind 8), or the return portal (kind 6) on the run's last floor (`0x438b4d4 == floors − 1`).
- Event FUN_0044d0a4(1): 240-tick lock, fixed camera, then the fight; no actor left → the portal rises
  (`s32(0x438b8a0) == 1`). Taking it: `floor + 1`, `0x438b4d4 + 1`.
- `s32(0x9643128) == 1` (no-exit flag) on every run floor but the last.
- Level: `f = 5·floor − 145`, then the source dungeon's formula (mode 1: `f/2 + 2`; 2: `f/3 + 6`; 3: `f/4 + 16`;
  4: `f/5 + 25`). HP ×(1 + 0.2·lv), AT ×(1 + 0.1·lv).
- Drops: the source dungeon's tables at its boss floors — mode 1: 4, 9, 14; mode 2: 4, 9, 19, 24, 29; mode 3: 4, 9,
  14, 19, 29, 39, 44, 49; mode 4: 4, 9, 14, 19, 24, 34, 39, 49, 54, 59, 69, 89, 99.

## 2. Roster (enemylist `f:N` = floor N − 1; first entry × count is placed, state 8)

| `floor` | boss (type) | lv | HP each | card |
|---|---|---|---|---|
| **30** | crown slime (23) | 4 | — | `crown-slime.md` |
| **31** | **ねずみバール = the mushroom king (24)** — "the rat boss" | 7 | 360 | `mushroom-king.md` |
| **32** | Charme (25) | 9 | 420 | `charme.md` |
| 33 | boss ahriman (54) | 12 | 51 | `boss-ahriman.md` |
| 34 | Ohm (67) | 14 | 570 | `ohm.md` |
| 35 | crab (68) | 16 | 630 | `crabs.md` |
| 36 | crown slime (23) × 2 | 17 | — | `crown-slime.md` |
| 37 | Tielle (27), own setup + timeline | 19 | 720 | `tielle.md` |
| 38 | pumpkin ghost (55) | | | — |
| 39 | jellyfish (70) | | | — |
| 40 | boss ahriman (54) × 2 | | | `boss-ahriman.md` |
| 41 | golem (61 + parts 62 / 63), own setup | | | — |
| 42 | griffon (28) | | | — |
| 43 | Ohm (67) × 2 | | | `ohm.md` |
| 44 | black knight (49) | | | — |
| 45 | griffon (28) + demon lord's hand (72, own setup) | | | — |
| 46 | pumpkin ghost × 2 | | | — |
| 47 | ねずみハリセン (59) | | | `mushroom-king.md` |
| 48 | boss ahriman × 3 | | | `boss-ahriman.md` |
| 49 | Ohm × 3 | | | `ohm.md` |
| 50 | crown slime × 3 | | | `crown-slime.md` |
| 51 | pumpkin ghost × 3 | | | — |
| 52 | red jellyfish (71) | | | — |
| 53 | アルマ (41) | | | — |
| 54 | black knight × 2 | | | — |
| 55 | ねずみマグロ (60) | | | `mushroom-king.md` |
| 56 | dark golem (64 + 65 / 66), own setup | | | — |
| 57 | black crab (69) | | | `crabs.md` |
| 58 | アルマゴーレム (73..76), own setup, five movers (HP 100 = unbreakable) | | | — |

## 3. What differs from the story floors

| boss | difference in the rush |
|---|---|
| mushroom king | level 7 instead of 6 (HP 360 vs 330); otherwise identical |
| Charme | no timeline: state 8 ends at her tick 240 (FUN_0041fa6e's dungeon-5 arm); death: fades from tick 60 and frees (no scene) |
| boss ahriman | level 12; minion cap 4, kinds 2 (floor < 38); floors 38..44: cap 6, 3 kinds; ≥ 45: cap 8, 4 kinds |
| Ohm | **bee cap 8** (dungeon ≥ 4 arm) instead of 3; up to 39 bees |
| crab | level 16 |
| Tielle | FUN_0044db3c(1): no dialogues, bank flags untouched; her portal (−45, 0, 65) is the run's return portal |
| ×2 / ×3 floors | the death arms of 23 / 49 / 54 / 55 give the player 60 invulnerable ticks, not 1200, while another of the type lives; the "last of its kind" sweep (FUN_0042f746) fires only for the last |

## 4. Recipe (mode 1: floors 30..32)

1. Per floor: `--until "lock == -1"` (240 ticks), run the boss's card, then
   `--until "s32(0x438b8a0) == 1"`, walk to (−40, −40), `portal` → `A`.
2. Progress predicates: `floor == 31`, `floor == 32`, `s32(0x438b4d4) == 1 / 2`.
3. HP / SP carry across floors; no exit before the last floor (`s32(0x9643128) == 1`).
4. Last floor: the portal is the return portal (kind 6) → results.

## 5. Arms checklist

| arm | predicate |
|---|---|
| run entered | `dungeon == 5 and s32(0x438b4d0) == 1 and floor == 30` |
| each floor's intro | `lock == 0` for event ticks 0..239 |
| floor cleared → portal | `s32(0x438b8a0) == 1` |
| next floor | `floor` + 1, `s32(0x438b4d4)` + 1 |
| no-exit flag | `s32(0x9643128) == 1` on floors 30 / 31, 0 on 32 once cleared |
| last floor's return portal | object kind 6 at (−40, 0, −40); leaving ends the run |
| run failed (party falls) | `hp <= 0` (dungeon2-run recording) |
| run passed | results after floor 32 (dungeon2-run2 recording) |

## 6. Port status

- Mode 1 (30..32): setup, events, all three bosses ported; RNG-identical on dungeon2-run / -run2 (FRONT).
- Mode 2 (33..37): bosses ported; Tielle's blocks cannot break in the port (`tielle.md` §7); no recording.
- Modes 3 / 4: types 28, 41, 49, 55, 61..66, 70..76 have no AI (`PORT-DEBT(stub, FUN_0042f28c)`,
  actor_ai.c:3638) and no death arm (`PORT-DEBT(stub, FUN_0042f7ad)`, :820); golem parts
  `PORT-DEBT(stub, FUN_00430c6d)` (:4095).
- FRONT's dungeon paragraph still says "Tielle's FUN_0044db3c [is a stub]" for dungeon 5 — stale: it is ported
  (dungeon_flow.c:854) and called for floor 37 (:1295).
