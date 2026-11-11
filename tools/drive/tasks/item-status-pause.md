---
name: item-status-pause
start: save:64d43650865f
steps:
  - the faced stand's item status is open: cc08 == 50
  - the pause menu over it: scene == 9 and cc08 == 50
  - both closed on one tick: scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **the pause menu opened over the item-status window** — the one arm of the cc08 50 port (`25496473`) no trace
exercises: `Y` opens the pause menu (`scene == 9`) with `cc08` still 50; `B` then closes the pause menu and the window
on the same tick.

Read `docs/findings/probes/stand-item-status.md` (how the window opens: day-15 save, `.*60 A .*30 A`, a search to the
spot below the centre table at about (−0.20, 3.74) facing it, `C` → `cc08 == 50`). Do: open the window, `Y`, move the
pause cursor once down and up, `B`; then open the window again, `Y`, enter the Items page and leave it with `B`, then
`B` again; then a few steps of walking. Report the frames and `scene` / `cc08` per tick around each close.
