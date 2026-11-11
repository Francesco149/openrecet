# Dungeon 2, 15F — no boss: a clear-the-floor checkpoint

**code-read**: enemylist.txt (user data, read at runtime) section `ダンジョン３ f:15`; FUN_004319d6 (port
`src/stage_gate.c:126`), floor layout (port `src/floor_layout.c:190, :416`), placement FUN_00431d54 (port
`src/enemy_place.c:30`), setup FUN_0044cc80 no-boss arm (port `src/dungeon_flow.c:525`), event FUN_0044d0a4
(port `:409`). Shared rules: `boss-floor-common.md`.

## 1. Where / what

- `dungeon == 2`, `floor == 14`. Checkpoint floor (`floor % 5 == 4`).
- Its list: blue slime (2), blue ahriman (8), blue roper (19), red bomb (37), blue knight (44). **None is boss-class**
  (FUN_00431990) → FUN_004319d6 = 0 → no boss, no intro, no pad lock, no fixed camera.
- Dungeon 2's boss run skips it: stages map to floors 4, 9, 19, 24, 29 (DAT_005adb74).
- Layout: grid 3 × 5 cells of 40 (a boss floor is 3 × 3); the first up-to-10 listed cells are marks; start = mark 0.
- Enemies (checkpoint variant of the placement): every room except the start room gets **one group of 4, 5 or 6**
  (sizes `i % 3 + 4`, shuffled) of one list type (the 5 types in shuffled order, cycled). Placement point: 5..19 from
  the room centre, ≤ 100 tries each.
- No respawns (the 600-tick respawn is off on checkpoint floors), no floor event, no lost-girl / type-30 specials.
- Portals: next-floor (kind 8) and return (kind 6) beside the first variant-≥5 room (x = 3.5 − 40·col, z = −40·row;
  the return portal 7 to −x), else in the first / second room. **Both start sunk** (`gate == 3`).
- Event (stage 0): title banner at tick 30; from tick 61, **once no actor has `+0x428 ≠ 0`** → both portals rise,
  `s32(0x438b8a0) == 1`.

## 2. Stats (level 14/3 + 6 = 10 → HP ×3.0, AT ×2.0)

| type | table HP / AT / DF | HP | note |
|---|---|---|---|
| 2 blue slime | 15 / 30 / 4 | 45 | |
| 8 blue ahriman | 15 / 25 / 4 | 45 | flies |
| 19 blue roper | 15 / 25 / 4 | 45 | |
| 37 red bomb | 15 / 20 / 20 | 45 | killed → fuse 300 ticks → blast (reach 4; player within 6: 1–2 + knock-down 70); the slot frees only at the blast |
| 44 blue knight | 15 / 2 / 20 | 45 | front guard (`+0xb9c`): a hit from within ±63° of its facing does 0 and puts it in state 4 while `+0xb18 == 0` (FUN_0043865e actor arm; port record_hit.c:583) |

## 3. Recipe

- Goal `s32(0x438b8a0) == 1` (or `gate != 3`); sub-goal per room: the count of in-use actors falls.
  No single expression counts actors; track the nearest few slots' `+0x428` / HP, or score on
  `-(sum of f32(B_i+0x42c) for the slots seen in use)`.
- Knights: hit from the side / back. Bombs: kill, step > 6 away, wait for `s32(Bb+0x428) == 0`.
- Then walk onto the next-floor portal (`ex`, `ez`), `portal` → `A`.

## 4. Arms checklist

| arm | predicate |
|---|---|
| floor loads with the exit sunk | `gate == 3`, `lock == −1` from tick 0 |
| group sizes 4 / 5 / 6 | per-room actor counts |
| each of the 5 types met | `s32(B_i+0x424)` in (2, 8, 19, 37, 44) |
| knight guard | hit SE with HP unchanged, its `+0xa54 == 4` |
| bomb fuse → blast | bomb `+0x428 == 2`, `+0xaf0` 240 → 540, then 0 |
| last actor gone → portals | `s32(0x438b8a0) == 1` |
| player dies | `hp <= 0` |

Port: the placement, layout, setup and event are ported and RNG-verified on dungeon3-floors / -deep (FRONT); the
knights' AI (types 44..48, FUN_00427f59) and bombs are ported.
