# Arma — type 41 (アルマ)

**code-read**: IDA `sub_425C3F` (AI, 0x425c3f), `sub_423789` (the walk-and-align helper, shared with Tielle's kobolds
and the black knight), `sub_425BFF` (on-damage hook from `sub_42E791`), `sub_445A8C` cases 160..163 / 156 (record
spawn geometry), `sub_42F7AD` case 41, `sub_450443` default arm (floor 49), `sub_450877` (floor-49 timeline).
**Port (2026-11-13): AI `actor_ai_arma` (FUN_00425c3f, from IDA + disasm; floor 99's prop FUN_0044eff5 still a stub),
voices `arma_voice` (FUN_00498699), on-damage hook, death arm (shared with Griff's case 28), d4 floor-49 setup / timeline
(`dungeon4_stage_setup`, `d4_floor49_event`). Unverified: no recording yet.** Shared rules: `boss-floor-common.md`.

FUN_00498699(0)'s switch falls through (asm 0x4986b1..0x4986cc): pick 0 plays 09ar_01, 02 and 03, pick 1 plays 02
and 03, pick 2 plays 03. The state-0 finisher trigger does not end the tick: the rest of state 0 (charges, the
line-up) still runs and can replace state 5 with an attack the same tick; the finisher's abort (goal > 25) returns
before the anim tail.

## 1. Where

| place | `dungeon` / `floor` | level → HP (305·(1 + 0.2·lv)) |
|---|---|---|
| Lapis Ruins 50F | 4 / 49 | 34 → **2379** |
| dungeon 5 floor 53 | 5 / 53 | 89 → 5734 |
| Lapis's Boss Rush, 8th floor | 5 / 53, `0x438b4d0 == 4` | 49 → 3294 |

- d4 setup (`FUN_00450443` default arm, floor 49): `FUN_0044cc80(1)`; if bank+0x2bd13 == 0 or mode 2: party at
  (−40, 0, −52 − 1.3k), Arma = `FUN_0041f903(−40, 0, −34, 41)`, `+0xa54 = 8`; else event stage 7 (no boss).
- d5: generic placement at (−40, 0, −34), state 8.
- Also a **prop** on Lapis floor 99 (AI `FUN_0044eff5`: `+0xb04 = +0xb08 = 1`; removed at the timeline's stage 4) —
  not a fight.

d4 floor-49 timeline (`FUN_00450877`, stage `s32(0x438b8a0)`, tick `s32(0x438b8a4)`):

| stage | what |
|---|---|
| 0 | locked. ticks 60..119 `lock = 4`. tick 140: event 12/6 (mode ≠ 2; **no skip byte**). tick 150: camera class 2, partner hold, `0x9643128 = 1`, banner. tick 270 → stage 3 |
| 3 | `lock = −1`. No actor with `+0x428` outside {0, 2} → bank+0x2bcf6 = 1 → stage 4 |
| 4 | locked; tick 180 fade 120; then party re-placed → stage 5, fade in 30 |
| 5 | tick 30: event 12/7 (mode ≠ 2); bank+0x2bd13 = 1; tick 60 → stage 7 |
| 7 | `lock = −1`; from tick 21 both portals rise |

## 2. Stats

Table (baked): HP **305**, EXP 30, AT 20, DF 20, MA 0, MD 10, boss flag 0; rf0 0.6 → radius 1.2, half-height 1.2.
No `+0xb54`: hits stagger her (40), knock her back and reset `+0xa54 / +0xa58 / +0xa5c` to 0.

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`

Fields: sub `s32(B+0xa5c)`; hit clock `s32(B+0xa64)` (++ every AI tick); state-0 clock `s32(B+0xa68)`; charges
`s32(B+0xa74)`, `s32(B+0xa78)`, `s32(B+0xa7c)`; side `s32(B+0xa84)`; **untouchable countdown `s32(B+0xb08)`** (−1 per
AI tick while > 0); phase `s32(B+0xae4)` is read but never written here (0).

| state | length | what |
|---|---|---|
| 8 intro | 250 | t == 250 → 0 (every dungeon; on d4 the event starts the fight at event tick 270) |
| 0 hunt | — | below |
| 1 volley | 60 | t == 1: `rand & 7 == 0` → voice. t < 3 anim 0, else 3. **t 7, 15, 23, 31: record 160** (8 shots each), SE 695. t ≥ 60 → 0 |
| 2 lob | 60 | t == 1: one `rand`, voice. **t == 7: record 161**, SE 385. t ≥ 60 → 0 |
| 3 spread | 43 | t == 1: `rand & 7 == 0` → voice. **t 7, 11, 15, 19: record 162**, SE 384. t ≥ 43 → 0 |
| 4 fans | 63 | t == 1: one `rand`, voice. **t 7, 17, 27, 37: record 163** (8 shots each), SE 677. t ≥ 63 → 0 |
| 5 finisher | ≤ 40 + 229 | `+0xb08 = 1` every tick; anim 5. Sub 0: side = 2 if `pz ≥ −40` else 1 (latched); goal = (player x, −55) for side 2 / (player x, −25) for side 1; **goal farther than 25 → abort to state 0**; spring `v = 0.97·v + 0.0097·Δ`, and for t > 30 `v ·= 1 − 0.01·(t − 40)` (literal: > 1 for t 31..39); t ≥ 40: sub = 1, yaw = π (side 1) / 0 (side 2). Sub ≥ 1 (++ each tick): alpha 255, anim 4; sub 2: voice; **sub 12: record 156**, SE 368; sub ≥ 230 → 0 |

**State 0**, each tick, in order:

1. `+0xa68++`. **If the player's knock-down counter `s32(0x56daeb0) > 60` and `+0xa68 ≥ 60` → state 5** (`+0xa68`,
   t, sub, side = 0). The counter is 70 right after a hit by records 14 / 18 / **161** (30 after others): her own
   lob (state 2) opens the finisher, during the 9 ticks the counter is above 60.
2. `+0xa74++`, `+0xa78++`, `+0xa7c++`. `want` = 3 if `+0xa7c > 360`, else 2 if `+0xa78 > 240`, else 1 if
   `+0xa74 > 180`, else 0 (thresholds `30·(12 / 8 / 6 − phase)`). **If `t == 1` (read before the helper's ++):
   `want = rand & 3`** — one tick per walk leg picks at random.
3. Range by `want`: 0 → 20, 1 → 18, 2 → 25, 3 → 15. `FUN_00423789(a, range, 5, 0.03, 5, 30, 0)`:
   - t++; **t == 1**: `+0xa60 = rand & 63` (leg length jitter); heading: Δ = player − Arma (negated when closer
     than 3), keep the dominant axis; up to 20 tries of {yaw = (rand & 3)·π/2; `rand & 1` → yaw = along that axis;
     accept if a 2-unit probe is clear}; then, distance ≥ 2 and `rand & 3 == 0` → yaw = (rand & 3)·π/2.
   - t ≥ 5: **aligned** = (|dx| < 1.5 and 2.0 < |dz| < range) or (|dz| < 1.5 and 2.0 < |dx| < range) → returns 1,
     yaw = at the player along the axis, `+0xa5c = 0`.
   - t ≥ 6: accelerate 0.03/tick along yaw; t − 5 ≥ 30 + `+0xa60` → t = 0, `+0xa5c++`.
4. Helper returned 1: `want` 3 → state 4 (`+0xa7c = 0`); else yaw = atan2 to the player, and `want` 2 → state 2
   (`+0xa78 = 0`), 1 → state 1 (`+0xa74 = 0`), 0 → state 3. t = 0.

So: default answer to "player on my axis, 2..20 away" is the 4-shot spread (state 3); the volley, lob and fans
come due after 180 / 240 / 360 state-0 ticks (fans beat lob beats volley), each with its own reach.

## 4. Being hit (`sub_425BFF`, on every damage application)

```
if (+0xa64 >= 60) { +0x440 = 0; +0xa64 = 0; +0xb08 = 10; }   // fresh hit: no stagger, 10 untouchable AI ticks
else              { +0xa60++;  +0xa58 = 0;  +0xb08 = 30; }   // inside 60 AI ticks: stagger stays, 30 ticks
```

The generic hit code first sets `+0x440 = 40` and state 0. While staggered the AI does not run and `+0xb08` does not
count down → a non-fresh hit = 40 (stagger) + 30 AI ticks untouchable; a fresh one = 10. (`+0xa60` is also the
helper's jitter field; nothing reads it as a counter here — unlike Griff there is no counter move.)

## 5. Attacks (spawn geometry from `sub_445A8C`; o = facing octant·π/4, origin = Arma + 1.2·(sin o, cos o), y + 1.3)

| record | per call | direction | speed | mult (+0xb4) | radius (+0x108) | notes |
|---|---|---|---|---|---|---|
| 160 volley | 8, start delays 0..7 ticks | o + (rnd − 0.5)·0.3 | 0.5, vy −0.1 + 0.01·rnd | **0.125** | 0.15 | 32 shots per state 1 |
| 161 lob | 1 | o | 0.12, vy +0.35 | 1.0 | 0.3 | knock-down 70 → enables state 5 |
| 162 spread | 1 | o + (rnd − 0.5)·π/2 | 0.3 | 0.5 | 0.8 | 4 per state 3 |
| 163 fans | 8, start delays 0..7 | o + π·(0.0025·(k/8) + 0.015·((k & 7) − 4)), k = 0..7 → −0.19..+0.14 rad | 0.25 | 0.5 | 0.3 | 32 per state 4 |
| 156 finisher | 1 | o | (1.8 — speed or scale, not resolved) | — | — | sub 12 of state 5 |

Physical (AT 20·mul): 160..163 are not in the "magic" list; 156 not checked. What each record does in flight
(gravity, lifetime, pierce): records tick, not read.

## 6. When she can be damaged

`s32(B+0xb08) == 0 and s32(B+0x428) == 1`: states 0..4 and 8, except 10 / 30 AI ticks after each hit and the stagger
of a non-fresh hit; never in state 5 (≈ 270 ticks).

## 7. Kill recipe

1. Wait `s32(B+0xa54) != 8` (and on d4 `s32(0x438b8a0) == 3`).
2. Cell: `(s32(B+0xa54), s32(B+0xb08) > 0, s32(B+0xa64) >= 60, int(f32(B+0x42c))//80, int(px − f32(B+0x3f0)), int(pz − f32(B+0x3f8)))`;
   score `-f32(B+0x42c)`; avoid `hp <= 0`.
3. Every attack needs the player on one of her axes at 2..25: approach **diagonally** and stand inside 2.0 or off
   both axes by ≥ 1.5 → she only walks. Her shots leave along the facing octant: after she commits (state 1..4,
   yaw frozen) step off the axis and hit from the side / back (×1.2 / ×1.5, plus ×1.2 for state 1..6).
4. A landed hit cancels her attack (state → 0) and, when fresh, costs no stagger: hit when `s32(B+0xa64) >= 60`,
   again 10 AI ticks later (that one staggers her 40 and locks 30), then wait for `s32(B+0xb08) == 0`.
5. Never get hit by the lob (161): `s32(0x56daeb0) > 60` with `s32(B+0xa68) >= 60` → the finisher. If state 5 starts,
   put > 25 between the player's x-line goal and her (the abort test is on the goal (player x, −55 / −25)) or leave
   the line she will face (yaw π / 0, i.e. along z).
6. Done: `s32(B+0x428) == 2`; d4 → stage 4.

## 8. Arms checklist

| branch | predicate |
|---|---|
| intro | `s32(B+0xa54) == 8` for 250 AI ticks |
| walk leg + jitter | `s32(B+0xa54) == 0`, `s32(B+0xa60)` 0..63 at t 1, `s32(B+0xa5c)` counts legs |
| flee heading (closer than 3) | player within 3 at t 1 → moves away |
| spread (want 0) | `s32(B+0xa54) == 3` |
| volley (want 1) | `s32(B+0xa54) == 1` with `s32(B+0xa74)` reset |
| lob (want 2) | `s32(B+0xa54) == 2` |
| fans (want 3) | `s32(B+0xa54) == 4` |
| random want at t == 1 | an attack other than the spread before its charge is due |
| finisher, side 1 / 2 | `s32(B+0xa54) == 5`, `s32(B+0xa84)` 1 / 2 |
| finisher abort | state 5 → 0 at sub 0 with the goal > 25 away |
| fresh hit / non-fresh hit | `s32(B+0xb08)` 10 / 30 right after; `s32(B+0x440)` 0 / 40 |
| death d4 (stage 5 walk-over, stage 7 shrink) / d5 (fade from tick 60) | `s32(B+0x428) == 2` |
| already beaten | bank+0x2bd13 = 1 → `s32(0x438b8a0) == 7`, no type 41 |

## 9. Unresolved

- Records 156, 160..163 in flight; 156's `1.8`.
- Whether the AI runs during the stage-0 dialogue on d4 (state 8's 250 ticks vs the event's 270).
- `FUN_00498699(n)` / the `09ar` voice lines: cosmetic, but their `rand` draws are in the RNG stream (listed per
  state above).
- `+0xae4` for type 41: no writer in the AI; if the enemylist variant ever sets it the thresholds shift by 30/step.
