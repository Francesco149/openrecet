# Probe: order-customer-week3

- **Run:** `order-customer-week3` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/order-customer-week3.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 185 turns, $3.28, 23 min
- **Scenario:** `tests/scenarios/order-customer-week3/` (10139 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/order-customer-week3.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `order-customer-week3`** (from save:64d43650865f) is 10139 frames long. `rd check` gives **PASS** on all 4 steps with no forbid hit, and `rd verify` gives **MATCH**. I looked at a strip of f9181–9221, which shows the Accept Order / Refuse box and then "Than…" after accepting, and at f10139, where the Advance Order Summary page shows the order row.

Both halves were possible on this save. Merchant level was 5 at the start. I raised it to 6 during the day's third session, so the fourth session got an order customer and Tear's menu got entry 9.

**Step frames**
1. Store open (cc08 4): f264.
2. Order slot active: f9216. The order was accepted with the `A` at f9202, and b534 went 15→10 on the same tick the slot was filled.
3. Free roam after the session (cc08 1, dlg 0): f9433. That is the end of day 15 (stime 3→4).
4. Outstanding Orders page open (cc08 17, tentry 9, cc0c 4): f10129. This is the next morning (day dword 16, HUD Day 17).

**Key finding: an order needs merchant level ≥ 6.**
- The roster scan (all_ida.c:62825) adds an order entry to the session queue only if:
  - `(bank dword 46937 ∈ {2,3} or 35 − day > 1)`, and
  - `730B5E8 == 0`, and
  - `(rand & 1 and 730B5FC == 0 and mlevel dword 45312 ≥ 6)`, or the byte `bank+179468` is set.
- So on the level-5 save, no session can ever produce `b5a8 == 3`. You have to level up first, then open the store again, because the queue is built when the store opens.
- The queue is laid out like this:
  - Count is at `0x730AC98`.
  - Records are 24 bytes each, starting at `0x730ACA0`: customer id, orderee index, kind, item, days, qty.
  - Kind values: 0 = normal, 2 = order, 3/4 = an order coming back (on time / late).
  - The shuffled order the records are served in is at `0x730B1A8`. The current position is `0x730B318`.
- In session 4 the queue was a single record `{13, 11, kind 2, item 3, days 3, qty 2}`, and it came up on the first try.

**How merchant XP works**
- Each sale adds `bank dword 45309` (XP) toward `dword 45311` (the next level's threshold; it was 1350 for level 6). This is done by `sub_4606FC`.
- Base award per sale, set by `sub_460672` comparing the price with the customer's target `0x730B588`:
  - 30 if within ±0.5% of the target;
  - 15 if within ±5%;
  - otherwise 10.
- Combo bonus on top: 2, 4, 8 … (capped at 128), counted in `0x730B5C4`. It only grows when the sale was accepted on the first offer (`b584 == 1`), and any pushback (`b534` 8) resets it.
- The customer's maximum is `0x730B574`. So the reliable method is: at `b534 == 6`, read `b588` and type exactly that price. Every such sale was accepted on the first offer and gave 30+ XP.
- XP went 1179 → 1350 over 9 sales, and the level changed to 6 at f7595.

**Price entry:** U/D change the digit under the cursor and L/R move the cursor. The cursor stays where it was left after a pushback.

**Customers (b5a8 / b56c)**
- **Session 1** (stime 0; the day's first event was skipped with ESC):
  - 1/15: a request ("some food"), sold a Beef Bowl at 2400.
  - 2/15: Very Odd Vase at 22400.
  - 2/15: Big Shield. It was pushed back at 2800 and at 2500 (its max was about 1938), then sold at 1900.
  - 2/11: sold at 4400.
- **Session 2:** 2/11 (1555, perfect) and 2/9 (1936, perfect).
- **Session 3:** 2/15 (1212), 2/20 (4362; level 6 reached), 2/15 (1405).
- **Session 4:** a level-up event and a story event, both skipped with ESC. Then **3/13 = the order**.
- That makes **9 customers before the order; the order customer was the 10th**.

**The order dialogue (`rd text`)**
1. "Welcome!"
2. "Would it be possible for / you to have / 2 bracelets"
3. "ready in 3 days?"
4. Recette: "So they want / 2 bracelets... / in 3 days... Should I?...", with the buttons "Accept Order" / "Refuse". The cursor `0x730B540` starts at 0, which is Accept Order.

The order machine's b534 went 1 → 2 → 7 → 15 (the choice) → 10 → 12 → 0.

**The slot after accepting** (slot 0 at bank+173560 = 0x450dd90):

| Field | Value |
|---|---|
| active | 1 |
| item id | 3 (name at 0x6A5DB98+76·3+32 reads "bracelets") |
| days left | 3 (2 the next morning) |
| s16 at +12 | 11 (the orderee index) |
| s16 at +14 | 13 (orderee name "Woman", at 0x6A5EA94+181872·13) |
| quantity | 2 |

Slots 1–9 are empty.

**The page:** it shows "No Deadline / Caillou / Charred Lizard, Bat Wings / Slime Liver" and then "in 2 days / Woman / 2 bracelets".
- The Caillou row has appeared since the earlier probe. Its flag was probably set by one of the events I skipped; I did not check which one.
- The page row reads "Charred Lizard, Bat Wings / Slime Liver", whereas tear-outstanding-orders.md wrote it as "Charred Lizard Bone, Slime Liver".

**Other facts**
- On this save the counter menu is cc08 0, not 10; cc08 10 is the 15-tick dispatch after pressing `A`.
- Every store opening costs one time period, so there are 4 sessions per day (stime 0..3), and the day ends straight after the 4th.
- After the level-up, Tear stands next to the counter. The usual walk then opens her menu instead of the counter's; `rd search` found `L*6 A` to reach the counter.
- At the end of the day: a bubble dialogue, then Daily Profits (scene 9), then a "Day N" title card that blocks input for about 200 ticks. After that, `R*2 A` reaches Tear in the morning.
- `rd text` shows nothing for the ESC-skippable story events, and pressing `A` for 2100 ticks did not end the first one.

**Names worth sharing** (defined in this run):
- `b5a8` = s32 0x730b5a8, `b56c` = s32 0x730b56c, `b534` = s32 0x730b534.
- `ord0` = s32(bank+173560).
- Further leads, not defined as vars: `0x730B574` (customer max), `0x730B588` (perfect-price target), `0x5C6BB8` (current ask), `0x730B5C4` (combo), `0x730AC98` / `0x730ACA0` (session queue), bank dword 45309 / 45311 (XP / next threshold).

**Tool gaps**
- Search cells cannot see a timed lockout like the Day title card, so search reports "one cell" until the card ends.
- There is no way to dump a struct array in one `eval`.
- The `rd tap … --until` loops wasted ticks waiting on "Thanks!" lines, so the movie is longer than it needs to be.
