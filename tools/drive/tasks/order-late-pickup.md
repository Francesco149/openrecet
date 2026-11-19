---
name: order-late-pickup
start: save:44e9baea3987
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - Louie's order still active and past due (days < 0), the shop not opened since: u8(bank + 173560) == 1 and s32(bank + 173568) < 0
  - the shop open with the late pickup queued / serving (b5a8 5, FUN_00464a26): cc08 == 4 and s32(0x730b5a8) == 5
  - the late customer's line 18 on screen (b534 7): cc08 == 4 and s32(0x730b5a8) == 5 and s32(0x730b534) == 7
  - the late customer's line said and the visit over (b534 back to 0 after state 7): cc08 == 4 and s32(0x730b5a8) == 5 and s32(0x730b534) == 0 and frame > 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the late order pickup** — `FUN_00464a26`, the customer machine's `b5a8 == 5` arm: an order whose days
went below 0 while the shop stayed closed is queued as kind 4 when the shop next opens (the roster build,
all_ida 63057; kind 3 = on time → b5a8 4, kind 4 = late → b5a8 5), and that customer walks in, says line 18
(state 2 → 7) and leaves (state 11). The retail coverage sweep (`docs/findings/retail-coverage.md`) found no
recording that runs it; the port has it ported, unverified. Read `docs/findings/probes/order-deadline-missed.md`
first (the order's memory: slot 0 at `bank + 173560`, +0 active, +8 days, +14 the customer id; the page row by day).

Start save `44e9baea3987` (day dword 15, Louie's order of 2 scarves due in 3 days). The order's days drop by one
each morning while its slot is active (`sub_45E67E`); the slot is cleared (and queued) only when the shop opens.
So: **do not open the shop** on days 15, 16, 17, 18 — pass the periods without the counter menu's "Open Store"
(the cheap way: the town round trip of that report, "Cheap way to pass a period", or the dungeon). Watch
`s32(bank + 173568)` each morning: 3 → 2 → 1 → 0 → -1. On the first morning with days == -1 (day 19), open the store
and serve customers until Louie comes in (`b56c` 2 is Louie). Record: the frame his visit starts, `rd text` of his
line, what Recette says, whether any item / gold / reputation word (bank + 184848) changes, and the state sequence
of `s32(0x730b534)`. Then let the session end or close the shop; stop there.

If the slot is cleared without a late visit, or the days stop at 0 (the decrement skipping it), stop and report the
dword values per morning — the premise is wrong then.
