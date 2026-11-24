---
name: shop-story-caillou
start: save:3228c6dabd3d
steps:
  - loaded with control: scene == 1 and dlg == 0
  - Caillou's three reagents held (bag or display): u8(bank + 179403) == 0
  - Caillou's quest scene 5/4 (story probe; the reagents taken, the ask = their price sum): sid == 5004 and u8(bank + 179403) == 1
  - Caillou joins, scene 5/5: sid == 5005
  - the scene over, the shop running: cc08 == 4 and dlg == 0 and sid == 5005
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Caillou's reagent quest and his joining** (`docs/findings/shop-story-scenes.md` — read it first; the arms 5/2..5/5).
Start: `3228c6dabd3d` = the end of `shop-story-griff` (played lineage; day dword 28 morning, slot 022: `.*60 A .*30 A`
loads it). Bank: `+179401` = 1 (5/1 played), `+179402` = 17 (the day it played), `+179403` = 0. `FUN_0045E6E0` returns
1 (→ the reminder 5/2, seen in `shop-story-nagi`) until items **3101 Charred Lizard, 3110 Bat Wing, 3106 Slime Liver**
are all in the bag or on the display grid; the bag has 3110 only. With all three and d = day − 17 ≥ 4 it returns 4 →
**5/4** at a session's first customer with Caillou (customer 4) eligible: the quest takes the three items and the ask
`s32(0x5c6bb8)` = their price sum ×1 (`cs_story_item_quest`), sets +179403, b534 → 30. Then Caillou is status 2; **5/5**
(he joins: +179404, roster byte `0x2bc4e`) needs his closeness (slot 2 = int16 `bank + 0x2d218`) > 2 — a deal at his
target `s32(0x730b588)` gives +5.

Get 3101 and 3106 the way a player would: check the Market / Merchants' Guild stock first; else a short dungeon trip
(Slime Liver drops from slimes, Jade Way; Charred Lizard — find which floor's enemies drop it: grep
`docs/findings/item-table.md` / `findings/` for 3101 / "Charred"). Only if neither works in reasonable effort, stop and
say so (I'll craft them in). Then open the store until 5/4 plays, finish the quest's sale (what does b534 30 do? record
the states and the gold), and serve Caillou until his closeness ≥ 3 → 5/5.

Record: where the reagents came from, frames + transcripts of 5/4 and 5/5 (and any other scene), the b534 states of the
quest sale, gold before / after, bytes changed, Caillou's closeness per deal. Save in game at the end (a new slot if it
offers one), `rd endsave R`.
