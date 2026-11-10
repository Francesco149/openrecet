---
name: shop-session-week3
start: save:64d43650865f
steps:
  - in the shop with control: scene == 1 and cc08 == 1 and dlg == 0
  - the store is open (customer service): cc08 == 4
  - a first deal closed: cc08 == 4 and gold != 470000
  - the session is over, free roam again: cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **a whole shop session in week 3** — open the store and serve every customer of one time slot: sales with
haggling at different mark-ups (one at ~105%, one at ~130% that needs a counter-offer, one pushed until the
customer walks out), a customer who SELLS to Recette (buy low), and whatever else walks in (orders, requests,
story customers). The recordings of customer service stop in week 2 and are mostly first-offer accepts.

**Start save:** the week-3 save a driver made at the end of `payday-week2` (sha 64d43650865f: day dword 15, 470,000
pix — lineage: the played day-14 save with crafted gold; see `docs/findings/probes/payday-week2-paid.md` for how the
morning before it went). Load: title → Continue → slot (`.*60 A .*30 A`), then check `dlg`.

Read first: `docs/findings/customer-service-haggle-RE.md`, `docs/findings/shop-counter-menu-RE.md` (counter →
"Open Store" → `cc08 == 4`), `src/state_va.c` (the `cs …` rows: `b534` sub-state, `b5a8` transaction kind — 0
customer sells, 1 request, 3 order filed, 4 / 5 order served —, `b584` haggle round, `b574` customer price, `bb8`
player price, `b56c` customer id, `b528` customers advanced). `rd text` shows the dialogue and the price box.

Report, beyond the card's list: per customer the frames, `b56c` (who), `b5a8` (kind), the item, base price, each
offer and the outcome; how the price is entered (which buttons change which digit); `gold` and merchant experience
after; what ended the session; the named state you used. A strip of one haggle. Then do an in-game Save and
`rd endsave` (report the sha).
