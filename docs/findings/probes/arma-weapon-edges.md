# Probe: arma-weapon-edges

- **Run:** `awe-1` → scenario `arma-weapon-edges` · **start:** save:352fc5a20843 (**crafted** hire lineage + items 701..715) · **task:** `tools/drive/tasks/arma-weapon-edges.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/arma-weapon-edges/` (9,627 frames; Hall of Trials 1F / 2F)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f9627, rng 8d08e8fb); golden + triage: **identical**; gate row. No port change.

The record branches `arma-weapons` missed (`FUN_0043ae20`; `findings/retail-coverage.md` "Dead in retail" lists the rest).

1. **Hammer Arm boost** (record 0x75): 9 taps, 7 alive at f3717 → the boost (age 100 → 200); one flew into a wall and died
   at age 215 with no bounce; facing +z: boost f4043, bounces f4076 / f4105, killed on the 3rd bounce f4130 (age 287). The
   other six orbit at age 100 through weapon swaps until the floor changes.
2. **Omega Cannon** (0x7c): at the bare wall 4 beams hit (z −125.5) at age 23–25 → age 200, died at 232; toward an enemy each
   beam homes from age 21 (`+0x114` = the actor, `+0x9c` 1..40), overshoots, lands (y 1), phase 2 ~16 ticks; facing away,
   one beam hit the enemy mid-air (f4949, 15 → 0).
3. Punch / Drill / Chaos facing ±x (`p_oct` 6). **`p_oct` only ever reads 0 / 2 / 4 / 6** (a held diagonal gives a
   neighbouring even octant; record velocity follows the true yaw `+0xea4`) — the odd-octant arm may be unreachable.
4. Wall tail: the Drill point-blank into a pillar / the wall → 15 particles of types 41 / 42 (table A); the Phalanx
   point-blank spawns past the wall face and never hits it (the table-B tick flag `0x6a46f98` turns 1 only once the state
   machine runs, age ≈ 6); from ~4 units back it is killed on the wall at age 7.
5. Hurt kills: Crisis record 116 and Gravity 121 are killed in the same tick as `p_hurt` (30) — on 2F (on 1F no enemy hit a
   standing Arma, perhaps the orbiting Hammer balls). Skill 4 (0x9b) not tried.

Tool gaps: no `rd ram R particles` (table A `0x69b2f80`, stride 0x94, type +0x30); no previous-tick reference in a stop
condition; the bag / Equip list cursor is not in `rd text`; `ck load` cuts the movie.
