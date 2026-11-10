# Probe: calendar-week3

- **Run:** `calendar-week3` · **start:** save:fe0c2efd7af8 · **task:** `tools/drive/tasks/calendar-week3.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 158 turns, $2.72, 14 min
- **Scenario:** `tests/scenarios/calendar-week3/` (38835 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/calendar-week3.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

I walked the calendar from day dword 15 (HUD Day 16) afternoon to control on the morning after the week-3 payment (day 22, HUD Day 23). `rd check` gives PASS and `rd verify` gives MATCH (f38835, rng 7e08ba4f). There was no story event in any morning and no new destination, adventurer or customer; the new findings are the town events below.

Run: calendar-week3, from save:fe0c2efd7af8, 38,835 frames. I named two expressions for this run only: `wms` (the seven map states of 0x9643588 as digits) and `sid` (script id = s32(0x5c7a2c)*1000 + s32(0x5c7a30)).

Step frames (rd check):
1. Loaded with control: f137.
2. day 16: f4345.
3. day 17: f12423.
4. day 19: f21516.
5. day 22 (payment day over): f36397.
6. Control on the day after the payment: f37434.
No game over and no title.

Pictures I looked at: Daily Profits at f4190, f12345, f32629 and f36363; the collection form stamped PAID at f36851; the Old Man in Town Square at f23043; the morning after at f38618 (HUD Day 23, 398,930 pix, Merchant Level 6).

Morning saves (in-game save to the next empty slot, then rd endsave; checkpoint named dayNN):
- day 16 (HUD 17): slot 021, save a263daa7feb2, ck day16.
- day 17 (HUD 18): slot 022, save 0e58e63f7582, ck day17.
- day 18 (HUD 19): slot 023, save 3351a6e87a30, ck day18.
- day 19 (HUD 20): slot 024, save 36d4804b3bc0, ck day19.
- day 20 (HUD 21): slot 025, save c184ba7bfdbd, ck day20.
- day 21 (HUD 22, payment day): slot 026, save dcf2fe8486e1, ck day21.
- day 22 (HUD 23, after the payment): slot 027, save 59e71a0bcb65, ck day22.
I read slots 021–024 back on the save screen. Slots 025–027 were not read back; each save gave a new sha. There is one more checkpoint, prepay (f35994), on the payment night before going home.

Map states (0x9643588[0..6] = Home, Market, Town Square, Merchant's Guild, Pub, Chapel, Adventurer's Guild):
- At stime 3 (night) the map always read [1,0,1,1,1,0,0]: Market, Chapel and Adventurer's Guild disabled. On day 15 night the Pub was marked 2 instead of 1.
- At night the Pub plays script 16/4, "nobody's here", about 230 ticks. That makes it the cheapest night visit.
- On day 17 morning the Adventurer's Guild was disabled ([1,1,1,1,0,1,0]).
- After the Town Square evening event on day 19 the Guild became 0 again.

Days and events (the Pub is shut in the morning and opens at stime 2):
- Day 15 (HUD 16), stime 2: Pub marked 2. Script 6/1 is Charme drunk, the same event an earlier probe recorded (f675–f2502).
  - stime 3: Pub marked 2 again. New script 14/21: "Oh, Charme! Hello!" / Charme: "Ooooh, h'llo Recette.." / "Pffawww, Tear's here too?" (f2928–f3804).
  - Daily Profits "Day 16": Profits +8,930, Score 5485 (+293). Sold Thankful Statue, Longbow, Thief's Knife; bought Thankful Statue.
- Day 16 (HUD 17): no morning event (I waited 880 ticks).
  - Morning: Town Square marked 2, script 11/1, the known "Being in the plaza reminds me…" rumour (f5614–f8399).
  - stime 1: Town Square marked 2 again. New script 15/7: "What the... how peculiar. There is something shining in the fountain…" / "C-COINS?! Why are COINS in a publ[ic fountain]…" (f8790–f9627). Gold and item count did not change.
  - stime 2: Pub marked 2. New script 17/31: "Hey, coming to the pub reminds me." / "I heard there's a girl here who's been…" (f10040–f11340).
  - Daily Profits "Day 17": Profits 0, Score 5908 (+300).
- Day 17 (HUD 18): no morning event; the Adventurer's Guild was disabled.
  - stime 1: new script 17/33 in Town Square: "Alright! Finally get to have a nice big lunch for once!" … "Woof!" (a dog appears; f14220–f15398).
- Day 18 (HUD 19):
  - stime 1: new script 17/6 in Town Square: "Ahh, it's such a nice day today!" / "Bright sun, blue skies, white clouds, happy people," (f18931–f19885).
- Day 19 (HUD 20):
  - Morning: new script 15/8 in Town Square. Old Man (picture checked): "Hello!" / "Ah, little Recette! Working hard, I hope?" (f22818–f23760).
  - stime 1: new script 17/5: "Hmm..." / "Aha! Enlightenment! A bo[…]" (f24139–f25692).
  - stime 2: new script 14/5: "The plaza in the evening feels kinda... lonely." / "It makes you think, 'It's really time to go home now!'" (f26071–f26735).
- Day 20 (HUD 21):
  - stime 1: new script 17/35: "Ah, what to do, what to do..." (f29688–f31045).
  - Daily Profits "Day 21": Profits 0, Score 6508 (+100). A red "Like Father, Like Daughter" badge appears. It was not there on Day 17; I did not look at Days 18–20.
- Day 21 (HUD 22), payment day: no town events at any time.

The payment (from the payment-day morning save, slot 026):
- Night exchange from f36052 (stime 4 at f36053). Daily Profits from f36213: "Day 22", Profits 0, Score 6508 (0), the same badge.
- A set day 22 at f36397.
- The collection form asked for 80,000 pix. Gold counted down from f36458 (478,930) to 398,930 at f36701, then the PAID stamp and confetti.
- Verdict script, id 0/4: dlg 1 from f36852, "So, this is the payment for this week. Let me see...", dlg 0 at f37418. Mostly A-tapped, so I read only that first line.
- Morning: scene 1, stime 0 at f37434. No event in the next 1,200 ticks.

Route facts:
- The shop door needs A while facing +x within about 1.8 of it.
- After you come home, Tear stands in the doorway (2.9, −3.24) and input is locked for about 100–150 ticks. An A there opens her menu. This loop reached the map every time: `.*140 D*20 .*60 U*24 R*8 . A .*10 A .*30`.
- From the morning spot: `R*20 U*72 R*8 . A .*10 A .*30`.
- Market loop: `L .*6 A .*120 D .*8 D .*8 A .*100`. Home from the Market cursor: `R D A`. Home from the Pub: `R D D A`.
- The A that leaves the payment screen starts the verdict dialogue on the same tick.

What the tool lacked:
- No dialogue log over a range of frames. I had to stop every event to read its first lines, so the middles are unread.
- No list comprehension in expressions, so I could not read the map-state array in one eval; I defined `wms` to get around it.
- rd text cannot read sprite screens, so the Profits and payment figures needed pictures (one batched `look --at` covered nine frames).

I suggest shared vars for `wms` and `sid`.
