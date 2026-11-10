# Probe: change-tables-l16

- **Run:** `change-tables-l16` · **start:** save:2afe7fb42617 · **task:** `tools/drive/tasks/change-tables-l16.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 92 turns, $1.46, 10 min
- **Scenario:** `tests/scenarios/change-tables-l16/` (2149 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/change-tables-l16.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

I finished the run `change-tables-l16`: 2,149 frames. `rd check` gives PASS and `rd verify` gives MATCH (frame 2149, rng ab9200ba, sem 20421e485c0118d1). The end save is dea37b7a7519. I looked at real-device pictures: f562 (the editor before buying), f1550 (Legendary preview), f1590 (Barrel preview) and f2149 (the shop at the end).

Main finding: Change Tables restyles every table at once. You cannot pick one table. It is the same Counters editor as the walls, floor and carpet editors, and the whole shop is stored in one bank dword.

**Step frames**
1. In the shop with control: `check` judges it at f137, when the save loads. Real control comes at f410.
2. At the Market: f865.
3. A counter was bought: f1097.
4. Back in the shop at the end: f1749. The movie then saves in-game and ends in free roam at f2149 (`scene 1`, `cc08 1`, `dlg 0`).

**Timeline**
- Both events skip with `~`, wait ~45 ticks, `A` on Yes. The skip box ("Do you want to skip this event?" / Yes / No) appears both times.
  - f312: the level-up event ends (`dlg` 0 for one tick).
  - f410: the vending tutorial is skipped.
  - My first `~ .*45 A` at f139 did not close the event. The second `~` brought up the box and `A` worked.
- Counter: from the start spot, `L*2 A` opens the counter menu (`cc08` 0). The old route walked into Tear, who now stands at (1.09, 8.25).
- Change Tables is cursor `cc0c` 5 (`centry` 4), out of 7 entries.
- Editor opened before owning a counter: f531–548.
- Town map and Market:
  - Door to town map at f791.
  - On the map, `L` and `R` moved `wm_cur` 0→1→3→2 and then 2→3→1. Market = 1; entry 2 is a different place (`scene` 14).
  - Bought Legendary at f1098 and Barrel at f1148.
  - Left at f1203 and was home at f1252. The trip cost one time period (`stime` 0→1).
- Editor, second time (counter menu reached at f1473):
  - Opened at f1527, previewed, cancelled with `B` at f1594.
  - Reopened and confirmed Legendary at f1666.
  - Reopened and confirmed Barrel at f1735.
- After: opened the centre table's display menu at f1810, then saved in-game. The overwrite was confirmed at about f2026 on slot 020.

**1. Change Tables before owning a new counter**
- The editor opens anyway, with no message.
- Window title "Counters", one row "Wooden Counter".
- Help text: "A simple, well-built, sturdy wood counter. Isn't simplicity of function the first virtue of furniture?", "Base Price- 500", "Number possessed- 1", "Button 3: Item Details".
- Opening it puts the current kind (Wooden) into the bag (`items` 32→33); `B` takes it back out.

**2. Market Counters tab** (purchase price = base × 0.7)
- Wooden Counter: 350 (base 500), unlimited stock. Not bought: it is the default.
- Legendary Counter: 70 (base 100), unlimited stock. Bought.
- Barrel Counter: 350 (base 500), unlimited stock. Bought.
- Draped Counter: 1,050, 1 Left. Not bought.
- Heretical Table: Out Of Stock.

I bought the two cheapest new kinds, Legendary and Barrel; Wooden is also 350 but is the kind the shop already has. `gold` went 470000 → 469930 → 469580. The buy box reads "Buying Legendary Counter. Are you sure?", quantity 1, "Stock Price 70pix", Yes / No. Wooden showed "Number possessed- 0" at the Market, so the shop's current kind is not counted.

Where they land: bag dwords at 0x44e3830 onward (bank dword 0x26), as id<<6. Legendary 0x52d80 (id 5302) went in slot 0, Barrel 0x52e00 (id 5304) in slot 1. Other changes per buy:
- 0x450f2b0 (the bag count) 32→33→34.
- 0x450fb78 0→3→6.
- 0x450ba30 1 → 0x101 → 0x1000101, which looks like "seen" flag bytes.
- The `items` variable read 33→34 at the same time.

**3. The editor**
- States: in the counter menu, `A` makes `cc08` 10 for 15 ticks, then 23 for one tick (0x17), then 3 (editing). `cc04` stays 0.
- What it offers: one list of the kinds you own: Wooden, Legendary, Barrel (`iw_n` 3). The cursor opens on the shop's current kind. There is no per-table choice and no second step.
- Buttons:
  - `U` / `D` move the row and do not wrap. The preview is live: the table-kind dword follows the row on the next tick.
  - `A` confirms. It returns to the counter list 7 ticks later (`cc08` 3→0), with no dialogue and no cost.
  - `B` cancels at once (next tick `cc08` 0). It restores the table kind and takes the added default back out of the bag.
  - `B` in the list goes to free roam (`cc08` 1).
- The table kind is bank dword 0xb37c (VA 0x4510588), which I named `d_table` (= bank+0x2cdf0): 0 Wooden, 1 Legendary, 3 Barrel (id − 5301). It was the only bank dword that changed during a preview.
- Cancel test: I previewed Legendary (`d_table` 1) and Barrel (3), then pressed `B`. Everything went back: `d_table` 0, and the Wooden entry left the bag.
- Confirm diffs:
  - Legendary: `d_table` 0→1. Legendary leaves the bag and Wooden stays in it, giving a bag of [Barrel, Wooden].
  - Barrel: 1→3. The bag becomes [Wooden, Legendary] and 0x450f2b0 goes 35→34.
  - So the replaced kind always stays in the bag, and you can switch back to it later.
- Pictures:
  - Legendary turns every table into cardboard boxes.
  - Barrel turns every table into planks on barrels.
  - The camera pulls toward the tables while the editor is open.

**4. A changed table works as before**
- The display-grid cells keep their items: 63, 64, 83, 84 hold 0x6480, 0x64c0, 0x30882, 0x2ee80, and cells 2 and 20 are unchanged.
- The table list is unchanged: count 3, types 3 / 4 / 4 at 0x438bfcc.
- The display menu over the Barrel centre table (`cc04` 1) is unchanged: label "Thankful Statue", tab "Clothes", rows "Nothing", "Tough Clothes".
- New: the display menu has a "Counters" tab listing Nothing, Wooden Counter, Legendary Counter. So the counters you own, including the replaced one, can be put on display like goods; I did not place one.

**5. Save**
- In the pause menu, Save is index 4 (the type list is 0, 1, 6, 2, 3, 4, 6).
- The picker opens on slot 020 and asks "Overwriting file. Are you sure?"; `A` saves (TIME went 2:17:46 → 2:18:18).
- `rd endsave` gave dea37b7a7519.

**Names (this run's vars)**
- `d_table` = s32(bank+0x2cdf0)
- `iw_tab` = 0x734b968
- `iw_row` = s32(0x7337850 + 4*iw_tab)
- `iw_n` = s32(0x7337210 + 4*iw_tab), the row count
- `iw_id` = the item id under the cursor

The `iw_*` names also work for the Market's buy list. They are worth adding to the shared `vars.json`.

**Item ids:** 5301 Wooden, 5302 Legendary, 5304 Barrel. 5303 is the School Counter, which a crafted save in the decor-variants probe offered as a table kind.

**Tool gaps**
- Pause menu: `pm_type` for an index is not the visible order, so finding Save took a `dwords` read.
- Picture over text: `rd look` without `--at` replays to show one frame. Several `--at` frames in one call worked well.
- `B` in free roam moves Recette: her x changed after a `B` that was meant to close a menu. It did no harm here, but it is easy to miss.
