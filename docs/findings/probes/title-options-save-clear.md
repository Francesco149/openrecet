# Probe: title-options-save-clear

- **Run:** `title-options-save-clear` · **start:** `save:17264a73` (played, day 14) · **task:** `tools/drive/tasks/title-options-save-clear.md`
- **Driver:** subscription subagent (2026-11-19); filed by the orchestrator after correcting steps 3 / 6 (the task assumed
  DAT_09643524 returns to 0 on the fold; it stays 2, only DAT_09643520 drops 10 → 0).
- **Scenario:** `tests/scenarios/title-options-save-clear/` (335 frames, never leaves the title)
- **Verdicts:** check PASS (steps at f89 / 91 / 104 / 191 / 244 / 256); verify MATCH (headless + real device, rng f61bcc81);
  rhost triage identical every v3 block, save IDENTICAL; gate rows portrng + portstate (the title menu).

Retail facts (observed live):
- Options: Music 5 → 4 (L), B: dirty 1 → 2 at f94, the save.dat write + the fold at f95 (FUN_004905a8(-1)); Options
  re-entered shows 4.
- "Clear Save Data" + A: `Clearing all data. Are you sure?` Yes / No, **the cursor starts on No** (FUN_00434def's third
  argument 1, box cursor DAT_0438ac24 = 1); L then A answers Yes.
- Yes (FUN_004902aa): the header magic is zeroed, FUN_004901c2 at once re-seeds the header (magic 0x341944da, SE 9, BGM 5,
  9, slider3 1 — Music reads 5 again) and every bank; **the clear draws RNG** (00004de1 → f61bcc81, the banks' re-seed).
  DAT_09643560 = 1 (LABEL_186), so the B exit writes the save again (f247).
- The exit's FUN_0049a43d rebuilds the main menu: NEW GAME, CONTINUE, ITEM ENCYCLOPEDIA, OPTIONS, EXIT (codes 0 1 7 2 3)
  → NEW GAME, ITEM ENCYCLOPEDIA, OPTIONS, EXIT (0 7 2 3), cursor on OPTIONS; the slot past the new count keeps its old
  code (3). The encyclopedia then shows 0%.

Port before: no save write on the settings exit, no clear-data prompt (the A only played its SE), no menu rebuild.
