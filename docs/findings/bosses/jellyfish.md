# Jellyfish / Red Jellyfish — types 70 / 71 (クラゲ / 赤クラゲ)

**code-read**: IDA `sub_4219F2` (AI, 0x4219f2; the HP thirds checked in disassembly 0x421a38..0x421a60), `sub_4218E1`
(4-layer anim set), `sub_42F28C` case 70 / 71, `sub_43865E` (IDA 38919..38945: 2 hit parts), `sub_43AE20` (record 47,
IDA 40176..40200), `sub_42F7AD` case 70 / 71. **Port (2026-11-11): `actor_ai_jelly` (FUN_004219f2), `jelly_anim_set`
(FUN_004218e1), the death arm (anim 13) in `src/actor_ai.c`, from the disasm; with the 2 hit parts (`src/record_hit.c`)
verified on `obsidian-6f-10f-jellyfish`: portrng 46517 draws / f1..f15088, 0 divergences; portsave IDENTICAL; portblock
f9700 / f10200 / f11400 / f12750 / f13100 all blocks equal. Type 71, the ≥ 8-actor add skip, arm states 1 / 2 and the
tick-60 sweep: ported, no recording reaches them.** Shared rules: `boss-floor-common.md`.
**Live (2026-11-11):** run `ob-10f` (Obsidian Tower 10F, type 70, Louie L40 ATK 56 / DEF 58 / MATK 21 / MDEF 32), `rd verify` MATCH
f14788; probe `docs/findings/probes/obsidian-6f-10f.md`. Tags **[obs fNNNN]** = that run. Type 71 not run.

## 1. Where

| place | `dungeon` / `floor` | type | level → HP |
|---|---|---|---|
| Obsidian Tower 10F | 3 / 9 | 70 | 18 → 100·4.6 = **460** **[obs f9226]** (`+0xae0` 18, `+0x434` 460; HUD "LV19 HP 460") |
| Lapis Ruins 40F | 4 / 39 | 71 | 32 → 100·7.4 = 740 (if 71's table HP is also 100: not read) |
| dungeon 5 floor 39 (d3 Boss Rush 2nd) | 5 / 39 | 70 | own: 65 → 1400; rush: 28 → 660 (= the players' 1400 / 660) |
| dungeon 5 floor 52 (d4 Boss Rush 7th) | 5 / 52 | 71 | own: 87 → 1840; rush: 48 → 1060 (= the players' 1840 / 1060) |

Corrected from retail: the table HP is 100, not 150 (the ×1.5 figures of [W-Electro] are the wrong ones).

Generic boss floor: placed at (−40, 0, **−40**) in state 8 (`boss-floor-common.md`), `FUN_0044cc80` +
`FUN_0044d0a4(1)`. List second line: ローパー緑 (17) — the add it summons.

## 2. Stats and parts

- Table (type 70, `0x5c4060`) **[obs]**: HP **100**, EXP 25, AT **30**, DF 30, MA **20**, MD **30**, `+0x20` 1, `+0x24` 12; rf 1.0, 4.0, 3.0, 4.5,
  0.45, 1.0, 0.6. (Was: 150 / 25 / 20 / 30 / 0 / 10.) Type 71's record not read. Roper (17, `0x5c2ad8`): 15 / 10 / 16 / 4 / 0 / 0.
- Damage seen at L18: sword 20 base (ftol(28 − 30/4)): 24..27 with Side, 33 with Back, spin 44 / 53; slam 30 (ftol(30·2.8/2 − 58/4) = 27, +0..5);
  rain 23 (ftol(20·2.8/2 − 32/4) = 20, +0..3); roper contact 7.
- Every AI tick: velocity = 0, `+0xb54 = 1`; before the AI `+0x3dc = DAT_0438ccc4` (alpha from a global — the
  water? not traced).
- **The body is not a hit volume. Two hit parts** = joints `DAT_005c530c = {1, 2}` (the two arms): centre
  `f32(B + 0x6fc + 12·j)` (x, y, z), j = 1, 2 → `B+0x708..`, `B+0x714..`; radius `0.5 · 4.5·rf0` = 2.25 (70) / 1.8
  (71); half-height `0.5 · 3.0·rf0` = 1.5 / 1.2. A hit on either is a normal hit on the one HP pool (no part flag,
  full damage).
- **[obs]** body `+0x3f4` reads 5.95 after the tick (not 6.0); `+0x3dc` = 127 through the fight; idle joints hang at y 0.61..0.74,
  8.2..8.4 (xz) from the body centre — a ground swing reaches them at any time (also while they sweep in the spin).
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
| arm slam | 14 at the arm joint, 29 consecutive ticks | arm state 3, ticks 41..69 (40 ticks of warning) | physical AT 30·mul (**[obs f10185]** 30 at L18 vs DEF 58); knock-down 70 **[obs]**; the player's 30-tick invulnerability means at most one hit per slam |
| rain | 70 at a random point 4..8 from the centre | c = 550 + k·280/(phase+1), **only where that float is an integer**: phase 0 → 550; phase 1 → 550, 690; **phase 2 → 550 only** (643.33, 736.67 never match) **[obs f11730, f12718: one record per phase-2 cycle]**; phase 3 (71) → 550, 620, 690, 760 (code) | **[obs]** record 70: side 1, does not move, y 0, reach 3.0 (hit radius 5), power 1.0; a blue glow on the ground for 40 ticks, the bolt strikes at age 40, freed at age ≈ 60. MA 20: 23 damage (f10067), `p_hurt` 30 |
| ropers | type 17, 1..3 (2..4) per cycle, capped by 8 live actors | c == 190 | roper AI `FUN_0042a74c` (ported) |
| record 47 | arm-attached (index `+0xb0` = 1 / 2), dropped while that arm's state ≠ 0 | spawner not found in the AI | **not resolved** |

**[obs] also:** the spin (c 30..259) is not an attack — no record, the joints sweep through the player without damage.

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

| branch | predicate | live |
|---|---|---|
| intro descent | `s32(B+0xa54) == 8`, `f32(B+0x3f4)` 16 → 6 | **[obs f9226..9466]** t 0 → 240, y 14.95 at t 25 → 5.95; `lock` 0 → −1 at state 0; no line of dialogue |
| spin up / down | `s32(B+0xa68)` 0 → 60 → 0 inside one 800-tick cycle | **[obs f9496..9716]** 1 at c 30, 60 at c 90, 59 at c 200, 0 by c 270; yaw +0.06 / tick at 60 |
| add wave, phase 0 / 1 / 2 (71: up to 3) | new actors of type 17 with `s32(+0xb2c) == 1` at `c == 190`: `phase + 1` of them | **[obs f9656]** 1 (phase 0); **[obs f11350]** 2 (phase 1) at 12.0 / 11.8 from the centre; **[obs f12342]** 3 (phase 2). HP 69 (L18). `+0xb2c` not read |
| add wave skipped (≥ 8 live actors) | `c == 190` with no new actor | **not reached** (needs 7 ropers alive: ≥ 3 phase-2 cycles without killing one) |
| arm 0 slam / arm 1 slam | `s32(B+0xa88) == 3` / `s32(B+0xa8c) == 3` | **[obs f9518]** both at once (player under the torso, spin < 30); **[obs f11041]** arm 1 alone while arm 0 is at tick 67; at tick 120 → 0 for one tick, 3 again the next (f10915 → f10975 when out of range for 60). Tell: the joint lifts to y ≈ 4.8 by tick 33. **Hit taken [obs f10185]**: arm tick 43, 3.5..4 from the joint, hp 174 → 144, `p_hurt` 70 |
| slam not started while spinning | player within 8 of a joint and `s32(B+0xa68) >= 30` → state stays 0 | **[obs f11286..11305]** spin 60, d1 < 7.5, arm 0 stays 0; **[obs f11314..11339]** same for arm 1; it starts on the tick spin reaches 29 (f11390, c 230) |
| rain count per phase | records of type 70 per cycle = `s32(B+0xae4) + 1` | **wrong for phase 2.** **[obs f10028]** phase 0: 1 (c 550); **[obs f11016]** phase 1: the second at c 690; **[obs f12718..12872]** phase 2: c 550 only, none through c 745. Hit taken **[obs f10067]** 197 → 174 |
| phase steps | `s32(B+0xae4)` changes at HP ⅔ and ⅓ | **[obs f10791]** 0 → 1 (HP 325 → 298 at f10786; < 306.67); **[obs f11783]** 1 → 2 (155 → 130 at f11778; < 153.33) — on the boss's first tick after the 4-frame hit-stop |
| hit on joint 1 vs joint 2 | HP drops with the record nearest `j1` / `j2`; a record at the body centre (y 6) does nothing | **[obs f10434]** joint 1, arm idle, mid-spin, 434 → 407; **[obs f11174]** joint 2 (gap −0.43, joint 1 at +7.8) 275 → 251; **[obs f9536, 9572, 9608, 9644]** four swings under the body (0.7..2.8 from its centre, both joints out of reach): HP 460 flat. Reach = `hypot(joint − record) − 2.0 < 2.25` holds (hits at gap −0.07; one at +0.19 on swing tick 12) |
| red variant | type 71: phase starts at 1 | **not reached** (Lapis 40F) |
| death | `s32(B+0x428) == 2`, `s32(B+0x3dc)` fading from death tick 100 | **[obs f13005]** `+0x428` 2 (spin hit 53 → 0), slow-mo 60, `p_inv` 1200; the body drops y 5.95 → 0 in ≈ 17 death ticks; alpha 127 until tick 100, 71 at tick 106 (−8 / tick); **[obs f13164]** tick 115: `+0x428` 0, `estage` 1, `gate` 0 on the same frame. No add was alive (tick-60 sweep not seen) |
| arm states 1 / 2 | `s32(B+0xa88 + 4i) in (1, 2)` — report if ever seen | **never seen** (f9466..13005, every tick of the watched segments: only 0 / 3) |
| safe spot under the torso (players) | — | **[obs f9525..10066]** following the body (≤ 1.1 from its centre): both arms slam 4× (the joints stay 6.5..9 away), HP flat; then the rain hits there (f10067). Safe from slams and the spin, not from the rain |
| labels | — | picture `ob-10f/view/f010640.png`: "Back Attack!" 33 on a joint-1 hit (f10628) while the angle from the body centre (`a0rel`) was 1.18 — the angle is not taken from the body centre (the joint?: not resolved); the popup is drawn at the body. Spin hit 44, no label (`f011462.png`). No Counter (state stays 0) |

## 9. Unresolved

- Record 47 (arm-attached; who spawns it): not seen in table B at any sample. Record 70: resolved live (§5).
- Record 14 (the slam) was not in table B when read right after the hit (f10185, f10564): lives inside the tick; its reach is not read
  (a hit at 3.5..4 from the joint, none at ≥ 6.5).
- The label angle of a joint hit (above).
- ~~The arm joints' height when idle~~ y 0.61..0.74: reachable **[obs]**.
- `DAT_0438ccc4` (alpha source).
- Anim layer ids (0, 2, 3, 6, 10) named from use only.
