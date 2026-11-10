---
name: order-deadline-missed
start: save:44e9baea3987
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - Tear's Outstanding Orders page shows the order: cc08 == 17
  - four days on: day >= 19
  - control at the end: day >= 19 and scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **an advance order that is never filled** — the save has one outstanding order ("2 scarves in 3 days",
accepted from customer 2 during `vender-sales-l16`). Look at it on Tear's page, then let the deadline pass and
see what the game does: does the customer come back on the due day (a shop session is needed for that — open the
store on the due day and serve whoever comes, turning the orderer away with "Nothing" if asked), what is said,
what happens to the row, to the customer's reputation, to `gold`.

Read first: `docs/findings/probes/order-customer-week3.md` (an order from request to served: the state rows
`b5a8` kind 3 / 4 / 5, where the order list lives, how Tear's page shows a row) and `vender-sales-l16.md` (the
session in which this order was taken; the save is its end: day dword 15, afternoon, a vender on the centre
table). Tear's menu: talk to her (`reach == 20`, `A`), entry 9 = Outstanding Orders (`cc08` 16 → 17).

Do: (1) Tear → Outstanding Orders: transcribe the row (`rd text`), `B` out. (2) Pass time the cheap way (shop
door → town map → Market → Leave → home per period; night; morning) to the due day; each morning re-read the
page (does the deadline count down?). (3) On the due day open the store for one session and serve; note the
orderer's visit (customer id `b56c`, kind `b5a8`), every line they say, the outcome. (4) Pass one more day,
re-read the page, and open one more session if the orderer did not come. (5) In-game Save, `rd endsave`.

Report, beyond the card's list: the row's text per day, the order's memory (addresses, encoding, how the deadline
is stored), what a missed order does (text, state changes), frames; strips of Tear's page and of the orderer's
visit.
