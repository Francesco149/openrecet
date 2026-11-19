---
name: title-options-save-clear
start: save:17264a73
steps:
  - the title's Options page, slid in: scene == 0 and s32(0x9643524) == 2 and s32(0x9643520) == 10
  - a slider changed (the settings dirty flag): s32(0x9643560) == 1
  - exit with the change → saved, back on the main menu: s32(0x9643560) == 0 and s32(0x9643524) == 2 and s32(0x9643520) == 0
  - Options again, the "Clearing all data. Are you sure?" confirm up: s32(0x9643524) == 2 and s32(0x9643548) == 1
  - answered Yes (the data cleared; the settings count as changed): s32(0x9643548) == 0 and s32(0x9643560) == 1
  - exit → saved, main menu: s32(0x9643560) == 0 and s32(0x9643524) == 2 and s32(0x9643520) == 0
forbid:
  - left the title: scene != 0
---
Probe: **the title Options page's save paths** (`FUN_0049a59e`, all_ida 112117..112160 and its top 111725):
- A / B after a slider change (DAT_09643560 1 → 2) hides the hand at once and, next tick, writes the header with
  `FUN_004905a8(-1)` (save.dat + _save.dat), then folds back to the main menu; A / B with no change (→ 3) just folds.
- Row 5 ("Clear data") + A opens `FUN_00434def("Clearing all data. Are you sure?", 0, 1)` (DAT_09643548 = 1); Yes →
  `FUN_004902aa` (header magic 0, `FUN_004901c2` re-seeds every bank fresh, save.dat written alone) and DAT_09643560 = 1;
  No → back to the row.
The port had both as stubs (no save write, no prompt). No recording reaches them.

Start from a played save (`17264a73`, day 14) so the clear visibly wipes something. From boot, the title's main menu:
move to **Options** (`rd text` names the rows), A. Change one slider (Left / Right on a slider row; note which and its
value), then B to exit. Re-enter Options, go to the last row (row 5, Clear data), A, and answer **Yes** (note the box
text with `rd text`). Then B to exit. Record per step the frame; after the end, `rd ram` / `rd eval` the header:
`s32(0x56e5770)` (magic) and the slot list on the title (Continue / Load should show no saves — check with `rd text`
after moving to Continue, without loading). Do not leave the title (no New Game). The run ends on the title.
