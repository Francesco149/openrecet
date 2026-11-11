# Charme — type 25 (FUN_0041fa6e)

**code-read** from the port `src/actor_ai.c:3123..3339` (= FUN_0041fa6e, IDA `sub_41FA6E` all_ida.c:19783 — not
re-read line by line; the port is RNG-verified: dungeon1-charme / -clear, dungeon2-run*); hit counter FUN_0041fa37 (port record_hit.c:336); floor timeline FUN_0044d47d /
setup FUN_0044d1e4 (port dungeon_flow.c:585..789); death arm `sub_42F7AD` case 25. Shared rules:
`boss-floor-common.md`.

**Live probe 2026-11-11, run `ch-arms`** (§7): tags **[obs fNNNN]** = seen in retail at that frame; everything untagged is
still code-read. Numbers the probe corrected are marked "(was …)".

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
| 4 | pad locked; tick 60 a 120-tick fade-out starts; fade done (**[obs]** `+0xaf0` 179 on stage 5's tick 1; 262 frames f62528..f62790 because of the slow-mo) → party back at the start (−40, −40) → 5 |
| 5 | Charme set beside the party (**[obs]** (−43, −41.5)); tick 30 dialogue iv3_3 (**[obs f62820]** `dlg 1`, ends f64300); 31 → 6 |
| 6 | Charme freed (**[obs]** `+0x428 == 0` by tick 29); tick 30: first win → bank+0x2bc8d = 1, dialogue iv3_8 (**[obs f64331..f64869]**, byte 0 → 1); 31: both objects rise → 7 |
| 7 | `lock = −1` (**[obs f64871]**), but play is frozen while the rise counter `s32(0x438beb4)` runs 120 → 0: control at **[obs f64990]** |

- Lock before the fight: first visit 60 + 300 + 120 ticks + two dialogues (`dlg == 1`: tap A); later visit 120.
- Already beaten (bank+0x2bc8d) → no Charme, stage 6, portals up.
- Objects (was "portals: town / floor"), `0x695ef78 + i*0x2a0`, kind `+0x8c`, state `+0x268` **[obs f65208]**:
  - object 8, **kind 6 at (−35, −30) = the return crystal**. Solid (the player stops 1.46 from its centre while it is
    state 0). A sword hit sets state 1 (**[obs ~f65248]**); then standing within 1.0 raises the prompt
    (**[obs f65444]** at 0.886: "Return to town now?" Yes / No, `portal` counts up). `A` = Yes → 132 ticks later
    `scene == 9` (the return screen), `s32(0x438be98) == 3` (**[obs f65491 → f65622]**). A d-pad direction on the same tick
    as `A` answered No (undone branch).
  - object 10, **kind 4 at (−40, −30) = a treasure chest**, not a floor portal. A sword hit opens it (state 2,
    **[obs ~f65309]**; label 'Very Odd Vase'), freed (kind −1) 120 ticks later. Solid: the player stops 1.85 out.
  - object 9, kind 4, state 3, y −0.5, same spot: the chest that sank in stage 2 (`g_charme_portal`).
  - No kind-8 (next floor) object among objects 0..12: the floor is left through the crystal.
- Rush floor 32: generic boss floor (240-tick lock); Charme's state 8 ends at her tick 240.

## 2. Stats

- Stat record `0x5c23f0 + 25*0x68` **[obs f60417]**: **HP 100, AT 22, DF 20, MA 25, MD 30**, flags `+0x20` = 0, rf 0.6 / 1.5 / 2.0 / 2.0
  (was: HP 150, AT 20, MD 10). Body radius 1.5·0.6 = 0.9; sword hit radius rf[3]·rf[0] = 1.2 → Louie's sword reaches 4.2
  facing her (**[obs]** hits at 4.03, 3.98).
- d1 f14: level `+0xae0` = 9 (HUD "LV10") → **HP 280 / 280** (was 420) **[obs]**. AT·mul = 22·1.9 = 41.8: hit on the player
  = `ftol(20.9 − DEF/4)`; DEF 51 → **8** per slash and per dash **[obs ×5]**. DF_eff 20 (`+0xab8` 1.0 **[obs]**).
  Rush f32 not run.

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)` (pre-incremented every AI tick)

Top of every AI tick: `+0xb08` counts down; `+0xa58`++; `+0xa64`++ (hit-window clock). **[obs]** all three; none of them
moves while `+0x440 > 0`. `rd` reads state after the tick: a state entered this tick shows `ct == 0`, so "tick N" below
is `ct == N` one frame later (trigger at tick 10 = last state-0 row has `ct == 9`).

**Flee override** — `s32(B+0xa60) > 2`: anim 6, `+0xb08 = 1` (invulnerable). Tick 1: vy 1.0; up to 30 random
points in the ±10 square around (−40, −40) until one is > 10 from the player (2 unit draws each). Ticks < 20:
velocity = 0.08 · (target − pos). Lands (`y == ground`, tick > 20) → state 0, `+0xa60 = 0`.
**[obs f61524..f61560]**: first AI tick after the 40-tick stagger: `ct 1`, vy 0.931 (1.0 less gravity), y 0.95, `+0xb08 1`;
36 ticks in the air; landed (−47.75, −35.22), 14.3 from the player: `cst 0, ct 0, chits 0`, `+0xb08` still 1 on that
tick, 0 on the next (f61561, new plan). So a counted third hit buys 40 + 37 = 77 unhittable ticks.

| state | length | what |
|---|---|---|
| 8 entrance | to event stage 3 (d5: 240 ticks) | floor 14, stages 0 / 1: tick 240 hop (vy 0.8), 240..267 slides −z at 0.4 |
| 0 stalk | runs of 35..98 ticks | tick 1: plan `+0xae4` (3 rands). Tick % 30 == 1: a heading toward a point 4 from the player (1 unit + 1 rand, then ≤ 20 × 2 rands until a 4-ray is clear), run length `+0xa5c = rand & 63`. From tick 6 accel 0.047 (plan 3: only faces the player, stands). From tick 10: trigger test (below). Run ends at tick 35 + `+0xa5c` → tick 0 (new plan). **[obs]** `+0xa5c` is re-rolled at ticks 1, 31, 61: runs ended at `ct` 39 (`+0xa5c` 5, f61303), 46, 60, ~74 (40 rolled at tick 61, f60492). Speed ≈ 0.25–0.3 / tick: faster than Louie's 0.175. |
| 1 slash | 28 **[obs]** | anim 3 from tick 3; tick 7: voice, SE 343, record 16. **[obs]** she keeps sliding in (3.75 → 2.9); the player's HP drops on the row `ct == 12` (f60761, f61243; once at 15 when the player was 3.21 out) |
| 2 bind field | 51 **[obs f60676..f60727]** | anim 5; tick 12: record 83 (**[obs]** age 2 on the `ct == 13` row), voice |
| 3 dash | 110 | anim 6, from 45 anim 4; tick 1 cry; ticks 45..64 accel 0.08 along the yaw fixed at the trigger, **`+0xb08 = 1`**, afterimages; tick 50: SE 349, record 21; tick 65 `+0xb08 = 0`. **[obs f61316..f61430, f62078..]** `+0xb08` 1 on rows `ct` 45..64, 0 at 65; stands still for ticks 0..44; travels 8.89 (0.43 / tick at tick 64), stopped dead by the arena wall at x −52.76 after 7.26 (f62144); `ct` 109 → state 0 |

Plan roll (state 0 tick 1): `p = rand & 3`; `rand & 1` → 2; `rand & 3 == 0` → 2.
P(plan 2) = 23/32, P(0) = P(1) = P(3) = 3/32. **[obs]** rolled: plan 1 f60667, plan 0 f61264 / f62069 / f62356 (the last two
by `rd search`, 36 s / 44 s), plan 3 f61431, plan 2 most runs.

Trigger (tick ≥ 10, plan ≠ 3, Charme on the ground):

| plan | condition | → state |
|---|---|---|
| 0 | player within 1.5 of one of her axes and 1.5 < distance along the other < 8 | 3 dash (if the distance is < 3: plan := 2 → state 1). **[obs f61316]** lateral 0.63, along 6.4; **[obs f62078]** 0.71 / 5.86. Downgrade **[obs f62364 → f62365]**: `cplan 0, ct 9`, lateral 0.45, along 2.88 → next row `cst 1, cplan 2` |
| 1 | player within ±10 of (−40, −40) on both axes **and** a clear 12-ray along her current yaw | 2 bind field. **[obs f60676]** at tick 10, player 7.6 away, off both axes |
| 2 | within 1.5 of an axis and 0 < along < 4 | 1 slash. **[obs]** fired at along 3.96 / lateral 0.07 (f60613: 3.90 / 0.80); not at 4.12 / 0.84 one tick earlier |

Records:
- 16 (slash): 1 ahead (facing octant), 1 up, reach 1.5 → hits within 3.5 of that point; live only while state 1,
  record ages 6..9 = state ticks 13..16. Physical ×1.0. **[obs]** 8 damage, `p_hurt` 30, `p_inv` 30, knock-back
  2.9 → 7.3 (f60761, f61243, two more); hits landed with the player 2.85..3.21 from her.
- 21 (dash): 2 ahead, 2 up, reach 0 (owner 25) → within 2.0 of that point; record ages 0..14 = state ticks 50..64.
  **[obs f61370]** hit on the row `ct == 54` (record age 5), player 0.76 off her line and 3.47 ahead: 8 damage, `p_hurt` 30.
  **[obs f62128..f62142]** no hit standing 2.36 off the line. The record slot stays type 21 until age ~40 (state tick ~90).
- 83 (bind field): starts 0.3 ahead, slides 0.5/tick ×0.92 for 30 ticks (≈ 5.7 units), stops; record ages 46..554:
  radius grows to 1.9; a player within radius + 2 has **x / z velocity zeroed every tick** (FUN_0043865e type-83 arm);
  no damage. Lives 600 ticks (enemy-owned). Effect on walking input not traced past the velocity write.
  **[obs f60688..f60884, picture `ch-arms/view/f060745.png`: a spider web]** record slot 0 (`0x69324b0`), type 83, side 1:
  - reach `+0xa8` 0.5 through age 45, grows over ages 46..58 to 1.92, then stays 1.86..1.94;
  - it is still creeping (0.045 / tick) between ages 30 and 46, then stops: 5.5 from where it was at age 2;
  - zone = distance to its centre < reach + 2 (**[obs f60877 → f60878]** leaving: velocity 0 at 3.87, back at 3.97);
  - inside, the velocity reads 0 every tick and **walking still moves the player 0.1 / tick** (0.175 outside)
    **[obs f60840..f60877]**: a slow field, not a root. (f60733..f60760, standing 0.05 from its centre with a 0.05-long
    `dpad()` vector: the position did not change at all — not separated from the input being too short.)
  - not applied while `p_inv > 0` (**[obs f60811..f60823]**: walked in at 0.175 / tick from 3.77 to 1.68 during the 30
    ticks after a slash; slowed from f60824 — attribution by timing, not traced) nor during the knock-down slide;
  - no damage; she slashed the player standing in it (f60761). Slot free (type 0, age 600) when next read at f61326.

Hit counter (FUN_0041fa37, on every damage): `+0xa64 < 60` → `+0xa60`++, `+0xa58 = 0`; else `+0x440 = 0`,
`+0xa64 = 0`. Either way `+0xb08 = 10`.
- A hit also does the generic stagger (`+0x440 = 40`, state 0, knock-back): the **window-opening hit cancels its
  stagger**; hits inside the 60-AI-tick window keep it.
- AI (and so the `+0xb08` countdown and `+0xa64`) is frozen while `+0x440 > 0` → after a counted hit she is
  unhittable for 40 + 10 ticks; after an opener, 10.
- 3 counted hits → flee. `+0xa64` is not reset by the flee.
- **[obs]** opener f60573: `+0xa64` 576 → 0, `+0x440` 0, `+0xb08` 10 → 0 over the next 10 AI ticks, `chits` unchanged;
  the state is still reset to 0 (a bind-field cast at tick 9, slashes at ticks 9..11 and dash wind-ups at ticks 26 / 30
  were all cancelled) and she is knocked back 3 (3.53 → 6.51).
- **[obs]** counted hit f60623 (`+0xa64` 45 → 46): `chits` 0 → 1, `+0x440` 40, `+0xb08` 10 frozen for the 40 ticks, then 10 → 0;
  `+0xa64` frozen too (46 → 47 on the first AI tick after); knock-back 3.75 → 7.6.
- **`+0xa60` is never cleared by an opener** (was "the 4th hit of a burst"): the three counted hits of the first flee were
  f60623, f61138, f61480, in three different windows with five openers between them **[obs]**. Only the flee's landing
  zeroes it.
- A window allows at most opener + 3 counted hits (AI ticks ≥ 10, 20, 30 after the opener, < 60), and after an
  opener she walks off faster than Louie can follow his 30-tick swing: a plain second swing landed at `+0xa64` 44..52
  when it landed at all (`rd search` for ≤ 36: none in 88 s). **Sword → spin cancel** (`B` during the hit-stop of a
  connected swing) lands the spin at `+0xa64` 11 **[obs f61926 → f61941]**: counted, and the spin is one hit.

Death (`+0x428 == 2`): tick 1 slow-mo 120, player invulnerable 1200. Floor 14: she stays (stage 5 holds her
beside the party, stage 6 shrinks her out). Dungeon 5: fades from tick 60 (alpha −16) and frees.

## 4. When she can be damaged

- `s32(B+0xb08) == 0` only. Blocked: 10 ticks after each hit (50 when staggered), dash ticks 45..64, the whole flee.
  **[obs f62136..f62139]** a swing whose active ticks (`p_actt` 9..12, gap −1.15..−1.82) fell on dash ticks 58..61: no
  damage, no label, no hit-stop. The 10 / 50-tick blocks are read from `+0xb08` / `+0x440`; no swing was landed in them.
- State 8 (entrance): no `+0xb08` → hittable once the pad is free.
- Bonus ×1.2 in states 1..3; back ×1.5; side ×1.2 **[obs]** (table in §7). The dash wind-up (state 3, ticks 0..44,
  she stands) is a counter window too: hits at ticks 11, 26, 30.
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
- Per hit `ftol(ATK/2 − 5)` (ATK 54 → 22; ×1.2 in states 1..3; ×1.5 back), + `rand % (dmg/5)`.
  **[obs]** ATK 34 → 12: 12–13 plain, 14–15 counter, 18 side + counter, 19 back, 21 back + counter, 36 spin from behind.
  280 HP took 17 hits (2 spins) in `ch-arms`; a kill-only run needs ~14.
- **[obs]** what worked: a chase policy
  `{dpad(a0x - px, a0z - pz) | (A if (gap < -0.1 and cstag + cinv <= 9 and p_act == 0 and frame % 2 == 0) else 0)}`
  (`gap = hypot(a0x - rx, a0z - rz) - 3.2`) lands a hit roughly every 50–90 ticks and takes a slash now and then;
  plan 0 / plan 3 by `rd search` on `cst == 0 and cplan == N and ct <= 4` with walk-only actions (30–45 s, `-j 2`).

## 6. Arms checklist

| arm | predicate | observed (`ch-arms`) |
|---|---|---|
| first-visit intro (stages 0 → 1 → 2) | `s32(0x438b8a0)` passes 0, 1, 2; hop at `cst == 8 and ct == 240` | **not reached**: the run starts at f60417 = stage 3 tick 2 (bank+0x2bc8c already 1, `+0xa64` 421); the intro is in the scenario's prefix, not watched |
| later-visit intro (stage 2 start) | floor loads with `s32(0x438b8a0) == 2` | **not reached** (same) |
| plan 0 / 1 / 2 / 3 rolled | `cst == 0 and ct == 1 and cplan == N` | **[obs]** 0: f61264, f62069, f62356; 1: f60667; 2: f60737 and most runs; 3: f61431 |
| plan 3 (stand, face) | `cplan == 3`, no walk until the run ends | **[obs f61431..f61480]** distance constant 3.618 while the player stood (rows `ct` 49..53); cut short by a hit at `ct` 53, run end not seen |
| plan 0 downgraded (< 3) | `cst == 1` straight after `cplan == 0` | **[obs f62364 → f62365]** `cplan 0, ct 9`, along 2.88, lateral 0.45 → `cst 1, cplan 2` |
| slash | `cst == 1`, record at `ct == 7` | **[obs f60613, f60749, f61231, f62365]**; 28 ticks |
| bind field | `cst == 2`; player velocity zero while inside | **[obs f60676]** cast; record f60688; zone f60733..f60878 (§3: slow field 0.1 / tick, edge reach + 2) |
| dash | `cst == 3`; `cinv == 1` at `ct` 45..64 | **[obs f61316..f61430]** full 110 ticks, 8.89 travelled; **[obs f62078..f62182]** stopped by the wall |
| dash guard | swing active inside ticks 45..64 → no damage | **[obs f62136..f62139]** |
| dash recovery hit | hit with `cst == 3 and ct >= 65` | **[obs f61422]** `ct` 101, front (rel 0.70): 15 = 12·1.2 + 1; **[obs f62182]** `ct` 104, rel 2.94: 21 = `ftol(12·1.8)`, words "CounterAttack! BackAttack!" (`ch-arms/view/f062186.png`) |
| hit-window opener (no stagger) | hit with `s32(B+0xa64) >= 60` → `s32(B+0x440) == 0` | **[obs f60573]** (`+0xa64` 576; 14 in state 2); also f60896, f60986, f61090, f61202, f61422, f61860, f61926, f62182, f62377, f62484 |
| counted hit (stagger) | `chits` rises, `s32(B+0x440) == 40` | **[obs f60623]** (`+0xa64` on the hit row 46), f61138 (44), f61480 (54), f61941 (11, spin), f62424 (43), f62527 (39, the kill) |
| flee | `chits == 3`, `cinv == 1`, vy 1.0 | **[obs f61480]** `chits 3`; **[obs f61524..f61560]** the jump (§3) |
| flee point search exhausts (30 tries) | not observable without the draw count | not reached |
| side / back hit | `a0rel` > pi/4 / > 3pi/4 on the hit row | **[obs f61090]** back, state 0, rel 2.46: 19 = 12·1.5 + 1; **[obs f61926]** side + counter, rel 1.40, state 1: 18 = `ftol(12·1.44)` + 1; **[obs f61941]** spin, rel 2.82: 36 = 12·2·1.5 |
| player hit by slash / dash | `hp` drops in `cst == 1` / `3` | **[obs f60761]** slash 81 → 73 (bound in the field); **[obs f61370]** dash 57 → 49 |
| player two-hit knock-down | — | not seen: every slash / dash hit gave `p_hurt` 30; no second hit was taken inside one knock-down |
| pause-menu food | `scene == 9` | **[obs f61567..f61787]** Walnut Bread ×2: 49 → 60 → 70 (`~`, `D`, `A`, then `D` to the item, `A`, `A`, `A`) |
| player dies | `hp <= 0` (dungeon1-run / -charme recordings do this) | not in this movie (`hp` ≥ 49) |
| death, floor 14 scene | `s32(B+0x428) == 2`, stages 4 → 5 → 6 → 7, bank+0x2bc8d = 1 | **[obs f62527]** HP 0, `+0x428 2` (spin, state 3 tick 26); stage 4 f62528, slow-mo `s32(0x438becc)` 120 → 0 (1 sim tick per 3 frames), `p_inv` 1200; stage 5 f62790; stage 6 ~f64301; byte = 1 by f64869; stage 7 f64870 |
| floor's end | crystal hit → prompt → Yes | **[obs f65444]** prompt, **[obs f65622]** `scene 9`, `s32(0x438be98) 3` (§1) |
| revisit after the win | no type-25 actor, `s32(0x438b8a0) == 6` | not reached (needs a second visit) |
| death in dungeon 5 (fade) | `s32(B+0x3dc)` falls from tick 60 | not reached (floor 14: alpha stayed 255) |

## 7. Observed run `ch-arms` (2026-11-11)

`rd new ch-arms --from dungeon1-clear@60417`; Louie ATK 34 / DEF 51 / MATK 10 / MDEF 22, HP 81 / 81, SP 22 / 22.
f60417 (control, stage 3 tick 2) → f62527 Charme HP 0 (2,110 frames: event tick 1797, ~220 frames of pause menu, ~70 of
hit-stop) → f64990 control back → f65622 return screen. `rd verify ch-arms`: MATCH (fresh headless replay, unskipped
retail: frame 65652, rng 8c105b75, sem 1a7125365abce3cd). Not filed.

Names (this run's `vars.json`, `B = 0x76b970`): `cst ct c5c chits c64 cplan cinv cstag chp cy cyaw cuse cdt` =
`+0xa54 a58 a5c a60 a64 ae4 b08 440 42c 3f4 420 428 af0`; `estage etick` = `0x438b8a0 / a4`; `cd cdx cdz` = distance /
player − Charme; `gap` = `hypot(a0x - rx, a0z - rz) - 3.2`; `r0t r0x r0z r0age r0reach r0d` = hit-record slot 0
(`0x69324b0`: `+0 +0x5c +0x64 +0x98 +0xa8`); `slow` = `0x438becc`; `rise` = `0x438beb4`.

| frame | hit on Charme | state / tick | rel | `+0xa64` before | damage (HP after) |
|---|---|---|---|---|---|
| 60573 | opener | 2 / 9 | 0.20 | 576 | 14 (266) |
| 60623 | counted 1 | 1 / 9 | 0.22 | 45 | 15 (251) |
| 60896 | opener | 1 / 6 | 0.31 | 271 | 15 (236) |
| 60986 | opener | 1 / 9 | 0.24 | 81 | 15 (221) |
| 61090 | opener, **back** | 0 / 38 | 2.46 | 99 | 19 (202) |
| 61138 | counted 2 | 1 / 8 | 0.22 | 43 | 14 (188) |
| ~61202 | opener, dash wind-up cancelled | 3 / ~11 | ~0.09 | ≥ 64 | 14 (174) |
| 61422 | opener, **dash recovery** | 3 / 101 | 0.70 | 207 | 15 (159) |
| 61480 | counted 3 → flee | 0 / 53 (plan 3) | 0.21 | 53 | 13 (146) |
| 61860 | opener | 1 / 9 | 0.58 | 160 | 14 (132) |
| 61926 | opener, **side + counter** | 1 / 10 | 1.40 | 61 | 18 (114) |
| 61941 | counted 1, **spin** (cancel), back | 0 / 10 | 2.82 | 10 | 36 (78) |
| 62182 | opener, **dash recovery, back + counter** | 3 / 104 | 2.94 | 208 | 21 (57) |
| 62377 | opener | 1 / 11 | 0.00 | 186 | 14 (43) |
| 62424 | counted 2 | 3 / 30 | 0.35 | 42 | 15 (28) |
| 62484 | opener | not logged | 0.54 | ≥ 60 | 15 (13) |
| 62527 | counted 3, spin, **kill** | 3 / 26 | 0.26 | 38 | 13 left → 0 |

Player: 81 → 62. Lost 40 (slash 8 × 4: f60761, ~f60950, f61243, one inside the f62189..f62356 search; dash 8: f61370),
ate +21; SP 22 → 12 (two spins).

Port: no PORT-DEBT in FUN_0041fa6e / the timeline. Record 83's player arm and record 21 / 16 bodies are ported.
