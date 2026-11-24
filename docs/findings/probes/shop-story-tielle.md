# Probe: shop-story-tielle

- **Run:** `sst-2` → scenario `shop-story-tielle` · **start:** `amber-26f-30f-tielle@34699` (played lineage; 7 ticks before 4/4) · **task:** `tools/drive/tasks/shop-story-tielle.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-tielle/` (53,422 frames incl. amber's first 34,699); end save `d7c10c7e06df`
  (day dword 24 evening, Tielle joined, slot 021)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f53422, rng 1b62295c); golden + triage: **identical**; gate row. No port change.

Tielle's arc after Amber Garden 30F to her joining (`docs/findings/shop-story-scenes.md`). The task first named
`fc2e4d81f26d` as the start: that save already has 4/4 played (amber's movie plays it at f34706).

| frame | when | sid | bytes changed | excerpt |
|---|---|---|---|---|
| 34706–38712 | day 23, stime 2, arriving home | 4004 | `0x2bcbc` 1 → 0, `0x2bcbd` 0 → 1 | "… Tielle." / "That's my name... I'm Tielle." |
| 38920–39539 | day 23 evening, first customer | 8005 (Nagi, forced) | `0x2bcc4` → 1 | |
| 46095–46770 | day 24 morning, after the daily report | 4005 | `0x2bcbd` 1 → 0, 179390 0 → 1 | "So. You intend to search the city for your sister?" |
| 49408–50272 | day 24 noon, first customer | **4006 — Tielle joins** | 179391 0 → 1, `0x2bc4f` 0 → 1, `0x2d496` 0 → 1 (the script-viewed byte: 0x2d3a0 + 60·4 + 6) | "Welcome to... oh! Tielle!" / "I just finished the Hall of Trials!" |

- Closeness slot 3 (`bank + 0x2d21c`) 0 → 5 in one deal: a Longbow at exactly her target 3002 (214 % of base 1,400). The
  target is set per deal (2889 while she walked in, 3002 once the deal started).
- `sub_460672`: offer within ±0.5 % of the target → grade 1 (+5), ±5 % → 2 (+2), else +1.
- 4/5 needs stime 0 at home: a night session → stime 4 → Daily Profits → A → the morning plays 4/5.
- Tool gaps: no `hex()` in `rd eval`; `rd ram read` takes absolute addresses only; the haggle price is not in `rd text`
  (`s32(0x5c6bb8)`).
