---
name: arma-finisher
start: lapis-41f-50f@27600
steps:
  - on Lapis 50F with Arma (type 41) alive: scene == 1 and s32(0x438b4dc) >= 4 and actors(T == 41, 0, 31) >= 1
  - the lob knocked the player down long enough (knock-down > 60): s32(0x56daeb0) > 60
  - Arma's finisher (state 5): actors(T == 41 and ST == 5, 0, 31) >= 1
  - the finisher record 0x9c (156) spawned (table B slots 0..23): s32(0x69324b0) == 156 or s32(0x69325d4) == 156 or s32(0x69326f8) == 156 or s32(0x693281c) == 156 or s32(0x6932940) == 156 or s32(0x6932a64) == 156 or s32(0x6932b88) == 156 or s32(0x6932cac) == 156 or s32(0x6932dd0) == 156 or s32(0x6932ef4) == 156 or s32(0x6933018) == 156 or s32(0x693313c) == 156 or s32(0x6933260) == 156 or s32(0x6933384) == 156 or s32(0x69334a8) == 156 or s32(0x69335cc) == 156 or s32(0x69336f0) == 156 or s32(0x6933814) == 156 or s32(0x6933938) == 156 or s32(0x6933a5c) == 156 or s32(0x6933b80) == 156 or s32(0x6933ca4) == 156 or s32(0x6933dc8) == 156 or s32(0x6933eec) == 156
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **Arma's finisher** — boss Arma (Lapis 50F, type 41, AI `FUN_00425c3f`; card `docs/findings/bosses/arma.md`, read
§ state table rows "5 finisher", "161 lob", and the "never get hit by the lob" advice — this probe does the OPPOSITE).
`lapis-41f-50f` never reached state 5; the records 0x9c (156) and 0x9e it spawns (body `FUN_0043ae20` 0x43b7a3..0x43ba32,
0x43b364; spawn `FUN_00445a8c` 0x446c05 / 0x446c58 / 0x447d67) are the largest reachable never-run block group.

Start: `lapis-41f-50f@27600` (the movie's arrival on 50F, f27591; `rd new R --from lapis-41f-50f@27600`). Get hit by the
lob (record 161; `s32(0x56daeb0)` = the knock-down counter rises to 70) while Arma's `+0xa68` (≥ 60) allows it → state 5:
`+0xa54 == 5` on her actor (`rd ram R actors` gives her slot; read `s32(0x76b970 + slot·0xba4 + 0xa54)`); stay within 25 of
her goal line (side 1 / 2 by your z: goal (px, −25) / (px, −55)) so the finisher is not aborted; at sub 12 she fires record
156 (0x9c), which spawns 0x9e at age 1. Let it play out (whether it hits you — record HP before / after); if the party
falls, that is fine (record it; a game over after a wipe is not forbidden here — say what happens). Then finish the
fight or stop. Fix the step predicates if they are wrong (`ST` = the AI state field per DRIVER.md — check it is `+0xa54`) and say so.
Record: frames of the lob hit, state 5 entry, sub 12, the 156 / 0x9e records (types, ages, positions; `rd ram R records`),
damage to the party.
