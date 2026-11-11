# Ohm — type 67 (FUN_00422ed6)

**code-read**: port `src/actor_ai.c:1899..2034` against IDA `sub_422ED6` (all_ida.c:22016, skimmed: states,
constants, draws match; FRONT lists it RNG-verified on dungeon3-floors / -deep — the port's "unverified" header
comment is stale); record bodies 14
(owner 67) and 33 (port scene1_records_b_tick.c:3136, :2634); death arm `sub_42F7AD` case 67 (matches IDA; the port
notes no recording kills one). Shared rules: `boss-floor-common.md`.

## 1. Where

| place | `dungeon` / `floor` | count |
|---|---|---|
| dungeon 2 10F | 2 / 9 | 1 |
| Boss Rush (dungeon 2's), 2nd floor | 5 / 34, `0x438b4d0 == 2` | 1 |
| dungeon 5 floors 43 (×2), 49 (×3) | | 2 / 3 |

- Generic boss floor (FUN_0044cc80 / FUN_0044d0a4). Placed at (−40, 0, −34), state 8.
- Intro: pad locked 240 event ticks; Ohm idles 240 state ticks (no bees yet).
- End: Ohm frees ≈ death tick 132; bees die at 60 → portals, `s32(0x438b8a0) == 1`.

## 2. Stats

- Table: HP 150, AT 20, DF 30, MA 0, MD 10. No parts. `+0xab8` 1.0 → DF_eff 30. `+0xb54 = 1`.
- d2 f9: level 9 → **HP 420**, AT·mul 38. Rush f34: level 14 → HP 570, AT·mul 48.
- Body radius 4.5·1.3 = 5.85, half-height 3.9.

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`

| state | length | what |
|---|---|---|
| 8 intro | 240 | nothing. → 0 |
| 0 hold | 10..73 | anim 0. Tick 1: `+0xa60 = rand & 63`. At tick `+0xa60 + 10`: decide (2 rands, +1 when not charging) |
| 1 walk | 180 | anim 1; accel 0.01/tick along its yaw; turns ≤ π/50 per tick toward the player. → 0 |
| 2 charge | 211 | tick 1 anim 2 (wind-up 60); tick 61 anim 3 + SE 369; **ticks 61..120**: accel 0.05/tick along the yaw, turning π/200 per tick toward the player, dust (2 unit draws a tick); **tick 70: record 14**; tick 111 anim 4; 121..211 recovery. → 0 |
| 3 burst | 60 | ticks 20, 24, 28, 32: 8 × record 33 each (32 shots). → 0 |

Decide (state 0): `go` = |yaw − angle-to-player| < 0.2π or > 1.8π;
`rand & 7 == 0` → go = 1; then `rand & 7 == 0` → go = 0; go → state 2; else `rand & 3 != 0` → state 1, else state 3.
- Facing the player: charge 7/8, else walk 3/32, burst 1/32.
- Not facing: charge 7/64, walk 171/256, burst 57/256.

Bees (type 10; every tick outside state 8): while fewer than `cap` bees are in use, `+0xa64` counts to 120 → one
bee at a point 2..10 from the centre, > 10 from the player, y 10; `+0xa68` counts: > cap → next gap 320, > 2·cap →
520; **none once `+0xa68 >= 5·cap`**. cap 3 (dungeons < 3), 5 (dungeon 3), 8 (dungeon ≥ 4 — **the Boss Rush runs
in dungeon 5 → cap 8, 39 bees**). Dungeon 2: 14 bees in all.

Records:
- 14, owner 67 (charge): re-anchored every tick 2 ahead of Ohm, 2 up; reach 3.5 → **hits within 5.5 of that point**;
  record ages 0..59 = state ticks 70..129. Physical ×1.0: `ftol(AT·mul/2 − DF/4)`; knock-down 70.
- 33 (burst): spawn points at yaw ± {0.6π, 0.8π, 0.85π} · 3 out and ± 0.45π · 0.5 out, heights 2 / 0.5 / 3.2 / 3.5
  — i.e. from its sides and rear; each flies along that angle ± 0.1π at 0.025..0.05/tick; reach ≈ 0.1; live 72 ticks.
  "Magic" → 1–2.

Death (`+0xaf0`): 1 slow-mo; 60 last-of-kind sweep (every live actor dies if no other 67 lives); bursts to 119;
anim 5; from 100 alpha −8/tick, frees when it passes 0 (≈ tick 132).

## 4. When it can be damaged

- Always; never staggers, never knocked back.
- ×1.2 in states 1..3; back ×1.5; side ×1.2.
- Danger: only the charge strip (state 2 ticks 70..129, the 5.5 disc 2 ahead of it; it steers π/200 a tick =
  54° over the whole run) and bee stings.

## 5. Kill recipe (search)

- Names: `ost = s32(B+0xa54)`, `ot = s32(B+0xa58)`, `ohp = f32(B+0x42c)`, `oyaw = f32(B+0x420)`.
- Stand **behind** it (rear arc: `hit_angle` beyond ±3π/4), just outside the body radius 5.85. From behind it is
  "not facing" → charges only 11 % of decisions; the burst spawns at its rear/sides but does 1–2.
- When `ost == 2 and ot < 61`: stay behind / to the side beyond 7.5 of the point 2 ahead; it cannot turn more than
  54°. Keep hitting through the wind-up (ticks 1..60) and the recovery (121..211).
- Cell `(int(ohp/10), ost, ot//15, int(back_angle·4), int(dist/1.5), int(hp/10))`; score `-ohp`;
  avoid `hp < 30`. `back_angle = abs(atan2(px − ox, pz − oz) − oyaw)` wrapped.
- Kill bees when they crowd (5 HP·(1 + 0.2·lv) = 14 at level 9); they stop coming after the 14th.
- Per hit `ftol(ATK/2 − 7.5)` (ATK 54 → 20; back + state 36). 420 HP ≈ 12–21 hits.

## 6. Arms checklist

| arm | predicate |
|---|---|
| intro | `ost == 8` |
| hold → charge (facing) | `ost == 2` |
| hold → charge by the 1/8 override (not facing) | `ost == 2` with the player outside ±0.2π |
| charge vetoed by the second 1/8 (facing) | facing and `ost in (1, 3)` |
| walk | `ost == 1` (180 ticks) |
| burst | `ost == 3`; records at `ot` 20 / 24 / 28 / 32 |
| charge hits the player | `hp` drops with `ost == 2 and 70 <= ot < 130` |
| burst shot hits the player | `hp` drops 1–2 with `ost == 3` or shortly after |
| bee spawn | new slot type 10 with `+0xb2c == 1` |
| bee cap | 3 bees alive, `s32(B+0xa64)` stops counting |
| bee slowdown / exhaustion | `s32(B+0xa68) > 3` → `+0xa64 == −200`; `> 6` → −400; `>= 15` none |
| death + sweep | `s32(B+0x428) == 2`; others dying at `s32(B+0xaf0) == 60` |
| player dies | `hp <= 0` |

Port: fully ported, no PORT-DEBT in the AI. Death arm (anim 5, fade) unverified by recording, matches IDA.
