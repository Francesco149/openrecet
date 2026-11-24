---
name: charge-attacks
start: save:824053720e01
steps:
  - on a dungeon floor with control, Tielle leading: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1 and s32(0x438b7d8) == 3
  - Tielle's fully drawn shot (the draw counter past 225): s32(0x438b4dc) >= 4 and s32(0x56daff4) >= 225
  - on a dungeon floor with control, Caillou leading: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1 and s32(0x438b7d8) == 2
  - Caillou's charged swing (the charge past 45): s32(0x438b4dc) >= 4 and s32(0x56daff4) >= 45
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **the charge attacks** — Tielle's held draw `FUN_0049791f` and Caillou's held charge `FUN_00497e3a` (port
`party_attack_tielle` / `party_attack_caillou`, `src/dungeon_party.c` ~1569 / ~1897; read their header comments). The
`hire-tielle` / `hire-caillou` probes only tapped A: 44 of 71 / 37 of 75 blocks never ran (`where.py FUN_0049791f
--unhit`). Tielle: while A is held she walks and draws (`daff4` = `s32(0x56daff4)` +1 a tick, +4 with `daea0` up, to
269; SE 371 at every 45; the mote 99 from 10); released, the arrow by the draw: daff4 / 45 → records 77 / 78 / 79 / 80 /
165 / 166; a crit roll at 1. Caillou: held → the charge (anim 4, daff4 counts; SE 371 at 10, SE 353 + the ring 98 at 45);
released below 45 → the short swing (hit record 4), at ≥ 45 → the charged swing (hit record 82 at 20); both splash in water.

**Crafted start** `824053720e01` (everyone hirable at level 40, day 15; `SAVES.md`). Read `docs/findings/probes/hire-tielle.md`
and `hire-caillou.md` first (route, the chara id each reads as in `s32(0x438b7d8)` — fix the step predicates if they differ).

Do: hire Tielle, Hall of Trials 1F: release shots at draw levels in every band — ~20, ~60, ~100, ~150, ~190, ~240 and a
full 269 (each a separate hold → release; aim some at an enemy: `rd ram R actors`), and one draw interrupted by a skill
key. Then back to town (or a game over is NOT ok — use the floor's exit / the return), hire Caillou, a floor: a short
swing (release < 45), a charged swing (hold ≥ 45, release) aimed at an enemy, and if a floor has water, a swing there.
Record per shot: draw / charge at release, the record type seen (`rd ram R records`), hit or miss, any crit.
