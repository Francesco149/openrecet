# Jellyfish / Red Jellyfish — types 70 / 71 (クラゲ / 赤クラゲ)

**code-read**: IDA `sub_4219F2` (AI, 0x4219f2; the HP thirds checked in disassembly 0x421a38..0x421a60), `sub_4218E1`
(4-layer anim set), `sub_42F28C` case 70 / 71, `sub_43865E` (IDA 38919..38945: 2 hit parts), `sub_43AE20` (record 47,
IDA 40176..40200), `sub_42F7AD` case 70 / 71. **Port: AI stub, death arm stub; the 2 hit parts are ported,
unverified.** Shared rules: `boss-floor-common.md`. Nothing run.

## 1. Where

| place | `dungeon` / `floor` | type | level → HP |
|---|---|---|---|
| Obsidian Tower 10F | 3 / 9 | 70 | 18 → 150·4.6 = **690** |
| Lapis Ruins 40F | 4 / 39 | 71 | 32 → 150·7.4 = **1110** |
| dungeon 5 floor 39 (d3 Boss Rush 2nd) | 5 / 39 | 70 | own: 65 → 2100; rush: 28 → 990 |
| dungeon 5 floor 52 (d4 Boss Rush 7th) | 5 / 52 | 71 | own: 87 → 2760; rush: 48 → 1590 |

Generic boss floor: placed at (−40, 0, **−40**) in state 8 (`boss-floor-common.md`), `FUN_0044cc80` +
`FUN_0044d0a4(1)`. List second line: ローパー緑 (17) — the add it summons.

## 2. Stats and parts

- Table: HP 150, EXP 25, AT 20, DF 30, MA 0, MD 10, boss flag 1. rf0 1.0 (70) / 0.8 (71), rf2 3.0, rf3 4.5.
- Every AI tick: velocity = 0, `+0xb54 = 1`; before the AI `+0x3dc = DAT_0438ccc4` (alpha from a global — the
  water? not traced).
- **The body is not a hit volume. Two hit parts** = joints `DAT_005c530c = {1, 2}` (the two arms): centre
  `f32(B + 0x6fc + 12·j)` (x, y, z), j = 1, 2 → `B+0x708..`, `B+0x714..`; radius `0.5 · 4.5·rf0` = 2.25 (70) / 1.8
  (71); half-height `0.5 · 3.0·rf0` = 1.5 / 1.2. A hit on either is a normal hit on the one HP pool (no part flag,
  full damage).
- The body floats at y 6 over the arena centre: `x = −40 + 2·sin(0.01·t) + 0.5·sin(0.015·t)`,
  `z = −40 + 2·cos(0.01·t) + 0.5·sin(0.015·t)` — the arm joints are what comes down to the player.

## 3. Phase — `s32(B+0xae4)`, every tick

`0` (HP ≥ ⅔ max) / `1` (HP < ⅔) / `2` (HP < ⅓); **type 71: +1** (1 / 2 / 3). It scales the adds and the shots.

## 4. State machine

Body: `s32(B+0xa54)` is 8 (intro) or anything else (the cycle); cycle clock `s32(B+0xa58)` (never reset after the
intro), `c = t % 800`. Spin `s32(B+0xa68)`: yaw += 0.001·spin per tick.

| when | what |
|---|---|
| state 8 | y = 16 − 10·t/240; t ≥ 240 → state 0, t = 0 |
| c 30..89 | spin++ (0 → 60) |
| c 150..249 | arm-anim layer `+0xa74 = 10` (else 0) |
| **c == 190** | adds: if live actors (any type, incl. itself) < 8: `phase + 1` times: up to 20 tries of {angle = 2π·rnd, point = centre + 12·(sin, cos); accept if ≥ 8 from the player and on the floor (`FUN_00432e50`)} → `FUN_0041f903(x, 0, z, 17)` green roper, `+0xb2c = 1` |
| c 200..259 | spin−− (60 → 0) |
| **spin < 30** (c < 60 or c ≥ 230, roughly) | each arm i = 0, 1 not already in state 3: player within **8.0** (xz) of joint i+1 → arm state 3, arm tick 0 |
| **c 500..799** | `+0xa74 = 10`; `phase + 1` shots at `c == 550 + k·280/(phase+1)`, k = 0..phase: **record 70** spawned with the actor's position temporarily set to centre + r·(sin, cos)(2π·rnd), r = 4·(rnd + 1) (4..8), y 0 |

Arms: state `s32(B+0xa88 + 4i)`, tick `s32(B+0xa90 + 4i)` (++ every tick), anim layer `s32(B+0xa78 + 4i)` (the four layers `+0xa70..+0xa7c` go to `FUN_004218e1` each tick; the window code owns `+0xa74`).

| arm state | what |
|---|---|
| 0 | idle, layer 0 |
| **3 slam** | layer = i + 2. Tick 44: SE 373. **Ticks 41..69: every tick a record 14 at the joint** (the actor's position is swapped to the joint for the call) + effect 112. Tick ≥ 120 → 0 |
| 1 → 2 | 1: after 30 ticks → 2, layer 6; 2: until 600 ticks (or forever while `s32(B+0xa5c) >= 500`) → 0. **No writer of arm state 1 found** (record 47's code only reads the state: IDA 40189) |

## 5. Attacks

| attack | record | timing | notes |
|---|---|---|---|
| arm slam | 14 at the arm joint, 29 consecutive ticks | arm state 3, ticks 41..69 (40 ticks of warning) | physical AT 20·mul; knock-down 70; the player's 30-tick invulnerability means at most one hit per slam |
| rain | 70 at a random point 4..8 from the centre, 1..3 (70) / 2..4 (71) per cycle | c = 550 + k·280/(phase+1) | "magic" list → MA 0 → 1 damage; what a record 70 does after spawning (falls? explodes?) not read |
| ropers | type 17, 1..3 (2..4) per cycle, capped by 8 live actors | c == 190 | roper AI `FUN_0042a74c` (ported) |
| record 47 | arm-attached (index `+0xb0` = 1 / 2), dropped while that arm's state ≠ 0 | spawner not found in the AI | **not resolved** |

## 6. When it can be damaged

Any time after state 8 (`+0xb08` never set), but only at the arm joints — in practice while an arm is down
(arm state 3) or wherever the idle arm joints hang within reach (their rest height: not known from the code; read
`f32(B+0x70c)` / `f32(B+0x718)` live). `+0xb54 = 1`: no stagger, the cycle never resets.

## 7. Kill recipe

1. Wait `s32(B+0xa54) != 8`. Name the joints: `j1 = (f32(B+0x708), f32(B+0x70c), f32(B+0x710))`,
   `j2 = (f32(B+0x714), f32(B+0x718), f32(B+0x71c))`.
2. Bait: stand within 8 of a joint while `s32(B+0xa68) < 30` → `s32(B+0xa88 + 4i) == 3`. Step out of the joint's
   spot before arm tick 41, come back after tick 69 (or hit from the edge of the 2.25 radius during the slam — the
   records are at the joint itself) and hit the joint until arm tick 120.
3. Cell: `(s32(B+0xa88), s32(B+0xa8c), s32(B+0xa90)//20, int(f32(B+0x42c))//40, int(px − f32(B+0x708)), int(pz − f32(B+0x710)))`;
   score `-f32(B+0x42c)`; avoid `hp <= 0`.
4. Ropers: kill them (portals need an empty table; they also count toward the 8-actor add cap).
5. Rain windows `c` 500..799: keep moving; landing points are 4..8 from the centre.
6. Done: `s32(B+0x428) == 2` → at death tick 60 every live actor dies with it; freed after the alpha fade (from 100).

## 8. Arms checklist

| branch | predicate |
|---|---|
| intro descent | `s32(B+0xa54) == 8`, `f32(B+0x3f4)` 16 → 6 |
| spin up / down | `s32(B+0xa68)` 0 → 60 → 0 inside one 800-tick cycle |
| add wave, phase 0 / 1 / 2 (71: up to 3) | new actors of type 17 with `s32(+0xb2c) == 1` at `c == 190`: `phase + 1` of them |
| add wave skipped (≥ 8 live actors) | `c == 190` with no new actor |
| arm 0 slam / arm 1 slam | `s32(B+0xa88) == 3` / `s32(B+0xa8c) == 3` |
| slam not started while spinning | player within 8 of a joint and `s32(B+0xa68) >= 30` → state stays 0 |
| rain count per phase | records of type 70 per cycle = `s32(B+0xae4) + 1` |
| phase steps | `s32(B+0xae4)` changes at HP ⅔ and ⅓ |
| hit on joint 1 vs joint 2 | HP drops with the record nearest `j1` / `j2`; a record at the body centre (y 6) does nothing |
| red variant | type 71: phase starts at 1 |
| death | `s32(B+0x428) == 2`, `s32(B+0x3dc)` fading from death tick 100 |
| arm states 1 / 2 | `s32(B+0xa88 + 4i) in (1, 2)` — report if ever seen |

## 9. Unresolved

- Record 70 (the rain) and record 47 (arm-attached; who spawns it).
- The arm joints' height when idle → whether a ground swing reaches them outside a slam.
- `DAT_0438ccc4` (alpha source).
- Anim layer ids (0, 2, 3, 6, 10) named from use only.
