---
name: guild-unpause-equip-buzz
start: save:824053720e01
steps:
  - in the Adventurers' Guild: scene == 11
  - the pause opened over the guild with the item window up (bag packing): scene == 9 and s32(0x6a49b60) == 1
  - the pause closed, back in the guild's bag (FUN_0045bd36 ran): scene == 11 and s32(0x6a49b60) == 1
  - in the dungeon with control, past the floor intro: scene == 1 and s32(0x438b4dc) >= 4 and lock == -1 and dlg == 0 and not loading
  - the pause's Equip item list up (pause entry type 5): scene == 9 and s32(0x734b9a8) != 0
  - the buzz: A on a filled row of the Equip item list (mode 6, slot chosen, 74b28a4 == 0) leaves the confirm countdown at 0 (a valid item leaves it 1; FUN_0046940c zeroed it): scene == 9 and s32(0x734b9a8) == 6 and s32(0x74b28a4) == 0 and (s32(0x73dddd4) & 0x10) != 0 and s32(0x7337850 + 4*s32(0x734b968)) > 0 and s32(0x734b998) == s32(0x731f598 + 8*(s32(0x731f408 + 4*s32(0x734b968)) + s32(0x7337850 + 4*s32(0x734b968)))) and s32(0x734b994) == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
DRIVER COPY (2026-11-19): step 6 fixed — `s32(0x734b994) == 0` is the resting value (it held already at f1299, the tick
after step 5); the countdown goes 0 → 1 → 0 inside the A tick, so the step now keys on that tick (A edge, item list, the highlighted item 734b998 = the bag row's item — not the slot-pick
tick, where 734b998 still holds the slot's item). Step 4 adds `not loading`: lock reads −1 on the arrival tick (f861, loading)
before the intro takes control.

Probe: two retail-coverage targets (`docs/findings/retail-coverage.md`: ported, no recording runs them):
- `FUN_0045bd36` — the Adventurers' Guild's unpause hook (FUN_00453384, mode 11): when the pause closes over the
  guild with the item window up (DAT_0734b9a0 active), the window's scroll / cursor are restored (FUN_00468131), in
  the bag the highlighted slot and the hand cursor, and the compared adventurer (FUN_00468233).
- `FUN_0046940c` — in the dungeon pause's Equip page (FUN_004802cf), A on an item that fails the equip check
  (FUN_00469414 returns 3) plays SE 362 (a buzz) and zeroes the confirm countdown DAT_0734b994.

**Crafted start save** `824053720e01` (every adventurer hirable at level 40). Route to the guild as
`tasks/hire-elan.md`. Hire Louie (or anyone), accept "Equip items in bag?" so the **bag packing** window is up
(adv guild bag mode DAT_06a49b60 == 1; `rd text` shows the bag rows). Press **ESC** (the pause; `rd play R "~"` or
the DRIVER.md pause key), wait for the pause menu, then close it (B / ESC) — record the frames, the bag cursor row
and the hand cursor position before and after (`438abf4` / `438abf8` are its x / y float bits). Then finish packing,
choose a dungeon's first floor, Depart.

On the floor (after the intro and "Equip items in bag?" if asked): pause, open **Equip**, choose a slot (e.g. the
weapon), and in the item list press A on an item of the wrong kind (e.g. food or armour for the weapon slot) — the
buzz (`rd text`, SE). Record the frame, the item, the slot, `s32(0x734b994)` before / after, and that nothing changed
in the equipment. Then back out of the pause. If retail refuses differently (no list entry selectable, or a
different result), report what it did.
