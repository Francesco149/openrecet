# Probe: item-status-pause

- **Run:** `item-status-pause` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/item-status-pause.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 26 turns, $0.32, 2 min
- **Scenario:** `tests/scenarios/item-status-pause/` (478 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/item-status-pause.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

The item-status window and the pause menu over it close together on one tick, both times. Run `item-status-pause` is 478 frames from save:64d43650865f. `rd check` gives PASS (step 1 at f211, step 2 at f215, step 3 at f276) and `rd verify` gives MATCH.

**Timeline (movie frames):**
- **f60 / f90:** `A` then `A` load the save. f137: scene 1, cc08 1, dlg 0, with no event first.
- **f211:** I searched with walking moves plus `C` as actions until `cc08 == 50` (path `U*2 R*2 UR*8 U*52 DL*8 C`). `C` opened the window at f211 (step 1). The stand it opened is not the centre table from the findings doc: Recette was at (0.708, −0.40), and the window shows **Practice Lance** (Spears, ATK+20 DEF+3, 4200 pix, possessed 0).
- **f213:** `Y` pressed. **f215:** scene 1→9, with cc08 still 50 (step 2). The pause opened 2 ticks after the press.
- **f244 / f260:** `D` moved the cursor (pm_sel 0→1), then `U` moved it back (pm_sel 0 again at f260).
- **f276:** `B` pressed (step 3).

  | frame | scene | cc08 |
  |---|---|---|
  | 274 | 9 | 50 |
  | 275 | 9 | 50 |
  | 276 | 1 | 1 |
  | 277 | 1 | 1 |

  dlg stays 0 throughout. Over the next ticks Recette is pushed back from the stand (pz 0.107 by f296).
- **f297:** `C` reopened the window. She was still facing the stand, so the search's path was just `C`.
- **f299:** `Y` pressed. **f301:** scene 9, cc08 50.
- **f320 / f335:** `D` put the cursor on Items (pm_sel 1), then `A`. pm_state rose to 10 and the Items page shows "Tough Clothes".
- **f377:** `B` leaves the Items page. pm_state counts 10→9…→0 over f378–387. The pause stays open: scene 9, cc08 50.
- **f408:** `B` pressed.

  | frame | scene | cc08 |
  |---|---|---|
  | 406 | 9 | 50 |
  | 407 | 9 | 50 |
  | 408 | 1 | 1 |
  | 409 | 1 | 1 |

  This is the same one-tick close.
- **f416–478:** walking `D*20 L*20 U*10`, from (0.73, 0.11) to (−2.65, 2.88). scene 1, cc08 1, dlg 0 the whole time. The `U` moves did nothing, so there is probably a wall there.
- The forbid (back at the title after f400) never fired.

**Pictures:** I looked at a strip of f272–280 every 2 frames.
- f272 and f274 show the pause menu.
- At f276 the game is back in the shop, with the item-status window as a fading ghost (the closing transition).
- From f278 the shop is clear, with only the floating "Practice Lance" label. Its gate is `cc08 == 1 || 50`.

I also looked at f319: the pause list reads Status, Items, Encyclopedia, Options, Save, Exit Game. The calendar shows Today = 16.

**Game facts:**
- The pause menu opens 2 ticks after the `Y` press.
- B on a pause sub-page leaves the menu open: pm_state counts down 10→0 over 10 ticks, scene stays 9 and cc08 stays 50. Only the B on the main page closes both.
- pm_sel 0 is **Status**, a character page for Louie, not Items. Items is pm_sel 1.
- `pm_type` (`s32(0x74b2844 + 4*pm_sel)`) read 0 here with pm_sel 0. Its note "1 Items" may be wrong, or it may be laid out differently over this window. Worth re-checking.

**Defined (this run):** `cbfc` = s32 at 0x438cbfc and `cc00` = s32 at 0x438cc00, as in the findings doc. I did not need them.

**Tool gaps:** none blocking. The `pm_*` notes would help more if they listed the main-page order: 0 Status, 1 Items, 2 Encyclopedia, 3 Options, 4 Save, 5 Exit.
