# Probe: week5-gameover

- **Run:** `week5-gameover` · **start:** save:89190665bb8f · **task:** `tools/drive/tasks/week5-gameover.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 53 turns, $0.88, 8 min
- **Scenario:** `tests/scenarios/week5-gameover/` (25281 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/week5-gameover.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical week5-gameover                       ticks 1..25581, save IDENTICAL

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

## week5-gameover: the last payment is missed, and the game loops back to day 1

**Result:** `rd check` PASS, `rd verify` MATCH (frame 25281, rng e002425e). The movie is 25,281 frames from save:89190665bb8f. It ends with control in the shop on the new loop's first morning: day dword 1 (HUD "Day 2"), 1,000 pix. I looked at strips of the collection form and of the game-over screens.

**Frame of each step (from `rd check`):**
1. Loaded with control: f137
2. Day dword 35 (payment day): f17494
3. `scene == 2`: f21457. The day dword went back to 1 at f22623, and control came back at f25270.

**Payment-morning save:** **5f038738193e**, filed at f18035 by `rd endsave`. It is the day dword 35 (HUD 36) morning after the 1/18 event, saved to slot 035 (the first empty one on the picker). Checkpoints in the run: `paymorning` f18035 and `prepay` f20234 (stime 4 at home, before the Profits screen).

**How the days were burned (no money earned, gold 198,930 the whole way):**
- One period is a Town Square "nobody's here" visit (16/1–16/3) and then going home: the 531-tick macro from `ending-week5-paid`. A whole day is 4 periods; the 4th ends with `(A .*10)` taps through the day end.
- **Leaving the shop each morning:**
  - On mornings with no event, input is ignored for about 208 ticks after `scene` becomes 1. I used `(DL*4 .)x60 --until moved` to wait for it.
  - Then a fixed path that a search found: `DL*16 U*16 R*8 DR*16 UL*32 UR*8 L*28 B .*7 R*8 D*14 U*24 UR*8 R*12 D*2 UR*16 UL*8 UR*8 L*2 R*2 A .*7 A .*7 A`, about 253 ticks, to `scene == 8`. It worked every day.
  - The first path the search found (from f138) talked to Tear on day 30.
- **Events skipped with `.*10 ~ .*40 A`:**
  - Morning 1/17 (day 31) at f5955.
  - Morning 1/18 (day 35) at f17708.
  - The Town Square event 17/24 on day 35, stime 1, at f18931.
  - Each skip took about 60 ticks.
- No other events: days 29, 30 and 32–34 had none on this route.

**The failed payment, in order (scenes and scripts, with `sid` = group*1000 + id):**
1. **Night lines and Profits:**
   - stime 4 at f20234. Tear's night lines at f20255 ("Thanks for all your help today, Tear!" / "You also did well… get some rest.").
   - scene 1→9 at f20394. Daily Profits "Day 36": Profits 0, Score 6708 → 6808 (+100).
2. **The collection form:**
   - The day dword became 36 at f20592.
   - From f20653 gold counted down 198,930 → 0, with the paid amount rising on the form. The form reads "FINAL WEEK / DEBT REPAYMENT 500000pix" and was left at ₱198,930.
   - It was stamped **VOID** (red) at about f21040. There is no signature.
3. **The verdict, script 0/5 (sid 5), dlg 1 from f21040 to f21441.** Tear: "I am afraid this amount of money is insufficient." / "I am sorry, but as an agent of Terme Finance, I must now seize this house in order to repay the outstanding debt." Recette: "Wha?... No! I... I...". There is no prompt and no options.
4. **Game over, scene 9→2 at f21457** (stime → 0). Script 0/1 (sid 1), dlg 1 from f21466 to f22619. It shows a cardboard box labelled "Recette's House" under a spotlight, with "GAME OVER" written above it from about f22175. The narration reads "And so, the cute, courageous little heroine was crushed by the cruel bitterness of reality." / Recette: "Waaaahaahaaah... Why'd it have to end in a box?!". The ESC Event Skip hint shows. The title screen never appears.
5. **Day dword 36 → 1 and gold 0 → 1000 at f22623.** Script 0/2 (sid 2), dlg 1 from f22624 to f24395, in the bedroom. This is the "it was all a dream" loop opening: Tear: "... This must be a joke. If I did not know better, I would say you were in a coma." / Recette: "Tear was just a dream, I guess..." / "WOULD YOU WAKE UP, PLEASE?!" / "I didn't shoot the sheriff!" / "Yesterday we paid our respects at the Merchant's Guild, and then briefly opened shop…" / "Tear, let's give it our all!" / "wash yourself and meet me downstairs."
6. **scene 2→1 at f24421**, with a "Day 2" card. Script 2/6 (sid 2006), dlg 1 from f24611 to f25270, the shop tutorial lines: "So! I have an item shop!…" / "If you sit at the counter, we can open immediately…" / "Simply pick something." / "Well, okay...".
7. **Control at f25270:** scene 1, cc08 1, dlg 0, at (−0.3, 9.35).

**The new loop (end state):**
- day dword **1** (HUD Day 2), gold **1000**, stime 0, wms 1011200.
- mlevel **5** (the HUD shows Merchant Level 6, so the variable looks 0-based), unchanged from before the payment.
- items **32**, unchanged.

**Against `gameover-loop-dungeon0` (week 2):**
- The same: no prompt anywhere, the form takes all the gold and is voided, scene 2 holds the game over and the dream, then a restart at day dword 1 with 1000 pix, the merchant level and the items kept.
- That probe gave "merchant level 6 and 33 items". My mlevel 5 is probably the same HUD Level 6, but I did not compare their units. Their 33 items against my 32 is a different save, not a different rule.
- I did not check whether week 5's verdict text is the same as week 2's. That probe gives no script ids or verdict lines. Here the verdict is 0/5 and the game over is 0/1 → 0/2, against 1/19 for the paid ending and 0/4 for a paid weekly payment.
- I did not read the loop counter (the save picker's "LOOP 1"); it should now be 2.

**Script ids seen:** 16/1–16/3 (empty Town Square), 1/17 and 1/18 (morning reminders), 17/24 (Town Square, day 35), 0/5 (failed-payment verdict), 0/1 (game over), 0/2 (the dream morning), 2/6 (the day-2 shop intro).

**Scene ids in the sequence:** 1 → 9 (Profits, form, verdict) → 2 (game over and the dream morning) → 1. Elsewhere in the run: 8 (town map) and 14 (Town Square).

**Pictures:** `view/strip_20600_21200.png` (Profits, the form counting down, VOID, the verdict) and `view/strip_21350_24650.png` ("Wha?… No!", the box, GAME OVER, the bedroom dream, and the shop at f24650 with the HUD at 1,000 pix, Day 2, Merchant level 6).

**State I named:** `sidev` (this run only) is the same expression as the shared `sid`, defined with `--event` so `play` prints its changes. That is how I got the script frames above. It might be worth making `sid` itself an event var.

**Tool gaps:**
- Nested groups in an input spec, like `((A .*6)x28 …)x2`, crash `parse_spec` with a ValueError. I had to write the period macro out once per period.
- `tap --text` does not report scene, dlg or sid changes. To get the frames I reloaded `prepay` and replayed the same inputs with `play`; the movie came out identical (same rng).
- On mornings with no event, input is ignored for about 208 ticks after `scene` turns 1 while cc08 is 1 and dlg is 0. No named variable shows this lock; I could only detect it by waiting for movement. A search started at that moment explored nothing for 60 seconds.
- `px != -0.3` is true straight away because of float precision. Write it as `abs(px+0.3) > 0.05`.
