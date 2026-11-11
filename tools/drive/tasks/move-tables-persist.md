---
name: move-tables-persist
start: save:2afe7fb42617
steps:
  - Move Tables mode: cc08 == 2
  - back in free roam with table 0 moved off (3,3): cc08 == 1 and dlg == 0 and (s32(0x45105a8) != 3 or s32(0x45105ac) != 3)
  - out on the town map: scene == 8
  - home again in the shop, table 0 still where it was put: scene == 1 and dlg == 0 and cc08 == 1 and frame > 1500 and (s32(0x45105a8) != 3 or s32(0x45105ac) != 3)
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **moved tables persist across leaving and re-entering the shop, and across a save** —
`PORT-DEBT(simplified, FUN_0048ffd9)` (`src/scene1_postload.c:861`): the port re-seeds the furniture template on
every house entry, retail only at bank init / new game / the expansion, so in retail a table moved in Move Tables
stays moved.

Read first: `docs/findings/probes/counter-move-tables.md` (how the mode works: the counter walk, list index of Move
Tables, `mt_sel` 0x438bea4, table i's grid x / z at 0x45105a8 + 8i / 0x45105ac + 8i, legal placements) and
`docs/findings/shop-counter-menu-RE.md`.

The start save is CRAFTED level 16 (day 15 morning, 470,000 pix): a level-up event plays at load — `~`, wait ~40
ticks, `A` on Yes, check `dlg == 0`. The counter list at this level has more entries than at level 10 (Change
Tables, vending): read the list (`rd text`) and find Move Tables' index.

Do: Move Tables → move table 0 to another legal spot AND one other table too → leave the mode → walk out the shop
door to the town map → enter the Merchant's Guild (or any place) and come back home (this burns a period; fine) →
back in the shop check the tables' positions and the items on them (the 20-wide display grid at bank+80024 =
0x44f7030) → open the display menu of the moved table once (A facing it) and close it → walk around the moved
table (collision should be at the new spot: try to walk through the OLD spot and into the NEW one, report px / pz)
→ pause menu → Save. Then, if cheap: go out and in once more.

Report, beyond the card's list: table positions before / after each transition, whether anything reset, the
collision observations, the frames, a strip of the shop after returning.
