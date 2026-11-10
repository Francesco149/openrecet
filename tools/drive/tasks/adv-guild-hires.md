---
name: adv-guild-hires
start: end:dungeon3-run2-save
steps:
  - in the Adventurers' Guild: scene == 11
  - back out with someone hired, on a dungeon floor: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0   # (first run: `floor` 0x438b4cc is 0-based — 0 in the shop AND on 1F; the stage record 0x438b4dc is < 4 in shop stages)
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **an adventurer other than Louie or Charme in a dungeon** — the port has only those two hires' swings and
skills verified; chars 28 / 30 / 31 / 40 / 41 / 42 are ported "ahead of a recording".

Read first: `docs/findings/adventurers-guild-RE.md` (mode 11: the menu machine, the party list, hiring and
equipping, packing, choosing the dungeon), `docs/findings/town-map-RE.md`.

The start save is day 14 with 9,086 pix (`.*60 A .*30 A`; a morning event follows: `~` → A on Yes). Go to the
Adventurers' Guild, list who can be hired (names from `rd text`, the roster variables), hire someone who is NOT
Louie or Charme if the save offers one (and can be afforded), pick any dungeon they can enter, and get onto its
first floor with control (`dlg == 0`). Then make the hire swing at least a few times and, if you can find how, use a
skill (buttons 1..5 / C — find out) — with or without an enemy in reach. If only Louie and Charme are offered, say
so with the roster state and stop after the guild step.

The second step's floor variable (`s32(0x438b4cc)`, the port's "next floor") must be validated: it should not hold
in the shop (lint it on your run before leaving home). Adjust it in a copy of this task if it is wrong, and say so.

Report, beyond the card's list: the roster (ids, names, levels, prices), who was hired (char id), the dungeon and
floor, the frames of the first swings / skill uses and the state that showed them (the actor's animation / action
variables — name them), party HP / SP variables, and a strip of a swing.
