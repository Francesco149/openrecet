# Probe: guild-sell-expansion

- **Run:** `guild-sell-expansion` · **start:** save:175794a5f61c · **task:** `tools/drive/tasks/guild-sell-expansion.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 64 turns, $0.80, 7 min
- **Scenario:** `tests/scenarios/guild-sell-expansion/` (1100 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/guild-sell-expansion.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Result:** I did the whole sequence in retail — sold one item, bought the store Expansion, left the guild and got back into a bigger shop. But `rd check` FAILS at step 2: the task's predicate `s32(0x9642c00) == 3` can never become true. The retail guild never sets mode 3. `rd verify` prints MATCH.

**Run:** `guild-sell-expansion`, 1100 frames, from `save:175794a5f61c`.

**Steps and frames:**
- **Load:** A, A from the title; in the shop at f137. The morning event's skip prompt was at f199; `~` then A on Yes ended it, `dlg == 0` at f210.
- **Leave the shop:** walked to the door and pressed A facing right at (3.0, −3.9); town map (scene 8) at f470.
- **Map:** U moved the cursor to dest 2 (Town Square), R to dest 3 (Merchant's Guild), then A.
- **Step 1, in the Merchants' Guild:** `scene == 6` at f556 (`check` agrees), mode 1.
- **Step 2, the Sell list:** D then A on Sell. The Sell list is **mode 0, not 3**, entered at f661. First item: Tough Clothes, sell price 420, 1 owned.
  - A on the item → mode 8 (quantity / "Selling Tough Clothes. Are you sure?" Yes/No) at f703.
  - A on Yes → back to mode 0 at f749.
- **Step 3, an item was sold:** at f749 gold 9,999,999 → 10,000,419 (+420) and `items` 32 → 31. This is where step 3's predicate first holds.
- **Step 4, the Expansion confirm:** B back to mode 1 (f820–831), D D to cursor 3, A → **mode 4 at f869**. Text: "Expanding the store will cost 10000pix. Continue?" (Yes / No, cursor on Yes; I looked at f890).
  - A on Yes at about f890; the commit lands at **f901** and goes straight back to mode 1.
  - Gold 10,000,419 → 9,990,419 (−10,000), `items` 31 → 41, shop size `s32(bank+183776)` 0 → 1.
  - No cutscene or dialogue is triggered.
- **Leave:** D to Leave, A → town map (scene 8) at f948. Then L (cursor to dest 1, Market, which is disabled), D (dest 0, Recettear), A.
- **Step 5, back in the shop:** `scene == 1, cc08 == 1, dlg == 0` at **f1043**. The movie ends at f1100.
  - It was evening when I got back, so the game ends the day. Left running (not kept in the movie), the Daily Profits screen (scene 9) opens on its own at f1218. That is why I rewound to f1100.

**Why mode 3 can't happen:** in `all_ida.c`, `dword_9642C00` is only ever set to 0, 1, 2, 4, 5, 6 and 8 (lines 104348–105815). There is no `= 3` anywhere, and the switch at 105013 has no case 3. Buy and Sell share mode 0 → 8. The doc `merchant-guild-RE.md` mode map ("3 = sell item-pick") is wrong and should be corrected. The fix for the task is to change step 2 to `s32(0x9642c00) == 0 and s32(0x9640624 + 4*s32(0x9642c04)) == 1` (the item list opened from the Sell row). The rest of the run would then satisfy steps 3–5 at the frames above.

**Main option list** (count `DAT_005cfab4` = 6, types at `DAT_09640624[]`):
- Before: [0 Buy, 1 Sell, 3 Fusion, 6 Expansion, 2 Talk, 4 Leave]. Talk has a "New" badge.
- After the expansion: count 5, [0, 1, 3, 2, 4]. Expansion is removed (the commit sets the count to 0 and `sub_4918B0` rebuilds the list).

**Expansion code (all.c:105015–105039):** mode 4 runs `sub_434ED2(0)`.
- Yes: if gold ≥ `dword_5CFAD4[size]`, subtract that cost, play sound 333, set mode 1, shop size +1 (capped at 3), then call `sub_4864FD(bank)`.
- `sub_4864FD` is the function the task calls `FUN_004864fd`; in this run it took the displayed items back into the bag.
- Costs: `dword_5CFAD4` = {10000, 30000, 100000, …}.
- No: mode 1.

**Bank bytes the expansion changed** (`ram diff` across the Yes, bank = 0x44e3798):
- `+0x0c` gold: 10000419 → 9990419.
- `+0x2cde0` (183776) shop size: 0 → 1.
- `+0x2ce10` (183824): 3 → 4. Meaning unknown.
- `+0x2bb18` (`items`, dword 0xaec6): 31 → 41.
- `+0x94..+0xb8` (dwords 37–46): ten −1 slots filled with item words (96192, 6592, 192832, 192, 12928, 25728, 25792, 19328, 198786, 192128).
- `+0x138a0..+0x139e8` (0x44f7038–0x44f7180): the same ten words set to −1.
- So the ten items on display moved off the shelves into the bag.
- `+0x8` also changed (+31 over 31 frames); it is a frame counter, not part of the expansion.

**Shop afterwards** (picture at f1090): a larger room with new layout and empty shelves, evening lighting. Tear's "Thanks for all your help today, Tear!" bubble shows while `dlg` stays 0. The HUD reads 9,990,419 pix, Day 15, merchant level 14.

**Game facts learned:**
- The shop door zone for size 0 is within 1.8 of (3.0, −3.4), x > 2.895, facing π/2 (all.c:97285–97316). For shop size ≥ 1 the door position comes from a table.
- The map day value 14 is shown on the HUD as Day 15.

**Tool gaps:**
- There is no named variable for player facing (`f32(0x56db05c)`) or for the world-map cursor (`s32(0x9643684)`). Both would be worth adding.
- My door search failed (60 s) because the door is not in a bottom-right corner; reading the decompile found it. A note on the card with the door coordinates would help.
- `gmode` (0x9642c00) already shows up in event lines, but `ram list` did not list it.
