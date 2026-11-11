# Griff — type 28 (グリフ)

**code-read**: IDA `sub_424B42` (AI, 0x424b42; phase compare checked in disassembly 0x424ba8..0x424bd2), `sub_42460A`
(the on-damage hook, called by `sub_42E791`), `sub_42F7AD` case 28, `sub_44E08F` cases 29 / 49, the dungeon-3 arm
inline in `sub_4427F1` (IDA 44146..44345). **Port (2026-11-11): AI `actor_ai_griff` (`src/actor_ai.c`), death arm 28 / 41, d3 floor-29 setup / timeline (`griff_floor_setup` / `griff_floor_event`, `src/dungeon_flow.c`) — verified `obsidian-21f-30f-griff` (triage identical + save, ticks 1..39976). Floor-49 AI arms (phase 2, record 23, the 0-flag bats 15 / 35 / 55) ported UNVERIFIED; floor 49's timeline and the +0x2bce2 / +0x2bcf8 beats: PORT-DEBT.**
Shared rules: `boss-floor-common.md`. The hand of the 50F fight: `demon-hand.md`. **Run 2026-11-11:** run `griff-30f` (probe
`docs/findings/probes/obsidian-21f-30f.md`; Louie L40, ATK 56 / DEF 58 / MDEF 32): 30F fought and killed, `rd verify` MATCH at
f39676. Arms marked **[obs fNNNN]** in §9; retail numbers in §10 override the code-read ones above where they differ.

## 1. Where

| place | `dungeon` / `floor` | with | setup / event |
|---|---|---|---|
| Obsidian Tower 30F | 3 / 29 | — | `FUN_0044e08f` case 29: Griff at (−40, 0, −35), `+0x428 = 1`, `+0xa54 = 8`, pointer `DAT_06a470a0`; party at (−40, 0, −48 − 1.3k); portals (−35, 0, −40) / (−45, 0, −40) sunk. Skipped (event stage 8) when bank+0x2bce5 ≠ 0 and mode ≠ 2 |
| Obsidian Tower 50F | 3 / 49 | hand 72 | case 49: the same, party at z −56 − 1.3k; the hand is spawned by the event at tick 30. Skipped when bank+0x2bce7 ≠ 0: stage 8, one portal at (−40, 0.03, −40) |
| dungeon 5 floor 42 (d3 Boss Rush 5th) | 5 / 42 | — | generic placement (−40, 0, −34), `FUN_0044d0a4(1)` |
| dungeon 5 floor 45 (d3 Boss Rush 8th) | 5 / 45 | hand 72 | generic + `FUN_00450ee0` case 45 |

### d3 timelines (event stage `s32(0x438b8a0)`, tick `s32(0x438b8a4)`; `lock` 0 = pad locked)

Floor 29:

| stage | what |
|---|---|
| 0 | locked. tick 120: event 7/11 unless bank+0x2bce4 ≠ 0 or mode 2. ticks 60..99: `lock = 4` (scripted walk). tick 150: 0x2bce4 = 1, camera class 2, partner hold, `0x9643128 = 1`, banner. **tick 270 → stage 3, tick 0** |
| 3 | `lock = −1` (the fight). Griff's state 8 ends on the first AI tick here. When no actor has `+0x428` other than 0 or 2 (dying ones do not count): bank+0x2bce5 = 1 → stage 4 |
| 4 | locked. tick 180: fade out 120. After the fade: party re-placed, anim 6, stage 5, fade in 30 |
| 5 | tick 30: event 7/12 (mode ≠ 2). tick 60 → stage 7 |
| ≥ 7 | `lock = −1`; from tick 25 both portals rise |

Floor 49: stage 0 — ticks 0..279 a camera pan (look z = min(−30, 0.125·t − 50)); **tick 30: hand = `FUN_0041f903(−35,
20, −25, 72)`, `+0xa54 = 8`, pointer overwrites `DAT_06a470a0`**; ticks 45..129 `lock = 4`; tick 150: event 7/7 unless
bank+0x2bce6 ≠ 0 / mode 2; tick 160: 0x2bce6 = 1, camera, banner; tick 280: adventurer's line (`FUN_0044cbe4`) → stage 3.
Stage 3 as above (sets bank+0x2bce7). Stage 4: tick 180 fade; stage 5: tick 30 event 7/8; tick 45: bank+0x2bcf8 = 1,
`deepest[3]` / best time written, floor = 0, stage type 20 loaded (event 7/14, then out of the dungeon).

## 2. Stats

- **Retail (read live, `0x5c23f0 + 28*0x68`): HP 200, EXP 25, AT 35, DF 30, MA 40, MD 30; rf 0.6, 1.5, 2, 2; flags 0.**
  (The code-read "HP 150, EXP 30, AT 20, DF 20, MA 0, MD 10" was wrong.) Boss flag 0 (a normal-class record: it staggers and
  is knocked back in phases 0 / 1) **[obs]**.
- d3 f29: lv 23 → **HP 1120** **[obs f31614]** (`+0x434` 1120, `+0xae0` 23). With the retail table: d3 f49 lv 28 → 1320; d5 f42
  lv 70 → 3000; f45 lv 75 → 3200; d3 Boss Rush (floors 42 / 45) lv 32 / 36 → 1480 / 1640 (computed, not run).

## 3. Phase — `s32(B+0xae4)`, recomputed every AI tick

| floor | phase 0 | phase 1 | phase 2 |
|---|---|---|---|
| `floor == 49` (Obsidian 50F only) | HP ≥ 70 % | 30 % ≤ HP < 70 % | HP < 30 %: once: effects 26 / 27 / 28, shake 32, SE 694; `+0xb54 = 1` every tick (no stagger / knock-back / state reset); aura effects every 3 / 32 ticks |
| any other floor | HP ≥ 30 % | HP < 30 % | never |

(0x424ba8: `0.3·max` vs HP; on floor 49 `jbe` → the 0.7 test; phase 1 = `threshold > HP`.) Phase 2 is latched only by
this test: it is re-evaluated each tick, so it holds while HP < 30 %.

## 4. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)` (++ at the top of every AI tick)

Fields: sub `s32(B+0xa5c)`; counter-attack request `s32(B+0xa60)`; hit clock `s32(B+0xa64)` (++ every AI tick);
phase clock `s32(B+0xa68)`; guard countdown `s32(B+0xa6c)`; dash charge `s32(B+0xa74)`; fan charge `s32(B+0xa78)`;
teleport side `s32(B+0xa84)`; **untouchable `s32(B+0xb08)`** (cleared at the top of every AI tick, then set by the
branches below); alpha `s32(B+0x3dc)`.

Order inside a tick: phase → `+0xb08 = 0` → clocks → **counter** (if `+0xa60 > 0`, replaces the state switch) → guard
countdown → state switch.

| state | length | what |
|---|---|---|
| 8 intro | d5: 240; d3: until event stage 3 | idle |
| 0 roam | — | see below |
| 1 slash | 38 | t < 3 anim 0, else 3. **t == 7: record 22** (phase 2: **record 23**), SE 343. t ≥ 38 → 0 |
| 2 dash | 85 | `+0xb08 = 1` throughout. t == 1: velocity 0, anim 4, SE 378. t < 10: turns to the player 0.1/tick. t == 40: SE 675. **t == 50: record 21**; t 50..59: yaw = octant·π/4, velocity 0.8 along it (8 units), trail effect 30. t ≥ 85 → 0 |
| 3 fan | 148 | **t == 7: record 152** (5 shots: octant direction, ∓0.38, ∓0.56 rad, speed 0.3), SE 343. t ≥ 148 → 0 |
| 4 detonate | 30 | every tick: each live type-6 actor → **record 151 at the bat**, the bat is freed (`+0x428 = 0`, `+0xad0 = 0`), SE 331. t ≥ 30 → 0 |
| 5 vanish + swarm | 60 + 70 | `+0xb08 = 1` throughout, anim 5. Sub 0: alpha 0; side = 2 if `pz ≥ −40` else 1 (latched in `+0xa84`); spring toward (−40, −50) (side 2) / (−40, −30) (side 1): `v = 0.97·v + 0.0097·Δ`, damped from t 40; **t == 60: sub = 1, yaw = π (side 1) / 0 (side 2)** — it faces the player's half. Sub ≥ 1 (++ each tick): alpha 255; sub 12: SE 368; **sub 12..59: one bat per tick** (48 in all); sub ≥ 70 → 0 |

**Bats** (state 5): `FUN_0041f903(x, 0, z, 6)` at Griff, `+0xb2c = 1`, `+0xae4 = 3` (the drift AI `FUN_00424509`), yaw =
Griff's yaw + (rnd − 0.5)·0.942, `+0x9fc = 1` except the bats of sub 30 and 45 (phase 2: also 15, 35, 55) which get 0
(the field's meaning: not read). One `FUN_00471089` draw per bat (+ `FUN_0041f903`'s `rand`). The actor table has
128 slots; a failed alloc is skipped.

**State 0** (each tick):

1. Phase ≥ 1: `+0xa68++`; `== 120·(3 − phase)` (phase 1: 240, phase 2: 120) → **state 5**; `== 780 − 120·phase`
   (660 / 540) → **state 4** and `+0xa68 = 0`. So a swarm, then its detonation 420 state-0 ticks later.
2. `+0xa74++`, `+0xa78++`.
3. `t % 60 == 1` (i.e. t == 1; t wraps at 35): pick a heading — target = player + 4·(sin, cos)(2π·rnd); 20 tries of
   {yaw = (rand & 3)·π/2; `rand % 3 != 0` → yaw = toward the target's dominant axis; accept if a 4-unit probe
   (`FUN_00433674`) is clear}.
4. t ≥ 10: choose the attack: `want` = 2 if `+0xa78 > 30·(8 − phase)`, else 1 if `+0xa74 > 30·(6 − phase)`, else 0.
   Reach 18 and minimum 1.5 for want 1 / 2; reach 4, minimum 0 for want 0. **Aligned** = (|dx| < 1.5 and min < |dz|
   < reach) or (|dz| < 1.5 and min < |dx| < reach), dx / dz = player − Griff. Aligned and on the ground
   (`f32(B+0x3f4) == f32(B+0x3e0)`): yaw = at the player along that axis, `t = 0`, and want 2 → state 3 (`+0xa78 = 0`),
   want 1 → state 2 (`+0xa74 = 0`), want 0 → state 1.
5. t ≥ 6: accelerate 0.03/tick along yaw; t ≥ 35 → t = 0.

Charge thresholds (state-0 ticks): dash > 180 / 150 / 120, fan > 240 / 210 / 180 (phase 0 / 1 / 2). The fan wins
when both are due.

## 5. Being hit — the counter (`sub_42460A`, on every damage application)

```
if (+0xa64 >= 60) { +0x440 = 0; +0xa64 = 0; }      // "fresh" hit: the stagger just set (40) is cancelled
else              { +0xa60++;  +0xa58 = 0; }        // a second hit inside 60 AI ticks: counter requested
+0xb08 = 1;
```

- Before that, the generic hit code (phases 0 / 1, no `+0xb54`) has set `+0x440 = 40`, knock-back, and reset
  `+0xa54 / +0xa58 / +0xa5c` to 0 — **every landed hit cancels Griff's current attack** (not in phase 2).
- While staggered the AI does not run, so `+0xb08` stays 1: **untouchable for the whole stagger**.
- Counter (first AI tick with `+0xa60 > 0`): anim 6, `+0xb08 = 1`; **t == 4: record 14 at Griff** + effects 87 / 90
  / 88, shake 10, SE 365, rumble 32; `+0xa6c = 60` every counter tick; t > 20 → state 0, `+0xa60 = 0`.
- Guard: while `+0xa6c > 0` (60 AI ticks after the counter) `+0xb08 = 1`.
- `+0xa64` only resets on a fresh hit and only counts AI ticks; it starts at 0, so **the first hit of the fight
  inside Griff's first 60 AI ticks already counts as "second"**.

Net: fresh hit (damage, no stagger, 1 untouchable tick) → a second hit within 60 AI ticks (damage, 40-tick
stagger, then a 20-tick counter burst and 60 guard ticks = ~120 ticks untouchable, after which the next hit is
fresh again). Hits ≥ 60 AI ticks apart are all fresh and never trigger the counter.

## 6. Attacks

| attack | record | timing | notes |
|---|---|---|---|
| slash | 22 (phase 2: 23) — **3 records** per call **[obs f32260]**, 1 ahead of Griff along the octant, reach 1.5 | state 1 t 7 | physical: Louie took 45 (ftol(35·3.3/2 − 58/4) = 43, +0..8) |
| dash | 21, direction = octant | state 2 t 50; body moved **11.6** units (−35.75 → −47.36; v 0.656 then ×0.82 decay) **[obs f35051]** | Griff untouchable for all 85 ticks **[obs]**; Louie took 44 |
| fan | 152 ×5, speed 0.3, `+0xa8 = 20` | state 3 t 7 | **retail MA is 40**: the 5 rings (y 0.25, reach 0.7) home on the player and erupt at age 120: Louie took **66** and **61** (ftol(40·3.3/2 − 32/4) = 58, +0..11) **[obs f32703, ≈f35700]** |
| bat blast | 151 at each bat (reach 1.0) | state 4 | bats also bite on their own: each carries contact record 153 (magic, MA 15): Louie took 16 **[obs f≈36928]** |
| counter burst | 14 at Griff, reach 2.0 | counter t 4 | knock-down 70; physical: Louie took 43 **[obs f35198]** |

Record reach / lifetime / the 22-vs-23 difference: records table, not read here.

## 7. When it can be damaged

`+0xb08 == 0`: states 0, 1, 3, 4 and 8 outside stagger / counter / guard. Never during states 2 and 5, the stagger
after a second hit, the counter, and the 60 guard ticks. Predicate for "open": `s32(B+0xb08) == 0 and s32(B+0x428) == 1`
(read it on the tick you swing; it is rewritten each AI tick).

## 8. Kill recipe

1. `rd` state: B = Griff's actor (d3: the floor's first allocation; `s32(B+0x424) == 28`). Wait for
   `s32(B+0xa54) != 8` (d3: `s32(0x438b8a0) == 3`).
2. Cell: `(s32(B+0xa54), s32(B+0xb08), s32(B+0xa64) >= 60, int(f32(B+0x42c))//40, int(px − f32(B+0x3f0)), int(pz − f32(B+0x3f8)))`;
   score `-f32(B+0x42c)`; avoid `hp <= 0`.
3. Rhythm: two hits per window — the first when `s32(B+0xa64) >= 60`, the second right after; then retreat > the
   burst's reach until `s32(B+0xa6c) == 0`. Or one hit per 60 AI ticks to never see the counter.
4. Stay off its axes (|dx| ≥ 1.5 and |dz| ≥ 1.5) to deny every attack choice; it then only roams. Step onto an axis
   within 4 to bait the 38-tick slash (hit it from the side: slash direction is the octant).
5. Phase ≥ 1: at `s32(B+0xa68)` 240 (phase 1) it vanishes and reappears on the far side of z −40 from where the
   player stood, facing the player's half; 48 bats fly out in a ±27° cone — be off that line. Kill bats before the
   detonation (`+0xa68` 660): every surviving bat becomes a record 151.
6. 50F phase 2 (HP < 30 %): no stagger and no attack cancel any more; the counter still fires.
7. Done: `s32(B+0x428) == 2`; at death tick 60 every other live actor (hand, bats) starts dying; d3 → event stage 4.

## 9. Arms checklist

Run `griff-30f` (d3 f29). `rd` names (run-local `vars.json`): `gst gt gsub gctr ghc gpc gguard gdash gfan gside gph gunt gstag
galpha guse ghp gx gz gyaw gdie` = `B+0xa54 a58 a5c a60 a64 a68 a6c a74 a78 a84 ae4 b08 440 3dc 428 42c 3f0 3f8 420 af0` with
B = 0x76b970 (actor 0); `est etk` = `0x438b8a0 / a4`.

| branch | predicate | status |
|---|---|---|
| intro release (d3 / d5) | `s32(B+0xa54)` 8 → 0 at `s32(0x438b8a0) == 3` / at t 240 | d3 **[obs f31885]** (`est` 3 at `etk` 270; `gst` 0 on the next AI ticks). d5 not reached |
| roam heading pick | `s32(B+0xa54) == 0 and s32(B+0xa58) == 1` | **[obs]** (state 0 `gt` 1 after every state end, e.g. f35091) |
| slash, record 22 | `s32(B+0xa54) == 1`; phase 2 → record 23 | **[obs f32248]** state 1 (player 3.8 away on its axis), 3 records 22 at t 7, hit taken f32260. Record 23: not reached (floor 49 only) |
| dash | `s32(B+0xa54) == 2`; `f32(B+0x3fc)**2 + f32(B+0x404)**2 > 0.6` at t 50..59 | **[obs f35001]** (`gdash` 185 → 0, `gfan` 150, player aligned dx −1.28 dz −3.72); t 50 f35051: v (−0.11, −0.65), hit taken 183 → 139; t 85 → state 0 f35091 |
| fan | `s32(B+0xa54) == 3` | **[obs ≈f32583]** (rings' age 118 at f32701; state entry frame not read); eruption hits f32703, ≈f35700 |
| fan beats dash | both charges over threshold → state 3 | not isolated (no frame read with both charges due) |
| phase 1 (30F: HP < 30 %) | `s32(B+0xae4) == 1` | **[obs]** HP 340 → 317 at f36491 (< 336 = 0.3·1120); `gph` 1 by f36821 |
| phase 2 (50F only) | `s32(B+0xae4) == 2 and s32(B+0xb54) == 1` | not reached (floor 49) |
| swarm, side 1 / side 2 | `s32(B+0xa54) == 5`, `s32(B+0xa84)` 1 and 2; alpha 0 in sub 0 | side 2 **[obs f36820]** (`gpc` 240; player z −34.15 ≥ −40): alpha 0, spring to (−40.26, −49.47), sub 1 + yaw 0 at t 60 (f36880), alpha 255, 48 bats, state 0 f36955. Side 1 not reached |
| the 0-flag bats | bats with `s32(+0x9fc) == 0` (2 per swarm; 5 in phase 2) | not read (slot 1 had 1) |
| detonate with / without bats alive | `s32(B+0xa54) == 4` | **[obs f37613]** (`gpc` 660 → 0), 30 ticks, `gunt` 0 throughout; **1** bat alive → one record 151 at (−32, 1, −23.97), no damage. "Without bats": not isolated |
| fresh hit | `s32(B+0xa64)` resets to 0, `s32(B+0x440) == 0` right after | **[obs f32321]** first hit of the fight (`ghc` was 280: the clock ran through the intro) → `ghc` 0, `gunt` 1, 1120 → 1097. `+0x440` not read on that tick |
| counter | `s32(B+0xa60) > 0`, then `s32(B+0xa6c) == 60` | **[obs f32627]** (`ghc` 32: `gstag` 40, `gctr` 1) → counter t 1 f32672 (`gguard` 60), `gctr` 0 f32692, `gunt` 0 f32757. Again f35149 (`ghc` 44) → record 14 at t 4 f35196, hit taken f35198 |
| attack cancelled by a hit | state 1 / 3 → 0 with `s32(B+0x440) == 40` | not isolated (hits landed in state 0; the searches did not log the state at each hit) |
| death d3 (walks to the party, shrinks at stage 7) / d5 (fades from tick 60) | `s32(B+0x428) == 2`, `s32(B+0xaf0)` | d3 **[obs f37798]** (HP 0, `est` 4, bank+0x2bce5 = 1) … freed by f38801 (`gdie` 379). d5 not reached |
| already-beaten floor | bank+0x2bce5 / 0x2bce7 = 1 → `s32(0x438b8a0) == 8`, no type 28 | not run (the end save has 0x2bce5 = 1: a new run from `save:b7eae73ba6be` → guild row "26~30" reaches it) |

## 10. Retail facts (run `griff-30f`)

- Hit clock: `+0xa64` counts through the d3 intro (280 at state-0 `gt` 8, f32238) → the first hit of the fight is fresh. The
  §5 remark "the first hit inside Griff's first 60 AI ticks counts as second" does not apply on d3 30F.
- Louie's damage on Griff: sword `ftol(56/2 − 30/4)` = 20 (+0..4): 23, 25, 27 seen; spin 40.
- Damage taken by Louie (L40, HP 197): slash 45, dash 44, counter burst 43 (knock-down 70), fan eruption 66 / 61, bat contact 16.
- Swarm bats (type 6, HP 84 at L23; table HP 15, AT 20, DF 4, MA 15, MD 10): `+0xb08 = 1` (cannot be hit), `+0xb2c = 1`,
  `+0xae4 = 3`; each carries record 153; **freed at age 100** (slot 1: use 1 → 0 at f36999, at (−33.2, −15.1), flying out). So 47
  of 48 were gone before the detonation (420 state-0 ticks later): §8.5 "kill bats before the detonation" is moot — they cannot be hit and
  expire first.
- d3 timeline: `lock` 4 (scripted walk) at `etk` 60..99, party −48 → −40.28; no dialogue at tick 120 (0x2bce4 = 1). Death → `est` 4 the
  same tick, slow-mo 45 at f37818, `p_inv` 1195; stage 5 at ≈f38140 (party (−40, −45), Griff (−43, −46.5)); **event 7/12 at stage 5
  tick 30 (f38171..38741)**: "Impossible!... / Enough! Stop this at once! / I'm so sorry! We didn't hurt you, I hope... / How can a
  human and her friends drive me to such desperation... / Hm... / Very well then. Try to reach the top, if you can! / I will meet
  you there - if you survive!"; `est` 7 f38771, `lock` −1 f38786, `gate` 0 by f38801.
- Return door: "Return to town now?" (`portal` 22) at (−44.29, −39.48) (f39132).

## 11. Unresolved

- Records 21 / 22 / 23 / 151 / 152 / 14: reach, lifetime, multipliers (only the spawn geometry above was read).
- `+0x9fc` on the bats; `FUN_00497745(n)` (voice / face cut-in?) calls at slash, dash, phase 2.
- ~~Whether the drift bats can be killed before the blast~~: no — `+0xb08 = 1`, and they expire at age 100 (§10).
- Stage type 20 after the 50F clear (`ROSTER.md` §4).
