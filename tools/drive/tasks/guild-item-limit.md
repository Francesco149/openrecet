---
name: guild-item-limit
start: save:3f9c0a2684a2
steps:
  - in the Merchants' Guild main list: scene == 6 and s32(0x9642c00) == 1
  - the Buy item list: scene == 6 and s32(0x9642c00) == 0
  - A on an item with 15,000+ items held → the message box is up (its counter DAT_0438b148): scene == 6 and s32(0x9642c00) == 0 and s32(0x438b148) != 0
  - the box dismissed, the list still open (no quantity overlay): scene == 6 and s32(0x438b148) == 0 and s32(0x9642c00) == 0
  - out of the guild again: scene != 6
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
  - the quantity overlay opened (a purchase went through the limit): scene == 6 and s32(0x9642c00) == 8
---
Probe: **the Merchants' Guild's "Item limit reached, cannot buy any more."** — in the Buy item list (mode 0, the
select `FUN_00469414(1)` → v47 1), A on an item while the guild option is Buy (type 0) and `FUN_00491b16` (non-empty
inventory slots) ≥ 15,000 opens the message box `FUN_00434ceb` and returns (all_ida 105526); `FUN_00434d6a` then ramps it and closes it on Z / X. The port had it as a PORT-DEBT stub.

**Crafted start save** `3f9c0a2684a2` = `craft_pix_save.py --base 175794a5f61c --add-item 1001x15000` (the
guild-sell-expansion base: day 14, 9,999,999 pix, merchant level 14; + 15,000 copies of item 1001, one slot each →
15,032 held). Not a reachable-by-play state in any sane run; say so in the report.

Route: as `tasks/guild-sell-expansion.md` (the load starts with a morning event: `~` → A on Yes skips it; shop door →
town map → the guild). In the guild main list choose Buy, then press A on an item in the list. Record the frame the box opens,
`rd text` of it, the frame it closes (A or B — note which), and that no quantity overlay opened. Try Buy a second
time (the box again), then Leave the guild. Note anything else the 15,000 items change (load time, the morning event,
menus).
