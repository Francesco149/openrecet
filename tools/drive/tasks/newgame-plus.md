---
name: newgame-plus
start: save:c062daff8848
steps:
  - at the title: scene == 0 and frame > 60
  - in the game with control: scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
---
Probe: **the title menu after the ending, and New Game +** — which entries the title offers once the debt is paid
(the ending probe reports "New Game +" and the Survival modes as unlocked), how New Game + picks its source save,
what it carries over (items, merchant level, adventurers' levels, gold?), and the first minutes of the new loop.
The port's `FUN_004864fd` (the shop tables' re-layout) has a New Game + arm at the title that was never run.

**Start save:** the Endless save the `ending-week5-paid` probe wrote in slot 001 (score 7308, day dword 36; its
lineage has crafted gold). Read `docs/findings/probes/ending-week5-paid.md` first (the title menu picture at
f54731, the save prompt's text about New Game + / Survival), and `docs/findings/title-settings-submenu.md` for the
title's state (`t_item` = the entry under the cursor, `t_sub`, `t_cur`, `t_start`).

Do: (1) at the title, walk the cursor over every entry (`U` / `D`; `rd look` once for the menu — it is sprites, no
text): how many entries, which `t_item` each has, which are new against a plain save (New Game 0, Continue 1, …).
(2) Open each NEW entry far enough to see what it asks (a picker? a difficulty list? text) and back out with `B`,
except New Game +: select it, pick the Endless save, confirm whatever it asks; say exactly what each prompt says.
(3) Follow the new loop to the first moment of control in the shop (`rd tap R A --n 400 --gap 6 --text --until
"dlg == 0 and cc08 == 1"`; ESC + Yes if it offers Event Skip — say which scenes played before control and whether
the prologue differs from a first playthrough). (4) With control: `gold`, `day`, `mlevel`, `items`, the pause
menu's Status page (who is in the party list, levels), the table layout (`d_table`), the debt shown. `rd ram
mark/diff` around the New Game + confirm if you can (bank range `--lo 0x44e3798 --hi 0x4511760`): what was kept,
what was reset. (5) In-game Save to a NEW slot, `rd endsave` (report the sha).

Report, beyond the card's list: the title's entry list with `t_item` values, every prompt's text, the carried-over
state vs the reset state, the frames of each scene; strips of the title menu and of the New Game + picker.
