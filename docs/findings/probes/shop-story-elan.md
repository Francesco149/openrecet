# Probe: shop-story-elan

- **Run:** `sse-1` → scenario `shop-story-elan` · **start:** `shop-story-nagi` (its whole movie; day 25 morning) · **task:** `tools/drive/tasks/shop-story-elan.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-elan/` (46,574 frames; this probe's part f23990..46574, day 25 morning → 27 noon); end save `e3bd1eacd4a9`
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f46574, rng 7b8c514c); golden + triage: **identical**; gate row. No port change.

Elan's shop scenes and the roster scan's own story write (`docs/findings/shop-story-scenes.md`, the 6/5 / 6/6 rows).

## The sweet gate (FUN_0045ED12), live

- Row 0 by then held only col 3 = 3211 (sales had emptied cols 2 and 4).
- **Fails:** an Apple in col 1 with col 2 empty — col 1 is read only for j = 1, which tests col 2 (empty). Store opened:
  +179413 stayed 0, 6 not listed (cut from the movie).
- **Passes:** a second Apple in col 2 (f26378 / f26450): j = 1 tests col 2 (occupied) and reads col 1's Apple; j = 2 tests
  col 3 and reads col 2's. The open at f26662 set +179413 and listed 6. (j = 0 reads col 0 = −1 → `sub_4681F6` −1 → a
  harmless read at `dword_95D3804[-179]`.)

## Scenes

| frames | sid | bytes changed | excerpt |
|---|---|---|---|
| 26712–27758 (d25 noon) | 6005 | +179414 0 → 1; `+0x2d50d` (viewed 6/5) | "Yo! You doing okay, Recette?" / Tear: "For the record: we do not accept payments on tab." |
| 44072–45034 (d27 morning) | **6006 — Elan joins** | +179415, `0x2bc50` 0 → 1; `+0x2d50e` | "Oh! A guild card!" / "I'll go with you anytime you want!" |

- Closeness slot 4 (`bank + 0x2d220`): d26 morning he placed an order (2 weapons, 2 days; still open at the end) — no
  change; d26 night a Sturdy Glove (base 1000) at exactly b588 = 1035 → 0 → 5. A Steel Sword at 3163 got state 0x28
  ("way over budget": budget 1000 × 1.2 cap; cut).
- Roster record (`0x6a63bdc + 6·0x2c670`): activity mask (record dword 26) 0xb = morning / noon / night, **never evening**; budget 1000–50000 (dwords 33 / 34).
- Tool gaps: in the kind-4 order-back picker L / R / X / Y never switched tabs (stayed on Swords; the kind-1 request
  picker's R works after ~60 ticks) — unexplained; `cust` keeps the last customer (`cust == 6 and b534 == 1` = a visit).
