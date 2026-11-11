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
40F pair: verified `obsidian-31f-40f-ohms` (portrng identical ticks 1..44586 + save; shared bee cap, per-Ohm counters,
first death without the sweep, second death's sweep) — no Ohm change needed (2026-11-11).

## 7. 40F pair (Obsidian Tower floor 39, `obs-40f`, retail-observed)

Run `obs-40f` (`docs/findings/probes/obsidian-31f-40f.md`; `rd verify` MATCH at f44286, Windows). Louie L41: ATK 57, DEF 59, MDEF 33.
Tags: **[obs fNNNN]** = seen live in `obs-40f` (or its read-only side run `obs-40f-scan`).

**Corrections to §2 (stat table, read at `0x5c23f0 + 67*0x68`)**: HP 150, **EXP 25, AT 30, DF 25, MA 20, MD 25**, rf 1.3 / 4 / 3 / 4.5
(not AT 20 / DF 30 / MA 0 / MD 10). So the charge does `ftol(30·mul/2 − DEF/4)` and Louie's sword `ftol(ATK/2 − 25/4)`.
At L25 (`floor/4 + 16`): **HP 900** (150 · 6.0), mul 3.5 → charge `ftol(52.5 − 14.75)` = 37, +0..6: **taken 39, 40, 40, 42, 42**.
Bee (type 10): HP 5, EXP 10, AT 17, DF 4, MA 0, MD 0, rf 0.4 / 1.5 / 4 / 4 → HP 30 at L25; sting = record **17** (reach 0), 15 +0..2 →
**17 taken twice** [obs f40813 (side run), f42227].

Placement: count 2 → Ohm 0 at (−45, −34), Ohm 1 read at (−36.0021, −34) when control came (f40502) [obs]. Both intro 240, both
start state 0 on the same tick, and their bee timers (`+0xa64`) run in lock-step from 1.

| arm (pair) | predicate | result |
|---|---|---|
| intro, both | `o0st == 8 and o1st == 8` | **[obs f40261..40501]** 240 ticks, `lock` 0; state 0 t 1 at f40502 |
| Ohm 0 charge (facing) | `o0st == 2` | **[obs f40515]** first decision (t 11), and on most decisions after |
| Ohm 1 charge | `o1st == 2` | **[obs f40532]** |
| walk | `oNst == 1` | **[obs f41033 Ohm 0, f40856..40974 Ohm 1, f42803 Ohm 0]** |
| burst | `oNst == 3`, records 33 at t 20 / 24 / 28 / 32 | **[obs f42213]** Ohm 0 t 16; 8 records 33 per volley at t 20 / 24 (reach 0.10, power 1, side 1), 6–10 from the player: no hit |
| Ohm 0's charge hits the player | `hp` drops, `o0st == 2 and 70 <= o0t < 130` | **[obs f40597]** t 83: 202 → 163 (39), `p_hurt` 70, `p_inv` 30; again f41703 (40), f41965 (42) |
| Ohm 1's charge hits the player | same, `o1` | **[obs f41076]** t 70 (record age 0): 163 → 123 (40); again f42286 (42) |
| the 5.5 disc 2 ahead | hit iff `hypot(player − (ox + 2 sin yaw, oz + 2 cos yaw)) < 5.5` | **[obs f40596 / f40597]** 5.66 → no hit, 5.39 → hit (Ohm 0, t 82 / 83); f41076: 3.54 → hit |
| burst shot hits | `hp` −1..2 in / after `ost == 3` | **not reached** (one burst in 2,800 ticks of fight; its shots spawned behind it). Note MA is 20, not 0: the "1–2" of §3 does not hold (the roll would be `ftol(20·3.5/2 − 33/4)` = 26) — unverified |
| bee spawn | new type 10, `+0xb2c == 1`, at 2..10 from (−40, −40) | **[obs f40625]** both Ohms' `bt` hit 120 on the same tick → 2 bees at once, `bn` 1 / 1 |
| **bee cap: shared** | 5 live type-10 in the whole table blocks **both** timers | **[obs f40865..41157, side run idle]** `bees` 4 → Ohm 0 spawns the 5th (`bn` 3); Ohm 1's `bt` stays 119, `bn` 2, while 5 live. The count is global (`FUN_00422ed6` scans all 128 slots for type 10); each Ohm keeps its own `bt` / `bn` |
| bee slowdown | `bn > cap` → `bt = −200` (gap 320); `> 2·cap` → −400 (gap 520) | **[obs]** `bn` 6 / 6 at f41682, Ohm 0 `bt` −136 at f42885 (`bn` 8) → gaps grow; `bn` 9 / 8 at f43216 |
| bee exhaustion | `bn >= 5·cap` = 25 per Ohm (up to 50 for the pair) | **not reached** (`bn` 9 / 8 at the end) |
| how the two interact | — | **[obs]** they often charge together (both state 2 on 9 of 48 samples while both lived; Ohm 0 in state 2 without Ohm 1 on 11 more); bodies overlap near the player and a spin hits **both** (one search: 848 → 801 and 726 → 681, f41564..41623); the shared bee cap above; the first death's `p_inv` 1200 covers the rest of the fight (next row) |
| first death (Ohm 1) | `o1use == 2` | **[obs f42875]** tick 1: slow-mo 60, **`p_inv` 1200** (1199 at f42885) although Ohm 0 lives; **tick 60 (≈f42978): no sweep, `bees` stays 2**; alpha −8 / tick from tick 100 (255 → 39 by f43045); freed f43050 (death timer 131) |
| second death (Ohm 0) + sweep | `o0use == 2`; at tick 60 every live actor → dying | **[obs f43338]** slow-mo 60, `p_inv` 1200 again; 2 bees alive (idx 16 / 17) → **0 at tick 60 (f43440)**; Ohm 0 freed f43538 |
| floor end | `estage == 1` | **[obs f43539]** `gate` 3 → 0, `0x9643128` 1 → 0; portals kind 8 (−35, −40) "Go to the next floor?", kind 6 (−45, −40) "Return to town now?" (prompt f43735) |
| player dies | `hp <= 0` | **not reached** (lowest 50, f42306) |

Kill: Ohm 1 900 → 0 over f41443..42875, Ohm 0 900 → 0 by f43338 (the last 153 under the 1200 `p_inv` of Ohm 1's death), by damage searches
(goal `oNhp <= H − 60`, `--avoid "hp < H"`, actions approach / directed swing / spin / retreat / strafe, `-j 4`, 90–120 s). 4 searches ended
on a frame where a charge was already unavoidable (one cell for the whole search): the hits at f41703, f41965, f42286 and the bee at
f42227 came from those. Spins (5 SP) did most of the damage; SP 46 → 6 → 46 (Baked Yam) → 26.
