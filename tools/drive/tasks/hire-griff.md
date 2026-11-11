---
name: hire-griff
start: save:824053720e01
steps:
  - in the Adventurers' Guild: scene == 11
  - on a dungeon floor with control, Griff leading (chara 6): scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1 and s32(0x438b7d8) == 6
  - a normal attack: s32(0x438b4dc) >= 4 and p_act == 2
  - a skill: s32(0x438b4dc) >= 4 and p_act >= 4
  - an enemy hit: s32(0x438b4dc) >= 4 and frame > 600 and (a0hp < f32(0x76b970 + 0x430) or a0use != 1)
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **Griff (roster id 6) fights in a dungeon** — the port has only Louie's and Charme's swings / skills verified;
the other hires' attack code (`FUN_0048cdcc` arms, `src/dungeon_party.c:1562..1598` PORT-DEBT stubs) has no recording.

**The start save is CRAFTED** (`docs/findings/bosses/UNLOCKS.md`): the day-15-morning save with every adventurer
hirable at level 40 and dungeons 0..4 open. Not a state the game produced; report anything odd about it.

Route (measured on this save): `.*60 A .*30 A .*120` loads into the shop with control (f137); leave by the door:
`rd search R --goal "scene == 8" --apply -j 2`; on the town map `D .*7 D .*7 D .*7 R .*7` puts `wm_cur` on 6 (check),
`A` → `scene == 11`. The guild lists "Leave" then the adventurers (the list scrolls; `rd text`): pick **Griff**, pay the
fee, pack nothing special (accept "Equip items in bag?" if asked), choose the first dungeon (Hall of Trials) and
Depart. Validate step 2's chara variable on the floor (`s32(0x438b7d8)`; if it is not 6 for Griff, say what it reads,
and what the last step's max-HP guess `f32(0x76b970 + 0x430)` really is — fix the predicates in your report).

On the floor (read `tools/drive/DRIVER.md`'s dungeon facts and `docs/findings/bosses/combat-core.md` §1–3 first:
`p_act` / `p_actt`, the actor names `a0x a0z a0hp a0st a0d`, `rd ram R actors`, inputs that read state such as
`{dpad(a0x - px, a0z - pz) | A} .*9`): exercise, each at least once and with an enemy in reach where possible —
the normal attack (and its follow-ups if repeated presses chain), every skill the character has (`B` uses the
selected skill, `C` cycles; also try keys `1`..`5`), a dash (double-tap a direction), taking one hit. Per action
report the `p_act` value, its length in ticks, the tick the hit record appears / lands (`0x69324b0 + i*0x124`: type
+0, age +0x98), SP cost, damage dealt. Then walk to the exit (`{dpad(ex - px, ez - pz)}*N --until "portal > 0"`,
`A`) and do the same once on 2F. Do not grind; stop after 2F. Take one strip of a normal attack and one of each skill.

Report, beyond the card's list: the above table, the fee, the bag / equip prompts, and tool gaps.
