---
name: pause-encyclopedia
start: save:64d43650865f
steps:
  - the pause menu is up: scene == 9
  - a pause page is open: scene == 9 and pm_state == 10
  - back in the game: scene == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the pause menu's Encyclopedia in week 3** — every tab / category, list scrolling, an entry's detail page,
unknown ("???") entries, the completion figures. The port lists it DONE from one short recording; the data it
shows (item names, descriptions, categories, what counts as "seen") is also reference material — transcribe it.

**Start save:** the week-3 save of `payday-week2` (sha 64d43650865f). Load: `.*60 A .*30 A`, wait for
`scene == 1 and dlg == 0`. ESC (`~`) opens the pause menu (`scene == 9`); the main page order is Status, Items,
Encyclopedia, Options, Save, Exit Game (`pm_sel` 0..5; `pm_type` 6 = Encyclopedia; `pm_state` 10 = a page is
open). Read `docs/plans/pause-menu.md` (the Encyclopedia section) and
`docs/findings/probes/pause-status-items.md` (page mechanics, how the list / tab cursors were found) first.

Do: open the Encyclopedia; go through EVERY category / tab it has; in each scroll the list to its end and back
(`rd tap R D --n N --gap 6 --text` prints the rows as they come: use it to transcribe); open the detail of three
entries of different kinds (a known item, an unknown one if selectable, an adventurer / enemy / customer entry if
such categories exist); note the counters shown (n / total, percentages). Close page by page with `B`, leave the
pause menu.

Report, beyond the card's list: the category list, per category the entry count and the first / last entries, the
cursor / tab / scroll variables (name them), what each button does (L / R, U / D, A, B, C / X / Y if they do
anything), the detail page's fields; a transcript of at least one full category (`--text` output, trimmed);
strips of each kind of page.
