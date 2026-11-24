# Probe: item-boom

- **Run:** `ib-2` → scenario `item-boom` · **start:** save:11cb7b194beb (**crafted**: `1a5a45e631b4` + 12 Apples + gold 100,000) · **task:** `tools/drive/tasks/item-boom.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/item-boom/` (28,971 frames); end save `c9284c4b9647` (slot 018)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f28971, rng 3c125863); golden + triage: **identical**; gate row. No port change.

First recording of `FUN_00436623`'s boom roll + the player-driven boom news (steps 4 / 6 of `news_daily_update`).

- 10 Apple sales to display buyers (303..329) over day 14 night .. day 15 evening (non-Apple requests declined, so no
  other pairs).
- News updates: f18399 (morning → noon return): 4 Apple pairs → p 10, missed, TTLs −1. f25786 (day 16 morning): 9 pairs
  (4 at TTL 2, 5 at 3) → p 100 → the boom; the second roll (p2 90) picked the ALT id **37**: slot 4, target 4001, trend
  0xfe, duration 4 (3 after that update's expiry pass). Headline "Sweet Heaven, the market around Apples has completely
  collapsed!" (a collapse); all 9 Apple pairs cleared. Same update: "The Walnut Bread Loaves boom has ended." (id 30),
  a new id 3 ("The price of weapons has decreased.").
- Next day an Apple buyer's tgt / cmax was 105 / 130 (303–329 / 342–402 before); the display label turned red.
- **The day-start update can be skipped:** it runs only when `b928 == 1 && b924 == 0` reaches the master tick's main
  block; on the day after a payment the "Day N" card arm (`bank + 179437`) bumps `b924` first → no update that morning.
  No update on load. Sellers (b5a8 0) add no pair. The mid-day break never fired in ~30 customers.
- `9f429be80a97` (9,086 pix) dies at the first day change (the week-2 payment) — `ib-1`, a dead branch.
