---
name: tear-menu-pages
start: save:1a5a45e631b4
steps:
  - Tear's menu is open: cc08 == 15
  - the Merchant Level page is open: cc08 == 18
  - the Merchant Level page was scrolled: cc08 == 18 and (s32(0x438b8c4) != 0 or s32(0x438b8c8) != 0)
  - the Store Atmosphere page is open: cc08 == 17 and s32(0x74b28b0 + 4*s32(0x438cc0c)) == 0
  - Tear's Lectures is open: cc08 == 30
  - back in free roam: cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the rest of Tear's menu** — Store Atmosphere (entry 0), Merchant Level (entry 5, `cc08 == 18`, scrolls),
Tear's Lectures (entry 7, `cc08 == 30`), Time Lecture (entry 6). The port has cases 15 / 16 / 17 / 18 / 30 from one
recording that opened the lectures; the level page's scroll and the atmosphere page are unverified.

**Crafted start save** (merchant level dword 5 → 9, see `tasks/move-tables-l10.md`): the level page then has ten
levels of abilities to scroll.

Known from earlier runs (`tasks/tear-orders-l10.md` has the details): load `.*60 A .*30 A`; a long morning dialogue
follows (tap A, ~1,100 ticks, or `~` → A on Yes); Recette then stands at (−0.3, 9.35), Tear at (0.6, 9.35);
`U*4 R*2 A` puts Recette in reach (`s32(0x438be7c) == 20`) and opens the menu (`cc08 == 15`); list B is
`0x74b28b0[]` (count `s32(0x5cd0c0)`), cursor `s32(0x438cc0c)`; A on an entry → `cc08 == 16` for 15 ticks → the
page; `b8c0` = `s32(0x438b8c0)` counts to 10 before a page takes input.

Open each of the four pages; on Merchant Level scroll to the end and back (rows of 1 and of 5: `b8c4` / `b8c8`); in
Tear's Lectures open one lecture and leave it; find out what Time Lecture does (it may start a dialogue or a
tutorial — follow it to its end). Leave to free roam.

Report, beyond the card's list: per page the `cc08` value, the variables that move, what each button does, the text
shown (`rd text`), how it closes; strips of each page.
