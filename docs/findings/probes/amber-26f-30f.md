# Probe: amber-26f-30f

- **Run:** `am-30f` · **start:** `dungeon2-25f-crown-kill` (f13616, 25F cleared) · no task file (goal: 26F..30F, Tielle's arms)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`), `rd search -j 2`
- **Movie:** 39184 frames (25F exit → 26F..29F → 30F Tielle killed → door → the shop event → Save). Filed as `amber-26f-30f-tielle`; port: triage identical ticks 1..39484, save IDENTICAL (2026-11-11).
- **Verdict (2026-11-11):** `rd verify am-30f` (headless):
  ```
  fresh headless replay: frame 39184 rng f2be29db sem ca6d2498e8f71e0e dev 13caf73e20091932  (unskipped retail)
  live session:          frame 39184 rng f2be29db sem ca6d2498e8f71e0e dev 13caf73e20091932  (skips 414ee2: dev not compared)
  MATCH
  ```
- **End save:** `save:fc2e4d81f26d` (slot 021 overwritten in the shop after the Tielle event, day 23 `stime` 2, Tielle beaten:
  bank+0x2bd12 = 1). No Save / "continue?" prompt after 25F: the exit portal went straight to 26F.
- **Card:** `docs/findings/bosses/tielle.md` (arms marked, stats corrected).
- **Side run:** `am-scan` (stopped; re-plays segments of `am-30f`'s own movie from its checkpoints with `--watch`, to read state the
  searches had skipped over).

## Per floor

| floor (`floor`) | frames | banner | enemies at entry (type × count) | HP in → out | what it took |
|---|---|---|---|---|---|
| 25F (24) | 13616 → 13718 | — | 0 (both crown slimes dead) | 89 → 89 | `{dpad(ex − px, ez − pz)}` 67 ticks, `A` on "Go to the next floor?" |
| 26F (25) | 13718 → 16983 | "Enemy defense power doubled!" | 72: 3×5, 6×6, 7×4, 13×13, 15×5, 19×10, 20×7, 22×4, 37×5, 44×8, 45×5 | 89 → 89 | grid 9 × 11, start (2, 3) → exit (4, 9): 10 legs, 10 searches (18..26 s each), 3,116 frames |
| 27F (26) | 16983 → 19045 | "Attack power doubled for everyone!" | 41: 3×4, 6×3, 7×3, 13×6, 15×5, 19×3, 20×4, 22×3, 37×3, 44×5, 45×2 (45 at the exit) | 89 → 89 | 6 legs, 1,927 frames |
| 28F (27) | 19045 → 20815 | none | 72: 3×7, 6×4, 7×6, 13×15, 15×7, 19×4, 20×5, 22×6, 37×5, 44×6, 45×7 | 89 → 89 | 5 legs, 1,658 frames |
| 29F (28) | 20815 → 21737 | "Adventurer attack power doubled!" | 57: 3×7, 6×4, 7×7, 13×10, 15×4, 19×3, 20×3, 22×4, 37×4, 44×4, 45×7 | 89 → 89 | 2 legs, 787 frames |
| 30F (29) | 21737 → 34620 | — | Tielle (type 27, HP 400, L15); kobolds (16, HP 80) and block spawns (2 / 15, HP 60) | 89 → 39 (2 Walnut Bread +10 +11; 2 Candy SP 12 → 32) | below |

Not one HP lost on 26F..29F, no enemy killed there (all walked around).

## Navigation that worked (26F..29F)

- **The floor's maze is readable**: W / H `s32(0x5cc2e8)` / `s32(0x5cc2ec)`, open cells `s32(0x73dfd08 + 4*(y*W + x)) == 1`, start cell
  `s32(0x438b434)`, `s32(0x438b438)`, exit cell `s32(0x438b43c)`, `s32(0x438b440)` (`src/floor_layout.h`); cell (x, y) is centred on
  world (−40x, −40y). A BFS over the grid gives the cell route; every leg is 40 units, centre to centre.
- **Per leg** (waypoint WX, WZ): `rd search --goal "hypot(WX - px, WZ - pz) < 3 or portal > 0" --avoid "hp < HP_NOW or dlg != 0"
  --score "-hypot(WX - px, WZ - pz)" --cell "(int(px/2), int(pz/2))" --actions "{dpad(WX - px, WZ - pz)}*12" + the 8 plain 12-tick walks,
  `--apply --apply-best -j 2`: found every time on the first try, 18..34 s, 220..510 frames per leg (the straight line is 230), with
  the avoid set to "no damage at all".
- **Failed first:** a hand-rolled controller (walk the leg, swing at anything within 7): 89 → 48 HP in 1,350 frames on 26F (defence
  doubled: 13..16 per hit, three enemies at once). Discarded (`ck load`).

## 30F timeline (frames)

| frame | what |
|---|---|
| 21737 | on 30F, `tst` 0; 21767 dialogue 1 (tick 31) → 22427; 22517 dialogue 2 (tick 120) → 23822 |
| 23852 | stage 3, 31 blocks; 23902 stage 4; 23963 stage 5 (phase 0), Tielle at (−40, −25.91) |
| 24043 / 24085 | block 26: HP 2 → 1 (a crit swing) → 0 |
| 24133 | a 132 lands: hp 89 → 76 |
| 24211 | south clamp: pz −57.029 |
| 24894 | phase 0 hit (2.43 from her; blocks 26, 18, 12 gone; 2 kobolds perched) → stage 6 |
| 25182 | stage 3, phase 1, 32 blocks; 25232 `tblk` 2; 25293 stage 5 |
| 25586 | block 31 destroyed → a pumpkin (type 15, HP 60) |
| 26683 | her block (2) destroyed → stage 6 (6 blocks gone in phase 1; hp 62) |
| 26940 | stage 3, phase 2, 31 blocks; 27000 `tblk` 2; 27051 stage 5 |
| 27055..27703 | pause menu: 2 Walnut Bread (+10, +11 HP), 2 Candy (+10 SP each): hp 83, sp 32 |
| 27756 | 4 × record 104 seen; 27949 / 28190 / 28431 / 28680 / 28959 kobolds perch |
| 28516 | block 29 destroyed → a blue slime (type 2, HP 60); 28525 its kobold becomes a walker; 28531 a 104 hits: 83 → 70 |
| 28613 / 28768 | the walker: back hit 41, then one crit swing kills it (39) and the slime (60) |
| 29068..31013 | 8 more blocks (a 160 s search for a chest roll, not found; `--apply-best` kept); pumpkin at 30425; kobolds fall at 30269, 30826 |
| 31130 / 31135 | her block → a pumpkin, stage 6; 31279 stage 7: the fight (hp 61, sp 32) |
| 31314, 31823 | record 78 (1 arrow); 31528, 31636 3 × record 79; 31420 arrow hit 59 → 45 |
| 31391..31923 | damage on her: 66 (spin, back) 23 49 55 24 65 58 22 38 → dead at 31923 (6 spins, 3 sword hits; hp 39, sp 2) |
| 32267 | stage 9; 32297 dialogue → 34040; 34070 stage 10 |
| 34470 | "Return to town now?" at the door's −z side; 34620 bag list (`scene` 9); 34701 shop; event → 38708; Save 39019..39080 |

## Game facts (retail, this run)

- Tielle's stat record: HP 100, EXP 25, AT 26, DF 30, MA 10, MD 30 → HP 400 at level 15; her records hit 13 (+0..2) at DEF 75; a sword
  hit on her is 19 at ATK 54. Kobold: HP 20, EXP 9, AT 17, DF 4.
- Block spawns: 4 of 20 destroyed blocks (type 15 × 3, type 2 × 1), her own block included.
- The player stops 2.93 from a block's centre; a swing from there damages it (hit at swing tick 8..9).
- The kobold counter (`B+0xa68`) fires on its 241st tick and is held at 0 while phase + 2 kobolds are perched.
- Record 104 waits ≈ 39 ticks at y 20 near Tielle, then falls 2 / tick; a standing player is not hit.
- The floor's exit is a door (object kind 6) that only answers from its −z side.
- After "Yes": a bag list page (`scene` 9, 11/20, no prompt text; `B` leaves), then the shop and a long event.
- Pause-menu food: the cursor keeps its row index after a use (the list shrinks under it); Walnut Bread gave +10 and +11.

## Predicates per arm

`B = 0x76b970` (Tielle is actor 0). `tst = s32(0x438b8a0)`, `tph = s32(0x438b8a8)`, `tblk = s32(B+0xa64)`, `tsub = s32(B+0xae4)`,
`tt = s32(B+0xa58)`, `tkc = s32(B+0xa68)`, `nblk = s32(0x438c150)`.

| arm | predicate |
|---|---|
| dialogues | `dlg == 1 and tst == 0` (twice); `dlg == 1 and tst == 9` |
| blocks drop | `tst == 3 and nblk == 31` (phases 0, 2) / `== 32` (phase 1) |
| volleys | a table-B record (`0x69324b0 + i*0x124`) of type 132 / 104 with side 1 |
| phase hit | `tst == 6 and tph == P` |
| hides | `tst == 4 and tph >= 1 and tblk >= 0 and s32(B+0x3dc) == 0` |
| block destroyed | `s32(0x438c154 + 4*i) == 0` (sum over the phase's blocks = blocks left) |
| block spawn | an actor slot with `+0x424` in (2, 15) and `+0x428 == 1` |
| chest roll | table C (`0x6956cb0 + i*0x94`): `s32(+0x40) == 2 and s32(+0x28) > 3` |
| kobold perched / fallen | type 16, `+0x428 == 1`, `+0xae4 == 3` / `== 0` |
| fight | `tst == 7 and tsub == 1`; arrows: table-B type 78 / 79; state `a0st == 1` |
| death / end | `a0use == 2`; `tst == 8`, `9`, `10`; `u8(bank + 0x2bd12) == 1`; `portal > 0 and tst == 10` |

## Tool gaps

1. **No way to read state at an earlier frame of the run** (only at checkpoints): after a search applies 600 frames I cannot ask
   "which record types existed at f31528". Worked around by a second run that re-plays a slice of the first run's `movie.jsonl`
   as `{mask}*n` tokens under `--watch`. A `rd scan R --from CK --to F --watch ...` (or `rd eval --at FRAME`) would make it one command.
2. **`rd ram actors` has no y and no `+0xae4`**, and there is no record-table (`rd ram records`) or block / mover view; wrote three
   helper scripts.
3. **`floor`, the maze grid and the route are not named**: a `rd route R` (BFS over `0x73dfd08`, waypoints out) would turn a floor into
   one command; the per-leg search then never failed.
4. `--until "scene == 1"` after `B` in the pause menu is true one tick too early for the next `B` (it fired a spin in the dungeon;
   rewound). A named "pause fully closed" would help.
5. `rd ram dwords ADDR` takes hex only: a decimal address (zsh `$((...))`) reads another place without an error.
6. The Bash tool backgrounds a search after 180 s; the search's `--time-limit` plus refine has to stay under ~150 s.
7. `portal` only tells "a prompt is up"; on a boss floor the door's side matters and nothing names it (a picture was needed).
