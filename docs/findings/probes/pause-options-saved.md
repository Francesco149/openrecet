# Probe: pause-options-saved

- **Run:** `pause-options-change` · **start:** end:dungeon3-run2-save · **task:** `tools/drive/tasks/pause-options-change.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 30 turns, $0.40, 3 min
- **Scenario:** `tests/scenarios/pause-options-saved/` (419 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/pause-options-change.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

I recorded the run `pause-options-change` (from end:dungeon3-run2-save, 419 frames). `rd check` gives PASS and `rd verify` gives MATCH. I changed all five Options settings, left the page with B so the change was kept and save.dat was written, re-opened the page and saw the new values, then went back into the game. I looked at the strip `runs/pause-options-change/view/strip_267_395.png` (frames 267–395, one picture every 32 frames). It shows the page with the old values (Music 5 / Sound 9 / Voice 9 / Message Speed MED / Unread Text Skip OFF), the edits as they happen, and the re-opened page with Music 7 / Sound 6 / Voice 7 / FAST / ON.

**Inputs and step frames**
- **Load (f0–137):** `.*60 A .*30 A` loads the slot; the house is up at f137. Unlike the card, **no morning event came up**: `dlg` stayed 0, and a picture at f592 showed normal free roam. I rewound to f200.
- **Step 1, pause menu up (f204):** `~` gives `scene == 9`.
- **Step 2, Options open (f267):** I pressed D three times to reach `pm_sel` 3, then A. `pm_state` runs 0→2→…→10 over about 12 ticks, so `pm_type == 2` and `pm_state == 10` hold at f267.
- **Settings changed (f271–343):** Music 5→6→7→8→7 (R R R L), Sound 9→6, Voice 9→7, Message Speed 1→2, Unread Text Skip 0→1. An extra R on Skip while it was at 1 did nothing.
- **Left with B (f352):** the dirty flag goes 1→2. On f353 the save is written, the flag resets to 0, and `pm_state` counts back down to 0 by f363.
- **Re-opened (A, `pm_state` 10 at f391):** the values had stuck. Leaving with A without changing anything takes the no-save path (the flag stays 0).
- **Step 3, back in the game (f419):** B closes the pause menu, giving `scene == 1` and `dlg == 0`.
- Checkpoints: `opts_open` (f267) and `changed` (f351, dirty, page still open).

**Variables I named (this run's vars.json)**
- `o_bgm`, `o_se`, `o_voice`, `o_speed`: s32 at 0x56e5778, 0x56e5774, 0x56e577c, 0x56e5784.
- `o_skip`: s16 at 0x56e5782.
- `o_phase`: s32 at 0x74b2890.
- `o_row`: `s32(0x74b2834 + 4*s32(0x74b288c))`.

These are worth adding to the shared vars.json.

**The Options page (sub_47FC44, while `pm_state == 10`)**

| Row | Label | Variable | Header dword | Range, step 1 |
|---|---|---|---|---|
| 0 | Music | 0x56e5778 | 2 | 0..9 |
| 1 | Sound | 0x56e5774 | 1 | 0..9 |
| 2 | Voice | 0x56e577c | 3 | 0..9 |
| 3 | Message Speed | 0x56e5784 | 5 | 0..2 (1 = MED, 2 = FAST, 0 is presumably slow) |
| 4 | Unread Text Skip | u16 at 0x56e5782 | high half of dword 4 | 0..1 (OFF / ON) |

- I saw the values move; the top limits (9, 2, 1) come from the decompile and I did not press against them. Music's limit is 9 here, while the title-settings doc says 0..8 for the same sliders.
- **Left / right** lower or raise the value under the cursor and set the dirty flag to 1. Music re-applies the volume at once (sub_499583); Sound, Message Speed and Skip play SE 326; Voice plays no SE on right.
- **Up / down** move the cursor through the 5 rows, wrapping round.
- **A and B do the same thing:** both exit (mask 0x30). If the page is dirty the flag becomes 2 and the next tick calls `sub_4905A8(-1)`. If it is clean the flag becomes 3 and nothing is written. Either way the next tick plays SE 323, closes the page and resets the flag to 0.
- **Neither exit throws the change away:** the sliders are edited in place in memory. A dirty exit also writes the file.
- The pause main page order is Status, Items, Encyclopedia, Options, Save, Exit Game; Options is `pm_sel` 3.

**What is written, where and when**
- Header dwords 0–7 before: `341944da 9 5 9 0 1 0 0x12`.
- After: `341944da 6 7 7 0x00010000 2 0 0x12`. They change live as you press; dword 0 is not recomputed.
- `sub_4905A8(-1)` copies no slot (that only happens when the argument is not −1). It writes the whole in-memory file image (0x56e5770, 0x11F7530 bytes) to save.dat and then to a second file, presumably the `_save.dat` backup. The current day's progress is not put into a slot, but the new settings go to disk.
- **save.dat did change on disk:** at f363, right after the dirty exit, `rd endsave` succeeded and filed `save:40945a42e84e`. The run made no other save. I did not check separately on disk that a clean exit writes nothing; that comes from the decompile.

**Tool gaps**
- The card's day-14 note says a morning event owns the game after loading. In this replay there was none, so that note looks stale for this save.
- I could not diff save.dat against the start save, because `rd file` is not available.
