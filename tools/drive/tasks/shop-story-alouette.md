---
name: shop-story-alouette
start: shop-story-nagi
steps:
  - control: scene == 1 and dlg == 0
  - Alouette's closeness past 2: s16(bank + 0x2d268) > 2
  - Alouette's shop scene 17/3 (the story probe's table, flag 435): sid == 17003
  - the scene over, the shop running: cc08 == 4 and dlg == 0 and sid == 17003
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Alouette's shop scenes** — the story probe's customer-19 table (`DAT_005c6c7c`: {flag 435, closeness > 2, 17/3},
{436, > 6, 17/40}, {437, > 10, 17/49}, {438, > 14, 18/9}; one per session; `docs/findings/shop-story-scenes.md`). Start:
the end of `shop-story-nagi` (`rd new R --from shop-story-nagi` (the whole filed movie), day 25 morning, played lineage). Alouette = customer 19, closeness
slot 22 = int16 `bank + 0x2d268`, now 2; flags `bank + 179707..179710` all 0. She is status 2 (needs score ≥ 55).

Do: open the store each period, serve Alouette when she comes (`s32(0x730b56c) == 19`; eligible list `rd ram R dwords
0x6a5d450 20`, queue `0x730aca0` count `0x730ac98`; re-time Open Store when 19 is not listed), sell / buy at her target
`s32(0x730b588)` for the most closeness. Once closeness ≥ 3, the next session whose first customer comes with 19 eligible
plays **17/3** (sets +179707). Elan's 6/5 may not fire (needs a sweet on row 0 — don't place one) but Caillou's 5/2 may
replay at first customers — pass it. **Stretch:** keep raising her closeness; > 6 → 17/40 at a later session.

Record: closeness per deal (+ what the deal was), frames + transcripts of 17/3 (and 17/40), bytes changed. Save in game
at the end, `rd endsave R`.
