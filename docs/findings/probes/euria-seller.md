# Probe: euria-seller

- **Run:** `eus-1` → scenario `euria-seller` · **start:** save:3228c6dabd3d (`shop-story-griff`'s end; played lineage) · **task:** `tools/drive/tasks/euria-seller.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/euria-seller/` (9,201 frames); end save `37335a12dbf7` (day dword 29 morning, slot 023)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f9201, rng dd72cb40); golden + triage: **identical**; gate row. No port change.

First recording of `FUN_00461792`'s customer-18 relic half (the scan path) and a band-3 weapon pick.

| visit | session | queue adv. / b534 1 / 2 | item | base | ask b574 | floor b580 | target b588 | paid | closeness (slot 21) |
|---|---|---|---|---|---|---|---|---|---|
| 1 | d28 noon | f1398 / 1489 / 1535 | 3011 Picnic Basket (weapon pick, band 2) | 10,600 | 20,670 | 12,663 | 35,432 | 35,432 | 3 → 8 |
| 2 | evening | f3869 / 3960 / 4006 | 5405 Checkered Carpet (relic, scan) | 20,000 | 39,000 | 24,040 | 74,374 | 74,374 | 8 → 13 |
| 3 | night | f5967 / 6058 / 6104 | 3015 Silver Photo Stand (band 3 by closeness 13) | 18,800 | 36,660 | 21,639 | 62,295 | 36,660 (= ask) | 13 → 14 |

- Euria's roster record (`0x6a63bdc + 18·0x2c670`): time mask 15 (+0x68, every period), attr (−6, 8); centroid (1, −1)
  → band 2. Pass 1 of the queue fill skips kyaku 18; she is queued in pass 2, always kind 1 = a seller.
- The weapon / relic pick is fixed by the rng at the queue advance: night openings after n idle ticks → n = 1 relic 5111,
  4 weapon 3015, 7 relic 5105, 12 relic 5011; evening re-timing 0 / 2 / 4 / 6 → 3009 / 5405 / 5405 / 5110.
- Her target ≈ 1.7–1.9 × her ask; paying the target = +5 closeness, the ask = +1 ("Can't believe I actually managed to
  sell that." vs "This is why I just can't quit...").
- Seller price box: the offer starts on the highest digit, R moves lower, a D to 0 clamps to 1.
- The day-28 night session ends in the week-4 payment (200,000) and the day-29 morning; ESC dead ~300 ticks after.
- Tool gaps: `rd tap` has no `--watch`; a timing search over `(dlg, frame//2)` cells found nothing (a `ck load` + play
  loop did).
