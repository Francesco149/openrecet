# Pumpkin Ghost — type 55 (かぼちゃゴースト)

**code-read**: IDA `sub_426EAB` (AI, 0x426eab), `sub_42F7AD` case 23 / 49 / 55 / 54, table 0x52b96c. **Port: AI
stub; the death arm's 23 / 54 cases are ported, 55 is not in the port's case list.** Shared rules:
`boss-floor-common.md` (generic boss floor: placement, `FUN_0044cc80`, `FUN_0044d0a4`). Nothing run.

## 1. Where (level → HP each = 15·(1 + 0.2·lv))

| place | `dungeon` / `floor` | count | HP each | add cap |
|---|---|---|---|---|
| Obsidian Tower 5F | 3 / 4 | 1 | lv 17 → **66** | 4 |
| Lapis Ruins 5F | 4 / 4 | 2 | lv 25 → **90** | 6 |
| Lapis Ruins 35F | 4 / 34 | 3 | lv 31 → **108** | 5 |
| dungeon 5 floors 38 / 46 / 51 | 5 / … | 1 / 2 / 3 | own: lv 63 / 77 / 85 → 204 / 246 / 270; rush (d3 f38) lv 27 → 96, (d4 f46 / f51) lv 42 / 47 → 141 / 156 | 4 |

Placed by the generic boss placement (count 1: (−40, 0, −34); 2: x −45 / −35; 3: x −50 / −40 / −30), state 8.

## 2. Stats

Table: HP 15, EXP 30, AT 20, DF 20, MA 0, MD 10, boss flag 0; rf0 0.9 → radius 1.8, half-height 1.8. Every AI tick
`+0xb54 = 1` (no stagger / knock-back / state reset), `+0x9f4 = 1` (flies).

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`; cycle clock `s32(B+0xa5c)`

| state | length | what |
|---|---|---|
| 8 descent | 240 | y = 15 − 0.0625·t; nothing else runs. t == 240 → 0 |
| 0 drift | — | anim 0 |
| 1 cast | 60 | anim 3. **t == 20: summon or shot** (below). t == 60 → 0 |

Every tick in states 0 / 1 (after the state block):

- `c = +0xa5c % 400` (then `+0xa5c++`). **c == 1: hold `s32(B+0xa60) = rand & 63`.**
- **c == hold + 20: fade out** (`s32(B+0xa64) = 1`).
- **c == hold + 180: fade in** (`+0xa64 = 0`); if the player is within 8.0: up to 20 tries of a point
  (−40 + 15·(rnd − 0.5), −40 + 15·(rnd − 0.5)) more than 8 from the player → **teleports there** (x, z only).
- **c == hold + 300: state 1**, t = 0.
- Movement: φ = 2π·(50·(`s32(B+0xa50)` & 7) + c)/400; goal = player − 8·(sin, cos)(angle to the player +
  0.628·sin φ); turns to the player 0.1/tick; `v = 0.96·v + 0.005·dir` (top speed 0.125/tick); hovers at
  ground + 1.5 + 0.5·sin 3φ (vy within ±0.1).
- Alpha `s32(B+0x3dc)`: +4 per tick while `+0xa64 == 0`, −4 while 1, clamped 0..255. **`+0xb08` = 1 exactly on the
  ticks alpha would go below 0** (fully invisible and still fading), else 0.

Timeline of one 400-tick cycle (h = hold, 0..63): visible → h+20 starts fading → **h+84 .. h+179 invisible and
untouchable (≈ 96 ticks)** → h+180 reappears (maybe elsewhere), hittable at once (alpha 4) → h+300 casts (60 ticks)
→ next cycle.

**Cast (state 1, t == 20):** n = live actors of type 52, 53 or 15; cap = 4 (dungeon 4: 6 on floors < 10, else 5).

- n < cap: `k = rand % 3`; min(cap − n, 3) adds, each: up to 30 tries of {angle 2π·rnd, r = 2 + 8·rnd around
  (−40, −40); accept when > 10 from the player} → `FUN_0041f903(x, 10, z, T[k])`, T = {52 ghost O, 53 ghost N, 15
  pumpkin} (0x52b96c), `+0xb2c = 1`, `+0xae4 = rand & 1` (type 15: `= 2` after the draw); `k = (k + 1) % 3`.
  Ghost AI: `+0xae4 == 0` → `FUN_004275a7`, 1 → `FUN_004278de`; pumpkin variant 2 → `FUN_00429e6f` (all stubs,
  not read here).
- n ≥ cap: **record 90** + SE 330.

With 2 / 3 ghosts each runs its own cycle (their `+0xa50 & 7` and holds differ) but they share the add count.

## 4. Attacks

| attack | record | notes |
|---|---|---|
| shot | 90, only when the add cap is reached | "magic" list → MA 0 → 1 damage; flight not read |
| adds | ghosts 52 / 53 (AT 20), pumpkins 15 | the real damage of the floor |
| contact | not read | |

## 5. When it can be damaged

`s32(B+0xb08) == 0`: always except the invisible window (and the first tick after `+0xb8c ≠ 0`, writer not found).
Flying at ground + 1..2: vertical test as for the boss ahriman (`boss-ahriman.md` §2).

## 6. Kill recipe

1. Wait `s32(B+0xa54) != 8`.
2. It circles 8 from the player at ≤ 0.125/tick (player walk 0.175): walk it down and hit; no stagger, no block.
   HP 66..108 on the story floors.
3. Window: `s32(B+0xb08) == 0`; lose no time during c in h+84..h+179 (kill adds then). After h+180 it may be up to
   ~10 away (teleport) — re-acquire by `f32(B+0x3f0)`, `f32(B+0x3f8)`.
4. Cell: `(s32(B+0xb08), s32(B+0xa5c) % 400 // 40, int(f32(B+0x42c))//10, int(px − f32(B+0x3f0)), int(pz − f32(B+0x3f8)))`;
   score `-sum of the ghosts' HP`; avoid `hp <= 0`.
5. Several ghosts: each death (tick 1) gives the player 60 invulnerable ticks while another lives, 1200 when it was
   the last; at the last one's death tick 60 every add dies.
6. Done: no actor left → `s32(0x438b8a0) == 1`.

## 7. Arms checklist

| branch | predicate |
|---|---|
| descent | `s32(B+0xa54) == 8`, y 15 → 0 |
| hold roll | `s32(B+0xa60)` changes at `s32(B+0xa5c) % 400 == 1` |
| fade out / invisible | `s32(B+0xa64) == 1`; `s32(B+0x3dc) == 0 and s32(B+0xb08) == 1` |
| fade in without / with teleport | player farther / nearer than 8 at c == h+180; position jump |
| cast → summon (n < cap) | new actors type 52 / 53 / 15 with `s32(+0xb2c) == 1`, ≤ 3 per cast |
| ghost variants 0 / 1, pumpkin variant 2 | `s32(+0xae4)` of the adds |
| cast → shot (n ≥ cap) | record 90 at `s32(B+0xa54) == 1 and s32(B+0xa58) == 20` |
| cap 4 / 5 / 6 | d3 f4 / d4 f34 / d4 f4 |
| count 2 / 3 placement | d4 f4 / f34 |
| death, another alive / last | `s32(0x56db0f4)` 60 / 1200 at death tick 1 |

## 8. Unresolved

- Record 90; the ghosts' and pumpkin variants' AIs; contact damage.
- `+0xb8c` (non-zero once → `+0xb08 = +0xb04 = 1` for that tick): who sets it.
- The teleport keeps y; the hover code pulls it back within ±0.1/tick.
