---
name: easydisp-levelup
start: dungeon0-full@3935   # the first Hall of Trials 1F frame with control
ini: setup.s_easydisp=1     # rd new --ini (the movie keeps it)
steps:
  - the recet.ini override is live: s32(0x438b1a0) == 1
  - in the dungeon with control: scene == 1 and s32(0x438b4dc) >= 4 and lock == -1
  - an enemy killed, its EXP credited straight to the leader (no orbs): s32(bank + 0x2ceb0 + 0x6c * s32(0x438b7d8) + 0x44) >= 10   # a kill (a hit alone pays +1)
  - a level-up through that path: s32(bank + 0x2ceb0 + 0x6c * s32(0x438b7d8)) > 0
  - one more kill after the level-up (the EXP keeps landing): s32(bank + 0x2ceb0 + 0x6c * s32(0x438b7d8) + 0x44) > 365
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **recet.ini `s_easydisp` = 1** (DAT_0438b1a0, "simple display"). With it a kill (FUN_0042e1a6) does not spawn
EXP orbs: each would-be orb adds 10 EXP (20 on floor event 2) straight to the leader's record (`bank + 0x2ceb0 +
0x6c·DAT_0438b7d8`: level +0x00, EXP +0x44, next +0x4c), and when EXP ≥ next it calls the level-up FUN_0048a383
directly (all_ida 29694). The party's Dark Soul aura overlays are also skipped. The port stubbed the level-up call
(PORT-DEBT FUN_0048a383, `src/record_hit.c`); no recording ran with the flag.

Start: `./rd new easydisp-levelup --from dungeon0-full@3935 --ini setup.s_easydisp=1` (Hall of Trials 1F, control at
3935; 3511 is the load tick). Check step 1 first; if
`s32(0x438b1a0)` is 0 the override did not reach retail: stop and report.

Record at the start: L0 = the leader's level, E0 = EXP, the next threshold (+0x4c). Kill enemies with aimed swings
(`DRIVER.md`) until a level-up; note per kill the frame, the EXP before / after and the orb count (no table C EXP orbs
should appear). After the level-up (L0 + 1, the HP / SP refill, the burst), E1 = EXP then; kill one more. Then leave
the dungeon if cheap, else stop. Keep `hp` above 40 %.
