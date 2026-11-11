# The Demon Lord's hand — type 72 (魔王の手)

**code-read**: IDA `sub_424641` (AI, 0x424641), `sub_43865E` (the hit handler: IDA 38971 `type == 72 → part hit`),
`sub_43AE20` (record 154 follows the hand, IDA 40241), `sub_42F7AD` default arm, spawns in `sub_4427F1` (d3 floor
49, tick 30) and `sub_450EE0` case 45. **Port (2026-11-11): AI `actor_ai_arm` (`src/actor_ai.c`), record 154 on the owner-motion body (`src/scene1_records_b_tick.c`), d3 floor-49 spawn (`griff49_floor_event`, `src/dungeon_flow.c`) — verified `obsidian-41f-50f-finale` (triage identical + save; descend, release, sweeps, slam, death with Griff); the d5 240-tick release unverified.** Shared rules: `boss-floor-common.md`; the fight it belongs
to: `griff.md`. **Run 2026-11-11 (`obs-50f`): §8 — retail stats override §2 (HP 100 → 660 at L28, MA 35).**

## 1. Where

| place | spawn | released (state 8 → 0) |
|---|---|---|
| Obsidian Tower 50F (3 / 49) | event tick 30: `FUN_0041f903(−35, 20, −25, 72)`, `+0x428 = 1`, `+0xa54 = 8`, pointer `DAT_06a470a0` | event stage 3 |
| dungeon 5 floor 45 (and d3's Boss Rush 8th floor) | setup: same call, pointer `DAT_06a470c8` | 240 state ticks |

Always next to Griff (type 28). Enemylist `f:50` (d3) / `f:46` (d5) list it as the second line.

## 2. Stats

- Table (baked): HP 150, EXP 25, AT 20, DF 30, MA 0, MD 10, boss flag 1; rf0 0.8 → radius 3.6, half-height 2.4.
  d3 f49: lv 28 → HP 990 (never changes, below).
- Every AI tick: `+0xa00 = 1` (floats: gravity off), `+0xb54 = 1`, `+0xb04 = 1`.
- **Takes no damage.** In `FUN_0043865e` a hit on type 72 sets the "part" flag (`v171 = 1`, IDA 38971): damage is
  forced to 0 (LABEL_170), no stagger, metal spark effects 41 / 42 and SE 359 instead of the hit effects. The
  0-damage hit still goes through `FUN_0042e791(a, 0, 0)` (HP − 0).
- It ends with Griff: Griff's death tick 60 (`FUN_0042f746`) puts every live actor into dying; the hand's death arm
  is the default one (freed at death tick 2 with a puff, SE 337).

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`, sub `s32(B+0xa5c)`

Fields: cycle count `s32(B+0xa60)`, sweep count `s32(B+0xa64)`, ground y `f32(B+0x3e0)`.

| state | what |
|---|---|
| 8 descend | y −= 0.16/tick down to 3.0 (from 20: 107 ticks). d5: t ≥ 240 → 0. d3: event stage 3 → 0 |
| 0 hover | anim 0; turns to the player 0.01/tick. When `t ≥ 200` and `t ≥ 60 + count`: `count++`, t = 0, sub = 0, **state = 2 if `count % 4 != 0` else 1** — three sweeps, then a slam, repeating; no RNG |
| 1 slam | sub 0: turns to the player 0.01/tick, y += 0.1/tick, velocity = 0.9·(v + min(0.04, 0.1·|Δ|)·dir to the player) (≤ 0.36/tick); y > 15 → sub 1. Sub 1 / 2: `+0xa00 = 0` → falls (gravity 0.05/tick²). Sub 1 and y ≤ ground + 2: **record 14 at the hand**, effect 51, shake 16, rumble 16 → sub 2. y ≤ ground: y = ground, SE 365 → sub 3. Sub ≥ 3: counts; sub > 160 → state 0 (the hand lies on the floor ≈ 157 ticks) |
| 2 sweep | t ≤ 200 (sub 0): yaw → −1.8708 at 0.02/tick; y → 2.0 (±0.1/tick); drifts to (−20, −40). t > 200: sub = 1, `+0xa64++`. Sub ≥ 1 (++ each tick): **sub 10: record 154** (a beam that stays on the hand), SE 342; sub 75: SE 705; **sub 101..299: target z = 0 when `+0xa64` is odd, −80 when even**; else −40. Velocity = 0.9·(v + 0.04·dir to (−20, target z)) (≤ 0.36/tick). sub > 430 → state 0 |

Sweep geometry: the hand sits at x ≈ −20 (the arena's +x edge; the centre is −40), facing yaw −1.87 (≈ −x, slightly
−z), and slides along z: first sweep toward z 0, second toward z −80, alternating; ≈ 0.36·199 ≈ 71 units of travel
available, the arena is far smaller — it reaches the wall and stays.

## 4. Attacks

| attack | record | timing | notes |
|---|---|---|---|
| slam | 14 at the hand | state 1, when it has fallen to ground + 2 | physical, AT 20·mul; knock-down 70. The hand tracks the player while rising (≈ 120..150 ticks to y 15 from 3), then drops straight down |
| beam | 154, owner-attached: each record tick its origin = the hand's joint 1 (`f32(B+0x708..0x710)`), direction = (sin, cos)(yaw + 0.3), radius field `+0x108 = 3.0` | spawned at state 2 sub 10; lives while the hand is alive (`sub_43AE20` IDA 40195: removed when the owner's `+0x428 != 1`) — **its own lifetime not read** | "magic" list (152..154) → MA 0 → 1 damage per hit |

## 5. Kill recipe

It cannot be killed directly; kill Griff (`griff.md`). For the search:

- Predicate "slam incoming": `s32(H+0xa54) == 1 and s32(H+0xa5c) == 0 and f32(H+0x3f4) > 10` → keep moving;
  the drop point is the hand's (x, z) when `f32(H+0x3f4) > 15`.
- Predicate "beam live": `s32(H+0xa54) == 2 and s32(H+0xa5c) >= 10` → stay out of the line from the hand along
  yaw + 0.3; with the hand at x ≈ −20 the beam covers a band across the arena that moves with the hand's z.
- After a slam the hand lies still for 157 ticks: free time on Griff.
- Avoid (`--avoid`): `hp <= 0`. Do not put the hand's HP in the score (it never drops).

## 6. Arms checklist

| branch | predicate (H = the hand's actor) |
|---|---|
| descend | `s32(H+0xa54) == 8 and f32(H+0x3f4) > 3` |
| hover → sweep (count % 4 ≠ 0) | `s32(H+0xa54) == 2`, `s32(H+0xa60) % 4 != 0` |
| hover → slam (count % 4 == 0) | `s32(H+0xa54) == 1`, `s32(H+0xa60) % 4 == 0` |
| slam rise / fall / impact / rest | `s32(H+0xa5c)` 0 → 1 → 2 → 3..160; `s32(H+0xa00) == 0` while falling |
| sweep toward z 0 / z −80 | `s32(H+0xa64)` odd / even with `s32(H+0xa5c)` in 101..299 |
| beam spawn | a record of type 154 owned by H at `s32(H+0xa5c) == 10` |
| hit for 0 | player record lands: `f32(H+0x42c)` unchanged, SE 359 |
| death with Griff | `s32(H+0x428) == 2` at Griff's death tick 60, freed 2 ticks later |
| d5 release | `s32(H+0xa54)` 8 → 0 at t 240 |

## 7. Unresolved

- Record 154's lifetime, hit volume along the beam, and whether it hurts through the 30-tick invulnerability only
  once per pass.
- `+0xb04` (written 1 here and on every prop actor): meaning not read.
- Whether the idle threshold `t ≥ 60 + count` ever exceeds 200 (count would have to pass 140: no).
- Contact damage with the hovering hand.

## 8. Retail run (run `obs-50f`, Obsidian 50F, 2026-11-11)

Probe `docs/findings/probes/obsidian-41f-50f.md`. H = actor 1 (0x76c514); names `hst ht hsub hcnt hsw hx hy hz hhp ha00`.

**Stats (retail, `0x5c23f0 + 72*0x68`) override §2:** flags 1, HP **100**, EXP **30**, AT 20, DF **20**, **MA 35**, **MD 20**, rf 0.8 / **4 /
3 / 4.5**. d3 f49 (L28): **HP 660** (not 990). MA 35 means the beam (magic list) is **not** 1 damage: ftol(35·3.8/2 − MDEF/4) = 56 vs MDEF 41
(computed; no beam hit was taken).

| arm | status |
|---|---|
| spawn | **[obs f30200]** event tick 30, slot 1, (−35, 20, −25), state 8, HP 660 |
| descend | **[obs f30215..≈30320]** y −0.16/tick to 3.0 |
| release d3 | **[obs f30452]** 8 → 0 on event stage 3 (with Griff) |
| hover → sweep (count % 4 ≠ 0) | **[obs]** sweep 1 f30664 (`hcnt` 1), sweep 2 f31889, sweep 3 f33157, sweep 5 ≈f34501. Hover t ≥ 200 before each (plus hit-stop ticks) |
| sweep toward z 0 / z −80 | **[obs]** `hsw` 1 (odd): sub 101..299 f30981..31542, z −40 → **−27** (stops); `hsw` 2 (even): sub 101 f32202, z → **−53** by f32405. Sweep length sub 0..430 + 200 ≈ 630 ticks (f30664 → hover f31681) |
| beam spawn (record 154) | not read (no record-154 counter defined); never hit Louie (searches avoided `hst == 2 and hsub >= 10`) |
| hover → slam (count % 4 == 0) | **[obs f33992]** `hcnt` 4 → state 1 |
| slam rise / fall / impact / rest | **[obs]** rise to y > 15: sub 1 at f34122 (130 ticks); falling (`ha00` 0); **record 14 at y ≈ 1.1 (ground + 2), sub 2, f34149**; floor f34152 (sub 3); player 12.4 away — no damage. Rest 160 ticks not timed |
| hit for 0 | **[obs f31068]** a sword swing aimed at Griff also hit the Arm (y 2.0, sweeping): `H+0xb5c` −1 → 323 (the swing's hit id), HP 660 unchanged. At hover height y 3 the player's vertical window (±(2.4 + 1.6) around y + 1) does not reach the Arm's hit point (3 + 2.4) — computed, so it is hittable only while sweeping (y 2) or lying after a slam |
| death with Griff | **[obs f34763]** Griff's death tick 60: the Arm → dying (`+0x428` 2), freed the next tick (f34764) — and that emptied the table: event stage 3 → 4 on the same tick |
| contact damage | none observed (Louie stood within 2 of the hovering Arm several times without a hit) |
| d5 release | not reached |
