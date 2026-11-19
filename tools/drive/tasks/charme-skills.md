---
name: charme-skills
start: save:824053720e01
steps:
  - in a dungeon with Charme leading, control (past the floor intro): scene == 1 and s32(0x438b4dc) >= 4 and s32(0x56da1cc) == 27 and lock == -1 and dlg == 0 and frame > 900
  - Charme's skill state 5 (the rush, FUN_0049511d): s32(0x56da1cc) == 27 and s32(0x56db048) == 5
  - the rush ran out (back to 0): s32(0x56da1cc) == 27 and s32(0x56db048) == 0
  - Charme's skill state 6 (FUN_0049508c): s32(0x56da1cc) == 27 and s32(0x56db048) == 6
  - that one ran out too: s32(0x56da1cc) == 27 and s32(0x56db048) == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Charme's skills** — with Charme (char 27) leading, a used skill sets the party state DAT_056db048 by the
selected slot's skill (FUN_00489c79: skill 0 → 4, 1 → 5, 2 → 6; Charme's slots 0 / 1 / 2 = keys `1` / `2` / `3`). State 5 is her rush (FUN_0049511d: invincible,
SE 343 + hit record kind 62 at tick 5, 30 ticks of 0.5 along her facing with sparks, end at 35); state 6 is
FUN_0049508c (anim 3, invincible 30, SE 686 + hit record 83 at tick 8, end at 30). The retail coverage sweep
(`docs/findings/retail-coverage.md`) found no recording that runs either; the port has both ported, unverified.

**Crafted start save** `824053720e01` (day 15 morning, every adventurer hirable at level 40, dungeons 0..4 open;
`docs/findings/bosses/UNLOCKS.md` §5). Route as `tasks/hire-elan.md` / `tasks/obsidian-1f.md`: shop → door → town map
→ Adventurers' Guild; hire **Charme** (find her row in the roster with `rd text`), accept the equip prompt, pick any
open dungeon's first floor (Jade Way 1F is fine), Depart.

On the floor: the skill slot is DAT_056db0d8 (the HUD shows the selected skill); the skill button / slot-cycle keys
are in `DRIVER.md` (search "skill"). Select the slot whose skill is 1 (state 5), use it near an enemy if one is close
(note any hits), let it finish; then the slot with skill 2 (state 6), use it, let it finish. Record per use: the
frame, `s32(0x56db0d8)` (slot), SP before / after (`s32(0x56db0c0)` is a float — `rd eval f32(0x56db0c0)`), and any
enemy HP change. If SP runs short, wait for it to refill or use an item. If a slot's skill is neither 1 nor 2, report
the slot → skill map (`DAT_005ce520` costs, `chara_skills`) and what each produced (DAT_056db048 values).
