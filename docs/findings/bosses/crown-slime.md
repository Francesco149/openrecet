# Crown slime (actor type 23) — boss sheet

2026-11-11. AI = FUN_0042aef7 (port `actor_ai_t23`, `src/actor_ai.c`); death arm in FUN_0042f7ad; placement
FUN `enemy_place_boss`. Live runs: `bcore-1`, `bcore-2` (the kill), `bcore-4` (25F entry, both alive), all from
`dungeon2-21f` (dungeon 2 "Amber Garden" 25F, Louie L26, ATK 54, DEF 75).

Tags: **[obs]** seen live (frame, values) · **[code]** code-read lead. Combat rules: [combat-core.md](combat-core.md).

## TL;DR

- The boss's defence is its **power** (`+0xab8`): DF = 8 · power². Power 5 → DF 200 → every hit does 1–2. Power 1 → DF 8 → 25+.
- Power starts at 5, drops 1.34 every 300 ticks (5 → 3.66 → 2.32 → 1), and jumps back to 5 when the boss **absorbs a blue
  slime** (type 2) that is ≥ 600 ticks old.
- The boss spawns those blue slimes itself, one per 600 ticks. **Kill every blue slime before it is 600 ticks old** and the
  boss stays at power 1 from 900 ticks on.
- That is the earlier probe's blocker: the "vanished" small slime was absorbed at ~f12555 (survivor 2.32 → 5). Labels were
  never the cause: a "Side Attack!" at power 5 still shows its word and does 1 **[obs]**.

## Stats

| | value | tag |
|---|---|---|
| enemy.txt | HP 100, EXP 20, AT 30, DF 8, MA 0, MD 10; rf scale 0.8, radius 4, height 3, hit radius 4.5 | [obs] `0x5c23f0 + 23*0x68` |
| max HP | 100 · (1 + 0.2·level) = 380 at L14 | [obs] |
| level | by floor (`+0xae0`): 14 on dungeon 2's 25F | [obs] |
| attack | landing: `ftol(30·(1 + 0.1·level)/2 − DEF/4)` + random = 17–19 against DEF 75 | [obs] ×4 |
| defence | `ftol(8 · power²)`; level plays no part | [obs] |
| size | `+0xabc` eases 0.1 per tick to 0.15 + 0.15·power: 0.9 / 0.7 / 0.5 / 0.3 | [obs] |
| hit radius | 3.6 · size around its centre (sword reach from the player: 3.0 + 3.6·size) | [obs] |
| stagger / knock-back | none (`+0xb20 = 1` every AI tick): hits never interrupt it | [obs] state and timer unchanged by hits |

Sword damage at ATK 54 (unlabelled, before the random +0..20 %): `ftol(27 − ftol(8·power²)/4)`

| power | DF | base | side ×1.2 | back ×1.5 | counter + back ×1.8 | spin (×2) back |
|---|---|---|---|---|---|---|
| 5 | 200 | 1 (min) | 1 | 1 | 1 | 1 |
| 3.66 | 107 | 1 (min; 27 − 26.75 = 0) | 1 | 1 | 1 | 1 |
| 2.32 | 43 | 16 | 19 | 24 | 28 | 48 |
| 1 | 8 | 25 | 30 | 37 | 45 | 75 |

**[obs]** 1 at power 5 (f12598, with the Side word); 21 / 20 / 26 at 2.32; 26 / 38 / 66 at 1 (frames in combat-core §3).
With ATK ≥ 56 power 3.66 starts to give 1+; the general threshold is `ATK/2 > 2·power²`.

## Fields (actor `A = 0x76b970 + i*0xba4`)

| name | off | type | meaning | tag |
|---|---|---|---|---|
| state | `+0xa54` | s32 | 8 intro, 0 walk, 2 leap | [obs] |
| timer | `+0xa58` | s32 | tick in the state | [obs] |
| shed | `+0xa5c` | s32 | 0..299 power-shed timer; reset by an absorb; frozen in state 8 | [obs] |
| wait | `+0xa60` | s32 | state 0's random rest, 63..190 | [obs] 168, 117 |
| cycle | `+0xa64` | s32 | 0..599 blue-slime spawn cycle | [obs] |
| power | `+0xab8` | f32 | 5 / 3.66 / 2.32 / 1 | [obs] |
| size | `+0xabc` | f32 | see above | [obs] |
| yaw | `+0x420` | f32 | set once per state-0 cycle; NOT toward the player | [obs] |
| death | `+0xaf0` | s32 | 0..120 once `+0x428 == 2` | [obs] |
| absorb flash | `+0xb90 / +0xb94` | s32 | −1 / 30 on an absorb | [obs] |

## Per-tick order (alive)

1. First tick ever: power = 5 **[obs]** f9861: 5 (1.0 at f9836 before the first tick).
2. **Blue-slime loop** over every type-2 actor with `+0x428 != 0`:
   - age (`+0x398`) ≥ 600 and ≥ 3 away (horizontal): the blue slime is slid 0.2 per tick toward this boss;
   - age ≥ 600, within 3, |dy| < 2, not dying: **absorbed** — blue `+0x428 = 0`; boss power += 4 (cap 5), shed timer = 0,
     `+0xb90 = −1`, `+0xb94 = 30`, shake + SE.
   **[obs]** bcore-1 f12555–12563: slime at age 670 gone, power 2.32 → 5. bcore-2 f13087 (undone branch): age 605, already within 3
   → gone on the spot, power 1 → 5, shed 1. bcore-4 ~f10699 / ~f10715: both bosses absorb one each at age ~610–626.
3. State 8 → handle and return (no shed, no spawn).
4. **Shed:** `++shed >= 300` → 0; if power > 1: power −= 1.34 (floor 1), ring of 10 particles, SE 0x16e.
   **[obs]** ~f10376 5 → 3.66, ~f10680 → 2.32 (bcore-4); ~f12794 2.32 → 1 (bcore-2). Hit-stop frames do not tick the timer.
5. Size eases toward 0.15 + 0.15·power.
6. **Spawn cycle:** the cycle counter runs 0..599. While fewer blue slimes exist than the cap (dungeon id 2 → 3, 3 → 4,
   4 → 5, else 1; dying ones count), reaching 10 spawns one type 2: up to 30 tries of (angle, radius 2..10) around
   (−40, −40), first one > 10 from the player, at y 10; flagged `+0xb2c = 1` (EXP 1). At the cap the counter waits below 10.
   **[obs]** bcore-4 f10086: two spawned (one per boss) at cycle 10; bcore-2 f13082: one at cycle 10. Blue slime: 57 HP at L14, DF 4.
7. `+0xb20 = 1`.
8. State 2 or state 0 (below).

## States

| id | name | duration | what happens | exit |
|---|---|---|---|---|
| 8 | intro | 240 | placed at y 15, drops; shake + wobble at 30; `lock == 0` (no control) throughout | → 0 at 240 **[obs]** f9836 → 10076 |
| 0 | walk | wait + 60 = 123..250 | t 1: yaw = toward a random point 8 from the player (`rand·2π`), wait = `(rand & 0x7f) + 63`. t 1..30 still. t 31..69: creeps along yaw (accel 0.015, ~3 units) with hops (vy 0.3) at t 32 / 48 / 64. Then still | t ≥ wait + 60 and on the ground → 2 (in the air: restart 0) **[obs]** 168 → 228, 117 → 177 |
| 2 | leap | 61 | t 1: vy 1. t 1..19: horizontal velocity = (player − boss) · 0.025 per tick (homing), then ×0.95 decay. Peak y 7.64 at t 16. **t 31: the landing record** (type 14) + shake + wobble, boss still at y 2.8. On the ground from t 36 | t > 60 → 0 **[obs]** per-tick table f12981–13044 |

RNG arms: only state 0's t 1 (target angle, wait) and the spawn point. No other choice: the pattern is walk → leap → walk.

### The leap and the landing

- Travel: ~42 % of the takeoff distance toward where the player stands (3.23 → 1.87) **[obs]**. Yaw does not change.
- Landing record: stays at the boss's x / z of t 31, reach 2.0 → **hits a player within 4.0 horizontally**, record ages
  1..15 (t 31..45) **[code]** for the duration; **[obs]** hit at 1.87 on t 31 (19 damage, `p_hurt` 70, `p_inv` 30, thrown to 7.08);
  not hit at 4.23 during t 31..35.
- No contact damage: type 23 spawns no contact record **[code]**; **[obs]** over 1,700 idle ticks (bcore-1, bcore-4) HP only fell on landings
  (one 2-point hit near a blue slime).
- Hittable in the air only while `y < 0.2 + 2.4·size` (0.92 at power 1, 2.36 at power 5) **[code]**; **[obs]** hit at t 35 (y 0.04).
- **Counter window:** state 2 is an attack state (1..6), so every hit during it carries "Counter Attack!" ×1.2. Practical
  part: t 35..60 on the ground, 26 ticks. The swing's damage tick is `p_actt == 9` → press `A` at t 26..51 from outside 4.0.
  At power 1 the sword reaches 4.08: stand at 4.0..4.08, or use the spin (reach 5.58, 30 ticks of invulnerability).

### Death

- HP < 1 → `+0x428 = 2`; drop roll; (20 + level/2)·0.8 EXP orbs **[code]**.
- Death t 1: slow-mo 60 (`0x438becc`), `p_inv` = 60 if another type 23 lives, else 1200. t 60: if it was the last of its
  kind, every live actor starts dying. t 61..: a burst every 8 ticks. t 120: slot freed.
- **[obs]** bcore-2: kill f13216; `p_inv` 1200; blue slime (slot 5) gone by f13326; death timer 120 and exit `gate` 3 → 0 at f13386.
- First of two: `p_inv` 60 only, nothing else happens **[code]**; **[obs]** (dungeon2-21f f12403: the floor went on).
- Dungeon 1 only: kill flags at work bytes `0x2bd38 / 0x2bd39` (second kill suppresses the drop) **[code]**.

## Dungeon 2, 25F: two crown slimes

- Placement **[obs]** f9836: actors 0 and 1 at (−45, 15, −34) and (−35, 15, −34), state 8; player at (−40, −45); arena centre (−40, −40).
- They run in lockstep at first (same timers; both leap at f10313–10314) and drift apart with their random waits **[obs]**.
- Interactions **[code]** unless marked:
  - one shared blue-slime count against the cap of 3; each boss spawns its own (two at once at f10086 **[obs]**);
  - every old blue slime is pulled by BOTH (0.2 each); whichever it comes within 3 of absorbs it (**[obs]** f10700 / f10715: one each);
  - no collision, targeting or healing between the two;
  - the first death does not clear the floor.
- Timeline without intervention **[obs]** bcore-4: power 5 until f10386, 3.66 until f10686, 2.32 for ~25–50 ticks, absorb → 5.
  So an unmanaged fight offers ~40 ticks of real damage per 600. That is where the earlier probe's 20–33 hits came from.

## What the earlier probe saw, explained

| claim | cause | tag |
|---|---|---|
| "plain hits 1–3, labelled hits 20–33" | not the labels: power ≥ 3.66 vs 2.32 at the time of the hit | [obs] |
| "a small slime vanished at ~f12555" | absorbed by the survivor (age ≥ 600, within 3) | [obs] bcore-1 |
| "no labelled hit after ~f12550" | power back at 5 → every hit 1–2 for the next 600 ticks, then absorb again; the words still appear | [obs] f12598: Side word, 1 damage |
| "a type 2 spawns when one dies" | the 600-tick spawn cycle, not the death | [obs] |

## Kill recipe

Names (define per run): `c_pow c_st c_t c_size c_x c_z c_yaw c_use` on the boss, `sN_use sN_age sN_hp sN_x sN_z` on each type-2
actor, `rx rz c_rel c_gap` as in combat-core §3 / §6.

1. **Blue slimes first, always.** Scan the table for type 2 with `+0x428 == 1`. Kill each before age 600 (57 HP at L14: two
   side hits, or one spin). They spawn at boss-cycle 10, ≥ 10 away from the player, near the arena centre. Goal
   `sN_use == 2`, avoid `sN_use == 0 or c_pow > 4`, score `-hypot(px - sN_x, pz - sN_z)`.
2. **Wait out the power** if it is above 1: 300 ticks per step, no absorbs. Stay > 4.0 from each landing (walk away during
   the leap: `c_st == 2`). Nothing else it does can hurt.
3. **Hit at power 1** (2.32 is worth it too): from behind (`abs(c_rel) > 2.75`), during state 0's still phases the yaw and
   position are fixed for ≥ 60 ticks. Swing every 30 ticks: 37–44 per back hit. Positioning goal
   `abs(c_rel) > 2.75 and c_gap < -0.3 and p_act == 0 and c_st == 0`.
4. **Around a leap:** be > 4.0 from the landing point until t 45, or exactly at 4.0..4.1 and swing so that
   `p_actt == 9` falls in t 35..60 (counter). The spin (5 SP) hits from 5.5, ignores the landing, doubles the damage.
5. Search form that killed it (112 HP in 129 frames, 30 s, `-j 4`): goal `c_use == 2`, avoid `hp < 60 or c_pow > 4`, cell
   `(int(a0hp/12), int((px-c_x)/1.5), int((pz-c_z)/1.5), p_act, c_st)`, score `-a0hp - max(c_gap, 0)*3`, actions 8 walks
   `"DIR*6"` + 8 swings `"DIR A .*6"` + `".*6"`.

**Done live:** `bcore-2`, from the stuck state (f12513, survivor 311 / 380 at power 2.32, two blue slimes alive) to the kill at
**f13216 = 703 frames**, exit open at f13386; Louie HP 89 → 89, SP 22 → 12 (two spins). `rd verify`: MATCH (f13616,
sem 34467c40273e9a1e). Steps: spin kills the old blue slime + 43 on the boss (f12549) → back hit 26 (f12642) → wait, power 1
at f12794 → front hit 26 (f12812) → counter + side 38 after a leap (f13015) → spin kills the second blue slime at age 606 +
66 on the boss (f13087) → sword hits found by search, 112 HP (f13216). From a fresh floor the cost is the 900 ticks of shedding plus ~11
back hits per boss.

## Arms a thorough probe should exercise

| # | arm | how to reach | state check | done live |
|---|---|---|---|---|
| 1 | intro (state 8, shake at 30) | enter the floor | `c_st == 8`, `lock == 0`, 240 ticks | yes |
| 2 | first-tick power 5 | — | `c_pow == 5` | yes |
| 3 | state 0: yaw / wait roll | — | `c_t == 1`: yaw, `+0xa60` change | yes |
| 4 | state 0 hops at t 32 / 48 / 64 | — | `c_y > 0` | yes |
| 5 | state 0 → 0 (in the air at the deadline) | needs wait such that a hop is airborne at t = wait + 60: hops end by ~t 75, wait ≥ 63 → likely unreachable | — | no |
| 6 | leap with homing (player within ~10) | stand near | `c_st == 2`, lands ~42 % closer | yes |
| 7 | landing record hits the player | within 4.0 at t 31 | `hp` drop 17–19, `p_hurt == 70` | yes |
| 8 | landing record misses | > 4.0 | `hp` flat | yes |
| 9 | shed 5 → 3.66 → 2.32 → 1 | keep blue slimes dead 900 ticks | `c_pow` | yes |
| 10 | shed at power 1 (no-op branch) | 300 more ticks | `c_shed` wraps, power 1 | yes (~f13140: timer 76 at f13216, power still 1) |
| 11 | blue-slime spawn at cycle 10 | count < cap | new type 2, y 10 | yes |
| 12 | spawn held at the cap | 3 blue slimes alive (two bosses) | cycle waits < 10 | no |
| 13 | spawn retry (point within 10 of the player) | stand at the arena centre at cycle 10 | spawn ≥ 10 away or the 30th try | no |
| 14 | pull of an old blue slime | let one reach age 600 far away | blue x / z step 0.2 | yes |
| 15 | absorb (power +4, cap 5) | let it arrive | blue `+0x428` 1 → 0, `c_pow == 5` | yes |
| 16 | absorb at power 1 (→ 5) vs at 2.32 (→ 5, capped) | — | — | both |
| 17 | absorb blocked: blue slime dying or |dy| ≥ 2 | kill it inside the 3 radius / mid-hop | stays until the death frees it | no |
| 18 | hit in state 0, each label (none / side / back) | §3 | popup kinds 0 / 12 / 11 | yes |
| 19 | hit in state 2 (counter) | t 35..60 | kind 13 | yes |
| 20 | hit in state 8 | impossible while `lock == 0`? (a hire's lingering record) | would stagger: `+0xb20` not yet set | no |
| 21 | hit at power 5 / 3.66 (minimum 1) | — | 1–2 | yes (5) |
| 22 | crit, magic skill (state 5) on it | level ≥ 9 skill | kind 10; (physical + magic)/2 with MD 10·power² | no |
| 23 | first death of two (`p_inv` 60) | kill one | floor continues | yes (dungeon2-21f) |
| 24 | last death: slow-mo, blue slimes die at 60, exit opens at 120 | kill the last | `gate` 3 → 0 | yes |
| 25 | drops / EXP orbs / dungeon-1 kill flags | dungeon 1's crown slime floor | work bytes `0x2bd38 / 39` | no |
| 26 | caps 1 / 4 / 5 (other dungeons) | dungeons 1 / 3 / 4 | blue count | no |
