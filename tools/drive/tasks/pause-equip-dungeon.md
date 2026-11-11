---
name: pause-equip-dungeon
start: save:824053720e01
steps:
  - on a dungeon floor with control: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1
  - the pause menu on the floor: scene == 9 and s32(0x438b4dc) >= 4
  - back on the floor with control: scene == 1 and s32(0x438b4dc) >= 4 and lock == -1 and frame > 2200
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **the pause menu inside a dungeon — Status, Items (use food), and changing equipment** (the port's dungeon
pause "Equip" variant is unverified). **The start save is CRAFTED** (all hires level 40, dungeons open).

Route to a floor (measured; details in `docs/findings/probes/hire-caillou.md` / `obsidian-1f.md`): `.*60 A .*30 A .*120`;
`rd search R --goal "scene == 8" --apply -j 2`; `D .*7 D .*7 D .*7 R .*7` → `wm_cur == 6`; `A` → guild; hire Louie; in the
bag step pack a spare weapon / armour and two foods if the shop's stock has them (say what was packed), answer the
equip prompt, Hall of Trials, Depart. Read `docs/findings/probes/mushroom-king-arms.md`'s / the Charme card's notes on
pause-menu food if you need the item-list keys.

On the floor: take a hit or spend SP first if cheap; then `~` (pause): visit Status, then Items — use one food (HP /
SP before / after), then equip a different item from the bag and un-equip / swap back (stat totals `atk` / `pdef`
before / after each change), leave. Report every page's `rd text`, the pause variables (`pm_*`), frames, and what
differs from the shop's pause menu.
