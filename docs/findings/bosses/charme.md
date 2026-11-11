# Charme — type 25 (FUN_0041fa6e)

**code-read** from the port `src/actor_ai.c:3123..3339` (= FUN_0041fa6e, IDA `sub_41FA6E` all_ida.c:19783 — not
re-read line by line; the port is RNG-verified: dungeon1-charme / -clear, dungeon2-run*); hit counter FUN_0041fa37 (port record_hit.c:336); floor timeline FUN_0044d47d /
setup FUN_0044d1e4 (port dungeon_flow.c:585..789); death arm `sub_42F7AD` case 25. Shared rules:
`boss-floor-common.md`.

## 1. Where

| place | `dungeon` / `floor` | setup / event |
|---|---|---|
| dungeon 1 15F | 1 / 14 | FUN_0044d1e4 floor-14 arm / FUN_0044d47d |
| Boss Rush (dungeon 1's), 3rd = last floor | 5 / 32, `0x438b4d0 == 1` | FUN_00450ee0 → FUN_0044cc80(2) / FUN_0044d0a4 |

Floor 14 timeline (`s32(0x438b8a0)` stage, `s32(0x438b8a4)` tick; pad locked in stages 0..2, 4..6):

| stage | what |
|---|---|
| 0 | first visit only (bank+0x2bc8c 0): tick 60 dialogue iv3_1, Charme spawned at (−40, 5, −20) state 8 → 1 |
| 1 | camera sweep, 300 ticks; tick 300 dialogue iv3_2 → 2 |
| 2 | tick 1: floor portal sinks, title banner, voice; tick 120 → 3. A later visit starts here with Charme at (−40, 10, −32) |
| 3 | **the fight**, pad free. No actor with `+0x428` ∉ {0, 2} → 4 (story mode 2: 6) |
| 4 | tick 60 fade-out; party back at the start → 5 |
| 5 | tick 30 dialogue iv3_3; 31 → 6 |
| 6 | tick 30: first win → bank+0x2bc8d = 1, dialogue iv3_8; 31: both portals rise → 7 |
| 7 | free |

- Lock before the fight: first visit 60 + 300 + 120 ticks + two dialogues (`dlg == 1`: tap A); later visit 120.
- Already beaten (bank+0x2bc8d) → no Charme, stage 6, portals up.
- Portals: town (−35, 0, −30), floor (−40, 0, −30).
- Rush floor 32: generic boss floor (240-tick lock); Charme's state 8 ends at her tick 240.

## 2. Stats

- Table (.data defaults; no enemy.txt line): HP 150, AT 20, DF 20, MD 10. No parts. Body radius 2.0·0.6 = 1.2.
- d1 f14 and rush f32: level 9 → **HP 420**, AT·mul 38 (hit = `ftol(19 − DF/4)`). DF_eff 20 (`+0xab8` 1.0).

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)` (pre-incremented every AI tick)

Top of every AI tick: `+0xb08` counts down; `+0xa58`++; `+0xa64`++ (hit-window clock).

**Flee override** — `s32(B+0xa60) > 2`: anim 6, `+0xb08 = 1` (invulnerable). Tick 1: vy 1.0; up to 30 random
points in the ±10 square around (−40, −40) until one is > 10 from the player (2 unit draws each). Ticks < 20:
velocity = 0.08 · (target − pos). Lands (`y == ground`, tick > 20) → state 0, `+0xa60 = 0`.

| state | length | what |
|---|---|---|
| 8 entrance | to event stage 3 (d5: 240 ticks) | floor 14, stages 0 / 1: tick 240 hop (vy 0.8), 240..267 slides −z at 0.4 |
| 0 stalk | runs of 35..98 ticks | tick 1: plan `+0xae4` (3 rands). Tick % 30 == 1: a heading toward a point 4 from the player (1 unit + 1 rand, then ≤ 20 × 2 rands until a 4-ray is clear), run length `+0xa5c = rand & 63`. From tick 6 accel 0.047 (plan 3: only faces the player, stands). From tick 10: trigger test (below). Run ends at tick 35 + `+0xa5c` → tick 0 (new plan). |
| 1 slash | 28 | anim 3 from tick 3; tick 7: voice, SE 343, record 16 |
| 2 bind field | 51 | anim 5; tick 12: record 83, voice |
| 3 dash | 110 | anim 6, from 45 anim 4; tick 1 cry; ticks 45..64 accel 0.08 along the yaw fixed at the trigger, **`+0xb08 = 1`**, afterimages; tick 50: SE 349, record 21; tick 65 `+0xb08 = 0` |

Plan roll (state 0 tick 1): `p = rand & 3`; `rand & 1` → 2; `rand & 3 == 0` → 2.
P(plan 2) = 23/32, P(0) = P(1) = P(3) = 3/32.

Trigger (tick ≥ 10, plan ≠ 3, Charme on the ground):

| plan | condition | → state |
|---|---|---|
| 0 | player within 1.5 of one of her axes and 1.5 < distance along the other < 8 | 3 dash (if the distance is < 3: plan := 2 → state 1) |
| 1 | player within ±10 of (−40, −40) on both axes **and** a clear 12-ray along her current yaw | 2 bind field |
| 2 | within 1.5 of an axis and 0 < along < 4 | 1 slash |

Records:
- 16 (slash): 1 ahead (facing octant), 1 up, reach 1.5 → hits within 3.5 of that point; live only while state 1,
  record ages 6..9 = state ticks 13..16. Physical ×1.0.
- 21 (dash): 2 ahead, 2 up, reach 0 (owner 25) → within 2.0 of that point; record ages 0..14 = state ticks 50..64.
- 83 (bind field): starts 0.3 ahead, slides 0.5/tick ×0.92 for 30 ticks (≈ 5.7 units), stops; record ages 46..554:
  radius grows to 1.9; a player within radius + 2 has **x / z velocity zeroed every tick** (FUN_0043865e type-83 arm);
  no damage. Lives 600 ticks (enemy-owned). Effect on walking input not traced past the velocity write.

Hit counter (FUN_0041fa37, on every damage): `+0xa64 < 60` → `+0xa60`++, `+0xa58 = 0`; else `+0x440 = 0`,
`+0xa64 = 0`. Either way `+0xb08 = 10`.
- A hit also does the generic stagger (`+0x440 = 40`, state 0, knock-back): the **window-opening hit cancels its
  stagger**; hits inside the 60-AI-tick window keep it.
- AI (and so the `+0xb08` countdown and `+0xa64`) is frozen while `+0x440 > 0` → after a counted hit she is
  unhittable for 40 + 10 ticks; after an opener, 10.
- 3 counted hits → flee. `+0xa64` is not reset by the flee.

Death (`+0x428 == 2`): tick 1 slow-mo 120, player invulnerable 1200. Floor 14: she stays (stage 5 holds her
beside the party, stage 6 shrinks her out). Dungeon 5: fades from tick 60 (alpha −16) and frees.

## 4. When she can be damaged

- `s32(B+0xb08) == 0` only. Blocked: 10 ticks after each hit (50 when staggered), dash ticks 45..64, the whole flee.
- State 8 (entrance): no `+0xb08` → hittable once the pad is free.
- Bonus ×1.2 in states 1..3; back ×1.5.
- Best window: **dash recovery, state 3 ticks 65..110** (45 ticks, stationary after the slide), and state 1 / 2
  (28 / 51 ticks, she stands).

## 5. Kill recipe (search)

- Names: `cst = s32(B+0xa54)`, `ct = s32(B+0xa58)`, `chp = f32(B+0x42c)`, `cinv = s32(B+0xb08)`,
  `chits = s32(B+0xa60)`, `cplan = s32(B+0xae4)`.
- Cell `(int(chp/10), cst, ct//10, cinv > 0, chits, int((px − cx)/1.5), int((pz − cz)/1.5))`; score `-chp`;
  avoid `hp < 30`.
- Stay off her axes (|dx| ≥ 1.5 and |dz| ≥ 1.5) → plans 0 / 2 never trigger; plan 1 needs no alignment (bind
  field: leave its radius, ≤ 3.9 around where it stops).
- To bait a dash: stand on an axis 3..8 away, step off the line before tick 45 (the yaw is fixed at the trigger),
  hit her from behind during ticks 65..110.
- Each hit costs ≥ 10 ticks of invulnerability; the 4th hit of a burst makes her flee (≥ 21 ticks, lands > 10 away).
- Per hit `ftol(ATK/2 − 5)` (ATK 54 → 22; ×1.2 in states 1..3; ×1.5 back). 420 HP ≈ 11–19 hits.

## 6. Arms checklist

| arm | predicate |
|---|---|
| first-visit intro (stages 0 → 1 → 2) | `s32(0x438b8a0)` passes 0, 1, 2; hop at `cst == 8 and ct == 240` |
| later-visit intro (stage 2 start) | floor loads with `s32(0x438b8a0) == 2` |
| plan 0 / 1 / 2 / 3 rolled | `cst == 0 and ct == 1 and cplan == N` |
| plan 3 (stand, face) | `cplan == 3`, no walk until the run ends |
| plan 0 downgraded (< 3) | `cst == 1` straight after `cplan == 0` |
| slash | `cst == 1`, record at `ct == 7` |
| bind field | `cst == 2`; player velocity zero while inside |
| dash | `cst == 3`; `cinv == 1` at `ct` 45..64 |
| hit-window opener (no stagger) | hit with `s32(B+0xa64) >= 60` → `s32(B+0x440) == 0` |
| counted hit (stagger) | `chits` rises, `s32(B+0x440) == 40` |
| flee | `chits == 3`, `cinv == 1`, vy 1.0 |
| flee point search exhausts (30 tries) | not observable without the draw count |
| player hit by slash / dash | `hp` drops in `cst == 1` / `3` |
| player dies | `hp <= 0` (dungeon1-run / -charme recordings do this) |
| death, floor 14 scene | `s32(B+0x428) == 2`, stages 4 → 5 → 6 → 7, bank+0x2bc8d = 1 |
| revisit after the win | no type-25 actor, `s32(0x438b8a0) == 6` |
| death in dungeon 5 (fade) | `s32(B+0x3dc)` falls from tick 60 |

Port: no PORT-DEBT in FUN_0041fa6e / the timeline. Record 83's player arm and record 21 / 16 bodies are ported.
