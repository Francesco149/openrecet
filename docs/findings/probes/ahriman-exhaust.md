# Probe: ahriman-exhaust

- **Task:** `tools/drive/tasks/ahriman-exhaust.md` — boss ahriman (type 54) minion slowdowns + exhaustion, card
  `docs/findings/bosses/boss-ahriman.md` §3 / §6.
- **Run:** `ahx` · **start:** `bossrush-jade-amber@10219` (Amber Garden rush 1st floor, d5 `floor` 33, crafted lineage — see
  `bossrush-jade-amber.md`). Louie L47 ATK 65 / DEF 85 / MDEF 44, HP 228 / SP 51.
- **Driver:** subscription subagent. Every step by `rd search -j 2 --hold 30 --apply-best`, goal `bn >= K`, score
  `bn*20 - bats*3`, avoid `hp < H or bhp < 200` (boss kept at 340 the whole spawn phase); actions = aimed swing / aimed walk
  at actor slots 1..16 (per-slot `sNx sNz` defines) + retreat / strafe from the boss + idle. Kill: 2 damage searches (score
  `-a0hp`). No food eaten.
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`).
- **Movie:** 14,959 frames. Filed as `tests/scenarios/ahriman-exhaust/trace.jsonl`. No golden yet (`rhost.py golden ahriman-exhaust`).
- **Verdict (2026-11-16):**
  ```
  task ahriman-exhaust: step 1 f10476, step 2 f11284, step 3 f12651, step 4 f13331, step 5 f14959 — PASS
  fresh headless replay: frame 14959 rng 8cbcd394 sem 25adf3b7416a2c1b dev 2767079f1f013c3e  (unskipped retail)  [win]
  live session:          frame 14959 rng 8cbcd394 sem 25adf3b7416a2c1b dev 2767079f1f013c3e  [win]
  MATCH
  real-device replay:    frame 14959 rng 8cbcd394 scene 1  (Windows, Direct3D 8)
  MATCH
  ```
- Strip looked at: f13300..13420 every 15 (`ahx/view/strip_13300_13420.png`): the 20th counter tick with 3 bats up, then a
  beam wind-up / fire with Louie beside the strip.

## IDA check (`sub_426811`, all_ida.c:24596..24668)

Kinds `v7` / cap `v8`: default 2 / 3; `dword_438B4C8` (dungeon) 2 → cap 4; 3 → 3 / 6; 4 → 4 / 8; 5 → floor
`dword_438B4CC` ≥ 45 → 4 / 8, ≥ 38 → 3 / 6, else cap 4 (kinds stay 2). Live count = slots with `+0x428 != 0` and type ∈
`dword_52B95C..68` (6..9). `if (live < cap && ++a64 >= 30) { a64 = 0; n = ++a68; if (n > 2cap) a64 = −60; if (n > 4cap)
a64 = −120; if (n < 5cap) spawn type 6 + rand % kinds }`. `floor` = `s32(0x438b4cc)` reads **33** here (not 0-based in the
rush). **The task predicates were right; no edit.**

## The setup

| frame | what |
|---|---|
| 10219 | arrival, `bst` 8, `bt` 0 (`lock` reads −1 on the arrival tick, 0 after) |
| 10253 | "Equip items in bag?" answered (`A`): **the boss's state 8 does not tick until it is answered** (400 idle ticks left `bt` 0) |
| 10492 | `bst` 0, control (`lock` −1); 240-tick descent f10253..10492 |

## Per spawn (`bn` = `+0xa68`, `bto` = `+0xa64` on the spawn tick, `bats` = live 6 / 7 after it)

| # | frame | `bto` after | type | bats | gap |
|---|---|---|---|---|---|
| 1 | 10522 | 0 | 7 | 1 | (30 after state 0) |
| 2 | 10552 | 0 | 7 | 2 | 30 |
| 3 | 10582 | 0 | 7 | 3 | 30 |
| 4 | 10612 | 0 | 7 | 4 | 30 |
| 5 | 10804 | 0 | 6 | 4 | 192 (cap held) |
| 6 | 10850 | 0 | 7 | 4 | 46 |
| 7 | 11082 | 0 | 7 | 4 | 232 |
| 8 | 11194 | 0 | 6 | 4 | 112 |
| **9** | **11284** | **−60** | 7 | 4 | 90 |
| 10 | 11615 | −60 | 6 | 2 | 331 |
| 11 | 11709 | −60 | 7 | 3 | 94 |
| 12 | 11799 | −60 | 7 | 4 | 90 |
| 13 | 11955 | −60 | 7 | 3 | 156 |
| 14 | 12053 | −60 | 6 | 3 | 98 |
| 15 | 12143 | −60 | 6 | 4 | 90 |
| 16 | 12561 | −60 | 6 | 2 | 418 |
| **17** | **12651** | **−120** | 6 | 3 | 90 |
| 18 | 12801 | −120 | 7 | 4 | 150 |
| 19 | 13033 | −120 | 6 | 4 | 232 |
| **20** | **13331** | −120 | — (no spawn) | 3 | 298 |
| 21..29 | 13527, 13681, 13835, 13989, 14139, 14289, 14439, 14597, 14763 | −120 | — | 2, 1, 1, 0 … 0 | 196, 154, 154, 154, 150, 150, 150, 158, 166 |

- 19 bats in all (types: 7 ×11, 6 ×8). Gaps under the cap: **30 / 90 / 150** exactly (rows 2..4, 9→, 12, 15, 17, 18; 25..27 with
  no bats); a gap stretches by 4 per connecting player hit (hit-stop: 154 / 158 / 166 while I swung at bats or the boss) and by the
  time spent at the cap (192, 232, 331, 418).
- **Exhaustion:** from f13839 no bat lives anywhere in the table (slots 0..63) and none comes: `bn` 23 → 29 over the 956 ticks to the
  kill (f13839..14795), every 150 + hit-stop.
- Spawn points not recorded (tool gap 1).

## Damage taken (HP 228 → 48)

| frame | − | source |
|---|---|---|
| 11383, 12253, 13106, 13519, 14728 | 29, 28, 31, 27, 27 | beam (`bst` 1, `bt` 99..128) |
| 10977, 11319, 11675, 11890, 12189, 13042 | 7, 7, 7, 5, 7, 5 | bats (outside the beam window) |

- **Beam = 27..31 vs MDEF 44 at L12**, not 40..50 (the task's guess from the L19 / L27 rows): `35·(1 + 0.1·12)/2 − 44/4` = 27.5 + roll,
  the same AT/MA level multiplier `1 + 0.1·lv` as Ohm's charge (`ohm-exhaust.md`).
- **Bat shots DO hit** (5..7, = MA 15 / 17 · 2.2 / 2 − 11): the Lapis trio card's "bat shots never hit" was luck / distance.

## Death

| frame | what |
|---|---|
| 14795 | killing swing 53 → 0, `+0x428` 2 |
| 14800 | death tick 1 (4 hit-stop frames), `p_inv` 1200 (last 54) |
| 14899 | tick 60 (slow-mo: 99 frames for 59 ticks); no actor left to sweep |
| 14959 | tick 120: freed, `estage` 1, `gate` 3 → 0 |

## Card corrections (`boss-ahriman.md`)

- §6 slowdowns / exhaustion reached ([obs f11284 / f12651 / f13331]); 19 bats on d5 f33, as the code says.
- Beam damage formula: magic at `MA·(1 + 0.1·lv)/2 − MDEF/4` (27..31 here); bats' shots land (5..7).
- On the rush's first floor the descent waits for "Equip items in bag?" (the boss AI is frozen until it is answered).

## Tool gaps

1. No "new actor this tick" field: `actors(AGE == 0 …)` never matched at a spawn, so spawn points / slots need per-slot scans.
   A `NEW` (allocated this tick) field in `actors()` or `rd ram R actors --at-frame F` would give the point in one call.
2. Searches started at the same bat slots 1..4 stalled for 2 × 160 s (`32 cells`) once the bats moved to slots 12..15 (freed slots are
   reused high); slot-agnostic actions needed 32 per-slot defines. A `nearest(COND)` x / z helper (nearest actor matching COND) would
   make `{dpad(nx - px, nz - pz) | A}` one action.
3. The Bash tool backgrounds any command after ~180 s, so a chained search loop has to run detached + polled.
