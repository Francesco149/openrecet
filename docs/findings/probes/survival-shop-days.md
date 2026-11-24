# Probe: survival-shop-days

- **Run:** `svs-1` → scenario `survival-shop-days` · **start:** save:099af5b31445 (`survival-start`'s end: Normal Survival, day dword 1 morning) · **task:** `tools/drive/tasks/survival-shop-days.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/survival-shop-days/` (32,386 frames); end save `eb3734386f63` (slot 041, score 412, day 4 morning)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f32386, rng c2cf40d2). Port triage: **split f5950** (scalars:
  `DAT_0438b764`, the actor-say age, +1 in the port) → fixed → **identical**; gate row.

First recording of the shop in Survival (the roster's Survival gates, `FUN_0045edaa` mode 2). Three days, 12 sessions.

## The port bug

At f5950 (day 1 evening, the first free-roam frame after the session) the rank ≥ 6 escalation plays Tear's
advance-order lecture **19/1** (`sid 19001` — it plays in Survival too) and FUN_0048670f **returns** (IDA 97241..97245;
the rank arrivals' LABEL_428 likewise) — no LABEL_675 tail (FUN_00486435 + FUN_00485861) that frame. The port ran the
tail from its main tick anyway → the actor-say age advanced one tick more. Now the escalation flags the frame and the
tail is skipped.

## Facts

- Customers 4..8 never appeared (Survival gates `0x2bd28..0x2bd31`); seen: 2 Louie, 3 Charme, 9 Arma, 18 Euria (seller
  only, ~500 % asks), 20 Prime; 19 Alouette eligible, never queued.
- Queue kinds: 0 buy / sell, 1 request, 2 advance order (7 accepted, none came back by day 4).
- Daily Profits: "Day 2 / +10705 / 98(+98)", "Day 3 / +22843 / 299(+201)", "Day 4 / +8936 / 412(+113)" (the score).
- Wallets are low: `b534 == 40` = "can't afford"; a second failure → they leave. Budget `FUN_0045ecc0` = min + closeness/10
  × (max − min), check budget × 1.2 ≥ price; all cases fit closeness 0 (14: 1,500 → 1,800 ok / 1,801 not; Louie 1,000;
  Charme / Arma 2,000; 17: 500).
- In a buyer's price box B sets the price to the gold held. The pick list remembers category / row between customers;
  its first L / R ~15 ticks after opening is eaten.
- After the day card the state reads control ~200 ticks before Recette can move.
