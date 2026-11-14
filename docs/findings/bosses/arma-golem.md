# Arma Golem — types 73 body, 74 core, 75 right hand, 76 left hand (アルマゴーレム / コア / 右手 / 左手)

**code-read**: IDA `sub_4205E5` (body + core AI, 0x4205e5), `sub_420BD0` (hands, 0x420bd0), `sub_44F037` (state-8
descent), `sub_420591` (HP gate, called by `sub_42E791`), `sub_42F7AD` cases 73 / 74, `sub_44ECC4` + `sub_44F13D`
(Lapis floor 99 setup / timeline), `sub_450EE0` case 58, `sub_445A8C` cases 152 / 164, `sub_43AE20` (record 136,
IDA 40204..40232), table 0x52b874. **Port: ported, unverified** — `actor_ai_golem_body` / `actor_ai_golem_hand` /
`actor_golem_descend` (`src/actor_ai.c`), the 73 / 74 death arms, the floor-99 props, `d4_floor99_setup` /
`d4_floor99_event` / `d4_floor99_ambience` (`src/dungeon_flow.c`); the HP gate was already in `actor_damage`; no
recording reaches it yet (`lapis-91f-100f` pending).
Shared rules: `boss-floor-common.md`. Nothing run.

## 1. Where

| place | `dungeon` / `floor` | level → body HP / hand HP |
|---|---|---|
| Lapis Ruins 100F | 4 / 99 | 44 (×9.8) → **9.8** / 1470 |
| dungeon 5 floor 58 | 5 / 58 | 97 (×20.4) → 20.4 / 3060 |
| Lapis's Boss Rush, 13th (last) floor | 5 / 58, `0x438b4d0 == 4` | 54 (×11.8) → 11.8 / 1770 |

### Lapis floor 99 (`FUN_0044ecc4`, `FUN_0044f13d`; stage `s32(0x438b8a0)`, tick `s32(0x438b8a4)`)

Setup: 6 movers (`DAT_0438c150 = 6`, all at (−40, 0, −40)); then by save bytes (mode 2 = all bytes ignored):

| bank+0x2bd00 | bank+0x2bcff | start |
|---|---|---|
| 1 | — | stage 24: a return portal at (−40, 0, −45), nothing else |
| 0 | 0 | stage 0: party at (−40 + k, 0, −73 − k); props Alouette 30 at (−38, 0, −46), Arma 41 at (−42, 0, −46); 400 particles |
| 0 | 1 | **stage 4, tick 130** (the golem spawns on the first event tick); prop Alouette only |

| stage | what |
|---|---|
| 0 | free walk in a corridor (x clamped −41..−39, z ≥ −78 while stage < 10); `pz > −67` → event 12/8 → 1 |
| 1 | camera drift; tick 120: event 12/9; tick 200 → 2 |
| 2 | `lock = 4` (scripted walk); `pz > −55` → event 12/13 → 3 |
| 3 | particle swirl ticks 31..119; tick 200: event 12/14 → 4 |
| 4 | tick 10: fade out. **tick 130: every type-41 actor freed; `0x9643128 = 1`; spawn 73 at (−40, 15, −40) (`+0x428 = 1`, `+0xa54 = 8`), 74 at the same point, 75 at (−32, 15, −40) (`+0xa54 = 8`), 76 at (−48, 15, −40) (`+0xa54 = 8`)**; party at (−40 + 0.9k, 0, −50 − k); fade in when 0x2bcff == 0. 0x2bcff == 0: tick 350 event 12/15, 0x2bcff = 1 → 8. Else tick 131 → 8 |
| 8 | tick 30: banner. tick 240: adventurer's line. **tick ≥ 240: `+0xa54 = 0` on body, core, both hands; camera class 3 → stage 10** |
| 10 | **the fight** (`lock = −1`). tick > 60 and body `+0x428 == 2` → 13 |
| 13 | tick 120 fade out; tick 240: body to (−40, 0, −50), hands freed, party re-placed, fade in; tick 360 → 20 |
| 20..23 | events 12/16 (20, tick 30), 12/17 (21, tick 60), body + core freed (22, tick 320), Prime 77 spawned (22, tick 440), 12/18 (22, tick 710), Arma prop (23, tick 200), 12/19 (23, tick 250); **23 tick 300: bank+0x2bd00 = 1 (first time also 0x2bd02 = 1), `deepest[4]`, best time, floor end kind 3** |

During state 8 on d4 (`FUN_0044f037`): `+0xb04 = +0xb08 = 1`, `+0xa98 = 1`, y −= 0.1/tick down to 1.0 (140 ticks from 15).

### Dungeon 5 floor 58 (`FUN_00450ee0` case 58)

5 movers; 73 at (−40, 1, −40) (`+0x428 = 1`, state 8), 74 same point (state 0), 75 at (−32, 1, −40), 76 at (−48, 1,
−40) (state 8); portal at (−40, 0, −47); player start (−40, 0, −60), camera class 3. Event `FUN_0044d0a4(1)`. State 8
= 240 state ticks, no flags written during it (the parts are hittable while waiting; the body's HP gate still
applies).

## 2. Stats and parts

| part | table HP / AT / DF | volume (radius 4.5·1.1, half-height 3.0·1.1) | flags each AI tick |
|---|---|---|---|
| 73 body | **1** / 20 / 30, EXP 0 | 4.95 / 3.3 | `+0xa00 = 1` (floats), `+0xb54 = 1`, `+0x9fc = 1`, `+0xb04 = 1`; `+0xb08 = 0` except during a break |
| 74 core | 1 / **0** / 30 | — | copies the first live 73's position and yaw; `+0xb04 = +0xb08 = 1` → **never hittable** |
| 75 / 76 hands | 150 / 20 / 30 | 4.95 / 3.3 | `+0xa00 = 1`, `+0xb54 = 1`, `+0x9fc = 1`, y fixed 1.5; hittable (`+0xb08` never set after state 8) |

**The body's HP gate** (`sub_420591`, on every damage application): segment `s = s32(B+0xa64)` (0..5),
`floor = max − max/6·(s + 1)`. If HP < floor: HP = floor, `+0xa58 = 0`, **`+0xa60 = 1` (a break starts)**. So the
body dies on the 6th segment: 5 breaks, then HP can reach 0. On Lapis 100F a segment is 1.63 HP; every landed hit
does ≥ 1.

Killing a hand: default death arm (freed at death tick 2). The body does not need the hands dead (d4: the
timeline frees them; d5: the body's death tick 60 kills every live actor).

## 3. Body state machine — `s32(B+0xa54)`, tick `s32(B+0xa58)` (++ at the top of every active tick)

Motion (all active states): goal (gx, gz), height h; Δ = goal − pos, clamped to length 5;
`v = 0.97·v + 0.0097·Δ`; y moves ±0.05/tick toward h.

| state | length | goal / h | what |
|---|---|---|---|
| 8 | d5: 240; d4: until event stage 8 tick 240 | — | descent (d4) / wait (d5) |
| 0 hover | 300 | (player x, max(player z + 7, −57)), h = 1 + sin(0.05·t) | anim 0. **t == 120: record 152** (5-shot fan), SE 330. t == 300: t = 0, **state = 1 + (rand & 1)** |
| 1 lob | 120 | (−40, −30), h = 5 | **t == 80: record 164**, SE 385. t == 120 → 0 |
| 2 rain | 320 | (−40, −40), h = 8 | **t = 80, 100, …, 220 (k = 0..7): record 70** at centre + r·(sin, cos)(2π·rnd), r = 4·(rnd + 1) + k, y 0 (two `FUN_00471089` each). t == 320 → 0 |
| **break** (`+0xa60 > 0`, replaces the switch) | 330 | (−40, −25), h = 8 | `+0xb08 = 1`. t 30..209: mover `s` y = 0.2·(rand & 3) (shakes; one `rand` per tick); t ≥ 210: mover y −= 0.5/tick. t == 30: effect at (−0.2·X_s − 40, 0, 0.2·Z_s − 40), SE 684. t == 150: SE 369. **t ≥ 330: state 0, t 0, `+0xa60 = 0`, mover `s` off (`s32(0x438c154 + 4s) = 0`), `+0xa64 = s + 1`** |

Mover table (X, Z) at 0x52b874: (58, −76), (115, 24), (−54, −10), (−54, 55), (0, 24), (−50, −54) → effect points
(−51.6, −55.2), (−63, −35.2), (−29.2, −42), (−29.2, −29), (−40, −35.2), (−30, −50.8). Mover y = `f32(0x438c53c + 4s)`.

## 4. Hands — `s32(H+0xa54)` (8 or 0), tick `s32(H+0xa58)`, clock `s32(H+0xa60)` (++), cycle `s32(H+0xa64)`

Active only while `s32(H+0xa5c) == 0` (no writer found: always). Per tick:

- Goal x = `4·sin(0.02·clock) − 20` (75) / `−60 − 4·sin(0.02·clock)` (76); goal z = `4·cos(0.03·clock) − 40`;
  `v += 0.1·Δ`, |v| capped 0.5. The hands patrol the arena's two sides (x ≈ −20 and x ≈ −60).
- t ≤ 149: yaw → ∓4.084 at 0.0314/tick: cycle % 8 in 0..4 → 75: −4.084, 76: +4.084; cycle % 8 in 5..7 → swapped.
- **t == 120: record 136** (beam) when: 75 and cycle % 5 ∈ {0, 2, 4}; 76 and cycle % 5 ∈ {1, 3, 4}. So cycles 0, 2 right
  only; 1, 3 left only; 4 both; period 5.
- t ≥ 180: yaw += 0.02·ramp (75) / −= (76), ramp = min(1, 0.1·(t − 180)) − 0.1·max(0, t − 255), floored at 0 →
  ≈ 1.5 rad of sweep over t 180..265.
- t == 300: t = 0, cycle++.

## 5. Attacks

| attack | record | notes |
|---|---|---|
| body fan | 152 ×5: octant direction, ∓0.38, ∓0.56 rad, speed 0.3, from the body (y + 0.25) | "magic" list → 1 damage |
| body lob | 164 ×1: aimed at the player ± 0.1 rad, **then forced to |angle| ≥ 2.513** (within 36° of −z), speed 0.15, vy 0.35, mult 1.0 | physical (not in the magic list) |
| rain | 70 ×8, spreading outward (r 4..8 + k) | "magic" list → 1 damage; in-flight behaviour not read |
| hand beam | 136, owner-attached: origin = the hand's joint 1 (`f32(H+0x708..)`), direction (sin, cos)(yaw + 0.3) for 75 / (yaw − 0.3) for 76, radius field 3.0; removed when the hand's `+0x428 != 1` | not in the magic list → physical AT 20·mul; lifetime not read. The t ≥ 180 yaw sweep moves it |

## 6. When it can be damaged

- Body: `s32(B+0xb08) == 0` = active states 0 / 1 / 2 (not state 8 on d4, not a break). Vertical reach: the body
  centre is at y 0..2 in state 0, 5 in state 1, 8 in state 2 — with half-height 3.3 (+0.8·reach) a ground swing
  reaches it in state 0; states 1 / 2: probably not (depends on the swing record's y and reach: combat-core).
- Hands: always after state 8; killing them removes their beams.
- Core: never.

## 7. Kill recipe

1. Names: B = type 73, C = 74, HR = 75, HL = 76 (d4: allocated at event stage 4 tick 130; scan
   `s32(0x76b970 + i·0xba4 + 0x424)`).
2. Start of the fight: d4 `s32(0x438b8a0) == 10`; d5 `s32(B+0xa54) == 0`.
3. Body hover (state 0) parks itself 7 units "behind" the player on z (toward +z, capped at −57) at the player's x
   and bobs to the floor: wait for `f32(B+0x3f4) < 1.5`, hit once. A hit that crosses the segment floor starts a
   break (`s32(B+0xa60) == 1`): 330 ticks untouchable, the body leaves to (−40, −25).
4. Cell: `(s32(B+0xa64), s32(B+0xa60), s32(B+0xa54), s32(B+0xa58)//30, int(px − f32(B+0x3f0)), int(pz − f32(B+0x3f8)))`;
   score `s32(B+0xa64)*100 - f32(B+0x42c)`; goal `s32(B+0x428) == 2`; avoid `hp <= 0`.
5. Six scoring moments: breaks at segment 0..4, then the kill. Between them: body states 1 (lob from (−40, −30)) and
   2 (rain) are waiting time; dodge the beams (hand cycle % 5: 0, 2 right; 1, 3 left; 4 both), or kill the hands
   first (1470 HP each on d4 — far more than the body's whole fight).
6. Done: `s32(B+0x428) == 2`. d4 → event stage 13 after tick 60; d5 → portals when the table is empty.

## 8. Arms checklist

| branch | predicate |
|---|---|
| d4 descent / d5 wait | `s32(B+0xa54) == 8`; d4: `f32(B+0x3f4)` 15 → 1, `s32(B+0xb08) == 1` |
| hover + fan | `s32(B+0xa54) == 0`, record 152 at t 120 |
| lob (rand & 1 == 0) | `s32(B+0xa54) == 1` |
| rain (rand & 1 == 1) | `s32(B+0xa54) == 2`, 8 records 70 |
| HP gate clamp | `f32(B+0x42c) == max − max/6·(s+1)` exactly, `s32(B+0xa60) == 1` |
| break s = 0..4 | `s32(B+0xa64)` 0 → 5; mover off: `s32(0x438c154 + 4s) == 0`; mover y `f32(0x438c53c + 4s)` sinking |
| kill on the 6th segment | `s32(B+0xa64) == 5 and s32(B+0x428) == 2` |
| core follows | `f32(C+0x3f0) == f32(B+0x3f0)` every tick; `s32(C+0xb08) == 1` |
| right beam / left beam / both | record 136 owned by HR at cycle % 5 ∈ {0, 2, 4}; by HL at {1, 3, 4} |
| hand yaw swap | `s32(H+0xa64) % 8` in 5..7 |
| hand killed | `s32(H+0x428) == 0` with the body alive; no more record 136 from it |
| death d4 (stays dying; anim 12 → 13) / d5 (alpha −4 from tick 60) | `s32(B+0x428) == 2`, `s32(B+0xaf0)` |
| 100F intro long / short | bank+0x2bcff 0 → stages 0..4 with events; 1 → stage 4 tick 130 at load |
| 100F already cleared | bank+0x2bd00 = 1 → stage 24, no type 73 |

## 9. Unresolved

- Records 136 / 152 / 164 / 70 in flight (lifetime, hit volume); 136's damage class.
- Which body states a ground swing reaches (vertical test numbers from combat-core).
- Mover 5 on d4 (`f32(0x438c550) = −5` every event tick: hidden) vs segment 5: the 6th segment never breaks a
  mover (death comes first); d5 has only 5 movers.
- `+0x9fc`, `+0xb04`: meaning not read.
- The hands' `+0xa5c` gate: no writer found.
- Whether a hit landing during d5's 240-tick wait (state 8, no `+0xb08`) can start a break before the fight.
