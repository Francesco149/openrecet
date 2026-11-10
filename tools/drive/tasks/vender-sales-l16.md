---
name: vender-sales-l16
start: vending-level16
steps:
  - the machine stands in the shop (the prefix placed it): s32(bank + 4*0xb1d4) == 0x51440 and scene == 1
  - the store is open (customer service): cc08 == 4
  - money came in while the store was open: cc08 == 4 and gold > 449000
  - the session is over, free roam again: cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **a shop session with a stocked vending machine** — do customers buy from the machine, at what price, and
what state moves when they do. The port has no vending code (probe `vending-level16` splits at f5500), so every
retail fact here is new: this trace is the verification target of the sales half.

**Start:** the scenario `vending-level16` itself (5,622 frames; your run continues its movie). At its end Recette
stands at (0.635, −1.775) facing the placed "Old Vender" (centre table; 1/6 stocked with a Beef Bowl; tooltip on
screen), 449,000 pix, merchant level 16, day dword 15. Read `docs/findings/probes/vending-level16.md` first (items
5–6: placing, the machine's menu `cc04 == 2`, "Done Stocking", slots 1..6) and
`docs/findings/probes/shop-session-week3.md` (how a session runs: counter → "Open Store" → `cc08 == 4`, the `cs …`
state rows, what ends a session).

Do: (1) `A` facing the machine → fill the remaining slots (cheap food / items; say what the picker offers and what
it refuses, the per-slot quantity if there is one), "Done Stocking"; note `rd ram mark/diff` for one stocking
action — where does the stock live (the report of the first probe saw bank dwords 0x26..0x29 and a block around
0x4e4b change on placement). (2) Walk to the counter, Open Store. Serve counter customers as quickly as possible
(accept at ~105–110%; this probe is not about haggling) and WATCH the machine: `gold`, the tooltip count
"(n/6)", the stock dwords you found, per tick (`--watch`), and customers' positions if you can name them — does
anyone walk to the machine, does gold rise without a deal at the counter, is there a sound / text. (3) When the
session ends, face the machine again: the count, what is left. (4) In-game Save, `rd endsave` (report the sha).

If nobody uses the machine in one session, run one more time slot (the afternoon) before concluding; then say so
plainly — "no vending sale in N customers / M ticks" is a result.

Report, beyond the card's list: the stock layout (addresses, encoding), every vending sale (frame, item, price vs
base price, gold before / after, what the customer did), the tooltip texts, the machine menu's rows after the
session; strips of a vending sale (or of the machine during the session). Name the state you used.
