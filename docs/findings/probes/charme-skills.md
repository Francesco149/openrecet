# Probe: charme-skills

- **Run:** `charme-skills` · **start:** save:824053720e01 (crafted, day 15) · **task:** `tools/drive/tasks/charme-skills.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (cutestation via `win-wifi`)
- **Scenario:** `tests/scenarios/charme-skills/` (1296 frames)
- **Verdicts (2026-11-19):** check PASS (5/5); verify --real MATCH (fresh headless, clean session and real device
  all f1296 rng 7a7f9ad6). No golden / triage run.

## Step frames

| step | frame | note |
|---|---|---|
| 1 Charme leading, control | f702 (check) | **weak:** f702 is the first floor tick (`lock` reads −1 there); real control after "Equip items in bag?" (f911, `A` = Yes) at **f922**. Fix: add `dlg == 0 and frame > 900` or `ticker == 0` plus the box gone. |
| 2 state 5 (rush) | f1038 | key `2` pressed f1038 |
| 3 rush over | f1077 | state tick 35 |
| 4 state 6 | f1106 | key `3` pressed f1106 |
| 5 state 6 over | f1136 | state tick 30 |

Route: `.*60 A .*30 A .*120` (shop f137) → `search --goal "scene == 8"` (f335) → town map: `D D D R` lands on
`wm_cur` 3 (not 6, same as hire-elan); a search over single presses found `R .*7 U` from `wm_cur` 0 → 6 → `A` →
`scene == 11` f462. Roster cursor on Louie; `D` → Charme (fee **4,000**; Louie 3,950); `A` → packing screen (help
scroll; first `A` dismisses it), `U` → Depart!, `A` → dungeon map (cursor on Jade Way), `A` → floor rows (Jade Way 1~5
first, best time 00:07:17), `A` → f702 floor 1 (gold 470,000 → 466,000).

## Charme's skill table (retail, observed)

Chara index `s32(0x438b7d8)` = 1 (Charme, `s32(0x56da1cc)` = 27). Skill count `dword_438B7DC[1]` = 3.
`dword_438B7FC[5*1 + slot]` = 0 / 1 / 2 / 3 (slot 3 locked by the count); costs `dword_5CE520[5*1 + slot]` = 5 / 5 / 5 / 5.
`sub_489C79`: skill 0 → state 4, 1 → 5, 2 → 6, 3 → 7, 4 → 8. Keys `1`..`5` (`sub_489D52`, pad bits 0x200..0x2000)
select slot 0..4 **and use it at once** (only if slot < count). So the task's "skill 1 → state 5" is right per skill
id, but the slot is 1 (key `2`), not slot 0.

| key / slot | skill | state | SP | length | record | observed |
|---|---|---|---|---|---|---|
| `1` / 0 | 0 | 4 | 96 → 91 (f1226) | ticks 0..9, 0 at tick 10 (f1236) | none in slots 0..9 | after-image (port: `FUN_00494f87`, trail 600 ticks); slime-type 10 at 2.5 not hit |
| `2` / 1 | 1 | 5 | 106 → 101 (f1038) | 0 at tick 35 (f1077) | **62** at tick 5 (f1043), slot 1 | rush; `p_inv` 1 from tick 1; moves 0.5/tick along facing ticks 5..29 (|Δ| 0.3536 per axis, diagonal), decelerating 30..34; hit at tick 5 = 4 frozen frames (f1043..1047, tick stays 5); **a2 (type 22) 21 → 0, "Back Attack! 30"** |
| `3` / 2 | 2 | 6 | 101 → 96 (f1106) | 0 at tick 30 (f1136) | **83** at tick 8 (f1114), slot 1 | `p_inv` set 30 at tick 1 (counts down); record 83 = **the bind web** (spider web picture f1120+) |

SP is debited on the tick the state is entered (same tick as `s32(0x56db048)` changes). No SP regen seen over f1038..f1296.

### Record 83 (player-owned web), retail

- At age 1 (f1114) 0.77 ahead of Charme (x −60.39 vs −59.62; ≈ 0.3 ahead + one 0.46 step), vx −0.46, ×~0.92 per tick to ~0.0445, then a constant
  creep 0.0445 until age ~46, then stops (here at x −66.34, ~5.9 from spawn); same as the enemy-Charme web in
  `docs/findings/bosses/charme.md`.
- Reach `+0xa8` 0.5 through age ~45, grows to ~1.93 by age ~58, stays (still alive at age 94, f1211).
- **Binds enemies:** a5 (type 10, 7 HP) at 2.43 from the web centre (< reach + 2) stops dead at f1162 (age ~48) and
  stays there through f1211; a4 (type 13) at 6–10 moves freely. No damage dealt by the web.

## Other events

- f1274: hp 142 → 140, `p_act` 1, `p_hurt` 30, knock back −x — an enemy hit (record slot 0 type 10, owner a4) while
  standing next to the web. Only damage taken.

## Tool notes

- `rd undo` after a `tap` (6 presses, f460 → f586) reported "undone (savestate) frame 460", but the next `.*30` and
  `rd text` / picture showed the later packing screen with Louie hired (state not reverted). Worked around with
  `rd rewind --to 442`. Later `rd verify` said the live session's state differed by "write-tracking residue" (sem
  hash differed, frame/RNG equal) and moved the run to a clean session. Possibly related; not isolated.
- `rd tap --until` whose predicate already holds runs 1 tick (expected, but the step-1 predicate held during the
  floor intro, see above).
- No name for the record table by slot; watching a record needs raw `s32(0x69324b0 + i*0x124 + off)`.
