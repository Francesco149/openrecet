# Mushroom king — types 24 / 59 / 60 (FUN_0042b6b7)

**code-read** throughout, from the port `src/actor_ai.c:2609..2955` (= FUN_0042b6b7, IDA `sub_42B6B7`
all_ida.c:27804 — spot-checked only: the `DAT_056db108` test, the record-37 call; the port is RNG-verified on the
dungeon1 recordings); death arm read in IDA (`sub_42F7AD` case 24/59/60, matches). Shared rules:
`boss-floor-common.md`. enemy.txt names: 24 ねずみバール ("the rat boss"), 59 ねずみハリセン, 60 ねずみマグロ.

## 1. Where

| type | place | `dungeon` / `floor` |
|---|---|---|
| 24 | dungeon 1 10F | 1 / 9 |
| 24 | Boss Rush (dungeon 1's), 2nd floor | 5 / 31 (`0x438b4d0 == 1`) |
| 59 | dungeon 5 floor 47 (list `f:48`) | 5 / 47 |
| 60 | dungeon 5 floor 55 (list `f:56`) | 5 / 55 |

- Setup FUN_0044d1e4 → FUN_0044cc80(1) (d1) / FUN_00450ee0 → FUN_0044cc80(2) (d5); event FUN_0044d0a4.
- Placed at (−40, 0, −34), state 8. Intro: pad locked event ticks 0..239; the king's own state 8 runs 240 AI ticks.
- End: king's slot frees at death tick 500 → no actor left → portals rise, `s32(0x438b8a0) == 1`.

## 2. Stats

| | table HP / AT / DF | level | HP | AT·mul | DF_eff (`+0xab8`²) |
|---|---|---|---|---|---|
| 24, d1 f9 | 150 / 20 / 30 | 6 | 330 | 32 | ftol(30·1.5²) = 68 |
| 24, rush f31 | same | 7 | 360 | 34 | 68 |
| 59 | 150 / 20 / 30 | by floor | | | 30·2² = 120 |
| 60 | 150 / 20 / 30 | by floor | | | 30·3² = 270 |

- No parts. Body radius 4.5·0.8 = 3.6.
- `+0xab8` is rewritten every tick: 1.5 / 2 / 3 by type; **state 4 sets 1.0 and `+0xb28 = 1` → DF 0**.
- `+0xb20 = 1` every tick: hits never stagger it or reset its state.

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`

Every tick outside state 8: `+0xa64` counts 1..180 (wraps); at 10, if fewer than 4 mushrooms (types 13 + 14, in use)
exist → one more, rising (`+0xae4 = 2`), at a point 2..12 from the centre. Draws: 2 unit (angle, radius), 1 rand
(type: 24 `&1`, 59 `%3`, 60 `&3`; 0 → purple 14, else 13), + FUN_0041f903's own 2.

| state | length | what |
|---|---|---|
| 8 intro | 240 | anim 13 (0 from 200); tick 45 SE 672 + wobble + burst 68. → 0 |
| 0 wander | ≥ 121 | anim 1. Tick 1: target 7..12 from the centre (2 unit draws). Accel 0.02/tick, turn π/50. `+0xa60` counts; past 60 a contact record 15 every tick (reach 1.5 at 0.5 ahead, ×0.2, AT/4 → 1–2). `+0xa5c` bit 0: within 8 of the target or tick > 240; bit 1: tick > 120. Both → pick (below). |
| 1 approach + swing | walk ≤ 121, then 60..141 | walk (`+0xa58 == 0`, `+0xa5c` counts): accel 0.02 / 0.022 / 0.025, turn π/50, re-aimed 3.5 short of the player for the first 60 ticks while > 5 away. Arrives < 4 from the target or `+0xa5c > 120`. Then 1 rand picks the swing (`+0xa5c`): 24/59: odd → 0 (one swing), even → 1 (two); 60: odd → 2 (three lunges), even → 1. |
| 2 eat | walk ≤ 179, then 120 | walks to mushroom `+0xa50` (stops 1.4 short), anim 14; tick 38: mushroom freed if still alive, `+0xa74` = 2 (type 13) / 1 (type 14), SE 337; tick 48: `+0xa70 = +0xa74`; tick 120: `+0xa70 ≠ 0` → state 4 (`+0xa74 == 1`, purple) or 5 (else); `+0xa70 == 0` → 0. |
| 3 spray | 209 | anim 13; turns to the player for 14 ticks; ticks 40, 48, …, 136 (13×): record 38 at yaw + 0.3π·sin(k·2.5π/13); SE 330 every 4th. → 0, `+0xa70 = 0`. |
| 4 power-up | 120 | anim 16, SE 360 at 20. **DF 0** (`+0xb28 = 1`, `+0xab8 = 1`). → 0 |
| 5 heal | 40 | anim 15; tick 11 ring 67 + shake; tick 40: HP += max/6 (capped), popup. → 0 |

Pick (state 0, each tick once both bits are set — a declined roll re-rolls next tick):
1. nearest mushroom with `+0x398 >= 600` and > 4 away exists **and** `rand & 1 == 0` → state 2;
2. else `rand % 20 == 0` → state 3;
3. else `rand & 3 == 0` → stay (new wander target next tick);
4. else state 1, target 1.5 short of the player, `+0xa60 = 0`.

Swings (state 1, `+0xa58` ≥ 1; record 14, owner 24/59/60 → reach 6.5, 8.5 once `+0xa58 >= 100`; centred on the
king; live 16 ticks; physical ×1.0):

| `+0xa5c` | records at tick | ends at |
|---|---|---|
| 0 | 25 | 75 (59: 60) |
| 1 | 25, 56 | 114 (59: 99) |
| 2 (type 60) | 25, 56 from 1 ahead; 100 from 2 ahead (reach 8.5) + ring 67 8 ahead, shake, wobble | 141 |

- **A swing hits everything within reach + 2 = 8.5 of the king's centre** (10.5 for the third lunge, from the stepped
  point), any direction, ticks 25..40 and 56..71 (100..115).
- Record 38 (spray): from 2.5 ahead, 4.8 up, speed 0.02..0.22 along the aim, bounces; reach 0.2..0.26; live 152 ticks;
  "magic" → 1–2.
- **Dead arm:** `DAT_056db108 > 100` (player bound) → state 0 jumps straight to state 1. Its only writer is record
  type 39 (FUN_0043865e), and no call in the IDA export spawns a 39 → unreachable as far as read.
- **Stale meal:** the walk in state 2 times out at `+0xa5c == 180` and falls into the `+0xa70` test with the previous
  meal's value → a repeat power-up / heal without eating (possible only after a first meal; spray clears it).

Death (`+0x428 == 2`, `+0xaf0`): anim 18; 1 slow-mo; 60 cinematic camera, body to (−40, 0, −40), records cleared,
every other live actor starts dying; 300.. fades 2/tick; **frees at 500**.

## 4. When it can be damaged

- Always (`+0xb08` never set). Never staggers.
- Best: **state 4** (DF 0, 120 ticks, stationary); then states 0 / 2 / 5 (no swing).
- States 1..5 give the ×1.2 state bonus; from behind ×1.5.
- Unsafe: state 1 with `+0xa58` in 25..40 or 56..71 (60: 100..115) inside 8.5 (10.5).

## 5. Kill recipe (search)

- Names: `kst = s32(B+0xa54)`, `kt = s32(B+0xa58)`, `khp = f32(B+0x42c)`,
  `kd = hypot(px − f32(B+0x3f0), pz − f32(B+0x3f8))`.
- Cell `(int(khp/8), kst, kt//12, int(kd/1.5), int(hp/10))`; score `-khp`; goal `khp == 0`;
  avoid `hp < 30`.
- Actions: the 8 directed attacks (`"DIR*2 A .*8"`) + 12-tick walks.
- Stand behind it (body radius 3.6). Leave past 8.5 when `kst == 1 and kt >= 15`; return at `kt > 41` (one swing)
  / `> 72`.
- Do not kill mushrooms while it is in state 2: a purple one eaten = the DF-0 window; a normal one = +55 HP (heal).
  Killing the normal (type 13) mushrooms and leaving purple (14) ones biases toward state 4.
- Hits: per hit `ftol(ATK/2 − 17)` (ATK 54 → 10; ×1.5 back, ×1.2 in states 1..5; state 4: 27 base).
  330 HP ≈ 15–20 back hits at ATK 54.

## 6. Arms checklist

| arm | predicate |
|---|---|
| intro | `kst == 8` then `kst == 0` at `kt` wrap |
| mushroom planted (13 / 14) | a new slot with `s32(+0x424) in (13, 14)` and `s32(+0xae4) == 2` |
| mushroom cap (4) holds | count of 13/14 stays ≤ 4 over a 180-tick cycle |
| wander, contact record | `kst == 0 and s32(B+0xa60) > 60` |
| pick → eat | `kst == 2` |
| eat: mushroom gone before tick 38 | `kst == 2`, `s32(B+0xa74) == 0` at `kt == 39` → `kst == 0` at 120 |
| eat normal → heal | `kst == 5`; HP rises by max/6 at `kt == 40` |
| eat purple → power-up | `kst == 4`, `s32(B+0xb28) == 1` |
| eat walk timeout (stale meal) | `kst == 2 and s32(B+0xa5c) >= 179 and kt == 0` |
| spray | `kst == 3` (5 % per pick) |
| stay roll | `kst == 0`, `kt` restarts at 1 with no state change |
| approach timeout | `kst == 1 and kt == 0 and s32(B+0xa5c) > 120` |
| one swing / two swings | `kst == 1 and kt > 0 and s32(B+0xa5c) == 0` / `== 1` |
| three lunges (type 60 only) | `s32(B+0xa5c) == 2` |
| swing hits the player | `hp` drops while `kst == 1` |
| player dies | `hp <= 0` |
| death, minions swept at 60 | `s32(B+0x428) == 2`, `s32(B+0xaf0) == 60` |
| floor opens | `s32(0x438b8a0) == 1` |

Port: all arms ported, no PORT-DEBT in this function. Types 59 / 60 share the code (the type-60 lunge arm has no
recording).
