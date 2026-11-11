# Boss ahriman — type 54 (FUN_00426811)

**code-read** from the port `src/actor_ai.c:2036..2161` (= FUN_00426811, IDA `sub_426811` all_ida.c:24468 — **not
re-read line by line here**; FRONT lists the port RNG-verified on dungeon3-floors / -deep, the port's "unverified"
header comment is stale); beam record body 0x89 (port scene1_records_b_tick.c:709); death arm read in IDA
(`sub_42F7AD` case 54, matches). Shared rules: `boss-floor-common.md`.

## 1. Where

| place | `dungeon` / `floor` | count |
|---|---|---|
| dungeon 2 5F | 2 / 4 | 1 |
| Boss Rush (dungeon 2's), 1st floor | 5 / 33, `0x438b4d0 == 2` | 1 |
| dungeon 3 15F (list `f:15` ×2), dungeon 5 floors 40 (×2), 48 (×3) | | 2 / 3 |

- Generic boss floor: setup FUN_0044cc80, event FUN_0044d0a4. Placed at (−40, **15**, −34), state 8.
- Intro: pad locked 240 event ticks; the boss descends y = 16 − t/16 over its 240 state ticks.
- End: no actor left (minions die with it at death tick 60; it frees at 120) → portals, `s32(0x438b8a0) == 1`.

## 2. Stats

- Table: ~~HP 15, AT 20, DF 20, MA 0, MD 10~~ → **retail record `0x5c39e0` [obs ob-15f]: HP 100, EXP 30, AT 20, DF 20, MA 35,
  MD 20**, rf 0.9 / 1.5 / 2 / 2. No parts. `+0xab8` 1.0. `+0xb54 = 1` (no knock-back / stagger).
- d2 f4: level 7 → HP ~~36~~ 240 (from the retail HP 100; not run). Rush f33: level 12 → ~~51~~ 340. d3 15F: **L19, HP 480 [obs]**.
- Flies: `+0x9f4 = 1`, hovers 1.5 ± 0.5 (up to +1) over the ground. **[obs ob-15f] `+0x3f4` reads 0.0 from state 0 on (both,
  the whole fight)**; the hover is not in y. Body radius = half-height = 2.0·0.9 = 1.8
  (.data row 54: rf[0] 0.9, rf[2] 2.0, rf[3] 2.0).
  Vertical test: `|hit-centre y − record y| − 0.8·reach < 1.8`. With the body 1.5..3.5 up a ground swing should
  reach; **not established by reading** (the swing record's y belongs to combat-core).

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`; cycle clock `s32(B+0xa5c)`

| state | length | what |
|---|---|---|
| 8 descent | 240 | y = 16 − t/16. No minions, no movement. → 0 |
| 0 circle | until the cycle fires | anim 0; turns toward the player 0.1/tick (FUN_0041f789) |
| 1 beam | 300 | anim 3; yaw frozen. Tick 20: record 137 + SE 698. Tick 100: velocity −0.5 along the facing octant (recoil). Velocity ×0.95 each tick. → 0 |

Every tick in states 0 / 1:
- **Cycle** `c = +0xa5c % 400` (then ++): `c == 1` → hold `+0xa60 = rand & 63`; `c == 300 + hold` → state 1.
  First beam 300..363 ticks after the descent; a beam ends 300 later; next one 37..163 ticks after that.
- **Movement:** target = player − 8·dir(angle-to-player + 0.2π·sin(phase)); accel 0.005/tick toward it, velocity
  ×0.96 → top speed ≈ 0.125/tick (player walk 0.175). It keeps ≈ 8 from the player, swinging ±36°.
  phase = ((`+0xa50` & 7)·50 + c)·2π/400.
- **Minions:** bats of the first `kinds` of types 6, 7, 8, 9 while fewer than `cap` (in use, any of 6..9) live;
  one every 30 ticks (`+0xa64`), `+0xa68` counts them: > 2·cap → 90-tick gap, > 4·cap → 150, **none once
  `+0xa68 >= 5·cap`**. Point: 2..10 from the centre, > 10 from the player (≤ 30 tries, 2 unit draws each), y 10;
  1 rand (`% kinds`) for the type.

| where | kinds | cap | total |
|---|---|---|---|
| dungeon 2; dungeon 5 floor < 38 | 2 (6, 7) | 4 | 19 |
| dungeon 3; d5 floors 38..44 | 3 | 6 | 29 |
| dungeon 4; d5 floor ≥ 45 | 4 | 8 | 39 |
| other | 2 | 3 | 14 |

Beam (record 137): anchored 2 ahead of the boss along its **facing octant** (+0x18, 8 directions), 2 up, follows the
boss. Record ages 11..: charge particles; age 80: SE 705; **ages 80..159** (state ticks 100..179): 20 probes a tick,
1 unit apart, reach 2.5 → a strip 20 long, ± 4.5 wide to the player. "Magic" → ~~1–2 damage~~ **MA 35: 42 + 0..7 at L19 vs
MDEF 32 [obs 46 / 49 / 48 / 46]**, knock-down 30.
Dies with the owner (`+0x428 != 1`) or at age 175.

Death: tick 1 slow-mo, alpha 0, burst; player invulnerable 1200 only if no other type-54 lives (else 60); tick 60: if
it was the last 54, every live actor starts dying; frees at 120.

## 4. When it can be damaged

- Always (`+0xb08` never set), from the moment the pad frees. State 1 gives ×1.2; back ×1.5.
- Safe side: anywhere but the beam strip in state 1 ticks 100..179. The beam direction is fixed from tick 0 of
  state 1 (yaw frozen) — stand behind / beside it.

## 5. Kill recipe (search)

- Names: `ast = s32(B+0xa54)`, `at = s32(B+0xa58)`, `ahp = f32(B+0x42c)`, `ay = f32(B+0x3f4)`.
- It retreats to 8 from the player at 0.125/tick: chase it (0.175/tick) into a wall / corner, or hit it in state 1
  (it only drifts on its recoil).
- Cell `(int(ahp/4), ast, at//20, int((px − ax)/1.5), int((pz − az)/1.5), int(ay))`; score `-ahp`;
  avoid `hp < 30`.
- Per hit `ftol(ATK/2 − 5)` (ATK 54 → 22). **36 HP = 2 hits** (1 with a back + state bonus roll ≥ 36).
- If no directed attack ever connects, suspect the vertical reach (§2) first: read `ay` against the swing's record
  y; a jump attack / skill may be needed.

## 6. Arms checklist

| arm | predicate |
|---|---|
| arm | predicate | 15F pair (`ob-15f`) |
|---|---|---|
| descent | `ast == 8`, `ay` from 16 to 1 | **[obs f10508..10748]** y 14.71 (t 12) … 0.80 (t 232), 0 from state 0 (not 1) |
| circle + turn | `ast == 0` | **[obs f10748]** |
| beam charge / fire / recoil | `ast == 1`; `at == 20` (record), `at == 100` (recoil), `at` 100..179 live | **[obs f11886..12030]** recoil at `at` 100: x −33.1 → −30.0 against the beam |
| beam hits the player | `hp` drops by ~~1–2~~ 42..49 with `ast == 1 and 100 <= at < 180` | **[obs f12030]** q 46; **[obs f11285]** r 48 |
| minion spawn, type 6 and type 7 | new slot `s32(+0x424) in (6, 7)`, `s32(+0xb2c) == 1` | **[obs f10755..10845]** types 6, 7, 8 (d3: 3 kinds) |
| minion cap (4) | count of 6..9 holds at 4 | **[obs]** d3 cap 6 is shared: 6 live with two bosses (not 12) |
| slowdowns at 9th / 17th | `s32(B+0xa68) > 8` → `s32(B+0xa64) == −60`; `> 16` → −120 | **not reached** (`+0xa68` 7 at f13320; d3 thresholds 12 / 24) |
| minions exhausted | `s32(B+0xa68) >= 20`, no more spawns | **not reached** (same) |
| death (last of kind sweeps the floor) | `s32(B+0x428) == 2`, at `s32(B+0xaf0) == 60` all others `+0x428 == 2` | **[obs f13784..13948]** `p_inv` 1199 at tick 2, bats 5 → 0 at tick ≈60, `estage` 1 at f13948 |
| death with a second 54 alive (×2 / ×3 floors) | `s32(0x56db0f4) == 60` at tick 1 | **[obs f13157]** 21 → 60; bats kept; freed tick 120 (f13320) |
| player dies | `hp <= 0` | **not reached** (lowest 42) |

Port: fully ported, no PORT-DEBT. Death arm matches IDA. The 15F pair (§7) verified 2026-11-11: `obsidian-11f-15f-ahrimans`
identical + save (rhost triage ticks 1..15311): shared minion cap, beams, both death arms (`p_inv` 60 / 1199), the door.

## 7. 15F pair — retail (`ob-15f`, probe `docs/findings/probes/obsidian-11f-15f.md`)

Dungeon 3 15F (`floor` 14), Louie L40 (ATK 56, MDEF 32). `q` = actor 0 (x −45), `r` = actor 1 (x −35), both type 54, L19,
HP 480. **[obs]** unless noted.

- **Clocks:** both `+0xa5c` equal all fight (placed the same tick); separate holds (6 / 42, then 45 / 60, q 40 next), so the beams
  overlap: q's state 1 f11057 / 11886 / 12534 / 12933, r's f11104 / ≈11926 / ≈12549 / 12928. A state 1 can follow only 95 ticks of
  state 0 (q ≈f12840 → 12933). No other interaction seen: each turns to the player and keeps ≈8 from it on its own.
- **Beam record** 137 (side 1): anchor = boss + 2·(octant dir), y 2.0, vel = the unit dir (probe step), reach 2.5; age = `at` − 19.
  The octant is the player's bearing at state-1 tick 0 (q: octant 6 with the player straight −x, `qlat` 0.01).
- **Strip, measured** (`al` / `lat` = player offset from the anchor along / across the octant): hits at `lat` −2.44 (`al` 0.30),
  −3.26 / −3.46 (both live, undone branch); no hit at |`lat`| 4.66..4.69 (`al` 2.1..4.2, `p_inv` ≤ 0, both live, f13074..13078) and
  at 10.9..13 (r, f12280..12298). A hit 17.6 from q's centre (f≈11200, q `qt` ≈137; r not live at `rt` 97). Consistent with ±4.5 /
  20 long; the length end was not probed.
- **Damage to the player:** 46, 49, 48, 46 (magic, MA 35 — not 1–2). Knock-down 30, `p_inv` 30.
- **Damage to it** (DF 20 → base `ftol(56/2 − 5)` = 23): state 0 front 23 (f10915); state 1 counter 27 (f11075); state 0 back 36
  (f≈12920); counter + side 32 / 34 (f≈12620, f13034 at `rt` 102 = beam live); spin counter 55 (f≈11600), spin counter + side
  77 / 78 (f12397, f13049 at `rt` 113 = beam live). **Ground swings reach** (y 0).
- **Hit while the beam is live:** stand behind it (|`lat`| < 4.5 is only dangerous for `al` > −4.5) or spin through (`p_inv` 30).
  A swing locks 30 ticks; one that lands next to the anchor at `at` ≈100 eats the beam unless cancelled into the spin
  (`B` from `p_actt` ≥ 5 after the hit connected).
- **Deaths:** r first (f13152, spin): tick 1 at f13157 after 4 hit-stop frames, `p_inv` 60, slow-mo 60, bats untouched, freed at
  tick 120 = f13320. q last (f13784, sword): `p_inv` 1199 at tick 2, bats die at tick ≈60, `estage` 1 / `gate` 0 at f13948.
- **Floor end:** door at (−45, −40) answers from the −z side facing +z (`p_oct` 0), `A` → "Return to town now?" → Yes → results
  f14598.

Predicates (run-local, `ob-15f/vars.json`): `qst qt qc qhold qn qhp quse qdt qoct qd qgap qrel`;
`qal = (px-qx)*sin(qoct*pi/4) + (pz-qz)*cos(qoct*pi/4) - 2`, `qlat = (px-qx)*cos(qoct*pi/4) - (pz-qz)*sin(qoct*pi/4)`;
`r…` the same at `0x76c514`. Beam live: `qst == 1 and 100 <= qt < 180`; in the strip: `-4.5 < qal < 24.5 and abs(qlat) < 4.5`
(approximate, from the measured points above).
