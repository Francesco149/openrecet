# Probe: day8-visitor

- **Run:** `day8-visitor` · **start:** save:92f349ae39d4 · **task:** `tools/drive/tasks/day8-visitor.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 78 turns, $1.13, 16 min
- **Scenario:** `tests/scenarios/day8-visitor/` (14479 frames)
- **Verdicts (2026-11-11):** check FAIL (tools/drive/tasks/day8-visitor.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical day8-visitor                         ticks 1..14779, save IDENTICAL

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

## day8-visitor: the day-8 visitor scene played out in full. Verify MATCHes; check FAILs on one forbid, which I think is mislabelled

**Results**
- **Run:** `day8-visitor`, from save:92f349ae39d4, 14,479 frames.
- **`rd verify`:** MATCH (f14479, rng ab500522).
- **`rd check`:** FAIL. All 4 steps pass, but the forbid "game over: scene == 2" fires at f6771 and holds for 1,648 frames.
  - That stretch is not a game over. It is a story event the game forces outdoors on the morning of day 8. A picture at f7613 shows the town map with Alouette in front of the shop and "ESC Key: Event Skip". It starts right after the week-1 payment verdict, and the game passes through scene 2 to play it.
  - I see no input that avoids it. The forbid (or the vars note that scene 2 means game over) needs changing: scene 2 also hosts outdoor event scripts.
- **End save:** c4aac28d9cab (slot 021 in game, score 5392).

**Step frames**
1. In the house with control on day 7: f137, after `.*60 A .*30 A`.
2. Day rolled to 8: f5612.
3. Visitor scene armed (`u8(0x450f470) == 1`): f8419. This is the house entry after the Alouette outdoor event.
4. Scene over, seen flag set: f14137. I then walked from (−0.3, 9.35) to (−1.5, 8.94) and saved from the pause menu to slot 021 (f14205–f14407). Back in scene 1 at f14459.

**Addresses checked first:** bank = 0x44e3798. The day dword (0x450fb84) read 7 and stime read 0. The arm byte (0x450f470) and seen byte (0x450f480) both read 0 on the loaded save.

**How day 7 passed, and the payment night**
- **Town trips:** visiting a place costs no time; the map says "This action will not take any time". What costs a period is coming home after visiting at least one place. Going straight home from the map cost nothing (f2565, stime stayed 1).
  - Loop per period: leave by the door → Market → `L .*6 A .*120 D .*8 D .*8 A .*100` → home with `R D A`. That took stime 1→2 at f3439 and 2→3 at f4043.
  - The first trip (Market, then the Town Square rumour event, skipped with ESC) gave stime 0→1 at f2248.
- **Night:** at stime 3 I visited the Pub. It played a new Charme script ("It looks like nobody's here..." / "Heeeey, I'm here!" ... "Ponce"), which I skipped with ESC. Home then gave stime 4 and Daily Profits "Day 8": Profits 0, Score 5392 (+200), seen at f5523.
- **Payment night: yes, it happened.** Gold went 470,000 → 460,000 (a 10,000 pix payment) between f5612 and f6212. Then the verdict script (dlg 1 from f6213): "So, this is the payment for this week. Let me see..." / "Congratulations. Well done." / "I have officially received your payment for this week." ... "YAY! I did it!"
  - The day number seems to pick the week-1 payment, whatever the week-3 flags say. I did not look at the payment screen itself.
- **Forced outdoor event (scene 2, f6771–f8419):** Alouette in front of the shop. "Mmmm-hmmm! So! This is it! That insolent little plebeian's shop..." ... Prime: "Uh, y'know y'don't own that store yet, right?" ... "Oh little shop, "Recettear"... Your days are numbered!" ... "... AND I TOLD YOU TO ADDRESS ME AS "YOUNG MISTRESS!"". I A-tapped through it, no skip.

**The visitor scene (no Event Skip; the timer `vt` = s32(0x438b924), T = s32(0x5c7df0) = 1200)**
- **Who:** Alouette ("Young Mistress Alouette", heir of the Big Bash store chain) hiding in a cardboard box, with her fairy Prime.
  - Char 47 is the box. Char 48 is Alouette.
- **Arm, f8419:** the box spawns at (3, −3), grid cell (6, 2). The seen byte 0x450f480 became 1 between f8419 and f8519, i.e. at the start of the scene, not the end.
- **Wander, vt 180→1200 (f8599–f9619):** the box roams the shop: (2.7, −1.8) → (1.5, 0.7) → (2.5, 2.8) → (0.2, −4.8) → (−5.9, −3.5) → (−2.7, 2.9) → (2.2, −3.5).
  - I stayed put, so T stayed 1200. The code shortens T if Recette comes within 2.5.
- **Script 10/1 at vt == T, f9619–f10882:** the timer pauses while it runs. "O-hohohoho! My genius astounds even me, sometimes!" ... "The interior decoration is as seedy and plebeian as the outside..." ... "Perhaps... one of those books father says I'm too young to read?..." / Recette: "Uh, could you come out of the box, maybe, at least?" ... "... Well then!"
- **Reveal, T+50 (f10942):** the visitor record's +0x7c becomes 1 (Alouette shown). The box flies off: its draw position rises 0.3/tick in y and moves 0.1/tick in x. At T+65 the record's +0x78 becomes 0 (box hidden). Alouette appears near the door (strip f11100).
- **Script 10/2 at T+260, f11142–f14078:** "I should have expected a foe as canny as you..." / "You are indeed worthy of being my rival!" / "I am the owner of the monolithic item store chain, "Big Bash"!" / "Alouette is my name." ... Tear: "...fifteen billion pix..." ... Prime: "Call me Prime. ...the mistress's guardian." ... "Recette! Listen, and listen well! Enjoy your happy, carefree days..." ... Tear: "...not dissimilar to meeting a hurricane in human form..."
- **Fade out from T+290:** the arm byte clears and `vt` resets to 192 at f14137.

**State the scene drives**
- **`g_story_visitor`** = 0x73a6dd0, 0x26 dwords, to 0x73a6e67. This is from `sub_4706EA` (IDA all_ida.c:76377).
  - +0x00 = 1 (active); +0x14/+0x18/+0x1c = x, y, z (f32).
  - +0x48/+0x4c = grid cell (6, 2 when bank dword 45944 == 0, else 7, 0); x = 2·cx − 9, z = 2·cz − 7.
  - +0x40 = −1, +0x5c = −1, +0x64 = rand & 1 (read 1 here), +0x68 = 0.7f.
  - +0x78 = box shown, +0x7c = Alouette shown.
- **Sprites:** char 47's actor block is 0x73a8b50, char 48's is 0x73a8b7c. Their draw positions are at 0x73a8b38 and 0x73a8b44.
- **Camera:** I did not read any camera state. The strip shows the normal house camera all through.
- **Strip I looked at:** f8900–f11300, every 200 frames. It shows the box wandering, both dialogues, and Alouette standing by the door.

**Variable I named:** `vt` = s32 at 0x438b924 (the house scene timer the visitor phases count on), defined for this run only. Worth sharing. The scene-2 note in vars.json should also say "or an outdoor event script".

**What the tool lacked:** a forbid can't tell this scene-2 event from a real game over. Checkpoints only exist where I saved them, so I could not bracket the seen-byte write any closer than f8419–f8519 without replaying.
