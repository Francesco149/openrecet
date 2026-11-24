---
name: arma-weapons
start: save:352fc5a20843
steps:
  - in the Adventurers' Guild: scene == 11
  - on a dungeon floor with control, Arma leading (chara 7): scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and s32(0x438b7d8) == 7
  - a normal attack: s32(0x438b4dc) >= 4 and p_act == 2
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **Arma's weapon kinds** — `FUN_004986ee` (port `party_attack_arma`, `src/dungeon_party.c` ~2051): at attack state 1
the item in her first equipment slot (ids 701..715) picks the shot's record kind (113 / 117 / 119 / 118 / 120 / 115 /
114 random / 126 / 116 / 123 / 121 / 125 / 122 / — / 124 for 701..715; 714 = a random kind with a 1-in-16 trap of kinds 1
/ 3), its gauge cost (db0e0), SE and voice; rapid weapons loop back to state 9 while A is held and the gauge lasts (118
from 19, 120 / 122 from 17, 115 / 126 from 14; 114 from 30 back to 20 for 5 more); the end at 40 (60 for 116 / 121 /
124). The coverage index (`where.py FUN_004986ee`) shows only `hire-arma`'s Punch Unit run: 59 of 87 blocks never ran.

**Crafted start** `352fc5a20843` = `824053720e01` (the crafted hire lineage: everyone at level 40, day 15; `SAVES.md`)
`--add-item 701 … 715` (one of each, bag slots 32..46). Read `docs/findings/probes/hire-arma.md` first (the route to the
guild, Arma's fee and gear, the bag / Depart screens, the Hall of Trials row, the "Equip items in bag?" box).

Do: hire Arma, bring the 15 items (the bag screen: pick what to bring; if the bag limit stops you, bring as many as fit
and do a second trip for the rest), go to the Hall of Trials. On the floor, for **each** item: equip it in her weapon
slot (dungeon pause → Equip; `bossrush` keys in `tools/drive/SAVES.md` § loadouts show the Equip page path), then fire
a single shot (tap A) and one held burst (hold A ~60 ticks) — one after another, each a separate stretch in the movie.
Record per item: its name (`rd text` on the Equip page), the shot's record kind (`rd ram R records` during the shot:
type), whether the hold looped, the gauge (find its VA: daf.. / db0e0 per the port comment) before / after. Hit an
enemy with at least a few of them. If an item does not equip in that slot, try the others and report which slot takes
it. Stop after all 15 (or when out of time), back to town not needed.
