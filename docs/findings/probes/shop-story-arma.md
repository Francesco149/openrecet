# Probe: shop-story-arma

- **Run:** `ssa-1` → scenario `shop-story-arma` · **start:** save:64d43650865f (17264a73 + crafted gold, day 15 morning) · **task:** `tools/drive/tasks/shop-story-arma.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-arma/` (5,579 frames); end save `9eebe297073c` (day 15 noon, +179439 = 1)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f5579, rng 89be999c); golden + triage: **identical** ticks
  1..5879, save IDENTICAL; gate row portrng ok (29,181 draws). No port change.

First recording of the story probe's arm 9/2 (Arma's first shop visit; `docs/findings/shop-story-scenes.md`).

| step | frame |
|---|---|
| loaded | 137 |
| store open | 253 |
| sid 9002 (50 ticks after the open) | 303 |
| scene over | 2910 |
| session end (stime 0 → 1) | 5105 |
| in-game save, slot 020 | ~5430 |

- Eligible list: `15, 11, 2, 15, 17, 15, 9, -2` — Louie listed but his 7/5 arm off (+179424 = 0), Charme absent; not a
  news session.
- Only +179439 changed (0 → 1, at the scene start). Arma buys / sells nothing ("I currently lack \"money\""); closeness
  slot 7 stays 0. The scene is ~2,600 ticks of A taps.
- Pause menu entry types (`0x74b2844`): `0, 1 Items, 6, 2, 3 Save, 4, …` → Save at `pm_sel == 4`. ESC is ignored during
  customer service and at the session's end (opened ~60 ticks later).
- Tool gaps: the counter menu ignores an A before its slide-in (`b74c`) ends (~15 ticks after it opens; DRIVER's walk
  recipe ends with `A .*7`); `rd text` doesn't show which of Yes / No has the cursor.
