# Probe: ohm-exhaust

- **Task:** `tools/drive/tasks/ohm-exhaust.md` — Ohm (type 67) bee slowdowns + exhaustion, card `docs/findings/bosses/ohm.md`
  §3 / §6.
- **Run:** `ohx` · **start:** `bossrush-jade-amber@11640` (Amber Garden rush 2nd floor, d5 `floor` 34, crafted lineage — see
  `bossrush-jade-amber.md`). Louie L47 ATK 65 / DEF 85 / MDEF 44, HP 209 / SP 31 on arrival. Ohm L14, HP 570.
- **Driver:** subscription subagent. A loop of `rd search -j 2 --hold 40 --apply-best`, one per spawn: goal `bn >= K`, score
  `bn*1000 + bto - bees*30`, cell `(bn, bto//60, bees, int(px/4), int(pz/4), ost)`, **avoid `hp < H − 12 or ohp < O`** (H / O = now:
  no charge hit, no damage to Ohm; on a failed search `H − 32`); actions = aimed swing / walk at slots 1..12, retreat / strafe from
  Ohm, idle, 4 plain walks. A first pass (avoid `ohp < 250`) dropped Ohm 570 → 277 by `bn` 13: rewound to `bn` 9 (f13018). Food
  from the pause menu: Beef Bowl f15590 (+55), Watermelon f26203 (+60), Watermelon f30714 (+64). Kill: 2 damage searches.
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`).
- **Movie:** 31,189 frames. Filed as `tests/scenarios/ohm-exhaust/trace.jsonl`. No golden yet (`rhost.py golden ohm-exhaust`).
- **Verdict (2026-11-16):**
  ```
  task ohm-exhaust: step 1 f11881, step 2 f12988, step 3 f16483, step 4 f28854, step 5 f31189 — PASS
  fresh headless replay: frame 31189 rng 97a7af48 sem fffa6e990212b717 dev f006de44f86e71dd  (unskipped retail)  [win]
  live session:          frame 31189 rng 97a7af48 sem fffa6e990212b717 dev f006de44f86e71dd  [win]
  MATCH
  real-device replay:    frame 31189 rng 97a7af48 scene 1  (Windows, Direct3D 8)
  MATCH
  ```
- Strip looked at: f28300..28900 every 75 (`ohx/view/strip_28300_28900.png`): Ohm walking / charging at Louie with 5 bees; the
  charge hit for 14 at f28898.

## IDA check — the task predicates were wrong (edited)

`sub_422ED6`, all_ida.c:22097..22111: live = slots with `+0x428 != 0` and type 10; `v5 = 3; if (dword_438B4C8 == 3) v5 = 5; if
(dword_438B4C8 >= 4) v5 = 8;` — **no floor test**: the rush (dungeon dword 5) has **cap 8**, not the dungeon-2 cap 3. Then `if (live < cap
&& ++a64 >= 120) { ++a68; a64 = 0; if (a68 > cap) a64 = −200; if (a68 > 2cap) a64 = −400; if (a68 < 5cap) spawn type 10 }` → slowdowns
at `bn` > 8 / > 16, **39 bees**. Task steps 2..5 changed from 3 / 6 / 15 to 8 / 16 / 40 (the card §3 sentence "Boss Rush runs in dungeon 5 →
cap 8, 39 bees" was right; the task draft took the "Dungeon 2: 14 bees" line).

## Per spawn (`bn` = `+0xa68`, `bto` = `+0xa64` on the spawn tick, bees = live type 10 after it)

| # | frame | `bto` | bees | gap | | # | frame | `bto` | bees | gap |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 12000 | 0 | 1 | (120 after f11881) | | 21 | 18587 | −400 | 4 | 528 |
| 2 | 12124 | 0 | 2 | 124 | | 22 | 19115 | −400 | 3 | 528 |
| 3 | 12252 | 0 | 2 | 128 | | 23 | 19639 | −400 | 4 | 524 |
| 4 | 12372 | 0 | 3 | 120 | | 24 | 20167 | −400 | 3 | 528 |
| 5 | 12496 | 0 | 4 | 124 | | 25 | 20687 | −400 | 4 | 520 |
| 6 | 12616 | 0 | 5 | 120 | | 26 | 21207 | −400 | 5 | 520 |
| 7 | 12740 | 0 | 6 | 124 | | 27 | 21735 | −400 | 4 | 528 |
| 8 | 12864 | 0 | 7 | 124 | | 28 | 22255 | −400 | 5 | 520 |
| **9** | **12988** | **−200** | 8 | 124 | | 29 | 22791 | −400 | 3 | 536 |
| 10 | 13432 | −200 | 8 | 444 | | 30 | 23311 | −400 | 4 | 520 |
| 11 | 13838 | −200 | 8 | 406 | | 31 | 23835 | −400 | 5 | 524 |
| 12 | 14338 | −200 | 6 | 500 | | 32 | 24363 | −400 | 5 | 528 |
| 13 | 14662 | −200 | 7 | 324 | | 33 | 24883 | −400 | 6 | 520 |
| 14 | 14986 | −200 | 7 | 324 | | 34 | 25445 | −400 | 5 | 562 |
| 15 | 15310 | −200 | 8 | 324 | | 35 | 25969 | −400 | 6 | 524 |
| 16 | 16075 | −200 | 8 | 765 | | 36 | 26746 | −400 | 7 | 777 |
| **17** | **16483** | **−400** | 6 | 408 | | 37 | 27278 | −400 | 4 | 532 |
| 18 | 17007 | −400 | 6 | 524 | | 38 | 27802 | −400 | 5 | 524 |
| 19 | 17535 | −400 | 5 | 528 | | 39 | 28326 | −400 | 6 (slot 31) | 524 |
| 20 | 18059 | −400 | 6 | 524 | | **40** | **28854** | −400 | 5 — **no spawn** | 528 |

- Gaps under the cap: **120 / 320 / 520**, + 4 per connecting player hit (hit-stop: 124 / 128 / 324 / 524..536); longer ones = time at the
  cap 8 (444, 406, 500, 765) or paused in the menu (36: 777 incl. the f26203 meal). All 39 type 10; 9th / 17th set −200 / −400 as coded.
- **Exhaustion:** `bn` 40 f28854 (no bee), 41 f29386, 42 f29940, 43 f30484 — counter ticks on every ~540, nothing spawns. The 800-tick
  watch (f29668..30520) had **1 bee alive in slot 31** (my `bees` define counted slots 1..30 only; tool gap 2), so it was "1 bee, cap 8,
  timer running, no spawn", not "no bee" — same proof that the exhaustion is the counter, not the cap.

## Damage taken

| what | value | frames |
|---|---|---|
| charge (`ost` 2, `ot` 70..113, `odisc` 2.9..5.49) | **14 / 15** | 12018, 12619, 12866, 13856, 14351, 15077, 15332, 18638, 19125, 23386, 23888, 28422, 28898, 30294 |
| bee stings | 1 / 2 | 12775, 13018, 14080, 14222, 15857, 17573, 21264, 22287, 24991, 25488, 26300, 27540, 27997, 28358 |
| unattributed | 14 | 13139 (`ost` 3 `ot` 0, `odisc` 8.3 — not a burst shot: those start at `ot` 20) |

- Charge `ftol(30·m/2 − 85/4)` = 14..15 ⇒ **m = 1 + 0.1·lv = 2.4 at L14** (HP uses 1 + 0.2·lv = 3.8). The same `1 + 0.1·lv` fits the 40F
  row (L25: 3.5) and the ahriman beam at L12 (`ahriman-exhaust.md`). Stings 1 / 2 = the floor (17·2.4/2 − 21.25 < 0).
- No burst shot hit (card row stays open).

## Death + sweep

| frame | what |
|---|---|
| 31014 | killing hit 74 → 0 (`+0x428` 2) |
| 31019 | death tick 1, `p_inv` 1200 |
| **31118** | **tick 60: the last bee (slot 31) 1 → 0 — the last-of-kind sweep** |
| 31189 | tick 131: freed, `estage` 1, `gate` 3 → 0 |

## Card corrections (`ohm.md`)

- §6 bee slowdown / exhaustion reached ([obs f12988 / f16483 / f28854]): rush cap 8, 39 bees, as §3 says.
- Charge damage multiplier `1 + 0.1·lv` (not the HP's `1 + 0.2·lv`); at L14 vs DEF 85: 14..15.
- Death + sweep with one Ohm: sweep at tick 60, freed at tick 131 (§3 "≈ 132").

## Tool gaps

1. As in `ahriman-exhaust.md`: no "allocated this tick" actor field, no `nearest(COND)` helper.
2. `actors(COND, LO, HI)` with a narrow range silently misses actors in high slots (the 39th bee went to slot 31 with 5 bees alive):
   the 598-token limit pushes range narrowing; a cheaper full-table count (`count(T == 10)`) would avoid it.
3. **`live: err savestate pool exhausted`** after ~35 checkpoints on a 28k-frame movie: every `rd search` then fails with no
   `found:` line (my loop read it as "stuck" 3 times). `rd stop` + rebuild cleared it. `ck save` should evict, or the error should
   name the fix.
4. Pause-menu food is a 12-press menu walk (`~ D A`, `D ×11` to the last bag row, `A A`); a named `bag_cur` / item-at-cursor var would
   let `--until` find the row.
