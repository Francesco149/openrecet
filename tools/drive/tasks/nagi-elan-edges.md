---
name: nagi-elan-edges
start: save:824053720e01
steps:
  - on a dungeon floor with control, Nagi leading: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and loading == 0 and lock == -1 and s32(0x438b7d8) == 5
  - Nagi's dash record (0x8b = 139) alive: s32(0x69324b0) == 139 or s32(0x69325d4) == 139 or s32(0x69326f8) == 139 or s32(0x693281c) == 139 or s32(0x6932940) == 139 or s32(0x6932a64) == 139 or s32(0x6932b88) == 139 or s32(0x6932cac) == 139
  - on a dungeon floor with control, Elan leading: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and loading == 0 and lock == -1 and s32(0x438b7d8) == 4
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **Nagi's and Elan's record edges** (records-B tick `FUN_0043ae20`; port `src/scene1_records_b_tick.c`,
`src/dungeon_party.c`). The `hire-nagi` / `hire-elan` probes tapped one attack and one skill; never run: Nagi's dash (record
0x8b from `FUN_00489e66`) hitting an enemy (`p_hurt` = 15, `p_act` = 1 on the hit), her swing (0x58 / 0x66) with `p_oct == 2`
and killed by a hurt; Elan's swing / follow-up (0x5b group, `FUN_00494ca4` / `FUN_00494dc5`) into a wall (SE 0x29e) and
killed by a hurt. Read `docs/findings/probes/hire-nagi.md` and `hire-elan.md` first (route, chara ids in `s32(0x438b7d8)` —
fix the predicates if they differ), and `charge-attacks.md` (the hire predicates fire during the load: wait for `lock == -1`).

Do, Nagi leading (crafted hire lineage `824053720e01`, Hall of Trials or Jade Way): (1) double-tap a direction to dash INTO
an enemy (find the dash input from the port: `FUN_00489e66`, `db040` bit 4); (2) swing while facing `p_oct == 2`; (3) take
an enemy hit while a swing record is alive. Then back to town, Elan leading: (4) swing and the follow-up point-blank into a
wall; (5) take a hit while a swing record lives; (6) skill 4 / 5 interrupted into a wall if possible. Record per action:
frame, record types (`rd ram R records`), `p_hurt` / `p_act` / `p_oct`, what happened.
