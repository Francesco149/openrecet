# Probe: move-tables

- **Task:** `tools/drive/tasks/move-tables.md` · **start:** end:dungeon3-run2-save
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 20 turns, $0.31, 2 min
- **Scenario:** none filed
- **Verdicts (2026-11-10):** stopped by design: no Move Tables below merchant level 10 (HUD); the mode read from the code

The driver's report, as written (retail facts observed live through `rd`; treat code-read claims marked as such as leads):

---

I could not enter Move Tables from this save: at merchant level 6 the menu has no Move Tables entry. As the card says to, I stopped once the list was confirmed. `rd check` gives FAIL: step 1 is reached, step 2 is not, so step 3 was never judged. `rd verify` gives MATCH.

**Run `move-tables`** (from end:dungeon3-run2-save), 210 frames.
- f0–151: title → Continue → slot 0. In game at f108 (scene 1, cc08 1), day 14 (the HUD shows Day 15), gold 9086.
- **Step 1, the counter menu list is up: frame 208.** A search found the walk to the counter: `DL*16 U*8 UR*16 L*6 A .*7 D*3`. At f208, cc08 is 0, b74c ≥ 10 and the cursor cc0c is 0.
- f210: I pressed B and was back in free roam (cc08 1, dlg 0).
- **Step 2, Move Tables mode (cc08 == 2): not reached.**

**Level and list**
- Merchant level `s32(0x450fb98)` = 5 (0-based; the HUD shows 6). Move Tables needs ≥ 9, which is HUD level 10.
- At f208 the list `DAT_074b2d98[]` holds [0, 2, 6] and the count `DAT_005cd0bc` is 3. `rd text` shows the rows "Open Store", "Change Walls", "Cancel", and the strip at f200–208 shows the same three buttons.
- The count reads 7 right after load and only becomes 3 once the menu is built, so it is only meaningful while the menu is up.
- Getting from level 6 to 10 would take many shop days, which doesn't fit this budget.
- To probe the mode, someone needs a save at merchant level ≥ 10 (HUD).

**How the mode should work (read from the IDA code only, not checked in game)**
These are the cc08==2 arm of the per-frame handler (around line 97087), `sub_48940E` and `sub_48960D`. "bank" is `0x44e3798 + 188360*slot`.
- **Tables:** there are `s32(0x438bfb4)` of them (3 in this save).
- **Selected table:** `s32(0x438bea4)`, set to 0 on entry. Its type is `dword_438BFCC[i]`, and `flt_438C01C[i]` (0 or not) picks between the two orientations of the long table.
- **Grid position:** dwords at bank+183824+8*i (x) and bank+183828+8*i (z).
- **Footprints:** type 3 is 2×2; type 4 is 4×1 when the float is 0, otherwise 1×4; type 6 is 1×4.
- **Carrying:** `sub_48940E(i, 1)` lifts table i. It saves the floor-grid cells under it (the room grid is 20 wide at bank+80024, dwords) into `dword_74B28D8[]` and writes −1 into them. `sub_48940E(i, 0)` writes the saved cells back at the table's current position. The selected table is always the one in hand: entry calls `(0, 1)`.
- **Arrows (edge mask `DAT_073dddd4`):** 0x20000 means x−1, 0x10000 x+1, 0x40000 z−1, 0x80000 z+1. The step is undone, with SE 362, if the check returns bit 1 (wall) or 4 (off grid). Otherwise SE 326 plays.
- **While moving:** overlapping another table is allowed. The selected table's `flt_438C0BC[sel]` is set to 0.5, probably its highlight or lift.
- **0x10 (confirm):** only if the check returns 0. It puts the current table down, moves to the next one (wrapping around) and lifts it, with SE 323. If the check fails it plays SE 362.
- **0x20 (cancel):** only if the check returns 0. It sets cc08 0 (back to the menu list), b754 0 and b4e8 0, puts the table down, and plays SE `bin/se/00re_system…`. If the check fails it plays SE 362.
- **What makes a placement legal (`sub_48960D`):** it copies the room's static grid (`unk_5CD104` + 1200 × room size, the size being bank dword at +183776) into `dword_74B28E8`. It then stamps every table's footprint there (2, 3 or 4).
  - A cell of 1 or 9 under a table sets bit 1 (wall or blocked).
  - Any other nonzero cell sets bit 2 (overlap).
  - A negative coordinate returns 4.
  - A placement is legal only if the result is 0.
- **Is the change kept?** No revert path shows up in this arm: the moved x/z stay in the bank. Whether these bytes are the ones save_arena_map.json names as furniture needs an in-game RAM diff.
- **Camera:** the camera class `b4e8` is 1 in the mode, and the look-at eases toward a per-room-size target.

**Proposed names (not defined in this run):** `mlevel = s32(0x450fb98)`, `mt_sel = s32(0x438bea4)`, `mt_n = s32(0x438bfb4)`, `mt_x = s32(bank+183824+8*mt_sel)`, `mt_z = s32(bank+183828+8*mt_sel)`, `menu_n = s32(0x5cd0bc)`.

**Tool gaps**
- `tools/save_arena_map.json` is not readable from here (read and grep both refuse it), so I could not match bank+80024 or +183824 to its region names.
- There is no way to give a task a precondition like "level ≥ 9" before choosing its start save.

**Pictures:** the menu strip is `runs/move-tables/view/strip_200_210.png`. There are no strips of the mode itself, since it was never entered.
