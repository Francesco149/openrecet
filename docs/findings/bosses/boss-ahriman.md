# Boss ahriman — type 54 (FUN_00426811)

**code-read** from the port `src/actor_ai.c:2036..2161` (= FUN_00426811, IDA `sub_426811` all_ida.c:24468 — **not
re-read line by line here**; FRONT lists the port RNG-verified on dungeon3-floors / -deep, the port's "unverified"
header comment is stale); beam record body 0x89 (port scene1_records_b_tick.c:709); death arm read in IDA
(`sub_42F7AD` case 54, matches). Shared rules: `boss-floor-common.md`.

## 1. Where

| place | `dungeon` / `floor` | count |
|---|---|---|
| dungeon 2 5F | 2 / 4 | 1 |
| Boss Rush (dungeon 2's), 1st floor | 5 / 33, `0x438b4d0 == 2` | 1 |
| dungeon 3 15F (list `f:15` ×2), dungeon 5 floors 40 (×2), 48 (×3) | | 2 / 3 |

- Generic boss floor: setup FUN_0044cc80, event FUN_0044d0a4. Placed at (−40, **15**, −34), state 8.
- Intro: pad locked 240 event ticks; the boss descends y = 16 − t/16 over its 240 state ticks.
- End: no actor left (minions die with it at death tick 60; it frees at 120) → portals, `s32(0x438b8a0) == 1`.

## 2. Stats

- Table: HP 15, AT 20, DF 20, MA 0, MD 10. No parts. `+0xab8` 1.0. `+0xb54 = 1` (no knock-back / stagger).
- d2 f4: level 7 → **HP 36**, AT·mul 34. Rush f33: level 12 → HP 51.
- Flies: `+0x9f4 = 1`, hovers 1.5 ± 0.5 (up to +1) over the ground. Body radius = half-height = 2.0·0.9 = 1.8
  (.data row 54: rf[0] 0.9, rf[2] 2.0, rf[3] 2.0).
  Vertical test: `|hit-centre y − record y| − 0.8·reach < 1.8`. With the body 1.5..3.5 up a ground swing should
  reach; **not established by reading** (the swing record's y belongs to combat-core).

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`; cycle clock `s32(B+0xa5c)`

| state | length | what |
|---|---|---|
| 8 descent | 240 | y = 16 − t/16. No minions, no movement. → 0 |
| 0 circle | until the cycle fires | anim 0; turns toward the player 0.1/tick (FUN_0041f789) |
| 1 beam | 300 | anim 3; yaw frozen. Tick 20: record 137 + SE 698. Tick 100: velocity −0.5 along the facing octant (recoil). Velocity ×0.95 each tick. → 0 |

Every tick in states 0 / 1:
- **Cycle** `c = +0xa5c % 400` (then ++): `c == 1` → hold `+0xa60 = rand & 63`; `c == 300 + hold` → state 1.
  First beam 300..363 ticks after the descent; a beam ends 300 later; next one 37..163 ticks after that.
- **Movement:** target = player − 8·dir(angle-to-player + 0.2π·sin(phase)); accel 0.005/tick toward it, velocity
  ×0.96 → top speed ≈ 0.125/tick (player walk 0.175). It keeps ≈ 8 from the player, swinging ±36°.
  phase = ((`+0xa50` & 7)·50 + c)·2π/400.
- **Minions:** bats of the first `kinds` of types 6, 7, 8, 9 while fewer than `cap` (in use, any of 6..9) live;
  one every 30 ticks (`+0xa64`), `+0xa68` counts them: > 2·cap → 90-tick gap, > 4·cap → 150, **none once
  `+0xa68 >= 5·cap`**. Point: 2..10 from the centre, > 10 from the player (≤ 30 tries, 2 unit draws each), y 10;
  1 rand (`% kinds`) for the type.

| where | kinds | cap | total |
|---|---|---|---|
| dungeon 2; dungeon 5 floor < 38 | 2 (6, 7) | 4 | 19 |
| dungeon 3; d5 floors 38..44 | 3 | 6 | 29 |
| dungeon 4; d5 floor ≥ 45 | 4 | 8 | 39 |
| other | 2 | 3 | 14 |

Beam (record 137): anchored 2 ahead of the boss along its **facing octant** (+0x18, 8 directions), 2 up, follows the
boss. Record ages 11..: charge particles; age 80: SE 705; **ages 80..159** (state ticks 100..179): 20 probes a tick,
1 unit apart, reach 2.5 → a strip 20 long, ± 4.5 wide to the player. "Magic" → 1–2 damage, knock-down 30.
Dies with the owner (`+0x428 != 1`) or at age 175.

Death: tick 1 slow-mo, alpha 0, burst; player invulnerable 1200 only if no other type-54 lives (else 60); tick 60: if
it was the last 54, every live actor starts dying; frees at 120.

## 4. When it can be damaged

- Always (`+0xb08` never set), from the moment the pad frees. State 1 gives ×1.2; back ×1.5.
- Safe side: anywhere but the beam strip in state 1 ticks 100..179. The beam direction is fixed from tick 0 of
  state 1 (yaw frozen) — stand behind / beside it.

## 5. Kill recipe (search)

- Names: `ast = s32(B+0xa54)`, `at = s32(B+0xa58)`, `ahp = f32(B+0x42c)`, `ay = f32(B+0x3f4)`.
- It retreats to 8 from the player at 0.125/tick: chase it (0.175/tick) into a wall / corner, or hit it in state 1
  (it only drifts on its recoil).
- Cell `(int(ahp/4), ast, at//20, int((px − ax)/1.5), int((pz − az)/1.5), int(ay))`; score `-ahp`;
  avoid `hp < 30`.
- Per hit `ftol(ATK/2 − 5)` (ATK 54 → 22). **36 HP = 2 hits** (1 with a back + state bonus roll ≥ 36).
- If no directed attack ever connects, suspect the vertical reach (§2) first: read `ay` against the swing's record
  y; a jump attack / skill may be needed.

## 6. Arms checklist

| arm | predicate |
|---|---|
| descent | `ast == 8`, `ay` from 16 to 1 |
| circle + turn | `ast == 0` |
| beam charge / fire / recoil | `ast == 1`; `at == 20` (record), `at == 100` (recoil), `at` 100..179 live |
| beam hits the player | `hp` drops by 1–2 with `ast == 1 and 100 <= at < 180` |
| minion spawn, type 6 and type 7 | new slot `s32(+0x424) in (6, 7)`, `s32(+0xb2c) == 1` |
| minion cap (4) | count of 6..9 holds at 4 |
| slowdowns at 9th / 17th | `s32(B+0xa68) > 8` → `s32(B+0xa64) == −60`; `> 16` → −120 |
| minions exhausted | `s32(B+0xa68) >= 20`, no more spawns |
| death (last of kind sweeps the floor) | `s32(B+0x428) == 2`, at `s32(B+0xaf0) == 60` all others `+0x428 == 2` |
| death with a second 54 alive (×2 / ×3 floors) | `s32(0x56db0f4) == 60` at tick 1 |
| player dies | `hp <= 0` |

Port: fully ported, no PORT-DEBT. Death arm matches IDA.
