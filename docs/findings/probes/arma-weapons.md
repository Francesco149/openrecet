# Probe: arma-weapons

- **Run:** `arw-1` (live, render skip on) → `arw-2` (the same movie, skip off) → scenario `arma-weapons` · **start:** save:352fc5a20843 (**crafted**: `824053720e01` + items 701..715) · **task:** `tools/drive/tasks/arma-weapons.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/arma-weapons/` (12,477 frames)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f12477 rng 59acb130); `rd verify` on `arw-1` MISMATCH in
  the state hash only (frame + RNG equal) — the render skip `414ee2` changes state here (open item below).
  Port triage: **split f2514** (recb #2 +0x90) → fixed; **split f6775** (3 extra draws) → fixed; then **identical**
  ticks 1..12777; gate row.

All 15 of Arma's weapons (her weapon slot; `wpn` 701..715), a tap and a held burst each — every record kind matches
`party_attack_arma`'s table. Gauge `s32(0x56db0e0)`, max 100.

| id | name | kind | hold loops | gauge tap / hold |
|---|---|---|---|---|
| 701 | Punch Unit | 113 | no (40) | −10 / −10 |
| 702 | Hammer Arm | 117 | no | −10 / −10 |
| 703 | Disc Unit | 119 | no | −5 / −5 |
| 704 | Flamethrower | 118 | 19 → 9 ×4 | −5 / −25 |
| 705 | Gatling Unit | 120 | 17 → 9 ×5 | −3 / −18 |
| 706 | Null-parts | 115 | 14 → 9 ×8 (−2), to gauge 0 | −2 / −17 |
| 707 | Drill Arm | 114 | 30 → 20 ×3 (−5) | −5 / −20 |
| 708 | Gumball Unit | 126 | 14 → 9 ×10 (−1) | −1 / −11 |
| 709 | Crisis Cannon | 116 | no (60) | −10 / −10 |
| 710 | Messiah Unit | 123 | no | −10 / −10 |
| 711 | Gravity Cannon | 121 | no (60) | −5 / −5 |
| 712 | Chaos Unit | 125 | no | −10 / −3 (to 0) |
| 713 | Phalanx Cannon | 122 | 17 → 9 ×6 | −3 / −21 |
| 714 | Pandora Unit | 118 (random at equip) | 2nd hold 19 → 9 ×4; a trap roll | −5 / −25 |
| 715 | Omega Cannon | 124 (5 beams) | no (60) | −5 / −5 |

## The two port bugs

1. **f2514, record 0x75 (Hammer Arm's 117) ROT_X off by ~8e-6.** `body_0x75`'s phase-1 angle went through a local
   `angle_step_toward` in plain float C (gnu11 keeps it in x87 registers); retail `FUN_00482ae7` spills every wrap step to
   float (`fstps`), and the caller rounds the target once after `pc·2π/n + frame·0.04f`. Now the shared x87-faithful
   `angle_ease` (FUN_00482ae7) + spilled target / scale / position / the boost's CRT sin·cos (0x43e7e1..0x43e9cb).
2. **f6775, record 0x74 (Crisis Cannon's 116): 3 extra RNG draws.** The every-4th-tick smoke `FUN_004147d5(…, 0x2c, 1.8,
   −1, 0.0, 3)` passes **shape mode 3** (the owner's +0x904.. drive the body has just written; no draws); the port passed
   0 (random jitter, 3 draws). An audit of every overlay-spawn call site's arguments followed.

## Facts

- The gauge refills only after it hits 0: 90 ticks, then +10 every 5 ticks; frozen while paused; a shot with less gauge
  than its cost still fires (gauge → 0). Non-looping weapons need a new press.
- Pandora's trap: kind −1, three type-140 records with no owner, no gauge cost; they hit Arma (−2 HP) and flew on past
  age 1170.
- The bag screen holds 15 of 20; Arma's Equip page: weapon / armour / hat; the removed weapon goes to the bag slot.
- Tool gaps: `rd play --watch` shows only the last rows; `rd scan` has no `--rows`; dpad-walking at an enemy sticks on
  walls (a scored walk-only search works); search without `--actions` adds A presses.
