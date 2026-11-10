---
name: item-storage-l17
start: save:352c146ab865
steps:
  - in the shop with control: scene == 1 and cc08 == 1 and dlg == 0
  - back in the shop at the end: scene == 1 and cc08 == 1 and dlg == 0 and frame > 1500
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Exploration: **"Item Storage" — the merchant level 17 ability.** Tear's Merchant Level page lists L17 as "Item
Storage" (probe `vending-level16`); nobody knows from a recording what it is, where it is used, or what state it has.

**Crafted start save:** the level-16 crafted save (week 3, 470,000 pix) with the merchant level dword raised once
more (`--set 0xb100=16`, HUD 17). Expect events at load, as at level 16: a level-up event, then possibly a tutorial
by Tear (`~`, wait ~45 ticks, `A` on Yes skips each; a first `~` sometimes only shows the box — check `dlg`). If a
tutorial about storage plays, do NOT skip it: `rd tap R A --n 400 --gap 6 --text --until "dlg == 0"` and keep the
transcript.

Read `docs/findings/probes/vending-level16.md` and `change-tables-l16.md` first (how the level page, the counter
menu and its editors were read; `centry` / count `s32(0x5cd0bc)`).

Find out, in this order, spending little: (1) Tear's menu → Merchant Level page: the L17 text, and L18+ if shown
now. (2) Where storage lives: a new counter-menu entry? a new row in a table's display menu or the pause Items
page? a new object in the shop? Look at the counter list (entry codes), Tear's menu list, the pause menu, and
the display menu. (3) Use it: store two different items, take one back; `rd ram mark/diff` (bank range `--lo
0x44e3798 --hi 0x4511760`) for one store action — where stored items live, capacity shown. (4) In-game Save,
`rd endsave` (report the sha). If storage cannot be found, say exactly what you checked.

Report, beyond the card's list: the ability texts, where storage is reached (states: `cc08`, `cc0c`, `cc04`, the
entry code), its screens' text, what each button does, the bank layout of stored items; strips of each new screen.
