# Probe: finishers-boss-web

- **Run:** `fbw-a` → scenario `finishers-boss-web` · **start:** save:824053720e01 (crafted hire lineage) · **task:** `tools/drive/tasks/finishers-boss-web.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/finishers-boss-web/` (9,500 frames)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f9500, rng 22763804). Port triage: **split f9450** (recb #0
  +0xa8 / +0x108: the web's LIFE_MULT) → fixed → **identical**; gate row.

- Caillou's finishing blows on 2-HP slimes: short swing (record 4, f1662) and charged (record 82, f2138): the hit value
  min(damage, HP left) / 10 = 0 → clamped to 1. `DAT_06a46f94` is reset every pass of record 4's hit loop (reads 0
  after the tick); the owner's +0xe30 (`0x56dafe8`) keeps it, countdown +0xe38 = 30.
- Charme's web (record 83 = 0x53, key `3`) on Jade Way's Boss Rush floor 30 (crown slime): died at **age 120** — the
  boss-floor lifetime (`FUN_004319d6` holds).

## The port bug

`body_0x53`'s kill age reads `FUN_004319d6` through a hook production never installed (default 0) → the party web lived
600 ticks on boss floors and its LIFE_MULT ramp-down started at 555 instead of 75 (the split at age 75). `record_hit.c`
now installs `stage_gate_query` (FUN_004319d6). The other records-B hooks were already installed (FUN_0044b255 is `ret`).
