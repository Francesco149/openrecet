# Probe: lapis-31f-40f

- **Run:** `lapis-40f` · **start:** `save:68c43a00466e` (end save of `lapis-30f-story`: town map, day dword 23 = HUD Day 24, `stime` 2) · no
  task file (goal: Lapis Ruins 31F → 40F in one trip, the 35F pumpkin-ghost trio, the 40F red jellyfish, back, Save)
- **Driver:** two subscription subagents (`tools/drive/DRIVER.md`; the first ran out of usage at f32527, the second finished the return /
  day end / Save); legs `rd search -j 2 --hold 30`, fights `--hold 30`.
- **Host:** sessions on `bunker` (the Windows box answered only as `win-wifi`); headless verify on `bunker`, `--real` on `win-wifi`.
- **Movie:** 33,528 frames (town map → Adventurers' Guild → one trip 31F → 40F → return door → day end → Day 25 morning → Save).
  Filed as `tests/scenarios/lapis-31f-40f/trace.jsonl`; retail golden made (`rhost.py golden`, Windows, 128,714 draws). Not triaged.
- **Verdict (2026-11-13):** `rd verify lapis-40f` (headless, bunker) and `--real`:
  ```
  fresh headless replay: frame 33528 rng f18c68eb sem 54cb55c68836f342 dev 988007ff85b57b3f  (unskipped retail)  [bunker]
  live session:          frame 33528 rng f18c68eb sem 54cb55c68836f342 dev 988007ff85b57b3f  (skips 414ee2: dev not compared)  [bunker]
  MATCH
  real-device replay:    frame 33528 rng f18c68eb scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:1a6b7c4af68d` (slot 020 overwritten from the shop's pause menu at f33528: day dword 24 = HUD Day 25, `stime` 0,
  SCORE 6270 → 6648, TIME 3:42:35 → 3:51:50).
- **Cards:** `docs/findings/bosses/pumpkin-ghost.md` § "Lapis 35F trio", `docs/findings/bosses/jellyfish.md` § "Lapis 40F (type 71)".
- **No damage taken on the whole trip:** `hp` 210 / 210 from f121 to the end (no food for HP needed).

## Before the dungeon

| frame | what |
|---|---|
| 137 | town map (`scene` 8), gold 299,840 |
| 212 | Adventurers' Guild (`scene` 11): Louie L43 hired, pack (checkpoint `pack0` f401) |
| 1757 | **31F**, `stime` 2 → 3 (departed in the last period), gold 299,840 → 295,590 (fee 4,250); HP 210 / 210, SP 47 |

## Per floor

Level `floor/5 + 25`: L31 (31F–35F), L32 (36F–40F). Enemy lists = types in the actor table ≈60 ticks after arrival (`type × count (HP)`).
Grid = W × H, start cell → exit cell (`0x438b434..440`). Frames from `rd scan` (`floor` / `loading` edges).

| floor (`floor`) | frames | banner | enemies | grid, start → exit |
|---|---|---|---|---|
| 31F (30) | 1757 → 3234 | none | 6×3 (108), 8×3 (129), 10×5 (36), 11×2, 12×2 (36), 13×4 (72), 15×5, 16×5 (144), 17, 18, 19×3, 22×3, 35×2, 37, 38×2, 44 (86), 45×2 (100), 50×3, 51×5, 52×2 (144); 55 | 9 × 9, (2, 5) → (4, 6). Spins (SP 47 → 22, f2394..2807) killed 3 × type 10 + a type 37 (f2817 / 2823) |
| 32F (31) | 3234 → 4907 | "Movement speed increased for everyone!" | 6, 8, 10×2, 11, 12×2, 13×6, 15×4, 16×4, 17×4, 18, 19×3, 22×3, 35×3, 37×3, 38, 44, 45, 50, 51, 52; 44 | 9 × 9, (5, 5) → (7, 2) |
| 33F (32) | 4907 → 8283 | "Defense power doubled for everyone!" | 6×2, 8×2, 10×2, 11×2, 12×4, 13×7, 15×3, 16×6, 17×3, 18×2, 19×2, 22×2, **29**, 35×2, 37×3, 38×2, 44×4, 45×4, 50×5, 51×4, 52×2; 64 | 9 × 11, (1, 4) → (1, 2). A type 12 killed f6811 |
| 34F (33) | 8283 → 9809 | "SP consumption halved!" | 6, 8, 10, 11×2, 12×3, 13×4, 15×7, 16×7, 17, 18, 19×3, 22, 35, 37, 38×3, 44, 45, 50, 51×2, 52×4; 46 | 9 × 9, (1, 4) → (2, 7) |
| **35F (34)** | 9809 → 21494 | none | **55 ×3 (1080 each, L31)** + adds (cap 5) | boss floor — `pumpkin-ghost.md` § Lapis 35F trio; clear (`estage` 1) f21256 |
| 36F (35) | 21494 → 23476 | "Consumables now half as effective!" | 6×2 (111), 8×4 (133), 10×4 (37), 11×2, 12×2, 13×4 (74), 15×4, 16×9 (148), 17×3, 18×3, 19×2, 22×2, 35×2, 37×2, 38×2, 44×4 (88), 45×3 (103), 50×4, 51×2, 52×2; 62 | 9 × 11, (1, 5) → (4, 7) |
| 37F (36) | 23476 → 25745 | "Will-o'-Wisps will appear soon. Enjoy!" — `ticker` 1..73 at arrival; **no type 34 appeared** | 6×3, 8×3, 10×2, 11×3, 12×2, 13×10, 15×4, 16×5, 17×2, 18×2, 19×4, 22×2, 35×2, 37×2, 38×4, 44×4, 45×3, 50×2, 51×3, 52×2; 64 | 11 × 11, (7, 4) → (3, 4) |
| 38F (37) | 25745 → 28416 | "Consumables now half as effective!" (`ticker` 1..73) | 6, 8, 10, 11, 12, 13×4, 15×2, 16×3, 17, 18×3, 19×2, 22, 35, 37×3, 38, 44×3, 45×2, 50, 51×2, 52; 35 | 9 × 9, (3, 1) → (2, 6) |
| 39F (38) | 28416 → 29292 | "Will-o'-Wisps will appear soon. Enjoy!" (`ticker` 1..73); again no type 34 | 6, 8×2, 10, 11×6, 12, 13×2, 15×3, 16×7, 17, 18×2, 19, 22×3, 35, 37, 38×2, 44×2, 45×2, 50×3, 51, 52×2; 44 | 9 × 9, (4, 5) → (2, 5) |
| **40F (39)** | 29292 → 32597 (results) | none | **71 red jellyfish ×1 (740, L32)** + roper adds (17) | boss floor — `jellyfish.md` § Lapis 40F; clear (`estage` 1) f31910 |

Floors were walked around the enemies: only 6 kills on 31F..39F (5 on 31F, 1 on 33F). On the 35F boss grid the start / exit cell words
keep 34F's values (3 × 3 floor, stale `0x438b434..440`).

## New enemy types (not killed / not listed in any earlier `docs/findings/probes/*.md`)

Stats at `0x5c23f0 + type*0x68` (+0x2c HP, EXP, AT, DF, MA, MD; rf[0..3] at +0x44; +0x20 flags):

| type | HP | EXP | AT | DF | MA | MD | rf | flags | kill |
|---|---|---|---|---|---|---|---|---|---|
| **12** | 5 | 20 | 21 | 4 | 0 | 0 | 0.6, 1.5, 4, 4 | 0 | **33F f6811** (first probe with this type in the table: 31F..39F, 36 / 37 HP) |
| **71 red jellyfish** | **100** | 25 | 30 | **35** | 20 | **35** | 0.8, 4, 3, 4.5 (then 0.45, 1, 0.6) | **1** (+0x24 13) | **40F, f31751** (card) |
| 29 | 15 | 30 | 20 | 20 | 0 | 10 | 0.6, 1.5, 2, 2 | 0 | not killed (33F ×1; listed before only in `obsidian-3f-5f`) |
| 51 | 15 | 20 | 22 | 18 | 0 | 20 | 0.9, 1.5, 2, 2 | 0 | not killed (every floor; before only `obsidian-41f-50f`) |

Killed before: 10, 37 (here: 31F), 55 (35F trio, card). The jellyfish card's "type 71's record not read / HP if also 100" is now read: 100.

## 35F — three pumpkin ghosts (summary; card § Lapis 35F trio)

3 × type 55, 1,080 HP each (150·7.2, L31), adds up to 5 (8 live actors). f9810 → f21256 (11,446 frames): ghost 2 HP 0 f18543
(freed f18711), ghost 1 f20411 (freed f20579), ghost 0 last f21092 → adds swept (5 dying f21196) → freed + **`estage` 0 → 1 f21256**.
Two pause-menu food breaks (Taiyaki f11299: SP 2 → 33; f19356: SP 3 → 45); SP spins (`B`, 5 SP) hit two ghosts at once (≈55 each);
sword ≈25–35 otherwise. **HP 210 throughout.**

## 40F — the red jellyfish (summary; card § Lapis 40F)

Type 71 at slot 0, 740 HP (100·7.4, L32), state 8 at f29293 → 0 at f29532. Fight f29782 (`jfight0`, after a pause-menu SP food —
Candy Apple by the bag list — SP 0 → 43) → HP 0 **f31751** → freed + **`estage` 1, `gate` 3 → 0 at f31910** (same frame). Phase `+0xae4` **starts at 1** (red variant),
2 at f30141 (HP 489 < 493.3), 3 at f30770 (237 < 246.7). Roper waves of **2 (phase 1) and 4 (phase 3)** at once. **No damage taken.**

## The return and the day end

| frame | what |
|---|---|
| 31910 | `estage` 1 (generic boss floor: `FUN_00450877` → `FUN_0044b2e6(0/1)` every tick while `estage` 1 — it stays 1, never 2 here), `gate` 0 |
| 32040..32454 | walk to the return door; blocked at (−44.21, −40.30) walking −x (`p_oct` 6) |
| 32455 → 32475 | `portal` 2 → 22 (the prompt rising, `A` pressed at f32466 during the rise); **f32476 `portal` 0** = answered Yes |
| 32476..32596 | "Came back alive!" banner on the floor (picture `view/f032557.png`); the pad does nothing (`lock` −1 still) |
| 32597 | results bag (`scene` 9) "16/20": Rearrange, Iron Armor, Spiked Armband, Warrior's Helm, Apple ×… (first 7 rows read) |
| 32654 | `B` → **`stime` 3 → 4: the day ends** (departed at `stime` 3); stage 0 (shop) |
| 32822 | Daily Profits "Day 24": Found Platinum Halberd, Blue Teacup; Profits 0; Score Difference 6270 (+0) |
| 33137 | Day 25 morning (day dword 24, `stime` 0), shop at (−0.3, 9.35); "Day 25" title, then the news ticker ("The price of armor has increased.", "The price of Swords has normalized.", "Beware of adventurers without cards! - the Adventurer's Guild") |
| 33341 | `~` → pause menu at once (the ticker did not block ESC this time; cf. `lapis-1f-10f` f39094..40270) |
| 33395 / 33436 / 33467 / 33528 | `D` ×4 → `pm_sel` 4 (`pm_type` 3 Save) / `A` → slot list, cursor on 020 / `A` → "Overwriting file. Are you sure?" / `A` (Yes) → SCORE 6648, TIME 3:51:50; `rd endsave` → `save:1a6b7c4af68d` |

## Story / events

- **None:** `dlg` 0 from f1 to f33528 (no event on 35F / 40F, none at the return or the morning).

## Food

Taiyaki (+31 SP, f11299), one more SP food on 35F (+42 SP, f19356; bag 18 → 17), Candy Apple (by the bag list) before the 40F fight (+43 SP, f29536). No HP
food (never hurt).

## Tool gaps

1. `rd look` from a bunker session ran the real-device replay under wine on maximus: 493 s for one picture (the Windows box was reachable
   only as `win-wifi`; `rd look` did not pick it up without `RHOST_WIN=win-wifi`). `rd verify --real` likewise printed "--real needs the
   Windows box" until `RHOST_WIN=win-wifi` was exported.
2. `rd text` has no `--at-frame`: the floor banners above came from throwaway runs (`rd new tmp --from lapis-40f@F`), one per floor.
