# Probe: survival-week1

- **Run:** `sw1-a` → scenario `survival-week1` · **start:** save:eb3734386f63 (`survival-shop-days`' end; Normal Survival day dword 4) · **task:** `tools/drive/tasks/survival-week1.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/survival-week1/` (33,012 frames); end save `e3c22cc63bb0` (slot 042, day dword 8 morning)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f33012, rng c1e44a21); golden + triage: **identical**; gate row. No port change.

Normal Survival through its first payment (20,000, paid; no game over), with 10 advance-order pickups (b5a8 4) incl. one
unfilled.

| day dword | gold at end | profits | score (`score` = bank dword 0xb0f7) |
|---|---|---|---|
| 4 | 42,083 | −1,401 | 457 (+45) |
| 5 | 50,771 | +8,688 | 392 (−65) |
| 6 (payment) | 42,390 after paying | +11,619 | 442 (+50) |
| 7 | 42,670 | +280 | 564 (+122) |

- Order pickups: the unfilled one ("All set!" with rows empty) 3 → 17 → 18 → 11 → 13 → 0, nothing paid; filled ones
  3 → 4 → 5 → 6 → 15 → 7 → 10 → 12 → 0; one 15 → **40** (over 1.2 × qty × budget: "My allowance isn't that big") → 6 → …
  → 7. No pushback: any price in [tgt, cmax] accepted at once.
- The week end (night of day 6): Daily Profits, then the payment scene (sid 4; a "Collection Form" for 20,000, gold counts
  down over 16 ticks), the "Survival Mode / Day 8" card. The calendar then shows 80,000 due on day 14.
- The score can fall on a profitable day. A night session can end with a customer still queued. An order with days 0
  comes back at the next Open Store (which clears the slot; a new order may reuse it at once). Merchant level 7 → 8.
- Market: Treasures, Scarves, Food; Merchants' Guild: 15 equipment categories (Hats ≠ Helms).
