---
name: caillou-quest-5-3-g2
start: save:1a25f4ed4e95
steps:
  - loaded with control: scene == 1 and dlg == 0
  - Caillou's quest scene: sid == 5003 and u8(bank + 179403) == 1
  - the quest's sale paid (b534 30 → 0) and the shop running: cc08 == 4 and dlg == 0 and b534 != 30 and u8(bank + 179403) == 1
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **a variant of Caillou's reagent quest** (`docs/findings/shop-story-scenes.md` 5/x rows; read
`docs/findings/probes/shop-story-caillou.md` first — the played 5/4 run: the quest sale b534 30, b530 to 32, line 8,
the ask paid, A ends it). **Crafted start** `1a25f4ed4e95` = `1a5a45e631b4` (played day-14 save + merchant level 10; Caillou
forced in by level ≥ 6) `--add-item 3101 3110 3106` (the three reagents in the bag) and the bank dword: 0xaf32=0x000d0101 (+179400 forced, +179401 1, +179402 13 = day − 1 → grade 2, ×2).
`FUN_0045E6E0` with all three items held and d = day − `bank + 179402` picks the arm: d < 2 → 2 (5/3, ask ×2),
d 2..3 → 3 (5/3, ×1.5), first beat (+179401 0) → 5/7 (×3).

Do: `.*60 A .*30 A` loads the save (the crafted level plays a level-up event first: `~`, wait ~40 ticks, `A` on Yes —
DRIVER.md); walk to the counter, Open Store (wait `.*15` after the menu opens). If 4 is not in the eligible list
(`rd ram R dwords 0x6a5d450 20`) re-time Open Store. Let the scene play, the sale finish (A on the line), then ~200 ticks.
Record: sid, the ask `ask` (expected: the three prices × the grade), gold before / after, frames of b534 30 → 0, and a
transcript excerpt.
