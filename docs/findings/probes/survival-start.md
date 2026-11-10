# Probe: survival-start

- **Run:** `survival-start` · **start:** save:c062daff8848 · **task:** `tools/drive/tasks/survival-start.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 64 turns, $0.95, 7 min
- **Scenario:** `tests/scenarios/survival-start/` (6918 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/survival-start.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     survival-start                       f243 - ⇄ floor_layout_maze (-7)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `survival-start`: Normal Survival starts and the trace passes.** The movie is 6,918 frames from save:c062daff8848. `rd check` gives PASS, and `rd verify` gives MATCH at f6918 (rng d1fe6e46). No game over.

**Frame of each step (from `rd check`):**
1. At the title: f61.
2. In the game with control: f283. This is the first tick in the shop; the opening script takes over right after.
3. A day later: f6738.

**What step 3's "day >= 2" actually covers.** In Survival the day dword starts at **0**, not 1 as in the story and in New Game +. So `day >= 2` meant passing **two** days, and I did that. The first morning comes at f4040 (day 1), the second at f6738 (day 2). The task's predicate seems to have assumed the story's starting value.

**The save:** in-game Save to slot 007 on the first morning (day dword 1, HUD "Day 2"). `rd endsave` gives **099af5b31445**. The slot reads "LOOP 2, TIME 2:57:07, Normal Survival", score 0. I did not save on the second morning, so the endsave at f6918 is the same file.

**(1) Prompts and their options:**
- Title: U from LOAD GAME goes to SURVIVAL MODE (t_cur 2, t_item 6) at f71. A opens a scroll list with "Survival Hell" (pointer on it) and "Normal Survival".
- The list's cursor is **s32 0x9643558** (0 = Hell, 1 = Normal). I named it `surv_cur` for this run only. The selected row draws as ff7f7f7f, the other as ff606060.
- A on Normal Survival (f156) sets t_sub 1. It opens the **same picker as New Game +**: titled LOAD GAME, with the message "Please choose a file for New Game+ data." It lists only slot 001 (Endless, SCORE 7308, LOOP 1, 2:55:58).
- The first A (f197) closes the message. The second A (f238) starts at once, with no confirm.
- t_start counts 1 per tick to 46. scene 1 at f283.

**Bank diff (picker f237 → f283, 0x44e3798..0x4511760): 68 dwords changed.**
- The picker itself changed nothing in the bank: slot 001 was already the bank.
- Gold 400000 → 1000 (dword 3 and 0xb0f6). Score 0xb0f7: 7308 → 0.
- **0xb0f9: 0 → 1 and 0xb0fa: 0 → 7, the same as New Game +.** So these do not tell the two modes apart.
- **0xb0fb (day): 36 → 0.** New Game + set it to 1.
- **0xb759: 3 → 2.** New Game + set it to 1. This is my best candidate for a mode dword (guess: 1 NG+, 2 Normal Survival, 3 Endless / cleared). It needs a Survival Hell start to confirm.
- 0xb3c0 / 0xb3c2 / 0xb3c3: 5, 3, 2 → 0.
- 0x450de58: 0 → 3.
- 0x450ad60..0x450ae48: 25 dwords went from −1 to 0.
- About 30 byte-flag dwords at 0x450f3e4..0x450f5e0 were cleared, and 0x450f4c0 / 4c4 / 4c8 went to 257 / 0x01000000 / 257. This is the same pattern as New Game +.
- **Unlike New Game +, no item moved:** items stayed 33 (NG+ went 33 → 41).

**(2) The opening:** Tear talks to Recette in the shop. It is script sid 18 (0/18) and shows "ESC Key: Event Skip". I advanced it with A, f416–f1252, about 970 ticks. The full transcript:
- "Welcome, everyone, to Normal Survival mode!"
- "Like Alouette and Prime said at the end of the main story, Survival Mode is where you have to pay back debt every week... forever!"
- "And, naturally, the amount due increases every week near-infinitely, so push yourself and see how far you can get before you are crushed beneath the relentless wheels of capitalism!"
- "The gameplay is more or less like in the main game, but there aren't any events or story things and stuff. Which is to say: you can not recruit adventurers in this mode. You will only be able to rely on those who have given you their "True Cards" in the saved game you are loading data from."
- "And in Normal Survival, you get to keep your merchant level, customer reputation, and all your items, so if you have a really good save, you might be able to get pretty far! You'll always start out with 1000 pix though, so be careful!"
- "You may also save and load as normal, of course. Survival mode saves will be clearly marked as such."
- "Once you finally reach a point where you can no longer pay back any debt, you will get a game over, and if you set a new record, it will be recorded in the Survival Score section of the title menu."
- "Well, good luck! Try to survive as many days as you can!"

**(3) State at first control (f1252):**
- gold 1000, day 0 (calendar "Today" = 1), mlevel 5 (HUD Merchant Level 6), items 33, stime 0, d_table 0.
- Recette at (−0.3, 9.35), Tear at (0.6, 9.35).
- **Payment schedule** (pause Status page, picture f1382): the calendar shows days 1–35. "20,000 pix Payment Due!" is circled on **day 7**. New Game + had 10,000 due on day 8.
- Town map `wms` = 1111011, a normal morning. It went 1111111 at stime 2, 1111110 later that period, and 1011100 at stime 3. No "2" (event waiting) appeared on either day.

**(4) The day routine:** shop door → map → Town Square → Home, once per period.
- The Town Square plays "It looks like nobody's here..." (scripts 16/1–16/3, scene 14, about 200 ticks).
- stime only goes up on reaching Home: two Town Square visits in one trip still counted as one period.
- When Recette comes home she stands at (2, −3.24). After about 60 ticks, `R*12 A` leaves again. **Leaving needs A at the door**: walking into it facing +x was not enough.
- First night: stime 4 at f3692. Tear: "Thanks for all your help today, Tear!" / "You also did well. You look tired; get some rest." Then **Daily Profits "Day 1"** (Profits 0, Score Difference 0(0)). That screen runs with **scene == 9**, f3847..f4023.
- day dword 1 at f4023; scene 1 / cc08 1 / dlg 0 at f4040.
- A "**Survival Mode / Day 2**" title card is still on screen at f4096–f4160 (pictures), although the state already reads control. There was no morning event in 120 ticks. Gold, mlevel and items were unchanged.
- Second night: Profits at f6543 (scene 9), day 2 at f6721, control at f6738 under a "Survival Mode Day 3" card (f6798). Gold is still 1000.

**Pictures I looked at:** the mode list (f124 with the pointer on Hell, f156 on Normal), Tear's opening (f416), the pause calendar (f1382), and the first and second mornings' day cards (f4096, f4160, f6798).

**Checkpoints:** modelist f124, normalsel f155, picker f237, shop0 f283, control f1252, map0 f1720, evening f3332, night f4023, morning f4160.

**Game facts:**
- Survival reuses the NG+ file picker and its NG+ text.
- The day dword starts at 0. Daily Profits uses scene 9.
- The day card covers the screen after state reads control, and ESC is ignored then. I saw ESC ignored right after control both at f1252 and at f4160; on the morning it worked about 40 ticks later (f4202).

**Tool gaps:**
- `rd play` cannot nest groups: `( … ( … )xN … )xM` gives "ValueError: substring not found" from parse_spec.
- No state var tells when the day card / fade has ended, so the first ESC after control silently does nothing.
- `rd text` cannot tell the mode list's cursor, so `surv_cur` (0x9643558) would be worth a shared var.
- No var marks the game mode. 0xb759 is a candidate, as is a named debt amount / due day.
