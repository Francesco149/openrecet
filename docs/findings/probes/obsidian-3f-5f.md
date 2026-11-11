# Probe: obsidian-3f-5f

- **Run:** `ob-5f` · **start:** scenario `obsidian-1f` (f9659, Obsidian Tower 3F, Louie L40, hp 110 / 197, sp 35 / 45) · no task file
  (goal: 3F → 5F, the pumpkin ghost's arms)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`), `rd search -j 2`
- **Movie:** 18333 frames (3F → 4F → 5F boss killed → door → town map → shop → Save). **Not filed.**
- **Verdict (2026-11-11):** `rd verify ob-5f` (headless):
  ```
  fresh headless replay: frame 18333 rng 1bc0b022 sem c2de5ad9ed6f84ab dev 4c5e4978e86608db  (unskipped retail)
  live session:          frame 18333 rng 1bc0b022 sem c2de5ad9ed6f84ab dev 4c5e4978e86608db  (skips 414ee2: dev not compared)
  MATCH
  ```
- **End save:** `save:de58486d3cb9` (slot 020 overwritten in the shop, day 15 `stime` 2, after the return from 5F; TIME 2:17:46 → 2:22:46).
  No Save prompt inside the dungeon.
- **Card:** `docs/findings/bosses/pumpkin-ghost.md` (arms marked, stats corrected).
- **Side run:** `ob-scan` (stopped; `--from ob-5f@F`, reads only).
- **Dialogues:** none played (floors, boss floor, return, shop).

## Per floor

| floor (`floor`) | frames | banner | enemies at entry (type × count) | HP in → out | what it took |
|---|---|---|---|---|---|
| 3F (2) | 9659 → 10989 | "Attack power doubled for everyone!" (ticker until f9739: no movement before) | 19: 0×2, 1×1, 2×3, 7×2, 17×2, 44×4, 45×3, 53×2 | 110 → 110 | grid 7 × 9, start (1, 1) → exit (4, 2): 4 legs, 4 searches (25..34 s), 1,198 frames; `A` on "Go to the next floor?" |
| 4F (3) | 10989 → 12410 | "Enemy attack power doubled!" (until f11071) | 18: 0×4, 1×2, 2×2, 7×2, 17×1, **29×1** (HP 63; not on 1F–3F), 44×1, 45×4, 53×1 | 110 → 110 | start (1, 4) → exit (3, 2): 4 legs (36..45 s), 1,288 frames |
| 5F (4) | 12410 → 17684 | none | pumpkin ghost (type 55, HP 660, L17); adds 15 / 52 / 53 (HP 66 / 88 / 66) | 110 → 35 (three hits taken on purpose; bag empty: no food) | below |

Nothing killed and no HP lost on 3F / 4F.

## Navigation (3F, 4F)

- **The grid facts hold in dungeon 3**: W / H `s32(0x5cc2e8)` / `s32(0x5cc2ec)` = 7 / 9, open cells `s32(0x73dfd08 + 4*(y*W + x)) == 1`,
  start / exit cells `0x438b434..440`, cell (x, y) = world (−40x, −40y): the start cell matched `px`, `pz` on both floors and the portal
  prompt came up at the exit cell's centre both times.
- Per leg: the amber-26f-30f recipe unchanged (score `-dist`, cell `(int(px/2), int(pz/2))`, aimed 12-tick walk + 8 plain, avoid
  `hp < 110 or dlg != 0`, `-j 2`): 8 legs, 8 finds, 256..393 frames each. The goal radius 3 stops short of the portal: a
  `{dpad(WX - px, WZ - pz)}*40 --until "portal > 0"` finishes it (12..13 ticks).
- **The floor banner freezes play** (`ticker != 0`, `lock` −1): the first search on 3F found one cell. `.*40 A` then `--until "ticker == 0"`.

## 5F timeline (frames)

| frame | what |
|---|---|
| 12410 | on 5F: `gate` 3, boss state 8 at (−40, −34), HP 660, `lock` 0 |
| 12650 / 12670 | state 0 / `lock` −1, clock running, hold 32 |
| 12703 / 12766 / 12863 | cycle 1: fade flag / untouchable (alpha 0) / back (alpha 4) |
| 12983 / 13003 / 13043 | cast 1 / 3 adds (slots 1..3: 53, 15, 52) / state 0 |
| 13052 | hold 32 → 38 |
| 13170 | swing 1 from 4.08: the boss backs off 0.11 / tick, gap +0.42 at tick 9 (miss, also untouchable) |
| 13239..13251 | swing inside reach (gap −0.70 at tick 9), `+0xb08` 1: HP 660 flat |
| 13269 | fade-in with teleport: d 4.60 → 17.87 |
| ≈13408 | cast 2: 1 add (n 3 → 4) — inside a search, not watched |
| 13486 | sword hit, state 0, front: 660 → 634 |
| 13580 | a pumpkin's hit (record 10): hp 110 → 97 (a swing at f13565 hit that pumpkin 66 → 32; the boss untouchable again) |
| 13666 | ghost 52's hit (record 108): hp 97 → 78 |
| 13680 | fade-in with teleport (d 6.4 → 14.7) |
| 13800 / 13824 | cast 3 / record 90 (n 4 = cap) |
| 13939 | the shot hits: hp 78 → 35 |
| 14003..14085 | pause menu: Items = 0/20 (nothing to eat) |
| 14088..14318 | two adds killed by sword (no HP lost) |
| 14345 / ≈14375 | cast 4 (2 adds at t 20) / sword hit in state 1 (counter): 634 → 605 |
| 14401..14497 | two sword hits: 605 → 554 |
| 14613 | fade-in without teleport (d 12.59) |
| 14614..16611 | 554 → 28: sword + 6 spins (SP 30 → 0), adds killed to stay under the cap; n was 4 at the cast of ≈f15986 (a second record 90 expected; not watched, no HP lost) |
| 16843 | kill (state 1, t 1, clock 3909); 16947 death tick 60: 4 adds die; 17007 tick 120: `estage` 1, `gate` 0 |
| 17184 | circle: "Go to the next floor?" → No |
| 17540 / 17553 | door: "Return to town now?" → Yes |
| 17684 / 17774 | `scene` 9 results + bag page (0/20) / `A` leaves |
| 17791 / 17881 | town map / Home → shop with control (f17942) |
| 18058..18180 | Save, slot 020 |

## Game facts (retail, this run)

- Stat records: type 55 HP 150, EXP 30, AT 20, DF 20, **MA 35**, MD 10 (`+0x24` 0x40); 15: 15 / 20 / 20 / 4 / 0 / 0; 52: 20 / 25 / 0 / 16 / 20 / 10;
  53: 15 / 25 / 0 / 20 / 24 / 10. **MA is not 0 for these** (`boss-floor-common.md` says MA is 0 for every enemy): the shot did 43 / 45, a 108 did 19 / 21.
- Record 90 = a slow homing ball: born at the boss, still for ≥ 22 ticks, then closes on the player, hits ≈ 115..123 ticks after the cast tick.
- The boss has no contact damage and keeps ≈ 8 from the player, backing off at 0.11 / tick: press `A` with `gap < −1.5` (gap = `hypot(a0x − rx, a0z − rz) − 3.8`).
- Hit-stop (4 ticks) also holds the boss's clock.
- After the clear the pad does nothing for 120 ticks with `lock == −1`.
- Results page with an empty bag: `A` leaves, `B` does not (am-30f, 11/20: `B` left).
- Pause menu in the dungeon: entry 0 = the status page (type 0), entry 1 = Items.

## Predicates per arm

`B = 0x76b970` (actor 0). `bst = s32(B+0xa54)`, `bt = s32(B+0xa58)`, `bc = s32(B+0xa5c)`, `bhold = s32(B+0xa60)`, `bfade = s32(B+0xa64)`,
`balpha = s32(B+0x3dc)`, `bun = s32(B+0xb08)`.

| arm | predicate |
|---|---|
| descent | `bst == 8 and bun == 1` |
| hold roll | `bhold` changes when `bc % 400 == 2` (read after the tick) |
| fade / untouchable / back | `bfade == 1`; `bun == 1 and balpha == 0`; `bfade == 0 and balpha == 4` |
| swing while untouchable | `p_act == 2 and p_actt == 9 and gap < 0 and bun == 1`, `a0hp` flat |
| teleport / none | at `bfade` 1 → 0: `a0d` jumps / does not |
| summon | `bst == 1 and bt == 20`, live actors of type 15 / 52 / 53 rise (`s32(+0xb2c) == 1`) |
| shot | a table-B record (`0x69324b0 + i*0x124`) of type 90, side 1; its hit: hp −39..45 and the record freed |
| adds' hits | record types 10 (pumpkin), 108 (ghost 52), 107 (ghost 53), side 1 |
| counter | `a0hp` drops while `bst == 1` |
| death | `a0use == 2`; `s32(B+0xaf0)` 60: adds gone; 120: `estage == 1 and gate == 0` |
| end | `portal == 2` at the circle / the door; `scene == 9` → `8` |

## Tool gaps

1. **Named `--expr` variables are substituted without parentheses**: `0 - addhp - nadd*20` with `nadd = 0 + (..) + (..)` evaluated to
   23840. Wrap every multi-term define in `( )` (or the tool should).
2. `rd eval R "-addhp"`: an expression starting with `-` is parsed as an option.
3. **`--apply-best` can apply a doomed state** (best score one tick before an unavoidable hit; the next search then finds one cell).
   A "best cell that survives N more idle ticks" option would save the rewind.
4. No state at an earlier frame (same as am-30f gap 1): the counter hit's exact frame and cast 2 were inside searches. Worked around
   with `rd search --save` + `ck load` + `rd play "$(cat FILE)" --watch` and a side run `--from ob-5f@F`.
5. `rd eval --at CK` stops working once the session was rebuilt (after `rd stop` / `rd verify`).
6. `rd ram actors` has no y / `+0xae4` / `+0xb2c`; `aN*` names stop at slot 5 (adds reached slot 10); no record-table view
   (the 90 / 107 / 108 records change slots).
7. `--watch` prints a row per sample; a "rows only when these change" mode (my `scan.py` wrapper) made the cycle readable.
8. `portal` says a prompt is up, not which side of a door answers (found by search: 15 frames).
