# Pumpkin Ghost — type 55 (かぼちゃゴースト)

**code-read**: IDA `sub_426EAB` (AI, 0x426eab), `sub_42F7AD` case 23 / 49 / 55 / 54, table 0x52b96c. **Port (2026-11-11): AI ported
(`actor_ai_t55`, src/actor_ai.c; the summoned pumpkin `FUN_00429e6f` = `actor_ai_pumpkin_bounce`; dungeon 3's setup
`FUN_0044e08f` / event arm for checkpoint floors, src/dungeon_flow.c); the death arm lists 55. `obsidian-3f-5f-pumpkin`:
rhost portrng identical f1..f18633, portsave IDENTICAL; portblock f13900 / f16900: actors, party, objects equal.**
Code facts vs this card: the descent writes y = **15** − 0.0625·t (asm 0x426f89; the 14.3 below = that less the
fall clamp 0.7 the same tick, read after the move); the pumpkin variant 2 bounces at speed 0.25 off walls and spawns its record 10 on its first tick. Shared rules:
`boss-floor-common.md` (generic boss floor: placement, `FUN_0044cc80`, `FUN_0044d0a4`).

**Run 2026-11-11: `ob-5f`** (Obsidian Tower 5F, Louie L40, ATK 56 / DEF 58 / MATK 21 / MDEF 32; `rd verify` MATCH at f18333;
probe `docs/findings/probes/obsidian-3f-5f.md`). **[obs fNNNN]** = seen there. Numbers below are corrected from retail.

## 1. Where (level → HP each = **150**·(1 + 0.2·lv) — the HP column below was computed with 15: ×10)

| place | `dungeon` / `floor` | count | HP each | add cap |
|---|---|---|---|---|
| Obsidian Tower 5F | 3 / 4 | 1 | lv 17 → **660** [obs f12410: `a0hp` 660, `+0xae0` 17] | 4 |
| Lapis Ruins 5F | 4 / 4 | 2 | lv 25 → **90** | 6 |
| Lapis Ruins 35F | 4 / 34 | 3 | lv 31 → **108** | 5 |
| dungeon 5 floors 38 / 46 / 51 | 5 / … | 1 / 2 / 3 | own: lv 63 / 77 / 85 → 204 / 246 / 270; rush (d3 f38) lv 27 → 96, (d4 f46 / f51) lv 42 / 47 → 141 / 156 | 4 |

Placed by the generic boss placement (count 1: (−40, 0, −34); 2: x −45 / −35; 3: x −50 / −40 / −30), state 8.

## 2. Stats

Table **[obs, `0x5c23f0 + 55*0x68`]**: HP **150**, EXP 30, AT 20, DF 20, MA **35**, MD 10, `+0x20` 0, `+0x24` 0x40; rf 0.9 / 1.5 / 2.0 / 2.0
→ hit radius 1.8, half-height 1.8 (sword reach from the player 4.8). Adds **[obs]**: type 15 HP 15, EXP 20, AT 20, DF 4, MA 0, MD 0
(66 HP at lv 17); type 52 HP 20, EXP 25, **AT 0**, DF 16, **MA 20**, MD 10 (88 HP); type 53 HP 15, EXP 25, **AT 0**, DF 20, **MA 24**, MD 10 (66 HP). Every AI tick
`+0xb54 = 1` (no stagger / knock-back / state reset), `+0x9f4 = 1` (flies).

## 3. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`; cycle clock `s32(B+0xa5c)`

| state | length | what |
|---|---|---|
| 8 descent | 240 | **[obs f12410..12650]** `+0x3f4` = 14.3 − 0.0625·t (13.05 at t 20, 0.55 at t 220), `+0xb08` = 1 throughout, `lock` 0; state 0 at f12650 (y 0.02), `lock` −1 and the clock running by f12670. `+0xb8c` reads 1 on the entry frame only |
| 0 drift | — | anim 0 |
| 1 cast | 60 | anim 3. **t == 20: summon or shot** (below). t == 60 → 0 **[obs f12983 → 13043; summon at f13003]** |

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

**[obs]** cycle 1 (hold 32; values read after the tick, so `+0xa5c` = c + 1): fade flag at clock 53 (f12703), `+0xb08` 1 at 116
(f12766, alpha 0), flag 0 / `+0xb08` 0 / alpha 4 at 213 (f12863), state 1 at 333 (f12983), new hold (38) at 402 (f13052). Holds seen:
32, 38, 21, 60, 8, 54, 37, 6, 7. `+0x3f4` stays 0.02 / −0 in states 0 / 1: the hover is not in the actor's y.

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

**[obs]** cast 1 (f13003, n 0): 3 adds, slots 1..3 = 53, 15, 52, all `+0xb2c` 1, level 17, `+0xae4` 1 / 2 / 1. Cast 2 (n 3): 1 add
(15). Cast 3 (f13824, n 4): record 90. Cast 4 (n 2): 2 adds (52, 53; `+0xae4` 1, 1). Every ghost seen had `+0xae4 == 1`.

With 2 / 3 ghosts each runs its own cycle (their `+0xa50 & 7` and holds differ) but they share the add count.

## 4. Attacks

| attack | record | notes |
|---|---|---|
| shot | 90, only when the add cap is reached | **[obs f13824 → f13939]** MA 35 at lv 17 vs MDEF 32: ftol(35·2.7/2 − 32/4) = 39, +0..6 → took **43** (78 → 35; 45 on an undone branch). The record is born at the boss's (x, 0.25, z), stands still ≥ 22 ticks (reach 0.6 + 0.003/tick → 0.7), then closes on the player; it hit 115..123 ticks after the cast tick and was freed on the hit. While `p_inv > 0` it sat 0.28 from the player without hitting |
| adds | ghost 52 → record **108**, ghost 53 → record **107** (magic, MA 20 / 24), pumpkin 15 → record **10** (physical, AT 20) | **[obs f13666]** 108: ftol(20·2.7/2 − 8) = 19 → took 19 (and 21 on an undone branch). **[obs f13580]** pumpkin: ftol(20·2.7/2 − 58/4) = 12 → took 13 (12 on an undone branch). 107: **not reached** (the type-53 ghosts near the player stayed in state 0 with `+0xb08` 1 for 200 ticks; no HP left to wait) |
| contact | **[obs]** none from the boss (stood 1.0..1.3 from it, f13476 / f13580, no record) | |

## 5. When it can be damaged

`s32(B+0xb08) == 0`: always except the invisible window, the descent **[obs]** (and the first tick after `+0xb8c ≠ 0`, writer not found).
**[obs f13248]** a swing inside the window: active ticks 9..12 with `gap` −0.70..−0.42 (2.9..3.4 inside reach), `+0xb08` 1, alpha 0 →
HP 660 flat, no hit-stop (again f13574..13580, gap −3.2). Damage at ATK 56: base ftol(28 − 5) = 23. **[obs f13486]** state 0, rel 0.36: 26
(23, +0..3), no word. **[obs ≈f14375]** state 1 (t ≈ 32), front: 29 (ftol(23·1.2) = 27, +0..4) = counter; 31 on an undone branch
(f13810, t 10). Spins (2×) took most of the rest: 554 → 0 in 2,229 frames with 6 spins. **Side / back: not reached** — it turns to the player; rel at the hit tick stayed < 0.4 even from
a press at rel 0.72 (the one position found with rel 1.8, f13564, came as it turned untouchable).
Flying at ground + 1..2: vertical test as for the boss ahriman (`boss-ahriman.md` §2).

## 6. Kill recipe

1. Wait `s32(B+0xa54) != 8`.
2. It circles 8 from the player at ≤ 0.125/tick (player walk 0.175): walk it down and hit; no stagger, no block.
   HP 660..1080 on the story floors (≈ 27 sword hits at ATK 56 on 5F; 3,357 frames from the first hit to the kill here, clock 816 → 3909).
3. Window: `s32(B+0xb08) == 0`; lose no time during c in h+84..h+179 (kill adds then). After h+180 it may be up to
   ~10 away (teleport) — re-acquire by `f32(B+0x3f0)`, `f32(B+0x3f8)`.
4. Cell: `(s32(B+0xb08), s32(B+0xa5c) % 400 // 40, int(f32(B+0x42c))//10, int(px − f32(B+0x3f0)), int(pz − f32(B+0x3f8)))`;
   score `-sum of the ghosts' HP`; avoid `hp <= 0`.
4b. **[obs]** A plain `--apply-best` damage search needs `hp < H` in the avoid and a doom check after it (one best cell was one tick before a pumpkin hit). At cap, kill an add before the next cast or the shot (39..45 here) comes.
5. Several ghosts: each death (tick 1) gives the player 60 invulnerable ticks while another lives, 1200 when it was
   the last; at the last one's death tick 60 every add dies.
6. Done: no actor left → `s32(0x438b8a0) == 1`.

## 7. Arms checklist

| branch | predicate |
|---|---|
| descent **[obs f12410..12650]** | `s32(B+0xa54) == 8`, y 14.3 → 0 |
| hold roll **[obs f13052]** (32 → 38, clock 402) | `s32(B+0xa60)` changes at `s32(B+0xa5c) % 400 == 1` |
| fade out / invisible **[obs f12703 / f12766]**; swing at it, no damage **[obs f13248]** | `s32(B+0xa64) == 1`; `s32(B+0x3dc) == 0 and s32(B+0xb08) == 1` |
| fade in without **[obs f14613: d 12.59, no jump]** / with teleport **[obs f13269: d 4.60 → 17.87, (−44.35, −26.62) → (−35.36, −45.54), yaw kept; also f13680]** | player farther / nearer than 8 at c == h+180; position jump |
| cast → summon (n < cap) **[obs f13003: 3; ≈f13408: 1; f14371: 2]** | new actors type 52 / 53 / 15 with `s32(+0xb2c) == 1`, ≤ 3 per cast |
| ghost variant 1 **[obs]**, pumpkin variant 2 **[obs]**; ghost variant 0 **not reached** (4 ghosts read, all 1: `rand & 1`) | `s32(+0xae4)` of the adds |
| cast → shot (n ≥ cap) **[obs f13824]**, its hit **[obs f13939]** | record 90 at `s32(B+0xa54) == 1 and s32(B+0xa58) == 20` |
| cap 4 **[obs]** / 5 / 6 (not reached: other floors) | d3 f4 / d4 f34 / d4 f4 |
| count 2 / 3 placement (not reached: other floors) | d4 f4 / f34 |
| death, last **[obs f16843]**: `+0x428` 2 on the hit (in state 1, t 1); `p_inv` 1141 at death tick 60 (f16947, slow-mo: 104 frames for 60 ticks), where all 4 adds went (`+0x428` ≠ 1, gone one tick later); tick 120 (f17007): actor freed, `estage` 1, `gate` 0. Another alive: not reached (one ghost here) | `s32(0x56db0f4)` 60 / 1200 at death tick 1 |

## 8. Unresolved

- Record 90; the ghosts' and pumpkin variants' AIs; contact damage.
- `+0xb8c` (non-zero once → `+0xb08 = +0xb04 = 1` for that tick): who sets it.
- The teleport keeps y; the hover code pulls it back within ±0.1/tick.

## 9. Floor end **[obs]** (`ob-5f`)

- f17007 `estage` 1: circle at (−35, −40) (`ex`, `ez` are right on this floor), red door at (−45, −40). Pad input moved nothing for the
  next 120 ticks (f17007..17127, event tick ≤ 120) although `lock == −1`.
- Circle: "Go to the next floor?" (`portal` 2, f17184); "No" = `R` then `A` → `portal` −1.
- Door: "Return to town now?" (f17540) walking −x into it at (−44.18, −40.14); not from (−44.9, −38.5) nor from z −41 walking +z.
  `A` → `lock` 0, f17684 `scene` 9 ("CAME BACK ALIVE!", empty bag page 0/20: **`A` leaves, `B` does not**) → f17791 town map
  (`scene` 8, `stime` 2, cursor 6). No Save prompt in the dungeon, no dialogue on the floor or after it.
- EXP 234605 → 234899 on the floor (+294).

## 10. Lapis 5F pair **[obs]** (run `lapis-10f`, 2026-11-12; probe `docs/findings/probes/lapis-1f-10f.md`; `rd verify` MATCH f40626)

Louie L42, ATK 61 / DEF 68 / MATK 22 / MDEF 42, HP 206. Frames = that movie.

- **Placement, count 2**: (−45, 0, −34) / (−35, 0, −34), both state 8, **HP 900 each** (150·6.0, L25) — the §1 row "90" is ×10 off.
- **Descent in lockstep** f17451..17671 (y 13.05 at t 20 → 0.55 at t 220, `+0xb08` 1 both); both state 0 at f17671, `lock` −1 f17672.
  Both clocks `+0xa5c` run in step (equal values all fight); the holds `+0xa60` differ (5 / 42, then 50 / 19, …), so casts and fades are
  offset per ghost.
- **Shared add cap = 6 (d4, floor < 10)**: ghost 0's cast f18009 (n 0) → 3 adds; ghost 1's cast f18084 (n 3) → 3 adds = 6. Every later cast
  of either ghost while n = 6 = **record 90** (pairs f18515 / f18550 and later). The count is global (types 52 / 53 / 15), not per ghost.
- **Ghost variant 0 seen**: slot 7, type 53, `+0xae4` 0 (f18084). (Was "not reached".)
- **Record 90 can be dodged and expires**: two shots in flight (f18515 / f18550); closest approach 0.85 at age 75 without a hit (`p_inv` ≤ 0);
  both **freed at age 120** with no hit (f18645 / f18675). A shot that did hit: ≈54 ticks after a search's goal frame (a doomed branch, undone).
  Shot damage expected ftol(35·3.5/2 − 42/4) = 50, +0..10.
- Adds' hits: record 108 (ghost 52) 24 = ftol(20·3.5/2 − 42/4) (f18398); 36 once (≈f25038, source not isolated).
- **Death with the other alive** (ghost 0, f21397, HP 0 in state 0): hit-stop 4 frames; death tick 1 f21402: **`p_inv` = 60**, slow-mo 60;
  tick 60 (f21501): **adds untouched** (n stays 6); tick 120 (f21561) freed. `estage` stays 0.
- **Last death** (ghost 1, f25204, state 1): death tick 1 f25209: `p_inv` 1200, slow-mo 60; tick 60 (f25308 → 25309): all 6 adds dying → gone;
  tick 120 (f25368): freed, `estage` 1, `gate` 0, circle at (−35, −40).
- Damage dealt (ATK 61): sword ≈25–31, spin 53 / 58 (no labels needed); 1,800 HP took 34 searches (~7,500 frames).
- Arms checklist updates: count 2 placement **[obs f17431]**; cap 6 **[obs f18084]**; ghost variant 0 **[obs f18084]**; death with another
  alive **[obs f21402 / f21501]**.
