---
name: dungeon2-21f
start: end:dungeon3-deep
steps:
  - loaded into dungeon 2 from the 20F save: scene == 1 and s32(0x438b4c8) == 2 and dlg == 0
  - one floor below the start: s32(0x438b4c8) == 2 and s32(0x438b4cc) >= 21
  - two floors below the start: s32(0x438b4c8) == 2 and s32(0x438b4cc) >= 22
  - floor 25 reached: s32(0x438b4c8) == 2 and s32(0x438b4cc) >= 25
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **dungeon 2 below 20F** — floors 21..29 (enemy mixes no recording has met), the double crown slime on 25F,
Tielle on 30F. `docs/FRONT.md`'s human queue asks the owner to play this; do as much of it as search allows.

The start save is the in-dungeon Save after 20F of dungeon 2 (title → Continue → the newest slot). `floor` is
`s32(0x438b4cc)` (DAT_0438b4cc; the port mirrors it as "next floor" — validate what it reads on the floor you load
into and adjust the step thresholds if it is off by one: copy this file into your run dir, edit, say why), `dungeon`
is `s32(0x438b4c8)`.

This is action play: move, fight, find the floor's exit, repeat. It is what `rd search` exists for — one search per
sub-goal (reach the exit; land one hit; get away from a hit), with the mechanic's state in the cell and the
constraints in `--avoid`, `-j 8` when a goal is hard. You will need state the table does not have yet: the party /
leader HP, the enemies (the actor table), the exit's position or the "floor cleared / gate open" flag. Sources: the
port (`src/actor_table.h`, `src/dungeon_flow.h`, `src/stage_gate.h`, `src/dungeon_party.h`, `src/state_va.c`), the
findings index, IDA, and mark / diff. Name what you find.

Stop conditions, in order of preference: 25F reached and its boss beaten; as deep as you got with the party alive;
or a clear account of what blocks search-driven dungeon play (which sub-goal the search cannot find and why). Keep
checkpoints per floor. Do not grind: if a floor costs more than ~15 searches, write down why and stop.

Report, beyond the card's list: per floor the frames, the enemy types met (actor type ids), HP at entry / exit, the
search settings that worked (goal, cell, avoid, actions, time) and those that did not; ticks/s you saw; every tool
gap that slowed you.
