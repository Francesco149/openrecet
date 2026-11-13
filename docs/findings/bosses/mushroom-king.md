# Mushroom king — types 24 / 59 / 60 (FUN_0042b6b7)

**code-read** unless tagged; **[obs fNNNN]** = seen live in retail, run `mk-arms` (2026-11-11, `rd`, from
`dungeon1-deep@56444`, Louie L11 ATK 26 / DEF 35 / MDEF 12, HP 52, SP 19; f56444..f60920, `rd verify` MATCH — see §7).
**[branch]** = seen live on a checkpoint branch that is not in the final movie. Code-read from the port `src/actor_ai.c:2609..2955` (= FUN_0042b6b7, IDA `sub_42B6B7`
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

| | table HP / AT / DF / MA / MD | level | HP | AT·mul | DF_eff (`+0xab8`²) |
|---|---|---|---|---|---|
| 24, d1 f9 | **150 / 24 / 42 / 10 / 42** [obs] | 6 [obs] | 330 [obs] | 38.4 | ftol(42·1.5²) = 94 |
| 24, rush f31 | same | 7 | 360 | 40.8 | 94 |
| 59 | not read live (the card's earlier 150 / 20 / 30 was wrong for 24) | by floor | | | DF·2² |
| 60 | not read live | by floor | | | DF·3² |

- **Correction (retail .data, `0x5c23f0 + 24*0x68`, read live):** flags 1, HP 150, EXP 25, AT 24, DF 42, MA 10, MD 42,
  rf 0.8 / 4 / 3 / 4.5. The card had AT 20 / DF 30 / MA 0. Confirmed by damage: swing 10–11 = ftol(24·1.6/2 − 35/4) + 0..1
  (AT 20 would give 7); spray 5 = ftol(10·1.6/2 − 12/4) (MA 0 would give 1–2).
- Mushrooms [obs]: type 13 (red) HP 10 → 22 at L6, AT 14, DF 0; type 14 (purple) HP 20 → 44, AT 20, DF 0; rf 0.8 / 1.5 / 5 / 2.
  Both level 6, `+0xae4 == 2`. Side / back labels apply to them (yaw π): 13 / 17 / 17 from one swing; a crit kills a red in one.
- No parts. Hit radius 4.5·0.8 = 3.6 (sword reach 6.6 facing it). The player's walk is blocked at ~3.25–3.45 from its centre [obs].
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

Pick (state 0, each tick once both bits are set — a declined roll re-rolls next tick). [obs] ×7: the state changes on the
tick after `+0xa58` reads 120. `+0xa50` (nearest eligible mushroom) is written even when the eat roll declines [obs f57997].
A **stay** leaves `+0xa58 = 0`, `+0xa5c = 3`: it rolls again on the very next tick (not after a new 121-tick wander)
[obs f60024 stay → f60025 eat].
1. nearest mushroom with `+0x398 >= 600` and > 4 away exists **and** `rand & 1 == 0` → state 2;
2. else `rand % 20 == 0` → state 3;
3. else `rand & 3 == 0` → stay (`+0xa58 = 0`: new wander target next tick, and the pick rolls again on that tick);
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
  "magic" → **5 at L6 vs MDEF 12 [obs f59068]** (MA is 10, not 0: `ftol(10·1.6/2 − MDEF/4)`; the card's 1–2 was wrong).
- **Dead arm:** `DAT_056db108 > 100` (player bound) → state 0 jumps straight to state 1. Its only writer is record
  type 39 (FUN_0043865e), and no call in the IDA export spawns a 39 → unreachable as far as read.
- **Stale meal:** the walk in state 2 times out at `+0xa5c == 180` and falls into the `+0xa70` test with the previous
  meal's value → a repeat power-up / heal without eating (possible only after a first meal; spray clears it).

Observed timings [obs]:
- Plant on `+0xa64 == 10`: f56453, f56633, f56813 (every 180); a fresh plant has `+0xb08 = 1` (seen at age 28), 0 later.
  Never more than 4 alive; a 5th only after a death / meal (f58898..f59440: 4 alive, next plant after slot 6 died).
- Approach: the king arrives **on the first tick** when the player is within ~7.5 at the pick (re-aim 3.5 short, arrive < 4)
  [obs f57997]; it then swings from where it stands (reach 8.5).
- Swing record on `+0xa58 == 25`; the player takes 10–11, `p_hurt` 70, `p_inv` 30, 4 frames of hit-stop [obs f56660].
  One swing ends at 75 [obs f57069], two at 114 [obs f56753].
- Eat walk lengths seen: 87, 55, 73, 47, 113 ticks; the king stops with its centre ~2.1 from the mushroom.
  Tick 38 frees the mushroom and sets `+0xa74`; tick 48 copies to `+0xa70`; `+0xa70` is NOT cleared by state 4 / 5: it
  stays 1 / 2 until the next meal's tick 48 or a spray's end [obs f57724 = 1, f58260 = 2, f59366 = 0 after the spray].
- State 4: the transition tick (`+0xa58 == 0`) still has `+0xb28 = 0`, `+0xab8 = 1.5`; DF 0 from tick 1 [obs f57401 / f57402].
  **Hit-stop freezes the king's timers** (4 frames per hit, 8 on a crit), so the window is 120 king ticks, not 120 frames.
- `+0xa60` is reset only by pick → approach: after an eat / spray / stay chain the contact record is live from wander tick 1
  [obs: 122 at f57193, 157 at f57568, 484 at f59695].
- Spray: the first record (tick 40) hit the player standing 4.2 in front: **5 damage**, `p_hurt` 30 [obs f59068]. Ends 209.

Death (`+0x428 == 2`, `+0xaf0`): anim 18; 1 slow-mo; 60 cinematic camera, body to (−40, 0, −40), records cleared,
every other live actor starts dying; 300.. fades 2/tick; **frees at 500**.
[obs] HP 0 + `+0x428 = 2` at f60376; `+0xaf0 = 1` at f60381 (after the 4 hit-stop frames): slow-mo 60, `p_inv` 1200;
while the slow-mo runs the death tick advances once per 3 frames (`+0xaf0 = 21` at f60441); 60 at f60480: body at (−40, −40),
mushrooms `+0x428 = 2` and free one tick later; alpha 253 at 300, 0 at 499; 500 at f60920: slot free, `estage = 1`,
`gate` 3 → 0, `s32(0x9643128)` 1 → 0, exit portal at (−35, −40).

## 4. When it can be damaged

- Always (`+0xb08` never set). Never staggers. [obs] `a0unhit` 0 and `a0stag` 0 on every read.
- **At ATK < 49 every hit outside state 4 does 1–2** (ftol(ATK/2 − 94/4) ≤ 0 → min 1, + rand & 1) [obs ATK 26: 1 or 2 on each of ~8 hits].
  The labels still show (Back 11 + Counter 13 at f57317; Counter 13 alone from the front in state 1 at f58109).
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
- Hits: per hit `ftol(ATK/2 − 23)` (DF_eff 94, not 68: ATK 54 → 4; ATK 26 → min 1); ×1.5 back, ×1.2 in states 1..5;
  state 4: `ftol(ATK/2)` base (ATK 26 → 13, ATK 54 → 27).
- **[obs] the kill at ATK 26 is the purple windows only** (330 HP + one 55 heal = 385 dealt: 375 in three windows, 10 chip):
  stand 4.1–4.5 straight behind the eating king (`krel > 3.0`), press `A` when `kst == 2 and kt >= 112` (lands at state-4
  tick 2), then mash the spin (`B`, 5 SP) while `p_act == 0 and kst == 4`, sword when SP < 5:
  `{A if (a0st == 2 and a0t >= 112) else 0}*14 .*20 ({(B if sp >= 5 else A) if (p_act == 0 and a0st == 4) else 0} .)x70`.
  Lands at state-4 ticks 2 / 35 / 71 / 107. Per window: 93 (sword, spin, side sword; one press lost), 149 (sword, 2 spins,
  sword), 178 (sword + 3 spins; the last overkills).
  Damage seen, back + counter: sword 23, 23, 24, 25 (13 → 15 → 22, + 0..3); spin 47, 48, 51, 52, 54, 54 (26 → 31 → 46, + 0..8);
  side + counter sword 19 (15 → 18, + 0..2). All inside combat-core §3.
- Kill the reds (1 hit after a pre-hit, 2 fresh; careful: one swing reaches every mushroom within 3.6 of the point 1 ahead),
  leave the purples; steer the pick with a short `rd search` (goal `a0st == 2 and k50 == N`): RNG moves with the swings.
- Swing safety [obs]: stay > 8.6 from its centre while `kst == 1 and 20 <= kt <= 72`; within ~7.5 at the pick there are only
  24 ticks to get out (1.2..2.5 units: fine unless mid-swing → do not start a swing at wander `kt >= 100`).

## 6. Arms checklist

Names (`rd`): `kst = a0st`, `kt = a0t`, `a0sub = s32(B+0xa5c)`, `k50 / k60 / k64 / k70 / k74 = s32(B+0xa50 / a60 / a64 / a70 / a74)`,
`kb28 = s32(B+0xb28)`, `kdt = s32(B+0xaf0)`, `estage = s32(0x438b8a0)`. Frames = run `mk-arms` (§7).

| arm | predicate | retail |
|---|---|---|
| intro | `kst == 8` then `kst == 0` at `kt` wrap | **[obs f56203..f56443]** in the movie's played prefix, read in the side run `mk-die` (§7): `kst 8`, `kt` 0..239, `kst 0` at f56443; `mk-arms` itself starts at f56444 (`kst 0, kt 1`, event tick 241) |
| mushroom planted (13 / 14) | a new slot with `s32(+0x424) in (13, 14)` and `s32(+0xae4) == 2` | **[obs f56453]** (`k64 == 10`; again f56633, f56813, ...): type 14 HP 44 / type 13 HP 22, `+0xae4 == 2`, `+0xb08 == 1` while rising |
| mushroom cap (4) holds | count of 13/14 stays ≤ 4 over a 180-tick cycle | **[obs f58898..f59440]** slots 3 / 4 / 5 / 6 alive across two or more `k64 == 10` ticks, no 5th; the next plant only after slot 6 died |
| wander, contact record | `kst == 0 and s32(B+0xa60) > 60` | **[obs f57568]** `k60 157`, player 3.25 from the centre at `krel` 1.96: hp 41 → 40, `p_hurt` 30, `p_inv` 30, popup kind 5. Again ~f59690 (−1) |
| pick → eat | `kst == 2` | **[obs f57193]** at full HP 330 (`k50 1`, purple); f57658 (`k50 3`, red); f58237; f59366; f59695; f60025 |
| eat: mushroom gone before tick 38 | `kst == 2`, `s32(B+0xa74) == 0` at `kt == 39` → `kst == 0` at 120 | **[obs f59484]** slot 6 killed during the walk (f59440, `a0sub` 70): `kt 38`: `k74 1 → 0`; `k70` 0 at 48; **f59566** `kst 0`. Again: slot 3 killed at `kt 6` (f59747) → `kst 0` at f59903 |
| eat normal → heal | `kst == 5`; HP rises by max/6 at `kt == 40` | **[obs f57836 → f57876]** `k74 2` at 38 (f57754), `k70 2` at 48, `kst 5` at f57836, HP 236 → 291 (+55) on the tick `kst` returns to 0, popup kind 1 value 55 |
| eat purple → power-up | `kst == 4`, `s32(B+0xb28) == 1` | **[obs f57401]** (`kb28 1`, `a0pow 1.0` from f57402 = tick 1); f58428; f60257. Hits: f57403 23 (back + counter), f57439 51 (spin), f57478 19 (side + counter) |
| eat walk timeout (stale meal) | `kst == 2 and s32(B+0xa5c) >= 179 and kt == 0` | **not reached**: walks seen 47..113 ticks. Needs a > ~20-unit walk (king 0.11 / tick) to the nearest aged mushroom; the arena is ~35 across and plants are within 12 of the centre. The precondition holds, though: `k70` stays 1 / 2 between meals |
| spray | `kst == 3` (5 % per pick) | **[obs f59028]** (found by `rd search` in 7 s, RNG steered with swings); first record `kt 40` f59068: player 4.2 in front takes 5, `p_hurt` 30; ends at 209 → `kst 0` f59241; `k70 1 → 0` |
| stay roll | `kst == 0 and kt == 0 and a0sub == 3` at a tick's end (the card's "`kt` restarts at 1" is never visible: the pick runs again on that same tick) | **[obs f60024]** (`rd search`, 10 s), `k50 5`; next tick f60025 → eat |
| approach timeout | `kst == 1 and kt == 0 and s32(B+0xa5c) > 120` | **[obs f56994 → f56995]** `a0sub 120`, king 4.93 from its target, player 20.7 away → `kt 1`, `a0sub 0` (one swing, in the air) |
| one swing / two swings | `kst == 1 and kt > 0 and s32(B+0xa5c) == 0` / `== 1` | **[obs f56995..f57069]** one (ends 75); **[obs f56636..f56753]** two (ends 114); again two from f57999 |
| three lunges (type 60 only) | `s32(B+0xa5c) == 2` | not reachable on type 24 |
| swing hits the player | `hp` drops while `kst == 1` | **[obs f56660]** `kt 25`, player 7.39 from the centre: hp 52 → 41, `p_hurt` 70. Edge **[branch from f57997]**: walking in during ticks 25..40: no hit at 8.517, hit at 8.343 (`kt 35`, 40 → 30) → reach + 2 = 8.5 holds |
| player dies | `hp <= 0` | **[branch, run `mk-die` f57775]** swing at `kt 25` takes the last 9 HP; `s32(0x438be98) = 1`; `dlg = 1` at f57898. Not in the `mk-arms` movie (hp stays ≥ 40) |
| death, minions swept at 60 | `s32(B+0x428) == 2`, `s32(B+0xaf0) == 60` | **[obs f60376]** dying; **[obs f60480]** `kdt 60`: body to (−40, −40), mushroom slot 8 `+0x428 = 2`, free at f60481 |
| floor opens | `s32(0x438b8a0) == 1` | **[obs f60920]** `kdt 500`, `estage 1`, `gate 0`, exit (−35, −40), `lock −1` |

Extra arms seen: pause-menu food mid-fight **[obs f58564..f58893]** (Walnut Bread hp 40 → 51, Candy sp 4 → 15, Taiyaki sp 15 → 19;
page flow: `~`, `D`, `A` = Items with the cursor on the first item; `A` = Use / Throw Away popup, `A` = use; `A` dismisses the
message); hit labels on the boss: back + counter (kinds 11 + 13), side + counter (12 + 13), counter alone from the front (13).
Not exercised: a front hit in state 0 (no label), a crit on the king.

## 7. Observed run `mk-arms` (2026-11-11)

`rd new mk-arms --from dungeon1-deep@56444`; Louie L11, ATK 26 / DEF 35 / MATK 8 / MDEF 12, HP 52 / 52, SP 19 / 19.
f56444 (control) → f60376 king HP 0 (3,932 frames) → f60920 exit open (4,476 frames). `rd verify mk-arms`: MATCH (fresh headless replay,
unskipped retail: frame 60920, rng d4344b5e, sem 9e1c2e91095bdce2). Not filed.

| frames | what |
|---|---|
| 56444..56753 | wander; plant f56453 (purple, slot 1); approach 71 ticks, two swings, hit taken at f56660 (−11) |
| 56844..57069 | pick f56874 → approach; the player runs to the east wall: timeout at f56995, one swing in the air |
| 57193..57529 | eat purple (slot 1) at full HP; chip hit f57317 (1, Back + Counter); window 1: 329 → 236 |
| 57568 | contact record (−1) |
| 57658..57876 | eat red (slot 3) → heal 236 → 291 |
| 57997..58116 | approach (arrives at once), two swings dodged at 9.9; front hit in state 1 f58109 (2, Counter only) |
| 58237..58564 | eat purple (slot 4); window 2: 289 → 140 |
| 58564..58893 | pause menu: Walnut Bread, Candy, Taiyaki |
| 58898..59241 | `rd search` → spray f59028; hit by its first record f59068 (−5) |
| 59366..59566 | eat red (slot 6) picked; slot 6 killed during the walk → no meal, state 0 |
| 59695..59903 | eat red (slot 3) picked; killed at eat tick 6 → no meal; slot 7 (red) killed too (a crit, 8 frames of hit-stop) |
| 60024..60025 | `rd search` → stay roll, then eat purple (slot 5) |
| 60257..60376 | window 3: 133 → 0 (25, 54, 47, 52) |
| 60381..60920 | death arm; floor opens |

Budget: the king took 385 (330 + the 55 heal): 375 in the three DF-0 windows, 10 in chip. The player lost 19 HP
(11 swing, 5 spray, 3 × 1 contact), ate +11; ends HP 44, SP 4; spins used 6 (30 SP, 15 from food).

Side run `mk-die` (`--from dungeon1-deep@56200`, not verified, not filed; it reaches f56444 with the same rng b60e4bc7 as
`mk-arms` starts from, so the intro frames are those of the `mk-arms` movie's prefix):
- Intro: f56203 floor 8 → 9, king type 24 placed at (−40, 0, −34), `kst 8`, `kt 0`, `lock` −1 → 0 at f56204; `s32(0x9643128) = 1`
  seen at event tick 31 (f56234); `kst 8 → 0` when `kt` reaches 240 (f56443); `lock = −1` at event tick 241 (f56444).
- Player death: walking into the king from f56444 (`{dpad(a0x - px, a0z - pz)}`): contact 2, 2, 2, 1, 1, 2, 1 (each `p_hurt` 30,
  `p_inv` 30, blocked at 3.2 from the centre), swings 11, 10, 11, 9 → 0; **hp 0 at f57775** (`kst 1, kt 25`),
  `s32(0x438be98) = 1`, `p_act 1` held; `dlg = 1` (the defeat event) 123 ticks later, f57898. 1,331 ticks from control to death.

Port: all arms ported, no PORT-DEBT in this function. Types 59 / 60 share the code (the type-60 lunge arm has no
recording).

## 8. Type 59 **[obs]** (Lapis Ruins 10F; run `lapis-10f`, 2026-11-12; probe `docs/findings/probes/lapis-1f-10f.md`; `rd verify` MATCH f40626)

Port: `lapis-1f-10f` identical + save (2026-11-12); type 59 needed no AI change (d4 events `FUN_00450877` ported).

Also at **dungeon 4 / floor 9** (Lapis 10F), not only d5 f47 (§1). Louie L42 ATK 61 / DEF 68 / MDEF 42.

- **Stats (retail .data, `0x5c23f0 + 59*0x68`)**: flags 1, HP 150, EXP 25, **AT 26, DF 42, MA 10, MD 42**, rf 0.8 / 4 / 3 / 4.5. (Type 60
  read too: 150 / 25 / **28 / 52 / 10 / 52**, same rf.) Level 26 → **HP 930** **[obs f31212]**.
- **`+0xab8` = 2.0** from the tick after placement (1.0 on the entry tick f31212): DF_eff = ftol(42·4) = 168 → every hit outside state 4
  is min 1 (+0..1) at any ATK below 84·… (ATK 61 base ftol(30.5 − 42) < 0). State 4: `+0xab8` 1, `+0xb28` 1 → base ftol(61/2) = 30.
- **Intro** identical to 24: state 8 t 0..239 (f31212..31451), state 0 f31452, `lock` −1 f31453; `s32(0x9643128)` 1 by event tick 60.
- **Two swings end at kt 99** (24: 114): f36028 kt 1 (`ksub` 1) → record 14 live kt 25..38 and 56..69 (**14 ticks each**, the card said 16)
  → state 0 at f36126 (the tick after kt 98). One-swing end (60) not observed.
- Heal (state 5) at full HP: f32434, HP stays 930 (capped). A later red meal healed 875 → 930 (cap).
- Purple windows: 5 seen (meal tick 70 → state 4 50 ticks later). Spins from behind in state 4 (back + counter): **120, 114, 109** (f33954 /
  33994 / 34034) = ftol(30·2·1.2·1.5) = 108, +0..21. A spin pressed at state-4 kt ≈112 lands after the window: 2.
- Kill f36852 (state 4 kt 12, spin); death tick 1 f36857 (slow-mo 60); tick 60 f36956: 3 mushrooms dying → gone f36957; **tick 500 f37396:
  slot freed and `estage` 0 → 2** (not 1 — d4's `FUN_00450877` floor-9 arm with bank+0x2bcf4 = 1 skips event 10/4), `gate` 3 → 0 f37397,
  `s32(0x9643128)` 0. Circle (−35, −40), door (−45, −40).
- Not seen on 59: spray (state 3), the stay roll, contact record, a swing hit on the player, the eat-walk timeout. What differs from 24 and
  was seen: `+0xab8` 2, swing end 99, HP / AT / DF / MA / MD above. Mushroom planting (`rand % 3`: 0 → purple) not counted.

## 9. Type 60 **[obs]** (Lapis Ruins 60F; run `lapis-60f`, 2026-11-13; probe `docs/findings/probes/lapis-51f-60f.md`)

Louie L44 ATK 62 / DEF 82. Dungeon 4 / floor 59 (also d5 f55, §1).

- **HP 1230** = 150·(1 + 0.2·36) **[obs f24158]**; `+0xab8` 1 on the entry tick, **3.0** from the next (DF_eff ftol(52·9) = 468): outside state 4
  every hit is min damage. State 4: `+0xab8` 1, `+0xb28` 1 from t 1.
- Intro state 8 t 0..239 (f24158..24398), state 0 f24398.
- **Two swings (`ksub` 1) end after kt 113 → state 0 at 114** (= type 24; 59 ended at 99) **[obs f24972..25085, f25326..25439, f26953..27066]**.
  Records at kt 25 / 56; player ≥ 8.4 away each time: no hit.
- Red meal at full HP → heal (state 5, f25775 → 25815), HP capped.
- Purple windows (state 4 from behind, back + counter): spins **121, 119, 124, 114 / 117, 131, 111, 128 / 115** = ftol(31·2·1.8) = 111, +0..20;
  swords 56, 58, 36. Kill f27463 (state 4); **death tick 500 at f28007**: slot freed, `estage` 0 → 1.
- **Event 12/4 skipped** when bank+0x2bcf7 is already 1 (`FUN_00450877` floor-59 arm): `estage` stays 1, no dialogue; portals were raised by
  `FUN_0044d0a4(1)` as usual.
- Not seen on 60: three lunges (`ksub` 2), spray, stay roll, contact record, a hit on the player, the eat-walk timeout.
