---
name: stage-object-hits
start: save:824053720e01
steps:
  - on a dungeon floor with control: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1 and loading == 0
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **attack records hitting stage objects of kind 0 / 2 / 3** — the object arm of the record state machine
`FUN_0043865e` (0x43a17a / 0x43a1d7) spawns particle 0x15 (kinds 2 / 3) or 0x16 (kind 0) (`FUN_00447f4f` 0x449938 /
0x4499c0); no recording reaches it (`findings/retail-coverage.md` "Reachable, unrecorded"). Stage objects live in the
table at `0x695ef78` (stride 0x2a0: kind = s32 at +0x8c (dword 35), state = s32 at +0x268 (dword 154), position = f32 +0 / +4 / +8;
`src/dungeon_objects.h`). Kinds 0..3 come from map collision triangles of types 8..11 (`sub_432473`, the floor markers),
kind 8 from type 16; kinds 4 / 5 / 8 are chests, 6 the return door / exits. Kinds 0..3 have a hit counter of −1 (a hit
returns before any state change) — but the particle spawn happens on the hit.

**Crafted start** `824053720e01` (hire lineage; Louie leads by default — any leader is fine). Do: go into dungeons
(Jade Way / Hall of Trials / others), and on each floor list the stage objects (`rd eval` over the table's kind field for
slots 0..31, or `rd ram R dwords 0x695ef78 ...`) until you find kinds 0, 2 or 3; walk to one and hit it with the sword
(and a projectile if the leader has one). Watch table A (particles `0x69b2f80`, stride 0x94, type at +0x30) for types 0x15
(21) / 0x16 (22) right after the hit. Record: which floors carry kinds 0..3 (dungeon, floor, positions), what they look
like (a picture: `rd look`), the hit frame, the particles spawned. If no floor shows kinds 0..3 after ~10 floors across
3 dungeons, stop and report the kind histogram per floor.
