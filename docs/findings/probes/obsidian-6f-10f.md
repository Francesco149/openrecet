# Probe: obsidian-6f-10f

- **Run:** `ob-10f` · **start:** `save:de58486d3cb9` (shop, day 15 `stime` 2, crafted lineage) · no task file
  (goal: Obsidian Tower 6F → 10F, the jellyfish's arms, back to town, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`), `rd search -j 2`
- **Movie:** 14788 frames (shop → guild → 6F..9F → 10F boss killed → door → night → morning → Save). **Not filed.**
- **Verdict (2026-11-11):** `rd verify ob-10f` (headless):
  ```
  fresh headless replay: frame 14788 rng 652a6803 sem ad186d0106c9df52 dev bc63ac31ece21e73  (unskipped retail)
  live session:          frame 14788 rng 652a6803 sem ad186d0106c9df52 dev bc63ac31ece21e73  (skips 414ee2: dev not compared)
  MATCH
  ```
- **End save:** `save:549015ff6b95` (slot 020 overwritten, shop, day 16 `stime` 0 = the morning after; TIME 2:26:48, SCORE 5287).
- **Card:** `docs/findings/bosses/jellyfish.md` (arms marked, stats corrected).
- **Side run:** `ob-scan` (`--from ob-10f@10260`, the two unwatched searches replayed with a watch; stopped). **It replaced the
  earlier `ob-scan` of the obsidian-3f-5f probe (`--force`)** — that one was a reads-only side run.
- **Dialogues:** none played (shop load, guild, floors, boss floor, return, night, morning). No level-up event on load this time.

## Before the dungeon

- Load `.*60 A .*30 A .*120` → control f137 (no event). Door by search (f401 town map, `wms` 1121211: guild open at `stime` 2, so no sleep).
  Map `D D D R U` → `wm_cur` 6, `A` → guild f468.
- Hire Louie (`A`), pack screen. **Packed (5 slots): Beef Bowl ×3, Candy, Chocolate Bar** — all the Food the shop inventory had; nothing
  bought (a Market visit costs the period the guild needs). Pack UI: `A` on a slot opens the category list (`L` / `R`: Clothes, Robes,
  Breastplates, Armor, Bracelets, Helms, Shoes, Treasures, Ingredients, Food; the category is remembered), `D` + `A` picks, the cursor
  returns to the slot list. `B` on the slot list goes back to the hire list (the bag is kept).
- `U`×5 → Depart!, dungeon cursor already 3, floor-row cursor `u32(0x6a49b54)` (0 → 1 = "6~10"), `A` departs f1001: fee 3,950,
  `stime` 2 → 3. `A` on the equip prompt, control f1226. `stage` 11 on 6F.
- One Beef Bowl eaten on 10F (pause → entry 1 Items → `A` `A`): hp 137 → 190 (f12109). Bag 4/20 on the results page.

## Per floor

| floor (`floor`) | frames | banner | enemies at entry (type × count; HP) | HP in → out | what it took |
|---|---|---|---|---|---|
| 6F (5) | 1001 → 2822 | "Enemy defense power doubled!" (the ticker starts ≈ f1278, 50 ticks after `lock` −1; until f1364) | 28: 0×2, 1×3, 2×3, 7×3, 17×3, 44×4, 45×5, 53×5 (66 / 88 / 66 / 75 / 66 / 53 / 62 / 66 = L17) | 197 → 197 | grid 9 × 7, start (4, 1) → exit (6, 3): 4 legs, 4 searches (12..23 s), 1,458 frames |
| 7F (6) | 2869 → 4441 | "Will-o'-Wisps will appear soon. Enjoy!" (until f2946) — no new actor type by f4441 | 24: 0×3, 1×7, 2×2, 7×3, 17×2, 44×3, 45×2, 53×2 (L17) | 197 → 197 | 9 × 7, (2, 1) → (3, 5): 5 legs (11..26 s), 1,495 frames |
| 8F (7) | 4488 → 7468 | none | 26: 0×2, 1×5, 2×4, 7×3, 17×3, 44×3, 45×4, 53×2 (L17) | 197 → 197 | (3, 1) → (5, 3): 8 legs (12..19 s), 2,859 frames |
| 9F (8) | 7515 → 9179 | "EXP gain has doubled!" (until f7586) | 19: 0×2, 1×2, 2×2, 7×5, 17×2, 44×2, 45×2, 53×2 (69 / 92 / 69 / 78 / 69 / 55 / 64 / 69 = L18) | 197 → 197 | 7 × 9, (1, 4) → (3, 1): 5 legs (15..21 s), 1,593 frames |
| 10F (9) | 9226 → 13706 | none | Electrojelly (type 70, HP 460, L18); adds type 17 (HP 69) | 197 → 190 (hits taken on purpose: rain 23, slam 30, roper 7; one Beef Bowl +53) | below |

No new enemy type on 6F–9F (the types of 1F–4F minus 29). Nothing killed, no HP lost before 10F. 21 legs, 21 finds on the first try
(the recipe of obsidian-3f-5f; a first attempt on 6F found 4 cells because the banner's ticker had not started yet at `lock == -1`:
wait for `ticker != 0`, then `== 0`).

## 10F timeline (frames)

`B = 0x76b970`; `c = s32(B+0xa58) % 800`.

| frame | what |
|---|---|
| 9226 | on 10F: `gate` 3, boss state 8 at (−40, 16, −40), HP 460, `lock` 0 from the next tick |
| 9466 | state 0, `lock` −1 (t 240), y 5.95; idle joints (−31.4, 0.74, −37.9) / (−48.1, 0.74, −37.9) |
| 9518 | player under the torso (spin < 30): arms 0 and 1 both → 3 |
| 9536, 9572, 9608, 9644 | four swings at the body (U, L, D, R; 0.7..2.8 from its centre, gaps to both joints > 0): HP 460 flat |
| 9656 | c 190, phase 0: 1 roper (slot 1), killed by sword 27 + 27 + kill (f9780..9985) |
| 9709..10049 | under the torso: 4 double slams, HP 197 flat (joints 6.5..9 away) |
| 10028 / 10067 | c 550: record 70 at (−40.1, 0, −35.3) / it strikes at age 40: hp 197 → 174 (2.9 from it), `p_hurt` 30 |
| 10145..10185 | arm 0 slam taken on purpose: joint y 1.9 → 4.8 (tick 33) → hit at tick 43, hp 174 → 144, `p_hurt` 70 |
| 10378, 10434, 10484 | sword hits in the spin (spin 60): joint 2 (26), joint 1 with arm 0 idle (27), joint 2 (24): 460 → 383 |
| 10492 | c 190, still phase 0: 1 roper (slot 2) |
| 10532, 10628 | joint 1: 25; 33 with "Back Attack!" (picture f10640): → 325 |
| 10651 | the roper's contact (record 19) during the swing lock: hp 144 → 137 |
| 10786 / 10791 | joint 1 (gap −0.07, 5.07 from the joint): 325 → 298 / phase 0 → 1 |
| 10882 | one swing: joint 1 (23) + the roper killed |
| 11016 | c 690: the second rain of phase 1 (record at (−44.0, 0, −42.0)); avoided |
| 11041 | arm 1 slam starts alone (arm 0 at tick 67) |
| 11174 | joint 2 only (gap −0.43; joint 1 +7.8): 275 → 251 |
| 11286..11339 | spin 60, within 7.5 of an idle arm's joint: no slam starts |
| 11350 | c 190, phase 1: 2 ropers (slots 3, 4) |
| 11453 | spin (5 SP): joint 2 44 (no label) + roper 4 killed; 251 → 207 |
| 11518, 11642 | joint 2: 26, 26 (+ roper 3 killed): → 155 |
| 11730 | c 550 (phase 1): rain record, gone at c 609 |
| 11778 / 11783 | joint 1: 155 → 130 / phase 1 → 2; no rain at c 690 |
| 12015..12136 | pause menu: Beef Bowl, hp 137 → 190 |
| 12342 | c 190, phase 2: 3 ropers (slots 5..7); all three killed by one swing at f12590 (after earlier hits) |
| 12597 | joint 2 on swing tick 12: 130 → 106 |
| 12718..12872 | phase-2 cycle: one rain (c 550), none at c 643 / 644 / 690 / 736 / 737 |
| 12963, 13005 | two spins: 106 → 53 → 0; `+0x428` 2 at f13005 (c 33, clock 3233) |
| 13005..13164 | death: slow-mo 60, body falls to y 0, alpha fades from tick 100; freed at tick 115: `estage` 1, `gate` 0 |
| 13290 | (standing on the circle) "Go to the next floor?" → No (`R A`) |
| 13554 / 13574 | door at (−45, −40), answered from the +z side facing −z (`p_oct` 4): "Return to town now?" → Yes |
| 13706 / 13783 | `scene` 9 results + bag (4/20) / `B` leaves: `stime` 3 → 4, straight into the shop at night (2, −3.24), no town map |
| ≈13950 / 14218 / 14235 | Daily Profits "Day 16" (profits 0, score 5192 (+0)) / day 15 → 16 / morning with control, `stime` 0 |
| 14476..14768 | pause → entry 4 (Save, `pm_type` 3; in the shop: 0 status, 1 Items, 2 Encyclopedia, 3 Options, 4 Save) → slot 020 → "Overwriting file" Yes |

## Game facts (retail, this run)

- Stat records: type 70 HP 100, EXP 25, AT 30, DF 30, MA 20, MD 30, rf 1 / 4 / 3 / 4.5; type 17: 15 / 10 / 16 / 4 / 0 / 0.
- Rain = record 70: stationary, reach 3.0, 40 ticks of blue glow then the bolt; per cycle 1 / 2 / **1** in phase 0 / 1 / 2.
- Under the torso: no slam and no spin contact reaches; the rain does.
- The spin is not an attack. Idle joints are hittable from the ground.
- A trip started at `stime` 2 ends the day: results → shop at night → Daily Profits → next morning (no town map, no Home choice).
- Results page with a non-empty bag: `B` leaves.
- Hit-stop (4 frames) holds the boss clock; the phase is re-read on its next tick.

## Predicates per arm

`j1 = f32(B+0x708 / 70c / 710)`, `j2 = f32(B+0x714 / 718 / 71c)`, `spin = s32(B+0xa68)`, `ph = s32(B+0xae4)`, `arm0 / arm1 = s32(B+0xa88 / a8c)`,
`at0 / at1 = s32(B+0xa90 / a94)`, `d1 / d2 = hypot(px − j1x, pz − j1z)` …, `g1 / g2 = hypot(j − (rx, rz)) − 2.0 − 2.25`, `dt = s32(B+0xaf0)`.

| arm | predicate |
|---|---|
| descent | `a0st == 8`, `a0y` 16 → 5.95, `lock == 0` |
| slam start / tell / strike | `armN == 3` (needs `dN < 8 and spin < 30`); `jNy` rising to ≈ 4.8 at `atN` 33; hp drop with `p_hurt == 70` at `atN` 41..69 |
| no slam in the spin | `spin >= 30 and armN == 0 and dN < 7.5` holds for ≥ 20 ticks |
| body not a volume | `p_act == 2 and p_actt == 9 and hypot(a0x − px, a0z − pz) < 3 and g1 > 0 and g2 > 0`, `a0hp` flat |
| joint 1 / 2 hit | `a0hp` drops on `p_actt` 9..12 with `g1 < 0 < g2` / `g2 < 0 < g1` |
| adds | at `c == 190`: `ph + 1` new actors of type 17 (table-B records of type 19 appear on the same tick) |
| rain | a table-B record (`0x69324b0 + i*0x124`) of type 70, side 1, at `c == 550` (and 690 in phase 1); hp drop at its age 40 within 5 |
| phase | `ph` 0 → 1 → 2 at `a0hp` < 306.67 / < 153.33 |
| death | `a0use == 2`; `dt` 100: alpha falls; `a0use == 0 and estage == 1 and gate == 0` at `dt` 115 |
| end | `portal > 0` on the circle (−35, −40) / at the door (−45, −40) from +z; `scene == 9` → `1` (`stime` 4) |

## Tool gaps

1. **A session crash** on the first tick after the results page (f13783, `exception c0000005 at eip=7aef924a`, read 5001a000) during
   `rd play .*300 --watch`; the rebuild by replay passed the same frame and `rd verify` matches. Not reproduced.
2. **`--apply` on a goal frame keeps doomed states** (3 times: a slam / a roper hit inside the next 20 ticks, the next search finds
   1..4 cells). Worked around with a goal tail (`p_act == 0 and (armN != 3 or atN > 72 or atN < 8 or dN > 8.5)`). Same as gap 3 of
   obsidian-3f-5f; "the goal must still hold after N idle ticks" would fix both.
3. No state at earlier frames of an applied search: used `--save FILE` + `rd play "$(cat FILE)" --watch` behind a filter that prints
   rows only when chosen columns change (`scan.py`; a `--watch-changes COLS` would replace it). `--watch` with ~26 columns fails with
   `live: err undefined program`.
4. No record-table view (`rd ram R records`): wrote one over `rd ram read 0x69324b0 149504`. Record 14 (the slam) never showed in it.
5. `aN*` names stop at slot 5 (ropers reached slot 7); run-local `vars.json` is not inherited by `rd new --from RUN@F`.
6. `rd endsave R NAME` is in the task text; the command takes no name.
7. Sprite prompts / cursors with no name: the guild's floor-row cursor `u32(0x6a49b54)`.
