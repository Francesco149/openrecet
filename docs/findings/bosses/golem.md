# Golem / Dark Golem — types 61 + 62 + 63, 64 + 65 + 66 (ゴーレム / ダークゴーレム, 右 / 左)

**code-read**: IDA `sub_420F4D` (body AI, 0x420f4d), `sub_430C6D` part branch (IDA 31926.., 32040..32130),
`sub_42F7AD` case 61 / 64, tables at 0x52b8a4. Disassembly checked at 0x420fab..0x421004 (arm scan) and
0x42130d / 0x421316 (the side write). **Port: body AI stub, arm branch stub, death arm ported unverified.**
Shared rules: `boss-floor-common.md`. Nothing run.

## 1. Where

| place | `dungeon` / `floor` | types | setup |
|---|---|---|---|
| Obsidian Tower 20F | 3 / 19 | 61, 62, 63 | `FUN_0044e08f` case 19 |
| Lapis Ruins 70F | 4 / 69 | 64, 65, 66 | `FUN_00450443` case 69 |
| dungeon 5 floor 41 (also d3's Boss Rush, 4th floor) | 5 / 41 | 61, 62, 63 | `FUN_00450ee0` case 41 |
| dungeon 5 floor 56 (also d4's Boss Rush, 11th floor) | 5 / 56 | 64, 65, 66 | `FUN_00450ee0` case 56 |

- Setup: `for i in 0..2: a = FUN_0041f903(−40, −12, −35, base + i); if i != 0: a[+0xa04] = 0; a[+0xa54] = 8` — body
  first, then right arm, then left arm; the arms' parent index is the literal **0** (the body is assumed to be actor
  0). Then `FUN_0044cc80(1)` (d3 / d4: two sunk portals) or `(2)` (d5).
- Event: generic `FUN_0044d0a4(1)` — pad locked 240 event ticks, portals when no actor has `+0x428 ≠ 0`.

## 2. Stats and parts

| | table HP / AT / DF | d3 f19 (lv 20, ×5.0) | d4 f69 (lv 38, ×8.6) |
|---|---|---|---|
| body 61 / 64 | 150 / 20 / 30 | 750 | 1290 |
| arm 62 / 63, 65 / 66 | 150 / **1** / 30 | 750 | 1290 |

- d5 floor 41: lv `ftol(11·1.7 + 50)` = 68 → ×14.6 = 2190; floor 56: lv 94 → ×19.8 = 2970. Boss Rush: lv
  `(5·floor − 145)/4 + 16` = 31 (d3 run) → 1080; `(5·56 − 145)/5 + 25` = 52 (d4 run) → 1710.
- Body volume: radius `4.5·rf0` = 4.05 (61) / 3.6 (64), half-height `3.0·rf0` = 2.7 / 2.4, centred on the body's
  hit point (+0x3e4..).
- Body flags written every AI tick: `+0xb54 = 1` (no stagger, no knock-back, state never reset by hits),
  `+0x9f8 = 1`.
- **Arms cannot be hit**: every body-AI tick writes `+0xb08 = 1` into each actor of type 62 / 63 / 65 / 66 (0x420fc2,
  0x420fe5); the player-record scan skips `+0xb08 ≠ 0`. The arms follow the parent's position and yaw
  (`FUN_00430c6d`, IDA 32050).
- The AI reads "arm dead" = arm `+0x42c ≤ 0` (right: 62 / 65, left: 63 / 66) and `FUN_00430c6d` detaches an arm 30
  ticks after its `+0xa58` goes non-zero (`+0xa04 = −1`, flung 0.4 sideways, falls, sinks after 100 ticks).
  **No writer of an arm's HP or `+0xa58` found** → how (or whether) an arm is ever lost is unresolved; the
  arm-dead branches below are marked †.

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)` (++ at the end of every AI tick)

Other fields: side `s32(B+0xa5c)` (0 right arm, 1 left; in state 3: 0 = at the player, 1 = home), pattern index
`s32(B+0xa60)` (0..11), add timer `s32(B+0xa64)`, adds spawned `s32(B+0xa68)`, ground y `f32(B+0x3e0)`.

| state | length | what |
|---|---|---|
| 8 rise | 240 | `+0xa98 = 1`; anim 3; y = min(0, 0.2·t − 12) (up in 60 ticks). t ≥ 240 → 0 |
| 0 walk | pattern | anim 0; yaw turns to the player 0.0314/tick; velocity 0.06 along yaw. Adds (below). On the ground and `t ≥ dur[idx]`: `t = 0`, `idx = (idx+1) % 12`, run `act[idx_old]` |
| 1 punch | 161 | t < 80: yaw → player direction + 0.157 (side 0) / − 0.157 (side 1), 0.0157/tick. t == 80: vy = 0.5. **t == 100: the side's fist hits** (record 14 at the arm's joint `side`, shake 8, SE 328, dust). t == 130: anim 8 / 9. t > 160 → 0 |
| 2 double | 181 | t == 61: anim 7, SE 349. **t 76..104: both fists hit every tick** (record 14 at each arm's joint). t > 180 → 0 |
| 3 jump | 141 | anim 6, SE 373 at entry. t == 20: vy = 2.3. t 20..79: horizontal velocity = 0.05·(target − pos), target = the player (side 0) or the spawn point `+0x414/+0x41c` (side 1); else velocity ×0.95. **t == 90: record 14 at the body**, shake 30, SE 365, rumble 32. t > 140 → 0 |
| 4 wait † | 121 | nothing; t > 120 → 0 |
| 7 | 90 | t ≥ 90 → 0 (no writer of state 7 in `sub_420F4D`) |

Pattern table (0x52b8ac: `dur`, `act`), 12 entries, walked in order from index 0, wrapping:

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dur (walk ticks before it) | 60 | 20 | 20 | 100 | 20 | 20 | 20 | 100 | 100 | 60 | 20 | 20 |
| act | 0 | 1 | 2 | 3 | 4 | 5 | 2 | 2 | 2 | 4 | 4 | 5 |

| act | meaning |
|---|---|
| 0 | side = 0 → right punch (anim 1, state 1); † right arm dead → state 4 |
| 1 | side = 1 → left punch (anim 2, state 1); † left arm dead → state 4 |
| 2 | side = `rand() & 1` (the only RNG pick), then as 0 / 1 |
| 3 | double (state 2, effect 44) while at least one arm lives; † both dead → state 1 without a fist |
| 4 | jump at the player (state 3, side 0) |
| 5 | jump back to the spawn point (state 3, side 1) |
| 6 | state 4 + SE 349 (not in the table) |

One full pattern = 560 walk ticks + 6 punches (161) + 1 double (181) + 5 jumps (141) ≈ 2,410 ticks.

**Adds** (state 0 only): n = live actors of type 36 (golem: `DAT_0052b8a4`) or 35 (dark: `DAT_0052b8a8`) — both
types counted for both golems. `need` = 1 (golem) / 2 (dark). While `n < need`: `+0xa64` counts to 30, then
`+0xa68++`; `+0xa68 > 2·need` → next wait +60, `> 4·need` → +120; if `+0xa68 < 5·need` spawn one moai (golem 36 / dark
35) at radius 2..10 around (−40, −40), > 10 from the player (30 tries, `FUN_00471089` ×2 each), y 1, `+0xb2c = 1`.
Total over the fight: 4 (golem) / 9 (dark). Moai AI `FUN_0042cfd0` (stub; not read here).

## 4. Attacks

| attack | record | where / when | notes |
|---|---|---|---|
| fist (punch) | 14 from the **arm** actor, placed at its joint `f32(arm + 0x6fc + 12·side)` (x, y, z) | state 1 tick 100, once | arm AT is **1** → physical roll `ftol(1·mul/2 − DF/4)` ≤ 0 → min 1 (+0/1); knock-down 70 |
| fists (double) | same, both arms | state 2 ticks 76..104, every tick | 29 ticks of 1-damage hits, player invulnerable 30 after each |
| stomp | 14 from the body | state 3 tick 90 | body AT 20·mul; knock-down 70 |
| contact | — | not read (`FUN_0048b850` side) | |

Record 14's reach is the records table's (not read here).

## 5. When it can be damaged

- Body: any time after state 8 (`+0xb08` is never set on the body; `+0xa98` only gates physics). During state 8 the
  body is below the floor for 60 ticks.
- Arms: never (§2).
- Front / side / back multipliers and the "state 1..6 → ×1.2" bonus apply (states 1, 2, 3 are in 1..6).

## 6. Kill recipe (search-driven)

1. Wait out the intro: `s32(B+0xa54) != 8` (B = actor 0; check `s32(B+0x424) in (61, 64)`).
2. Cell: `(s32(B+0xa54), s32(B+0xa58)//20, int(f32(B+0x42c))//50, int(px − f32(B+0x3f0)), int(pz − f32(B+0x3f8)))`.
   Score `-f32(B+0x42c)`.
3. Stand behind it (yaw turns only 0.0314/tick in state 0 and not at all in states 1..3): attack from
   `atan2` back sector for ×1.5; states 1 / 2 / 3 add ×1.2.
4. Avoid: the fist joint at state 1 tick 100 and state 2 ticks 76..104 (harmless: 1 damage, but knock-down 70 costs
   time); the landing point at state 3 tick 90 — with side 0 it is where the player stood during ticks 20..79, so
   move after tick 80.
5. Moai: kill or ignore (the body does not need them dead; the portals do: no actor may remain).
6. Done: `s32(B+0x428) == 2` (dying; the arms copy it from death tick 100), then `s32(0x438b8a0) == 1`.

## 7. Arms checklist (every AI branch + a predicate)

| branch | predicate |
|---|---|
| rise | `s32(B+0xa54) == 8 and f32(B+0x3f4) < 0` |
| walk, pattern advance | `s32(B+0xa54) == 0` and `s32(B+0xa60)` steps 0 → 11 → 0 |
| right punch (act 0) | `s32(B+0xa54) == 1 and s32(B+0xa5c) == 0`, anim `s32(B+0x34 + 4·s32(B+0x30)) == 1` |
| left punch (act 1) | `s32(B+0xa54) == 1 and s32(B+0xa5c) == 1` |
| random side (act 2, idx 2 / 6 / 7 / 8) | both sides seen at those indices |
| hop + fist | state 1: `f32(B+0x400) == 0.5` at t 80; a record 14 owned by the arm at t 100 |
| double (act 3) | `s32(B+0xa54) == 2`, t 76..104 |
| jump at the player (act 4) | `s32(B+0xa54) == 3 and s32(B+0xa5c) == 0`, `f32(B+0x400) > 2` at t 20 |
| jump home (act 5) | `s32(B+0xa54) == 3 and s32(B+0xa5c) == 1` |
| add spawn | an actor with `s32(+0x424) in (35, 36) and s32(+0xb2c) == 1`; `s32(B+0xa68)` 1..4 (dark: ..9) |
| add back-off | `s32(B+0xa64) in (−60, −120)` |
| dark variant | type 64: `need` 2, add type 35 |
| death | `s32(B+0x428) == 2`; arms `s32(+0x3dc)` fading with the body from death tick 100 |
| † arm lost / state 4 / act 3 fallback | `f32(arm+0x42c) <= 0`, `s32(arm+0xa04) == −1`, `s32(B+0xa54) == 4` — report if ever seen |

## 8. Unresolved

- How an arm takes damage (§2).
- Record 14's reach / lifetime; contact damage.
- State 7 and act 6: no writer / no table entry.
- `FUN_0041f759` anim ids (0 idle, 1 / 2 wind-up, 3 rise, 6 jump, 7 double, 8 / 9 recover, 10 death) named from use.
