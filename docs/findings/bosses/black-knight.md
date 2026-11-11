# Black Knight — type 49 (ブラックナイト)

**code-read**: IDA `sub_4279CF` (AI, 0x4279cf), `sub_423789` (walk-and-align helper; described in `arma.md` §3),
`sub_43865E` shield block (IDA 39173..39196), `sub_445A8C` case 81, `sub_42F7AD` case 23 / 49 / 55 / 54, table
0x52b978. **Port: AI stub; death arm case 49 not in the port's list.** Shared rules: `boss-floor-common.md`
(generic boss floor). Nothing run.

## 1. Where (HP each = 15·(1 + 0.2·lv))

| place | `dungeon` / `floor` | count | HP each | knights kept alive |
|---|---|---|---|---|
| Obsidian Tower 45F | 3 / 44 | 1 | lv 27 → **96** | 3 |
| Lapis Ruins 55F | 4 / 54 | 2 | lv 35 → **120** | 5 |
| dungeon 5 floors 44 / 54 | 5 / … | 1 / 2 | own: lv 73 / 90 → 234 / 285; rush (d3 f44) lv 34 → 117, (d4 f54) lv 50 → 165 | 3 |

Generic placement (−40, 0, −34) (2: x −45 / −35), state 8.

## 2. Stats

Table: HP 15, EXP 30, **AT 2**, DF 20, MA 0, MD 10, boss flag 0; rf0 1.2 → radius 2.4, half-height 2.4. Every AI tick
`+0xb54 = 1` (no stagger / knock-back / state reset by hits).

## 3. Shield — `s32(B+0xb9c)`

Written every AI tick: 1, except **0 in state 1 once `+0xa58 ≥ 20`** and **0 in state 2**.

Hit handler (`FUN_0043865e`): shield ≠ 0 and not frozen (`+0xb18 == 0`): `a = atan2(actor − record) − yaw + π`
(wrapped to ±π); **|a| < 1.0996 (63°) → blocked**: damage 0 (part flag: sparks 41 / 42, SE 359), anim 4,
**`+0xa54 = 4`, `+0xa58 = 0`**. Outside ±63° of the front the hit is normal (side ×1.2 beyond 45°, back ×1.5 beyond
135°).

## 4. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`

| state | length | what |
|---|---|---|
| 8 intro | 240 | t == 240 → 0. No knights are summoned during it |
| 0 hunt | — | anim 0; `FUN_00423789(a, 20.8, 20, 0.025, 5, 30, 0)`: walk legs (heading re-picked at t 1 with `rand`s, accelerate 0.025/tick from t 6, leg = 30 + (rand & 63) ticks); from t 20: aligned = on one of its axes (off-axis < 1.5) at 2.0..20.8 → yaw at the player; aligned and on the ground → **state 1**, t = 0, sub = 0, anim 3 |
| 1 throw | 91 | anim 3. **t == 25: record 81** (5 shots: octant direction, ∓0.18, ∓0.36 rad, speed 0.5, origin 0.3 ahead, y + 0.7). **Shield down from t 20.** t ≥ 91 → 0 |
| 2 burst | 101 | shield down but **`+0xb08 = 1` from t 1**; anim 5; effect 68. **t == 40: record 14 at the knight**, effects 66 / 67 / 69, SE 675. t == 101: `+0xb08 = 0` → 0 |
| 3 lunge | 61 | anim 3; t == 20: record 13; t 21..35: accelerate 0.08/tick along yaw; t ≥ 61 → 0. **No writer of state 3 found** (the AI never enters it; the block sets 4) |
| 4 blocked | 20 | set by the shield block. t == 20: t = 0; **player nearer than 5.0 and `rand % 3 == 0` → state 2**; else state 0, anim 1. (The `rand` is drawn only when the player is nearer than 5.) |

**Knights** (every AI tick, any state but 8): n = live actors of types 44..48 (0x52b978); `n < 3` (dungeon 4: `< 5`)
→ **one knight per tick**: up to 30 tries of {angle 2π·rnd, r = 2 + 8·rnd around (−40, −40); accept when > 10
from the player}, `FUN_0041f903(x, 10, z, 44 + rand % 5)`, `+0xb2c = 1`. Dead knights are replaced on the next tick,
without limit. With two black knights both run the test (same count), so still 5 in all. Knight AI
`FUN_00427f59` (ported; other author's ground).

## 5. Attacks

| attack | record | notes |
|---|---|---|
| throw | 81 ×5, speed 0.5 | physical, **AT 2**·mul → `ftol(2·mul/2 − DF/4)` ≤ 0 for any DF ≥ 4·mul → 1 damage (+0/1) |
| burst | 14 at the knight, 40 ticks after the block-counter starts | same AT 2 → 1 damage; knock-down 70 |
| knights 44..48 | their own records (AT 2 as well) | the crowd, not the numbers |

## 6. When it can be damaged

- From the side / back (|a| ≥ 63°): states 0, 1, 4 (and 8).
- From the front: only state 1 from t 20 (71 ticks).
- Never in state 2 (`+0xb08`).
- A frontal hit with the shield up costs nothing but sends it to state 4 and, at ≤ 5 range, a ⅓ chance of the
  untouchable 101-tick burst.

## 7. Kill recipe

1. Wait `s32(B+0xa54) != 8`.
2. Bait the throw: stand on one of its axes at 2..20.8 → `s32(B+0xa54) == 1`; its yaw is frozen at the octant toward
   the player. Step off the axis (the 5 shots cover ±0.36 rad), close in during t 0..25, and hit from anywhere
   once `s32(B+0xa58) >= 20` (shield down), or from behind at any time. HP 96 / 120.
3. Predicate "safe to swing": `s32(B+0xb08) == 0 and (s32(B+0xb9c) == 0 or behind)`, behind =
   `abs(wrap(atan2(f32(B+0x3f0) − px, f32(B+0x3f8) − pz) − f32(B+0x420) + pi)) >= 1.1` (argument order per
   `boss-floor-common.md`'s yaw convention; validate once on a known blocked hit: HP unchanged, state → 4).
4. Cell: `(s32(B+0xa54), s32(B+0xb9c), s32(B+0xa58)//10, int(f32(B+0x42c))//10, int(px − f32(B+0x3f0)), int(pz − f32(B+0x3f8)))`;
   score `-f32(B+0x42c)`; avoid `hp <= 0`.
5. Ignore the knights (they are replaced instantly); the boss's death tick 60 kills them all.
6. Done: `s32(B+0x428) == 2`, then no actor left → `s32(0x438b8a0) == 1`.

## 8. Arms checklist

| branch | predicate |
|---|---|
| intro, no knights | `s32(B+0xa54) == 8` and no actor of type 44..48 |
| knight upkeep 3 (d4: 5) | live 44..48 count returns to 3 / 5 one tick after a kill; all five colours seen |
| walk leg / align | `s32(B+0xa54) == 0`, `s32(B+0xa5c)` counts legs |
| throw, shield drop | `s32(B+0xa54) == 1`, `s32(B+0xb9c)` 1 → 0 at t 20, record 81 at t 25 |
| frontal block | HP unchanged, `s32(B+0xa54) == 4 and s32(B+0xa58) == 0` |
| side hit / back hit through the shield | HP drops with `s32(B+0xb9c) == 1` |
| blocked → hunt (far, or rand % 3 ≠ 0) | 4 → 0 |
| blocked → burst (near and rand % 3 == 0) | 4 → 2; `s32(B+0xb08) == 1` for t 1..100; record 14 at t 40 |
| frozen (`+0xb18 > 0`) bypasses the shield | frontal hit lands while frozen (needs a freeze item) |
| two knights (d4 f54) | both actors; player invulnerable 60 at the first death, 1200 at the last |
| state 3 | `s32(B+0xa54) == 3` — report if ever seen |

## 9. Unresolved

- Record 81 / 14 / 13 in flight.
- State 3's writer.
- `+0xb8c` (cleared each tick here; set elsewhere?).
- The exact sign convention of the block angle (which of record→actor / actor→record) — taken from
  `boss-floor-common.md`; one live blocked hit settles it.
