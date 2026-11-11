# Boss floors — shared mechanics (code-read)

Shared by the cards in this dir (`mushroom-king.md`, `charme.md`, `boss-ahriman.md`, `ohm.md`, `crabs.md`,
`tielle.md`, `dungeon2-15f.md`, `boss-rush.md`). Damage formula / labels: `combat-core.md` (other author).
Everything here is **code-read**; nothing was run. Read in IDA (`all_ida.c`): FUN_0041f903, the player arm of
FUN_0043865e, FUN_0042f7ad. Read from the port only (RNG-verified code): placement, FUN_0044d0a4 / FUN_0044cc80,
the actor arm of FUN_0043865e.

## Notation

- `B` = `0x76b970 + i*0xba4` (actor i). The boss is the floor's first allocation → normally `i = 0`
  (`a0hp`); confirm with `s32(B+0x424) == TYPE`. Minions take the next free slots.
- "tick" = one sim step. `PX/PZ` = `px`/`pz` (0x56da1d8 / 0x56da1e0).
- Arena centre (−40, −40) on every scripted boss floor.

## Actor fields the boss AIs use

| off | type | meaning |
|---|---|---|
| +0x3f0/+0x3f4/+0x3f8 | f32 | x / y / z |
| +0x3fc/+0x400/+0x404 | f32 | velocity x / y / z |
| +0x3dc | s32 | alpha 0..255 (death fades, Tielle hidden = 0) |
| +0x18 | s32 | facing octant (0..7; records that use `octant·2π/8`) |
| +0x420 | f32 | yaw (atan2(dx, dz) convention: 0 = +z) |
| +0x424 | s32 | type |
| +0x428 | s32 | 0 free, 1 alive, 2 dying (death arm FUN_0042f7ad runs, +0xaf0 counts) |
| +0x42c / +0x434 | f32 | HP / max HP |
| +0x440 | s32 | stagger countdown (AI skipped while > 0: FUN_0042f28c) |
| +0xa54 | s32 | **AI state** (8 = intro on every placed boss) |
| +0xa58 | s32 | state tick |
| +0xa5c, +0xa60, +0xa64, +0xa68 | s32 | per-boss sub-state / counters (see cards) |
| +0xa70, +0xa74 | s32 | mushroom king: last meal |
| +0xa9c / +0xaa4 | f32 | walk target x / z (king, ahriman) |
| +0xab8 | f32 | **DF/MD scale**: effective DF = ftol(DF · s²) (FUN_0043865e actor arm; port record_hit.c:543) |
| +0xae4 | s32 | variant / sub-mode (Tielle 0 = scripted, 1 = fight; kobold 3 = perched; Charme: plan) |
| +0xaf0 | s32 | death tick (while +0x428 == 2) |
| +0xb08 | s32 | **≠ 0 = no record can hit it** (FUN_0043865e actor scan; port record_hit.c:439) |
| +0xb20 | s32 | 1 = a hit never staggers / resets the state (king) |
| +0xb28 | s32 | 1 = DF and MD count as 0 (king state 4) |
| +0xb2c | s32 | 1 = summoned (no 600-tick respawn) |
| +0xb54 | s32 | 1 = no knock-back, no stagger, state not reset by hits (ahriman, Ohm, crabs, Tielle fight, bombs) |
| +0xb98 | s32 | 1 = can be hit while dying (bombs) |

Globals: event stage `s32(0x438b8a0)`, event tick `s32(0x438b8a4)`, Tielle phase `s32(0x438b8a8)`, pad lock
`lock` (0x438b8b0: −1 free, 0 locked), player invulnerable ticks `s32(0x56db0f4)` (debug name MUTEKI; 30 after a hit,
1200 when a boss starts dying), player knock-down ticks `s32(0x56daeb0)`, slow-mo `s32(0x438becc)`, no-exit flag
`s32(0x9643128)`, boss-run mode `s32(0x438b4d0)` (−1 = none), run stage index `s32(0x438b4d4)`, player ATK
`s32(0x56db0ac)`, player DF / MD `s32(0x56db0b0)` / `s32(0x56db0b8)`.

## Placement (FUN_00431b1c arm = port `enemy_place_boss`, enemy_place.c:168)

- A checkpoint floor (FUN_0043195d: `floor % 5 == 4`; dungeon 5: `floor >= 29`) whose enemylist section holds a
  boss-class id (FUN_00431990: 23..25, 27, 28, 41, 43, 49, 54, 55, 59..73) → FUN_004319d6 = 1 = "boss floor".
- Boss = the section's first entry × its count, at (−40, 0, −34); count 2: x −45 / −35; count 3: x −50 / −40 / −30, the
  outer two at z −38; types 70 / 71 at z −40; types 23 / 54 at y 15. Each gets `+0x428 = 1`, **`+0xa54 = 8`**.
- Skipped (own setup): d0 f4, d1 f14, d2 f29, d3 f19/29/49, d4 f29/49/69/79/99, d5 f37/41/56/58.
- Save byte bank+0x2bcfe ≠ 0 → no boss is placed (meaning not traced).
- The other lines of the section (mushrooms, bats, bees, bombs) are **not** placed; the boss AI summons them.
- Level (FUN_0041f319): d1 `floor/2 + 2`, d2 `floor/3 + 6`; boss run (`0x438b4d0 >= 0`): `f = 5·floor − 145`, then the
  mode's formula on f. HP = table HP · (1 + 0.2·lv) (×0.1 with bank+0x2bc57). AT/MA multiplier
  `1 + 0.1·lv` (0.12 from lv 50) (FUN_0041f903, port record_hit.c:119).

## Floor event (FUN_0044d0a4, port dungeon_flow.c:409) — generic boss floors

Event stage 0:
- tick 30: title banner, `0x9643128 = 1`.
- ticks 0..239: `lock = 0`, fixed camera (eye (−40, 12, −65), look (−40, 3.5, −40)).
- tick 240: adventurer's line; from 240 `lock = −1`, camera class 2, partner held.
- from tick 61, once **no actor has +0x428 ≠ 0** (dying ones count as present): both portals rise
  (FUN_0044b2e6(0), (1)), `0x9643128 = 0`, event stage 1. Predicate: `s32(0x438b8a0) == 1` (and `gate != 3`).

Setup (FUN_0044cc80, port :475): party from the floor's start 5 back, 1 apart. Regular dungeons: next-floor portal
(−35, 0, −40) + return portal (−45, 0, −40), both sunk until the event raises them. Boss run: one portal at
(−40, 0, −40) (return portal on the run's last floor).

## Enemy records → player (FUN_0043865e player arm, IDA 38260.., port record_hit.c:1356)

- Hit when `hypot(player − record) − reach < 2.0` and `−(reach + 5) < dy < reach`, record state 1 / 3, and
  `s32(0x56db0f4) <= 0`.
- Physical: `ftol(AT·mul/2 − DF/4)` (record 15: `/4`), × record multiplier (+0xb4; 1.0 unless noted), min 1, then
  `+ rand % (hit/5)` (hit ≥ 5) or `+ rand & 1`.
- "Magic" record types 18, 20, 31, 33, 38, 42, 43, 70, 90, 107, 108, 137, 152..154 use MA. **MA is 0 for every
  enemy in enemy.txt / the .data defaults** → the roll is ≤ 0 → clamps to **1, +0/1**. (Surprising; unverified live.)
- After a hit: invulnerable 30 ticks (guard: 15), knock-down `0x56daeb0 = 30` (70 for record types 14, 18, 161).
- HP ≤ 0 → `DAT_0438be98 (floor end kind) = 1`. Predicate for "the player died to it": `hp <= 0`.

## Player records → boss (actor arm, port record_hit.c:425)

- Volume: horizontal `d − reach < r·rf[3]·rf[0]` (r = +0xabc, 1.0), vertical ±`r·rf[2]·rf[0]` (+0.8·reach).
  rf = the enemy record's floats +0x44.. (bosses 24/59/60/67/68/69: rf[2] 3.0, rf[3] 4.5; Charme / Tielle 2.0 / 2.0).
- `hit_angle = atan2(actor − record) − yaw + π`: |a| < π/4 front (no bonus), < 3π/4 side (×1.2), else back (×1.5).
- Actor state +0xa54 in 1..6 → ×1.2 (bit 8, the "counter" label).
- Player base `ftol(ATK/2 − DF_eff/4)`, min 1, + the same random top-up.
- Bosses flagged +0xb20 / +0xb54 keep their state when hit; others (Charme) get +0x440 = 40, state 0, knock-back.

## Death (FUN_0042f7ad)

All bosses: tick 1 → slow-mo, `0x56db0f4 = 1200`. Per-boss timelines in the cards. `+0x428` goes 1 → 2 → 0;
"boss dead" predicate: `s32(B+0x428) == 2` (dying) / `== 0` (gone), or `f32(B+0x42c) == 0`.
