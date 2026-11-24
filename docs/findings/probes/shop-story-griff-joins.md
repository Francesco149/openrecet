# Probe: shop-story-griff-joins

- **Run:** `sgj-1` → scenario `shop-story-griff-joins` · **start:** save:e26d0d428437 (**crafted**: `f795f8712b3b` + bank byte `0x2bce7` = 1, Obsidian 50F not played) · **task:** `tools/drive/tasks/shop-story-griff-joins.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-griff-joins/` (11,612 frames); end save `d25a0f10ed07` (day dword 30 night, slot 024)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f11612, rng 5eda24ea); golden + triage: **identical**; gate row. No port change.

| frames | when | sid | bytes | excerpt |
|---|---|---|---|---|
| 303–1475 | d29 evening, first customer | **7009** | `0x2bcea` 0 → 1 (scene start) | "It took more of my strength than I would have liked to re-seal the Archdevil…" |
| 8601–10410 | d30 evening, first customer (Open Store re-timed) | **7010 — Griff joins** | `0x2bc52` 0 → 1 (scene start) | "So. Do you want the card or not?" / "Yay-hay! Thank you, Griff!" |

- Closeness slot 6 (`bank + 0x2d228`) 0 → 5 in one deal (Thankful Statue at exactly his target 2453, 53 % of base 4550).
- After 7/9 Griff is eligible evening / night only. 7/10 needs no visit: it played at a session's first customer with 8
  eligible though Griff was not in the queue (13, 9).
- The same opening re-timed queued Griff as kind 2 (an order, b5a8 3) or kind 0 (a buyer, b5a8 2).
- Price cursor `0x730b560` (3 = thousands, 0 = ones).
