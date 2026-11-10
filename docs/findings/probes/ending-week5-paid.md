# Probe: ending-week5-paid

- **Run:** `ending-week5-paid` · **start:** save:67e22cc643bf · **task:** `tools/drive/tasks/ending-week5-paid.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 137 turns, $2.27, 15 min
- **Scenario:** `tests/scenarios/ending-week5-paid/` (57480 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/ending-week5-paid.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

## ending-week5-paid: the last payment is paid and the ending plays through to Endless Mode

**Result:** `rd check` PASS and `rd verify` MATCH (frame 57480, rng fe111b34). The movie is 57,480 frames from save:67e22cc643bf. It ends with control in the shop in Endless Mode: scene 1, cc08 1, dlg 0, day dword 36 (HUD 37), 400,000 pix. No game over at any point.

**Frame of each step (from `rd check`):**
1. Loaded with control: f137
2. day 31: f7246
3. day 33: f14661
4. day 36: f29629
5. `day >= 36 and frame > 30000`: f30001. This step only needs the frame number, so it came true during the payment form. The real "after the ending" state is the Endless Mode control at f57280–57480.

**Pictures I looked at:**
- The payment form, strip f29640–30120.
- The end of the rooftop scene, the "And so, my days of paying back endless debt… ended!" card and the start of the credits, strip f32400–33680.
- "Received Louie's True Card!" at f51664.
- The "Save the game?" Yes/No box at f54483.
- The title menu at f54731.
- The Endless Mode title card at f56919.

**Morning saves** (pause menu → Save, then `rd endsave` and a checkpoint):

| Day dword (HUD) | Slot | Save sha | Checkpoint |
|---|---|---|---|
| 29 (30) | — | start save, not re-saved | day29 f138 |
| 30 (31) | 035 | 7a7206319a8c | day30 f4381 |
| 31 (32) | 036 | d4162d20b10f | day31 f8630 |
| 32 (33) | 037 | b3c5345004f6 | day32 f11845 |
| 33 (34) | 038 | 3bd18e8e9a50 | day33 f15203 |
| 34 (35) | 039 | 9431e13ba1f0 | day34 f18418 |
| 35 (36, payment day) | 040 | 7705df41d668 | day35 f22279 |
| Endless save (day 36) | 001 | c062daff8848 (filed at f54483) | — |

- I read slots 035 and 036 back on the save screen. 037–040 are not confirmed: each sha changed, but I did not read their slot numbers.
- Other checkpoints:
  - d35s1 f23165: day 35, stime 1, on the map.
  - prepay f28845: payment night, on the map.
  - paid f30861: after the verdict.
  - saveprompt f54000: the "Save the game?" box.

**Days and events:**
- **Day 29 (HUD 30):**
  - stime 2: the map read 1111211 (Pub marked 2). Pub script 17/48: Recette asks Charme "Um. Yanno, Charme, I've been wondering... What made you want to become a thief?". Charme answers drunk: "At firsht I did it 'cuz I had to shurvive…", "...the more I do it, th'more I enjoy it!".
  - stime 3 (night): 1011100.
- **Day 30:** no events. Map states:

  | stime | map |
  |---|---|
  | 0 | 1111011 |
  | 1 | 1111011 |
  | 2 | 1111111 |
  | 3 | 1011100 |

- **Day 31:**
  - Morning event script 1/17 with Tear, about 200 ticks after control: "Good morning." / "So. The final payment is due in a week. It will be... sizable." She points to the pause menu to see how much is owed.
  - The map read 1111010 that morning (Pub and Adventurers' Guild closed). No other event.
- **Days 32–34:** no events. I waited 300 ticks each morning.
- **Day 35 (HUD 36, payment day):**
  - Morning script 1/18 with Tear: "Good morning, Recette." / "Well. Today is the final collection day." It ends with "I won't! Let's get to work!".
  - stime 0: 1111011.
  - stime 1: 1221021, with three events:
    - Market 14/18: Recette and the market woman. "Oh, hiya! Are you out shopping?". It is about the "legendary chef": "Slime Liver Salad", "Volcano Crab Cakes".
    - Town Square 17/24: a woman who dreams of flying ("Hmm...", "ever since I was young, I've dreamed of flying"). Tear quotes a book about humanity, then blushes.
    - Chapel 14/14: Louie, "Guys! Yo!" / "I was praying for Heaven to provide me with lunch."
    - None of these three visits cost a period. The trip still counted one when I got home.
  - stime 2: 1211211, with two events:
    - Market 15/9: the gratin woman ("Hmm...", "pumpkin gratin, sheep gratin, shark gratin..."). A third speaker suggests stew.
    - Pub 16/6: Charme. "It looks like nobody's here..." / "Heeeey, I'm here!", then the "Ponce" booze talk.
  - stime 3 (night): 1011100.
- **Adventurers' Guild list** (read days 29 and 32; closed on day 31): Leave, "Level:15 Swordsman Louie", "Level:8 Thief Charme". Nobody new.

**The payment (day dword 35 → 36):**
- stime 4 at f29232; Tear's day-end lines at f29254 and f29331.
- Daily Profits "Day 36" from about f29397: Profits 0, Score 6808 → 7308 (+500).
- The day dword became 36 at f29629.
- The collection form reads "FINAL WEEK / DEBT REPAYMENT 500000pix". Gold counted down 900,000 → 400,000 from about f29690 to f30000; Recette signs at about f30060 and PAID is stamped at about f30120 (confetti).
- Verdict script 1/19 (not 0/4 as in week 4), dlg 1 from f30102 to f30861 with Tear: "So, this is the payment for this week. Let me see..." / "Congratulations, Recette. With this payment, your father's loan has been repaid in full." / "You have worked very hard, Recette. Well done." / "May I... have a moment? Could we step outside, perhaps?" / "Sure!".

**The ending, in order:**
1. **f30877, scene 9 → 7:** stime goes back to 0. Script 1/20 starts at f30886 on the hilltop at night, with Tear and Recette.
   - "Ah... the evening breeze feels wonderful." / "Only five weeks have passed since I arrived here..."
   - Tear: "No, I deserved... nothing but your scorn and hatred. And yet you always trusted me…"
   - Recette: "That sounds like a lot of fun!" … "I got to meet you, Tear. I'm happier about that than I am about anything else!"
   - Tear: "M-merde... you say such embarrassing things so casually..." / "I... was happy to be there with you, Recette."
2. **About f32560:** fade to the card "And so, my days of paying back endless debt… ended!". The ESC Event Skip hint shows throughout.
3. **About f33500:** the credits ("RECETTEAR STAFF"). They run untouched until f47265 (dlg 1 → 0), about 13,600 ticks. A presses do nothing during them.
4. **Script 1/21, f47266–50764:**
   - The farewell: "Hey, Tear! You haven't forgotten anything, have you?" / "Farewell, Recette. Be the best shopkeeper you can possibly be." / "Teeeeeear!"
   - Then the ledgers: "Hm? There is something inside this barrel." / "These are... ledgers." / "THESE! ARE! ALL BLANK!"
   - Tear decides to stay: "Well! We need to go back, take inventory…" / "Okay... okay! Yeah! Let's go back!"
5. **About f51600:** a scroll reads "Received Louie's True Card!" and waits for A.
6. **Script 1/22, f51674–53700:** Alouette (rich-girl voice, "pardonne moi") and a second speaker explain the post-game:
   - Loading the save you are about to make starts "Endless Mode", with no debts.
   - "New Game +" (pick a save and keep its items and levels) and the Survival Modes are now unlocked; "Survival Hell" is mentioned.
   - True Cards let you hire that adventurer from the start in NG+ and Survival.
   - "aim to fill out the entire item encyclopedia!"
7. **f54000: the prompt "Save the game?", Yes / No.**
   - Yes opens the save picker (scene 7, t_sub 1), cursor on slot 001 (empty). A writes at once with no confirm: slot 001, SCORE 7308, TIME 2:55:58, tag "Endless".
   - B from the picker goes back to the Yes/No box. No (R, A) goes to the title.
   - I chose Yes and saved, then No to continue. No without saving would presumably reach the title with no Endless save.
8. **f54531, scene 7 → 0:** the title.

**What the title offers afterwards:** NEW GAME, NEW GAME +, SURVIVAL MODE, LOAD GAME, ITEM ENCYCLOPEDIA, SURVIVAL SCORE, OPTIONS, EXIT.
- The item codes at 0x9643358 are 5, 4, 6, 1, 7, 8, 2, 3.
- NEW GAME +, SURVIVAL MODE and SURVIVAL SCORE are new; Continue is now labelled "LOAD GAME".
- The cursor starts on LOAD GAME (t_cur 3).

**Loading the newest save:** A, then A on slot 001 (t_pcur 0) loads it at f54818: scene 1, day dword 36, 400,000 pix, stime 0.
- Script 12/1 then plays a title card, "Recettear Endless Mode ~A Time Of... Peace?...~", over a sepia shot of the "THESE! ARE! ALL BLANK!" moment. It has no text rows.
- dlg stays 1 until f57280, about 2,460 ticks; it looks timed, and A did not shorten it as far as I saw.
- Control in the shop from f57280, at Recette's morning spot. The movie ends there at f57480.

**Scene ids the ending passes through:**
- 9 (Profits, form, verdict)
- 7 (ending, credits, epilogue, True Card, save prompt and picker)
- 0 (title)
- 1 (Endless Mode shop)

Other scene ids met this run: 8 town map, 6 Market, 11 Adventurers' Guild, 13 Chapel, 14 Town Square, 15 Pub.

**Script ids:**
- 1/17 and 1/18: the morning reminders.
- 17/48: Pub.
- 14/18: Market.
- 17/24: Town Square.
- 14/14: Chapel.
- 15/9: Market.
- 16/6: Pub.
- 16/1, 16/2 and 16/3: the empty Town Square visits.
- 1/19: the verdict.
- 1/20: the rooftop scene and credits.
- 1/21: the farewell and ledgers.
- 1/22: the post-game explanation.
- 12/1: the Endless Mode title card.

**Route facts:**
- Burning a period: a Town Square visit ("nobody's here", about 210 ticks of A) and then home. Going straight home from the map costs no period.
- From home arrival at (2, −3.24), `L*8 R*16 A .*7 A .*7 A` leaves through the door to the map. One whole period is a fixed 531-tick macro: `.*30 U .*10 A .*40 (A .*6)x28 .*80 D .*10 A .*120 L*8 R*16 A .*7 A .*7 A .*40`.
- Map links, adding to week 4's:
  - Home L or R: Market or Merchants' Guild.
  - Market U: Pub. Pub R: Chapel. Pub D: Market.
  - Merchants' Guild D: Home.
- The Market is closed (0) at night.
- ESC is ignored for about the first 20 ticks of morning control. The first save attempt silently failed because of this.

**Tool gaps:**
- `rd tap --text` prints nothing for screens that are not dialogue (credits, the True Card scroll, the save prompt), so I needed pictures or `rd text` to see them.
- There is no "stop on a new prompt" predicate. I had to guess where Yes/No boxes would appear.
- The fifth task step (`frame > 30000`) is met during the form. It should require something like `scene == 0 or (day >= 36 and dlg == 0 and scene == 1)` after the credits.
- A shared name for the title item list (0x9643358 + 4i), or for script groups, would help.
