# Probe: order-late-pickup

- **Run:** `order-late-pickup` · **start:** save:44e9baea3987 (played, day dword 15) · **task:** `tools/drive/tasks/order-late-pickup.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (cutestation via `win-wifi`)
- **Scenario:** `tests/scenarios/order-late-pickup/` (8747 frames)
- **Verdicts (2026-11-19):** check PASS (5/5 with the predicate fix below; the task as written also says PASS, but
  its step 4 matches one tick after step 3, before Louie speaks); verify --real MATCH (fresh headless, live session
  and real device all f8747 rng d8bac981). No golden / triage run.

## Step frames

| step | frame | note |
|---|---|---|
| loaded with control | 137 | day 15, stime 2 |
| order active, days < 0 | 6954 | morning of day dword 19 (HUD Day 20): `orddays` 0 → −1, `ordact` 1 |
| shop open, late pickup serving (`b5a8` 5) | 8458 | Louie walks in, `b56c` 2 |
| (fix) line 18 on screen (`b534` 7) | 8590 | |
| visit over (`b534` 0 after 7) | 8747 | movie end |

**Predicate fix (copy, not applied to the task file):** step 4 `cc08 == 4 and b5a8 == 5 and b534 == 0` is already
true at f8459 (`b534` is 0 while the customer walks in), so it does not prove the line was said. The copy inserts
`the late customer's line 18 on screen (b534 7): cc08 == 4 and s32(0x730b5a8) == 5 and s32(0x730b534) == 7` before
it; with that, step 5 lands at f8747.

## The order slot, morning by morning (slot 0, `bank + 173560`)

Shop never opened on days 15–18 (each period passed with the town round trip of `order-deadline-missed`:
door → Merchants' Guild → Leave → Home; stime 4 = "Thanks for all your help today, Tear!", tap A to the next day).

| morning (day dword) | frame | `orddays` (+8) | `ordact` (+0) |
|---|---|---|---|
| 15 (load) | 137 | 3 | 1 |
| 16 | 1237 | 2 | 1 |
| 17 | 3142 | 1 | 1 |
| 18 | 5048 | 0 | 1 |
| 19 | 6954 | **−1** | 1 |

The decrement (`sub_45E67E`) does go below 0 while the slot stays active: the premise holds.

## Opening the store on day 19

- Counter menu → Open Store `A` at f7268; f7288: `cc08` 10 → 4, **`ordact` 1 → 0** (the roster build clears the slot
  on open, as in the earlier probe), `b5a8` 0 → −1. `orddays` stays −1 in the inactive slot.
- Queue at 0x730AC98: count 3; Louie's record reads {orderee 0, customer 2, 0, **kind 4**, item 5, qty 2, days −1};
  the other two records (not decoded field by field) are the two customers served first: customer 15 (`b5a8` 3,
  new order) and a normal buyer (`b5a8` 2; `b56c` was not re-read for it). Serve order (0x730B1A8): 1, 2, 0 — Louie last.
- f7334: a story event in the shop (`dlg` 1: "Welcome to Recettear!" / "Huh." — the event the earlier probe skipped
  on day 18; it waits for the first shop session). Skipped `~` → Yes at f7587.
- Customer 15 (`b5a8` 3, f7597): placed a **new order** — A-mashing accepted it, and it took slot 0 (f7881: `ordact`
  0 → 1, `orddays` −1 → 3; slot now item 6, days 3, +12 = 16, +14 = 15, qty 2). Then the normal buyer (`b5a8` 2) bought
  for 2,800 (f8290, gold 454,630 → 457,430).

## Louie's late visit (`b5a8` 5, `b56c` 2)

`b534` sequence (`rd scan`, changes only): f8458 0 (walk-in) → f8549 **1** → f8589 **2** → f8590 **7** → f8680 **11**
→ f8746 **13** → f8747 **0**. `0x730B5C4` (combo) 1 → 0 at f8681.

Lines (`rd tap --text`):
- Recette: "Welcome!" / "Can I help you with anything?"
- Louie: "Recette! I had an order, remember?" / "Try to be here next time, okay?" (picture f8650: Louie's portrait,
  name box "Louie")
- Recette (after he leaves): "... Well, they went home..."

No picker, no hand-over. Before (ck `louie`, f8458) vs after (f8747): gold 457,430 = 457,430; items 31 = 31;
reputation words s32 at bank+184848 / +184852 / +184856 / +184860 = 7 / 4 / 0 / 0 both times. **No gold, item or
reputation change**; the only side effect seen is the combo reset.

## Notes

- The day-end save prompt (scene 9) was passed with A taps; no save.dat was committed (`rd endsave`: none).
- A shop-open session can overwrite the cleared slot 0 with a new order the same day (kind 2 customer), so the late
  slot's data is gone by the time the late customer arrives; the queue record carries it.
- Door from the morning start spot (counter, (−0.3, 9.35)): input is locked ~210 ticks after the day card; the
  searched path `R*4 .*8 UR*8 U*24 B .*7 B .*7 B .*7 UL*32 U*4 UR*18 R*4 . A` then reaches the map.
