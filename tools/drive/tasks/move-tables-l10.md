---
name: move-tables-l10
start: save:1a5a45e631b4
steps:
  - the counter menu list is up: cc08 == 0 and s32(0x438b74c) >= 10
  - Move Tables mode: cc08 == 2
  - back in free roam afterwards: cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the counter menu's "Move Tables"** (`cc08 == 2`, `FUN_0048940e`) — `PORT-DEBT(counter-menu-move-tables)` in
the port; no recording enters it.

Read first: `docs/findings/shop-counter-menu-RE.md` (the counter menu: `A` at the counter → `cc08 == 0` list A
`DAT_074b2d98[]`, count `DAT_005cd0bc`, cursor `cc0c` = `s32(0x438cc0c)`; entries 0 Open Store, 1..4 decor, 5 Move
Tables (merchant level ≥ 9: `s32(0x450fb98)` is the 0-based level), 6 Cancel; choosing dispatches after 15 ticks of
`cc08 == 10`), and the IDA body of `sub_48940E` in `/mnt/ephemeral/openrecet/decompiled-ida/all_ida.c`.

The start save is day 14 (title → Continue → slot, ~190 ticks). Walk to the counter, open the menu, choose Move
Tables. Inside: pick a table up, move it, put it down somewhere else, and leave the mode with the change kept. If
the list has no entry 5 at this merchant level, report the level and the list and stop there.

The finding is the mode itself: which variables hold the selected table, its grid position and the carried
state; what each button does; what makes a placement legal; how the mode ends (and whether the furniture bytes of
the working bank change — `tools/save_arena_map.json` names the regions). Strips of the mode with a table in hand
and of the shop afterwards.

**Crafted start save** (`tools/craft_pix_save.py --base <dungeon3-run2-save's end save> --set 0xb100=9`): the same day-14 save with the merchant level dword `DAT_0450fb98` raised 5 → 9 (HUD 10), because the played saves stop at level 6 on the HUD. Everything else is as played (experience, unlock flags), so say in the report if something looks inconsistent with a real level 10.
A first run (`move-tables`, level 6) reached the counter menu at f208 with the walk `DL*16 U*8 UR*16 L*6 A .*7 D*3` from the load (`.*60 A .*30 A`, in the house ~f137) and found list A = [0, 2, 6] (no entry 5 below level 9). It also read the mode's code: selected table `s32(0x438bea4)`, table count `s32(0x438bfb4)`, grid x / z at `bank + 183824 + 8*i` / `bank + 183828 + 8*i`, arrows move, A puts down and takes the next table, B leaves (both only on a legal spot: `sub_48960D` returns 0). None of that was checked in game: check it.
