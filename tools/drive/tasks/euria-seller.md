---
name: euria-seller
start: save:3228c6dabd3d
steps:
  - loaded with control: scene == 1 and dlg == 0
  - Euria in the shop as a seller, offering a weapon (id 3000..3099): cust == 18 and b5a8 == 0 and b534 >= 2 and s32(0x730b5a4) >= 3000 * 64 and s32(0x730b5a4) < 3100 * 64
  - Euria as a seller offering a relic (id ≥ 5000): cust == 18 and b5a8 == 0 and b534 >= 2 and s32(0x730b5a4) >= 5000 * 64
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Euria (customer 18) selling to the shop** — `FUN_00461792`'s customer-18 half (port `cs_seller_select` /
`src/customer_seller.c`). The coverage index (`tools/rhost/where.py FUN_00461792 --unhit`) shows the relic half never
runs in any recording (blocks 0x4619e0..0x461b73) and the weapon pick's bands 3 / 4 neither. Per the port (transcribed
from the disasm): for customer 18 the seller's item is, on `rand & 1 == 0`, a weapon 3000..3099 whose dungeon level band
matches her closeness band (2 below closeness 10, 3 from 10, 4 from 20; closeness = int16 `bank + 0x2d264`, slot 21,
now 3); else a relic: if the wall decor (bank dword 0xb379) is 10 AND the floor (0xb37a) is 12 → rand % 6 picks 5404 /
5013 / 5111 / a scan; else if the bag holds 5111 or the wall is 10 → rand & 3 picks 5013 / 5111 / a scan; else
rand & 1 → 5111 or a scan of relics 5001..5050 / 5101..5150 / 5301..5350 / 5401..5450 (price > 1000, minus the three
fixed ones) whose market level matches the merchant-level band. The offered item handle is `s32(0x730b5a4)` = id << 6.

Start: `3228c6dabd3d` (played lineage, end of `shop-story-griff`; day dword 28 morning, slot 022: `.*60 A .*30 A`).
Euria is eligible (roster gate byte `bank + 0x2bceb` = 1; status 2: score ≥ 55, her time-of-day mask — find it from
the eligible lists). Do: open the store each period; when 18 is in the visit queue (`qn`, records at `0x730aca0`, 6
dwords: customer first), serve until she is in service; if she comes as a buyer (b5a8 2) or a request (1), just serve.
The seller's pick is made at the queue advance (rng): checkpoint before her turn and re-time (a few idle ticks) to get
both a weapon and a relic offer **in the one movie** — buy each (offer near her ask; record price states), or refuse,
but let each visit finish. Record per Euria visit: frame, b5a8, the item id, its price / her ask, `rd text` lines, her
closeness after. Stretch: a band-3 weapon (needs closeness ≥ 10: deals at her target `s32(0x730b588)` give +5).

If she never comes over ~10 sessions, report the eligible lists / queues and stop. Save in game at the end (a new slot
if offered), `rd endsave R`.

(The step predicates' `b534 >= 2` is a guess for "the visit is under way"; if the seller machine's states differ, use
the right ones and say so.)
