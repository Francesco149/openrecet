---
name: tear-orders-l10
start: save:1a5a45e631b4
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

**Crafted start save** (`tools/craft_pix_save.py --base <dungeon3-run2-save's end save> --set 0xb100=9`): the same day-14 save with the merchant level dword `DAT_0450fb98` raised 5 → 9 (HUD 10), because the played saves stop at level 6 on the HUD. Everything else is as played (experience, unlock flags), so say in the report if something looks inconsistent with a real level 10.
A first run (`tear-news-orders`, level 6) opened News Summary: `A` on the menu → `cc08 == 16` for 15 ticks → `cc08 == 17` (the page; `b8c0` = `s32(0x438b8c0)` counts to 10 before input; A or B closes back to `cc08 == 15`). Tear: position `f32(0x56da1f0)`, `f32(0x56da1f8)`; in reach when `s32(0x438be7c) == 20` (within 2, facing her). Entry 9 was missing there. Here it should exist: this run is about **Outstanding Orders** — do News Summary quickly, then spend the effort on entry 9 (what the page shows with this save's orders, what state it uses, whether its text differs from News Summary's path).
