# Probe: order-deadline-missed

- **Run:** `order-deadline-missed` · **start:** save:44e9baea3987 · **task:** `tools/drive/tasks/order-deadline-missed.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 144 turns, $2.55, 13 min
- **Scenario:** `tests/scenarios/order-deadline-missed/` (10716 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/order-deadline-missed.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical order-deadline-missed                ticks 1..11016, save IDENTICAL

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `order-deadline-missed`** (from save:44e9baea3987) is 10,716 frames long. `rd check` gives **PASS** on all 4 steps with no forbid hit, and `rd verify` gives **MATCH** (rng da5912d5, sem b742238a5644cd50). I made an in-game Save to slot 020 at f10663 (play time 2:20:16 → 2:23:11). The end save is **67129e910529** (`rd new NEXT --from save:67129e910529`).

**Step frames**
1. Loaded with control: f137 (day dword 15, stime 2).
2. Outstanding Orders page open (cc08 17): f205.
3. Day ≥ 19: f10202.
4. Control at the end: f10219. After that the movie runs on through Tear's page, the Save and closing the pause menu, and ends in free roam at f10716.

**Pictures I looked at**
- f8039: the hand-over picker.
- `view/strip_7890_8290.png`: the orderer's visit.
- f215, f5778 and f10490: Tear's page. f5778 shows "Today / Louie / 2 scarves" (HUD Day 19); f10490 shows only Caillou's row (HUD Day 20).

**The page row, day by day** (`rd text`; the HUD day is the day dword + 1)
- Day 15 (f215): "in 3 days / Louie / 2 scarves". So the orderer is Louie, customer id 2, not an anonymous customer.
- Day 16 (f1955): "in 2 days".
- Day 17 (f3980): "by tomorrow".
- Day 18 (f5778): "Today".
- Day 19 (f10490): Louie's row is gone. The page now shows "No Deadline / Caillou / Charred Lizard, Bat Wings / Slime Liver". That row probably comes from the story event I skipped on day 18 (see Customers below).

**The order's memory**
- There are 10 slots of 20 bytes (5 dwords) at bank+173560 (0x450dd90 for slot 0).
- Slot 0 fields:
  - +0 active: 1
  - +4 item id: 5 (scarves)
  - +8 days left: 3
  - +12 s16: 0 (the orderee index)
  - +14 s16: 2 (customer / name id; it reads Louie)
  - +16 quantity: 2
- `sub_45E67E` takes 1 off "days" each morning, only while the slot is active. Here it went 3 → 2 (f1682) → 1 (f3703) → 0 (f5507).
- The page text comes from `sub_46094A`: "by tomorrow" when days is 1, otherwise "in %d days". Days = 0 shows "Today".
- **The slot is cleared when the store opens, not when the customer is served** (all_ida.c:63057; I saw it live). The roster build turns every active slot with days ≤ 0 into a queue record with kind 3 (days == 0, on time) or 4 (days < 0, late). Then it sets every such slot's active flag to 0.
  - Here `ordact` went 1 → 0 at f5833, the same tick cc08 went 10 → 4.
  - Afterwards the slot keeps days 0, item 5 and qty 2; only active is 0. The morning decrement skips inactive slots, so days never goes negative.
  - So if the session ends before the orderer comes up in the shuffled queue, the order is lost anyway. That is from the code; I didn't see it happen.
- The day-18 queue (0x730AC98) had 3 records: {2, 0, kind 3, item 5, qty 2, days 0}, {11, 8, 0}, {15, 16, 0}. The serve order (0x730B1A8) was 1, 0, 2.
- `b5a8` mapping, from all_ida.c:66015: queue kind 2 (new order) → `b5a8` 3; kind 3 (on-time pickup) → 4; kind 4 (late pickup) → 5; normal customers here read 2. Louie came in with `b56c` 2, `b5a8` 4.

**Customers in the due-day session** (day 18, stime 0 → 1)
1. **Story event:** dialogue with someone the game calls "a fairy… interesting". I'm fairly sure this is Caillou's intro (his row appears on day 19). Skipped with `~` → Yes at f7243.
2. **Customer 11** bought a Magician's Staff for 1,200.
3. **Louie** (f7726–8359). His lines:
   - Recette: "Welcome! / Can I help you with anything?"
   - Louie: "Hey! Came to get my scarves!"
   - A hand-over picker opens (`b534` 3), with the label "scarves", slots "1:" / "2:", "All set!" and an item list ("Nothing", Steel Sword, …).
   - I moved the cursor up to "All set!" with both slots empty and pressed `A`. `b534` went 17 and Recette said: "We're, uh, a little understocked on that..."
   - Louie: "Well, if you don't have any, you don't have any."
   - Recette: "... I made them mad, didn't I..."
   - He left (`b534` 18 → 11 → 0).
4. **Customer 15** bought something for 2,800. The session ended at f8891.

**What a missed order does**
- Gold did not change on Louie's visit (455,830 before and after).
- His reputation word, s16 at bank+184848 + 4·orderee index (index 0, value 7), did not change. I named this `rep2`, though it is the index-0 word.
- In the code, b534 17 only resets the combo `0x730B5C4` to 0, and `sub_460EBA` gives back any items placed in the picker. I saw no other penalty.
- Day 19 (step 4): Louie's row is off the page, and no late visit is possible because the slot is already inactive. So I didn't open another session.

**Cheap way to pass a period**
- From the home arrival spot, the input is locked for about 44 ticks. Then: `R*70 A` at the door, `.*36 R .*3 A` (Merchants' Guild), `.*85 U .*6 A` (Leave), `.*44 D .*3 A` (Home). That is 271 ticks per period; stime goes up by one on reaching home.
- Visiting two places in one trip still costs only one period.
- The Market is closed in the evening; the Merchants' Guild stays open.
- Leaving the shop needs `A` at the door, at (3.0, −3.24) facing +x.
- At the end of a day: `rd tap A --gap 10` until the day dword changes, then wait about 200 ticks for the title card before moving.

**State I named** (this run): `orddays` (s32 0x450dd98), `ordact` (s32 0x450dd90), `rep2` (s16 bank+184848, the index-0 reputation word), `b5a8`, `b56c`, `b534`. I'd share `ordact`/`orddays` and a general `ordslot(i)` = bank+173560+20·i.

**Tool gaps**
- On Tear's page and during customer service, `rd text` repeats the news-ticker rows on every tick, so the transcripts are mostly noise. A filter for rows drawn at y = 52 would help.
- There is no shared variable for the title-card or arrival input lock. Fixed waits and searches had to work around it, and some searches spent time on locked ticks.
- Searches to the door return long paths, for example 399 frames once.
