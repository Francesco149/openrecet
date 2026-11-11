# Tielle — type 27 (FUN_00423b58), floor timeline FUN_0044db3c, platforms, perched kobolds (FUN_00423686)

**code-read**, and the port here is **unverified by any recording**: port `src/actor_ai.c:3341..3578` (AI, kobold),
`:605..640` (FUN_0042439e), `:790..817` (death), `src/dungeon_flow.c:821..1045` (timeline, setup) — each checked
against IDA `sub_423B58` (all_ida.c:22548), `sub_44DB3C` (:50824), `sub_42439E` (:22950), `sub_42F7AD` case 27,
`sub_42353C` (:22266), the mover-damage arm of `sub_43AE20` (:43134..43150). AI / timeline / death match IDA.
**The platform-damage path is not ported** (§7). Shared rules: `boss-floor-common.md`.

## 1. Where

| place | `dungeon` / `floor` | timeline |
|---|---|---|
| dungeon 2 30F | 2 / 29 | FUN_0044db3c(story mode == 2) |
| Boss Rush (dungeon 2's), 5th = last floor | 5 / 37, `0x438b4d0 == 2` | FUN_0044db3c(1): no dialogues, no save flags |

- Setup (FUN_0044d9d3 floor-29 arm / FUN_00450ee0 case 37): party in a column at x −40 from z −54 (1.3 apart) facing
  +z; Tielle at (−40, 0, −40), state 8 (unused by her AI), `+0xae4 = 0`; town portal (−45, 0, 65) sunk.
  Already beaten (bank+0x2bd12 == 1, not story mode 2) → no Tielle, stage 10.
- The floor is a corridor along +z in three sections (phases). Two different section edges, both retail:
  timeline `zbT` = −25 / 25.25 / 75.5; AI `zbA` = −27 / 23.25 / 73.5.

Timeline — stage `s32(0x438b8a0)`, tick `s32(0x438b8a4)`, phase `s32(0x438b8a8)`:

| stage | pad | what |
|---|---|---|
| 0 | locked | first time (bank+0x2bcba 0): dialogue iv4_1 at 30, iv4_2 at 120; ticks 60..99 pad mask 4; **tick 150 → 3**, `0x9643128 = 1` |
| 3 | locked | tick 1: the phase's blocks are created (below), y = 20 + 5·rand each (1 unit draw per block); every tick each sinks 0.5; all at 0 → 4 (≈ 41..51 ticks) |
| 4 | free | Tielle's AI: once she stands, tick > 60 → 5 |
| 5 | free | attack loop. Player z clamped to zbT − 32.2 .. zbT + 1.8. Tielle "hit" → 6 |
| 6 | locked 120 | player invulnerable (`0x56db0f4 = 5` each tick). Phases 0 / 1: blocks give way, Tielle runs off; player z > zbT + 25 → phase++, stage 3. Phase 2: → 7 |
| 7 | free | **the fight**. No actor with `+0x428` ∉ {0, 2} → 8 (sets bank+0x2bcbb, +0x2bd12 unless replay), `0x9643128 = 0` |
| 8 | locked | tick 180 fade; party to x-start, z 57 − 1.5i → 9 |
| 9 | locked | dialogue iv4_3 at 30 (not on replay); 60 → 10 |
| 10 | free | town portal rises |

## 2. Stats

- Table (.data defaults; no enemy.txt line): HP 150, AT 20, DF 20, MD 10. Body radius 1.2.
- d2 f29: level 15 → **HP 600**, AT·mul 50 (arrow `ftol(25 − DF/4)`). Rush f37: level 19 → HP 720, AT·mul 58.
- Blocks ("platforms", collision movers, model slot 13): **HP 2 each** (`DAT_0438c218[m]`), 31 / 32 / 31 per phase.
- Perched kobold (type 16): table HP 20 → 80 at level 15.

Blocks: grid 9 × 6, x = −25 − 3.8·col, z = zbT − 3.8·row; created row-major (index i from 0). `#` = block:

```
          x: -25 … -55.4          phase 0 (zbT -25)   phase 1 (25.25)   phase 2 (75.5)
row 0 (z = zbT)                   .##...##.           .##.#.##.         .##...##.
row 1 (zbT - 3.8)                 #.#...###           #.#.#.###         #.#.#.###
row 2 (zbT - 7.6)                 .#####.#.           .###.#.#.         .###.#.#.
row 3 (zbT - 11.4)                ##.######           ##.######         ##.######
row 4 (zbT - 15.2)                .#####.#.           .#####.#.         .#####.#.
row 5 (zbT - 19)                  ......##.           ......##.         .......##
```

(byte_005c5734, phase p reads bytes 50p..50p+53; phase 2's last two cells are the first bytes of the next string.)
Block i: on `s32(0x438c154 + 4*i)`, HP `s32(0x438c21c + 4*i)`, x / y / z `f32(0x438c474 + 4*i)` /
`f32(0x438c53c + 4*i)` / `f32(0x438c604 + 4*i)`; count `s32(0x438c150)`.

Block damage (sub_43AE20 wall-probe tail, IDA 43134): a **player** record whose wall ray hits a mover:
HP < 100 → HP −1 (and `DAT_0438c3a8[m] = 30`); HP ≠ 0 → SE 361 + spark; **HP 0 → FUN_0042353c**: block off, SE 318,
bursts, shake, then `rand & 3 == 0` → an enemy spawns there (`rand & 1` odd → type 2 blue slime, even → type 15
pumpkin), else `rand & 7 == 0` and FUN_00484e45() == 0 → a chest item roll. The record dies on the hit. Which player
records run the wall probe (flag +0xc4): sword swings 2 / 3 / 4 and the projectile families — per-type list not
re-derived here. Footprint of a block not read (collision mesh); the pitch is 3.8 and the collision cull box ±3, so
adjacent blocks probably form a wall — **unverified**.

## 3. Tielle's AI — sub-mode `s32(B+0xae4)`, state `s32(B+0xa54)`, tick `s32(B+0xa58)`, block `s32(B+0xa64)`

### Sub-mode 0 (scripted; `+0xb08 = 1`: unhittable, `+0xb04 = 1`, `+0x9fc = 1`)

- **Stages 3 / 4.** z < zbA and `+0xa64 == −1`: walks +z (accel 0.06, steers to x −41..−39). Else stands (vz ×0.9,
  yaw π). Phase ≥ 1: alpha 0 once z > zbA − 2; on arrival `+0xa64 = rand % 11` and **she is moved to that block's
  x / z** (one of the first 11 blocks: rows 0 / 1, in phase 2 also (−28.8, 67.9)); alpha 0 while the block's y is 0.
  Stage 4 and tick > 60 → stage 5.
- **Stage 5**, cycle `c = +0xa58 % 150`: anim 4 at 10, anim 0 at 50; 1 rand at c == 26 (voice).

| phase | volley | "hit" → stage 6 |
|---|---|---|
| 0 | record 132 at c = 26..30 (5) | player within **2.5** of her |
| 1 | record 132 at c = 26..35 (10) | her block gone (`s32(0x438c154 + 4*s32(B+0xa64)) == 0`) |
| 2 | record 104 at c = 26, 34, 42, 50 (4) | her block gone |

- **Stage 6**: alpha 255. Phases 0 / 1: FUN_0042439e on the event tick — block i bursts at 10 + 4i and is off at
  20 + 4i; tick 30: every other live actor starts dying. From tick 100 she runs +z until z ≥ zbA + 36,
  `+0xa64 = −1`. Phase 2: at tick ≥ 4·count + 20 → `+0xae4 = 1`, state 0, stage 7 (blocks stay).

### Sub-mode 1 (the fight; `+0xb08 = 0`, `+0xb54 = 1`)

| state | length | what |
|---|---|---|
| 0 line-up (FUN_00423789, reach 12, wait 20, speed 0.025, start 5, rest 30) | runs of 35..98 | walks axis headings; from tick 20, player within 1.5 of one of her axes and 2..12 along the other, and she is on the ground → faces the player, state 1 |
| 1 shot | 80 | anim 3; tick 12: SE 674, `rand & 1` odd → record 78 (1 arrow) else 79 (3 arrows, ±0.18 rad) |

Her z is clamped to zbA − 32.2 .. zbA + 1.8 (41.3 .. 75.3).

### Kobolds (stages 5 and 7)

While fewer than phase + 2 perched kobolds (type 16, `+0xae4 == 3`) are in use: `+0xa68` counts past 240 → reset,
`r = rand & 7`, blocks walked from the last to the first skipping gone / already-used ones, the r-th gets a kobold
5 above it (r = 0 → none this time). Perched kobold: `+0xb54 = 1`; rest 80..143 ticks facing the player, then a
91-tick shot (record 150 at tick 26, SE 691); once its y < 2 (block gone) it becomes an ordinary kobold
(`+0xae4 = 0`).

### Records

| id | what | hit |
|---|---|---|
| 132 | lobbed from 3 above her at the player's position (horizontal speed 0.01·dist + 0..0.04, dist clamped 5..30; vy 0.4, gravity 0.01 → ≈ 80 ticks of flight), ± 0.15 rad | reach −0.15 → within 1.85; physical; dies on landing |
| 104 | rain: from 20 up toward a random point 4..12 from the player | reach 0.2 → within 2.2; live record ages 20..79; physical |
| 78 / 79 | arrows from 0.8 ahead, 1.4 up, 0.5/tick along the facing octant | reach 0.5 → within 2.5; physical |
| 150 | kobold's lob (as 132, ± 0.35 rad, faster) | physical |

### Death (`+0x428 == 2`, FUN_0042f7ad case 27)

Tick 1: slow-mo, player invulnerable 1200, every other live actor dying. FUN_0042439e on the death tick (remaining
blocks burst from tick 10). She stays in the table; from stage 9 she stands by the party and fades after event
tick 60.

## 4. When she can be damaged

- Only in sub-mode 1 (stage 7). Never staggers. ×1.2 in state 1; back ×1.5.
- Before that the "damage" is positional: phase 0 reach 2.5 of her; phases 1 / 2 destroy the block she is in.
- She does not shoot a player within 2 along the axis or off both axes by ≥ 1.5.

## 5. Recipe (search)

- Names: `tst = s32(0x438b8a0)`, `tph = s32(0x438b8a8)`, `tx = f32(B+0x3f0)`, `tz = f32(B+0x3f8)`,
  `tblk = s32(B+0xa64)`, `thp = f32(B+0x42c)`, `tsub = s32(B+0xae4)`.
- Stage 0 / 3: wait (`--until "tst == 5"`).
- **Phase 0:** goal `tst == 6`; score `-hypot(px − tx, pz − tz)`. She stands at (≈ −40, ≈ −27). Rows 2..4 block
  the straight line; cut a lane (2 hits a block) — e.g. column x −40.2 (rows 4, 3, 2 → 6 hits), then row 1 / 0
  are open there. Put the lane blocks' HP in the cell: `s32(0x438c21c + 4*i)`.
- After each stage 6: walk +z until `pz > zbT + 25` (goal `tst == 3`); lock lasts 120 ticks.
- **Phases 1 / 2:** read `tblk`; target block = (`f32(0x438c474 + 4*tblk)`, `f32(0x438c604 + 4*tblk)`). She is in
  rows 0 / 1 (the far side): cut a lane to it and hit it twice. Goal `tst == 6` (phase 1) / `tst == 7` (phase 2).
  Standing still dodges 104 (targets are ≥ 4 away); 132 lands where the player stood ≈ 80 ticks earlier — keep moving.
- Kill or ignore kobolds (80 HP); a kobold whose block goes falls and becomes a walker.
- **Fight:** stay within 2 of her or off her axes; cell `(int(thp/10), s32(B+0xa54), s32(B+0xa58)//10,
  int((px − tx)/1.5), int((pz − tz)/1.5))`, score `-thp`, avoid `hp < 30`. Per hit `ftol(ATK/2 − 5)`
  (ATK 54 → 22; back + state 39). 600 HP ≈ 16–28 hits.
- End: `tst == 10`, portal at (−45, 0, 65).

## 6. Arms checklist

| arm | predicate |
|---|---|
| first-visit dialogues / replay without | `dlg == 1` at stage 0 ticks 30 / 120; none in the rush |
| blocks drop (each phase) | `tst == 3`, `s32(0x438c150)` = 31 / 32 / 31 |
| phase 0 volley (5 × 132) | `tst == 5 and tph == 0`, `s32(B+0xa58) % 150` in 26..30 |
| phase 0 proximity hit | `tst == 6` with `tph == 0` |
| blocks give way + sweep | block i off at tick 20 + 4i; others dying at 30 |
| Tielle runs off; player crosses | `tph` 0 → 1 → 2, `tst == 3` again |
| she hides in a block (11 choices) | `tblk` in 0..10, `s32(B+0x3dc) == 0` |
| phase 1 volley (10 × 132) | `tph == 1`, cycle 26..35 |
| block damaged / destroyed | block HP 2 → 1 → 0, `on` → 0 |
| destroyed block spawns slime / pumpkin (1/4) | new slot type 2 or 15 at the block |
| destroyed block rolls a chest item (≈ 3/32) | an item drop at the block |
| her block destroyed → reveal | `tst == 6`, alpha 255 |
| phase 2 volley (4 × 104) | `tph == 2`, cycle 26 / 34 / 42 / 50 |
| phase 2 reveal → fight | `tsub == 1`, `tst == 7` |
| kobold summoned (cap phase + 2) | slot type 16 with `+0xae4 == 3`, y ≥ 2 |
| kobold roll r = 0 (no spawn) | `s32(B+0xa68)` resets with no new slot |
| kobold shoots | its `+0xa54 == 1`, record at tick 26 |
| kobold falls (block gone) | its `+0xae4 == 0` |
| fight: 1 arrow / 3 arrows | `s32(B+0xa54) == 1` |
| arrows / volleys hit the player | `hp` drops |
| player z clamp | `pz` pinned at zbT + 1.8 / zbT − 32.2 in stage 5 |
| player dies | `hp <= 0` |
| death, remaining blocks burst, ending | `s32(B+0x428) == 2`; `tst` 8 → 9 → 10; bank+0x2bd12 = 1 |
| revisit after the win | no type 27, `tst == 10` |

## 7. Port status

- Ported ahead of a recording (FRONT): AI, timeline, FUN_0042439e, death arm, kobold — read line by line against
  IDA, no divergence found.
- **Not ported / wrong:** the block damage. Retail decrements `DAT_0438c218[mover]` — the same memory the timeline
  fills with 2 (`g_mapcol_movers.on[50 + m]`) — and calls FUN_0042353c at 0. The port's records tick
  (`src/scene1_records_b_tick.c:6335..6352`) decrements a separate array `g_scene1_b_wall_lifetime[]` (BSS 0 →
  goes to −1, never 0) and its destroy hook is never installed (`scene1_records_b_set_wall_destroy_hook` has no
  caller; FUN_0042353c has no port). In the port a block can never break → phases 1 / 2 cannot end, and phase 0 only
  if a walkable gap exists. No PORT-DEBT tag marks this.
