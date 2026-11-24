# Probe: shop-story-tielle-market

- **Run:** `stm-1` → scenario `shop-story-tielle-market` · **start:** save:3228c6dabd3d (`shop-story-griff`'s end; played lineage) · **task:** `tools/drive/tasks/shop-story-tielle-market.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-tielle-market/` (15,998 frames); end save `f795f8712b3b` (day dword 29 evening, slot 023)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f15998, rng 0764c5d9); golden + triage: **identical**; gate row. No port change.

Tielle's market arc to the story probe's 17/42 (`docs/findings/shop-story-scenes.md`).

| frames | when | sid | flags | excerpt |
|---|---|---|---|---|
| 569–1435 | day 28 morning, Market | 17027 | +179593 (0x141), `0x2bca9` | "I... just ate Mt. Sweet's Demon Parfait in under 30 minutes..." |
| 6316–7056 | day 29 morning, Market | 17041 | +179594 (0x142), `0x2bca9` | "Did you ever find your big sister?" |
| 7551–9507 | day 29 noon, shop, first customer | **17042** | +179595 | "I got a letter!" … "I'll do what she says and stay around here for a while longer!" |

- The map showed the Market waiting on day 28 (`wms` 1221011). `0x2bca9` cleared at the day change.
- Eligible list at the open: 14, 3, 5, 19, 9 (no re-timing).
- stime rises by 1 per return home with ≥ 1 visit before it, however many visits. Night map `wms` 1011100 (Market,
  Chapel closed). Day 28 → 29 brought the daily report + the weekly payment (~1,350 ticks of A); after the day card input
  stays locked up to ~600 ticks.
- A B straight after a market event's dialogue is ignored; ~30 ticks later B on Buy / Sell / Leave → scene 8.
- Tool gaps: the shop door from the home-arrival spot (2, −3.24) needs `DL*8` (not R + A); `wm_cur` neighbours (from Home
  L → 1, U → 2, R → 3, D → 5; Chapel U → 0; Market D → 0; Merchants' Guild L → 2) undocumented; searches during the
  post-day-card lock fail with "one cell" and no state says why.
