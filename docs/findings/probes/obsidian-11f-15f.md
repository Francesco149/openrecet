# Probe: obsidian-11f-15f

- **Run:** `ob-15f` · **start:** `save:549015ff6b95` (end save of `ob-10f`: shop, day 16 morning) · no task file
  (goal: Obsidian Tower 11F → 15F, the boss-ahriman pair's arms, back to town, Save)
- **Drivers:** two subscription subagents. The first drove f0..11523 (shop → guild → 11F..14F → 15F fight to the first
  Beef Bowl) and hit a usage limit; the second resumed at the head (f11523, checkpoint `ate1`) and finished. The first one's
  floors were reconstructed by a side run (`ob15-scan-1`, `--from ob-15f@1737`, the movie replayed in segments with
  `--watch`; end state rng `e83303d5` = the run's own at f11523; stopped).
- **Movie:** 15011 frames. **Not filed.**
- **Verdict (2026-11-11):** `rd verify ob-15f` (headless):
  ```
  fresh headless replay: frame 15011 rng 4b0b0c3a sem 32b0944c86aed823 dev 5782712bdb23788f  (unskipped retail)
  live session:          frame 15011 rng 4b0b0c3a sem 32b0944c86aed823 dev 5782712bdb23788f  (skips 414ee2: dev not compared)
  MATCH
  ```
- **End save:** `save:2b79eafc5fb0` (slot 020 overwritten from the town map's pause menu; day 16 `stime` 2, TIME 2:30:54, SCORE 5287).
- **Card:** `docs/findings/bosses/boss-ahriman.md` §7 (15F pair; arms marked, stats corrected).
- Louie L40: HP 197, SP 45, ATK 56, DEF 58, MATK 21, MDEF 32 (status page f11756). Bag: Candy, Chocolate Bar, Beef Bowl
  (the first driver's packing; 3/20 at f11819) → 2/20 on the results page.

## Per floor (reconstructed from the side run)

Frames are the first driver's checkpoints (`f11` … `f15` = control on the floor) and the side run's 150-frame rows.
Enemy lists = the actor table a few ticks after entry (`type × count (HP)`).

| floor (`floor`) | frames | grid | enemies at entry | HP in → out | notes |
|---|---|---|---|---|---|
| 11F (10) | 1737 → ≈4440 | 7 × 7 | 16: 1 (92), 2 (69), 8×5 (83), 18 (69), 29 (69), 35 (69), 45 (64), 46 (83), 52 (92), 53×3 (69) = L18 | 197 → 157 | hits taken ≈f1890 (−15), ≈f2330 (−15, ck `hit31`), ≈f2600 (−10); 3 kills by f2645 (cks `k46 k35 k18`; `k8` f4126), live count back up 13 → 15 at ≈f3690 |
| 12F (11) | 4557 → ≈7200 | 9 × 9 | 35: 1×3 (92), 2×3 (69), 8×3 (83), 18×6 (69), 35×3 (69), 45×3 (64), 46×5 (83), 52×4 (92), 53×5 (69) = L18 | 157 flat | nothing killed |
| 13F (12) | 7290 → ≈8900 | 9 × 9 | 47: 1×5 (96), 2×3 (72), 8×4 (86), 18×4 (72), 29 (72), 35×4 (72), 45×4 (67), 46×9 (86), 52×4 (96), 53×9 (72) = L19 | 157 flat | `ticker` 131 at f7262 (a banner; text not read) |
| 14F (13) | 8919 → 10448 (`f14exit`) | 9 × 9 | 41: 1×4, 2×4, 8×5, 18×3, 35×3, 45×6, 46×6, 52×7, 53×3 (L19) | 157 flat | nothing killed |
| 15F (14) | 10508 → 14598 (results) | boss floor | 54×2 (480, L19); summoned bats 6 / 7 / 8 (72 / 82 / 86) | 157 → 71 (+50 Beef Bowl at f12185, +55 one at f11285..11523 by the first driver) | below |

No enemy type new against 6F–10F's list except type 29 / 46 / 52 / 18 / 35 showing up again (1F–5F types).

## 15F timeline (frames)

`q` = actor 0 (spawned at x −45), `r` = actor 1 (x −35); names in `ob-15f/vars.json` (`qst qt qc qhold qn qal qlat …`,
`r…` the same for slot 1; `qal` / `qlat` = the player's along / lateral offset from the beam anchor, 2 ahead along the octant).

| frame | what |
|---|---|
| 10508 | on 15F: `gate` 3, event tick 0; both type 54 in state 8 at (−45, 15, −34) / (−35, 15, −34), HP 480 |
| 10508..10748 | descent: y 14.71 (t 12), 13.30 (t 32), then −1/16 per tick to 0.80 (t 232); y 0 from state 0 on |
| 10748 | state 0 (t 240), `lock` −1 |
| 10755..10845 | bats: live count 2 → 8 (3 per boss, one per 30 ticks each) |
| 10915 | sword on q in state 0, no label: 480 → 457 (23) — ground swing reaches it (y 0) |
| 11057 / 11104 | q / r enter state 1 (both clocks in lockstep, `qc == rc` throughout; the next cycle's holds read 6 / 42) |
| 11075 | sword on q in state 1 (counter): 457 → 430 (27) |
| ≈11200 | 157 → 108 (49): beam; r at `rt` 97 (not live), q at `qt` ≈137 (live), 17.6 from q's centre |
| 11285 | 108 → 60 (48): r's beam (`rt` 172 live, 2.4 from r; q `qt` 215 not live); then pause, Beef Bowl (first driver) |
| 11523..11563 | (resume) knock-down ends; the world held still 12 ticks after the pause closed |
| ≈11600 | spin on r in state 1: 480 → 425 (55 = counter 46·1.2) |
| ≈11680 | spin on r: 425 → 365 (60) |
| 11819 | pause → Items: Candy, Chocolate Bar, Beef Bowl |
| 11886 | q state 1 at c 306 (`qc` 706), octant 6 (−x), aimed at the player (`qal` 5.2, `qlat` 0) |
| 11890 / 11965 | bat shots (record 31, side 1): 115 → 102 (13), 102 → 88 (14) |
| 11995..12027 | q `qt` 100..132: recoil, q x −33.1 → −30.0 (+x, against the beam); player walked into the strip |
| **12030** | **q's beam hits: 88 → 42 (46)**, `qt` 135, player at `qal` 0.30, `qlat` −2.44 (3.35 from q's centre); `p_hurt` 30, `p_inv` 30. Record 137 slot 1: pos (−31.99, 2.00, −41.26), vel (−1, 0, 0), age 116, reach 2.5; r's record (slot 3) at age 80 = r `rt` 99 |
| 12034..12208 | pause → Items → Beef Bowl: 42 → 92 |
| 12214..12298 | knock-back −x; r's strip (octant 0, +z) left at `rlat` −10.9..−13 with `p_inv` ≤ 0, `rt` 161..179: no hit |
| 12300..12397 | spin on r (state 1, counter + side): 365 → 288 (77) |
| 12417..12501 | spins / swings: r 288 → 174, q 430 → 378 |
| 12534 | q state 1 (c 345, hold 45), octant 6; r (hold 60) at c 360 |
| ≈12620 | sword on q at `qt` ≈82 (counter + side): 378 → 346 (32) |
| ≈12920 | sword on q in state 0 (back): 346 → 310 (36) |
| 12933 | q state 1 again only 95 ticks after its last one ended (`qt` 0, c 340; hold 40); r `rt` 5 |
| ≈12980 | r 174 → 147 (27) |
| **13034** | **sword on r while r's beam is live** (`rt` 102), counter + side: 147 → 113 (34) |
| 13042 (undone branch) | 92 → 46 (46) with both beams live: `qal` 1.77 / `qlat` −3.26 and `ral` 1.10 / `rlat` −3.46 (source not separable) |
| **13049** | swing cancelled into a spin at `p_actt` 10 (`p_inv` 30): **r 113 → 35 (78) at `rt` 113, beam live** |
| 13074..13078 | `p_inv` 0..−4 at `qal` 2.10 / `qlat` −4.69 and `ral` 3.97..4.21 / `rlat` −4.66..−4.69, both beams live: no hit |
| 13152 | spin kills r (35 → 0) and hits q 310 → 255 (55) |
| 13157 | r death tick 1 (after 4 hit-stop frames): `p_inv` 21 → **60**, slow-mo 60; bats untouched (6) |
| 13290 | bat shot 92 → 71 (21; type 8) |
| 13320 | r freed (`+0x428` 0, death tick 120) |
| 13320..13784 | q killed by sword only (search, no damage), through one more beam cycle |
| 13784 | q 0, `+0x428` 2 |
| 13794 | q death tick 2: `p_inv` **1199** (last 54) |
| 13884..13894 | death tick ≈60: bats 5 → 0 |
| 13948 | `estage` 1, `gate` 0 |
| 14148 | walking at the door from the circle side stops at (−39.25, −39.36); from +z it stops at (−45.14, −38.46): A there does nothing |
| 14433..14454 | from −z, facing +z (`p_oct` 0) at (−44.92, −40.95), `A`: "Return to town now?" (`portal` 22) |
| 14465 / 14598 | `A` (Yes) / `scene` 9 results, bag 2/20 (`s32(0x438be98)` 2 meanwhile, `lock` 0) |
| 14656 | `B`: town map (`scene` 8), `stime` 1 → 2, `wms` 1121211 |
| 14720..14974 | `~` on the map opens the pause menu: entry 4 = Save (`pm_type` 3) → slot 020 (cursor there) → "Overwriting file. Are you sure?" → Yes |
| 15011 | town map; `rd endsave` → `save:2b79eafc5fb0` |

## Game facts (retail, this run)

- Type 54 stat record (`0x5c39e0`): +0x24 63, +0x28 1.0, **HP 100, EXP 30, AT 20, DF 20, MA 35, MD 20**, rf 0.9 / 1.5 / 2 / 2.
  L19 → HP 480. Bats, +0x2c..+0x40 (HP EXP AT DF MA MD): type 6 15 12 20 4 15 10, 7 17 14 20 7 17 15, 8 18 18 20 10 20 20,
  9 20 22 20 14 24 25.
- Beam damage = magic `ftol(35·2.9/2 − 32/4) = 42`, + 0..7: 46, 49, 48, 46 taken. Knock-down 30 (`p_hurt` 30).
- Bats shoot record 31 (side 1, magic list): 13 / 14 / 21 taken = `ftol(MA·2.9/2 − 8)` + rand: type 6 (13 + 0..1), type 8 (21) by value.
- A trip begun at `stime` 1 returns to the town map at `stime` 2 (no night). ESC works on the town map.
- 15F's door answers from the −z side facing +z; on 10F the probe reported the opposite side.

## Tool gaps

1. Reconstructing another driver's floors: `rd new --from RUN@F` gives only the end state; replaying its movie with `--watch`
   needed a converter (movie → `0xMM*N` tokens; `{"esc": F}` lines → `~0xMM` on that tick; without them the replay diverged at the
   first pause). A `rd replay R --from F --to F --watch …` (or `rd new --from RUN@F --watch`) would replace it.
2. `rd eval --at CK` fails after a session rebuild ("no savestate"); only `ck load` (cuts the movie) or a side run read it.
3. A 40-term sum of `((use == 1) and (type in (6, 7, 8, 9)))` evaluated to 0 (each term alone 1); the same as products
   `(s32(..)==1)*(abs(s32(..)-7.5)<2)` works.
4. Run-local `rx` / `rz` (the first driver's names for actor 1) shadow nothing visibly but break the global `aNrel` definitions,
   which use `rx`/`rz` for the sword anchor: in this run `a0rel` is wrong. Names should not shadow silently.
5. Goals that test "HP dropped" fire on a later frame than the hit (a drop in state 0 met `qst == 1` on its next state): needs
   an avoid on the drop in the wrong window, or an "event happened while P" goal form.
6. Same pitfall as before: the search applied on its goal frame (`rt` 102 hit) left the player inside both strips with the swing
   locked; rescued by cancelling into the spin (30 ticks of `p_inv`).
