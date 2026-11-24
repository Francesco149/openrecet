---
name: shop-story-alouette-2
start: save:28a91c8d8673
steps:
  - loaded with control: scene == 1 and dlg == 0
  - Alouette's scene 17/49 (closeness past 10): sid == 17049
  - Alouette's scene 18/9 (closeness past 14): sid == 18009
  - the scene over, the shop running: cc08 == 4 and dlg == 0 and sid == 18009
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Alouette's last two shop scenes** — rows 3 and 4 of the story probe's customer-19 table (`DAT_005c6c7c`:
{437, > 10, 17/49}, {438, > 14, 18/9}; `docs/findings/shop-story-scenes.md`). Start: `28a91c8d8673` = the end of
`shop-story-alouette` (played lineage; day dword 26 noon; her closeness slot 22 = `s16(bank + 0x2d268)` = 7; 17/3 and
17/40 seen). Read `docs/findings/probes/shop-story-alouette.md` (her periods: morning, noon / evening after re-timing,
never night; a deal at exactly her target `tgt` = +5).

Do: serve Alouette (`cust == 19 and b534 == 1` = a visit start; re-time Open Store until 19 is in the queue `qn` /
records `0x730aca0`) at her target until closeness > 10 → the next session whose first customer comes with 19 eligible
plays 17/49 (+179709); then > 14 → 18/9 (+179710). Record each deal (item, price, closeness after) and each scene
(frame, transcript excerpt, bytes changed). Save in game at the end (a new slot), `rd endsave R`.
