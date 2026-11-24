# Probe: order-return-sale (+ order-return-incomplete, order-return-wrong-item)

- **Runs:** `ors-1`, `ors-inc` (from `ors-1@7474`), `ors-wrong` (from `ors-1@7556`) · **start:** save:44e9baea3987 (played + crafted level, day 15, Louie's order of 2 scarves due in 3 days) · **task:** `tools/drive/tasks/order-return-sale.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenarios:** `order-return-sale` (8,900 frames), `order-return-incomplete` (7,853), `order-return-wrong-item` (7,912)
- **Verdicts (2026-11-24):** check PASS (main run); verify --real MATCH (all three); goldens + triage: **identical** (sale:
  ticks 1..9200 save IDENTICAL; incomplete 1..8153; wrong item 1..8212); gate rows portrng ok. No port change.

First recordings of `FUN_00463cfb` (b5a8 4, the order brought back) with goods: the pick rows, the detail card, a
non-matching pick, the offer, pushback, accept, and both visit-ending branches (17 = done with a row empty, 16 = a pick
that does not match).

## The sale (ors-1)

Bought 2× Old Muffler at the Market (35 each), passed days 15 → 18 with guild trips (days field 3 → 0), opened the store
f6454 (the order slot cleared at the open; queue: Louie {2, 0, kind 3, item 5, qty 2} + customer 15).

| frame | b534 | |
|---|---|---|
| 7022 | 0 | b5a8 = 4 |
| 7131 | 2 | "Hey, I asked for scarves. You got it?" |
| 7185 | 3 | pick rows ("All set!", "1:", "2:"); cursor `0x5c6bcc` 1 → 2 → 1 → 0 → 1 |
| 7458 | 3 | detail card (button 3 = pad 0x40, the C key; only on a filled row) |
| 7537 / 7621 | 3 | Warrior's Helm into row 2, swapped for the 2nd muffler (the helm back to the bag) |
| 7663 | 4 | gear comparison, "How about this?" |
| 7756 | 6 | b574 119, b588 104, b580 254, base 100 |
| 7808 → 7843 | 15 → 8 | offer 200 → pushback ("Isn't that just a little much?"); b574 → 171, b584 → 2, combo → 0 |
| 7987 → 8022 | 15 → 7 | offer 104 → accept |
| 8089 / 8198 | 10 / 0 | paid (+104) / visit over |

Gold +104; merchant XP +30 at f8209 (11 ticks after b534 → 0, "Just Bonus"); closeness slot 0 7 → 12 (+5, perfect price),
the high word 0 → 1.

## The branches

- **incomplete** (row 2 empty, "All set!"): 17 (f7486, the pick back to the bag) → 18 → 11 → 13 → 0; no way back to the rows.
- **wrong item** (helm in row 2): 4 (the upgrade pick `0x5c6bc8` = the helm, which beats his gear; with the scarves it
  stays −1) → 16 (f7603, both picks back) → 11 → 13 → 0.

## Facts

- At state 15: above b574 and below b580 → 8; ≥ b580 → walk-out (9); above 1.2 × qty × budget (budget 1000 / item from
  `sub_45ECC0`) → 40. A second pushback with b584 == `sub_460F16()` (3 here, from the `+0x2d212` word) → he leaves.
- Price entry opens on the hundreds digit; each A gets an "Okay! / Start Again" confirm.
- A Market / guild trip counts a period only when you next leave home.
