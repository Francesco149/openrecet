---
name: fusion-confirm
start: end:dungeon3-run2-save
steps:
  - in the Merchants' Guild: scene == 6
  - Fusion list takes input: scene == 6 and s32(0x9642c00) == 0 and s32(0x9642c24) == 25
  - "Fusing X. Continue?" is up (guild mode 5): s32(0x9642c00) == 5
  - the result animation runs (guild mode 6): s32(0x9642c00) == 6
  - back on the Fusion list after the result: s32(0x9642c00) == 0 and s32(0x9642c24) == 25   # (bfc is 0 again on the tick the mode returns to 0)
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **fuse an item at the Merchants' Guild** — the confirm (mode 5) and result (mode 6) arms of the guild
tick `FUN_004922c0`. No recording reaches them; the port's `scene_guild.c` has them ported but unverified, and the
Fusion render `FUN_00493616` is PORT-DEBT.

Read first: `docs/findings/merchant-guild-RE.md` (the 2026-10-23 "Fusion ported" block at the top: recipe count
`DAT_09642bf4`, the list cursor `c30` / scroll `c34`, A checks the first three ingredient slots, mode 5 Yes / No,
mode 6's `bfc` 1→60 then A → 61..75 → mode 0; "Key globals": the guild menu block is `DAT_09642c00` + offset, so
`c00` = `s32(0x9642c00)` is the mode, `c04` the main cursor), and `docs/findings/town-map-RE.md` for leaving the shop
to the town map and choosing the guild.

The start save is day 14 with 9,086 pix and 32 items (title → Continue → slot, ~190 ticks). Get to the town map
(the shop's front door), into the Merchants' Guild, onto Fusion, and fuse something the bag has ingredients for. If
no recipe is fusable with this bag, say which ingredients are missing and get them (the guild's Buy list, the
market) — or report exactly why it cannot be done from this save.

Wanted in the report, beyond the card's list: the recipe fused (list index, output item id and its `+N`), gold and
`items` before / after, the frames of mode 5 and mode 6, any RNG draw in mode 6 you can see (`rng` before / after
the burst at `bfc == 2`), and a strip of the confirm box and of the "Fused" banner.
