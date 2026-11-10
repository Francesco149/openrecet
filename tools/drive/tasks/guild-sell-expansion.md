---
name: guild-sell-expansion
start: save:175794a5f61c
steps:
  - in the Merchants' Guild: scene == 6
  - the Sell item list (mode 0 entered from the Sell option): scene == 6 and s32(0x9642c00) == 0 and s32(0x9640624 + 4*s32(0x9642c04)) == 1   # (the first run showed retail has no mode 3: Buy and Sell share modes 0 → 8)
  - an item was sold: scene == 6 and gold > 9999999
  - the Expansion confirm (guild mode 4): scene == 6 and s32(0x9642c00) == 4
  - back in the shop: scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the Merchants' Guild's Sell commit and store Expansion** — `FUN_00468fd0` (Sell) and `FUN_004864fd`
(Expansion) are PORT-DEBT; no recording sells or expands.

**Crafted start save** (`craft_pix_save.py --base <dungeon3-run2-save's end save> --gold 9999999 --set 0xb100=13`):
day 14, 9,999,999 pix, merchant level 14 on the HUD (so Expansion should be offered and affordable); 32 items in
the bag.

Read first: `docs/findings/merchant-guild-RE.md` ("Key globals": the guild menu block `DAT_09642c00 + off`: `c00`
mode — 1 main list, 0 item list, 3 sell item-pick, 4 Expansion confirm, 5 / 6 Fusion, 8 quantity overlay; `c04`
main cursor; option types in `DAT_09640624[]`, count `DAT_005cfab4`: 0 Buy … 6 Expansion; "Transitions"),
`docs/findings/town-map-RE.md` (shop door → town map → the guild). The load starts with a morning event (`~` → A
on Yes skips it).

Sell one item (pick, quantity, confirm), then take Expansion if the guild offers it (confirm Yes), then Leave and
get back into the shop — a bigger shop if the expansion went through. If Expansion is not in the list, report the
option list and why, and still sell.

Report, beyond the card's list: the main option list (types), per action the modes with frames, gold before /
after each, `items` before / after the sale, the bank bytes the expansion changes (the shop size dword is at
`bank + 183776`: `rd eval R "s32(bank + 183776)"`), what the shop looks like after (a strip), and any scene the
expansion triggers.
