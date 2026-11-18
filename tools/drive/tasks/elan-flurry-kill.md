---
name: elan-flurry-kill
start: hire-elan:near7
steps:
  - on a dungeon floor with control, Elan leading (chara 4): scene == 1 and s32(0x438b4dc) >= 4 and lock == -1 and s32(0x438b7d8) == 4
  - Elan's flurry (skill 1 on contact, p_act 7): s32(0x438b4dc) >= 4 and p_act == 7
  - two slimes held at HP 0 by the flurry's punches (record 0x86 sets +0xba0 = 30): a9use == 1 and a9hp == 0 and a10use == 1 and a10hp == 0
  - both killed (the final blast, record 0x87): a9use != 1 and a10use != 1 and p_act == 0
---
Probe: **Elan's flurry kills** — record 0x86 hits leave an enemy alive at HP 0 with `+0xba0` = 30 (actor_damage's
guard); the actor tick FUN_00430c6d kills it (FUN_0042e1a6) once `+0xba0` runs out without another hit. `hire-elan`
saw the flurry but no kill.

Played by the orchestrator 2026-11-18 (run `elan-flurry`, from `hire-elan:near7`): slimes 9 / 10 lured within 2.1
(f3409), Elan's hurt state over at f3441, the flurry from f3488 holds both at HP 0 from f3525; the final blast kills both
at f3639 (`+0xba0` 21 → 0 in the hit). **The tick-death arm itself was NOT reached:** the blast reaches every enemy the
punches reach; a 300 s search (850k ticks) for an HP-0 enemy with `+0xba0 == 1` from two slimes found none.
