# Golem / Dark Golem — types 61 + 62 + 63, 64 + 65 + 66 (ゴーレム / ダークゴーレム, 右 / 左)

**code-read**: IDA `sub_420F4D` (body AI, 0x420f4d), `sub_430C6D` part branch (IDA 31926.., 32040..32130),
`sub_42F7AD` case 61 / 64, tables at 0x52b8a4. Disassembly checked at 0x420fab..0x421004 (arm scan) and
0x42130d / 0x421316 (the side write). **Port (2026-11-11): body AI `actor_ai_golem`, part branch `actor_golem_part` (`src/actor_ai.c`), death arm — verified by
`obsidian-16f-20f-golem` portrng + portsave IDENTICAL (type 61; the adds type 36). Dark golem 64 / 65 / 66, the † arm-lost /
detached-arm branches: no recording.**
Shared rules: `boss-floor-common.md`.
**Run live 2026-11-11** (Obsidian 20F, Louie L40, ATK 56 / DEF 58): probe `docs/findings/probes/obsidian-16f-20f.md`, run
`golem-20f` (15177 frames, `rd verify` MATCH). Tags **[obs fNNNN]** = that run (frames there); corrections from retail are marked
**(corrected)**.

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
| body 61 / 64 | 150 / **30** / 30 | 750 | 1290 |
| arm 62 / 63, 65 / 66 | 150 / **30** / 30 | 750 | 1290 |

**(corrected)** Retail record (`0x5c23f0 + type*0x68`, read live) for 61 and 62 alike: HP 150, EXP 35, **AT 30**, DF 30, MA 0,
MD 30, rf 0.9 / 4 / 3 / 4.5. 20F: all three parts HP 750 = L20 **[obs f10062]** (the wiki's "Lv 32 / 1080" is not this floor).
Add type 36: HP 15, EXP 20, AT 16, DF 20, MA 16, MD 20, rf 1.5 / 2 / 2 / 0.18 (HP 75 on 20F).

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
- **[obs]** The arms never take damage: HP 750 / 750 and `unhit` (`+0xb08`) 1 on both from f10062 to the death, through every
  body hit (e.g. f10735: sword at `a0d` 3.52, body 750 → 726). The arm actors sit at the body's x / z (actor table f10693) with the
  same rf, so every swing that reached the body also reached both arms' volumes. Their `+0xa54` stays 8 (only the body's AI runs).
  The † branches were never taken.

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
| 1 | **(corrected)** side = **0** too → a second right punch (IDA: `if (v12 == 1) side = 0`) **[obs f10553]** `a0sub` 0, the right arm's joint struck at f10653 |
| 2 | side = `rand() & 1` (the only RNG pick), then as 0 / 1 |
| 3 | double (state 2, effect 44) while at least one arm lives; † both dead → state 1 without a fist |
| 4 | jump at the player (state 3, side 0) |
| 5 | jump back to the spawn point (state 3, side 1) |
| 6 | state 4 + SE 349 (not in the table) |

One full pattern = 560 walk ticks + 6 punches (161) + 1 double (181) + 5 jumps (141) ≈ 2,410 ticks by the table.
**[obs]** idx 0 punches started f10363 and f13329 = 2,966 frames, of which 434 were the pause menu (f11306..11740) and the rest
of the excess hit-stop (4 frames per hit landed or taken) and slow ticks — consistent with ≈2,410 AI ticks (not counted exactly). Index steps seen: 0 (f10363),
1 (f10553), 2 (f10735), 3 double (f11005), 4 jump-at (f11211), 5 jump home (f11791), 6 left punch (f11952), …, 9 / 10 jumps,
11 jump home, then 0 again.

**Adds** (state 0 only): n = live actors of type 36 (golem: `DAT_0052b8a4`) or 35 (dark: `DAT_0052b8a8`) — both
types counted for both golems. `need` = 1 (golem) / 2 (dark). While `n < need`: `+0xa64` counts to 30, then
`+0xa68++`; `+0xa68 > 2·need` → next wait +60, `> 4·need` → +120; if `+0xa68 < 5·need` spawn one moai (golem 36 / dark
35) at radius 2..10 around (−40, −40), > 10 from the player (30 tries, `FUN_00471089` ×2 each), y 1, `+0xb2c = 1`.
Total over the fight: 4 (golem) / 9 (dark). Moai AI `FUN_0042cfd0` (stub; not read here).
**[obs]** add #1 ≈f10340 (≈37 ticks into the first state 0), slot 3 at (−44.61, −31.75), `gn` 1; killed f12524 → add #2 ≈f12736
(slot 4), `gn` 2; killed → **`gn` 3, `gadd` −60** and add #3 (slot 5) at f13376. The adds never move. They shoot record 31 (side 1,
owner = the add) along their facing at 0.15 / tick, reach −0.35, volleys of 4 spaced 1.2 (IDA: ticks 2 / 10 / 18 / 26 of a 60-tick
cycle while `+0xa5c == 1`): **16** damage to DEF/MDEF 58/32 (`ftol(16·3.0/2 − 32/4)`, undone branch f13047). The boss death frees
live adds (add #3, HP 75 untouched, use 1 → 0 between f13664 and f13679).

## 4. Attacks

| attack | record | where / when | notes |
|---|---|---|---|
| fist (punch) | 14 from the **arm** actor, placed at its joint `f32(arm + 0x6fc + 12·side)` (x, y, z) | state 1 tick 100, once | **(corrected)** arm AT is 30: `ftol(30·3.0/2 − 58/4)` = 30 +0..5 → **[obs f10463]** 32, **[obs f10653]** 34 (right), **[obs f12061]** 30 (left); knock-down 70 |
| fists (double) | same, both arms | state 2 ticks 76..104, every tick | one record per arm per tick (slots 8..15 at t 78..80) **[obs f11085]** 35, actor 1's record 3.64 from the player; then `p_inv` 30 + `p_hurt` 70 cover the rest |
| stomp | 14 from the body | state 3 tick 90 | body AT 30 → **[obs f11302]** 34 (jump at the player; the body landed 0.24 from the player); knock-down 70 |
| contact | — | none | **[obs]** no damage while standing 0.24..3.6 from the body centre (f11005..11115 undone branch, f12584..13574) |

Record 14 **[obs]**: reach **3.5**, lives ~5 ticks (age 1 at t 100 → gone by t 106, `golem-scan` @10459), so a fist still hits a
player whose `p_inv` runs out one tick later (f12061, t 101). Right fist at its joint (−39.14, 2.48, −47.23) hit at 2.37; the
stomp record sits at the body, y +4.76 (jump home f11880). **Double geometry [obs]**: the fist joints sweep a circle ~8.5–9 from
the body centre (t 81..105); standing within ~3.6 of the centre the double never hits.

## 5. When it can be damaged

- Body: any time after state 8 (`+0xb08` is never set on the body; `+0xa98` only gates physics). During state 8 the
  body is below the floor for 60 ticks.
- Arms: never (§2). **[obs]** 750 / 750 to the end.
- Front / side / back multipliers and the "state 1..6 → ×1.2" bonus apply (states 1, 2, 3 are in 1..6).
- **[obs]** Louie ATK 56: sword `ftol(28 − 7.5)` = 20 → 24 as a counter (f10735, state 1 tick 1, front); spin 40 → **72**
  counter + back in state 3 (f12905). Body volume reaches a sword at `a0d` 3.52 from the front.

## 6. Kill recipe (search-driven) — **[obs]** done f10303 → f13574 (3,271 frames incl. a 434-frame pause), no hit taken after f12061

1. Wait out the intro: `s32(B+0xa54) != 8` (B = actor 0; check `s32(B+0x424) in (61, 64)`). **[obs]** state 0 and `lock` −1 at
   f10303 (240 ticks after arrival).
2. Cell used: `(gidx, a0st, a0t//15, int(a0hp/12), int((px-a0x)/1.5), int((pz-a0z)/1.5), p_act)`, score `-a0hp`, actions = 8 walks
   ×6, `{dpad(a0x - px, a0z - pz) | A} .*6`, the same with `B` (spin), a back-off `{dpad(px - a0x, pz - a0z)}*8`, `.*6`; `--avoid
   "hp < H"`; goals 70–80 HP apart: each found in 21–33 s (`-j 2`).
3. Stand close: within ~3.6 of the centre the double misses (fists at ~8.5–9); the fist lands ~2–5 in front of the body; the stomp
   lands on where the player stood during t 20..79 (jump-at) or on the spawn point (−40, −35) (jump home).
4. Avoid: every hit is ~30–35 (**not** 1 — see §4) with knock-down 70; the adds' shots (16).
5. Adds: kill them (4 swings, HP 75) or ignore — the boss's death frees the live ones **[obs f13664..13679]**.
6. Done: `s32(B+0x428) == 2` **[obs f13574]**; **(corrected)** the arms copy 2 between death tick 46 and 61
   (f13664..13679), alpha fades from ~tick 100 (199 at 106, 79 at 121), all three freed at death tick ~131 → `s32(0x438b8a0) == 1`,
   `gate` 0 **[obs f13754]**. `p_inv` 1200 at death tick 1, slow-mo 60.
7. Return portal (−45, −40): `A` from −z facing +z at (−44.95, −40.91) → "Return to town now?" **[obs f13971]**.

## 7. Arms checklist (every AI branch + a predicate)

| branch | predicate | seen |
|---|---|---|
| rise | `s32(B+0xa54) == 8 and f32(B+0x3f4) < 0` | **[obs f10062..10303]** y −12 → −6 (t 31) → 0 (t 61); 240 ticks |
| walk, pattern advance | `s32(B+0xa54) == 0` and `s32(B+0xa60)` steps 0 → 11 → 0 | **[obs f10303..13376]** idx 1..11, wrapped to 1 at f13376 (idx 0 punch f13329) |
| right punch (act 0) | `s32(B+0xa54) == 1 and s32(B+0xa5c) == 0`, anim `s32(B+0x34 + 4·s32(B+0x30)) == 1` | **[obs f10363]**, hit f10463 (32) |
| act 1 | **(corrected)** also `a5c == 0` (right) | **[obs f10553]**, hit f10653 (34) |
| left punch (act 2 only) | `s32(B+0xa54) == 1 and s32(B+0xa5c) == 1` | **[obs f11952]** (idx 6), hit f12061 (30), left joint actor 2 + 0x708 |
| random side (act 2, idx 2 / 6 / 7 / 8) | both sides seen at those indices | **[obs]** idx 2 → 0 (f10735), idx 6 → 1 (f11952; a search picked the branch) |
| hop + fist | state 1: `f32(B+0x400) == 0.5` at t 80; a record 14 owned by the arm at t 100 | **[obs f10443..10463]** y 0.45 (t 81), 1.89 (t 91); record 14 owner actor 1 at t 100 |
| double (act 3) | `s32(B+0xa54) == 2`, t 76..104 | **[obs f11005..11185]**, hit f11085 (35); records of both arms per tick |
| jump at the player (act 4) | `s32(B+0xa54) == 3 and s32(B+0xa5c) == 0`, `f32(B+0x400) > 2` at t 20 | **[obs f11211..11351]** peak y 32.9 (t 55), stomp hit f11302 (34) |
| jump home (act 5) | `s32(B+0xa54) == 3 and s32(B+0xa5c) == 1` | **[obs f11791]** lands t 90 (f11880) at (−39.94, −35.63); not taken (no HP food) |
| add spawn | an actor with `s32(+0x424) in (35, 36) and s32(+0xb2c) == 1`; `s32(B+0xa68)` 1..4 (dark: ..9) | **[obs]** type 36 ≈f10340 / ≈f12736 / f13376, `gn` 1 / 2 / 3 |
| add back-off | `s32(B+0xa64) in (−60, −120)` | −60 **[obs f13376]** (`gn` 3); −120 **not reached** (needs `gn` 5; the body died at `gn` 3) |
| dark variant | type 64: `need` 2, add type 35 | **not reached** (Lapis 70F) |
| death | `s32(B+0x428) == 2`; arms `s32(+0x3dc)` fading with the body from death tick 100 | **[obs f13574..13754]** (arms → 2 at t 46..61, see §6) |
| † arm lost / state 4 / act 3 fallback | `f32(arm+0x42c) <= 0`, `s32(arm+0xa04) == −1`, `s32(B+0xa54) == 4` | **not reached**: arm HP 750 and `+0xb08` 1 the whole fight; nothing a player does reaches it |

## 8. Unresolved

- How an arm takes damage (§2): **[obs]** it does not, in the 20F fight (the † branches look dead for 61 / 62 / 63).
- ~~Record 14's reach / lifetime; contact damage~~: reach 3.5, ~5 ticks, no contact damage **[obs]** (§4).
- State 7 and act 6: no writer / no table entry.
- `FUN_0041f759` anim ids (0 idle, 1 / 2 wind-up, 3 rise, 6 jump, 7 double, 8 / 9 recover, 10 death) named from use.
