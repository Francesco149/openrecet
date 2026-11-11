# Probe: move-tables-persist

- **Run:** `move-tables-persist` · **start:** save:2afe7fb42617 · **task:** `tools/drive/tasks/move-tables-persist.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 71 turns, $1.01, 13 min
- **Scenario:** `tests/scenarios/move-tables-persist/` (1901 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/move-tables-persist.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     move-tables-persist                  f1712 - ⇄ sim_step_a (-1) — **fixed 2026-11-11** (no template re-seed on house entry, FUN_0048ffd9's three retail callers only): identical, ticks 1..2201, save IDENTICAL

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

Run **move-tables-persist**: 1901 frames. `rd check` gives PASS and `rd verify` gives MATCH (frame 1901, rng 93e6a9b4). I looked at a strip covering f860–1900, plus single frames at f403, f900 and f989. **Result: in retail, moved tables stay where they were put.** They survived leaving the shop, a visit to the Merchants' Guild, coming home, a Save, and a second trip out and back. Nothing reset at any point.

**Steps and frames**
- **Load and events:** `.*60 A .*30 A` puts Recette in the shop at f137.
  - The level-up event starts at f138. `~ .*45 A` skips it, and dlg reaches 0 at f200.
  - A second event (sid 19002) starts about 5 ticks later and also needs skipping: `~ .*45 A`, then dlg is 0 at f297.
- **Counter walk:** the documented walk does not work at level 16. It is blocked at x = −1.5 (the red stool near the counter), and its A talked to Tear instead (cc08 15).
  - A search found `D*8 .*8 L*4 A` from (−0.08, 8.89), which gives cc08 = 0 at f424, standing at (−1.11, 9.5).
- **Counter list at level 16:** Open Store, Change Walls, Change Flooring, Change Carpet, Move Tables, Change Tables, Cancel. Move Tables is index 4.
- **Step 1 (Move Tables mode, cc08 == 2): f458.**
- **The moves:**
  - Table 0 went from (3,3) to (1,2) with L L U, and A put it down.
  - Table 1 went from (1,0) to (2,0) with R, and B put it down.
- **Step 2 (free roam, table 0 moved): f505.** Checkpoint `moved`.
- **Walk to the door:** found by search.
- **Step 3 (town map, scene 8): f680.**
- **Merchants' Guild and home:**
  - On the map, `R` moves the cursor from Home to wm_cur 3. A opens the Guild (scene 6, f726). Its menu is Buy / Sell / Fusion / Expansion / Talk / Leave, and `U A` is Leave (map again at f813).
  - `D` takes the cursor from 3 to 0 (Home), and A brings Recette home at f860, at (2, −3.24) on the stone patch.
  - This trip used up a period: stime went 0 → 1. wms read 1121011, so Town Square had an event waiting; I kept the cursor off it.
- **Collision test and display menu:** f900–1218 (details below). Checkpoint `home1` at f900.
- **Save:** pause menu, Save, slot 020, Yes on "Overwriting file. Are you sure?". The slot's TIME changed from 2:17:46 to 2:18:09, so the file was written (about f1490). The menu closed at f1571.
- **Step 4 (home, table still moved, frame > 1500): f1571.** Checkpoint `saved` at f1581.
  - Save filed with endsave: **save:651193132990**.
- **Second trip out (the optional part):** door reached by search at f1826, Home picked straight from the map at f1866. Going straight Home did not use a period (stime stayed 1). Tables still moved at f1881–1901.

**Table positions (grid x,z; table count 3; types [3,4,4]; shop size 0)**
- **At load:** t0 (3,3), t1 (1,0), t2 (0,1).
- **After the moves (f505), at f860–900 back home, after the Save, and at f1881 after the second trip:** t0 (1,2), t1 (2,0), t2 (0,1). Same every time.

**Items on the display grid (0x44f7030)**
- **Table 0** carried its items to the new spot: (1,2)=0x6480, (2,2)=0x64c0, (1,3)=0x30882, (2,3)=0x2ee80 (Thankful Statue). The old cells (3..4,3..4) are −1.
- **Table 1's** items moved from columns 2..4 of row 0 to columns 3..5: 0x177c0, 0x19c0, 0x2f140.
- **Table 2:** (0,1)=0xc0, (0,3)=0x3280, (0,4)=0x4b80, and (0,2) is −1. This is unchanged.
- All of these values were identical after each transition.

**Display menu:** facing up at the new table from (−4.36, 0.83), A opened the display menu (cc04 1 at f1167) over Thankful Statue (Clothes / Nothing / Tough Clothes list). B closed it at f1189.

**Collision:** grid to world is roughly x ≈ −9 + 2·col and z ≈ −7 + 2·row, with 2-unit cells. That is my estimate from where she stopped and the pictures.
- **Old spot:** walking L at z 1.44 crossed x 1 → −7.49 without stopping (row 4 of the old spot, where she stopped at the left shelf's bow). Then at x −0.68, walking U went freely from z 1.63 to −5.08 through the old spot's whole z range. She stopped only at table 1 (row 0).
- **New spot:** at x −4.97, walking U stopped at z 0.857 (the new table's bottom edge). Walking L at z ≈ 0.03 stopped at x ≈ −3.36 (its right edge), then slid down along the corner to (−4.35, 0.83).
- So collision is at the new spot, and the old spot is open.

**Other game facts**
- The pause menu on this save is Status, Items, Encyclopedia, Options, Save, Exit Game. pm_type by pm_sel is 0, 1, 6, 2, 3, 4, so **Save is pm_sel 4** (type 3).
- After saving, the picker ignores B for about 40 ticks.
- On re-entry from the town map Recette appears at (2, −3.24).

**Strip of the shop after returning (f860–1900):** table 0 sits next to the left shelf with its 4 items, table 1 is shifted right along the top wall, and the rug is empty. It looks the same before and after the Save and after the second trip.

**Tool gaps**
- `rd eval` has no list comprehensions, so reading grid cells takes long expression lists.
- There is no `--every` style digest for one dword across many addresses.
- The card's counter walk and its "Save is the 2nd entry" layout do not hold at level 16. Searching worked for the walk.
