# Probe: nagi-elan-edges

- **Run:** `nee-a` → scenario `nagi-elan-edges` · **start:** save:824053720e01 (crafted hire lineage) · **task:** `tools/drive/tasks/nagi-elan-edges.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/nagi-elan-edges/` (11,790 frames)
- **Verdicts (2026-11-24):** check PASS (task predicates + `loading == 0`); verify --real MATCH; golden + triage: **identical**; gate row. No port change.

| # | action | frame | records | p_hurt / p_act / p_oct |
|---|---|---|---|---|
| 1 | Nagi's dash into a slime (`U . U*20`) | dash 1395, hit 1415 | 139 (0x8b) killed on contact (~2.6 units), "Back Attack!" 15 → 11 | 15 / 1 / 0 |
| 2 | Nagi's swing facing p_oct 2 | 1483 | 88 (0x58) at p_actt 16 | 0 / 2 / 2 |
| 3 | Nagi hurt with a swing record alive | 1695 | 88 (age frozen at 7 by the hit-stop) killed at 1700 | 30 / 1 / 6 |
| 4 | Elan punch / follow-up point-blank into a wall | 10782 / 11500 | 91 (0x5b) / 92 (0x5c) killed at age 2 (the wall path) | 0 / 2-3 / 6 |
| 5 | Elan hurt with a punch record alive | 11609 | 91 killed the hurt tick (age 11) | 30 / 1 / 4 |
| 6 | Elan skill 0 / skill 1 into a wall | 11653 / 11720 | 95 (0x5f) full 25 ticks; 133 (0x85) killed at age 1 | 0 / 4-5 / 6 |

- Dash: Nagi is chara 31 in the dash-flag table (`db040` = 0x85 → record 139); Elan 0xa2 → 138. `db034` = 1 while
  dashing. A diagonal `dpad()` both starts (direction edges = a double tap) and ends a dash.
- Elan's follow-up only fires after a punch that connected.
- Hall of Trials 5F: the return door is object 1 (kind 6, (−40, −30)), so `doorobj` / `doorst` read the wrong object there.
