---
name: order-return-sale
start: save:44e9baea3987
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - two scarves held (bag or display), Louie's order still active: u8(bank + 173560) == 1
  - Louie back for his order (b5a8 4, FUN_00463cfb), at the pick rows: cc08 == 4 and s32(0x730b5a8) == 4 and s32(0x730b534) == 3
  - a pushback on the order's price (state 8): cc08 == 4 and s32(0x730b5a8) == 4 and s32(0x730b534) == 8
  - the order sold (state 7 = accept), the slot cleared: s32(0x730b5a8) == 4 and s32(0x730b534) == 7
  - the visit over, the shop running: cc08 == 4 and u8(bank + 173560) == 0 and s32(0x730b534) == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **an advance order delivered and haggled** — `FUN_00463cfb`, the customer machine's `b5a8 == 4` arm (port
`cs_order_pick_machine`, `src/customer_service.c`). The basic-block coverage (`tools/rhost/where.py FUN_00463cfb
--unhit`) shows 64 of its 206 blocks never run in any recording: the existing order probes either never came back for
the goods (`order-deadline-missed`: Louie came with nothing ready) or never reached the due day. Never run: the pick
row's detail card (button 0x40 on a row), "done" with a row still empty (→ state 17, the picks go back), the cursor
moving over rows (b5c8 scroll), the offer states 5 / 6 / 8 (pushback) / 7 (accept) / 16 (a pick that does not match the
order), the closeness / merchant-XP bookkeeping of a sale. Read `docs/findings/probes/order-deadline-missed.md`
(the order's memory: slot 0 at `bank + 173560`: +0 active, +4 item id 5 = scarves, +8 days, +14 customer 2 = Louie,
+16 qty 2) and `docs/findings/probes/order-customer-week3.md` (price entry, b588 / b574, merchant XP).

Start save `44e9baea3987` (played lineage + crafted level, day dword 15, Louie's order of **2 scarves** due in 3
days). Get two scarves: buy them at the Market or the Merchants' Guild (check the bag first: `rd text` on the pause
Items page; `items`). Then open the store on the due day (the days field `s32(bank + 173568)` reads 0 that morning;
Louie is queued kind 3 when the store opens on time) and serve customers until Louie comes back (`b5a8 == 4`).

At the pick rows (`b534 == 3`), in this order, each one a short separate action so the movie shows them:
1. move the cursor over the rows (up / down) and open a row's detail card (the 0x40 button — find which pad bit /
   key it is; `rd ram R mark` / `diff` on `0x73dddd4` if unsure), close it;
2. press "done" with a row still empty → state 17 (the picks go back), and back to the rows;
3. fill both rows with the scarves (if you have a third item, first try a non-scarf pick in a row and see whether the
   offer goes to state 16 — then swap it for a scarf), "done" → Recette's offer;
4. type a price well above `s32(0x730b574)` (the customer's max) → pushback (state 8);
5. then the exact target `s32(0x730b588)` → accept (state 7), the sale.

Record: frame of each state change of `s32(0x730b534)` during Louie's visit, `rd text` at each, gold before / after,
merchant XP (bank dword 45309) before / after, Louie's closeness word (int16 at `bank + 0x2d210`; customer 2 → index
0 — confirm which index moves), the slot after. If Louie says something about upgrading his gear (the adventurer
offer FUN_0048093f), quote it. Then serve one more customer and stop.

If Louie never comes on the due day, report the queue (`0x730ac98` count, records at `0x730aca0`, 24 bytes) per session.
