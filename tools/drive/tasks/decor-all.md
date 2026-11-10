---
name: decor-all
start: save:175794a5f61c
steps:
  - the counter menu list is up: cc08 == 0 and s32(0x438b74c) >= 10
  - the decor editor is open: cc08 == 23
  - back in free roam afterwards: cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the counter menu's decor editor, all four kinds** — Change Flooring, Walls, Carpet, Tables (`cc08 == 0x17`
and 3). The port has it (2026-10-23) from one recording that changed one thing; its render (the item window over the
room, the variant meshes) is unchecked.

**Crafted start save** (`craft_pix_save.py --base <dungeon3-run2-save's end save> --gold 9999999 --set 0xb100=13`):
day-14 save, 9,999,999 pix, merchant level 14 on the HUD — list A then holds every entry (level ≥ 5 Walls, ≥ 7
Flooring, ≥ 8 Carpet, ≥ 9 Move Tables, ≥ 13 Tables). Owned decor is as played: a kind may offer only its default.

Read first: `docs/findings/shop-counter-menu-RE.md` ("The decor editor" section: `cc08` 0x17 / 3, the preview, the
camera). An earlier run found: load = `.*60 A .*30 A`; a morning event follows (ESC `~` → A on Yes skips it); the
counter is reached with `.*30 DL*16 U*8 UR*16 L*6 A .*7` from where the event leaves Recette; the list cursor is
`s32(0x438cc0c)`, the list `0x74b2d98[]`, count `s32(0x5cd0bc)`; a choice dispatches 15 ticks after A.

For each of the four kinds: open it, move through the variants it offers (the preview changes), pick one that
differs from the current one if there is one, confirm; cancel out of one kind without changing it. Leave to free
roam.

Report, beyond the card's list: per kind the variants offered (ids / names from `rd text`), which variable holds
the current / previewed variant, the bank bytes that changed on confirm (`rd ram mark` / `diff` restricted to the
bank: `--lo` / `--hi` from `rd eval R bank`), the cost if any (`gold`), what cancel restores; strips of each kind's
preview and of the shop after.
