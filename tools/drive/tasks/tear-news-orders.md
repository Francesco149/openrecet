---
name: tear-news-orders
start: end:dungeon3-run2-save
steps:
  - Tear's menu is open: cc08 == 15
  - cursor on News Summary (list B entry 8): cc08 == 15 and s32(0x74b28b0 + 4*s32(0x438cc0c)) == 8
  - the News Summary page was opened and Tear's menu is back: cc08 == 15 and s32(0x74b28b0 + 4*s32(0x438cc0c)) == 8
  - cursor on Outstanding Orders (list B entry 9): cc08 == 15 and s32(0x74b28b0 + 4*s32(0x438cc0c)) == 9
  - back in free roam at the end: cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Tear's menu pages "News Summary" and "Outstanding Orders"**. The port draws neither (PORT-DEBT: entries 8 / 9
of `FUN_0040a765` LABEL_100) and no recording opens them.

Read first: `docs/findings/shop-counter-menu-RE.md` ("FUN_004850fe — the entry lists": list B `DAT_074b28b0[]`,
count `DAT_005cd0c0`, is the `cc08 == 15` Tear menu: `[0, 5, 7]`; day ≥ 9 adds 8; merchant level ≥ 6 adds 9;
`DAT_0450f423` adds 6; then 4), `docs/PROGRESS.md` (search "be7c": Tear in reach = within 2 of her, facing her, once
the shop is open; `A` then talks to her: `cc08 == 15`, "Yes? What is it?"), `docs/findings/scene1-walker.md`
("case 8 — News Summary").

The start save is day 14 (title → Continue → slot, ~190 ticks). Find Tear (her position: look for it —
`docs/findings/scene1-tear-visual-diffs.md`, `src/state_va.c`, or mark / diff while she wanders), get in reach,
talk, move the cursor (`cc0c` = `s32(0x438cc0c)`) onto News Summary, open it, look at it, close it, then the same
for Outstanding Orders if the list has entry 9 (merchant level `s32(0x450fb98)` ≥ 6; if it does not, say so and
stop after News Summary — the fourth step then cannot pass and that is the finding).

The third step's predicate is the same as the second on purpose (the checker only needs the menu back on that
entry after the page); what the page IS in state terms is the finding: report which `cc08` value / variables each
page uses while open, what the inputs do there (scroll? pages?), and how it closes. Strips of both pages.
