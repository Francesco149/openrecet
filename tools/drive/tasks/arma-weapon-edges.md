---
name: arma-weapon-edges
start: save:352fc5a20843
steps:
  - on a dungeon floor with control, Arma leading: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1 and s32(0x438b7d8) == 7
  - seven or more Hammer Arm records (0x75) alive at once: sum(1 if s32(0x69324b0 + 0x124 * 0) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 1) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 2) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 3) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 4) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 5) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 6) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 7) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 8) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 9) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 10) == 117 else 0, 1 if s32(0x69324b0 + 0x124 * 11) == 117 else 0) >= 7
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **the edges of Arma's weapon records** — the branches `arma-weapons` did not reach (records-B tick `FUN_0043ae20`;
port `src/scene1_records_b_tick.c`). Read `docs/findings/probes/arma-weapons.md` first (route, Equip page keys, the gauge).
**Crafted start** `352fc5a20843` (hire lineage + items 701..715; `SAVES.md`). Arma's records' owner = the player struct
`0x56da1b8`: `p_hurt` `0x56daeb0`, `p_act`, `p_actt`, `p_oct` (`+0x948`, the 8-way facing octant), Arma's weapon kind `+0xf58`.

Do, each a separate stretch:
1. **Hammer Arm (702)**: 7+ taps in open space so more than 6 type-0x75 (117) records live → the "boost" (age ≥ 100,
   count > 6: they fly off, then bounce on the ground 3 times — phase 2).
2. **Omega Cannon (715)** at enemies AND at the bare ground: record 0x7c — hits an actor, homes (`43ab6e`), lands, the
   post-impact phase.
3. **Punch (701), Drill (707), Chaos (712)** while facing a diagonal or ±x (`p_oct` not 0 and not 4).
4. **Drill (707) and Phalanx (713) fired point-blank into a wall** (the wall tail: particles 0x29 / 0x2a, SE 0x167).
5. **Get hit while a record is live**: fire Crisis (709) / Gravity (711) or skill 4 (the cannon, `FUN_0049755c`), then take
   an enemy hit (`p_hurt > 0`) while its 0x74 / 0x79 / 0x9b record exists — the record is killed by the hurt.
Record per item: frame, record types / counts / ages (`rd ram R records`), what happened.
