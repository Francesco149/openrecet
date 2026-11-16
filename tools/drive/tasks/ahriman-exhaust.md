---
name: ahriman-exhaust
start: bossrush-jade-amber@10219
steps:
  - the boss ahriman on Boss Rush floor 33 with control: scene == 1 and s32(0x438b4cc) == 33 and s32(0x76b970 + 0x424) == 54 and lock == -1
  - first slowdown (more than 2 x cap bats spawned): s32(0x76b970 + 0xa68) > 8 and s32(0x76b970 + 0xa64) == -60
  - second slowdown (more than 4 x cap): s32(0x76b970 + 0xa68) > 16 and s32(0x76b970 + 0xa64) == -120
  - spawns exhausted, boss still alive: s32(0x76b970 + 0xa68) >= 20 and s32(0x76b970 + 0x428) == 1
  - floor cleared after exhaustion: estage == 1 and s32(0x76b970 + 0xa68) >= 20
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **boss ahriman (type 54) minion slowdowns + exhaustion** — `docs/findings/bosses/boss-ahriman.md` §3 (Minions) and
§6 rows "slowdowns at 9th / 17th" and "minions exhausted", both **not reached** so far (the 15F pair and Lapis trio died first).

Start: `bossrush-jade-amber@10219` = arrival on d5 floor 33 (Amber Garden rush, 1st floor; probe
`docs/findings/probes/bossrush-jade-amber.md` row 33): boss ahriman L12, HP 340, in actor slot 0 (`B` = 0x76b970, stride
0xba4). Floor < 38 → kinds 2 (bats 6 / 7), **cap 4, total 19**: `+0xa68` counts spawns; > 8 → the next gap is 90 ticks
(`+0xa64` reset to −60), > 16 → 150 (−120), none once `+0xa68 >= 20`. Validate these against the IDA reading in your run
(`all_ida.c` `sub_426811`) and say if the predicates are off (step 1's floor var may be 0-based: `floor` = `s32(0x438b4cc)`
— fix it from `rd status` if so).

How: a spawn needs a free slot under the cap, so **kill bats, keep the boss alive**. Its beam (state 1, ticks 100..179,
strip ±4.5 along its facing octant) does 40..50: stay out of the strip (card §7 predicates `qal / qlat`). Bats die to 1..2
swings; a spin hits the boss too — mind its HP (340; ~20 per hit), prefer plain swings aimed at bats
(`{dpad(aNx - px, aNz - pz) | A}`), and step away from the boss. `rd search --score "s32(0x76b970 + 0xa68)"` with
`--avoid "hp < 60 or s32(0x76b970 + 0x42c) < 80"` is a reasonable shape. Eat from the bag if HP runs low.

Record per spawn: frame, `+0xa68`, `+0xa64` right after, type, point. After exhaustion watch ≥ 600 ticks with free slots
(no bats) to show no spawn comes; then kill the boss and wait for `estage` 1 (minions die at death tick 60, frees at 120).
Report the table, the gaps measured, and tool gaps. Do not go on to floor 34.
