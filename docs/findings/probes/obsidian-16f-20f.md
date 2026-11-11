# Probe: obsidian-16f-20f

- **Run:** `golem-20f` · **start:** `save:2b79eafc5fb0` (end save of `ob-15f`: town map, day 16 `stime` 2, crafted lineage) · no task
  file (goal: Obsidian Tower 16F → 20F, the golem's arms, back to town, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`), `rd search -j 2`
- **Side run:** `golem-scan` (reads only: `--from golem-20f@11745` / `@10459` / `@11082`, idle ticks + hit-record dumps; stopped)
- **Movie:** 15177 frames. **Not filed.**
- **Verdict (2026-11-11):** `rd verify golem-20f` (headless):
  ```
  fresh headless replay: frame 15177 rng 8b4896f4 sem 14e9f5b5d8ef34cb dev 76d3e23b668b6794  (unskipped retail)
  live session:          frame 15177 rng 8b4896f4 sem 14e9f5b5d8ef34cb dev 76d3e23b668b6794  (skips 414ee2: dev not compared)
  MATCH
  ```
- **End save:** `save:671c5819784b` (slot 020 overwritten from the shop's pause menu, day 17 `stime` 0 = the morning after;
  TIME 2:35:03).
- **Card:** `docs/findings/bosses/golem.md` (arms marked, stats corrected).
- **Dialogues:** none on the load, the guild, the floors or the boss floor. The return at night plays the normal end-of-day lines
  ("Thanks for all your help today, Tear!", f14244), then the day-17 morning (f14659) with no event.

## Before the dungeon

- Load `.*60 A .*30 A .*120` → town map f137 (`wms` 1121211: the guild is open at `stime` 2). `D D D R` → `wm_cur` 3, `U` → 6,
  `A` → guild f269.
- **Hired Louie** (L40: HP 197, SP 45, ATK 56, DEF 58, MATK 21, MDEF 32). Not Caillou: against DF 30 his sword does
  `ftol(25/2 − 30/4)` = 5, his HP is 80 (a golem hit is ~30–35, see below). Louie's sword does 20 and his spin 40, and his spin
  gives 30 ticks of `p_inv`.
- Food in stock: **Candy and Chocolate Bar only**. Both packed. **Both are SP items**: used at HP 62 / SP 45 full →
  "Can't recover any further." (f11608 / ≈f11690), HP unchanged. So the trip had **no HP healing**.
- Depart f572: fee 3,950 (458150 → 454200), `stime` 2 → 3, floor row `u32(0x6a49b54)` 2 → 3 ("16~20").
- Equip prompt: `A` at f653 and f774 were ignored (the box was still up at f945, picture `golem-20f/view/f000945.png`);
  `rd tap A --gap 10` closed it, control at f956.

## Per floor

Enemy lists = the actor table after the banner (`type × count (HP)`). Route = BFS of the maze grid + one scored search per
1–2 cells (`-j 2`, 60 s): 15 legs, 15 finds on the first try. Nothing killed, no HP lost on 16F–19F.

| floor (`floor`) | frames | banner | enemies (type × count (HP)) | grid / route |
|---|---|---|---|---|
| 16F (15) | 956 → 3474 | "SP consumption halved!" (f957..1023) | 52: 1×5 (96), 2×4 (72), 8×9 (86), 18×7 (72), 35×6 (72), 45×5 (67), 46×6 (86), 52×6 (96), 53×4 (72) = L19 | 9 × 9, (1, 1) → (4, 3), 4 legs |
| 17F (16) | 3474 → 6824 | "Defense power doubled for everyone!" (to f3546) | 48: 1×7 (100), 2×5 (75), 8×5 (90), 18×5 (75), 35×6 (75), 45×5 (70), 46×4 (90), 52×6 (100), 53×5 (75) = L20 | 9 × 9, (5, 1) → (3, 3), 5 legs |
| 18F (17) | 6824 → 9121 | "Enemy defense power doubled!" (to f6893) | 46: 1×4, 2×6, 8×4, 18×5, 35×7, 45×7, 46×5, 52×4, 53×4 (L20) | 9 × 9, (1, 4) → (6, 3), 4 legs |
| 19F (18) | 9121 → 10062 | "EXP gain has doubled!" (to f9187) | 39: 1×3, 2×5, 8×5, 18×3, 35×6, 45×4, 46×6, 52×3, 53×4 (L20) | 9 × 9, (3, 4) → (5, 3), 3 legs |
| 20F (19) | 10062 → 14123 (results) | none | golem body 61 + arms 62 / 63, HP 750 each (L20); adds type 36 (HP 75) | below |

No new enemy type on 16F–19F (all in the 11F–15F list). **New on 20F: type 36** (the golem's add, below).

## Stats read (`0x5c23f0 + type*0x68`, +0x2c HP, EXP, AT, DF, MA, MD; rf +0x44..)

| type | HP | EXP | AT | DF | MA | MD | rf[0..3] |
|---|---|---|---|---|---|---|---|
| 61 golem body | 150 | 35 | **30** | 30 | 0 | 30 | 0.9, 4, 3, 4.5 |
| 62 / 63 arms | 150 | 35 | **30** | 30 | 0 | 30 | 0.9, 4, 3, 4.5 |
| 36 add ("moai") | 15 | 20 | 16 | 20 | 16 | 20 | 1.5, 2, 2, 0.18 |

Pattern table at 0x52b8ac (`dur`, `act` pairs): (60,0) (20,1) (20,2) (100,3) (20,4) (20,5) (20,2) (100,2) (100,2) (60,4) (20,4) (20,5); add
types at 0x52b8a4 / a8: 36 / 35.

## 20F timeline (frames)

`B = 0x76b970` (actor 0, body), arms = actors 1 (62, right) and 2 (63, left). Run-local names: `gidx` (`+0xa60`), `gadd` (`+0xa64`),
`gn` (`+0xa68`), `gvy` (`+0x400`); `a0sub` = side (`+0xa5c`). Right fist joint `f32(0x76cc10 / 14 / 18)` (actor 1 + 0x6fc), left fist
joint `f32(0x76d7c0 / c4 / c8)` (actor 2 + 0x708).

| frame | what |
|---|---|
| 10062 | on 20F: body and both arms at (−40, −12, −35), state 8, HP 750 / 750 / 750; arms `unhit` 1; player (−40, −45); `lock` 0 |
| 10093 / 10123 | y −6 (t 31) / 0 (t 61): risen; arms follow (y −6 / 0) |
| 10303 | state 0 (t 240), `lock` −1. No dialogue |
| ≈10340 | add #1: type 36 in slot 3 at (−44.61, −31.75), `gn` 1 (age 353 at f10693) |
| 10363 | idx 0 → state 1, side 0 (right). Yaw −2.98 at t 10; hop: y 0.45 at t 81, 1.89 at t 91 |
| **10463** | **right fist**: record 14 owned by actor 1 at (−39.14, 2.48, −47.23), reach 3.5, age 1; player at (−40, −45) 2.37 away: **197 → 165 (32)**. The record lives ~5 ticks (age 4 at t 104, gone at t 106) |
| 10553 | idx 1 → state 1, **side 0 again** (`act 1` is a right punch) |
| **10653** | right fist: **165 → 131 (34)**, `p_hurt` 70, `p_inv` 30 |
| 10735 | idx 2 (act 2, rand → side 0); sword on the body at `a0d` 3.52, rel 0.13, at the state's tick 1: **750 → 726 (24 = 20·1.2 counter)**; arms 750 / 750 |
| 10755..10904 | search: two hits 726 → 674 |
| 11005 | idx 3: state 2 (double), t 1. Fists sweep a circle ~8.5–9 from the body centre (joints at t 81..105: (−34.6, −51.0), (−43.8, −34.4), (−30.1, −45.9), (−48.0, −39.4) …). On an undone branch (f10934..11115) the player stood 3.0–3.6 from the centre through t 1..105: **no hit** |
| **11085** | **double**, t 80: player 6.6 from the centre; records 14 of both arms spawn 1 per arm per tick (slots 8..15); the one that hit: actor 1's at (−28.32, 1.61, −48.35), 3.64 from the player: **131 → 96 (35)**, `p_hurt` 70 |
| 11211 | idx 4: state 3 side 0 (jump at the player). y 10.3 (t 25), 32.9 (t 55, peak), 11.1 (t 85) |
| **11302** | **stomp**, t 91 read (code t 90): body landed 0.24 from the player: **96 → 62 (34)**, `p_hurt` 70 |
| 11306..11740 | pause: Candy / Chocolate Bar refused ("Can't recover any further.") |
| 11791 | idx 5: state 3 side 1 (jump home); lands t 90 (f11880) at (−39.94, −35.63) ≈ spawn (−40, −35): record 14 owned by the body at (−39.94, 4.76, −35.64), reach 3.5 (`golem-scan` @11745, the same timeline with idle inputs) |
| 11952 | idx 6 (act 2, rand → **side 1**): state 1 left punch, t 1 (found by search) |
| **12061** | **left fist**, t 101 read: player at (−43.75, −45.35), joint (−44.44, −43.86) at t 100: **62 → 32 (30)**, `p_hurt` 70 (the record still hit one tick after `p_inv` ran out) |
| 12524 | add #1 killed (search, 4 swings; HP 75) |
| 12584 → 12621 | sword + spin: 674 → 597 |
| 12688 | → 515 |
| ≈12736 | add #2 (slot 4) at (−42.32, −42.18), `gn` 2 |
| 12852 / 12905 | → 430 / → 358: **one spin, 72 = 40·1.2·1.5** (counter + back, during state 3) |
| 13047 (undone) | add #2's shot: **32 → 16 (16)** (record 31; branch cut at ck `d358`) |
| 13376 | add #2 killed; **`gn` 3, `gadd` −60** (back-off); add #3 at slot 5; body 248; `gidx` 1 = pattern wrapped (idx 0 punch again) |
| 13401 / 13481 | → 168 / → 92 |
| **13574** | **body 0** (`a0use` 2); spins cost 5 SP each (45 → 15 by f13511) |
| 13589 | slow-mo `0x438becc` 50 (→ 0 at f13649), `p_inv` 1197 (1200 at death tick 1) |
| 13664..13679 | **arms `+0x428` 1 → 2** (death timer 46 → 61); **add #3 freed** (use 1 → 0, HP 75, never hit) |
| 13724 / 13739 | alpha (`+0x3dc`) 199 / 79 on the body and both arms (death timer 106 / 121) |
| **13754** | body and arms freed (death timer 131), **`estage` 1, `gate` 0** |
| 13919..13940 | walking +z at x −45 stops at z −41.6 (portal (−45, −40)); `A` there: nothing |
| 13971 | (search) at (−44.95, −40.91) facing +z (`p_oct` 0): "Return to town now?" (`portal` 2) |
| 14123 / 14200 | `scene` 9 results, bag 2/20 / `B`: shop at night (`stime` 3 → 4) |
| 14244 | session crash (wine `c0000005`, read 0x50012000) in the next command; the rebuild replayed to f14244 and play went on |
| 14659 | day 17 morning (`stime` 0) |
| 14859..15135 | pause → entry 4 (Save, `pm_type` 3) → slot 020 → "Overwriting file. Are you sure?" → Yes: TIME 2:35:03 |
| 15177 | shop; `rd endsave` → `save:671c5819784b` |

## Game facts (retail, this run)

- Golem L20 on 20F: HP 750 = 150 × 5.0 (the wiki's "Lv 32 / 1080" is not this floor).
- Golem hits on Louie (DEF 58): `ftol(30·3.0/2 − 58/4)` = 30, + 0..5: right fist 32, 34, left fist 30, double 35, stomp 34. All
  knock down (`p_hurt` 70).
- Damage dealt: sword `ftol(56/2 − 30/4)` = 20 (24 counter); spin 40 (72 counter + back).
- No contact damage: the player stood 0.24..3.6 from the body centre many times with no hit.
- Type 36 (add): never moved (slot 3 at (−44.61, −31.75) from spawn to death). Shoots record 31 (side 1, `+0x10/+0x14` owner =
  the add), 0.15 / tick along its facing, reach −0.35; volleys of 4, 1.2 apart (IDA `sub_42CFD0`: ticks 2, 10, 18, 26 of a
  60-tick cycle while `+0xa5c == 1`). Damage `ftol(16·3.0/2 − 32/4)` = 16. Louie's guard (records 0x1f, ±45°, state 0) not tried.
- Boss death removes live adds (add #3 freed with the arms, no EXP popup checked).
- Return portal (−45, −40) answers from −z facing +z (as on 15F).

## Tool gaps

1. Session crash (wine `c0000005`) at f14244 right after the results page → night; the rebuild replayed and went on; `verify`
   MATCH. Not reproduced.
2. `rd eval` cannot take tuples / list comprehensions; a hit-record table view (`rd ram R records`, like `actors`) would have
   replaced a scratch script (`rec.py`: 0x69324b0 + i·0x124 → type, side, owner, pos, age, reach, power).
3. `--watch` is not a `search` option; the jump-home frames needed a side run.
4. `--apply-best` alone does not apply a found path (needs `--apply` too) — as hire-caillou reported.
5. A damage search's goal frame left a moai shot in flight that hit 22 ticks later during the spin's last ticks; the search from that
   frame had no escape (one cell). Goal tails / "no enemy record within R" in the goal would prevent it.
