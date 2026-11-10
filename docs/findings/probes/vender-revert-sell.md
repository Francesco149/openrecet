# Probe: vender-revert-sell

- **Run:** `vender-revert-sell` · **start:** save:44e9baea3987 · **task:** `tools/drive/tasks/vender-revert-sell.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 54 turns, $0.71, 5 min
- **Scenario:** `tests/scenarios/vender-revert-sell/` (1569 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/vender-revert-sell.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

Run `vender-revert-sell` (start save:44e9baea3987): 1,569 frames. `rd check tools/drive/tasks/vender-revert-sell.md` gives **PASS**. `rd verify` gives **MATCH** (rng 835e84a0, sem e173d4379ffedb2c). The end save after the in-game Save is **435d876bf8c8** (`rd new NEXT --from save:435d876bf8c8`).

**The answer in short:** "Revert To Table" works at once, with no prompt and no confirm box, stocked or not. The stocked items and the machine itself go back into the inventory, and the centre table comes back empty. The Merchants' Guild buys the machine back for **9,000 pix** (30% of the 30,000 base; I had paid 21,000). Afterwards the shop's table menu no longer has a "Venders" category.

**Step frames (from `rd check`)**
1. Loaded with control: f137 (load inputs `.*60 A .*30 A`).
2. Machine menu open (`cc04 2`): f257, found by search.
3. Machine gone from the shop: f301. The revert itself was at about f296; the step also needs `frame > 300`.
4. At the Merchants' Guild (`scene 6`): f897.
5. Back in the shop at the end: f1539.

**(1) Revert with stock (f277→f326)**
- The menu opened with the cursor on slot "1:". Rows: Revert To Table, Done Stocking, 1:–6:, with the tooltip "Old Vender (4/6)".
- Inputs `U .*8 U .*8 A`. The menu closed with no text and no dialogue, `cc04` went back to 0, and Recette was nudged off the machine (her `px`/`pz` moved slightly).
- Bank diff (`--lo 0x44e3798 --hi 0x4511760`), 13 dwords:
  - dword 0x02: 0x7b529 → 0x7b55a. It went up by exactly the 49 ticks of the action, so it is very likely the play-time counter (noise).
  - dwords 0x25..0x29, the inventory list (it starts at dword 0x1f): −1 → 0x51440 (the vender), 0x3eb00, 0x3eb00 (Beef Bowl ×2), 0x3ee80 (Chocolate Bar), 0x6480 (Iron Spear). They were appended to the end of the list.
  - dword 0x4e66 (VA 0x44f7130), the table cell that held the machine: 0x51440 → −1.
  - dword 0xaec6 (`items`): 31 → 36, i.e. 4 stocked items plus the machine.
  - dword 0xb1d4 (machine code): 0x51440 → **0** (not −1).
  - dwords 0xb1e8, 0xb1ea, 0xb1ec, 0xb1ed: their item codes → −1. 0xb1e9 and 0xb1eb were already −1.
- The strip (`view/strip_280_326.png`) shows the menu closing at about f296 and a plain, empty 4-cell centre table from f300.
- The machine is back in the bag under the "Venders" category of the table's display menu. In the display menu the categories run …Food → Venders → Spears.

**(2) Place again and revert without stock (f352–f698)**
- `A` facing the table opens the display menu (`cc04 1`). From the Spears page `L` reaches "Venders"; `D` then `A` on "Old Vender" places it straight away, with no prompt. Tooltip "(0/6)", `items` 36 → 35.
- The first `A` right after placing did not open the machine's menu; one a few ticks later did (`cc04 2`).
- Revert with the same `U U A`: again no prompt. Bank diff, 5 dwords:
  - dword 0x02: the play-time counter again.
  - dword 0x29: −1 → 0x51440 (the machine back in the inventory list).
  - dword **0x4e7a** (VA 0x44f7180): 0x51440 → −1. This is a different cell from the first revert's 0x4e66. My guess is the machine's cell depends on which table cell you place it from; I did not check this.
  - `items`: 35 → 36.
  - dword 0xb1d4: 0x51440 → 0.

**(3) Selling the machine**
- Getting there: door → town map (`scene 8`), `wm_cur` 3 = Merchants' Guild (`R` from 0), `A`.
- Guild menu rows: Buy, Sell, Fusion, Expansion, New, Talk, Leave.
- Sell → the list opens on Spears ("Sell Price- 540" for the Iron Spear); `L` reaches **"Venders"**: "Old Vender", "An old, refurbished vending machine. Still usable, although its cooling unit cools the entire SHOP.", "**Sell Price- 9,000**", "Number possessed- 1". Picture: `view/f001001.png`.
- `A` opens the prompt "Selling 1 Old Vender. Are you sure?" / "Purchase Price 9,000pix" / Yes, No (`view/f001040.png`). `A` on Yes sells it with no further text.
- Gold went **454,630 → 463,630**, `items` 36 → 35, and the Venders category stayed in the list but empty.
- I did not try the Market: the Guild buys it back.
- Going out cost no time period (`stime` stayed 2). Getting home moved `stime` 2 → 3 (evening).

**(4) Back home (f1202)**
- The table's display menu goes Spears → Food → Ingredients. The **"Venders" category is gone** entirely.
- The returned Beef Bowl ×2, Chocolate Bar and Iron Spear are listed again.

**Save**
- ESC, then `U U` to `pm_type 3` (Save), `A`, slot 020 → "Overwriting file. Are you sure?" → Yes. Play time went 2:20:16 → 2:20:38.
- `B` back to the shop, then `rd endsave` (sha above).

**Mistake I undid:** my first `A` taps at the guild went into Buy and bought 2 Worn Swords at 112 each. I undid that with `rd undo`, so it is not in the movie.

**State I named:** nothing new. I used the bank offsets above:
- 0xb1d4: machine code, 0 when no machine is placed.
- 0xb1e8..0xb1ed: the 6 stock slots.
- 0x4e66 / 0x4e7a: table cells.
- 0x1f..: the inventory list.

**Tool gaps**
- `rd search` (and anchors) silently drop a `ram mark`. I had to `undo`, mark again and redo the action to get the second diff. A warning, or keeping the mark across a search, would help.
- There is no variable for the display menu's category, so finding "Venders" took a `tap --text` scan.
- There is no variable for "facing the machine or table", so I used searches for `cc04`.
