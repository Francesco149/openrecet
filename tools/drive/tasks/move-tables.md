---
name: move-tables
start: end:dungeon3-run2-save
steps:
  - the counter menu list is up: cc08 == 0 and s32(0x438b74c) >= 10
  - Move Tables mode: cc08 == 2
  - back in free roam afterwards: cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the counter menu's "Move Tables"** (`cc08 == 2`, `FUN_0048940e`) — `PORT-DEBT(counter-menu-move-tables)` in
the port; no recording enters it.

Read first: `docs/findings/shop-counter-menu-RE.md` (the counter menu: `A` at the counter → `cc08 == 0` list A
`DAT_074b2d98[]`, count `DAT_005cd0bc`, cursor `cc0c` = `s32(0x438cc0c)`; entries 0 Open Store, 1..4 decor, 5 Move
Tables (merchant level ≥ 9: `s32(0x450fb98)` is the 0-based level), 6 Cancel; choosing dispatches after 15 ticks of
`cc08 == 10`), and the IDA body of `sub_48940E` in `/mnt/ephemeral/openrecet/decompiled-ida/all_ida.c`.

The start save is day 14 (title → Continue → slot, ~190 ticks). Walk to the counter, open the menu, choose Move
Tables. Inside: pick a table up, move it, put it down somewhere else, and leave the mode with the change kept. If
the list has no entry 5 at this merchant level, report the level and the list and stop there.

The finding is the mode itself: which variables hold the selected table, its grid position and the carried
state; what each button does; what makes a placement legal; how the mode ends (and whether the furniture bytes of
the working bank change — `tools/save_arena_map.json` names the regions). Strips of the mode with a table in hand
and of the shop afterwards.
