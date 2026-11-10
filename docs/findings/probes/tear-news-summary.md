# Probe: tear-news-summary

- **Task:** `tools/drive/tasks/tear-news-orders.md` · **start:** end:dungeon3-run2-save
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 32 turns, $0.51, 2 min
- **Scenario:** `tests/scenarios/tear-news-summary/`
- **Verdicts (2026-11-10):** check FAIL at step 4 (no entry 9 at merchant level 6 — the finding); verify --real MATCH; port triage IDENTICAL f1–577; portstate cc08 identical

The driver's report, as written (retail facts observed live through `rd`; treat code-read claims marked as such as leads):

---

Run `tear-news-orders`, started from end:dungeon3-run2-save, 277 frames. `rd check` gives FAIL: steps 1–3 pass and step 4 cannot be reached from this save. `rd verify` gives MATCH (frame 277, rng 0b9a35ea).

Step frames:
- Boot: `.*60 A .*30 A` (Continue, then the slot) puts me in the house at f137. The day dword is 14; the HUD shows Day 15.
- Getting in reach: Tear was at x 0.6, z 9.35 and the player at x -0.3, z 9.35. The search path `UR .*2 DR .` set `reach` (0x438be7c) to 20 at f147. Being close is not enough: the player also has to face her.
- Step 1, f148: `A` opened Tear's menu (cc08 15). The menu list is [0,5,7,8,6,4], count 6 = Store Atmosphere, Merchant Level, Tear's Lectures, News Summary, Time Lecture, Never Mind.
- Step 2, f191: three `D` presses put the cursor (cc0c 3) on entry 8.
- Step 3, f192: the checker counts this one frame after step 2 because the two predicates are identical. In the movie, `A` at f192 opened the page, it stayed open f193–f244, `B` at f244 closed it, and Tear's menu was back on entry 8 from f245.
- Step 4: impossible. Merchant level s32(0x450fb98) is 5 (the HUD shows 6). sub_4850FE adds entry 9 only if level >= 6, so it is not in the list. I did not try to level up by selling.
- End, f257: `B` from Tear's menu gives cc08 1, dlg 0. The checker never judges this step because step 4 failed.

The News Summary page in state terms (decompile FUN_0048670f cases 16 and 17, confirmed live):
- `A` on Tear's menu sets cc08 = 16. b754 counts 1..15, then the game looks up the entry under the cursor. Entries 0 (Store Atmosphere), 8 (News) and 9 (Orders) all go to cc08 = 17 (here at f208). So Outstanding Orders uses the same mode; only the drawing differs.
- In mode 17, b8c0 (0x438b8c0) counts up to 10 before any input is read. Back on the menu it counts down by one per frame.
- No scrolling or paging. Only A or B (input mask 0x30) does anything; both close the page to cc08 15 with b754 = 0. D, D, R and U pressed while it was open changed nothing: cc08, b8c0/b8c4/b8c8 and the text stayed the same.
- Text on screen: the title "News Summary" at (255,168), then from y 200: "The price of longswords has decreased.", "The new diet centered on Walnut Bread has taken off with housewives!" (wrapped over two rows), "The price of shields has decreased." Tear's "Yes? What is it?" stays drawn.
- Other modes: mode 18 is the Merchant Level page; it scrolls with b8c4/b8c8 by 1 or 5 rows, up to 28. Mode 30 is Tear's Lectures.

Strip: view/strip_186_262.png (f186–262). I looked at it: cursor on News Summary, the parchment panel with the three news items, the panel sliding out, Tear's menu back, then free roam. There is no strip of Outstanding Orders because it cannot be opened.

Game facts:
- reach is set to 20 when Tear is within 2 units and the player faces within about 0.2π of the direction to her. This needs flag 0x450f407 set and 0x450f3f7 == 0.
- Bank dwords 0x450fb90 / 0x450fb94 hold 1000 / 1350, possibly experience toward the next merchant level. Not checked.

Names defined in this run (worth sharing): tx f32 @ 0x56da1f0 and tz f32 @ 0x56da1f8 (Tear's position), reach s32 @ 0x438be7c, and tentry = s32(0x74b28b0 + 4*s32(0x438cc0c)) (the entry under Tear's menu cursor). Also worth naming: b8c0 at 0x438b8c0 (the page lockout counter).

Tool gaps:
- tools/save_arena_map.json could not be grepped, so I could not confirm which bank dword holds merchant experience.
- To test Outstanding Orders, this task needs a start save with merchant level >= 6 (HUD 7).
