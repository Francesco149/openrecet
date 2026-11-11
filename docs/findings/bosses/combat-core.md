# Dungeon combat core (driver account)

2026-11-11. For agents driving retail with `./rd`. Sources: IDA export, port mirror
(`src/record_hit.c`, `src/actor_ai.c`, `src/dungeon_party.c`, `src/scene1_records_b_tick.c`), live runs
`bcore-1` / `bcore-2` / `bcore-4` (dungeon 2, 25F, Louie L26, ATK 54 / DEF 75 / MATK 15 / MDEF 29); `cs-arms` / `cs-die`
(dungeon 1 "Jade Way" 5F, Louie L8, ATK 23 / DEF 32 / MATK 7 / MDEF 10, HP 41 / 50, SP 16 / 16; frames tagged **[obs fNNNN]**
are `cs-arms` unless a run is named; `rd verify cs-arms` MATCH at f36595).

Tags: **[obs]** = seen live (frame, values). **[code]** = read from code only: a lead.

Boss sheet using this: [crown-slime.md](crown-slime.md).

## 1. Addresses

### Party block (base `0x56da1b8`)

| name | expr | meaning | tag |
|---|---|---|---|
| `p_act` | `s32(0x56db048)` | action state (table §2) | [obs] 0 / 1 / 2 / 4 |
| `p_actt` | `s32(0x56db04c)` | tick in the state | [obs] |
| `p_inv` | `s32(0x56db0f4)` | invulnerability; enemy records hit only when ≤ 0; idles at −100 | [obs] 30 after a hit, 1200 on boss death |
| `p_hurt` | `s32(0x56daeb0)` | knock-down hold; `p_act == 1` while > 0 | [obs] 70 from the crown slime's landing |
| `p_oct` | `s32(0x56dab00)` | facing octant 0..7; direction = (sin, cos)(oct·π/4); hit records use this | [obs] 6 = −x |
| `p_yaw` | `f32(0x56db05c)` | facing angle, same convention | [obs] 4.7124 with oct 6 |
| `px py pz` | `f32(0x56da1d8 / dc / e0)` | position | [obs] |
| vel | `f32(0x56daabc / ac0 / ac4)` | velocity; knock-back is written here | [code] |
| ATK / DEF / MATK / MDEF | `s32(0x56db0ac / b0 / b4 / b8)` | totals (base + 5 equipment slots) | [obs] 54 / 75 / 15 / 29 |
| `hp` `sp` | `f32(0x56db0bc / c0)` | leader HP / SP | [obs] |
| crit | `s32(0x56db064)` | this swing is a crit (rolled at swing tick 1) | [obs f35846] 0 → 1 at `p_actt` 1; still 1 after the swing ends, 0 on the next swing (f35965) |
| buffs | `s32(0x56db010 / 14 / 18 / 1c)` | 3600-tick item buffs: speed ×1.3 / dealt ×2 / taken ÷2 / all | [code] |
| skill slot | `s32(0x56db0d8)` | skill `B` fires | [code] |
| chara | `s32(0x438b7d8)` | current chara: 0 Louie, 1 Charme, 2 Caillou, 3 Tielle, 4 Elan, 5 Nagi, 6 Griff, 7 Arma | [obs] 0 |
| member | `m = bank + 0x2ceb0 + 0x6c*chara` | `s32(m)` level (0-based), `s32(m+0x44)` EXP, `s32(m+0x4c)` next | [obs] 25, 99022, 105300 |
| party fell | `s32(0x438be98)` | 1 = HP reached 0, floor ends | [obs cs-die f36131] 0 → 1 on the hit that took HP 13 → 0 |
| hit-stop | `s32(0x438bed8)` | world frozen N ticks after a hit (4; 8 on a crit); 30 at a crit swing's tick 3 (the flash) | [obs] 4 per hit; [obs f35846] 30 at `p_actt` 3; [obs f35881, f35019] 8 on the crit hit |
| slow-mo | `s32(0x438becc)` | boss death slow motion, 60 → 0; while > 0 the world ticks once per 3 frames (−3 per world tick) | [obs f36227..36287] death timer 1 → 21 over 60 frames |

### Actor table: `0x76b970 + i*0xba4`, 128 records

| off | type | meaning | tag |
|---|---|---|---|
| `+0x3e0` | f32 | ground height under the actor | [code] |
| `+0x3e4..ec` | f32×3 | hit point ("head"): x, y + rf[2]·rf[0], z | [obs] popup y 2.4 for type 23 |
| `+0x3f0 / f4 / f8` | f32 | position x / y / z | [obs] |
| `+0x3fc / 400 / 404` | f32 | velocity (x, z damped ×0.82 per tick) | [code] |
| `+0x414..41c` | f32×3 | home (spawn point); leash 300 | [code] |
| `+0x420` | f32 | yaw; facing = (sin yaw, cos yaw) | [obs] label test |
| `+0x424` | s32 | type (enemy.txt index) | [obs] |
| `+0x428` | s32 | 0 free, 1 alive, 2 dying | [obs] |
| `+0x42c / 430 / 434` | f32 | HP / HP bar (eases 2 % per tick) / max HP | [obs] |
| `+0x398` | s32 | age in ticks | [obs] |
| `+0x440` | s32 | stagger: AI suspended while > 0; 40 per hit (60 some records) | [code] |
| `+0x44c` | s32 | hit flash, 30 | [code] |
| `+0xa54` | s32 | AI state | [obs] type 23: 0 / 2 / 8 |
| `+0xa58` | s32 | AI state timer | [obs] |
| `+0xa5c / a60 / a64` | s32 | AI scratch (per type) | [obs] type 23 |
| `+0xab8` | f32 | power: DF and MD scale by power² (1.0 for normal enemies) | [obs] type 23: 5 / 3.66 / 2.32 / 1 |
| `+0xabc` | f32 | size: scales the hit radius and height | [obs] |
| `+0xae0` | s32 | level | [obs] 14 on 25F |
| `+0xaf0` | s32 | death timer (runs while `+0x428 == 2`) | [obs] 0 → 120 |
| `+0xaf4` | s32 | asleep (> 0): no AI, cannot be hit. Sleeps beyond 24, wakes within 12 / 24. Always 0 for boss-class, on checkpoint floors | [code] |
| `+0xb08` | s32 | ≠ 0: cannot be hit (burrowed mushrooms, Charme / type 28 / 41 guard windows) | [code] |
| `+0xb18` | s32 | sleep-spell timer (record 0x53) | [code] |
| `+0xb20` | s32 | 1 = no stagger, no knock-back, state not reset by a hit (bosses) | [obs] 1 on type 23 |
| `+0xb28` | s32 | ≠ 0: DF / MD count as 0 | [code] |
| `+0xb38` | s32 | > 0: damage taken ÷2 (timer) | [code] |
| `+0xb54` | s32 | 1 = no knock-back or stagger (type 5), 2 = halved | [code] |
| `+0xb9c` | s32 | front shield: a hit within ±63° of its facing does 0 and sets state 4 | [code] |
| `+0xb5c..b80` | s32×10 | last 10 hit ids: one record hits an actor once | [code] |
| `+0xb90 / b94` | s32 | delayed heal value (−1 none) / countdown | [obs] −1 / 30 on absorb |
| `+0xb3c` | s32 | own index | [code] |

Enemy stat record: `0x5c23f0 + type*0x68`: `+0x20` flags (1 boss-class), `+0x2c` HP, `+0x30` EXP, `+0x34` AT,
`+0x38` DF, `+0x3c` MA, `+0x40` MD, `+0x44` f32 rf[0..6] (rf[0] scale, rf[1] body radius, rf[2] height, rf[3] hit
radius). **[obs]** type 23: 100 / 20 / 30 / 8 / 0 / 10, rf 0.8, 4, 3, 4.5; type 2: 15 / 8 / 16 / 4 / 0 / 10, rf 0.8, 1.5, 2, 2.

- Max HP = enemy HP × (1 + 0.2·level) **[obs]** 380 = 100 × 3.8, 57 = 15 × 3.8 at L14.
- Enemy attack multiplier `lvmul` = 1 + 0.1·level (0.12 from L50) **[obs]** via damage taken.
- Enemy DF is NOT scaled by level, only by power² **[obs]**.

### Hit records (table B): `0x69324b0 + i*0x124`, 512 records

`+0x00` type (0 free), `+0x04` side (0 = the player's, 1 / 3 = an enemy's), `+0x10 / +0x14` owners, `+0x5c / 60 / 64`
pos, `+0x68..70` vel, `+0x98` age, `+0xa8` reach, `+0xb4` power, `+0xb8` crit, `+0x11c` hit id.
**[obs]** sword = type 2 in slot 0, reach 2.0, age = `p_actt − 3`, freed at age 20.

### Popups: `0x44e28f0 + k*0x1c`, 32 entries

`[0..2]` f32 pos, `[3]` ticks left, `[4]` 60, `[5]` value (damage, or the row of a word), `[6]` kind:
0 damage, 1 heal, 5 damage to the player, 10 crit word, 11 back, 12 side, 13 counter.
**[obs]** a hit writes (dmg, 0) then one entry per word. Caveat: entries age in a way that is unreliable under
`rd`'s render skip (a "ticks left" of 22 sat for 40 frames); find the newest by position / value, not by `[3]`.

## 2. Player actions

Dungeon pad is flipped against the shop: `R` = −x, `L` = +x, `U` = +z, `D` = −z; walk 0.175 per tick **[obs]**.

| `p_act` | meaning | tag |
|---|---|---|
| 0 | free: walk + attack input (only state that reads input) | [obs] |
| 1 | hurt / down; held while `p_hurt > 0`, forever at HP 0 | [obs] |
| 2 | sword swing | [obs] |
| 3 | chara 40 only | [code] |
| 4..8 | skill 0..4 | [obs] 4 |
| 11 | pickup (12 ticks) | [code] |
| 12 | 26-tick pose | [code] |

Input in state 0: `A` → swing; `B` → the selected skill; `1`..`5` → skill slot 0..4; button 0x40 cycles the skill **[code]**.

### Louie's sword (`p_act == 2`, FUN_0049831c)

| swing tick (`p_actt`) | event | tag |
|---|---|---|
| 0 | press frame; x / z velocity zeroed; facing = current octant | [obs] |
| 1 | crit roll (`rand < chara crit_rate`) | [obs f35846] flag set at `p_actt` 1 (press f35845, by chance); a second one found by `rd search` (hit f35019) |
| 3 | on a crit: hit-stop 30, `p_inv` = max(`p_inv`, 8), flash | [obs f35846..35875] `p_actt` 3 for 30 frames, `p_inv` 8, boss timer frozen at 51 |
| 4 | record type 2 spawns (0x54 on a crit) | [obs] age 1 at tick 4 (0x54 not read live) |
| **9..12** | **active**: record ages 6..9, tested each tick, up to 5 actors per tick, one hit per actor | [obs] every hit landed at tick 9 |
| ≥ 5 | a skill key cancels into the skill, only after the swing connected | [code] |
| 23 | record freed (age 20) | [obs] |
| 29 → 0 | state ends: 30 ticks from press to free (+4 per hit, hit-stop) | [obs] |

- Hitbox: a sphere test around a point **1.0 ahead of the player** along the octant, 1.0 up, re-anchored each tick **[obs]**
  (`rx = px + sin(p_oct*pi/4)`, `rz = pz + cos(p_oct*pi/4)`).
- Hit when `hypot(ex − rx, ez − rz) − 2.0 < size·rf[3]·rf[0]` and the vertical window
  `−hh − 1.6 < (ey + rf[2]·rf[0]) − (py + 1) < hh + 1.6`, `hh = size·rf[2]·rf[0]` **[obs]** (hits at gap −0.18, −0.92, −1.78;
  [code] for the vertical part, consistent with a hit on a slime at y 0.04).
- Reach from the player, facing straight at it: 3.0 + size·rf[3]·rf[0] (blue slime 4.6; crown slime 4.08 at power 1, 6.24 at power 5).
- No walking, no re-swing, no combo chain during the 30 ticks **[obs]** (inputs ignored). Set the facing with a direction
  on the tick before `A` (`"UR A"`); a bare `A` keeps the octant.
- One swing can hit several actors **[obs]** f12553: crown slime 21 + blue slime 33.

### Louie's skills (5 SP each **[obs]** 22 → 17 → 12)

| skill | state | timeline | record | tag |
|---|---|---|---|---|
| 0 spin (`B` default) | 4 | tick 1: `p_inv` ≥ 30; tick 5: record + hop; ticks 5..19 turn; ends 35 | type 0x22, power 2.0, reach 3.5, same anchor / ages as the sword → active ~tick 10 | [obs] hit at `p_actt` 10; damage = 2× sword |
| 1 (level ≥ 9) | 5 | tick 1: `p_inv` ≥ 30; tick 12: projectile; ends 30 | type 0x51, 0.5 per tick along the octant, power 1.5, damage = (physical + magic) / 2 | [code] |

- Spin reach from the player, facing it: 4.5 + size·rf[3]·rf[0] **[code]**; **[obs]** one spin hit two actors 3.1 and 3.2 away on different sides (f12549).
- Spin = 30 ticks of invulnerability: it passes through the crown slime's landing **[obs f34575..34615]**: `B` at boss t 27,
  `p_inv` 30 at spin tick 1 (f34577), landing record at t 31 (f34579) with the boss 2.17 away, `p_inv` 28 → HP flat 27; the spin's own
  hit at `p_actt` 10 (f34586, boss t 38, on the ground) = counter ×1.2: 22 = ftol(9·2·1.2) = 21, +0..4. SP 16 → 11. `p_inv` reaches 0
  at spin tick 31; the state ends at tick 35 (f34615).
- Skill 1 (state 5): **not reached** — Louie is level 8 here (`s32(m)` = 7 < 9): key `2` in state 0 is ignored (`p_act` 0, SP 16,
  f33585 on an undone branch). Still [code].
- Other hires **[code]**, stubs in the port: swings FUN_004977df (Charme), 00497e3a, 00494ca4, 004984bf, 004986ee,
  0049791f, 00497c83; skill 0: FUN_00494f87 (Charme), 00495305, 00495a32, 004964bf, 00496a63, 0049708e, 0049755c;
  skills 1..4: FUN_0049511d …, 0049508c …, 00495f3b …, 0049630d (`src/dungeon_party.c` dispatch FUN_0048cdcc).

## 3. Damage dealt (FUN_0043865e actor arm, `record_hit_actors`)

Order of operations, integer truncation (`ftol`) at each step:

1. `def = ftol(DF · power²)` (0 if `+0xb28`). **[obs]**
2. Physical `v = ftol(ATK/2 − def/4)`. Magic records: `v = ftol(MATK/2 − ftol(MD·power²)/4)`; mixed records average the two. **[obs]** physical
3. `dmg = ftol(v · record power)` (sword 1.0, spin 2.0). **[obs]**
4. ×2 on floor events 3 / 5; ×2 with buff `0x56db014` / `0x56db01c`; ÷2 on floor events 6 / 7; ÷2 if `+0xb38 > 0`. **[code]**
5. **Counter Attack!** (kind 13): enemy `+0xa54` in 1..6 → ×1.2. **[obs]**
6. Angle `rel` (below): 45°..135° → **Side Attack!** (kind 12) ×1.2; beyond 135° → **Back Attack!** (kind 11) ×1.5. **[obs]**
7. ×1.5 if `0x56dae9c` or `0x56daea4` (special states). **[code]**
8. Crit (kind 10, the swing's flag): ×2. **[obs f35881]** boss, power 1, state 2 t 57, front: 130 → 110 = ftol(9·1.2) = 10, ×2 = 20,
   +0..3; kinds 13 + 10. **[obs f35019]** blue slime (DF 4 → base 10), back + counter + crit: 42 = 10·1.2 → 12, ·1.5 → 18, ·2 → 36, +0..6;
   popups (42, kind 0), kind 10 row 0, kind 11 row 1, kind 13 row 2. So the crit doubles after the labels and before the random term.
9. Minimum 1 (type 5: 0). Then random: `+ rand % (dmg/5)` if dmg ≥ 5, else `+ rand & 1`. **[obs]**

Labels multiply: counter + side = ×1.44, counter + back = ×1.8. A labelled hit also sheds `dmg/4` EXP orbs (max 25, +1 EXP each) **[code]**.

**The angle.** `rel = angle(enemy → hit record) − enemy yaw`, wrapped to ±π. The record, not the player:

```
rx    = px + sin(p_oct*pi/4)            rz = pz + cos(p_oct*pi/4)
raw   = atan2(ex - rx, ez - rz) - eyaw + pi
rel   = atan2(sin(raw), cos(raw))
label = none if abs(rel) < pi/4  else  side if abs(rel) < 3*pi/4  else  back
```

with `ex ez eyaw` = `f32(A+0x3f0)`, `f32(A+0x3f8)`, `f32(A+0x420)`, `A = 0x76b970 + i*0xba4`.

**[obs]** on actor 0 (type 23), ATK 54:

| frame | run | power | state | rel | words (kinds) | damage | predicted |
|---|---|---|---|---|---|---|---|
| 12553 | bcore-1 | 2.32 | 0 | 1.04 | Side (12), picture `bcore-1/view/f012558.png` | 21 | 16·1.2 = 19, +0..2 |
| 12642 | bcore-2 | 2.32 | 0 | 2.75 | kind 11 | 26 | 16·1.5 = 24, +0..3 |
| 12703 | bcore-2 (undone) | 2.32 | 0 | 2.31 | kind 12 | 20 | 19, +0..2 |
| 12812 | bcore-2 | 1.0 | 0 | 0.25 | none | 26 | 25, +0..4 |
| 13015 | bcore-2 | 1.0 | 2 (t 35) | −0.80 | kinds 12 + 13 | 38 | 25·1.2·1.2 = 36, +0..6 |
| 12549 | bcore-2 | 2.32 | 0 | side | kind 12, spin | 43 | 16·2·1.2 = 38, +0..6 |
| ~12481 | dungeon2-21f | 2.32 | 2 | side | "Counter Attack!" over "Side Attack!" (`dungeon2-21f/view/f012510.png`), kinds 12 + 13 | 22 | 16·1.2·1.2 = 22, +0..3 |

Word ↔ kind, all four **[obs]** from pictures: 12 = "Side Attack!", 13 = "Counter Attack!" (orange), 11 = "Back Attack!" (green;
`cs-arms/view/f034645.png`: the back hit of f34633, 14, kind 11, over the spin's fading "Counter Attack!" 22), 10 = "CRITICAL!" (red;
`cs-arms/view/f035030.png`: "Counter Attack!" / "Back Attack!" / "CRITICAL!" stacked top to bottom = rows 2 / 1 / 0, 42;
`cs-arms/view/f035895.png`: "Counter Attack!" over "CRITICAL!", 20). Stack order: crit lowest, then back / side, counter on top.

**[obs]** `cs-arms`, actor 0 (type 23), ATK 23 (base at power 1 = ftol(11.5 − 8/4) = 9; at power ≥ 2.32 the base is ≤ 0 → 1):

| frame | power | state | rel | words (kinds) | damage | predicted |
|---|---|---|---|---|---|---|
| 33622 | 5 | 0 | 1.04 | side | 1 | min 1, +0..1 |
| 33905 | 3.66 | 0 | 2.13 | side | 2 | min 1, +0..1 |
| 34240 | 2.32 | 0 | 2.05 | side | 2 | min 1, +0..1 (ftol(11.5 − 43/4) = 0) |
| 34524 | 1 | 0 | 0.003 | none | 9 | 9, +0..1 |
| 34586 | 1 | 2 (t 38) | 0.10 | kind 13, spin | 22 | 9·2 = 18, ·1.2 = 21, +0..4 |
| 34633 | 1 | 0 | 3.139 | kind 11, picture | 14 | ftol(9·1.5) = 13, +0..1 |
| 35881 | 1 | 2 (t 57) | 0.42 | kinds 13 + 10, picture | 20 | ftol(9·1.2) = 10, ·2 = 20, +0..3 |
| 35974 | 1 | 0 | 1.64 | kind 12 | 10 | ftol(9·1.2) = 10, +0..1 |

**Counter = hitting an enemy inside its own attack state** (`+0xa54` 1..6). A normal enemy's hit then staggers it
(`+0x440 = 40`, state and timer reset to 0, knock-back), so the hit also cancels the attack **[code]**. **[obs]** blue slime in
state 1 (lunge): kind 13, 32 damage (26·1.2 = 31, +0..5), f12812 bcore-2.

What "defence by dungeon level" means: nothing. Level raises enemy HP and attack only.

## 4. Damage taken (`record_hit_player`, FUN_0043865e player arm)

- Enemy records (side 1 / 3) hit when `p_inv <= 0` and `hypot(player − record) − reach < 2` (+ a vertical window) **[obs]** landing record hit at 1.87, not at 4.23.
- Physical `ftol(AT·lvmul/2 − DEF/4)`; magic `ftol(MA·lvmul/2 − MDEF/4)`; × record power; ÷2 buffs / special states; ×2 floor
  events 3 / 4; ÷2 events 6 / 8; minimum 1; then the same random term (`+ rand % (d/5)` or `+ rand & 1`) **[code]**.
  **[obs]** crown slime landing: 30·2.4/2 − 75/4 = 17 → took 17, 19, 18, 17; at L4 vs DEF 32: ftol(30·1.4/2 − 8) = 13, +0..1 → took 14
  (f33801), 14 and 13 (cs-die). Blue slime contact at L4 vs DEF 32: ftol(16·1.4/2 − 8) = 3, +0..1 → took 4 (undone branch, f34714;
  it also cancelled the swing in progress at `p_actt` 8). Blue slime contact: 16·2.4/2 − 18.75 = 0 → min 1, +0..1; a 2 was taken
  (bcore-1 ~f13100, source not isolated).
- After a hit: `p_inv = 30`, `p_act = 1`, `p_hurt = 30` (70 for record types 0xe, 0x12, 0xa1), knock-back 0.3 away (vy 0.4),
  4 frames of hit-stop **[obs]** f13011: hp 89 → 70, `p_hurt` 70, `p_inv` 30, pushed from 1.87 to 7.08.
  `p_inv` only counts down once `p_hurt` is 0 → 100 ticks without control or damage after a knock-down **[obs]**.
- Contact damage is a record too: small slimes carry a permanent type-11 record (reach ≈ their body) **[code]**; **[obs]** type-11
  side-1 records sit in table B while blue slimes live. An enemy without one has no contact damage.
- Guard **[code]** (**not reached** in `cs-arms`: the crown slime fight has no projectile record — only the landing (type 14) and the
  blue slimes' contact (type 11), which nothing guards): no button. Louie (and Nagi) deflect projectile records (0x56 / 0x96; Louie also 0x1f / 0x51 / 0x6c) arriving within
  ±45° of the facing, in state 0 only: no damage, half knock-back, `p_inv = 15`. Nothing guards melee, contact or shockwaves.
  The shield item only adds DEF. Charme counters while `0 < s32(0x56dae30) < 30`.
- HP ≤ 0 **[obs cs-die f36131]** (side run from `cs-arms:pow1b`; landings 27 → 13 at f35825, 13 → 0 at f36131): `s32(0x438be98)` 0 → 1 on
  the hit, `p_act` 1 and stays 1 after `p_hurt` 70 → 0, `p_inv` frozen at 30, `lock` stays −1, the boss goes on (state 2 → 0). 123 frames
  later (f36254) `dlg` 0 → 1: the defeat event (first defeat: "Oh no! Wh-what do we do now?!" … Tear's one-item box lecture; still
  `scene == 1`, `stage == 5` 440 ticks of taps later, not followed further). One-chara party: that is the wipe.
  Accessory 2510 revives to full once; 2505 heals below half.
- Falling off the stage: 10 % of max HP, respawn at the floor start **[code]**.

## 5. SP, EXP, items

- No HP or SP regeneration **[code]**; **[obs]** HP flat between hits over 1,000 idle ticks (bcore-4), SP only moved by skills.
- Skill cost 5 (Louie), halved on floor event 1 / accessory 2507 **[code]**.
- EXP: a kill sheds orbs worth `(EXP + level/2)·0.8 + combo` (halved when the chara's level > 2× the enemy's and ≥ 9); an orb
  is taken within 1.5 once 30 ticks old, +10 (+1 for a labelled-hit orb) **[code]**. A slime spawned by a boss gives 1.
- Level-up fires mid-floor at EXP ≥ next: HP and SP refill to max, stats recomputed **[code]** (not seen: 99022 / 105300 at the end of bcore-2;
  **not reached** in `cs-arms`: 8847 → 9081 of 10800 over the whole fight — the floor cannot supply the missing 1,719).
- EXP seen **[obs]** `cs-arms`: crown slime L4 kill = 17 orbs × 10 = 170 = ftol((20 + 4/2)·0.8) (+10 at death t 31, f36297, then 16 more
  f36328..36356 as the orbs fly in; `p_inv` 1200 meanwhile); labelled hits and the two boss-spawned slime kills together +64 over the fight
  (8847 → 8871 by f34894, → 8887 after the crit kill, → 8911 at the boss kill).
- Pause-menu food: HP / SP + the item's value + a small random term, capped at max; `~` opens the menu (`scene == 9`) **[obs]** by the
  earlier probe (Walnut Bread +13 HP, Taiyaki SP → 32).

## 6. Fighting by search

- **Name the enemy first** (once per run): `ex ez eyaw est et epow esize ehp euse` at the offsets above, then the derived
  `rx rz rel gap`, with `gap = hypot(ex - rx, ez - rz) - 2.0 - esize*RF3*RF0` (RF3·RF0: crown slime 3.6, blue slime 1.6).
  A swing started now connects when `gap < 0` holds 9 ticks later; `abs(rel)` picks the label.
- **Positioning goal:** `abs(rel) > 2.75 and gap < -0.3 and p_act == 0` (back; use 2.75, not 2.356: the player drifts
  ~0.05 rad while stopping). Cell `(int(px), int(pz), p_oct)`, score `abs(rel) - max(gap, 0)`. Found in 9–12 s **[obs]**.
  Then `rd play R "A .*14"`: the damage lands on `p_actt == 9`.
- **Damage goal:** `ehp < N`, cell `(int(ehp/12), int((px-ex)/1.5), int((pz-ez)/1.5), p_act, est)`, score
  `-ehp - max(gap, 0)*3`, actions = 8 walks `"DIR*6"` + 8 directed swings `"DIR A .*6"` (+ `"DIR B .*6"`), `--apply-best`.
  112 HP of crown slime at power 1 in 30 s, `-j 4` **[obs]**.
- **Always in `--avoid`:** `hp < H`, and the boss's own "do not let this happen" (crown slime: `epow > 4`).
- **Timing a counter:** put the enemy's state timer in the cell (`et // 5`) and its state in the avoid
  (`est != 2`): found in 35 s **[obs]**.
- **Read the outcome from state, not from words:** the HP delta against the table in §3 tells which labels applied.
  Pictures cost minutes when the box is loaded (486 s for one `rd look` on 2026-11-11).
- **A goal is checked before the avoid** on the frame both turn true **[obs]** (a search "killed" a slime that was in fact
  absorbed): make the goal exact (`euse == 2`, not `euse != 1`).
