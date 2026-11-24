---
name: survival-week1
start: save:eb3734386f63
steps:
  - loaded with control: scene == 1 and dlg == 0
  - an advance order brought back in Survival (b5a8 4 or 5): cc08 == 4 and (b5a8 == 4 or b5a8 == 5) and b534 >= 2
  - the first Survival payment day passed (day dword ≥ 8, no game over): day >= 8 and scene == 1
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **Normal Survival's first week** — continue `survival-shop-days` (end save `eb3734386f63`: Survival, day dword 4
morning, score 412, 7 advance orders accepted and not yet due). Read `docs/findings/probes/survival-shop-days.md` and
`docs/findings/probes/survival-start.md` first (what Survival shows; customers' low wallets; the queue kinds).

Do: keep running the shop every period through day dword 8: serve the order customers when they come back (fill the
order if you can — buy the goods at the Market / Merchants' Guild between sessions; otherwise let a missed order play
out: record what happens), sell well (buyers' budget × 1.2 is the ceiling), and pass the first payment / week end
(find out from the screen what Survival asks on day 7 — a payment, a score target — and whether a game over can
happen; if a game over (scene 2) is the natural outcome, record it and stop there — that is a result too). Record per
day: gold, score (Daily Profits screen), the orders' fate (bank + 173560: 10 slots × 20 bytes: +0 active, +4 item,
+8 days, +14 customer, +16 qty), each order-back visit's b534 states, and any event (`sid`). Save in game at the end
(a new slot), `rd endsave R`.
