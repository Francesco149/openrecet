# Probe: shop-story-nagi

- **Run:** `ssn-1` → scenario `shop-story-nagi` · **start:** save:a23d9e447eb4 (played, `end:dungeon3-deep`, day 22 night) · **task:** `tools/drive/tasks/shop-story-nagi.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-nagi/` (23,990 frames)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f23990, rng 0fe8225c); golden (Windows) + triage: **identical**
  ticks 1..24290, save IDENTICAL; gate row portrng ok (140,137 draws). No port change.

First recording of the story probe's Nagi arms 8/5 and 8/6 (`FUN_00462403` b524 == 50; `docs/findings/shop-story-scenes.md`)
and of Caillou's reminder 5/2 (roster event state 1, items missing).

## Story scenes at a session's first customer

| frame | day / period | sid | bytes changed |
|---|---|---|---|
| 1367–1986 | 23 morning | 8005 | +179396 0 → 1 |
| 4301–5535 | 23 noon | 5002 (Caillou: "Have you completed my order yet?") | none |
| 8266 | 23 evening | 5002 again | none |
| 22884–23930 | 25 morning | **8006 — Nagi joins** (guild card) | +179397 0 → 1, +179281 (= `0x2bc51`, the roster byte) 0 → 1 |

Task steps: control f137, store open f1317, sid 8005 f1367, scene over f1986.

## Facts

- Nagi's closeness (slot 5, `bank + 0x2d224`) +1 per deal at 100 %: f6715 (bought Spiked Armband 1,200) → 1, f17310 (sold
  Unthankful Statue 140) → 2, f19451 (sold Candy 300) → 3 → 8/6 at the next first customer with 7 eligible.
- Roster record kyaku 7 (`0x6a63bdc + 7·0x2c670`): attr (4, −3), like mask 0x2233, activity mask 7 (morning / noon / evening).
  Forced (flag 1) while +179396 == 0; after 8/5 status 2 (score > 54) → drops out of some lists (re-timed Open Store
  on d25 morning).
- Eligible ≠ visits: the visit list is the queue `0x730aca0` (6 dwords / entry, count `0x730ac98`).
- While 7's arms do nothing, Caillou (kyaku 4, event state 1) replays 5/2 at every first customer with 4 eligible.
- She asks for "a bracelet" (only the Bracelets category satisfies her; a sword made her leave); she also sells.
- A session opened at night (stime 3) ends into the next day.
- Request customers with a multi-row "All set!" list trap a plain A-mash (stalled > 1,800 ticks twice).
- Eligible lists per period: in the run's report (driver transcript in the session log).

## Tool gaps (driver)

`rd file` printed "Next: rhost.py golden" (now points at `rd port R`); `rd tap --text` flooded by the news ticker (y = 52);
a search with cell `(cc08, frame)` spends cells after Open Store — `--avoid "(cc08 == 4 and e7 < 1)"` fixed it; no shared
names for the eligible list / queue / closeness.
