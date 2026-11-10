# Probe: shop-session-week3

- **Run:** `shop-session-week3` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/shop-session-week3.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 159 turns, $2.90, 19 min
- **Scenario:** `tests/scenarios/shop-session-week3/` (8990 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/shop-session-week3.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

Run shop-session-week3 (from save:64d43650865f), 8990 frames. rd check: PASS. rd verify: MATCH (f8990, rng efdbe670). End save after the in-game Save: sha fe0c2efd7af8 (rd new NEXT --from save:fe0c2efd7af8). Two sessions on day 16: morning (stime 0->1) and noon (1->2).

Movie rebuilt mid-run: I loaded checkpoint 'open' (f244) to read merchant exp. That cut the movie to 244 and deleted every later checkpoint. I rebuilt it from my logged inputs, turning each --until stop into a fixed tick count. The rng matched the original at f3524, f4611, f6872 and f8673.

Task steps:
1. In the shop with control: f137. Load with `.*60 A .*30 A`; dlg stays 0 (no level-up event).
2. Store open: f244. Walk `.*30 DL*16 U*8 UR*16 L*6 A`; the counter menu is cc08==0 (not 10); A on "Open Store" (centry 0).
3. First deal closed: f3714 (gold 470,000 -> 475,920).
4. Session over, free roam: f4581 (cc08 4->1).

Session 1 (morning):
- f294-3011, story event: two customers walk in; a dialogue event that offers "ESC Key: Event Skip". I tapped through with A (2,677 ticks). It used up most of the morning, so only two regular customers came.
- C1 f3112-3841: b56c 19, b5a8 1 (request, "a treasure"). Picked Thankful Statue (base 4,550; b574 6779). Asked 5,920 (130%), accepted at once. +5,920 at f3714.
- C2 f3992-4431: b56c 15 "Old Man", b5a8 2 (sale of a showcase item), Big Shield ("Low Price 2,800"; b574 1963).
  - Round 1: 2,940 (105%) -> pushback (b534 8, "It's that expensive?").
  - Round 2: his b574 rose to 2138; I asked 2,940 again -> b534 0xb, he walked out (no sale). This is the walk-out case.
- Session ended f4581 by the time slot running out (b528 3).

Session 2 (noon): reopened at f4617; cc08 4 at f5042.
- C3 f5193: b56c 9, b5a8 2, Longbow (base 1,400; b574 1904). Asked 1,470 (105%), accepted first round. +1,470 at f5396.
- C4 f5655: b56c 3, b5a8 1, "a piece of armor". Picked Shell Chestpiece (base 2,800; b574 3444).
  - Tried 3,640 / 3,500 / 3,440 / 2,800: every one gave b534 0x28 "I, uh, forgot my purse." No sale.
  - The movie keeps the 2,800 attempt.
- C5 f6460, customer sells: b56c 14, b5a8 0, Thankful Statue (base 4,550; b574 1547 = 34%). Offered 2,730 (60%), accepted. Gold -2,730 at f6746 (bought low).
- C6 f7023: b56c 11, b5a8 1, "some food". Picked Beef Bowl (Low Price 2,300; b574 1572).
  - Side test: 2,990 (130%) as the first ask -> b534 9, instant walk-out ("What, you want to drive off customers?").
  - Kept: 2,500 -> pushback (b574 1822), 2,420 -> pushback (b574 1953). Round 3: 2,300 and 1,950 both gave 0x28 (over budget, she leaves).
- C7 f8015: Old Man (b56c 15), b5a8 2, Thief's Knife (base 3,400; b574 4080). This is the 130% sale that needed a counter-offer.
  - 4,420 (130%) -> "Too expensive, girl!", b584 2, b574 4277.
  - 4,300 was pushed back again (tried from a checkpoint).
  - 4,270 (125%) accepted. +4,270 at f8406.
- Session ended f8673 by the time slot running out (b528 6).

Price entry:
- The box opens at 100% of base, cursor on the leading digit.
- L/R move one digit; L past the leading digit adds a higher digit.
- U/D add or subtract 10^digit, with carry and borrow.
- The cursor resets for each new customer but stays put between rounds with the same customer.
- A shows Okay!/Start Again (b534 0xf); A on Okay! commits.
- After a commit, b534 tells the outcome: 7 sold, 8 pushback (b584 +1, b574 rises), 9 walk-out, 0x28 over budget.
- The customer's price b574 is never drawn as text (sprite only).

After: gold 470,000 -> 478,930. Merchant exp (bank dword 0xb0fd) 1179 -> 1230. Level 5 (HUD 6); bar runs 1000 -> 1350.

Save: ESC; Save is pm_sel 4; slot 020 overwrite -> Yes (play time 2:17:46 -> 2:20:12); B back to the shop.

Named state (this run): b534, b5a8, b584, b574, bb8, b56c, b528 (the cs rows from src/state_va.c) and mexp = s32(bank+4*0xb0fd). Also used stime from the shared vars. Worth sharing: mexp and the cs set.

Strip: view/strip_8130_8410.png, the Thief's Knife haggle: 130% -> refused -> 125% accepted -> gold up.

Tool gaps:
- `ck load` of an earlier checkpoint cuts the movie and deletes later checkpoints with no warning, and undo cannot reverse it. A way to read state at an old frame without moving the run is needed.
- The cs vars are not in the shared vars.
- The customer's offer is not visible in rd text.
- --watch without --every prints a row every tick.
