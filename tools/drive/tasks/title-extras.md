---
name: title-extras
start: save:c062daff8848
steps:
  - at the title: scene == 0 and frame > 60
  - a title sub-page is open: scene == 0 and t_sub != 0 and frame > 60
  - back on the title's main menu at the end: scene == 0 and t_fade == 0 and frame > 2000
forbid:
  - left the title: scene == 1
---
Probe: **the title's pages after the ending** — ITEM ENCYCLOPEDIA (`t_cur` 4 / `t_item` 7), SURVIVAL SCORE
(`t_cur` 5 / `t_item` 8, `t_sub` 4), OPTIONS (`t_cur` 6 / `t_item` 2), and LOAD GAME's picker (`t_cur` 3) with a
slot list that holds story, Endless and cleared saves. Never start a game: every page is opened, read, closed.

Read `docs/findings/probes/newgame-plus.md` (the title's entry table and state names) and
`pause-encyclopedia.md` (the in-game Encyclopedia: categories, cursor variables `enc_*`, buttons) first; the
title's Encyclopedia may be the same page with another data source — say whether its counts differ from the
in-game page's 65 / 538 and whether the `enc_*` variables drive it.

Do: for each page: open (`A`), `rd text` + one picture, walk it (categories / rows / sliders; in OPTIONS move
one slider and put it back), close (`B`); name the state that says which page is open (`t_sub` values) and its
cursors (`rd ram mark/diff` around one move). In LOAD GAME: scroll the whole slot list with `rd tap R D --n 45
--gap 6 --text` (slot texts: day, pix, level, score, loop, mode tag), do not load.

Report, beyond the card's list: `t_sub` per page, cursor variables, each page's text and buttons, the slot list
as read; strips of each page.

**After the first run (2026-11-10):** `t_sub` is NOT cleared when a page closes (only the title's init
writes 0): the main menu has input again when `t_fade` (0x9643520) is back at 0 — step 3 now says so.
