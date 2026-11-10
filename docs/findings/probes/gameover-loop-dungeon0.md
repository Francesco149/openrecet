# Probe: gameover-loop-dungeon0

- **Run:** `adv-guild-hires` · **start:** end:dungeon3-run2-save · **task:** `tools/drive/tasks/adv-guild-hires.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 99 turns, $1.74, 22 min
- **Scenario:** `tests/scenarios/gameover-loop-dungeon0/` (9541 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/adv-guild-hires.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

The probe could not be done. This save only has Louie (id 0) and Charme (id 1), so no other adventurer can be hired. I went on with Louie to get to a dungeon floor, swing and use skills there. The task file's floor predicate is wrong: `rd check` gives step 1 PASS at f6885 and step 2 NOT REACHED, so overall FAIL. `rd verify` gives MATCH at f9541, and I looked at a strip of the swings and skills.

**Run:** `adv-guild-hires`, 9541 frames, from `end:dungeon3-run2-save`.

**The start save doesn't match the card or the task**
- **No morning event, and it is evening:** the save loads at f137 in period 3 (`u32(0x450fb88) == 3` = bank dword 0xb0fc). `dlg` stays 0.
- **The guild is closed then:** on the town map the guild is entry 6 (`wm_cur == 6`). Its state `u32(0x096435a0)` is 0 (disabled), and A on it does nothing. The map's tooltip says "Going to a dungeon will take 2 periods of time".
- **Roster:** byte `bank+0x2bc4c+k` gives 0x101, so only ids 0 (Louie) and 1 (Charme) are listed. Both `bank+0x2bd18..` dwords are 0. Names come from the 128-byte records at `0x5cc680`: 0 Louie, 1 Charme, 2 Caillou. Base fees are 50 and 100 (record +0x28). The fee flag `bank+0x2bc81` is 1.
- **The game ends that night:** the save can't reach the guild in the daytime before the week-2 debt (30,000 pix) is due. I opened the store at the counter and tapped A. Day 14→15 at f2650, the "collection form" took the 11,846 pix the store had made, then game over (`scene == 2`, the title was never shown). The game restarted at day 1 with 1000 pix (f~6375), keeping merchant level 6 and 33 items.
- **The restarted loop (HUD day 2):** after skipping a morning event with `~ A`, all town-map entries are open and the guild is highlighted (state 2). Its intro runs (iv2_7 / iv2_8, skipped with `~ A`). Louie joins (`bank+0x2bc4c = 1`), and he is the only one. The guild list (`rd text`) reads: "Leave", "Level:15 Swordsman Louie", fee "Free" (fee flag 0x2bc81 = 0). Ids 2..7 can't be hired here.

**Step frames**
- f613: left the shop for the town map (evening).
- f2650: day 14→15. f5449: game over. Then the restarted loop at day 1.
- f6817: town map, day 2 morning.
- **f6885: `scene == 11` (step 1).** f7180: back to the map after the intro. f7208: guild again.
- f7341: hired Louie (`0x438b7d8 = 0`) and pressed Depart. With fees off this goes straight to dungeon 0.
- f7540–9068: a dungeon tutorial dialogue, then the "Equip items in bag?" box (Yes at f9129).
- f9329: Hall of Trials 1F with control.
- Swings start at **f9340, f9380 and f9425**.
- **Skill 1 (key `1`) at f9460, skill 2 (key `2`) at f9501.**

**Floor predicate (must change)**
- `s32(0x438b4cc)` is 0-based: it reads 0 both in the shop and on 1F. The port's floor plate draws it +1 (`src/dungeon_hud.c` `floor_plate`). So `>= 1` never holds on a first floor.
- What tells the shop from the dungeon is `s32(0x438b4dc)`, the stage record. It reads 0 in the shop (checked at f6706) and 4 in the Hall of Trials. `src/scene1_preload.c` treats values below 4 as shop stages.
- Suggested step 2: `scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0`. It needs a copy of the task file, which I couldn't write (no shell).
- That predicate also holds during the frames between the load and the dungeon dialogue (f7341–7539), and while the Equip box is up (f9068–9129).
- Dungeon id `0x438b4c8 = 0`, `0x438b4d0 = -1`, `0x438b4d4 = -1` on this trip.

**State I named (this run's vars.json)**
- `p_act` = s32 `0x56db048`, the player's action: 0 idle, 2 normal attack (A), 4 skill 1 (key `1`), 5 skill 2 (key `2`; B also gave 5). It lasts about 30 ticks for a swing and about 35 for a skill.
- `p_actt` = s32 `0x56db04c`, the action's tick counter (counts up from 0).
- `conv` (`0x56daafc`) reads 3 during any action.
- `p_hp` = f32 `0x56db0c4`; `0x56db0bc` is a twin. Both read 87, matching the HUD's 87/87.
- `p_sp` = f32 `0x56db0c8`; `0x56db0c0` is a twin. It reads 22, matching the HUD's 22/22. Each skill costs 5 SP, drained 0.22 per tick: 22→17 at f9461–9483, then 17→12 at f9502–9524.
- Player actor record at about `0x56da1b8`; facing is `0x56db05c`. `px`/`pz` read −40 the whole time in the dungeon, so they don't track the dungeon player.
- Key `Y` (E) opened the pause menu, and `X` (D) did nothing visible.

**Strip:** `view/strip_9340_9508.png`. Blue sword arcs at f9352, f9388 and f9436. Skill 1 is a red spin slash at f9472 with the HUD at SP 19/22, and SP is 15/22 at f9508. No enemy was in reach.

**What the tool lacked**
- `rd ck load` of an earlier checkpoint quietly cuts the movie back to that point and drops every later checkpoint. `rd undo` can't bring it back, and `rewind` does the same. I had to replay about 2,800 frames by hand to get the trace back; the replay was identical and still verifies.
- A read-only "eval at frame F" would avoid this.
- `rd lint` and `check` only take a task file, so I couldn't test the corrected predicate without a shell.
- The card's notes for `dungeon3-run2-save` (morning event, 9,086 pix and the guild reachable) don't fit this save.
