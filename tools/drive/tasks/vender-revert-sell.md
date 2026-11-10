---
name: vender-revert-sell
start: save:44e9baea3987
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - the machine's menu is open: cc04 == 2
  - the machine is gone from the shop: s32(bank + 4*0xb1d4) != 0x51440 and scene == 1 and frame > 300
  - at the Market or a guild: scene == 6
  - back in the shop at the end: scene == 1 and cc08 == 1 and dlg == 0 and frame > 1500
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **getting rid of a vending machine** — "Revert To Table" on a stocked machine, then selling the machine
back ("You cannot sell machines off yourself; to get rid of one, sell it back to the Merchant's Guild or market at
a steep loss" — Tear's tutorial). Completes the vending subsystem's retail facts (placement: `vending-level16`;
stocking + sales: `vender-sales-l16` — read both reports in `docs/findings/probes/` first).

**Start save:** the end save of `vender-sales-l16` (day dword 15, the afternoon after one session; the "Old
Vender" stands on the centre table with 4/6 stocked: slots 1 and 3 Beef Bowl, 5 Chocolate Bar, 6 Iron Spear;
~454,000 pix; an order for 2 scarves is outstanding). Load: `.*60 A .*30 A`, wait `scene == 1 and dlg == 0`. The
machine is at the centre table; Recette faced it from (0.635, −1.775). Stock: bank dwords 0xb1e8..0xb1ed (item
code or −1), the machine's code in dword 0xb1d4 (0x51440), inventory count dword 0xaec6 (`items`).

Do: (1) `rd ram mark`, face the machine, `A` → "Revert To Table" → whatever it asks; `rd ram diff` (bank only:
`--lo 0x44e3798 --hi 0x4511760`): where do the 4 stocked items go, what do dwords 0xb1d4 and 0xb1e8.. become,
what stands on the table afterwards (its 4 display cells: empty?), is the machine back in the bag (`items`,
which category). (2) Place it again and revert again once WITHOUT stock — same questions, briefly. (3) Go to the
Merchants' Guild → Sell: is the machine listed, at what price (base 30,000; bought for 21,000)? If not there, the
Market → Sell. Sell it; note `gold` before / after and the text. (4) Back home; the display menu's "Venders"
category now (empty? gone?). In-game Save, `rd endsave` (report the sha).

Report, beyond the card's list: the bank diff of a revert (with and without stock), every prompt's text, where the
machine can be sold and for how much, the state after; strips of the revert and of the sell list row.
