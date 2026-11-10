---
name: market-buy-display
start: save:64d43650865f
steps:
  - in the Market: scene == 6 and s32(0x963c5f0) == 1
  - something was bought: scene == 6 and gold < 470000
  - home, the display menu over a table cell: scene == 1 and s32(0x438cc04) == 1
  - an item was put on display and control is back: scene == 1 and cc08 == 1 and s32(0x438cc04) == 0 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **buy at the Market, then stock the shop** — the Market is the guild scene's variant 1 (`DAT_0963c5f0 == 1`;
its textures and stock are unverified in the port), and the display menu (put an item on a table cell, take one
off, swap) has only been recorded with tutorial stock.

**Start save:** the week-3 save of `payday-week2` (sha 64d43650865f; day dword 15, 470,000 pix).

Read first: `docs/findings/merchant-guild-RE.md` (the Buy flow: mode 1 main list → mode 0 item list → mode 8
quantity → confirm; `gmode` / `gcur` are in `rd ram R list`), `docs/findings/shop-display-menu-RE.md`,
`docs/findings/town-map-RE.md`. An earlier probe found: the display menu opens with `A` facing a table cell
(`cc04` = `s32(0x438cc04)` → 1; the highlighted cell's col / row are `s32(0x438cbfc)` / `s32(0x438cc00)`), its
first entry "Nothing" takes the item off.

At the Market buy three different items (one with quantity > 1). At home: put two of them on empty cells of
different tables, take one existing display item off, and replace an item on a cell with another (the third buy).

Report, beyond the card's list: the Market's option list and stock (names, prices from `rd text`), the buy price
rule you observe (base × ?), per display action the frames and the bank cells that changed (the display grid is at
bank dword 0x4e26, 20 columns), how the display menu's list is organised (tabs, sort). Strips of the Market list
and of the display menu.
