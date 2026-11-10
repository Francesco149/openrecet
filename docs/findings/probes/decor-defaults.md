# Probe: decor-defaults

- **Task:** `tools/drive/tasks/decor-all.md` · **start:** save:175794a5 (crafted: gold, level dword 13)
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 32 turns, $0.36, 2 min
- **Scenario:** `tests/scenarios/decor-defaults/`
- **Verdicts (2026-11-10):** check PASS; verify MATCH; port triage IDENTICAL f1–835; only default variants owned

The driver's report, as written (retail facts observed live through `rd`; treat code-read claims marked as such as leads):

---

The trace passes the task's check and replays to the same state on a fresh process, but it only partly covers the probe. In this save every decor kind offers only its default variant, so the editor never showed a different preview and no confirm changed the room. What I could cover was opening all four kinds, confirming three, cancelling one and returning to free roam.

**Run:** decor-all, 535 frames, start save:175794a5f61c. `rd check` prints PASS and `rd verify` prints MATCH (frame 535, rng c9ff9bd2).

**Steps**
- f92 save loaded (`.*60 A .*30 A`); in game at f137.
- Morning event f138: `.*20 ~ .*20 A` did not take, because the A came before the Yes/No box was ready. One more `A` skipped it, dlg 0 at f190.
- The card's walk `.*30 DL*16 U*8 UR*16 L*6 A` reached the counter. The counter-menu list was up at f277 (**task step 1**).
- Walls, menu cursor 1: cc08 10 at f280, 23 at f295 (**task step 2**), 3 at f296. Cancelled with B, back at the menu f327.
- Flooring, cursor 2: cc08 23 at f345, 3 at f346. Pressed D then U (nothing to move to), confirmed with A at f372.
- Carpet, cursor 3: cc08 23 at f400, 3 at f401. L/R does not switch the item window to another category. Confirmed at f441.
- Tables, cursor 5: cc08 23 at f471, 3 at f472. Confirmed at f494.
- B on the counter menu: free roam (cc08 1, dlg 0) at f505 (**task step 3**). Movie ends at f535.

**Game facts**
- **List A in this save:** count 7, `0x74b2d98[] = [0, 2, 1, 3, 5, 4, 6]` (Open Store, Walls, Flooring, Carpet, Move Tables, Tables, Cancel). So the menu cursor values are Walls 1, Flooring 2, Carpet 3, Tables 5.
- **Timing:** cc08 goes 10 → 0x17 15 ticks after A, and 0x17 → 3 on the next tick (b1d4 = 1 on the 0x17 tick). Each kind took one tick at 0x17.
- **Variants offered (from `rd text`; window title / the only row):**
  - Wallpapers / "Lacquer Walls" (item 5101)
  - Flooring / "Plank Floor" (5001)
  - Carpets / "Red Carpet" (5401)
  - Counters / "Wooden Counter" (5301; the Tables kind is titled "Counters")
  - Each row says Base Price 500 and "Number possessed- 1". The bag held no other decor items (no other 5001..5499 ids).
- **Current-variant variables:** wall at bank+0x2cde4, floor +0x2cde8, carpet +0x2cdec, tables +0x2cdf0. All stayed 0 the whole run; no preview variable could change with one row.
- **Bank bytes on open + confirm / cancel:** bank = 0x44e3798, diff over 0x44e3798..0x4511760. The same 3 dwords changed for every kind, whether confirmed or cancelled:
  - bank dword 2 (0x44e37a0): a counter that ticks, probably play time.
  - bag slot at 0x44e3830 (dword 0x26): goes from `(id<<6)` to −1. The room's decor is put into the bag when the editor opens and taken out again when it closes.
  - items count at 0x450f2b0 (dword 0xaec6): 33 → 32.
- **Cost:** none, gold stayed 9,999,999.
- **What cancel restores:** with only the default on offer, cancel and confirm leave the bank identical. The difference (the doc's result 2 restoring b7b4.. plus SE 0x13d) can't be seen with one variant.
- The HUD shows Day 15 while `day` = 14 (the bank value is 0-based).

**Pictures:** I looked at f316 (Wallpapers window over the room, camera turned toward the wall), f416 (Carpets, camera over the carpet), f487 (Counters, camera turned toward the table area) and f535 (shop afterwards: plank floor, red carpet, standard walls and counters). I also took f361 (Flooring) but did not open it. I took single frames rather than contact strips; the camera easing between frames is not covered.

**Run-local names:** wall / floor / carpet / tables (the expressions above, flagged `--status`).

**What the probe still needs and the tool lacked:**
- A save that owns extra decor variants. The crafter has no "add decor items" option; something like `--add-item 5102` would let the preview and confirm paths actually be exercised. Buying them at the market would make the movie much longer.
- No variable for the item-window row cursor was found, since nothing could move with one row.
- `rd text` cannot show which confirm/cancel result happened, because SE ids are not visible as state.
