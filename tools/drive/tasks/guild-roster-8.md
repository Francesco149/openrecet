---
name: guild-roster-8
start: save:824053720e01
steps:
  - in the Adventurers' Guild: scene == 11
  - back on the town map without hiring: scene == 8 and frame > 600
forbid:
  - back at the title: scene == 0 and frame > 400
  - a dungeon: scene == 1 and s32(0x438b4dc) >= 4
---
Probe: **the Adventurers' Guild's roster with all eight adventurers** — the port's guild menus were verified with Louie
and Charme only. **The start save is CRAFTED** (all 8 hirable at level 40; `docs/findings/bosses/UNLOCKS.md`).

Route: `.*60 A .*30 A .*120` (control at f137); `rd search R --goal "scene == 8" --apply -j 2`; `D .*7 D .*7 D .*7 R .*7`
→ `wm_cur == 6`; `A` → `scene == 11`. Read `docs/findings/adventurers-guild-RE.md` for the menu machine.

In the roster list ("Leave" + 8 rows, it scrolls): put the cursor on each adventurer in turn, top to bottom and back up
(the description panel and equipment rows change: `rd tap` / `rd text` per row — name, class line, three description
lines, equipment, fee). For TWO of them other than Louie (one caster, one of the last three): press `A`, go through
what the game asks up to the dungeon / floor-row list (report the dungeon list and every floor row shown for each
dungeon), then back out with `B` all the way to the roster without departing. Finally "Leave" → the town map.
Report the texts per adventurer, the list scroll behaviour (cursor / scroll variables: mark / diff), frames.
