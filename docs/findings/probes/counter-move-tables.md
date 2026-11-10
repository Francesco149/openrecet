# Probe: counter-move-tables

- **Task:** `tools/drive/tasks/move-tables-l10.md` · **start:** save:1a5a45e6 (crafted: level dword 5 → 9)
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 35 turns, $0.61, 4 min
- **Scenario:** `tests/scenarios/counter-move-tables/`
- **Verdicts (2026-11-10):** check PASS; verify MATCH; **port triage SPLIT f618** `- ⇄ init_type_1f_100 (-5)` (PORT-DEBT counter-menu-move-tables)

The driver's report, as written (retail facts observed live through `rd`; treat code-read claims marked as such as leads):

---

Run move-tables-l10: 749 frames, check PASS, verify MATCH. I entered Move Tables at the crafted level 10, moved the centre table to a new spot, put it down and left with the change kept. I looked at pictures of the table in hand (f620, f650, f676) and of the shop afterwards (f745).

Steps and frames:
- Load (.*60 A .*30 A) puts Recette in the shop at f137.
- An event starts straight away (dlg=1, "ESC Key: Event Skip" on screen). I did not find what it is; the picture shows Recette and a second character together by the counter. `~` opens "Do you want to skip this event?" and A on Yes clears it: dlg goes to 0 at f504.
- Walk `.*30 DL*16 U*8 UR*16 L*6 A .*7`: cc08 goes to 0 at f584.
- Step 1 at f591, once s32(0x438b74c) reaches 10 (it counts up while the menu opens).
- `D . D . D . D . A` moves cursor cc0c to 4; cc08 is 10 at f600 and becomes 2 at f615 (step 2), 15 ticks later.
- In the mode:
  - R R takes table 0 from x 3 to 5; a third R (to x 6) was refused.
  - Five L presses take it to x=0, on top of the left shelf; A there was refused (f647-658).
  - R R U takes it to (2,2); A at about f681 put it down and selected table 1.
- B at f693: cc08 2 → 0 (back to the list). B again: cc08=1, dlg=0 at f719 (step 3). I added 30 idle ticks so the last picture shows the shop.

Merchant level and list: level dword s32(0x450fb98)=9 (HUD 10). List A at 0x74b2d98 = [0,2,1,3,5,6], count 0x5cd0bc = 6; Move Tables is at cursor index 4.

Every claim the first run read from the code held in game:
- s32(0x438bea4) is the selected table: starts at 0, A moves to the next one and wraps round.
- s32(0x438bfb4) is the table count (3 here).
- Table i's grid x and z are at bank+183824+8i and bank+183828+8i (0x45105a8/0x45105ac for table 0). Start positions: t0 (3,3) = the 2x2 centre table, t1 (1,0) = the shelf along the top, t2 (0,1) = the shelf down the left.
- 0x438bfcc[i] is the table type: [3,4,4] (3 = 2x2, 4 = 1x4). Rotation float 0x438c01c[i]: 0 = lying across, pi/2 = standing upright (table 2).

Buttons:
- Arrows move one cell per press: R x+1, L x-1, U z-1, D z+1.
- A (Z) puts the table down and picks up the next one.
- B (X) puts it down and leaves to the counter list (cc08 → 0); free roam needs one more B from the list.
- A refused press plays sound 362.

What makes a placement legal (sub_48960D):
- Each check copies a 20-wide room template from 0x5cd104 + 1200*shopsize (shopsize = bank+183776, 0 here) into 0x74b28e8. It then walks the tables in order: each reads the cells it covers, then marks them 2/3/4.
- Bit 1 = the table covers a cell valued 1 or 9 (wall, counter, stone floor patch); the value 4 = a negative coordinate.
- Moving is undone only on bit 1 or the value 4, so a table can be carried over another table.
- A and B need the result to be exactly 0, so they also refuse a spot on another table (an earlier table's marks show up as bit 2).
- Room grid: rows 0-4 are open floor at columns 0-6, with 9s at (5..6,1..2) under the stone patch; columns 7-9 are 1 everywhere; the counter is rows 6-7.

Furniture bytes in the working bank:
- While moving, the only bank change is the position dword.
- Picking up (sub_48940E(i,1)) copies the item values on the table's cells from the 20-wide grid at bank+80024 (0x44f7030) to 0x74b28d8 and writes -1 in their place; putting down copies them back at the new cells, so the items travel with the table.
- After the move, table 0's items (0x6480, 0x64c0, 0x30882, 0x2ee80) sit at (2,2),(3,2),(2,3),(3,3). Table 1's items went back to cells 2-4 of row 0 when I pressed B.
- Other per-mode state: 0x438c0bc[i] is the in-hand lift (0.5 for the carried table); 0x438b4e8 is 1 while in the mode.

Pictures: f650 the carried table drawn raised and tinted on top of the left shelf; f676 raised at (2,2), top-left of the rug; f745 standing at the new spot with its four items.

Crafted save vs a real level 10: the list did include Move Tables and the HUD shows 10. The one mismatch is the HUD Day 15 while the day variable reads 14, which may just mean day counts from 0. The event at load also looks like a day-start event.

Names in this run's vars.json, worth sharing: mt_sel (0x438bea4), mt_count (0x438bfb4), mt_t0x / mt_t0z (0x45105a8 / 0x45105ac).

What the tool lacked: rd eval has no list comprehensions, and `ram read N` counts bytes, not dwords, which cost a few extra calls.
