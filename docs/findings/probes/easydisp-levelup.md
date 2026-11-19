# Probe: easydisp-levelup

- **Run:** `easydisp-levelup` · **start:** `dungeon0-full@3935 --ini setup.s_easydisp=1` (the human recording up to the
  first 1F frame with control, replayed under the override) · **task:** `tools/drive/tasks/easydisp-levelup.md`
- **Driver:** subscription subagent (2026-11-19)
- **Scenario:** `tests/scenarios/easydisp-levelup/` (7135 frames; movie header `{"ini": "setup.s_easydisp=1"}`)
- **Verdicts (2026-11-19):** check PASS (E0 = 0, L0 = 0, E1 = 365 filled in; steps at f3936 / 3937 / 4357 / 6703 / 7105;
  with step 3 as `exp >= 10` (a kill, not a hit) it lands at f4417); verify MATCH (headless + real device, rng 92fdb71f).
  Golden / triage: not run (orchestrator).

Retail facts (observed live, leader record `bank + 0x2ceb0 + 0x6c·DAT_0438b7d8`):
- The override is live from boot: `s32(0x438b1a0)` = 1. The floor is **Hall of Trials 1F** (HUD; `s32(0x438b4dc)` = 4),
  not Jade Way as the task text says. 13 enemies, all type 0, HP 15.
- First control after the floor load: f3935 (f3511 = the load tick: `lock` reads −1 with `loading` = 1; then lock 0 and
  the 1F event at f3710..3845).
- **Hits pay EXP too** (FUN_0042e791's arm, all_ida ~29926): under easydisp each would-be orb adds +1 straight to EXP
  (`dmg/2/2` orbs, ≤ 25). Observed +1 per sword hit (8–9 damage), +3 on a 15-damage one-shot. **That arm never calls
  FUN_0048a383**: a hit that pushes EXP past `next` would not level up until the next kill (not reached here).
- **Kills** (FUN_0042e1a6): +10 per would-be orb; orbs = `(stat + x/2)·0.8 + DAT_0438b90c`, the on-screen
  **"N Chain!"** counter, so the kill payout grows 2, 3, 4 … orbs along the chain:

  | kill | frame | slot | EXP before → after | of which kill | chain after |
  |---|---|---|---|---|---|
  | 1 | 4417 | 3 | 2 → 22 | 20 | 0 |
  | 2 | 4457 | 2 | 22 → 52 | 30 | 1 |
  | 3 | 4815 | 6 | 52 → 93 | 40 (+1 hit) | 2 |
  | 4 | 5269 | 7 | 96 → 146 | 50 | 3 |
  | 5 | 5753 | 11 | 148 → 209 | 60 (+1) | 4 |
  | 6 | 5921 | 12 | 211 → 282 | 70 (+1) | 5 |
  | 7 | 6703 | 9 | 284 → 365 | 80 (+1) | 6 |
  | 8 | 7105 | 10 | 365 → 458 | 90 (+3) | 7 |

- **Level-up at f6703, the same tick as kill 7:** level 0 → 1 (HUD "LV 2"), `+0x40` max HP 20 → 24, `hp` 24 at once (the
  HUD bar counts up 20 → 24 over ~20 ticks), `+0x48` (previous threshold) 0 → 300, `next` 300 → 900, EXP not reset
  (cumulative). SP 10/10 unchanged. A red "LEVEL-UP!" banner under the chain counter, a burst on Recette.
- **No EXP orbs:** table C (`0x6956cd8 + i·0x94`, type −1 = free) holds no orb at any frame; the only entries are one
  type-0xc28 drop at kills 1 (f4417, gone at f4696) and 7 (f6703).
- One enemy respawned at f6096 (alive count 7 → 8).

Tool gaps: `rd check` does not parse `--ini` in a task's `start:` (it rejects the line; the filled copy used
`start: dungeon0-full@3935`, the run's movie carries the ini); no `rd rm` for scratch runs (removed the run dir by hand);
no named state for table C (orbs / drops) or the chain counter `0x438b90c`.
