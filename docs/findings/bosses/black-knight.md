# Black Knight — type 49 (ブラックナイト)

**code-read**: IDA `sub_4279CF` (AI, 0x4279cf), `sub_423789` (walk-and-align helper; described in `arma.md` §3),
`sub_43865E` shield block (IDA 39173..39196), `sub_445A8C` case 81, `sub_42F7AD` case 23 / 49 / 55 / 54, table
0x52b978. **Port (2026-11-11): AI `actor_ai_black_knight` (`src/actor_ai.c`), death arm case 49 (shared 23 / 49 / 54 / 55) — verified `obsidian-41f-50f-finale` (triage identical + save; intro, knight upkeep, walk / throw / block / burst, death).** Shared rules: `boss-floor-common.md`
(generic boss floor). **Run 2026-11-11 (`obs-50f`, 45F): §10 — retail stats override §2 / §5 (HP 100, AT 35, MD 20: HP 640 at L27).**

## 1. Where (HP each = 15·(1 + 0.2·lv))

| place | `dungeon` / `floor` | count | HP each | knights kept alive |
|---|---|---|---|---|
| Obsidian Tower 45F | 3 / 44 | 1 | lv 27 → **96** | 3 |
| Lapis Ruins 55F | 4 / 54 | 2 | lv 35 → **120** | 5 |
| dungeon 5 floors 44 / 54 | 5 / … | 1 / 2 | own: lv 73 / 90 → 234 / 285; rush (d3 f44) lv 34 → 117, (d4 f54) lv 50 → 165 | 3 |

Generic placement (−40, 0, −34) (2: x −45 / −35), state 8.

## 2. Stats

Table: HP 15, EXP 30, **AT 2**, DF 20, MA 0, MD 10, boss flag 0; rf0 1.2 → radius 2.4, half-height 2.4. Every AI tick
`+0xb54 = 1` (no stagger / knock-back / state reset by hits).

## 3. Shield — `s32(B+0xb9c)`

Written every AI tick: 1, except **0 in state 1 once `+0xa58 ≥ 20`** and **0 in state 2**.

Hit handler (`FUN_0043865e`): shield ≠ 0 and not frozen (`+0xb18 == 0`): `a = atan2(actor − record) − yaw + π`
(wrapped to ±π); **|a| < 1.0996 (63°) → blocked**: damage 0 (part flag: sparks 41 / 42, SE 359), anim 4,
**`+0xa54 = 4`, `+0xa58 = 0`**. Outside ±63° of the front the hit is normal (side ×1.2 beyond 45°, back ×1.5 beyond
135°).

## 4. State machine — `s32(B+0xa54)`, tick `s32(B+0xa58)`

| state | length | what |
|---|---|---|
| 8 intro | 240 | t == 240 → 0. No knights are summoned during it |
| 0 hunt | — | anim 0; `FUN_00423789(a, 20.8, 20, 0.025, 5, 30, 0)`: walk legs (heading re-picked at t 1 with `rand`s, accelerate 0.025/tick from t 6, leg = 30 + (rand & 63) ticks); from t 20: aligned = on one of its axes (off-axis < 1.5) at 2.0..20.8 → yaw at the player; aligned and on the ground → **state 1**, t = 0, sub = 0, anim 3 |
| 1 throw | 91 | anim 3. **t == 25: record 81** (5 shots: octant direction, ∓0.18, ∓0.36 rad, speed 0.5, origin 0.3 ahead, y + 0.7). **Shield down from t 20.** t ≥ 91 → 0 |
| 2 burst | 101 | shield down but **`+0xb08 = 1` from t 1**; anim 5; effect 68. **t == 40: record 14 at the knight**, effects 66 / 67 / 69, SE 675. t == 101: `+0xb08 = 0` → 0 |
| 3 lunge | 61 | anim 3; t == 20: record 13; t 21..35: accelerate 0.08/tick along yaw; t ≥ 61 → 0. **No writer of state 3 found** (the AI never enters it; the block sets 4) |
| 4 blocked | 20 | set by the shield block. t == 20: t = 0; **player nearer than 5.0 and `rand % 3 == 0` → state 2**; else state 0, anim 1. (The `rand` is drawn only when the player is nearer than 5.) |

**Knights** (every AI tick, any state but 8): n = live actors of types 44..48 (0x52b978); `n < 3` (dungeon 4: `< 5`)
→ **one knight per tick**: up to 30 tries of {angle 2π·rnd, r = 2 + 8·rnd around (−40, −40); accept when > 10
from the player}, `FUN_0041f903(x, 10, z, 44 + rand % 5)`, `+0xb2c = 1`. Dead knights are replaced on the next tick,
without limit. With two black knights both run the test (same count), so still 5 in all. Knight AI
`FUN_00427f59` (ported; other author's ground).

## 5. Attacks

| attack | record | notes |
|---|---|---|
| throw | 81 ×5, speed 0.5 | physical, **AT 2**·mul → `ftol(2·mul/2 − DF/4)` ≤ 0 for any DF ≥ 4·mul → 1 damage (+0/1) |
| burst | 14 at the knight, 40 ticks after the block-counter starts | same AT 2 → 1 damage; knock-down 70 |
| knights 44..48 | their own records (AT 2 as well) | the crowd, not the numbers |

## 6. When it can be damaged

- From the side / back (|a| ≥ 63°): states 0, 1, 4 (and 8).
- From the front: only state 1 from t 20 (71 ticks).
- Never in state 2 (`+0xb08`).
- A frontal hit with the shield up costs nothing but sends it to state 4 and, at ≤ 5 range, a ⅓ chance of the
  untouchable 101-tick burst.

## 7. Kill recipe

1. Wait `s32(B+0xa54) != 8`.
2. Bait the throw: stand on one of its axes at 2..20.8 → `s32(B+0xa54) == 1`; its yaw is frozen at the octant toward
   the player. Step off the axis (the 5 shots cover ±0.36 rad), close in during t 0..25, and hit from anywhere
   once `s32(B+0xa58) >= 20` (shield down), or from behind at any time. HP 96 / 120.
3. Predicate "safe to swing": `s32(B+0xb08) == 0 and (s32(B+0xb9c) == 0 or behind)`, behind =
   `abs(wrap(atan2(f32(B+0x3f0) − px, f32(B+0x3f8) − pz) − f32(B+0x420) + pi)) >= 1.1` (argument order per
   `boss-floor-common.md`'s yaw convention; validate once on a known blocked hit: HP unchanged, state → 4).
4. Cell: `(s32(B+0xa54), s32(B+0xb9c), s32(B+0xa58)//10, int(f32(B+0x42c))//10, int(px − f32(B+0x3f0)), int(pz − f32(B+0x3f8)))`;
   score `-f32(B+0x42c)`; avoid `hp <= 0`.
5. Ignore the knights (they are replaced instantly); the boss's death tick 60 kills them all.
6. Done: `s32(B+0x428) == 2`, then no actor left → `s32(0x438b8a0) == 1`.

## 8. Arms checklist

| branch | predicate |
|---|---|
| intro, no knights | `s32(B+0xa54) == 8` and no actor of type 44..48 |
| knight upkeep 3 (d4: 5) | live 44..48 count returns to 3 / 5 one tick after a kill; all five colours seen |
| walk leg / align | `s32(B+0xa54) == 0`, `s32(B+0xa5c)` counts legs |
| throw, shield drop | `s32(B+0xa54) == 1`, `s32(B+0xb9c)` 1 → 0 at t 20, record 81 at t 25 |
| frontal block | HP unchanged, `s32(B+0xa54) == 4 and s32(B+0xa58) == 0` |
| side hit / back hit through the shield | HP drops with `s32(B+0xb9c) == 1` |
| blocked → hunt (far, or rand % 3 ≠ 0) | 4 → 0 |
| blocked → burst (near and rand % 3 == 0) | 4 → 2; `s32(B+0xb08) == 1` for t 1..100; record 14 at t 40 |
| frozen (`+0xb18 > 0`) bypasses the shield | frontal hit lands while frozen (needs a freeze item) |
| two knights (d4 f54) | both actors; player invulnerable 60 at the first death, 1200 at the last |
| state 3 | `s32(B+0xa54) == 3` — report if ever seen |

## 9. Unresolved

- Record 81 / 14 / 13 in flight.
- State 3's writer.
- `+0xb8c` (cleared each tick here; set elsewhere?).
- The exact sign convention of the block angle (which of record→actor / actor→record) — taken from
  `boss-floor-common.md`; one live blocked hit settles it.

## 10. Retail run (run `obs-50f`, 45F, 2026-11-11)

Louie L41 → L42 (ATK 60 / DEF 68 / MDEF 41, HP 202 → 206). Probe `docs/findings/probes/obsidian-41f-50f.md`. `rd` names (run-local
`vars.json`): `bksh` = `s32(B+0xb9c)`, `bkunt` = `s32(B+0xb08)`, `kn` = `actors(T >= 44 and T <= 48 and U == 1)`, `r81` = records of
type 81 in table-B slots 0..47; B = actor 0 (`a0*`).

**Stats (retail, `0x5c23f0 + 49*0x68`) override §2 / §5:** HP **100**, EXP 30, **AT 35**, DF 20, MA 0, **MD 20**, rf 1.2 / 1.5 / 2 / 2,
flags 0. 45F (L27): **HP 640** (`+0x434`), not 96. The throw is not harmless: **47 per hit** (ftol(35·3.7/2 − 68/4) = 47, +0..9).
Knights (retail): 44 HP 12 EXP 10 AT 17 DF 18 · 45 14 / 15 / 19 / 20 · 46 18 / 20 / 21 / 23 · 47 35 / 25 / 24 / 27 · 48 15 / 30 / 27 / 30
(MD 10); MA 0, rf[0] 0.8 / 0.95 / 0.8 / 0.9 / 0.92. At L27: 77 / 90 / 115 / 224 / 96 HP.

| arm | status |
|---|---|
| intro, no knights | **[obs f15713..15953]** placed (−40, 0, −34) state 8, party (−40, −45); `kn` 0 through the intro; 8 → 0 at t 240 = `etick` 240 (f15953), `lock` −1 the same tick |
| knight upkeep 3 | **[obs f15954..15956]** one knight per tick (types 48, 44, 46), dropped from y ≈ 9.5 at 5..7 from the BK. **[obs f16290..16297]** knight 48 killed (dying f16290) → freed at its death tick 2 (f16296, after 4 hit-stop ticks) → a type-46 knight in slot 4 on f16297: **a dying knight still counts**, the refill comes the tick after the free. Colours seen: 44, 45, 46, 48 (47 never rolled) |
| walk / align | **[obs]** state 0 between every attack; `+0xa5c` legs not read |
| throw, shield drop | **[obs f16080]** state 1 (player 2.4 off-axis but on its z row, 4.3 away); `bksh` reads 1 at t 20 and **0 from t 21** (as `rd` reads it after the tick, f16105); **one** record 81 in slots 0..47 at t 25 (f16224; the 5-shot fan not seen in those slots); state 1 → 0 after t 90 (f16174 → f16175: 91 ticks) |
| wave guarded by Louie | **[obs f16225]** facing it (octant toward the BK) at 4.3: the record spawns at t 25, next tick `p_inv` 15, no damage (Louie's projectile guard, `combat-core.md` §4) |
| wave hit taken | **[obs f16405]** state 1 t 25, player facing away (octant 4) 4.2 from it: **168 → 121 (47)**, `p_hurt` 30, `p_inv` 30 on the spawn tick |
| frontal block | **[obs f16008]** sword at `p_actt` 9, `a0rel` 0.56 (front), `bksh` 1: **HP 640 unchanged**, state 0 t 46 → **4 t 0**; t frozen 4 ticks (hit-stop), state 4 → 0 at t 20 (f16032; player 3.08 away, the `rand % 3` ≠ 0) |
| side hit through the shield | **[obs f16048]** `a0rel` 1.27 (> 1.0996), `bksh` 1, state 0: 640 → 606 (34 = ftol(25·1.2) = 30, +0..6); again f16193 (`a0rel` 1.41): 606 → 571 (35). The block angle is the `aNrel` angle (record → actor vs yaw): settled |
| frontal hit, state 1 t ≥ 20 | **[obs f16239]** state 1 t 36, `bksh` 0, `a0rel` 0.048: 571 → 538 (33 = ftol(25·1.2) counter = 30, +3) |
| blocked → hunt | **[obs f16032]** (above) |
| blocked → burst | **[obs f16519 → f16539]** block, player 2.29 away → state 2 at t 20; `bkunt` 1 for t 1..100, `bksh` 0; **record 14 at t 40 (f16579, gone after t 46)**; player 6.07 away → no damage; state 2 → 0 at t 101 (f16640), `bkunt` 0 |
| frozen bypass | not reached (no freeze item) |
| state 3 | never seen (f15713..17289) |
| death | **[obs f17289]** dying; slow-mo, `p_inv` 1200 (1191 at f17321); **death tick 60 (f17393): `kn` 3 → 0** (all knights dying); tick 120 (f17453): freed, **`estage` 1 and `gate` 0 the same tick**. EXP: Louie L41 → L42 at f17366 (HP 121 → 206, SP 11 → 46 refilled, ATK 60 → 61) |
| floor exit | object 0 = kind 8 at (−35, −40): **"Go to the next floor?"** (`portal` > 0 at (−35.86, −40.44), f17617); the return portal (kind 6, (−45, −40)) not visited |

Damage dealt (ATK 60 vs DF 20 → sword 25): side 34 / 35, counter front 33; a spin (×2) killed knight 48, took knight 46 83 → 20 and
the BK 538 → 477 (61) at once (f16290); spins from behind 101 (477 → 376, f16689) and 101 / 139 per later chunk. Damage taken: knight 48
contact / lunge 202 → 168 (34 = ftol(27·3.7/2 − 17) = 32, +0..6, f16100), the wave 47. Whole fight f15713 → f17453 (1,740 frames).

## 11. Retail run (run `lapis-60f`, Lapis 55F pair, 2026-11-13)

Louie L44, ATK 62 / DEF 82, HP 215. Probe `docs/findings/probes/lapis-51f-60f.md`. Slot 0 = the BK at x −45, slot 1 = x −35.

| arm | status |
|---|---|
| placement / HP | **[obs f12566]** (−45 / −35, 0, −34), state 8, **HP 800 each** = 100·(1 + 0.2·35) (§1's 120 used the wrong table HP) |
| intro, no knights | **[obs f12566..12806]** `kn` 0; both 8 → 0 at etick 240 (f12806) |
| knight upkeep (d4: 5) | **[obs f12807..12809]** `kn` 0 → 2 → 4 → 5: **both BKs spawn one each per tick** while the shared count < 5 (§4's "still 5 in all" holds; the fill takes 3 ticks, not 5). Refill the tick after a kill (f12931 → 12938 etc.). Colours: 45, 46, 47, 48 (47 seen here) |
| throw, shield drop | **[obs f12856..12877]** both in state 1 together; `bksh` 1 → 0 at t 21 (after-tick read) |
| frontal block → hunt | **[obs f13110 → 13134]** slot 1: HP unchanged, 0 → 4 → 0 |
| side hit through the shield | **[obs f12843]** 800 → 738 with `bksh0` 1 |
| two BKs: invulnerability | **[obs]** first death (slot 1, f13777): `p_inv` **60** (f13782); last (slot 0, f14005): **1200** (f14010) |
| death sweep | **[obs f14113]** `kn` 5 → 0 at the last BK's death tick 60; freed f14173 → `estage` 1, `gate` 0 the same tick |
| blocked → burst, frozen bypass, state 3 | not seen |
