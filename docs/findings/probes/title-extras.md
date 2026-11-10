# Probe: title-extras

- **Run:** `title-extras` · **start:** save:c062daff8848 · **task:** `tools/drive/tasks/title-extras.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 56 turns, $0.87, 4 min
- **Scenario:** `tests/scenarios/title-extras/` (2568 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/title-extras.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical title-extras                         ticks 1..2868

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `title-extras`** (start save:c062daff8848): 2568 frames. `rd verify` gives MATCH. `rd check` gives **FAIL**: steps 1 and 2 pass, but step 3 can never pass from the title. Every page was opened, read and closed, no game was started, and scene was 0 the whole time.

**Why step 3 fails.** `t_sub` (0x9643524) is not cleared when a page closes. In the decompile the only `dword_9643524 = 0` is in sub_49A3A3, the title's init, and that runs only when the title scene is loaded (at boot, or on a return to the title from a game). Closing a page with B only fades it out: `t_fade` (0x9643520) goes 10→0, and at 0 the main menu takes input again. `t_sub` keeps its value: 3 after the Encyclopedia, 4 after records, 2 after Options, 1 after the picker. The run ends with the picker closed, the main menu working, `t_cur` 3, `t_fade` 0 and `t_sub` 1. The task should test "back on the main menu" as `scene == 0 and t_fade == 0` instead.

**Step frames:**
- Step 1 (at the title) at f61.
- Step 2 (a sub-page is open) at f106. That is the Encyclopedia: D at f81 (`t_cur` 3→4), then A.

**Timeline:**
- f81–132: Encyclopedia opens.
- f132–154: one R and one D; the ram diff was taken around them.
- f154–649: A×33 walks all 33 categories and wraps back to 0.
- f650–665: C opens the detail box; C closes it again.
- ~f681: B closes the page. I first misread this because `t_sub` stayed 3.
- f789 D; f809 A opens SURVIVAL SCORE (`t_sub` 4). B closes it at f863.
- f898 D; f918 A opens OPTIONS (`t_sub` 2):
  - Sound 9→8→9.
  - D×6 walks the rows and wraps.
  - B at f1015.
- U×3; f1087 A opens LOAD GAME (`t_sub` 1). Checkpoint `picker` at f1113.
- f1113–1784: scrolled to the end of the list.
- f2161–2527: walked back up in steps of 4 slots.
- f2528: B closes the picker.
- Checkpoints: `enc_open` (f763) and `picker` (f1113).

**Pictures I looked at:** f132 (Encyclopedia Swords grid), f836 (records), f946 (options), f2176, and a strip of the picker from f2176 to f2527 (every 39 frames).

**Which page is open (`t_sub`) and the cursors:**
- `t_sub` values: 1 = LOAD GAME picker, 2 = OPTIONS, 3 = ITEM ENCYCLOPEDIA, 4 = SURVIVAL SCORE. It is set when the page opens (`t_fade` takes about 5 ticks to go 0→10) and stays stale after it closes.
- `t_fade` (0x9643520) is the fade: 10 = a page is fully in, 0 = the main menu takes input.
- Main-menu cursor: `t_cur` / `t_item` as in newgame-plus.md (3 LOAD GAME, 4 ENC (item 7), 5 SCORE (8), 6 OPTIONS (2)).
- Picker: `t_pcur` (0x9643530) is the slot cursor, 0..99. A new name, `t_ptop` = s32 0x9643534, is the window's top slot (defined in this run only). The visible window is 3 slots.
- OPTIONS: `t_pcur` is reused as the row, 0..5, wrapping. The values are:
  - Music s32 0x56E5778 (0..9)
  - Sound 0x56E5774 (0..9)
  - Voice 0x56E577C (0..9)
  - Message Speed 0x56E5784 (0..2)
  - Unread Text Skip word 0x56E5782 (0..1)
- SURVIVAL SCORE: no cursor. D/R/C change nothing in 0x9600000+.

**ITEM ENCYCLOPEDIA** (it is the in-game page's code, sub_49F365):
- The `enc_*` variables drive it. One R set `enc_col` 0→1 and one D set `enc_row` 0→1; those were the only changes in 0x964xxxx apart from the timers.
- Buttons:
  - A = next category (`enc_cat` goes 0..32, then wraps to 0).
  - C = detail box on/off (`enc_detail`).
  - B = close (input bit 0x20, SE 317).
  - X (button D) did nothing here (in-game it is "previous category").
- **The counts differ from the in-game page:** seen 538 / total 538 (s32 0x964C420 / 0x9643688), 33 categories, "Completion Rate 100%". In-game it was 65 / 538. The title page shows every item.
- `enc_rows` per category 0..32: 5 ×17 (cat 0–16), 4, 4, 4, 5, 4, 16, 16, 11, 3, 12, 4, 5, 5, 1, 3, 3.
- Text: heading "Swords"; the grid Worn Sword, Longsword, Steel Sword / Gilded Sword, Knight's Blade, Breaker Sword / Crystal Sword, Demonbane, Tuna Cutter; help "A worn-out, dented, chipped sword. Still better than going into the wild bare-handed, though. Price- 200".
- Detail box: "Worn Sword (Equippable) / Type Swords / Effect ATK+8 / Base Price 200 pix / Highest Sale Price ---- / Lowest Sale Price ----".

**SURVIVAL SCORE:** "Record End-game Score 7308 pt / Record End-game Money 400000 pix / Survival Hell Record Day -- / Normal Survival Record Day --". B closes it.

**OPTIONS:** Music 5, Sound 9, Voice 9, Message Speed MED, Unread Text Skip OFF, and a "Clear Save Data" row (never pressed). Left/right change the value under the cursor (SE 326). B closes it.

**LOAD GAME slot list** (100 slots, 001–100; day, pix and Merchant Level are sprites read from the pictures, score and time from text; LOOP is 1 everywhere):

| Slot | Day | Pix | Level | Score | Time | Note |
|---|---|---|---|---|---|---|
| 001 | 37 | 400,000 | 6 | 7308 | 2:55:58 | Endless, Cleared stamp |
| 002 | 1 | 790 | 1 | 0 | 0:02:21 | |
| 003 | 2 | 155 | 1 | 0 | 0:11:05 | |
| 004 | | | | 0 | 0:03:42 | |
| 005 | 1 | 790 | 1 | 0 | 0:03:19 | |
| 006 | 1 | 790 | 1 | 0 | 0:02:21 | |
| 007 | | | | | | NO-DATA |
| 008 | | | | 0 | 0:15:08 | |
| 009 | 5 | 4,664 | 3 | 440 | 0:24:23 | |
| 010 | 6 | 3,554 | 3 | 440 | 0:31:29 | |
| 011 | 5 | 4,384 | 3 | 440 | 0:36:31 | |
| 013 | 8 | 0 | 3 | 1300 | 1:09:58 | |
| 014 | 9 | 7,550 | 4 | 1736 | 1:17:25 | |
| 015 | 9 | 0 | 4 | 1736 | 1:30:57 | |
| 017 | 14 | 14,658 | 5 | 4532 | 2:05:01 | |
| 018 | 15 | 2,606 | 6 | 4542 | 2:02:48 | |
| 019 | 15 | 500,000 | 6 | 4542 | 2:16:03 | |
| 021 | 17 | 478,930 | 6 | 5608 | 2:21:39 | |
| 022 | 18 | 478,930 | 6 | 5908 | 2:23:54 | |
| 023 | 19 | 478,930 | 6 | 6008 | 2:25:11 | |
| 025 | 21 | 478,930 | 6 | 6408 | 2:28:10 | |
| 026 | 22 | 478,930 | 6 | 6508 | 2:29:31 | |
| 027 | 23 | 398,930 | 6 | 6508 | 2:30:55 | |
| 029 | 25 | 398,930 | 6 | 6508 | 2:34:27 | |
| 030 | 26 | 398,930 | 6 | 6708 | 2:36:36 | |
| 031 | 27 | 398,930 | 6 | 6708 | 2:37:37 | |
| 033 | 29 | 398,930 | 6 | 6708 | 2:39:41 | |
| 034 | 30 | 900,000 | 6 | 6708 | 2:40:59 | |
| 035 | 31 | 900,000 | 6 | 6808 | 2:42:08 | |
| 037 | 33 | 900,000 | 6 | 6808 | 2:44:13 | |
| 038 | 34 | 900,000 | 6 | 6808 | 2:45:09 | |
| 039 | 35 | 900,000 | 6 | 6808 | 2:46:02 | |
| 040 | | | | | 2:47:07 | |
| 041 | | | | | | NO-DATA |
| 042–100 | | | | | | number only, no text |

- Every 4th slot (012, 016, 020, 024, 028, 032, 036) was off-screen in the pictures and its text was cut from the transcript, so I have no values for those. Slot 004 had only text, no picture.
- Only slot 001 has a mode tag (Endless) and the Cleared stamp. Nothing was loaded.

**Tool gaps:**
- Nested groups like `((U .*5)x4 .*15)x9` crash `parse_spec` with a traceback ("substring not found").
- The `rd tap --text` transcript of 45 picker pages was cut in the middle (22 KB lost). A compact or diff-only text mode, or a "text at frame F" query, would help.
- A `t_ptop` variable and a corrected `t_sub` note ("stale after close; use `t_fade`") should go into the shared vars.json.
