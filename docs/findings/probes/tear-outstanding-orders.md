# Probe: tear-outstanding-orders

- **Task:** `tools/drive/tasks/tear-orders-l10.md` · **start:** save:1a5a45e6 (crafted: level dword 5 → 9)
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 44 turns, $0.50, 4 min
- **Scenario:** `tests/scenarios/tear-outstanding-orders/`
- **Verdicts (2026-11-10):** check PASS; verify MATCH; port triage IDENTICAL f1–1982

The driver's report, as written (retail facts observed live through `rd`; treat code-read claims marked as such as leads):

---

Run **tear-orders-l10** (from save:1a5a45e631b4) is 1682 frames long. `rd check` gives PASS on all 5 steps and `rd verify` gives MATCH. I looked at a strip of frames 1500/1550/1600: the News Summary page, Tear's menu with the cursor on Outstanding Orders, and the Advance Order Summary page. One finding up front: this save has no outstanding orders, so the Outstanding Orders page opens but shows only its headers.

**Step frames**
1. Tear's menu open (cc08 15): f1435. Tear's dialogue was opened by the talk `A` at f1436, and cc08 changed 1→15 at f1437.
2. Cursor on News Summary: f1464.
3. Back on that entry after the page: f1524. The page opened f1465–1523 and was closed with B.
4. Cursor on Outstanding Orders (entry 9): f1555. The page opened at f1560 and was closed with A at f1631.
5. Free roam (cc08 1, dlg 0): f1662.

**How the run went**
- I booted, then held `A` through a long morning event: the "Recette. May I have a moment?" dialogue, which ended at f1417.
- In the morning Recette stands in front of the counter at (-0.3, 9.35), with Tear at (0.6, 9.35). `rd search` found the inputs `U*4 R*2 A`. `be7c` (= `s32(0x438be7c)`) became 20 at about f1434 and the talk opened the menu.
- I did not have to open the shop first. Walking around the counter in the morning was enough: the be7c gate (`v0[179311]!=0 && v0[179295]==0`, all_ida.c:97339) is already met at that point. That doesn't match PROGRESS's "once the shop is open", so the condition is wider than that.

**Tear's menu (list B)**
- The menu shows "Yes? What is it?". Its list is `DAT_074b28b0[]` = [0, 5, 7, 8, 9, 6, 4], count `DAT_005cd0c0` = 7: Store Atmosphere, Merchant Level, Tear's Lectures, News Summary, Outstanding Orders, Time Lecture, Never Mind.
- Entry 9 is present because merchant level `0x450fb98` = 9.
- The cursor (`cc0c` = `s32(0x438cc0c)`) is 3 on News Summary and 4 on Outstanding Orders.

**What a page is in state terms (both pages behave the same way)**
- **Opening:** `A` sets cc08 to 16 for 15 ticks, then 17 (the page). `cc0c` stays on the entry, and `b8c0` (`s32(0x438b8c0)`) counts 0→10 at about +1 per tick.
  - The drawing code puts the panel's x at `(b8c0<<6) − 640`, so it slides in from the left.
  - Retail drawing code FUN_0040a765 picks the page from the list entry: `v21 == 8` for News, `v21 == 9` for Orders. It draws for cc08 15, 16 or 17.
  - `b8c4` and `b8c8` stay at 0.
  - The ram diff while the Orders page opened shows only cc08, `b8c0`, the tick counters, `b74c` 12→0 and `b754` 0→15. Nothing else moves.
- **Input while open:** U, D, L, R and X do nothing — no scrolling or paging on either page, at least with this little content. Either `A` or `B` closes it: cc08 goes straight back to 15 and `b8c0` counts 10→0 in about 10 ticks, so the panel slides out.
- **Closing the menu:** `B` in cc08 15 returns to cc08 1 in one tick. Afterwards Recette's px drifted from -0.04 to 0.67 with no input, probably the end of the talk pose.

**News Summary**
- Title "News Summary" with three news lines: "The price of longswords has decreased.", "The new diet centered on Walnut Bread has taken off with housewives!", "The price of shields has decreased.".

**Outstanding Orders**
- **What is drawn:** a taller panel titled "Advance Order Summary", with green column headers Deadline, Orderee and Order Details. There are no rows, because there are no orders. The text is not the News Summary path: it is a separate branch at all_ida.c:7642–7738.
- **Where the orders live:** 10 order slots of 20 bytes each, starting at bank+173560. In this run that is 0x450dd90–0x450de58, and it is all zero. The fields are relative to bank+173568+20·i:
  - −8: active (== 1)
  - −4: item id (name from 0x6A5DB98 + 76·id + 32)
  - 0: days left (<0 "Due date passed" in red, 0 "Today", 1 "By tomorrow", else "in %d days")
  - +6: s16 orderee id (name from 0x6A5EA94 + 181872·id)
  - +8: quantity ("%d %s"; "Cold Weather Clothes" gets a special two-line layout)
- **Caillou row:** a hard-coded row shows when the bank byte at +179401 != 0 and +179403 == 0: "No deadline / Caillou / Charred Lizard Bone, Slime Liver". Both bytes are 0 here.
- The bank pointer is `0x44E3798 + 188360·DAT_0438B1E0`; `b1e0` = 0 here.
- To see filled rows, a later task needs a save with an accepted order, or the Caillou flag set.

**Level-10 consistency:** the HUD shows Merchant Level 10 and the menu offers entry 9. Nothing I saw looked inconsistent with a real level 10, but I only checked these menus.

**Names worth sharing:** `be7c` = `s32(0x438be7c)` (Tear in reach), `b8c0` (page slide timer 0..10), `orders_base` = bank+173560 (stride 20, 10 slots), Caillou flags at bank+179401 / bank+179403.

**Tool gaps:**
- `rd eval` has no list comprehension or loop, so reading the 10 slots took `ram read`.
- The morning dialogue before free roam cost about 1100 ticks of `A` taps.
