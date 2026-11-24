# Probe: shop-story-alouette-2

- **Run:** `sl2-1` → scenario `shop-story-alouette-2` · **start:** save:28a91c8d8673 (`shop-story-alouette`'s end; played lineage) · **task:** `tools/drive/tasks/shop-story-alouette-2.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-alouette-2/` (17,119 frames); end save `70692be982a8` (day dword 27, slot 022)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f17119, rng cf21212e); golden + triage: **identical**; gate row. No port change.

The customer-19 table's rows 3 and 4 — with `shop-story-alouette`, all four of Alouette's shop scenes recorded.

| deal | item | price = target | closeness |
|---|---|---|---|
| d26 noon, request "a bracelet" | Spiked Armband (base 1,200) | 560 (cmax 845) | 7 → 12 |
| d27 morning, request "some food" | Apple (base 300) | 329 (cmax 450) | 12 → 17 |

| scene | frames | when | bytes |
|---|---|---|---|
| 17049 | 1372–3706 | d26 period 2, first customer (re-timed) | +179709; viewed `0x2d7cd` |
| 18009 | 13899–14693 | d27 noon, first customer | +179710; viewed `0x2d7e1` |

- Queue kind → customer kind: `b5a8 = kind + 1` (kind 0 = request, 2 = order). A story scene counts as a customer
  (b528 1 after it). Caillou's 5/2 replays at many first customers and competes.
- Price box: U / D add / subtract 10^pcur (300, D on the ones → 299).
- At night the Chapel does not open.
- Tool gaps: `sid` keeps the last script (an avoid on `sid == X` is true from the start); `rd scan` right after `ck load`
  refuses frames of the cut movie; expressions need quoting under zsh.
