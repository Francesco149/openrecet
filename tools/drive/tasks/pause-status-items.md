---
name: pause-status-items
start: save:64d43650865f
steps:
  - the pause menu is up: scene == 9
  - a pause page is open: scene == 9 and pm_state == 10
  - back in the game: scene == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the pause menu's Status and Items pages in week 3** — Status (the party list `FUN_0047f1ce`, verified only
right after Louie joins) and Items (tabs, scrolling, the detail box; "the pause Equip variant" is listed unverified).

**Start save:** the week-3 save of `payday-week2` (sha 64d43650865f). ESC (`~`) opens the pause menu (`scene == 9`);
the main page order is Status, Items, Encyclopedia, Options, Save, Exit Game (`pm_sel` 0..5, `pm_type`,
`pm_state` 10 = a page is open; `docs/findings/probes/pause-options-saved.md` shows the page mechanics for Options).

Open Status: move through whoever is listed, open anything it offers (equipment? skills?), close. Open Items: go
through every tab, scroll a long list to its end and back, open an item's detail if there is one, try A on an item
(is there an Equip / Use variant here, or only in a dungeon? say what the game offers), close. Leave the pause menu.

Read `docs/plans/pause-menu.md` and `src/scene_pause.h` first. Report, beyond the card's list: per page the
variables for cursor / tab / scroll, what each button does, the text shown; strips of both pages.
