---
name: pause-options-change
start: end:dungeon3-run2-save
steps:
  - the pause menu is up: scene == 9
  - the Options page is open: scene == 9 and pm_type == 2 and pm_state == 10
  - back in the game: scene == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the pause menu's Options page, changed and saved** — change settings (BGM / SE / voice volumes, Message
Speed, Unread Text Skip), leave the page so that the change is kept, re-open it to see it stuck. Only an unchanged
visit is recorded (`house-pause-options`).

Read first: `docs/plans/pause-menu.md`, `docs/findings/title-settings-submenu.md` (the title's Options has the same
sliders: the slider state machine), `src/scene_pause.h` (search "Options": "A/B exit (saving if dirty)"). The
sliders persist in the save header (`tools/save_arena_map.json`: header dwords 1 `se_slider`, 2 `bgm_slider`, …;
the save arena is at `0x56e5770`, so `u32(0x56e5774)` is the SE slider) — find out whether leaving the page
writes save.dat (an overlay write: `rd endsave` succeeds only if the run committed a save) or only memory.

The start save is day 14 (title → Continue → slot: `.*60 A .*30 A`; a morning event follows: `~` → A on Yes). ESC
(`~`) opens the pause menu (`scene == 9`); `pm_sel` / `pm_type` / `pm_state` are in `rd ram R list`.

Report, beyond the card's list: each setting's variable, range and step, what A / B / left / right do on the page,
which exit keeps the change, what is written where and when (header dwords before / after; did save.dat change?),
and a strip of the page with changed values.
