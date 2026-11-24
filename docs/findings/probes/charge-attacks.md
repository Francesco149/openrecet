# Probe: charge-attacks

- **Run:** `cha-t` → scenario `charge-attacks` · **start:** save:824053720e01 (crafted hire lineage) · **task:** `tools/drive/tasks/charge-attacks.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/charge-attacks/` (10,882 frames)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f10882, rng de03842f); golden + triage: **identical**
  (with the arma-weapons record fixes); gate row. Not reached: the water splash (no water on Hall of Trials 1F).

Tielle's held draw `FUN_0049791f` and Caillou's charge `FUN_00497e3a`.

| Tielle | draw at release | records (at p_actt 13) | result |
|---|---|---|---|
| 1 | 19 | 1 × 77 (0.3 / tick, ~27 ticks) | miss |
| 2 | 59 | 1 × 78 (0.5 / tick) | kill |
| 3 | 99 | 3 × 79 (spread) | kill |
| 4 | 149 | 5 × 80 | kill |
| 5 | 189 | 6 × 165 | miss, **crit** (freeze `0x438bed8` = 30 at p_actt 5) |
| 6 | 239 | 5 × 166 | miss |
| 7 | 269 (cap, held 320) | 5 × 166 | miss |
| 8 | skill key during the draw (59) | draw → 0, skill 0 | — |
| 9 | 156 with `daea0` up (+4 / tick) | 80 | miss |

| Caillou | charge | record | result |
|---|---|---|---|
| short | 19 | 4 at p_actt 12 | kill |
| charged | 51 | 82 at p_actt 20 — a travelling projectile (0.3 / tick, alive > 36 ticks) | kill at ~12 units |

- `daea0` = 360 from Tielle's skill state 7 (key 4, 10 SP), counting down while she draws. An enemy contact during the
  draw resets it. An arrow opens the Hall of Trials 5F return door.
- Tool gaps: `rd ram records` has no `--at-frame` / scan form (a `recs(T)` count expression would help); the hire
  predicates fire during the dungeon load (add "equip prompt gone"); `s32(0x438b4dc)` stays ≥ 4 back in the guild.
