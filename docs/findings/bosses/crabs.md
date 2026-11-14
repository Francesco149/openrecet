# The crabs — types 68 (カニ) / 69 (黒カニ) (FUN_00422092) + hit parts (FUN_0043865e)

**code-read**: port `src/actor_ai.c:1612..1897` against IDA `sub_422092` (all_ida.c:21427, read in full — the
port matches, quirks included); hit parts / flip / angle gate in port `src/record_hit.c:446..504, 577..582`
(FUN_0043865e actor arm); bombs FUN_004293f0 + the fuse arm of FUN_0042f7ad (port actor_ai.c:518, :1369); death arm
`sub_42F7AD` case 68/69. Shared rules: `boss-floor-common.md`.

## 1. Where

| type | place | `dungeon` / `floor` |
|---|---|---|
| 68 | dungeon 2 20F | 2 / 19 |
| 68 | Boss Rush (dungeon 2's), 3rd floor | 5 / 35, `0x438b4d0 == 2` |
| 69 | dungeon 5 floor 57 (list `f:58`) | 5 / 57 |

- Generic boss floor (FUN_0044cc80 / FUN_0044d0a4). Placed at (−40, 0, −34), state 8.
- Intro: pad locked 240 event ticks; the crab's state 8 ends when the **event tick** ≥ 240 (anim 0, anim 12 from 80).
- End: crab frees ≈ death tick 132 → portals, `s32(0x438b8a0) == 1`.

## 2. Stats, parts

- Table: HP 150, AT 20, DF 30, MA 0, MD 10 (code-read, "both types"). **Live type 69 row (`0x5c23f0 + 69·0x68`): HP 150, +0x30 25, AT 40, DF 30, MA 20, MD 30** (`lapis-81f-90f`); type 68 not re-read.
- d4 f89 (Lapis 90F): level 42 → **HP 1410** (live), jab ≈ `ftol(40·9.4/2 − DF/4)` ≈ 167 vs Louie DEF 84.
- d2 f19: level 12 → **HP 510**, AT·mul 44. Rush f35: level 16 → HP 630, AT·mul 52.
- **`+0xab8 = 5.0` every tick outside state 8 → DF_eff = 30·25 = 750**; state 6 sets 1.0 → DF_eff 30.
- `+0xb54 = 1`: no knock-back, no stagger, state never reset by a hit.
- Seven hit volumes (FUN_0043865e, table DAT_005c5314 = {−1, 0, 1, 2, 3, 6, 7}), tested in this order:

| m | centre | radius | damage |
|---|---|---|---|
| 0 | body (+0x3e4..) | 4.5·rf0 (68: 4.5; 69: 5.85) | only from the front arc (below) |
| 1..6 | model anchors 0, 1, 2, 3, 6, 7 (+0x6fc + 12k; 6 / 7 = the pincers) | half the body's | **0** ("no hit": SE 359, sparks 41 / 42) |

- The parts have **no HP**; they are shields. A record that reaches the body volume counts as a body hit (m = 0 wins).
- Body hit counts only when `|hit_angle| < 0.9425` (54° either side of its facing); else 0 damage.
- **Flipped (state 6, `+0xa5c == 1`): every volume, every angle takes damage.**

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`

Each tick: `d` = angle-to-player − yaw (wrapped ±π), `dist` = distance. Velocity ×0.9 a tick. **It turns only in
state 3.**

| state | length | what |
|---|---|---|
| 8 intro | to event tick 240 | → 0 |
| 0 walk | ≤ 120 | tick 1: side `+0xa60` = 2 if d > 0.157, 1 if d < −0.157, else 0 (2 rands are drawn and discarded: retail overwrites the roll with 0 — IDA 21648..21653). Walks forward / yaw − π/2 / yaw + π/2 at accel 0.02. Tick 60: `dist < 15` and |d| < 0.9425 → decide. Tick 120 → decide. |
| 1 / 2 jab (pincer 6 / 7) | 130 (69: 110) | anim 4 / 5; SE 702 at 20; **ticks 25..39: record 14 at the pincer anchor every tick**. End: `rand & 1` and dist < 15 and |d| < 0.9425 → the other jab; else state 0 |
| 3 turn + slam | 95 | anim 6; tick 35: SE, the turn to the player is fixed; ticks 35..64 yaw eases onto it; tick 75 shake + SE; **ticks 75..79: record 14 at both pincers**; tick 95 → 0, `+0xa64 = 0` |
| 4 spray | 160 | anim 12; ticks 20..99: 1 unit draw each; **ticks 51..99: record 38** at yaw ± π/20 (49 shots), SE every 4th |
| 5 bubbles | 200 | anim 12; **ticks 20..99: record 43 from anchor 5** (80 bubbles), overlay 82, SE 703 at 20 |
| 6 flipped | ≈ 60 + 180 + 120 | see below |
| 7 | 0 | decide at once (no writer found) |

Decide:

| situation | draws | result |
|---|---|---|
| (dist < 7.5 and |d| < 0.9425) or the tick-60 trigger | `rand & 3`, then `rand & 1` | 3/4 a jab (odd → 1, even → 2); 1/4 bubbles (5) |
| player outside the front arc | `rand & 1`, (`rand & 1`) | 1/2 turn + slam (3); 1/2 bubbles (5) |
| front arc, dist ≥ 7.5 (tick 120) | `rand & 1`, `rand & 1` | 1/2 spray (4); 1/2 bubbles (5) |

Bombs (state 0 only): `+0xa64` counts −60 → 60 (120 state-0 ticks); at 60, if fewer than 2 (69: 3) actors are in use
**in total** (the crab counts) → one bomb, type 37 (69: `rand & 3` → 37 / 39 / 38 / 40), at a point 2..10 from the
centre, > 10 from the player, y 10. So a lone type 68 keeps exactly one bomb on the floor.

Flip (state 6) — entered from FUN_0043865e: **a record of type 18 (a bomb blast) reaching the body volume, any
angle, while state ≠ 6** → `+0xa58 = +0xa5c = +0xa60 = 0`, state 6:

| `+0xa5c` | length | what |
|---|---|---|
| 0 launch | until y ≤ 1.0 after tick 40 (vy 1.51, gravity 0.05) | `+0xb08 = 1` (unhittable); splash effects |
| 1 on its back | **180** | `+0xb08 = 0`, DF_eff 30, all volumes / angles hit, ×1.2 state bonus; anim 9 from 60 |
| 2 get up | 120 | anim 11, `+0xb08 = 1`; → state 0 |

Records:
- 14, owner 68 / 69: at the pincer anchor, reach 1.15·rf0 (68: 1.15, 69: 1.495) → within ≈ 3.15 / 3.5 of the pincer;
  2 ticks each. Physical: `ftol(AT·mul/2 − DF/4)`, knock-down 70.
- 38 (spray), 43 (bubbles: random direction, speed 0.1 (69: 0.15), lobbed, pop on landing): "magic" → 1–2.

Bombs (types 37..40; table HP 15, DF 20 — live type-37 row: HP 15, AT 16, **DF 16**, MA 18, MD 10; level 12 → 51 HP, level 42 → 141): `+0xb98 = 1`. Killed → `+0x428 = 2` and the fuse
`+0xaf0` starts at 240 (red 37, yellow 40) / 360 (green 39) / 480 (blue 38) and **blows at 540** (300 / 180 / 60
ticks later). A dying bomb can still be hit and pushed; moving faster than 0.01 it blows at once within 1.5 of another
live actor or on a wall ahead. Blast = record 18, flag 3 (hits actors and the player), reach 4, one tick:
- vs the crab: body volume if within 4 + 4.5 = **8.5 of the crab's centre** (69: 9.85) → flip.
- vs the player: within 6; 1–2 damage, knock-down 70.
- Alive, a bomb drifts toward the player and lobs record 90 from < 15.

Death: 1 slow-mo; 60 sweep; bursts to 119; anim 14; fade from 100; frees ≈ 132.

**Engine quirk (state 6, `+0xa5c == 0`):** the splash position is `flt_52B90C[2·(+0xa64)]`, an 8-float table,
but `+0xa64` holds the bomb clock (−60..59) → retail reads neighbouring .data. Cosmetic only.

## 4. When it can be damaged

- Normal states: front-arc body hits only, DF 750 → **1 (+0/1) per hit**. Useless.
- **State 6, `+0xa5c == 1`: 180 ticks**, DF 30, any angle (back ×1.5), ×1.2. This is the fight.
- Unhittable: launch and get-up (`+0xb08 == 1`).

## 5. Kill recipe (search)

- Names: `kst = s32(B+0xa54)`, `ksub = s32(B+0xa5c)`, `kt = s32(B+0xa58)`, `khp = f32(B+0x42c)`; the bomb = the
  other in-use slot (type 37): `bhp`, `bst = s32(Bb+0x428)`, `bfuse = s32(Bb+0xaf0)`, its x / z.
- Loop:
  1. Wait for a bomb (crab alone, in state 0, ≤ 120 state-0 ticks). The crab does not turn outside state 3 — stay
     out of its front arc (|d| > 54°) to avoid jabs; then it rolls slam / bubbles.
  2. Kill the bomb (51 HP: `ftol(ATK/2 − 5)` per hit → 2–3 hits) **within 8.5 of the crab's centre**, or kill it and
     knock it toward the crab (it blows on contact within 1.5 of an actor — the crab's body radius is 4.5, so the
     fuse path is the reliable one). Red: blast 300 ticks after the kill.
  3. Step > 6 from the bomb before `bfuse == 540`.
  4. `kst == 6 and ksub == 1`: attack from behind for 180 ticks. Per hit `ftol(ATK/2 − 7.5)·1.2·1.5`
     (ATK 54 → 36). 510 HP ≈ 15 hits ≈ 2–3 flips.
- Search goal per stage: `bst == 2` near the crab; then `kst == 6`; then score `-khp` with cell
  `(int(khp/10), ksub, kt//15, int((px − kx)/1.5), int((pz − kz)/1.5))`, avoid `hp < 30`.
- A crab already flipped ignores further blasts (state ≠ 6 gate); a blast during launch / get-up is wasted.

## 6. Arms checklist

| arm | predicate |
|---|---|
| intro | `kst == 8` until `s32(0x438b8a4) >= 240` |
| walk forward / left / right | `kst == 0 and s32(B+0xa60) == 0 / 1 / 2` |
| tick-60 trigger | `kst` leaves 0 at `kt == 60` |
| tick-120 decide | `kst` leaves 0 at `kt == 120` |
| jab 6, jab 7 | `kst == 1`, `kst == 2` |
| jab chain | `kst` 1 → 2 or 2 → 1 with `kt` back to 0 |
| turn + slam | `kst == 3`; yaw changes over `kt` 35..64 |
| spray | `kst == 4` |
| bubbles | `kst == 5` |
| each attack hits the player | `hp` drops in that state |
| bomb called | new slot type 37 with `+0xb2c == 1` (69: 38 / 39 / 40 too) |
| no bomb while one exists | count stays 2 |
| front-arc body hit (1–2 dmg) | `khp` falls by 1–2 with `kst != 6` |
| hit outside the arc / on a part (0 dmg) | hit SE with `khp` unchanged |
| flip by blast | `kst == 6`, `ksub == 0`, `s32(B+0xb08) == 1` |
| on its back, part hit damages | `ksub == 1`, `khp` falls from a pincer-side hit |
| get-up | `ksub == 2`, `s32(B+0xb08) == 1` |
| blast hits the player | `hp` drops 1–2, `s32(0x56daeb0) == 70` |
| bomb blows on contact (rolling) | bomb frees before `bfuse == 540` |
| death + sweep | `s32(B+0x428) == 2`; others dying at `+0xaf0 == 60` |
| player dies | `hp <= 0` |

Port: AI, parts, flip, bombs ported; no PORT-DEBT tag. State 6's splash read (2026-11-11): the port
mirrors retail's .data VA 0x52b72c..0x52baeb (240 dwords, `k_drop_bits` in `actor_ai.c`; dword 120 = the table) so
`flt_52B90C[2c]`, c = −60..59, is in bounds and retail-valued — every IDA xref into that range is a read; unverified
by recording for c outside 0..3. Death arm unverified by recording, matches IDA.

## 7. Live: Lapis 90F (`lapis-81f-90f`, 2026-11-14)

Louie L46 (ATK 64, DEF 84, HP 224); fight f21572..25309; **0 damage taken**. Full timeline in the probe.
- Intro: state 8 → 0 at etick 240 (arrival + 241). The bomb clock `+0xa64` was 0 (not −60) on the first state-0 tick → the first bomb
  came at state-0 tick 60.
- Bombs: 39, 39, 37, 38, 40, 40 (all four), never more than 2 alive (crab + 2 = the cap 3). Fuses matched the card: green ≈ 185, blue
  65, yellow ≈ 343 from the kill. A red blew 186 ticks after the kill (pushed into contact), and its blast lit the blue next to it.
- **Blasts damage the crab** (not in § 3/§ 4): flips in states 1 / 2 (jabs) did 0 damage; flips in states 0 / 5 did −179 / −290; a blast
  on the already flipped crab (state 6, sub 1) did −210 and later −119+ (the kill), with no re-flip. 4 blasts ≈ 800 of 1410 HP. Not
  yet read in code (record 18 vs the body volume's damage path).
- Flip timing: launch (sub 0, `unhit` 1) 58 ticks; on its back (sub 1) 150..200 ticks; get-up (sub 2, `unhit` 1) 95 ticks; `+0xa5c`
  stays 2 in the following state 0.
- Sword while flipped: front 31–32, side 36–38, back 45–49. Normal states: front arc 1–2, outside 0. One hit on the first state-0 tick
  after the get-up did 27 (DF scale still 1 that tick).
- Arms seen: walk, decide at tick 60 / 120, jab 6 ×4, jab 7 ×2, jab chain 1 → 2, turn + slam ×3, bubbles ×3, flip ×4, death (slot
  freed 175 ticks after HP 0 → `estage` 1). **Not seen:** spray (4), state 7, any attack landing on the player, blast on the player.
- Search recipe that worked: flip goal = `st == 6` and within 8 of the crab (score: bomb lit, bomb-to-crab distance, flipped bonus
  minus distance); damage goal = −130 HP with `--apply-best` (never "or sub 2": waiting satisfies it); get-up goal = `st != 6`, score
  = distance to a point 9 behind the crab. `--avoid "hp < H − 6"`, `-j 2`, 60–120 s each, 13 searches.
