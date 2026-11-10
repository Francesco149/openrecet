---
name: order-customer-week3
start: save:64d43650865f
steps:
  - the store is open: cc08 == 4
  - an advance order was taken (an order slot is active): s32(bank + 173560) == 1 or s32(bank + 173580) == 1 or s32(bank + 173600) == 1
  - free roam after the session: cc08 == 1 and dlg == 0
  - Tear's Outstanding Orders page is open with the order in it: cc08 == 17 and tentry == 9
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **an advance order, from the customer to Tear's list** — the order arm of customer service (`b5a8 == 3`,
`FUN_004639f5` files it) and the Outstanding Orders page with a real row (the page probe found it empty).

**Start save:** the week-3 save of `payday-week2` (sha 64d43650865f; day dword 15, merchant level dword 5 — Tear's
menu then has NO entry 9: the page needs level ≥ 6). So this task has two halves and the second may be impossible
on this save: if `mlevel < 6` after the session, stop after step 3, report the order slot's contents instead
(10 slots of 20 bytes at bank+173560: active, item id, days left, orderee id (s16 at +14), quantity — see
`docs/findings/probes/tear-outstanding-orders.md`), and say so. (Selling raises merchant experience: if the level
reaches 6 during the session, do the page too.)

Open the store (counter → Open Store) and serve customers until one asks to ORDER something for a later day
(`b5a8` = `s32(0x730b5a8)` == 3; not every session has one — if a whole session passes without, open again while the
day lasts; `docs/findings/roster-scan-RE.md` and `customer-service-haggle-RE.md` describe who comes). Accept the
order. Deal with the other customers any way that keeps the session going (sell at ~105%).

Report, beyond the card's list: how many customers until the order, the order dialogue's lines (`rd text`), the
slot's fields after, `b5a8` / `b56c` per customer, and a strip of the order being placed.
