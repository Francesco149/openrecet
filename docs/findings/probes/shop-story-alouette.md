# Probe: shop-story-alouette

- **Run:** `ssl-1` → scenario `shop-story-alouette` · **start:** `shop-story-nagi` (its whole movie; day 25 morning, played lineage) · **task:** `tools/drive/tasks/shop-story-alouette.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-alouette/` (36,207 frames); end save `28a91c8d8673` (day dword 26 noon)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f36207, rng 647c6b59); golden + triage: **identical**; gate row. No port change.

The story probe's customer-19 table (`DAT_005c6c7c`; `docs/findings/shop-story-scenes.md`): rows 1 and 2.

| frame | when | sid | bytes changed | excerpt |
|---|---|---|---|---|
| 27489–28654 | day 25 evening, first customer (Open Store re-timed) | 17003 | +179707 0 → 1 at the start; `bank + 0x2d79f` 0 → 1 at the end | "Father and I are going on an excursion to eat foie gras…" |
| 33522–34785 | day 26 morning, first customer (re-timed) | 17040 | +179708 0 → 1; `bank + 0x2d7c4` 0 → 1 at the end | "Father and I are off to have some truffles." |

- Closeness slot 22 (`bank + 0x2d268`) 2 → 7 in one deal: she sold Taiyaki (base 1,100), target 817, offered 817 (grade 1,
  +5). An offer at 1,100 (grade 0) gave +1 (undone, not in the movie). Ends at 7 (17/49 needs > 10).
- The end-of-scene bytes are the script-viewed table `bank + 0x2d3a0 + 60·group + n` (`FUN_0046c320`; cf.
  `lapis-30f-story`'s `+0x2d5fe`): 0x2d3a0 + 60·17 = 0x2d79c, + 3 = 0x2d79f, + 40 = 0x2d7c4 ✓.
- Alouette's periods: morning, noon / evening after re-timing, never at night (a 90 s search found none).
- She came as queue kind 1 with `b5a8` 0 (she sold).
- The order-back customer (`b5a8` 4) trap: B out of the item pane, U to "All set!" (`0x5c6bcc` = 0), A → state 17.
- Tool gaps: a shared scratchpad clash between parallel drivers (fixed by per-driver subdirs in the brief); `rd scan --to`
  past the movie's end errors instead of clamping.
