# Probe: calendar-week4

- **Run:** `calendar-week4` · **start:** save:59e71a0bcb65 · **task:** `tools/drive/tasks/calendar-week4.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 166 turns, $2.75, 21 min
- **Scenario:** `tests/scenarios/calendar-week4/` (36468 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/calendar-week4.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

## calendar-week4: week 4 walked through to the morning after the 200,000 payment

**Result:** `rd check` PASS and `rd verify` MATCH (frame 36468, rng e09bbab3). The movie is 36,468 frames long and starts from save:59e71a0bcb65 (day dword 22, HUD Day 23). No game over and no title screen.

**Frame of each step (from `rd check`):**
1. Loaded with control: f137
2. day 23: f8656
3. day 25: f20126
4. day 27: f27512
5. day 29, payment day over: f34857
6. Control on the morning after: f35953

**Pictures I looked at:**
- Town map at f386.
- Merchants' Guild event at f841, and the man in it at f3115.
- Girl event at f13367.
- Daily Profits "Day 23" (f8601), "Day 25" (f20035) and "Day 29" (f34847).
- The collection form stamped PAID at f35290.
- The morning after at f36468: 198,930 pix, Merchant Level 6. The screen is mid fade-in, so I could not read the HUD day.
- I also rendered the Profits screens at f12305, f23715, f27408 and f31101 but did not open them.

**Names defined for this run only:**
- `wms`: the 7 map states at 0x9643588 as digits, in the order Home, Market, Town Square, Merchant's Guild, Pub, Chapel, Adventurer's Guild.
- `sid`: s32(0x5c7a2c)*1000 + s32(0x5c7a30).

I suggest both become shared vars.

**Morning saves:** each one is pause menu → Save, the next empty slot, then `rd endsave`, plus a checkpoint. The picker writes without a confirm box. I read slots 027–029 back on the save screen (028 and 029 showed the new data); 030–034 were not read back.

| Day dword (HUD) | Slot | Save sha | Checkpoint |
|---|---|---|---|
| 22 (23) | — | start save 59e71a0bcb65, not re-saved | day22 f138 |
| 23 (24) | 028 | c5527143e016 | day23 f9248 |
| 24 (25) | 029 | 707a7abf3b06 | day24 f12901 |
| 25 (26) | 030 | 0309ff7e867a | day25 f20658 |
| 26 (27) | 031 | a58abeba37d2 | day26 f24351 |
| 27 (28) | 032 | 54bd221da407 | day27 f28044 |
| 28 (29, payment day) | 033 | 717ab3b7efd4 | day28 f31737 |
| 29 (30, after the payment) | 034 | 89190665bb8f | day29 f36468 |

There is also a checkpoint `prepay` at f34459: the payment night on the map, before going home.

**Map states (`wms`):**
- Normal morning: 1111011 (Pub shut).
- stime 1: 1111011.
- stime 2: 1111111.
- Night: 1011100.
- Day 24 morning: 1121010. Town Square was marked 2 and the Adventurers' Guild was disabled (0).
- Day 24 stime 1: 1121010 again. stime 2: 1111110, so the Guild stayed shut all of day 24.
- Day 22 had no 2 anywhere, but the guild event happened anyway (see below).
- No new destination appeared all week.

**Days and events:**
- **Day 22 (HUD 23), morning:**
  - No morning event.
  - Entering the Merchants' Guild started a new scripted chain with no 2 on the map: script 6/2 → 6/3 → 6/4, f551–f5407, about 4,850 ticks.
    - 6/2 opens with the guildmaster: "Good day!". Tear later says "We automatically refuse to place horse heads in anyone's bed."
    - 6/3 moves to the Town Square: "Thank you! I hope everyo[ne]…".
    - 6/4: "Hold it! You there!" / "Cease whatever vaguely sneak[y…]". A new tall blond man appears with Recette.
  - After the event it returned to the map with stime still 0. Visiting a guild costs no period.
  - At stime 1–3 the Town Square played the empty "It looks like nobody's here..." scripts 16/1, 16/2, 16/3. I used these to burn periods, about 240 ticks each.
- **Day 23 (HUD 24):** no events.
- **Day 24 (HUD 25):**
  - Morning: new script 18/1 in the Town Square, f13074–f15704. A character named "Girl" (brown pigtails, green dress, satchel; picture checked) says "Ummm... where is she..." / "There she is! There she is! Miss Receeeeette!".
  - stime 1: new script 17/21 in the Town Square, f16652–f18418: "*Sigh*..." / "What should I do?...". I did not see who speaks.
- **Days 25–28 (HUD 26–29):** no events. No morning event on any day (I waited 300 ticks each morning).

**Adventurers' Guild "Adventurer Selection":** I read it on days 22 (before and after the guild event), 23, 25, 26, 27 and 28. The guild was shut on day 24. It always showed Leave, "Level:15 Swordsman Louie" and "Level:8 Thief Charme". No new adventurer appeared through day 28.

**Merchants' Guild main list:** every day it was Buy, Sell, Fusion, Talk (tagged "New"), Leave, with no new option. The Talk topics all carry "New" only because none was ever read: What is the guild?, What can I do here?, About merchant levels, About the town, About unknown items, About fusion.

**Daily Profits** (Profits 0 every day, since no shop was run):
- "Day 23": Score 6508 (0), with the "Like Father, Like Daughter" badge.
- "Day 25": Score 6708 (+200), with no badge shown.
- "Day 29": Score 6708 (0), with the badge.
- I did not view Days 24, 26, 27 and 28.

**The payment (day 28 / HUD 29 night):**
- stime 4 at f34514; the Profits screen from f34674.
- A set day 29 at f34857.
- Gold counted down from f34918 (398,930) to 198,930 at f35185. The form reads "Week 4 Debt Repayment 200000pix" and was stamped PAID.
- The verdict script 0/4 ran with dlg 1 from f35306 to f35937: "So, this is the payment for this week. Let me see..." then "Congratulations. Well done.".
- Morning control at f35953, at 198,930 pix.

**Route facts:**
- Town-map cursor (`wm_cur`): Home 0, Market 1, Town Square 2, Merchant's Guild 3, Pub 4, Chapel 5, Adventurer's Guild 6. From Home: U goes to the Town Square, U again to the Chapel, then R to the Adventurers' Guild. From that guild D goes to the Merchants' Guild, L from there to the Town Square, and D from the Town Square to Home.
- The map ignores input for about 30 ticks after it appears.
- Visiting a guild or the Market costs no period. A Town Square visit does, and stime goes up when you get home.
- The map says "This action will not take any time" over the Chapel.
- Pause menu entry types are [0, 1(Items), 6, 2, 3(Save), 4], so Save is pm_sel 4. Opening and saving: `~ .*30 (D .*8)x4 A .*40 D .*12 A .*30`, then repeat `B .*30` until scene 1.

**Tool gaps:**
- `rd tap ... --until` crashed once with an AttributeError (`a.text`). I used `play "(A .*5)xN" --until` instead.
- No dialogue log over a range of frames, so the middles of the events are unread.
- Profits figures need pictures.
- The session was rebuilt twice mid-run (about 35–40 s of replay each).
