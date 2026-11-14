# Probe: crystal-21f-30f

- **Run:** `crystal-30f` · **start (CRAFTED lineage):** `save:2b9e8df3acad` = `crystal-1f-20f`'s end save (shop, day dword 33 = HUD
  Day 34 morning, `stime` 0, Louie L47, slot 020, 44,760 pix). Upstream: `7874cefc5cb5` ← … ← `b4766a99d74b` (`lapis-51f-60f`'s end
  save with bank `+0x2bcf7` cleared by `tools/craft_pix_save.py`), so this save descends from a crafted one. No task file.
- **Goal:** Crystal Nightmare row "Floor 21～30" in one trip on day dword 33 (guild open; the 500,000 payment on the night of dword 35
  stays out of reach), the 30F rest floor's layout and objects, return portal → town → day end → dword 34 morning → Save.
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`). Floors: the `crystal-1f-20f` nav script (BFS of `0x73dfd08` + one scored
  search per cell, `-j 2`, `--hold 45 --hold-no-goal`, `--avoid "hp < 228"`); exit goal `portal > 0 and pkind == 8`, tap `--until
  "floor != N or scene != 1"`. All 9 floors on the first pass (29F: one leg backtracked once).
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`, `RD_HOST=win`). Nothing ran under wine on maximus.
- **Movie:** 20,030 frames: Day 34 guild → one trip 21F → 30F → return door → town → Merchants' Guild ×2 (time burn) → day end →
  dword 34 morning → Save. Filed as `tests/scenarios/crystal-21f-30f/trace.jsonl`. **No retail golden** (next: `rhost.py golden
  crystal-21f-30f`, `rhost.py triage crystal-21f-30f`). Not triaged.
- **Verdict (2026-11-14):** `rd verify crystal-30f` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 20030 rng d8cf9072 sem 9c5ec4c0a07bb70f dev 09e5e7eab597ff3b  (unskipped retail)  [win]
  live session:          frame 20030 rng d8cf9072 sem 9c5ec4c0a07bb70f dev 09e5e7eab597ff3b  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 20030 rng d8cf9072 scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:1049877e95a4` (slot 020 overwritten from the shop's pause menu at f20030: day dword 34 = HUD Day 35, `stime` 0,
  40,110 pix, SCORE 9061 → 9227, TIME 5:13:15 → 5:18:45). The 500,000 payment (night of dword 35) is one day away and unpayable on
  this lineage: the next leg must not play past dword 35's evening.
- **Damage taken: none** (`rd scan` 2700..17860 `--where "hp < 228 or sp != 51"`: 0 rows). No level-up (Louie L47, HP 228, SP 51),
  no food eaten.

## Before the dungeon

| frame | what |
|---|---|
| 172 | shop, HUD Day 34 morning (`.*60 A .*30 A .*80`); control at once, no event |
| 461 | door by search → town map (`wms` 1121011); `U U R` → `wm_cur` 6 → guild (scene 11, f546) |
| 600 | roster as in `crystal-1f-20f` (Louie L47 fee 4,650; Charme, Caillou, Tielle … L40). Bag empty (last trip's bag went to storage) |
| 641..1970 | hire Louie. **Bag menu mechanics:** after a pick the cursor goes back to the SLOT column on the same slot; `D` = next slot, `A` opens the item list on the category AFTER the last pick's (equipment) or the same one (food, cursor kept). `A` on a filled slot replaces its item. Packed Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, Taiyaki ×7, Beef Bowl ×2, Watermelon ×2 (15/20) |
| 2153 / 2215 | Depart → plates → `D` → Crystal Nightmare rows: "Floor 1～10" **00:04:19**, "Floor 11～20" **00:05:30** (= 15591 / 19822 ticks ÷ 60), "Floor 21～30" `--:--:--`, "Boss Rush" `--:--:--`; cursor restored to row 0 (`bank+4·0xb380` = 0) |
| 2264 | `D D A` → **21F** (`floor` 20, start floor `0x438b4d8` 20), fee 4,650 (44,760 → 40,110), `stime` 0 → 1 |
| 2636 / 2647 | "Equip items in bag?" → `A`; control f2647 |

## Per floor

Level `ftol(floor·1.7 + 50)`: L84 (21F) … L97 (29F); HP = table HP·(1 + 0.2·L) — table HP 15 → 267 at 21F, 306 at 29F (live).
Enemy lists = the actor table at arrival + 60 (`type × count (HP)`). Banner = `rd text` at arrival + 60.

| floor (`floor`) | frames | banner | enemies (arrival) | grid, start → exit, legs |
|---|---|---|---|---|
| 21F (20) | 2264 → 4081 | Will-o'-Wisps will appear soon. | **list 3** (as 16F–20F, + 40): 8, 9 ×2 (356), 10, 11 ×2, 12 (89), 14 ×2, 15 ×3, 16 ×7, 17..20, 22 ×2, 36 ×2, 39, 40 ×2, 44 ×3 (214), 45 ×3 (249), 46 (320), 47 ×3 (623), 50 ×4, 51, 53; 46 | 11 × 7, (5, 3) → (3, 4), 3 |
| 22F (21) | 4081 → 6583 | Enemy attack power doubled! | list 3; 48 | 11 × 7, (5, 1) → (9, 3), 6 |
| 23F (22) | 6583 → 8840 | Enemy defense power doubled! | list 3; 59 | 9 × 11, (1, 5) → (6, 5), 5 |
| 24F (23) | 8840 → 10274 | Adventurers now nearly blind! | list 3; 39 | 7 × 11, (3, 4) → (4, 2), 3 |
| 25F (24) | 10274 → 11798 | Adventurers now nearly blind! | list 3; 41 | 11 × 7, (5, 3) → (7, 4), 3 |
| 26F (25) | 11798 → 13242 | Enemy movement speed increased! | **list 4:** 5 (194), 12 (97), 14, 16 (388), 17..20, 35, 36, 40, 46 (349), 48, 51, 53 (291 unless noted); 35 | 11 × 7, (6, 1) → (8, 2), 3 |
| 27F (26) | 13242 → 14018 | **Consumables now half as effective!** (new banner) | list 4; 44 | 11 × 7, (5, 3) → (5, 4), 1 |
| 28F (27) | 14018 → 15115 | Enemy movement speed increased! | list 4; 36 | 11 × 7, (1, 3) → (2, 4), 2 |
| 29F (28) | 15115 → 17033 | The floor is now slick! | list 4 (306 for HP 15, 5 = 204, 46 = 367); 46 | 11 × 7, (7, 3) → (3, 4), 5 |
| **30F (29)** | 17033 → 17988 | none | **no actors** | 3 × 3, one open cell (1, 1); see below |

Every exit 21F–29F: the next-floor circle (kind 8) is object 0 at the exit cell centre (the nav goal `portal > 0 and pkind == 8`
held = `0x438bea8` 0, object 0 kind 8); a return portal only exists on floor % 10 == 9 (code). "Go to the next floor?" → Yes.

**Live table stats** (`0x5c23f0 + type·0x68`, s32, HP / AT / DF / MA / MD), types new to this leg: **5 10/20/50/0/80** · **40
15/16/22/24/15** · **48 15/27/30/0/10**. The rest equal `crystal-1f-20f`'s table (8, 9, 10..12, 14..20, 22, 35, 36, 39, 44..47, 50, 51, 53).

## 30F — the rest floor (`floor` 29, `FUN_00450ee0` case 29)

- **Layout:** grid 3 × 3 with one open cell (1, 1) = world (−40, −40), the party placed at (−40, −40). Stage cell values `exitcx`/`exitcz`
  are stale (3, 4 = 29F's). A square hall with a raised dais (stairs on its −z side) holding the chest. No banner, no enemies, no event
  dialogue. `estage` 0.
- **Intro camera:** `FUN_00451546` case 29: `lock` 0 for 371 ticks (camera swing, f17033 → f17405), then control.
- **Objects** (`0x695ef78 + k·0x2a0`):
  - **object 0 = kind 6 at (−48, 0, −42), scale 0.35 = the RETURN DOOR** (a red door, picture `view/f017718.png`), state 0 = shut. As on
    the Lapis boss floors, **a sword hit opens it** (`doorst` 0 → 1: f17788, path `L*4 A .*9` from (−51, −39.3)); walking into it shut
    does nothing (the portal scan only takes kind 6 with state 1, IDA 44668). `doorobj` = 0.
  - **object 1 = kind 4 at (−40, 2, −30), scale 0.5 = a TREASURE CHEST**, not a warp plate. One chest because the trip started at 21F:
    `FUN_00450ee0` places `v18` chests: 3 from start floor 0, 2 from 10, **1 from 20** (at party + 10 z, then (−53, −35), (−27, −35)).
    A sword hit opens it (state 0 → 2 at f17469, item rises, `view/strip_17460_17518.png`); its slot then reads kind −1. Trip loot
    (Daily Profits "Found"): **Cane's Staff, Dragon Fang** (one of them is the chest's; not attributed).
  - No other objects; **no warp plates, no next-floor circle**. Boss floors 30..58 are reached only from the Boss Rush row.
- **Return:** f17826 on the door (`portal` 2, `0x438bea8` = 1) → f17856 "Return to town now?" → `A` → `0x438be98` 2 (returned) →
  "CAME BACK ALIVE!" (scene 9, f17988; bag 17/20) → town map f18045 (`stime` 2, `wms` 1121211).
- **Row time:** at the prompt (floor 29: `floor % 10 == 9`) **row 2 = 11710** (`bank+181700`, 0 before; trip counter `0x438b1e8`
  11673 at f17788 → 11710) — the guild row "Floor 21～30" now reads 00:03:15. Row 3 "Boss Rush" untouched.

## The return and the day end

| frame | what |
|---|---|
| 18065..18168 | map `D D` (`wm_cur` 6 → 3 → 0) → home, `stime` 2 |
| 18225..18706 | door → map → Merchants' Guild (`wm_cur` 3, scene 6) → `B` → map → `D` → home: `stime` 2 → **3** on entering home |
| 18815..19029 | again: home → Merchants' Guild → home: `stime` 3 → **4** (evening; "Thanks for all your help today, Tear!") |
| 19059..19454 | `A` taps: "You also did well. … get some rest." → Daily Profits **Day 34**: Found Cane's Staff, Dragon Fang, Score 9061 (+0) → day dword 34 |
| 19454..19694 | morning HUD Day 35, no event; ticker: "The price of metal items has decreased." / "The price of Daggers has normalized." |
| 19755..20030 | `~` → pause, `D` ×4 → `pm_sel` 4, `A` → slots (cursor on 020) → `A` "Overwriting file. Are you sure?" → `A` → saved; `rd endsave` → `save:1049877e95a4` |

Notes: home → map → home again (no other place) does NOT advance `stime`; a place visit does (on re-entering home). On the map,
`L` from Merchants' Guild (3) is Town Square (2) — it plays a square event (scene 14, `dlg` 1); rewound.

## Card corrections

- **`0x438bea8` is the boolean `(kind == 6)`, not an object index** (IDA 44683: `dword_438BEA8 = *(_DWORD *)i == 6`). On 30F the return
  door is object 0 and `0x438bea8` read **1** on it; the vars.json `pkind` (= kind of object `0x438bea8`) read **−1** there (object 1, the
  opened chest). On 10F / 20F the return portal is object 1, which is why "index 1" looked right. A return goal is
  `portal > 0 and s32(0x438bea8) == 1`; a next-floor goal `== 0`. `pobj` / `pkind` in `tools/drive/vars.json` and DRIVER.md (line ~221)
  need fixing (not edited here).
- ROSTER d5 / UNLOCKS: floor 29 has **1..3 treasure chests (kind 4)**, not warp plates, and the return portal is a **door** opened by a
  sword hit. 21F–25F = list 3 (+ 40), 26F–29F = a fourth list (5, 12, 14, 16..20, 35, 36, 40, 46, 48, 51, 53).

## Tool gaps

1. `pkind` (above) is wrong whenever the return portal is not object 1.
2. The nav script's banner capture (`rd text` at arrival + 60) works; `Will-o'-Wisps` was cut by my parser's quote split, not by `rd`.
3. The town-map `stime` rules (a visit counts, home ↔ map does not) cost two dead ends; a `stime` note in DRIVER.md's Recettear facts
   would save them.
