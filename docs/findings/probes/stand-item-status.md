# Probe: stand-item-status

- **Run:** `stand-item-status` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/stand-item-status.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 50 turns, $0.66, 6 min
- **Scenario:** `tests/scenarios/stand-item-status/` (1082 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/stand-item-status.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     stand-item-status                    f212 FUN_00414345 < FUN_004147d5 < FUN_0048670f ⇄ init_shape_lt2 (+2)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

The C key opens the stand's item status. Run `stand-item-status` is 1082 frames from save:64d43650865f. `rd check` gives PASS (step 1 at f137, step 2 at f210, step 3 at f328) and `rd verify` gives MATCH.

**The button:** `C` (the keyboard C key) is the 0x40 bit. I read the edge mask at u32(0x73dddd4): on a press frame it shows 0x40 for C, 0x10 for A and 0x20 for B. `X` sets no bit and does nothing in free roam. `Y` opens the pause menu (scene 9).

**Timeline:**
- f60/f90: `A` then `A` load the save. f137: scene 1, cc08=1, dlg=0. No event played, so step 1 holds at f137.
- f209: I searched to the spot below the centre table, at (−0.20, 3.74). The faced cell is cbfc=4, cc00=4, grid[84] = 0x2ee80 (Thankful Statue).
- **f210:** `C` gives cc08 1→50 on the press frame itself. That is step 2.
- f211–285: I pressed R, L, U and D inside the window. Nothing changed: cc08 stayed 50, cbfc/cc00 stayed 4/4, the position stayed the same and the text was identical.
- **f328:** `A` closes it (cc08 50→1 on the press frame). That is step 3. Recette is then pushed back out of the table, from (−0.20, 3.74) to (−0.54, 3.03) over about 20 ticks. The same push happens after any non-C frame there.
- f573: she faces the top back shelf's empty leftmost slot (cbfc=1, cc00=0, grid = −1). `C` did nothing: cc08 stayed 1, no text was drawn and she kept walking.
- f601: she faces the same shelf's Big Shield (2,0). **f602:** `C` gives cc08=50. **f618:** `C` again closes it.
- f620–858: I walked to the right wall at (3.10, 1.14), clear of the door at (3.0, −3.4), pressing right; cbfc and cc00 were −1. At f859 `C` did nothing: cc08 stayed 1, scene stayed 1, no text. The f860 picture shows her against the wall below the door.
- f1025–1035: she faces the left shelf's Steel Sword (0,1). f1036: `C` gives cc08=50. f1052: `B` closes it.

**Buttons inside the window (tested from checkpoints, then cut from the movie):**
- `A`, `B` and `C` each close it on the press frame.
- The arrows, `X` and `1`..`5` do nothing.
- `Y` opens the pause menu (scene 9) and cc08 stays 50 underneath. `B` then closes the pause menu and the window together: scene 9→1 and cc08 50→1 on the same tick.
- You cannot browse other stands from inside: the arrows do not move cbfc/cc00 or the player.

**Mode variables:** I ran a mark/diff around the third open, f1035→f1051, over 0x438b000–0x438d000. Only cc08 1→50 changed, plus the usual frame counters and camera floats at 0x438cc38–cc58. Nothing else in the 0x438c… block marks the mode. Outside that range: 0x56daaf0–dab50 changed (9→4, 9→24, 0→2, 2→7, 32→7, 3→0), probably the player animation. In the earlier diff over the open window, 0x438be6c went 0→1 and 0x438be74/78 went to f32 −1 / −2.5.

**Texts (`rd text`):**
- Left box: item name (with "(Equippable)" where it applies), Type, Effect, Base Price, Highest Sale Price, Lowest Sale Price.
- Bottom box: the two-line description, "Base Price- N", "Number possessed- N", and "Price Down" in red when it applies.
- Floating label: the item name.
- Thankful Statue: Treasures, ----, 4550, 3450, 1950. "Once an object of worship, these are just simple statues now. Still worth a fair bit." Number possessed 1.
- Big Shield (Equippable): Shields, DEF+12 MDEF+4, 2800, ----, ----. "It's bigger, more protective... and heavier. Few people mind, though, if it keeps them alive." Price Down, possessed 0.
- Steel Sword (Equippable): Swords, ATK+20, 3000, ----, ----. "A heavy blade forged of tempered steel. The go-to weapon for any real swordsman." Price Down, possessed 0.
- For equipment there is also a right panel, "Characters who can equip/current equipment", showing Louie's portrait. It is drawn as a sprite, so it is not in `rd text`; I saw it in the f610 picture.

**Pictures:** I looked at single frames from `rd look`, not an `rd strip` contact sheet, which the task asked for. The window is open in f215 (Statue) and f610 (Shield). f573 shows the empty slot, f860 the wall. They are in runs/stand-item-status/view/.

**Game facts:**
- The display grid is at bank + 0x4e26*4 = 0x44f7030, 20 columns. In this save the stocked cells are 2, 3, 4 / 20 / 60, 63, 64 / 80, 83, 84.
- The top back shelf is columns 1–4 of row 0.
- cbfc/cc00 keep their last value after you turn away from a stand: they were still 4/4 after the push-back.
- The camera follows Recette, so pictures from different places look very different.

**Defined (this run only):** `cbfc` = s32 at 0x438cbfc and `cc00` = s32 at 0x438cc00. Worth sharing, along with the edge mask u32(0x73dddd4) as `pressed`.

**Tool gaps:**
- `rd eval` has no `hex()`.
- `rd strip` cannot take a list of frames; I used `look --at` for that.
- The session was rebuilt partway (at about f620), which dropped the savestates and the mark.

## Port

(2026-11-11; IDA `all_ida.c`, port `src/scene1_player_ctrl.c`.)

- **Open** (sub_48670F 0x40 press arm, all_ida 97425-97441): gates `!at_door && !at_counter && furniture word == 0 && +0x2bc5a != 0 && +0x2bc68 == 0 && cbfc != -1 && grid[cbfc + 20*cc00] != -1`; then `sub_47F1CE()` (party list), `cc08 = 50`, `sub_468286(item)` (DAT_0734b998 + the possessed count), `sub_4681DB(2)` (DAT_0734b96c = 3), SE 710; `goto LABEL_675` (room clamp + rumble only: no walk, no anim tick, no FUN_0048a833 on the press frame).
- **Per tick, case 50** (all_ida 97069-97085): Recette's anim record → 0 if not already 0; `sub_4897C6` (status + the three anim ticks); `sub_48A833` (Tear); if `pressed & 0x70`: `sub_4681D3()` (DAT_0734b96c = 0), `cc08 = 1`, SE 710. Then LABEL_675. No FUN_0048b850 (walk / table push-out), no proximity detect, no customer escalate.
- **Pause over it**: the pause close (all_ida 54485) does `if (cc08 == 50) cc08 = 1` — why B closes both on one tick. Ported (`player_ctrl_cc08_item_status_pause_close`); no scenario covers it yet.
- **Draw** (sub_409925, all_ida 6728, after the level badge + LEVEL UP pop): `sub_46A336()` (the stats card, mode 3 → base (256,160)) + `sub_469B3A(0, 0)` (description panel at x 0). The floating label's gate is `cc08 == 1 || cc08 == 50` (all_ida 6566).
- The old split (f212, +2 port draws): the stub left cc08 at 1, so the port ran the free-roam arm (walk + proximity) on frames where retail runs only case 50.
- Evidence: `rhost.py triage stand-item-status` → identical, ticks 1..1382.
