---
name: obsidian-first-trip
start: save:bbdbe357822b
steps:
  - in the Adventurers' Guild: scene == 11
  - the first-trip flag set by Depart (still in the guild or loading): u8(bank + 0x2bce2) == 1
  - event 7/6 playing on the cutscene stage: sid == 7006 and dlg == 1
  - the cutscene cleared itself, the seen byte set: u8(bank + 0x2bce2) == 0 and u8(bank + 0x2bce3) == 1
  - on Obsidian Tower 1F with control: scene == 1 and s32(0x438b4c8) == 3 and floor == 0 and dlg == 0 and lock == -1
  - one floor down: s32(0x438b4c8) == 3 and floor >= 1 and lock == -1
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **the Obsidian Tower first-trip cutscene** — guild Depart to dungeon 3 row 0 while `bank+0x2bce3` == 0 sets
`bank+0x2bce2` = 1; `FUN_0045bcea` then loads stage type 20 (the cutscene stage), `FUN_0044e08f`'s +0x2bce2 arm sets
it up (party row 5 back, `DAT_0438c148` = 1, the `DAT_0438c150..` curtain rows, `DAT_0438c15c` = 0) and
`FUN_004427f1`'s arm plays it (the curtain camera, event 7/6 at event tick 200, fade, at tick 460 clears +0x2bce2,
sets +0x2bce3, reloads floor 0). No recording has played it; the port stubs all three (`docs/port-debt.md`).

**The start save is CRAFTED**: `824053720e01` (day-15 morning, every adventurer hirable at level 40, dungeons 0..4 open,
`docs/findings/bosses/UNLOCKS.md` §5) with dword 0xaf38 = 0x00000101 (+0x2bce2 = 0, +0x2bce3 = 0: the natural
first-trip state). Report any other dialogue / cutscene that plays.

Route (measured on the base save, `tasks/obsidian-1f.md`): `.*60 A .*30 A .*120` loads into the shop (control at f137);
door: `rd search R --goal "scene == 8" --apply -j 2`; town map `D .*7 D .*7 D .*7 R .*7` → `wm_cur == 6` (check), `A`
→ `scene == 11`. Hire **Louie** (first row under "Leave"), accept the equip prompt, choose Obsidian Tower, its **first
row** (1F), Depart. Then let the cutscene play out: advance its text with `A` only when the box waits (watch `dlg`,
`rd text`); do not skip ahead with menus. Record per phase the frame, `s32(0x438b4dc)` (stage record), `u8(bank + 0x2bce2)`
/ `u8(bank + 0x2bce3)`, the event id(s) and their text, and a `rd look` picture of the cutscene stage.

On 1F: `rd ram R actors` (types, HP), kill one enemy with aimed swings, take the exit
(`rd play R "{dpad(ex - px, ez - pz)}*400" --until "portal > 0"`, `A` on the prompt). 2F reached is enough; stop there.
Keep `hp` above 40 %.
