# Probe: venders-limit

- **Run:** `venders-limit` · **start:** save:0703ebb339bf · **task:** `tools/drive/tasks/venders-limit.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 120 turns, $2.21, 16 min
- **Scenario:** `tests/scenarios/venders-limit/` (9043 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/venders-limit.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     venders-limit                        f2793 FUN_0045edaa ⇄ customer_service_session_init (+101)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `venders-limit`**: 9,043 frames, starting from save:0703ebb339bf. `rd check` gives PASS and `rd verify` gives MATCH (rng 84a71170, sem 29cf45b87acde0f4). The end save is **c94e0bb56b91**. The limit refusal was reproduced in retail; until now it was known from the asm only. The new machines sold nothing (see Sales).

**Step frames (from check)**
- Step 1, in the shop with control: f137.
- Step 2, two tables hold a vender: f1185.
- Step 3, three tables hold a vender: f1695.

**Validation:** on the loaded save, table 0's handle `u32(0x450fee8)` read 0x51440 (the Old Vender). The shop had `s32(0x438bfb4)` = 3 tables and merchant level dword 15.

**Timeline**
- **Shop expansion first:** walked out the door (town map, scene 8, at f491). Merchant's Guild at f550. Expansion is option 4 of [Buy, Sell, Fusion, Expansion, Talk, Leave]. The prompt read "Expanding the store will cost 10000pix. Continue?". Yes committed at f680:
  - gold 454,630 → 444,630, shop size 0 → 1, `0x438bfb4` 3 → 4 tables.
  - `items` went 35 → 45.
  - **The expansion (sub_4864FD) also reverts every vending machine.** Table 0's handle went 0x51440 → 0, and the Old Vender and its stock went back into the bag. This is shown in the decompile too: it zeroes every handle and empties every stock slot. I did the expansion first for that reason.
- **Back home at f786, evening (stime 3).** Free roam continues; the day does not end until a period is used up. The new shop has 4 tables:
  - table 0: the 2×2 in the middle;
  - table 1: the 1×4 on the back wall;
  - table 2: the 1×4 on the left wall;
  - table 3: the 1×4 on the right wall, by the door.
- **f928, SuperVend on table 3:** placed from the table's display menu (`cc04 1`, `L` to "Venders"). Handle 0x514c0, cell (9,1). Its menu reads "SuperVend SDXK56 (0/10)" with last row 11. Stocked Steel Sword in slot 1.
- **f1185, "Vending Machine" on table 0:** handle 0x51480, cell (5,3). Its menu reads "(0/8)". Stocked Cherry and Beef Bowl.
- **f1695, Old Vender on table 2:** handle 0x51440, cell (0,1). Its menu reads "(0/6)". Stocked Chocolate Bar and Beef Bowl.
- **Tear gets in the way:** in the expanded shop she follows Recette. Twice an `A` aimed at a table opened Tear's menu instead (`cc08 15`) when she was about 2.6 away. I avoided this with a search that had `cc08 == 15` in avoid.

**The three vender kinds**

| Item | Name | Handle | Base price | Slots | Description |
|---|---|---|---|---|---|
| 5201 | "Old Vender" | 0x51440 | 30,000 | 6 | "An old, refurbished vending machine. Still usable, although its cooling unit cools the entire SHOP." |
| 5202 | "Vending Machine" | 0x51480 | 50,000 | 8 | "A standard vending machine that can sell things to people automatically. Isn't technology grand?" |
| 5203 | "SuperVend SDXK56" | 0x514c0 | 120,000 | 10 | "A super-efficient, high-yield vender that can even act as a slot machine. Tear disabled THAT feature..." |

- Only the slot count and the look differ. The menu rows are the same for all three: Revert To Table, Done Stocking, then the slots. The tooltip shows "name (n/cap)" with caps 6 / 8 / 10, as the asm says.
- What they look like (strip f2330/f2350):
  - Old Vender: red and green, on the left wall.
  - Vending Machine: blue, on the middle 2×2.
  - SuperVend: red and blue, on the right wall.
- Placing one is accepted on a 1×4 table as well as on the 2×2.

**The 4th machine (checkpoint `pre4` f2076 → press at f2077)**
- Setup: facing back-wall table 1 (cell (4,0)), cursor on "Old Vender x2", then `A`.
- `cc04` goes 1 → 0 at f2083. A modal box shows **"We are at our level's vending machine limit."** at (88,212).
- The box is FUN_00434ceb. Its state `msgbox` = `s32(0x438b148)` ramps up to 8 and stays there: it was still up 240 ticks later. A or B closes it (sub_434D6A; the decompile says it plays SE 323 = 0x143). It then ramps 8 → 0 over f2329..2336.
- **Nothing changed:**
  - A `ram diff` of the whole bank across the attempt shows only the play-time counter at bank+8.
  - Table 1's handle stays 0, `items` stays 37, and the list still shows Old Vender x2.
  - `nvend` stays 3, and `dlg` stays 0 throughout.
- I did not observe sounds. By the asm, SE 05a/05b plays on the confirm.

**Sales: none from the machines**
I opened the store three times, mashing `A` for the counter:

| Session | Frames | Result |
|---|---|---|
| Evening, day dword 15 | f2793–6202 | Event script 5001 held the game (`dlg 1`) from f2843 to f5544. One counter sale: Magician's Staff, +1,200 at f5935. |
| Morning, day 16 | f7069–8058 | No customers' gold moved. |
| Noon | f8116–8691 | Nothing. |

- No machine slot changed in any session (named var `vs` stayed 11111).
- My best explanation: the only non-machine table was empty, so few or no customers came, and machine sales need flagged customers at the counter. A useful follow-up is to fill table 1 and run a full session from save c94e0bb56b91.
- Daily Profits for day 16 listed sold: Candy, Thief's Knife, Beef Bowl, Magician's Staff (that day's earlier sales came from the prefix save); bought: Cherry; profits −5,373.

**Save:** ESC, Save (pause entry index 4, type 3), slot 020, Overwrite, Yes, at about f8930. The slot's play time went 2:20:16 → 2:22:43 and its score 5192 → 5271. Back in the shop at f9043. That save was filed with `rd endsave`.

**State I named (this run)**
- `nvend`: machines on tables 0..3.
- `msgbox` = `s32(0x438b148)`: the FUN_00434ceb message box, 0..8, closed with A or B. Worth sharing.
- `cbfc` / `cc00` = `0x438cbfc` / `0x438cc00`: the faced display cell. Worth sharing.
- `vs`: occupancy of the stocked slots, as digits.

**Pictures:** `view/strip_2330_2350.png`. f2330 shows the limit box over the expanded shop with all three machines; f2350 shows it closed. Also `view/f000827.png`, the empty expanded shop.

**Tool gaps**
- `hex()` is not available in eval.
- `search --apply-best` without `--apply` printed "not applied" on a success.
- The display item list has no state var, so finding the "Venders" category cost a `rd text` after every `L`/`R`.
- There is no var for Tear's talk reach in the expanded shop: `reach` read 7 or 18 while `A` still opened her menu.
- A counter search failed for no clear reason; walking there by hand (`L*12 A`) worked.
