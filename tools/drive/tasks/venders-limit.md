---
name: venders-limit
start: save:0703ebb339bf
steps:
  - in the shop with control: scene == 1 and dlg == 0 and cc08 == 1
  - two tables hold a vender: (u32(0x450fee8) != 0) + (u32(0x450feec) != 0) + (u32(0x450fef0) != 0) + (u32(0x450fef4) != 0) + (u32(0x450fef8) != 0) + (u32(0x450fefc) != 0) + (u32(0x450ff00) != 0) + (u32(0x450ff04) != 0) >= 2
  - three tables hold a vender: (u32(0x450fee8) != 0) + (u32(0x450feec) != 0) + (u32(0x450fef0) != 0) + (u32(0x450fef4) != 0) + (u32(0x450fef8) != 0) + (u32(0x450fefc) != 0) + (u32(0x450ff00) != 0) + (u32(0x450ff04) != 0) >= 3
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **venders 5202 / 5203 and the 3-machine limit** — the port's vending machines were verified with the Old
Vender 5201 only; the limit message ("We are at our level's vending machine limit.", `lvl < 21 && n >= 3`) was read
from asm only.

Read first: `docs/findings/vending-machines-RE.md` (table t's vender handle = bank dword 0xb1d4 + t =
`u32(0x450fee8 + 4*t)`: 0 = an ordinary table, else id << 6; placing from a table's display menu, `cc04 == 1`,
`L`/`R` to "Venders"; the machine's own menu `cc04 == 2`) and `docs/findings/probes/vending-level16.md`,
`vender-sales-l16.md` (how a driver placed and stocked one).

**The start save is CRAFTED** from probe save `44e9baea3987` (level 16, day 15, one Old Vender placed on table 0
with stock): venders 5202, 5203 and two more 5201 were added to the bag (`craft_pix_save.py --add-item 5202 5203
5201 5201`). Validate the handle addresses on the loaded save first (table 0 must read 0x51440).

Do: load; place vender 5202 on a second table and 5203 on a third (what each is called, its description, its
capacity — the stock rows of its menu — and how it differs from the Old Vender); stock one item in each; then try
to place a 4th machine on another table: record exactly what happens (message text, sounds, state), and that the
table and the bag are unchanged. If the shop has only 3 tables, first buy the shop expansion at the Merchant's Guild (454,630 pix
in hand; how: `docs/findings/probes/guild-sell-expansion.md`), report the new table count, and do the 4th attempt on
a free table there. Then open the store once (counter → Open Store) and let customers come until
it closes, to see sales from the new machines (which items sold, prices), and Save from the pause menu.

Report, beyond the card's list: names / capacities / handles of the three vender kinds, the limit message and the
state around it, sales per machine, frames, a strip showing the three machines.
