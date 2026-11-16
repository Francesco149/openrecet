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

## 1b. Crystal Nightmare's own "Boss Rush" row (dungeon 5, row 3) — code-read

Not an alias of modes 1..4: it is the **native** dungeon-5 gauntlet, all 29 boss floors 30..58 in one trip, then the rest
floor 59 (60F). IDA refs: guild init `sub_45BDC2` all_ida.c:59767, commit (`sub_45C051`) :59929, `sub_45C025` :59808, `sub_450EE0`
:52562, `sub_451546` :52823, `sub_44CC80` :50134, level `sub_41F319` :19331, best time (`sub_442CEF`) :44836..44860.

- **Unlock / visibility:** dungeon 5's row count = `(s32(bank·0xb106) + 1) / 5 + 1`, capped at 4 (no cleared-group bump, unlike
  d1..4; :59769). Row 3 ("Boss Rush": the d5 label is `g < 3 ? "Floor 10g+1～10g+10" : "Boss Rush"`) is listed and pickable once
  Crystal's deepest floor dword `0xb106` ≥ **14** (15F reached) — the plate itself needs `0x2bc53 != 0 && 0x2bd00 == 1`.
- **Commit (:59929):** `0x438b4d0` := **−1**, `0x438b4d4` := **−1** (both before the dungeon test, never overwritten for d5),
  `dungeon` (`0x438b4c8`) = 5, **`floor` = `0x438b4d8` = 10·row = 30**. So `mode` stays −1: `sub_45C025` returns **0** (default arm);
  there is no floor count and no "last stage".
- **Floor chain:** every test `b4d0 >= 0 && b4d4 == count − 1` is false → on floors 30..58 the portal is always the **next-floor**
  kind 8 (incl. 37 Tielle's (−45, 65), 41 / 45 / 56 via `sub_44CC80(2)`, 58's (−40, −47)); `0x9643128` (no-exit) = 1 on 30..58.
  Floor 58's portal leads to **floor 59 = the `sub_450EE0` case 29 / 59 rest floor**: party in a row, return door (kind 6, scale 0.35)
  at (−48, 0, −42), **3 treasure chests** (kind 4: `v18` = 3, the −1 / −2 for start floors 10 / 20 applies only on floor 29) at
  party + 10 z, (−53, −35), (−27, −35); 371-tick intro camera (`sub_451546` case 59); no-exit 0. The door opens on a sword hit
  (as 30F, `probes/crystal-21f-30f.md`).
- **Levels (`sub_41F319`, b4d0 < 0, dungeon 5):** `ftol((floor − 30)·1.7 + 50)` = **50 .. 97** on floors 30..58 (Crystal's own curve,
  restarted at 30), not the source-dungeon formulas of modes 1..4. HP ×(1 + 0.2·lv) → 30F crown 1100, 53F Arma 4700, 58F golem body
  **6120**; attack ×(1 + 0.12·lv) (≥ L50) → ×7.0 .. ×12.64 (mode 4's 58F was ×7.48).
- **Drops:** `enemy_drop_pick` with mode < 0 reads dungeon 5's own enemylist tables at the real floor (30..58), not the source
  dungeons' boss floors.
- **Best time:** `sub_442CEF`'s portal prompt (:44836): dungeon 5, `b4d4 < 0`, `floor >= 30`, return portal (`0x438bea8` != 0) →
  dword **`bank·0xb172`** (= `0xb10b + 20·5 + 3`, dword index 45426) := trip counter `0x438b1e8` if 0 or smaller — the row's display slot.
  Floors < 30 use `0xb10b + 100 + floor/10` at `floor % 10 == 9` (rows 0..2).

| floor | 30 | 31 | 32 | 33 | 34 | 35 | 36 | 37 | 38 | 39 | 40 | 41 | 42 | 43 | 44 | 45 | 46 | 47 | 48 | 49 | 50 | 51 | 52 | 53 | 54 | 55 | 56 | 57 | 58 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| lv | 50 | 51 | 53 | 55 | 56 | 58 | 60 | 61 | 63 | 65 | 67 | 68 | 70 | 72 | 73 | 75 | 77 | 78 | 80 | 82 | 84 | 85 | 87 | 89 | 90 | 92 | 94 | 95 | 97 |

- **Live (`probes/bossrush-crystal.md`):** plate `D` → 5, rows `D` ×3 → "Boss Rush --:--:--", `A` → `dungeon` 5, `0x438b4d0` −1, `0x438b4d4` −1, `0x438b4d8` 30, `floor` 30 (fee 4,850 at L49); `+0xae0` = the table above on 30..55 and HP = table·(1 + 0.2·lv) on every floor; every portal 30..54 next-floor (37F at (−45, 65)). Floors 56..59, the rest floor and the 0xb172 write not reached (stopped on 55F). **`probes/bossrush-crystal.md` loadout run `bossrush-crystal-c`** (crafted `crystal` loadout): all 29 floors cleared, levels / HP = the table on every floor (56 dark golem 2970, 57 black crab 3000, 58 Arma golem body 6120, 6 clamps of 1020), every portal 30..58 next-floor (58's at (−40, −47) → floor 59), rest floor: 3 chests (sword opens them), door hit → "Return to town now?" → **0xb172 := 123,420 = `0x438b1e8`** on the `A` (f138711). Filed, `rd verify` / `--real` MATCH.

## 2. Roster (enemylist `f:N` = floor N − 1; first entry × count is placed, state 8)

| `floor` | boss (type) | lv | HP each | card |
|---|---|---|---|---|
| **30** | crown slime (23) | 4 | 180 [obs] | `crown-slime.md` |
| **31** | **ねずみバール = the mushroom king (24)** — "the rat boss" | 7 | 360 [obs] | `mushroom-king.md` |
| **32** | Charme (25) | 9 | 280 [obs `bossrush-jade-amber`] | `charme.md` |
| 33 | boss ahriman (54) | 12 | 340 [obs] | `boss-ahriman.md` |
| 34 | Ohm (67) | 14 | 570 [obs] | `ohm.md` |
| 35 | crab (68) | 16 | 714 [obs; table HP 170] | `crabs.md` |
| 36 | crown slime (23) × 2 | 17 | 440 each [obs] | `crown-slime.md` |
| 37 | Tielle (27), own setup + timeline | 19 | 480 [obs] | `tielle.md` |
| 38 | pumpkin ghost (55) | 27 | 960 [obs `bossrush-obsidian-ending`; table HP 150] | `pumpkin-ghost.md` |
| 39 | jellyfish (70) | 28 | 660 [obs] | `jellyfish.md` |
| 40 | boss ahriman (54) × 2 | 29 | 680 each [obs] | `boss-ahriman.md` |
| 41 | golem (61 + parts 62 / 63), own setup | 31 | 1080 each part [obs] | `golem.md` |
| 42 | griffon (28) | 32 | 1480 [obs; table HP 200] | `griff.md` |
| 43 | Ohm (67) × 2 | 33 | 1140 each [obs] | `ohm.md` |
| 44 | black knight (49) | 34 | 780 [obs; table HP 100, AT 35] | `black-knight.md` |
| 45 | griffon (28) + demon lord's hand (72, own setup) | 36 | Griff 1640 (slot 1), hand 820 (slot 0) [obs] | `griff.md`, `demon-hand.md` |
| 46 | pumpkin ghost (55) × 2 | 42 | 1410 each [obs `bossrush-lapis`] | `pumpkin-ghost.md` |
| 47 | ねずみハリセン (59) | 43 | 1440 [obs; table 150/26/42/10/42, `+0xab8` 2 → DF·4: only state 4 hurts] | `mushroom-king.md` |
| 48 | boss ahriman (54) × 3 | 44 | 980 each [obs] | `boss-ahriman.md` |
| 49 | Ohm (67) × 3 | 45 | 1500 each [obs] | `ohm.md` |
| 50 | crown slime (23) × 3 | 46 | 1020 each [obs; table 100/30/8/0/10] | `crown-slime.md` |
| 51 | pumpkin ghost (55) × 3 | 47 | 1560 each [obs] | `pumpkin-ghost.md` |
| 52 | red jellyfish (71) | 48 | 1060 [obs; table 100/30/35/20/35] | `jellyfish.md` |
| 53 | アルマ (41) | 49 | 2700 [obs; table 250/50/35/40/35] | `arma.md` |
| 54 | black knight (49) × 2 | 50 | 1100 each [obs; table 100/35/20/0/20] | `black-knight.md` |
| 55 | ねずみマグロ (60) | 51 | 1680 [obs; table 150/28/52/10/52, `+0xab8` 3] | `mushroom-king.md` |
| 56 | dark golem (64 + 65 / 66), own setup | 52 | 1710 each part [obs; table 150/30/30/0/40] | `golem.md` |
| 57 | black crab (69) | 53 | 1740 [obs; table 150/40/30/20/30] | `crabs.md` |
| 58 | アルマゴーレム (73..76), own setup, five movers | 54 | body 3540, core / hands 1180 [obs; body table 300/50/35/50/35, 74..76 100/20/30/0/30] | `arma-golem.md` |

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

## 3b. Observed (`probes/bossrush-jade-amber.md`, modes 1 and 2 played live)

- Floor → floor: `estage` 1, the pad ignored ~120 ticks, then the kind-8 portal (object 0) at (−40, −40): "Go to the next floor?".
- **Last floor: the return portal is a SHUT kind-6 door** (floor 32: object 8 at (−40, −40); floor 37: Tielle's door (−45, 65)); a
  walk into it stops 1.4 out, **a sword hit opens it** (state 1), then "Return to town now?" (`pret` 1).
- After it: the ordinary "CAME BACK ALIVE!" bag screen → town map; **no results screen, no reward, no rush-specific drop**.
- **Best time:** on the first tick of the return prompt, dword `0xb10b + 20·d + cap[d]` (d1: 0xb122, d2: 0xb139) = the trip counter
  `s32(0x438b1e8)` (Jade 4736 replaced 10387; Amber 13326 replaced 0).
- `0x9643128` (no-exit) stays 1 on cleared non-last floors, 0 from the last floor's clear.
- Louie L47 (ATK 65 / DEF 85): lowest HP 126 / 228 (the crab); food from the pause menu between hits.

## 3c. Observed, mode 3 (`probes/bossrush-obsidian-ending.md`, Obsidian Boss Rush played live)

- Departure: plate cursor 3 (Obsidian), row cursor 10 = "Boss Rush" (`--:--:--` before; d3 best dword **0xb151** = `0xb10b + 60 + 10`);
  `dungeon` 5, `0x438b4d0` 3, `floor` 38; fee 4,650.
- Levels = the code-read formula `f/4 + 16` on `f = 5·floor − 145`: 27, 28, 29, 31, 32, 33, 34, 36 for floors 38..45.
- Floor 45 (last, run stage 7): the return is a shut kind-6 door, **object 0** at (−40, −40) (`doorobj` 0); one sword hit from (−40, −42.5)
  facing +z opened it (f29018), "Return to town now?" at f29048. Griff's kill (slot 1) took the hand (slot 0) with it; `estage` 1 f28796.
- Best time 21,915 ticks (6:05) into 0xb151 at the prompt (was 0). No results / reward again.
- The pause menu does not open for ~125 ticks after `estage` 1 (ESC posted earlier is dropped).
- Louie L47 → level-up at the golem clear (f12711: max HP 228 → 232, SP 51 → 52, refilled). Lowest HP 151 (39F); Griff ×2 floors
  without damage. Danger floor = 43 (Ohm pair + bee cap 8): searches that stop on an arrival often sit 10..60 ticks before a committed
  charge / sting (43–65 damage); plain `--apply` with `--hold 100` and an undo on a failed search got through without loss.

## 3d. Observed, mode 4 (`probes/bossrush-lapis.md`, Lapis Boss Rush played live, floors 46..57 cleared, 58 in progress)

- Departure: plate cursor 4 (Lapis), row cursor 20 = "Boss Rush" (00:04:19 shown = d4 best dword **0xb16f** = 15591 already in
  the crafted lineage; d4 0xb105 = 99, 0xb16e = 1); `dungeon` 5, `0x438b4d0` 4, `floor` 46; fee **4,750** (L48 Louie).
- Levels = `f/5 + 25` on `f = 5·floor − 145`: 42..54 for floors 46..58 (all 13 live HPs match table·(1 + 0.2·lv)).
- Party at (−40, −45) on every floor incl. 58 (card said (−40, −60) for 58); 240-tick pad lock; next-floor portal as in modes 1..3.
- 47F / 55F rats: `+0xab8` 2 / 3 → outside state 4 every hit does 1–2. Mushrooms live in actor slots up to ≥ 24 (scan beyond 15).
  The eat pick only takes mushrooms of **age ≥ 600** (`sub_42B6B7`, IDA 27805+256). At eat tick 38 `+0xa74` := 0, then
  (mushroom alive) 2 red / 1 purple; tick 48 `+0xa70` := `+0xa74`. Killing the target before tick 38 → no meal (`+0xa70` 0, state 0);
  a walk blocked for 180 ticks re-uses the stale `+0xa70` / `+0xa74` (purple → a free state-4 window). Red meals heal max/6 (+280 at 1680).
- 49F: three Ohms, no damage taken by searches with hold tails that strafe perpendicular to whichever Ohm is in state 2.
- 57F black crab: bomb blasts on the crab damage it (≈ 270) as well as flipping it (as `crabs.md` §7).

## 4. Recipe (mode 1: floors 30..32)

1. Per floor: `--until "lock == -1"` (240 ticks), run the boss's card, then
   `--until "s32(0x438b8a0) == 1"`, walk to (−40, −40), `portal` → `A`.
2. Progress predicates: `floor == 31`, `floor == 32`, `s32(0x438b4d4) == 1 / 2`.
3. HP / SP carry across floors; no exit before the last floor (`s32(0x9643128) == 1`).
4. Last floor: the portal is the return door (kind 6): sword hit, then walk in → "Return to town now?" → bag screen (no results).

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
