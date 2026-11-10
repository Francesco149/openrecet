# Probe: vender-sales-l16

- **Run:** `vender-sales-l16` · **start:** vending-level16 · **task:** `tools/drive/tasks/vender-sales-l16.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 88 turns, $1.39, 10 min
- **Scenario:** `tests/scenarios/vender-sales-l16/` (9296 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/vender-sales-l16.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `vender-sales-l16`** (started from the scenario `vending-level16`, so my inputs begin at f5622): 9,296 frames. `rd check` gives **PASS** and `rd verify` gives **MATCH** (rng 79406c3c, sem 6b45c685d66a49a7). End save after the in-game Save: **sha 44e9baea3987** (`rd new NEXT --from save:44e9baea3987`).

**Result:** customers do buy from the machine. There were two vending sales in one session (noon, day dword 15 / HUD day 16), each paying exactly the item's base price. There is no dialogue or text box; the only visible sign is the HUD gold counting up.

**Step frames**
1. Machine in the shop: f5623.
2. Store open (`cc08 4`): f6111.
3. Money came in while the store was open: f7689, the first vending sale.
4. Session over (`cc08 4→1`, `dlg 0`): f8880. `stime` went 1→2: the time slot ran out.

**Stocking (f5622–5976)**
- `A` facing the machine opens its menu (`cc04 2`). Rows: Revert To Table, Done Stocking, 1:–6:.
- Picking a slot opens an item list with `L`/`R` categories and a "Nothing" row. The help line shows "Base Price" and "Number possessed".
- One `A` puts **one unit** in a slot. There is no quantity box.
- Once Food ran out, the list opened on "Spears" (Iron Spear, Practice Lance). So the machine takes non-food items too. I saw nothing refused, though I didn't try every category.
- Final stock: 1–3 Beef Bowl (base 2,300), 4 Candy (300), 5 Chocolate Bar (500), 6 Iron Spear (1,800). Slot 2's Beef Bowl was already in from the prefix.
- Tooltip text over the machine: "Old Vender" / "(n/6)", going (1/6)→(6/6).

**Stock layout**, from `ram mark/diff` of one stocking action:
- Bank dword **0xb1d4** (VA 0x450fee8) holds the machine's item code, 0x51440 (the prefix's `vend0`).
- The **6 slots are bank dwords 0xb1e8..0xb1ed** (VA 0x450ff38..0x450ff4c). Each holds one item code, or −1 when empty.
- The dwords between them (0xb1d5..0xb1e7) are 0. My guess is room for more machines; I did not test it.
- Stocking also removes the item from the inventory list at bank dword 0x1f.. (the later entries shift up and the last becomes −1) and lowers the count at dword 0xaec6 (35→34).
- Item codes: Beef Bowl 0x3eb00, Candy 0x3ee40, Chocolate Bar 0x3ee80, Iron Spear 0x6480.

**Vending sales**
- **f7689:** Candy from slot 4 (dword 0xb1eb 0x3ee40→−1). Gold 449,000→449,300 (+300 = 100% of base). The counter state did not move (`b534` stayed 0, `b528` stayed 3). It happened while Recette was saying "Well, I have an order!" after a counter order. At f7689 nobody was standing at the machine's front; customers were crowded on its left and right.
- **f8750:** Beef Bowl from slot 2 (0xb1e9→−1). Gold 452,330→454,630 (+2,300 = base). In the picture at f8752 a boy stands right at the machine's front (the side facing the counter, "CokeCola" panel) while the HUD counts up.
- Chocolate Bar and Iron Spear did not sell.
- Totals for the session: 6 counter customers (`b528` reached 6) plus at least 2 machine buyers, over 2,769 ticks.

**Counter customers** (`b56c`, `b5a8`)
- **f6161:** story event with Arma, skipped with `~` → Yes (`dlg` back to 0 at f6234).
- **C1** (14, request): wanted "some food". I had none left because it was all in the machine, so I picked "Nothing" → "We don't have anything…". No sale.
- **C2** (15, Big Shield, base 2,800): asked 2,800 → pushback, then 2,200 → pushback, then he walked out (`b534` 0xb).
- **C3** (2, kind 3 = order): "2 scarves in 3 days". I accepted it by mashing `A`; that order is now outstanding in the save.
- **C4** (15, buys Thief's Knife, base 3,400): his price `b574` was 4,114. I asked 3,570 (105%) and he took it: gold +3,570 at f8055.
- **C5** (11, kind 0, sells me a Cherry, base 900): his price was 315. I offered 540 and he took it: gold −540 at f8604.
- The session ended at f8880.

**Machine after the session** (f8963): tooltip "(4/6)". Rows: 1 Beef Bowl, 2 empty, 3 Beef Bowl, 4 empty, 5 Chocolate Bar, 6 Iron Spear. Empty slots stay where they are; nothing moves up.

**Save:** ESC, Save is `pm_sel` 4, slot 020 → Overwrite → Yes (play time 2:17:46→2:20:16), then `B` back to the shop.

**Pictures I looked at**
- `view/strip_7560_7700.png`: crowd around the machine up to sale 1.
- `view/f007689.png`: the moment of sale 1.
- `view/strip_8710_8770.png` and `view/f008752.png`: sale 2, with the boy at the machine front and the gold counting up.
- `view/f005664.png`: the stocking picker.

**State I named** (this run)
- `b534`, `b528`, `b5a8`, `b574`, `b56c`: the customer-service variables from `src/state_va.c`.
- `vstock`: how many of the 6 slot dwords at bank+0x2c7a0.. are not −1.
- I would share `vstock` and the per-slot dwords (`vslot1..6` = `s32(bank+4*(0xb1e8+i))`).

**Game facts**
- Vending sales run in the background during customer service, independently of the counter queue.
- They pay base price and take one unit from one slot.
- A request customer who wants a category you don't own can only be turned away with "Nothing".
- New kind: `b5a8 3` = an order request.

**Not done**
- I didn't run the afternoon session; there were already two sales.
- I didn't name customer actor positions. I don't know the table the shop customers live in, so the buyers are identified from pictures only.
- I didn't make a ram diff isolated to a sale tick, so other state that moves on a sale is still unknown.

**Tool gaps**
- `sum()` and generators aren't supported in `--expr`, so counting slots needed a long sum of comparisons.
- `rewind` triggered a 30-second session rebuild.
- There is no shared variable for the shop-customer actor table, so a "who walked to the machine" watch wasn't possible.
