---
name: shop-story-elan
start: shop-story-nagi
steps:
  - control in the house or shop: scene == 1 and dlg == 0
  - a sweet on display row 0 where Elan's gate reads it, and the store open: cc08 == 4
  - Elan's first shop scene 6/5 (story probe): sid == 6005
  - Elan joins, scene 6/6: sid == 6006
  - the scene over, the shop running: cc08 == 4 and dlg == 0 and sid == 6006
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Elan's shop scenes 6/5 and 6/6** (`docs/findings/shop-story-scenes.md` — read it first). Start: the end of the
`shop-story-nagi` movie (`rd new R --from shop-story-nagi` (the whole filed movie); day 25 morning, Nagi joined; played lineage from `a23d9e447eb4`). The
bank has `u8(bank + 179412) == 1` (Elan's 4th guild visit done) and `+179413 == 0`, `+179414 == 0`. When the store opens,
the roster scan itself sets +179413 and forces Elan (customer 6) into the list **only if `FUN_0045ED12` passes**: it
looks at display row 0 only, tests occupancy at counter column `{1,2,3,4,11,12,13}[j]` but reads the item from column
j, and passes when that item is a sweet (ids 4001–4006, 4011, 4016, 4025–4034; an Apple is 4001 — the bag has some).
Row 0 is now `−1, −1, 1202, 3211, 3013, …` (bank dwords 20005+: the display grid starts at bank byte 80024, 20 per row):
work out from the rule which cell to put the sweet in (e.g. column 1 is both tested and read for j = 1) and place it with
the counter menu's display editing. Then open the store: 6/5 plays at the first customer (`sid == 6005`, sets +179414).
After that Elan is status 2; 6/6 needs his closeness (slot 4 = int16 `bank + 0x2d220`) > 2 — sell to him (at his
target `s32(0x730b588)`) when he visits (`s32(0x730b56c) == 6`) until ≥ 3, then the next first customer with 6 eligible
plays **6/6** (sets +179415 and the roster byte `0x2bc50`). Re-time Open Store when 6 is not in the eligible list
(`rd ram R dwords 0x6a5d450 20`). Caillou's 5/2 may replay at first customers where Elan does not win — pass it.

Record: which cell made the gate pass (and one that did not, if you tried), frames + transcripts of 6/5, 6/6, bytes
changed, closeness per deal. Save in game at the end, `rd endsave R`.
