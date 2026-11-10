# Probe: payday-week2-paid

- **Run:** `payday-week2` · **start:** save:4c1259b71fd7 · **task:** `tools/drive/tasks/payday-week2.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 68 turns, $0.82, 7 min
- **Scenario:** `tests/scenarios/payday-week2-paid/` (6383 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/payday-week2.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

The week-2 payment night is recorded and checked: `rd check` PASSes and `rd verify` MATCHes. The save made at the end has sha **64d43650865f**, so later tasks can start in week 3 with `rd new NEXT --from save:64d43650865f`.

**Run:** `payday-week2`, from save:4c1259b71fd7, 6383 frames.

**Step frames (from `rd check`):**
1. In the shop on day 14 (HUD "Day 15"): **f73**.
2. The day rolled over (day == 15): **f1948**, when I pressed A on the Daily Profits screen.
3. The payment was taken (gold < 480000): **f2195**.
4. Free roam on the new day: **f3538**. That is the first frame back in the shop, before the morning scenes. Control comes back after them at f5688, and the run ends there in free roam.

**Night sequence (scene / cc08 / dlg):**
- **No morning event at load.** The load put me in the shop at f73 (1/1/0) and no event played, so no Event Skip was needed. The shop-time counter `stime` was already 3, so the day had one time period left.
- **One town trip ended the day:**
  - f505: I walked out the shop door to the town map (scene 8). It says "If you return now no time will pass".
  - f573: Town Square, scene 14. A dialogue event ran (dlg 1 from f574 to f1390) and I did not read its text.
  - f1407: back on the map.
  - f1467: I chose Recettear to go home. That frame scene goes to 1, `stime` 3→4, `b4e0` goes to 3 (night exchange running) and `b928` = 2.
- **Night exchange:** `b4e0` counts up to 160 with scene 1, cc08 1, dlg 0 throughout.
- **Daily Profits summary:** scene 9 from f1626. It reads "Day 15", Profits +493082 (inflated by the crafted gold), Score Difference 5192(+650). Sold: Shrimp Doria, Magician's Staff, Longsword, Candy, Sturdy Glove. Bought: Candy, Chocolate Bar, Beef Bowl, Thankful Statue.
  - My A at f1826 did nothing; the A at f1947 set day 15 at f1948.
- **Payment screen:** still scene 9, dlg 0. It is TERME Finance's "COLLECTION FORM", reading "WEEK #2 DEBT REPAYMENT 30000pix".
  - Gold counts down from f2023 (500000→499921) and settles at 470000 by about f2225.
  - A red "PAID" stamp and confetti follow (stamp visible by f2335).
- **Amount paid:** 30,000 pix (gold 500,000 before, 470,000 after).
- **Verdict script:** A at f2496 starts the dialogue (dlg 1 from about f2500, still scene 9). Lines I read:
  - "So, this is the payment for this week. Let me see..."
  - "Congratulations. Well done."
  - "I have officially received your payment for this week."
  - "Let us continue to work hard next week, as well!"
  - Recette: "YAY! I did it!"
  - "I'm counting on you next week too, Tear!"
  - dlg goes to 0 at f3522.
- **Into the morning:** scene 1 at f3538 with `stime` 0, cc08 1 and `b928` 1. The "Day 16" card shows around f3650, then the shop.

**Morning scripted scene (day 15, HUD "Day 16"):** one event.
- dlg 1 from f3845 to f5688, with "ESC Key: Event Skip" on screen.
- Recette: "Alright! Gonna work hard today, too! Capitalism, ho!"
- Then: "Huh? Hey, what's that girl doing outside the shop?" / "..." / "She's, uh... just kind of staring into the shop." / … "Aww".
- A green-haired, armored, horned girl looks in through the window (the strip shows her). She is probably Arma, but I didn't confirm that.
- I tapped through the rest of the lines without reading them.
- No further scene in the next 400 frames (to f6088).

**Save:** ESC opens the pause menu at f6092. The entries in order are pm_sel 0 (type 0), 1 Items, 2 Encyclopedia (type 6), 3 Options, 4 Save, 5 (type 4). In the Save page the slot cursor starts on the last used slot, 019. I pressed D and A to save to slot 020 (it was NO-DATA; it now shows score 5192, time 2:17:46). The save had no confirmation prompt. B closed the menu at f6332, then `rd endsave`.

**Strips I looked at:**
- `view/strip_1960_2560.png`: the summary, the countdown, then PAID.
- `view/strip_3540_4400.png`: the Day 16 card, the morning event and the girl at the window.

**State I named (this run only):** `stime` (s32 at 0x450fb88, shop-time periods used today), `b4e0` (s32 at 0x438b4e0, the night exchange counter), `b928` (s32 at 0x438b928, the morning beat). I suggest adding `stime` and `b4e0` to the shared vars.

**Game facts:**
- The cheapest way to end the day here was one town trip. The start save already has 3 of 4 periods used, so a single Town Square visit ends the day.
- Town Square is scene 14 and the town map is scene 8.
- The week-2 payment is 30,000 pix.
- The payment screen draws nothing through the text functions; it is all sprites.

**What the tool lacked:**
- No simple way to wait for the next fully shown dialogue line. `line`, `reveal` and `revdone` keep the old line's values across an A press, and an A press while a line closes is ignored, so I had to add frame guards by hand. A `rd tap --log-text` that records every line as it advances would have captured the whole event.
- One `rd play` with `--watch` and `--every` printed nothing and did not advance the run. The same input without `--watch` then worked.
