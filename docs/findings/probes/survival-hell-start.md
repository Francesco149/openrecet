# Probe: survival-hell-start

- **Run:** `survival-hell-start` · **start:** save:c062daff8848 · **task:** `tools/drive/tasks/survival-hell-start.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 81 turns, $1.34, 10 min
- **Scenario:** `tests/scenarios/survival-hell-start/` (20163 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/survival-hell-start.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     survival-hell-start                  f212 - ⇄ floor_layout_maze (-7)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `survival-hell-start`: Survival Hell starts, the first payment fails, and the game ends on the title.** The movie is 20,163 frames from save:c062daff8848. `rd check` gives PASS and `rd verify` gives MATCH at f20163 (rng 5e7306ff).

**Frame of each step (from `rd check`):**
1. At the title: f61.
2. In the game with control: f1593, the end of the opening script.
3. Payment day over or run ended: f7653. This is the day dword reaching 2, which the predicate allows. The run itself goes on to the game over and back to the title at f20036, where `scene == 0 and frame > 3000` also holds.

**Checkpoints:** modelist f124, picker f206, control f1593, map0 f1984, morning1 f4506, morning2 f7653, payday f16071 (day 6 morning), paynight f17823 (stime 4 on the payment night).

**Differences from Normal Survival:**
- **Prompts:** none differ. The list's cursor (s32 0x9643558) starts at 0 = Survival Hell, so A at f124 picks it (t_sub 1). It opens the same NG+ picker ("Please choose a file for New Game+ data.", only slot 001, Endless 7308). Two A presses later: scene 1 at f252, where Normal reached it at f283.
- **Bank diff (mark f206 → f252): 687 dwords changed; Normal changed 68.**
  - Everything is wiped: gold 400000 → 1000, day 36 → 0, mlevel 5 → 0 (HUD Merchant Level 1), items 33 → 0, 0xb3c0 5 → 0.
  - The 0x44e37b0.. block went to −1 (the inventory). Many adventurer stat and level dwords at 0x450d000.. and 0x4510648..0x4510a04 dropped to lower values or 0.
  - **0xb759 goes 3 → 2, the same as Normal.** So it is not "the mode": 2 just means "Survival".
  - **The Hell marker is bank dword 0xb78d (VA 0x45115cc), 0 → 1.** It is not in Normal's 68-dword diff. Suggested shared name `surv_hell`. It stays 1 after the game over.
  - Unchanged from Normal: 0xb0f9 = 1, 0xb0fa = 7, 0x450de58 = 3, score 0xb0f7 = 0.
- **Opening script:** sid 19 (Normal: 18). dlg 1 at f253, control at f1593, about 1,340 ticks with A every 6.
  - It opens with "Welcome, everyone, to what we call "Survival Hell" mode!" and keeps the Normal lines about debt, events and True Cards.
  - The Hell-only part: "Oh yeah, and in "Survival Hell" mode, lesse... You don't get to keep your money, items, merchant levels, or adventurer levels! All you get from your save are your customers and the reputation levels with them." / "..." / "Wait, hang on, WHAT? How is anyone supposed to get anything done then?" / "Well, it is called "Survival Hell" mode for a reason. Remember: if this mode causes you to gouge out your eyes in frustration, then we are doing our jobs correctly!" / "Boy, that'll sure encourage people to play..."
  - Then the save and game-over lines and "Well, good luck!...", plus a closing aside: "(Wow, they're so totally screwed, aren't they?)" / "(Shush!)".
- **Start state:** gold 1000, day 0, mlevel 0, items 0, stime 0. Recette at (−0.3, 9.35), Tear at (0.6, 9.35).
- **Payment schedule:** no difference. The pause Status calendar (picture f1694) shows "20,000 pix Payment Due!" on day 7, the same as Normal.

**Passing the days:**
- Each period: shop door → map → Market → Leave → Home.
- The Market counts as a period visit; stime goes up on reaching Home. Choosing Home straight from the map does not advance stime.
- The map ignores input for about 40 ticks after it appears.
- `wm_cur` moves by direction. From 0 (Home): L → 1 (Market), R → 3. From 1: R → 2, U → 4, D → 0. From 2: L → 1. From 4: L → 6, D → 1. From 6: U → 0. From 0: D → 5.
- In the evening the Market is closed (wms 1011100), so I used the Pub (wm 4). It plays "It looks like nobody's here..." (sid 16004, scene 15, about 200 ticks).
- **Each morning Recette cannot move for 207 ticks after control.** `conv` (0x56daafc) reads 6 and Tear shows a "!" (picture f4668). It ends on a timer, not on A.
- My route to the door: `.*210 R*20 U*72 R*4 A`. The door needs A.
- A whole day takes 2,112 ticks. Mornings came at f4506 (day 1), f7653, f9735, f11847, f13959 and f16071 (day 6 = calendar day 7).

**The failed payment** (night of day dword 6, from paynight f17823, A every 6 ticks):
- Tear's goodnight lines, then Daily Profits "Day 7" (scene 9 from f18033). The day dword goes 6 → 7 at f18173.
- A Terme Finance collection form "WEEK #1 DEBT REPAYMENT 20000 pix ₱1,000" appears. Gold drains 1000 → 886 (f18243) → 0 (f18313).
- Script sid 5, dlg 1 from f18453, still in scene 9. Liza stamps it red. Her lines: "I am afraid this amount of money is insufficient." / "I am sorry, but as an agent of Terme Finance, I must now seize this house in order to repay the outstanding debt." Recette: "Wha?... No! I... I..."
- scene 2 (game over) at f18873, sid 1: the cardboard "Recette's House" box scene, with "ESC Key: Event Skip". Lines: "And so, the cute, courageous little heroine was crushed by the cruel bitterness of reality." / "Waaaahaahaaah... Why'd it have to end in a box?!" "GAME OVER" is drawn at about f19670–f19835.
- scene 0 at f20036. On the title t_cur is 3 (t_item 1 = Continue). The bank reads gold 1000, day 1 and 0xb78d still 1 (nothing saved).

**Records page:** from t_cur 3, D D → t_cur 5 (t_item 8), then A → t_sub 4 (f20163). It shows Record End-game Score 7308 pt, Money 400000 pix, **Survival Hell Record Day 7**, Normal Survival Record Day --. Before this run the Hell record read "--".

**Pictures I looked at:** the calendar (f1694), the stuck morning (f4668), and a strip of the end sequence, f18020–f20000 every 165 (profits, the collection form, the stamp, the box, GAME OVER).

**Tool gaps:**
- The records page has no picture: the strip stopped at f20000.
- There is no shared var for the morning lock (`conv == 6`), and nothing in state says when the town map accepts input.
- Searched walking paths are wandering (239–263 frames). I hand-tuned the door route instead.
- `rd tap` cannot press anything but A. To log events I had to rewind and replay the tap as `play "(A .*6)xN"`.
- Suggested shared names: `surv_cur` 0x9643558, `surv_hell` = s32(bank + 4*0xb78d), `conv` lock.
