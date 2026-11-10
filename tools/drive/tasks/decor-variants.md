---
name: decor-variants
start: save:a8fac0798453
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

**This is the second run.** The first (`decor-all`, same save without the extra items) found every kind offering only
its default, so nothing could change. Crafted start save now: `craft_pix_save.py --base <dungeon3-run2-save's end
save> --gold 9999999 --set 0xb100=13 --add-item 5002 --add-item 5003 --add-item 5102 --add-item 5103 --add-item 5402
--add-item 5403 --add-item 5302 --add-item 5303` — two more floorings, walls, carpets and counters in the bag
(whether each id is a real decor item is part of what you find out: report what the lists show).

Known from the first run: list A = [0, 2, 1, 3, 5, 4, 6] (Open Store, Walls, Flooring, Carpet, Move Tables, Tables,
Cancel), so cursor 1 Walls, 2 Flooring, 3 Carpet, 5 Tables; `cc08` 10 → 0x17 15 ticks after A, → 3 on the next
tick; the item window is titled Wallpapers / Flooring / Carpets / Counters; the room's selection dwords are
bank+0x2cde4 (wall), +0x2cde8 (floor), +0x2cdec (carpet), +0x2cdf0 (tables); opening a kind moves the room's
current decor item into the bag (bag slot at 0x44e3830 → −1 on close); the morning event's skip needs `~`, then wait
for the Yes / No box (~40 ticks), then `A` (check `dlg == 0`).

This run: for each kind move through the rows (the preview should change — find the row cursor / previewed-variant
variable with mark / diff), confirm a non-default variant for at least two kinds, cancel out of one after
previewing a different variant (what does cancel restore?), and leave. Strips of a preview that differs from the
default and of the changed shop.
