# Probe: save-in-guilds

- **Run:** `sig-2` → scenario `save-in-guilds` · **start:** save:64d43650865f (17264a73 + crafted gold, day 15 morning) · **task:** `tools/drive/tasks/save-in-guilds.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/save-in-guilds/` (2,433 frames)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (rng ec35e2a1). Port: the pause Save's card types for modes 6 /
  11 ported first (was `PORT-DEBT(save-card-type-modes)`); triage then **split f2393** (scalars: `DAT_0734b98c`, the item
  window slide) → fixed → **identical**; gate row.

Saves taken inside the Market, the Merchants' Guild and the Adventurers' Guild, each reloaded through the title's Continue.

| place | frame | paused mode | resume dword 0xb381 | preview 0xb75a.. |
|---|---|---|---|---|
| Market (variant 1) | 510 | 6 | **4** | −1 |
| Merchants' Guild (variant 0) | 1148 | 6 | **3** | −1 |
| Adventurers' Guild (Louie, 2 items packed) | 2067 | 11 | **1** | `0000fac0 0003eb00` (id << 6), then −1 |

Reloads: the Market save resumes in scene 6 / variant 1 (Buy / Sell / Leave, the greeting typed again, HUD Day 16
morning); the guild save in scene 6 / variant 0 (Buy, Sell, Fusion, Talk, Leave); the Adventurers' Guild save on the
town map (cursor on Home). After any load the resume dword reads 1; the packed items are back in the inventory.

## Port fixes

- `FUN_0047f5bc`'s card type: mode 6 → 3 / 4 by `DAT_0963c5f0`, mode 11 → 1 with the expedition bag (`DAT_0438b5ec`, 30 + 8
  dwords) copied into the save's preview dwords 0xb75a / 0xb78e.
- `FUN_0049a59e` clears `DAT_0734b9a0` (`FUN_004682d0`, the display-menu slide's "active" flag) on every start / Continue;
  the port cleared a private copy, so the real flag stayed set after the Adventurers' Guild save (item window open) and the
  slide `DAT_0734b98c` held 5 instead of decaying 5 → 0 on the town map.

Facts: the 17264a73 save at evening has the Market / Adventurers' Guild closed and dies at the payment if the day ends (use
`64d43650865f`); after a save `0x74b289c` counts 1 → 60 and B is ignored meanwhile; the Merchants' Guild is open in the
evening.
