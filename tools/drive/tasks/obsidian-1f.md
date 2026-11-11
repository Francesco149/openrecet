---
name: obsidian-1f
start: save:824053720e01
steps:
  - in the Adventurers' Guild: scene == 11
  - on Obsidian Tower 1F with control: scene == 1 and s32(0x438b4c8) == 3 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1
  - one floor down: s32(0x438b4c8) == 3 and floor >= 1 and lock == -1
  - two floors down: s32(0x438b4c8) == 3 and floor >= 2 and lock == -1
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **dungeon 3 (Obsidian Tower), the first floors** — no recording has entered it; the port stubs its setup
(`FUN_0044e08f`) and events (the dungeon-3 arm of `FUN_004427f1`), its enemy lists are unverified.

**The start save is CRAFTED** (`docs/findings/bosses/UNLOCKS.md`): day-15 morning, every adventurer hirable at level
40, dungeons 0..4 and every floor row open, the first-trip cutscene and pre-fight dialogues switched off by their
"seen" bytes. Not a state the game produced: report every dialogue / cutscene that plays anyway.

Route (measured on this save): `.*60 A .*30 A .*120` loads into the shop (control at f137); door:
`rd search R --goal "scene == 8" --apply -j 2`; town map `D .*7 D .*7 D .*7 R .*7` → `wm_cur == 6` (check), `A` →
`scene == 11`. Hire **Louie** (first row under "Leave"), accept the equip prompt, choose Obsidian Tower (report the
dungeon list and the floor rows shown), its first row, Depart. Validate `s32(0x438b4c8) == 3` on the floor (if the
dungeon id reads otherwise, say so and keep going).

On each floor (read `tools/drive/DRIVER.md`'s dungeon facts, `docs/findings/bosses/combat-core.md` §1–3 and
`docs/findings/bosses/ROSTER.md`'s dungeon-3 rows): `rd ram R actors` for the enemy list (types, HP, level `+0xae0`),
kill at least one enemy of each type you meet with aimed swings (`{dpad(a3x - px, a3z - pz) | A} .*9`), then the exit:
`rd play R "{dpad(ex - px, ez - pz)}*400" --until "portal > 0"` where nothing blocks, a scored search
(`--score "-exitd"`, 12-tick aimed and plain walks as actions) around walls; `A` on the portal prompt. Go as deep as
the budget allows without grinding (3F is enough; if 5F's boss — the pumpkin ghost, `docs/findings/bosses/pumpkin-ghost.md`
— is in reach, try it by its card). Keep `hp` above 40 % (`--avoid`).

Report, beyond the card's list: per floor the frames, enemy types × counts, HP in / out, what worked for navigation,
every event / ticker text, a strip of 1F, and tool gaps.
