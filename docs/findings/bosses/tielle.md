# Tielle — type 27 (FUN_00423b58), floor timeline FUN_0044db3c, platforms, perched kobolds (FUN_00423686)

**code-read, corrected from retail 2026-11-11** (run `am-30f`, dungeon 2 30F, Louie L26 ATK 54 / DEF 75; `rd verify` MATCH
at f39184; probe `docs/findings/probes/amber-26f-30f.md`; tags **[obs fNNNN]** are that run's frames). The port is
**verified on that movie** (scenario `amber-26f-30f-tielle`, triage identical ticks 1..39484 + save IDENTICAL, §7): port `src/actor_ai.c:3341..3578` (AI, kobold),
`:605..640` (FUN_0042439e), `:790..817` (death), `src/dungeon_flow.c:821..1045` (timeline, setup) — each checked
against IDA `sub_423B58` (all_ida.c:22548), `sub_44DB3C` (:50824), `sub_42439E` (:22950), `sub_42F7AD` case 27,
`sub_42353C` (:22266), the mover-damage arm of `sub_43AE20` (:43134..43150). AI / timeline / death match IDA.
The platform-damage path is ported (§7). Shared rules: `boss-floor-common.md`.

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
| 8 | locked | tick 180 fade; party to x-start, z 57 − 1.5i → 9. **[obs]** stage 8 starts on the death frame (f31923 → tick 4 at f31938, slow-mo 60 → 0 first); → 9 at tick ≈ 300 (f32267), Louie at (−40, 57) |
| 9 | locked | dialogue iv4_3 at 30 (not on replay); 60 → 10 |
| 10 | free | town portal rises. **[obs f34070]** `lock` −1 at tick ≈ 15; `gate` 3 → 0. The "portal" is a red door (object kind 6) at (−45, 65), solid (the player stops 1.44 from its centre); the prompt "Return to town now?" (`portal` > 0) comes only from its −z side (f34470 at (−45.04, 64.1)); from +z nothing, `A` just swings |

## 2. Stats

- Table **[obs]** (live record `0x5c23f0 + 27*0x68 + 0x2c`): **HP 100, EXP 25, AT 26, DF 30, MA 10, MD 30** (the card's earlier
  150 / 20 / 20 / 10 were wrong). Body radius 1.2.
- d2 f29: level 15 (`+0xae0`) → **HP 400 [obs]**, AT·mul 65 → every one of her records hits `ftol(32.5 − DEF/4)` = 13, +0..2
  at DEF 75 **[obs]** (132: 89 → 76 f24133; 104: 83 → 70 f28531; arrow: 59 → 45 f31420). Rush f37: level 19 → HP 480, AT·mul 75 [code].
- Player's sword on her: `ftol(ATK/2 − 30/4)` = 19 at ATK 54 **[obs]** (23 = 19·1.2 +0..4 in state 1, f31549).
- Blocks ("platforms", collision movers, model slot 13): **HP 2 each** (`DAT_0438c218[m]`), 31 / 32 / 31 per phase.
- Perched kobold (type 16): table HP 20, EXP 9, AT 17, DF 4 → HP 80 at level 15 **[obs]**. Block spawns (types 2 / 15): HP 60 **[obs]**.

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
adjacent blocks probably form a wall — **unverified**. **[obs]** walking +z into block 26 (z −40.2) the player stops at
pz −43.13 (2.93 from the centre). A swing's hit lands on the block at swing tick 8..9 (HP 2 → 1 f24043, 1 → 0 f24085); a crit swing
(record 0x54) damages it too. `0x438c3a8 + 4*i` read 0 on the hit (the freshness slot is elsewhere / off by one — not traced).
Spawns seen: **4 enemies out of 20 destroyed blocks** (type 15 ×3: f25586, f30425, f31130; type 2 ×1: f28516), each at the block's
x / z, level 15. **Her own block rolls too** (f31130: block 2 of phase 2 → a pumpkin, stage 6 five frames later). No chest item in the
11 phase-2 blocks (pickup table watched); phases 0 / 1 not watched.

## 3. Tielle's AI — sub-mode `s32(B+0xae4)`, state `s32(B+0xa54)`, tick `s32(B+0xa58)`, block `s32(B+0xa64)`

### Sub-mode 0 (scripted; `+0xb08 = 1`: unhittable, `+0xb04 = 1`, `+0x9fc = 1`)

- **Stages 3 / 4.** z < zbA and `+0xa64 == −1`: walks +z (accel 0.06, steers to x −41..−39). Else stands (vz ×0.9,
  yaw π). Phase ≥ 1: alpha 0 once z > zbA − 2; on arrival `+0xa64 = rand % 11` and **she is moved to that block's
  x / z** (one of the first 11 blocks: rows 0 / 1, in phase 2 also (−28.8, 67.9)); alpha 0 while the block's y is 0.
  Stage 4 and tick > 60 → stage 5.
- **Stage 5**, cycle `c = +0xa58 % 150`: anim 4 at 10, anim 0 at 50; 1 rand at c == 26 (voice).
  **[obs]** `+0xa58` is not reset per stage / phase (416 at the first hit, 3308 entering phase 2) and `+0xa54` stays 8 through
  sub-mode 0. Phase 0 she stops at z −25.91 (x −40).

| phase | volley | "hit" → stage 6 |
|---|---|---|
| 0 | record 132 at c = 26..30 (5) **[obs f24074: 5 records, ages 18..14 at tick 343]** | player within **2.5** of her **[obs f24894 at 2.43]** |
| 1 | record 132 at c = 26..35 (10) **[obs f25586: 10 records]** | her block gone (`s32(0x438c154 + 4*s32(B+0xa64)) == 0`) **[obs f26683]** |
| 2 | record 104 at c = 26, 34, 42, 50 (4) **[obs f27756: ages 30 / 22 / 14 / 6 at c 56]** | her block gone **[obs f31135]** |

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
5 above it (r = 0 → none this time). **[obs]** the counter resets on its 241st tick with the new kobold (f27949, f28190, f28431,
f28680, f28959), **does not run while the cap is reached** (reads 0 for 1,300 ticks with 4 perched), restarts when one falls; the kobold
drops from y ≈ 5 to rest at **y 3.6754** on the block within 8 ticks; caps 2 / 3 / 4 seen in phases 0 / 1 / 2. Slots are the first free
ones (7, 8, 9, 11 … in phase 2), not 1..4. Perched kobold: `+0xb54 = 1`; rest 80..143 ticks facing the player, then a
91-tick shot (record 150 at tick 26, SE 691); once its y < 2 (block gone) it becomes an ordinary kobold
(`+0xae4 = 0`). **[obs f28516 → f28525]** block 29 off, its kobold at y 1.08 one frame later and `+0xae4` 0 nine frames after the
block; it attacks as a walker 32 frames later. Kobolds stay through the phase-2 reveal into the fight (4 perched + walkers at f31279).

### Records

| id | what | hit |
|---|---|---|
| 132 | lobbed from 3 above her at the player's position (horizontal speed 0.01·dist + 0..0.04, dist clamped 5..30; vy 0.4, gravity 0.01 → **86..87 ticks of flight [obs]**: launched tick 326..330, hit f24133, last record age 87 at y 0.4), ± 0.15 rad | reach −0.15 → within 1.85; physical; dies on landing |
| 104 | rain: from 20 up toward a random point 4..12 from the player. **[obs]** spawns at y 20 within ~3 of Tielle's x / z, sits there until age ≈ 39, then falls 2 / tick along its velocity (ground at age ≈ 49..50), goes on below the floor until freed (age 85 at y −72) | reach 0.2 → within 2.2; physical. **[obs]** a standing player is never hit (160 ticks, f27756..27906); the hit at f28531 was at record age ≈ 50 on a moving player |
| 78 / 79 | arrows from 0.8 ahead, 1.4 up, 0.5/tick along the facing octant | reach 0.5 → within 2.5; physical |
| 150 | kobold's lob (as 132, ± 0.35 rad, faster) **[obs f28565: vy 0.25 at age 15; one at age 101 still alive at y 2.15 with vy +0.185 — it bounces]** | physical; 2 at DEF 75 (hp 61 → 59 f31301, source not isolated from the pumpkins) |

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
- Stage 0 / 3: wait (`--until "tst == 5"`). First visit: two dialogues (`rd tap R A --until "dlg == 0"` twice, ≈ 620 + 1,300 ticks).
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
  (**[obs]** ATK 54 → 19, DF is 30; spin 38; back spin 66). 400 HP: 6 spins + 3 sword hits (66 23 49 55 24 65 58 22 38) killed her in 597 frames (f31326..31923),
hp 59 → 39, by `rd search` (goal `a0use == 2`, score `-a0hp - 0.5*a0d`, cell `(int(a0hp/12), a0st, a0t//20, dx/1.5, dz/1.5, p_act)`,
aimed walk / swing / spin / away / strafes as actions, 68 s at `-j 2`).
- End: `tst == 10`, door at (−45, 0, 65): stand on its −z side (`pz < 65`), walk +z into it → `portal > 0`, `A` = Yes.
- What worked for the box phases (`rd search`, `-j 2`): goal `tst == 6`, score `-hypot(px − target) − k·blockHP`, cell
  `(int(px/1.5), int(pz/1.5), blockHP.., tt % 150 // 50)`, actions 12-tick walks + `"DIR A .*29"` swings: 94 s / 216 s / 58 s for
  phases 0 / 1 / 2. A goal on the outcome of a block (`a type-2 / 15 actor in use`, `a kobold with +0xae4 == 0`) is found the same way
  (60 s, 26 s). Heal in the pause menu during stage 5 (the event tick freezes).

## 6. Arms checklist

| arm | predicate | retail (`am-30f`) |
|---|---|---|
| first-visit dialogues / replay without | `dlg == 1` at stage 0 ticks 30 / 120; none in the rush | **[obs f21767]** tick 31 ("Huh? Hey..." … "How could she..."), **[obs f22517]** tick 120 ("*sniffle*... Sis..." … "She's coming!"); ticks 60..99 `lock` reads 4, Louie walks z −54 → −46.29. Replay without: **not reached** (first visit only) |
| blocks drop (each phase) | `tst == 3`, `s32(0x438c150)` = 31 / 32 / 31 | **[obs f23852 / f25182 / f26940]** 31 / 32 / 31, all three grids = the card's maps; stage 3 lasts 50 / 50 / ≈ 50 ticks; stage 4 → 5 at tick 61 |
| phase 0 volley (5 × 132) | `tst == 5 and tph == 0`, `s32(B+0xa58) % 150` in 26..30 | **[obs f24074]** 5 records, launched ticks 326..330 |
| phase 0 proximity hit | `tst == 6` with `tph == 0` | **[obs f24894]** at 2.43 from her, after blocks 26 / 18 / 12 (the x −40.2 lane) |
| blocks give way + sweep | block i off at tick 20 + 4i; others dying at 30 | **[obs f24914]** block 0 off by tick 21, block 30 still on at 131; the two kobolds `+0x428` 2 at tick 31, 0 by tick 36; phase 1: kobold + pumpkin the same (f26713); `lock` −1 at tick 121; she runs from tick 100 |
| Tielle runs off; player crosses | `tph` 0 → 1 → 2, `tst == 3` again | **[obs f25182]** pz 0.11 > 0; **[obs f26940]** pz 50.35 > 50.25 |
| she hides in a block (11 choices) | `tblk` in 0..10, `s32(B+0x3dc) == 0` | **[obs f25232]** `tblk` 2 → (−40.2, 25.25), alpha 0 from z 22.6; **[obs f27000]** `tblk` 2 → (−47.8, 75.5) |
| phase 1 volley (10 × 132) | `tph == 1`, cycle 26..35 | **[obs f25586]** 10 records, ages 26..17 at c 51 |
| block damaged / destroyed | block HP 2 → 1 → 0, `on` → 0 | **[obs f24043, f24085]** block 26; 20 blocks destroyed in the run |
| destroyed block spawns slime / pumpkin (1/4) | new slot type 2 or 15 at the block | **[obs f25586]** type 15 (phase 1 block 31), **[obs f28516]** type 2 (phase 2 block 29), **[obs f30425, f31130]** type 15; 4 of 20 |
| destroyed block rolls a chest item (≈ 3/32) | an item drop at the block: table C `0x6956cb0 + i*0x94`, state `+0x40 == 2` and type `+0x28 > 3` | **not reached** (0 in the 11 phase-2 blocks, watched; a 160 s search over block timings did not find one) |
| her block destroyed → reveal | `tst == 6`, alpha 255 | **[obs f26683]** phase 1 (hit from the side, x −43.1); **[obs f31135]** phase 2 |
| phase 2 volley (4 × 104) | `tph == 2`, cycle 26 / 34 / 42 / 50 | **[obs f27748..27756]** |
| phase 2 reveal → fight | `tsub == 1`, `tst == 7` | **[obs f31279]** stage-6 tick 145 (= 4·31 + 20 + 1), `+0xb08` 0, state 0, z 75.5 → 75.3 (clamp), blocks stay (20 on) |
| kobold summoned (cap phase + 2) | slot type 16 with `+0xae4 == 3`, y ≥ 2 | **[obs f24894]** 2 in phase 0, **[obs f26683]** 3 in phase 1, **[obs f27949 … f28959]** 4 in phase 2 |
| kobold roll r = 0 (no spawn) | `s32(B+0xa68)` resets with no new slot | **not reached** (every one of the 7 watched resets in phase 2 came with a kobold) |
| kobold shoots | its `+0xa54 == 1`, record at tick 26 | **[obs f28565]** record 150 in flight, kobolds in state 1 (f26683, f28525) |
| kobold falls (block gone) | its `+0xae4 == 0` | **[obs f28525]** (block f28516); again f30269, f30826; killed as a walker f28768 (back hit 41, then a crit swing that also killed the slime) |
| fight: 1 arrow / 3 arrows | `s32(B+0xa54) == 1`; record 78 / 79 in table B | **[obs f31314, f31823]** 1 × record 78 at state tick 12 (0.8 ahead, y 1.41, 0.5 / tick, reach 0.5); **[obs f31528, f31636]** 3 × record 79. State 1 follows 20 ticks of state 0 when lined up (f31496 → f31516), lasts 80 |
| arrows / volleys hit the player | `hp` drops | **[obs f24133]** 132: 89 → 76; **[obs f28531]** 104: 83 → 70; **[obs f31420]** arrow at point-blank: 59 → 45 |
| player z clamp | `pz` pinned at zbT + 1.8 / zbT − 32.2 in stage 5 | **[obs f24211]** south: pinned at **−57.029** in phase 0 (not −57.2); north: **not reached** (she stands in the way in phase 0; not tried in phases 1 / 2) |
| player dies | `hp <= 0` | **not reached** (on purpose: the run ends alive, hp 39) |
| death, remaining blocks burst, ending | `s32(B+0x428) == 2`; `tst` 8 → 9 → 10; bank+0x2bd12 = 1 | **[obs f31923]** HP 0, `+0x428` 2, the 4 perched kobolds (alive on the death frame) gone by f32297 (not timed); slow-mo 60 → 0, `p_inv` 1200; bank+0x2bd12 and +0x2bcbb = 1 at stage 8; 20 blocks → 0 by death tick ≈ 121; **[obs f32267]** stage 9, **[obs f32297]** dialogue ("Aaahhhh..." … the chest … "It can not be...", 1,743 ticks of taps); **[obs f34070]** stage 10; she stays (`+0x428` 2) at (−43, 55.5), alpha → −1 by f34280 |
| floor's end | `portal > 0` at the door, Yes | **[obs f34470]** "Return to town now?"; **[obs f34620]** `scene` 9: the bag list (11/20); `B` → the shop (`stage` 0, f34701) and a story event (`dlg` 1, ≈ 4,000 ticks of taps: Tielle wakes in the shop), free at f38708, `stime` 2 |
| revisit after the win | no type 27, `tst == 10` | **not reached** (the run ends in the shop; start a probe from `save:fc2e4d81f26d`) |

## 7. Port status

- **Verified 2026-11-11** on `amber-26f-30f-tielle` (39,184 played frames: 26F..29F, the three box phases, kobolds, block
  breaks + spawns, the fight, death, stages 8 / 9 / 10, the door, the shop event, a Save): `rhost triage` identical ticks
  1..39484, `portsave` IDENTICAL; gate rows `portrng` + `portsave`. RNG stream + end save, not pixels (render unjudged).
- AI (type 27), timeline FUN_0044db3c, FUN_0042439e, death arm, perched kobolds, records 132 / 150 / 78 / 79, block damage
  (wall probe on `g_mapcol_movers.on[50..]` / `.slot[50..]` → FUN_0042353c = `mover_destroyed`, `src/record_hit.c`): ported
  ahead of the recording, no divergence on it.
- **One bug, f27776 — record 104 (phase 2's rain):** `body_0x68` (`src/scene1_records_b_tick.c`) returned when OWNER_A was
  NULL. Retail (sub_43AE20 `v25 == 104`) has no owner test: the NPC-side record (FUN_00445a8c(a, 104, 1): OWNER_A 0,
  FLAG_A 1 → DRAG 0.2, offset 20) passes the NULL to FUN_004147d5. The port's record never ran: no template 5 at age 1,
  no pos −= vel hold until age 40, no template 0 at age 50 (retail's 177 draws at f27776 = the first record, spawned
  c 26), no state machine at ages 20..79, no kill at 95.
- Not reached by the movie (so unverified): the replay without dialogues / the Boss Rush arm (FUN_0044db3c(1)), the chest
  roll of a destroyed block, the kobold roll r = 0, the player's death, the north z clamp, the revisit after the win.
