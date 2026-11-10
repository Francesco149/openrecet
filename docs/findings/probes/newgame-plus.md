# Probe: newgame-plus

- **Run:** `newgame-plus` · **start:** save:c062daff8848 · **task:** `tools/drive/tasks/newgame-plus.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 47 turns, $0.63, 5 min
- **Scenario:** `tests/scenarios/newgame-plus/` (1372 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/newgame-plus.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `newgame-plus`: New Game + works and is verified.** The movie is 1,372 frames from save:c062daff8848. `rd check` gives PASS (step 1 "at the title" at f61, step 2 "in the game with control" at f476), and `rd verify` gives MATCH (rng 390eae24). The new save is in slot 007; its endsave sha is **f3dd0ba9294b** (`rd new X --from save:f3dd0ba9294b`).

Step 2 passes at f476, on the first tick in the shop. A tick later (by f486) script 2/6 takes over (dlg 1). The real first moment of control after that dialogue is f1134.

**(1) The title's entries.** The cursor list is at 0x9643358 + 4i and wraps at both ends. At boot the cursor is on LOAD GAME (t_cur 3, t_item 1).

| t_cur | t_item | Entry | Help balloon |
|---|---|---|---|
| 0 | 5 | NEW GAME | |
| 1 | 4 | NEW GAME + | "Start a new game with your items and levels" |
| 2 | 6 | SURVIVAL MODE | |
| 3 | 1 | LOAD GAME | |
| 4 | 7 | ITEM ENCYCLOPEDIA | |
| 5 | 8 | SURVIVAL SCORE | |
| 6 | 2 | OPTIONS | |
| 7 | 3 | EXIT | |

The new entries are NEW GAME + (4), SURVIVAL MODE (6) and SURVIVAL SCORE (8). I did not compare against a plain save's list, so I don't know whether ITEM ENCYCLOPEDIA (7) is new. Pictures I looked at: the title menu at f156 and the NG+ picker at f389 / f430.

**(2) What each new entry asks:**
- **SURVIVAL MODE:** A opens a scroll list with "Survival Hell" (first, under the pointer) and "Normal Survival". t_sub stays 0. B backs out.
- **SURVIVAL SCORE:** t_sub becomes 4 (the records page). It shows "Record End-game Score" 7308 pt, "Record End-game Money" 400000 pix, "Survival Hell Record" Day --, and "Normal Survival Record" Day --. B backs out.
- **NEW GAME + (f363):** t_sub becomes 1. A picker titled LOAD GAME shows the message "Please choose a file for New Game+ data."
  - The first A (f390) only closes the message.
  - Only slot 001 is listed: Endless, Cleared stamp, 400,000 pix, Day 37, Merchant Level 6, SCORE 7308, LOOP 1, TIME 2:55:58.
  - The in-game save picker shows slots 002–006 and 008–011 holding data (score 0, or 440 in 009–011), and none of them are offered. So NG+ seems to list only cleared / Endless saves. That is inferred from this one case.
  - The second A (f431) starts at once: there is no further prompt or Yes/No. t_start counts up by 1 per tick to about 45, and at f476 scene is 1, cc08 1, day 1.

**(3) The new loop.** None of the first playthrough's prologue plays: no title-card scenes, no debt-collection opening.
- Script 2/6 runs in the shop from about f486. It shows "ESC Key: Event Skip"; I advanced it with A instead.
- It is Recette and Tear's short "what do I do now" talk, about 600 ticks against about 2,900 for a first-run prologue. Lines: "So! I have an item shop! And stuff! And it's all cool and awesome!" / "... So, uh, what do I do now?" / Tear: "That is up to your judgment. If you sit at the counter, we can open immediately, or we can re-organize the shop, or we could even go search for new stock around town... or elsewhere." / "Yikes, that's a ton of options... how do I choose?" / "It is as I said earlier. Running a place like this is more of an art than it is something you can simply follow a map through. Simply pick something. We will deal with what comes." / "Well, okay..."
- Control comes at f1134 (dlg 0, cc08 1, Recette at (−0.3, 9.35), Tear at (0.6, 9.35)).

**(4) State with control:**
- gold 1000, day 1 (HUD "Day 2"), mlevel 5 (HUD Merchant Level 6, XP bar part-filled), items 41 (was 33), d_table 0 (Wooden; it was also 0 before).
- The pause main page (the "Status" page: gold, level, calendar) shows debt "10,000 pix Payment Due!" circled on day 8. There is no party list on it, and I did not open a page with adventurers' levels.
- Pause entries by pm_sel: 0 Status, 1 Items, 2 Encyclopedia, 3 Options, 4 Save, 5 Exit Game.

**What the bank diff shows.** I diffed the bank (0x44e3798–0x4511760) from the picker (f389, where the bank already holds slot 001) to f476. Only 86 dwords changed, so everything else in the save carried over. That should include adventurers' levels, but I did not locate them, so that part is unconfirmed.

Reset or changed:
- bank dword 3 and dword 0xb0f6: gold 400000 → 1000
- 0xb0f7: score 7308 → 0
- 0xb0fb: day 36 → 1
- 0xb0f9: 0 → 1 (looks like an NG+ / loop flag)
- 0xb0fa: 0 → 7
- 0xb3c0, 0xb3c2, 0xb3c3: 5, 3, 2 → 0
- 0xb759: 3 → 1
- About 60 byte-flag dwords at 0x450f3e4..0x450f5e0 cleared (most likely event / story flags); 3 dwords in the same range went from 0 to 257 / 0x01000000 / 257.
- 0x450ad60..0x450ae48: 27 dwords went from −1 to 0.
- 0x450de58: 0 → 3.
- 8 item codes (96192, 192832, 192, 12928, 25728, 25792, 198786, 192128) moved out of bank dwords around 0x4e28 (0x44f7038..) into bank dwords 0x27–0x2e. items rose 33 → 41, so they look like items returned to the inventory list.

Kept: mlevel, the items, d_table.

**(5) Saving.** Pause → Save (pm_sel 4) → slot 007 (NO-DATA, 6 presses of D) → A. It writes at once with no confirm. The slot then reads "LOOP 2, TIME 2:56:12, New Game+", so play time carries over. I closed the pause menu at f1372 and filed the endsave.

**Frames:** title f0–f430; Survival list opened f163 (closed f203); Survival Score f273–f293; NG+ picker f363, slot chosen f431; shop f476; script 2/6 f486–f1134; pause f1135; saved about f1290; back in game f1372.

**Checkpoints:** title (f156), picker (f389), ngp_start (f536).

**Tool gaps:**
- There is no name for the title's item list or entry count, or for the in-game save picker's cursor.
- `rd text` cannot tell the Survival list's cursor: both rows draw in grey.
- A named var for the debt amount and due day, and for adventurer levels, would help check carry-over.

Port note: I could not tell from state whether `FUN_004864fd`'s NG+ arm ran; d_table was 0 both before and after.
