---
name: shop-story-tielle-market
start: save:3228c6dabd3d
steps:
  - loaded with control: scene == 1 and dlg == 0
  - the market event 17-27 seen (flag 0x141): u8(bank + 179593) == 1
  - the market event 17-41 seen (flag 0x142): u8(bank + 179594) == 1
  - Tielle's shop scene 17/42 (story probe): sid == 17042 and u8(bank + 179595) == 1
  - the scene over, the shop running: cc08 == 4 and dlg == 0 and sid == 17042
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Tielle's market arc → shop scene 17/42** (`docs/findings/shop-story-scenes.md` — the 17/42 row; read it first).
Start: `3228c6dabd3d` = the end of `shop-story-griff` (played lineage; day dword 28 morning, slot 022: `.*60 A .*30 A`).
Tielle joined (`0x2bc4f` = 1, +179391 = 1); the market event 17-14 played on day 25 (flag 0x140 = `bank + 179592` = 1).
From the code read: `data/event.txt` market events (morning / noon, days 5–35 and 36+, need Tielle joined + card 0x77)
— **17-27** sets flag 0x141 (`bank + 179593`), **17-41** needs 0x141 and sets 0x142 (`bank + 179594`). The market plays
at most one event per day (flag `bank + 0x2bca9`, cleared at day change — `shop-story-griff`'s report). Then at a
session's first customer with Tielle (customer 5) eligible (status 2: morning / noon / evening; score ≥ 55 — re-time Open
Store if 5 is not in `rd ram R dwords 0x6a5d450 20`) the story probe plays **17/42** (sets `bank + 179595`).

Do: visit the Market (town map) in the morning or at noon on successive days until 17-27 and then 17-41 play (record
`sid` of each visit, a transcript excerpt); between visits pass the rest of the day cheaply (an empty Chapel visit burns a
period). Then open the store until 17/42. If a market visit plays some other event, or nothing, read the flags
`0x2bd88..0x2bd8b` and `0x2bca9` and say what played. Save in game at the end (a new slot if offered), `rd endsave R`.
