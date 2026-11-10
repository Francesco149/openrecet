---
name: vending-level16
start: save:2afe7fb42617
steps:
  - in the shop with control: scene == 1 and cc08 == 1 and dlg == 0
  - back in the shop at the end: scene == 1 and cc08 == 1 and dlg == 0 and frame > 1500
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Exploration: **what merchant level 16 unlocks, vending machines above all.** FRONT asks for "the furniture-stand
tooltip — once vending machines are placeable (merchant level 15), face one"; nobody knows from a recording how they
are obtained or placed.

**Crafted start save:** the week-3 save of `payday-week2` with the merchant level dword `DAT_0450fb98` raised to 15
(HUD 16): `craft_pix_save.py --base 64d43650… --set 0xb100=15`. 470,000 pix. A level-up event will play at load
(`~`, wait ~40 ticks, `A` on Yes; check `dlg == 0`).

Find out, in this order, spending little: (1) Tear's menu → Merchant Level page (`cc08 == 18`): read the abilities
listed for levels 14–16 with `rd text` (scroll to them) — it names what each level unlocks; (2) the counter menu's
list at this level (`centry` over the cursor, count `s32(0x5cd0bc)`); (3) wherever (1) says vending machines come
from (the Market's stock? the counter menu? the guild?) — go there, get one, place it in the shop, and stand facing
it (the tooltip / the menu it opens). If it cannot be obtained with this save, say exactly what is missing.

Report, beyond the card's list: the level 14–16 ability texts, the full counter list, where vending machines come
from and how one is placed (state: which variables), what facing one shows (`rd text`, a picture). Strips of each
new screen. The steps above are deliberately loose: the report is the result.
