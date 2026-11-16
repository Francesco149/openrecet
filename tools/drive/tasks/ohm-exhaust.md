---
name: ohm-exhaust
start: bossrush-jade-amber@11640
steps:
  - Ohm on Boss Rush floor 34 with control: scene == 1 and s32(0x76b970 + 0x424) == 67 and lock == -1
  - first slowdown (more than cap bees): s32(0x76b970 + 0xa68) > 3 and s32(0x76b970 + 0xa64) == -200
  - second slowdown (more than 2 x cap): s32(0x76b970 + 0xa68) > 6 and s32(0x76b970 + 0xa64) == -400
  - bees exhausted, Ohm alive: s32(0x76b970 + 0xa68) >= 15 and s32(0x76b970 + 0x428) == 1
  - floor cleared after exhaustion: estage == 1 and s32(0x76b970 + 0xa68) >= 15
forbid:
  - back at the title: scene == 0 and frame > 400
---
Probe: **Ohm (type 67) bee slowdowns + exhaustion** — `docs/findings/bosses/ohm.md` §6 rows "bee slowdown / exhaustion"
(the 40F pair reached `bn` 9 / 8, never exhaustion) and the burst-shot hit, both **not reached**.

Start: `bossrush-jade-amber@11640` = arrival on d5 floor 34 (Amber rush 2nd floor; probe `bossrush-jade-amber.md` row 34):
Ohm L14, HP 570, actor slot 0 (`B` = 0x76b970). Read the card's §3 for the cap / gaps on THIS floor before trusting the
predicates (the card says dungeon 2: cap 3, 14 bees; d5 floor < 38 should take the dungeon-2 row — check `sub_` of the AI
in `all_ida.c`, and fix the numbers in your report if the floor reads otherwise). Bee timer `+0xa64` counts to 120; after
`+0xa68 > cap` it restarts from −200, `> 2·cap` from −400, none once `>= 5·cap`.

How: kill bees (HP ~30, 1..2 swings), keep Ohm alive (570; avoid spins near it). This is long (gaps 320 / 520 ticks): use
`rd play` with `--until` on the next spawn, and searches only for the kills. Opportunistic: if a burst shot (`ost == 3`)
hits you, record frame / damage (card row "burst shot hits"). After exhaustion watch ≥ 800 ticks with no bee alive, then
kill Ohm and wait for `estage` 1. Report per-spawn rows (frame, `+0xa68`, `+0xa64` after), tool gaps. Do not go on to floor 35.
