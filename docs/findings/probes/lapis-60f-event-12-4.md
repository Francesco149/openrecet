# Probe: lapis-60f-event-12-4

- **Run:** `lapis-60e` · **task:** `tools/drive/tasks/lapis-60f-event-12-4.md` · **start (CRAFTED):** `save:b4766a99d74b` =
  `lapis-51f-60f`'s end save `78c391141547` (shop, day dword 27 = HUD Day 28, Louie L44, slot 020) with bank dword `0xaf3d`
  0x01010101 → 0x00010101 (byte `+0x2bcf7` cleared; `+0x2bcf4..f6` kept) by `tools/craft_pix_save.py`. Not a played save: the
  flag state "60F cleared but 12/4 unseen" does not exist in any recording.
- **Goal:** Lapis "56～60" → 56F..59F → kill the 60F rat (type 60) → **event 12/4** → return → day end → Save.
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`); floors = BFS + one scored search per cell (`--hold 30 --hold-no-goal`,
  `-j 2`, 60 s; every leg first try); rat = staged searches (purple meal → behind / near the rat in state 4 → spin loop).
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`, `RD_HOST=win`).
- **Movie:** 27,008 frames. Filed as `tests/scenarios/lapis-60f-event-12-4/trace.jsonl`. **No retail golden** (next:
  `rhost.py golden lapis-60f-event-12-4`, `rhost.py triage lapis-60f-event-12-4`). Not triaged.
- **Verdict (2026-11-14):** `rd check` PASS (steps at f12923 / f23601 / f25196); `rd verify lapis-60e` and `--real`:
  ```
  fresh headless replay: frame 27008 rng efdda696 sem 15c4cf37153212d5 dev aa4fd00b55b76cfb  (unskipped retail)  [win]
  live session:          frame 27008 rng efdda696 sem 15c4cf37153212d5 dev aa4fd00b55b76cfb  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 27008 rng efdda696 scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:24ab07c65610` (slot 020 overwritten from the shop's pause menu at f27008: day dword 28 = HUD Day 29, `stime` 0,
  278,020 pix, SCORE 7221 → 7531, TIME 4:11:17 → 4:18:44). Bank `+0x2bcf7` = 1 again.
- **Damage taken:** 215 → 206, all rat contact (−2 f20824, −2 f21759, −2 f22012, −3 f22877). No level-up (L44). Food: 2 Taiyaki on 60F
  (SP 8 → 39 → 48).

## Before the dungeon

| frame | what |
|---|---|
| 137 | shop, Day 28 morning; `+0x2bcf7` reads 0 |
| 377 | door by search → town map (`wms` 1121011), `R U` → `wm_cur` 6, `A` → guild f462 |
| 506..1696 | hire `A A`, 3rd `A` → slot 1's category list (opened on Spears). Packed **Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, Taiyaki ×4, Baked Yam, Apple ×11** (20/20; the Food list's row cursor clamps when a row empties: after the yam `U` landed on "Nothing") |
| 1817 / 1858 | `U` ×20 + `A` → plates; `A` → rows (remembered "51～55") |
| 1880 / 1886 | `D A` → **"56～60"** → **56F**, fee 4,350 (282,370 → 278,020), `stime` 0 → 1 |
| 2086 / 2116 | "Equip items in bag?" (`equipq` 1 at arrival + 200) → `A` → **ATK 62, DEF 82**, HP 215, SP 48 |

## Per floor

| floor | frames | banner | enemies (type × n (HP)); total | grid, start → exit, legs |
|---|---|---|---|---|
| 56F (55) | 1886 → 6000 | "SP consumption halved!" | 6×5, 8×2 (148), 10×4 (41), 11×4, 12×3, 13×6 (82), 15×7 (123), 16×6 (164), 17×2, 18×2, 19×4, 22×5, 35×2, 37×3, 38×2, 44×2 (98), 45×6 (115), 50×3, 51×3, 52×6 (164); 77 | 9 × 11, (1, 7) → (6, 2), 10 legs |
| 57F (56) | 6000 → 7886 | "Consumables now twice as effective!" | 6×4, 8×2, 10×2, 11×3, 12×2, 13×5, 15×4, 16×7, 17×3, 18×4, 19×4, 22×2, 35×4, 37×3, 38×2, 44×2, 45×2, 50×3, 51×5, 52×3; 66 | 11 × 9, (6, 5) → (8, 3), 4 legs |
| 58F (57) | 7886 → 10269 | "Will-o'-Wisps will appear soon. Enjoy!" | 6×4, 8×2, 10×4, 11, 12×3, 13×5, 15×5, 16×8, 17, 18, 19×2, 22×2, 35, 37×2, 38, 44, 45×2, 50, 51×2, 52×2; 50 | 9 × 9, (5, 3) → (2, 6), 6 legs |
| 59F (58) | 10269 → 12923 | "Movement speed increased for everyone!" | 6×2, 8×3, 10×4, 11×2, 12×6, 13×5, 15×6, 16×9, 17×2, 18×2, 19×2, 22×2, 35×2, 37×2, 38×4, 44×2, 45×3, 50×2, 51×2, 52×4; 66 | 11 × 9, (4, 1) → (3, 7), 7 legs |
| **60F (59)** | 12923 → 25451 (results) | none | **60 rat ×1 (1230)**; mushrooms 13 / 14 | boss floor, below |

Banners and grids differ from `lapis-51f-60f`'s 56F..59F (the floors are rolled per visit). No kills, no damage on 56F..59F.

## 60F — the rat (card `mushroom-king.md` § 9)

| frame | what |
|---|---|
| 12923 | on 60F: type 60 HP 1230, state 8; `gate` 3, `estage` 0, `+0x2bcf7` 0 |
| 13164 | state 0, `lock` −1 |
| 14265 → 14419 → 14559 | purple meal, behind at state-4 t ≤ 6: **spins 132, 128, 115** → 855 (+2 chip) |
| 16093 → 16318 → 16458 | purple meal, behind: **spins 112, 120, 132, 115** → 372 |
| 16462..16850 | pause: 2 Taiyaki (Items rows: Rearrange, the 3 own items, Taiyaki… — **4 × `D`** from Rearrange, not 3; SP 8 → 39 → 48) |
| 16850 → 20820 | **4 red mushrooms alive, no purple** (the cap of 4 blocks planting): purple-meal searches found nothing (3 × 100 s each, twice); rat HP 372 → 369 by chips only |
| 20820 → 21055 | search "kill reds until `npurp > 0`" (aimed swings at slots 1..6) → a purple planted |
| 21075 | a red eaten instead → state 5 (`k74` 2) — branch abandoned (`ck load`) |
| 22175 / 22307 | purple meal (search with the meal in the score) → state 4; the run reached it late |
| 22380..22431 | behind at state-4 t 81: **one spin 118** → 246 |
| 22744 → 22851 | state 4 t 0 at kd 3.1, **in front** (`a0rel` 0.2; walking behind was blocked by the body): **spins 75, 78, 84** → 3 |
| 22877 | contact −3 (209 → 206) |
| 22922 / **22937** | two sword chips outside state 4 (2, 1) → **rat dies** (`+0x428` 2) |

## Event 12/4

| frame | `estage` | `etick` | `gate` | `lock` | `+0x2bcf7` | `sid` | `dlg` | what |
|---|---|---|---|---|---|---|---|---|
| 22937 | 0 | — | 3 | −1 | 0 | 1001 | 0 | rat dies |
| **23481** | **1** | 0 | **0** | **0** | 0 | 1001 | 0 | death tick 500: slot freed, floor opens; next-floor circle (−35, −40) + return door (−45, −40) raised (`door` state 3 → 0). **`lock` 0** (the floor-59 arm with the flag clear holds control; the `lapis-51f-60f` run had `lock` −1 here) |
| **23601** | 1 | 120 | 0 | 0 | **1** | **12004** | **1** | `FUN_0044ba2c(12, 4, 0)` at etick 120; the flag set the same tick |
| 23601..25196 | 1 | 120 → 121 | 0 | 0 | 1 | 12004 | 1 | the dialogue (A taps, `--gap 12`); `etick` stops while `dlg` 1 |
| 25196 | 1 | 121 | 0 | 0 | 1 | 12004 | 0 | dialogue over |
| **25197** | 1 | 122 | 0 | **−1** | 1 | 12004 | 0 | control (flag now set → the arm releases `lock`) |

Party not moved: Louie stays at (−52.64, −38.70) through the scene (pictures `view/f024300.png`, `f024750.png`, `f024950.png`,
`f025196.png`: portraits over the 60F room; circle + door visible after). `estage` stays 1 (no stage-2 transition), portals already up.

**Full text** (first-tick frame of each line; speaker by portrait / name plate: x 204 = Alouette, 160 = Recette, 88 / 276 = Prime,
last line Tear):

| frame | text |
|---|---|
| 23770 | *Gasp!* R-Recette?! |
| 23835 | Alouette? What're you doing in a place like THIS? |
| 23900 | Me, I'm, uh... I'm dungeoneering, of course! |
| 23939 | Can't you tell by my confident swagger? |
| 24004 | Oh, uh, okay... well, be careful. |
| 24069 | I don't need you telling me that. |
| 24108 | You're a million light-years too early to worry about me! |
| 24173 | Hehe! I guess I am. |
| 24316 | ... Recette. Recette, hold on. |
| 24381 | Huh? What's up? |
| 24433 | ... I... |
| 24485 | I just wanted to wish you luck! |
| 24524 | An underskilled plebeian like you will need it, after all! |
| 24563 | Now then, pardonne moi! |
| 24706 | WHAT in the?! |
| 24745 | Now just a sec, "mistress"! |
| 24862 | Wasn't talking to Recette the whole point in coming out here? |
| 24901 | Alouette! Wait up! |
| 24979 | Huh. |
| 25031 | Did Alouette... want to tell me something? |
| 25096 | I do wonder. |

1,595 frames with A every 12 ticks; "ESC Key: Event Skip" shown (not used).

## The return and the day end

| frame | what |
|---|---|
| 25254 / 25259 | walk to (−47.8, −40); `L A` → sword on the door: **`door` 0 → 1 at f25269** |
| 25299 | `L` → `portal` 2 at (−45.88, −39.86) |
| ≈25320 / 25330 | `A` → "Return to town now?" → `A` (Yes) → `portal` 0 |
| 25451 | results bag (`scene` 9) "18/20"; `B` ignored until f25639 (tap) |
| 25639 | town map, `stime` 1 → 2 |
| 25727..25858 | `D` → 3 → Merchants' Guild → `B` → `D` → 0 → home: `stime` 2 → 3 |
| 25963..26182 | shop door (`R A` taps from (3.0, −3.24)) → map → `R` 3 → Merchants' Guild → `D` → 0 → home: `stime` 3 → 4 |
| 26182..26650 | day end, Daily Profits (−4,350; score 7221 → 7531) → **Day 29** (day dword 28), shop f26650 |
| 26801 / 26875 / 26916 / 27008 | pause (ESC ignored ≈150 ticks) / `D` ×4 → `pm_sel` 4 / `A` → slots (cursor on 020) / `A` "Overwriting file. Are you sure?" → `A`; `rd endsave` → `save:24ab07c65610` |

## Tool gaps

1. **The task's last predicate `scene == 1 and dlg == 0` is already true on 60F** (`rd check` matched it at f25196, in the dungeon):
   a "back in the shop" step needs `stage < 4` (or `stime == 0 and day == 28`).
2. Speaker names (sprite name plates) are invisible to `rd text`; a speaker field (or the portrait id) would make transcripts complete.
3. `rd search --apply-best` with a `frame/50` score can apply a path ending in a committed rat swing (f18350: 3 × 100 s "one cell");
   `--hold` on the meal search fixed it. Contact damage (−2 / −3) at kd ≈ 3 makes every branch fail an `hp < H` avoid in one tick.
4. A purple meal needs a purple mushroom: with 4 reds alive the rat plants nothing; no named var for "plant possible" — the kill-reds
   step was found by looking at `nred` / `npurp`.
5. `rd play --changes` does not exist (only `scan` has it).
