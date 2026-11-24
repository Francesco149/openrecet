# Probes: caillou-quest-5-7, caillou-quest-5-3-g2, caillou-quest-5-3-g3

- **Runs:** `cq7-1`, `cq2-1`, `cq3-1` · **starts (crafted from `1a5a45e631b4` + the three reagents in the bag):** `ce8b62461cbf` (+179401 0), `1a25f4ed4e95` (`0xaf32=0x000d0101`: 5/1 on day 13), `69957b3f8229` (`0xaf32=0x000c0101`: day 12) · **tasks:** `tools/drive/tasks/caillou-quest-*.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Verdicts (2026-11-24):** check PASS ×3; verify --real MATCH ×3; golden + triage: **identical** ×3 (with the b534 30 arm
  ported for `shop-story-caillou`); gate rows.

The quest's other arms (`docs/findings/shop-story-scenes.md`; the played 5/4: `shop-story-caillou.md`). Caillou (4) was
in the first eligible list each time `[2, 11, 14, 20, 4, -2]`.

| run | scene | ask (base sum 3800) | gold | b534 30 from | dlg 0 | paid (b530 32) | b534 → 0 |
|---|---|---|---|---|---|---|---|
| 5-7 | 5007 (first meeting; the order taken and filled in one scene) | 11400 (×3) | 9086 → 20486 | f370 | f3580 | f3612 | f3844 |
| 5-3-g2 | 5003 | 7600 (×2) | 9086 → 16686 | f370 | f1474 | f1506 | f1747 |
| 5-3-g3 | 5003 | 5700 (×1.5) | 9086 → 14786 | f370 | f1474 | f1506 | f1747 |

- 5/7 leaves +179401 = 1, +179402 = 14 (the day), +179403 = 1. b534 is 30 from the tick `dlg` → 1, before any line;
  `cust` reads 0 during the scene. An A while line 8 types out completes it; the next A ends the visit.
- The two 5/3 grades differ only in the ask (same rng, same text).
