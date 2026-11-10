# Probe: tear-menu-pages

- **Run:** `tear-menu-pages` · **start:** save:1a5a45e631b4 · **task:** `tools/drive/tasks/tear-menu-pages.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 87 turns, $1.19, 10 min
- **Scenario:** `tests/scenarios/tear-menu-pages/` (13825 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/tear-menu-pages.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `tear-menu-pages`: all four pages opened, task check PASS, verify MATCH.** The movie is 13,825 frames, starting from save:1a5a45e631b4. `rd check` printed PASS and `rd verify` printed MATCH (fresh replay: rng d73efb46, sem 16cfbe1510d1fc4f). I looked at strips of the Merchant Level page (f275–455), the Store Atmosphere page and the lectures list (f510–650), the selling lecture (f723, f3783) and the Time Lecture event (f11200–13800).

**When each step passed (`rd check`):**
1. Tear's menu open (`cc08 == 15`): f227.
2. Merchant Level page open (`cc08 == 18`): f265.
3. Merchant Level page scrolled: f266. **This step passes before any input.** When the page opens, the game sets `b8c4 = 9` and `b8c8 = 8` itself (cursor on Level 10, the current level). My real scrolls came later: down from f276, by 5 from f285, back to 0/0 by f421. Changing the predicate to something like `b8c4 > 9` would make the step mean a real scroll.
4. Store Atmosphere page open (`cc08 == 17`, entry 0): f502.
5. Tear's Lectures open (`cc08 == 30`): f589.
6. Back in free roam (`cc08 == 1 and dlg == 0`): f10877, when the selling lecture ended. After that the movie reopens the menu and plays Time Lecture to its end, finishing in free roam at f13825.

**Route:** load `.*60 A .*30 A`. The morning dialogue opens at f138; `~` then `A` skips it, and `dlg` is 0 at f210. `U*4 R*2 A` opens the menu at f227. On this save list B is `[0,5,7,8,9,6,4]` (7 entries; index 3 is entry 8, index 4 is entry 9).

**Merchant Level page (`cc08 == 18`):**
- **Opening:** A, then `cc08 == 16` for 15 ticks, then 18. `b8c0` counts to 10 before the page takes input.
- **Variables:** `b8c4` = `s32(0x438b8c4)` is the cursor row (0..28). `b8c8` = `s32(0x438b8c8)` is the top visible row (0..24); 5 rows are shown.
- **Buttons:** D/U move the cursor 1 row and scroll at the edges. R/L move it 5 rows and scroll the top row by 5. Both are clamped to 28 and 24. A move plays sound 326 and moves the pointer.
- **Closing:** A or B both close it (`cc08` back to 15 in one tick, `b8c0` counts 10→0).
- **Text:** title 'Level Abilities' and rows like 'Level 1 Starting Level', 'Level 2 Customers Sell', 'Level 3 Can Take Orders', 'Level 4 More Items', 'Level 5 Fusion Rank 1', 'Level 9 Change Carpet', 'Level 10 Move Counters', 'Level 11 More Items'. Every row from 12 on reads '???'.
- **Levels listed:** row 28 is 'Level 50'. The last rows are 25, 26, 30, 40, 50.
- **Help box:**
  - Unlocked rows have a description (Level 10: "At this level, you may rearrange the counters in the store at will.").
  - Locked rows have no help text.
  - Row 0's help: "As you work in the store, your merchant level will rise ... Scroll down to see what each level allows."
- **Colours:** rows up to the current level are white, Level 10 is yellow with Recette's icon, rows above it are grey.

**Store Atmosphere page (`cc08 == 17`):**
- **What it is:** a chart titled "Store Atmosphere According To Tear", with axes Light/Dark and Gaudy/Plain and Recette's marker near the centre. `rd text` shows no rows except Tear's box, so it is drawn with sprites.
- **Buttons:** D/R/U/L change nothing (no `cc08`/`b8c4`/`b8c8` change, nothing in 0x438xxxx except timers).
- **Closing:** A or B (`(dword_73DDDD4 & 0x30)`) → `cc08 == 15`, sound 324. Case 17 in `sub_48670F` does only that.

**Tear's Lectures (`cc08 == 30`):**
- **What it is:** a submenu titled "Tear's Lectures" listing 'Selling Lecture', 'Buying Lecture' and 'Orders Lecture'. The cursor is `dword_438CC10` (D/U).
- **Opening a lecture:** A sets `b754`; 15 ticks later `cc08` becomes 4 and `sub_461BF6(cc10)` sets `DAT_005c6bb0` (the lecture index).
- **How a lecture runs:** it is a scripted practice sale run by `sub_461C00`.
  - **Script:** steps are 296-byte records at `0x5D1FC8 + 59200*lecture + 296*step` (+4 = step type; -1 ends it). The step index is `s32(0x730b604)`, the step tick `0x730b600`, the sub-state `0x730b608`.
  - **Pacing:** lines advance with A only after they finish typing, or with B held.
  - **Price entry:** at sub-state 3, U/D change a digit and L/R pick the digit; the price is `0x5c6bb8` and the base `0x5c6bbc` (Longsword, 1,200). A gives the "Okay!/Start Again" prompt (sub-state 4).
  - **Confirming:** an A there runs a 15-tick count in `0x730b590` and then accepts. My A taps every 9 ticks never got it accepted, so the movie sat at step 38 for a long time.
  - **Branches:** the step-type 5/12/13/14 branches judge the price. At step 39 a price ≤ base jumps back to try again. I sold at 1,400 (step 38), 1,500 (step 73) and haggled down to 1,300 (step 106).
- **End of the lecture:** at f10877 `cc08` goes straight to 1 (free roam), not back to the menu. Gold is unchanged at 9,086, and Recette is moved to (−1.5, 9).
- **There is no early way out:** the only extra button in a lecture, D ("Button 3: Item Details"), opens item details. You have to finish the lecture.

**Time Lecture (entry 6, index 5):**
- **What it starts:** A, then `cc08 16` for 12 ticks, then `cc08 = 1` with `dlg = 1` at f10945. It is an event dialogue (ESC = Event Skip available), not a page.
- **Content:** Tear explains the day clock (Morn/Noon/Eve/Night wheel) with the town map: "Holy cow, it's late! Time seemed to fly by today...", "Visiting a dungeon will consume two slices, no matter what.", "Exactly. Not only would you annoy your adventurer companions with...". It ends with the clock-wheel HUD.
- **End:** with A every 10 ticks it took about 2,880 ticks, ending at f13825 in free roam (`cc08 1`, `dlg 0`). `day` is unchanged (14; the HUD shows 15).

**Other facts:**
- The menu cursor `cc0c` is reset to 0 each time the menu opens.
- The Merchant Level page opens on the current level, so the crafted level-10 save looks consistent there: Level 10 is highlighted and abilities up to 10 are unlocked.

**Variables worth naming:**
- `tear_lvl_cur` = `s32(0x438b8c4)`
- `tear_lvl_top` = `s32(0x438b8c8)`
- `tear_lect_cur` = `s32(0x438cc10)`
- `lecture_id` = `s32(0x5c6bb0)`
- `lecture_step` = `s32(0x730b604)`
- `lecture_sub` = `s32(0x730b608)`
- `lecture_price` = `s32(0x5c6bb8)`, `lecture_base` = `s32(0x5c6bbc)`, `lecture_prev_price` = `s32(0x5c6bb4)`

**Tool gaps:**
- `rd tap` has no `--watch`.
- `rd eval` cannot take list comprehensions.
- `rd text` cannot show past frames, so a long dialogue's lines need a strip or stepped plays.
- One `rd strip` triggered an rhost rebuild (42 s).
