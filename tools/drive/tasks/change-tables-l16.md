---
name: change-tables-l16
start: save:2afe7fb42617
steps:
  - in the shop with control: scene == 1 and cc08 == 1 and dlg == 0
  - at the Market: scene == 6
  - a counter was bought: gold < 470000 and scene == 6
  - back in the shop at the end: scene == 1 and cc08 == 1 and dlg == 0 and frame > 1500
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **"Change Tables" — the level-14 ability** ("you may change the kind of counters in the store if you have a
new one"). Nobody has a recording of it; the port's counter menu lists the entry (code 4) and nothing behind it
is verified.

**Crafted start save** (as `vending-level16`): the week-3 save with the merchant level dword raised to 15 (HUD 16),
470,000 pix. At load a level-up event plays (`~`, wait ~40 ticks, `A` on Yes; check `dlg == 0`), then ~30 ticks
later Tear's vending tutorial starts by itself (~2,800 ticks: `rd tap R A --n 400 --gap 6 --until "dlg == 0 and
cc08 == 1"`, or `~` + Yes if it offers Event Skip — try that first, it is much shorter; say which worked).

Known from `docs/findings/probes/vending-level16.md` (read it): the counter menu at level 16 has 7 entries, codes
0, 2, 1, 3, 5, 4, 6 = Open Store, Change Walls, Change Flooring, Change Carpet, Move Tables, **Change Tables**,
Cancel (`centry` = the code under the cursor, count `s32(0x5cd0bc)`); the Market's Buy list has a "Counters"
category (Wooden, Legendary, Barrel, Draped, one out of stock); the Market is reached shop door → town map →
Market (`scene == 6`). `docs/findings/probes/market-buy-display.md` = how buying works there;
`docs/findings/probes/decor-variants.md` = how the wall / floor / carpet editors behave (`cc08` 0x17 / 3);
`docs/findings/probes/counter-move-tables.md` = Move Tables.

Do: (1) counter menu → Change Tables BEFORE owning a new counter: what does it say / show? (2) Market → buy two
different counters (the cheapest two); note prices, `gold`, where they land (`items`, `rd ram mark/diff`).
(3) Back in the shop: Change Tables → apply one counter kind to a table, then the other kind to another table (or
to all — say what the editor lets you choose: per table? all at once? a preview?), confirm; try cancel (`B`) once
mid-way and say what it keeps. (4) Walk up to a changed table: is its display menu the same; do the displayed
items stay. (5) In-game Save, `rd endsave` (report the sha).

Report, beyond the card's list: the editor's states (`cc08`, `cc0c`, `cc04`, cursor variables — name them), every
text it shows, what each button does, the bank dwords that hold the table kind (mark / diff across one change),
prices; strips of the editor and of the shop before / after.
