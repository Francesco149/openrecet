# Probe: lapis-41f-50f

- **Run:** `lapis-50f` · **start:** `save:1a6b7c4af68d` (end save of `lapis-31f-40f`: shop, day dword 24 = HUD Day 25, `stime` 0) · no
  task file (goal: Lapis Ruins 41F → 50F in one trip, the 45F mob floor, the 50F Arma fight + floor-49 timeline, back, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`); legs `rd search -j 2` (one scored search per BFS cell), 45F and Arma
  = chained searches `--hold 30 -j 2` on a damage goal.
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`; `rd ls` → `win`).
- **Movie:** 39,461 frames (Day 25 burned (guild closed) → Day 26 guild → one trip 41F → 50F → return door → day end → Day 27 morning →
  Save). Filed as `tests/scenarios/lapis-41f-50f/trace.jsonl`. **No retail golden** (`rd file` does not make one: next
  `rhost.py golden lapis-41f-50f`, `rhost.py triage lapis-41f-50f`). Not triaged.
- **Verdict (2026-11-13):** `rd verify lapis-50f` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 39461 rng f4c801c5 sem a0832384cb6b89d0 dev fe6c537b0b4d95ee  (unskipped retail)  [win]
  live session:          frame 39461 rng f4c801c5 sem a0832384cb6b89d0 dev fe6c537b0b4d95ee  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 39461 rng f4c801c5 scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:26ee88a4793c` (slot 020 overwritten from the shop's pause menu at f39461: day dword 26 = HUD Day 27, `stime` 0,
  SCORE 6648 → 6911, TIME 3:51:50 → 4:02:45).
- **Card:** `docs/findings/bosses/arma.md` § "Lapis 50F (observed)".
- **One hit taken on the whole trip:** 45F f14497 (210 → 168, a forced step with an attack already committed); level-up f15468
  (Louie level dword 42 → 43 = L44, HP 215 / 215). No food eaten.

## Before the dungeon

| frame | what |
|---|---|
| 137 / 212 | shop, Day 25 morning (day dword 24); control |
| 539 | door by search → town map, `wms` 1121010: **Adventurers' Guild closed** (7th digit 0; "Closed" at f634) |
| 672..1122 | Market (wm 1): Food tab = 7 × `L` from Necklaces then 3 × `L` back (`L` / `R` walk the tabs); bought **Taiyaki ×6** (770 each; quantity `U`, "Are you sure?" `A`); gold 295,590 → 290,970 |
| 1393 / 1684 / 1991 / 2298 | `stime` 0 → 1 → 2 → 3 → 4 by home after Market / Merchants' Guild / Merchants' Guild / Merchants' Guild (map cursor: 6 −D→ 3 −L→ 2 −L→ 1; 1 −R→ 2 −R→ 3 −D→ 0; 0 −R→ 3; 3 −L→ 2 −D→ 0). The shop door at noon / evening = `R*12 . A` from (2, −3.24) |
| 2361..2731 | "Thanks for all your help today, Tear!" → Daily Profits → **Day 26** (day dword 25), shop f2731; the pad is dead until ≈f2951 (title + news ticker) |
| 3186 | door by search → map `wms` 1121011 (guild open) |
| 3241 | guild (`R U` from Home → `wm_cur` 6): Louie **L43** on the list's first entry; Charme / Caillou / Tielle L40 also listed |
| 3315..4798 | hire `A`; a help scroll covers the pack screen: the 2nd `A` dismisses it, a 3rd enters slot 1's category list. After a pick the focus returns to the slot list and the slot cursor STAYS (press `D` to the next slot, `A` to enter); the category list per slot drifts but the food rows remembered their index. Packed Iron Armor, Spiked Armband, **Taiyaki ×6, Baked Yam, Apple ×11** (20/20). Category order after Spears: Clothes, Robes, Breastplates, Armor, Bracelets, Helms, Shoes, Treasures, Ingredients, Food |
| 4959 / 5000 | Depart → plates (cursor on Lapis) → rows: cursor remembered on **"31～35"** (picture `view/f005000.png`); `D D` → "41～45" (best time `--:--:--`) |
| 5039 | **41F**, fee 4,250 (290,970 → 286,720), `stime` 0 → 1; "Equip items in bag?" at ≈f5354 → `A`: ATK 62, DEF 70, MATK 23, MDEF 43; HP 210, SP 47 |

## Per floor

Level `floor/5 + 25`: L33 (41F–45F), L34 (46F–50F). Enemy lists = actor table ≈60 ticks after arrival (`type × count (HP)`).
Grid = W × H, start cell → exit cell. Each normal floor = BFS of `0x73dfd08` + one scored search per cell (`-j 2`, 60 s; all found
first try), `portal > 0`, `rd tap A --gap 10 --until floor == N` (38 ticks).

| floor (`floor`) | frames | banner | enemies | grid, start → exit |
|---|---|---|---|---|
| 41F (40) | 5039 → 6504 | "Consumables now half as effective!" (`ticker` 30 at f5395) | 6, 8, 11, 12×2, 15×3, 16×2, 17, 18×2, 37, 38, 45, 50, … (first 19 rows read) | 11 × 9, (7, 3) → (9, 3), 2 legs |
| 42F (41) | 6504 → 8248 | not read | not read | not recorded; 6 legs |
| 43F (42) | 8248 → 10324 | "Adventurer attack power doubled!" | 6×2 (114), 8×2 (137), 10×4 (38), 11×3, 12×2, 13×8 (76), 15×5, 16×6 (152), 17, 18×3, 19×2, 22×2, 35×3, 37×3, 38×5, 44×3 (91), 45×2 (106), 50, 51×2, 52×2; 61 | 11 × 9, (3, 5) → (6, 3), 5 legs |
| 44F (43) | 10324 → 12121 | "The automapper will not work here. Enjoy!" | 6×3, 8×3, 10×2, 11×3, 12×3, 13×7, 15×7, 16×11, 17×2, 18×7, 19×2, 22×4, **29**, 35×3, 37×2, 38×2, 44×2, 45×5, 50×4, 51×3, 52×2; 78 | 9 × 11, (1, 5) → (2, 8), 4 legs |
| **45F (44)** | 12121 → 19171 | none | **mob floor: 1×6 (152), 12×4 (38), 18×8 (114), 37×6 (114), 48×10 (114); 34** | 5 × 7 arena, `gate` 3, event stage 0 — below |
| 46F (45) | 19171 → 22525 | "Will-o'-Wisps will appear soon. Enjoy!" (no type 34 seen at +60) | 6×2 (117), 8×4 (140), 10 (39), 11×2, 12×2, 13×4 (78), 15×6, 16×8 (156), 17×4, 18×2, 19×2, 22×4, 35×2, 37×3, 38×3, 44×2 (94), 45×2 (109), 50×5, 51×2, 52×3; 63 | 9 × 11, (3, 3) → (6, 3) |
| 47F (46) | 22525 → 23969 | "Adventurers now nearly blind!" | 6×2, 8×2, 10×4, 11×2, 12×2, 13×6, 15×5, 16×9, 17×5, 18×3, 19×2, 22×4, 35×4, 37×2, 38×3, 44×3, 45×4, 50×4, 51×2, 52×2; 70 | 9 × 11, (2, 5) → (7, 7) |
| 48F (47) | 23969 → 26263 | none | 6×4, 8×3, 10×3, 11×2, 12×2, 13×4, 15×4, 16×14, 17×2, 18×3, 19×2, 22×2, 35×4, 37×3, 38×3, 44×2, 45×2, 50×2, 51×2, 52×2; 65 | 9 × 11, (1, 5) → (6, 5) |
| 49F (48) | 26263 → 27591 | "This level is full of traps. Enjoy!" | 6×2, 8×2, 10×2, 11×4, 12, 13×7, 15×5, 16×5, 17, 18×2, 19×2, 22, 35×3, 37×4, 38×2, 44×3, 45×2, 50×2, 51, 52×6; 57 | 11 × 9, (7, 5) → (6, 3) |
| **50F (49)** | 27591 → 37700 (results) | none | **41 Arma ×1 (1950, L34)** | own setup — below |

No kills on 41F..44F or 46F..49F (walked around everything).

## 45F — the mob floor (no boss)

`gate` 3 at arrival, event stage `s32(0x438b8a0)` 0 (the generic `FUN_0044d0a4`), no banner, no boss-class type; player at (−40, −40),
the exit object `ex ez` (−36.5, −40) sunk. 34 enemies spread over the 5 × 7 grid. Cleared by 40 chained searches (goal = the sum of
live enemy HP ≤ current − 60/80, `--score` minus that sum, `--hold 30`, `--avoid "hp < H − 12"`, actions = 8 walks ×8 / ×24 + 4 aimed
swings + `A .*9`). The 45F killing stalled once (f14491, "one cell" ×2: a type 48 at 3.4 units in its attack state 1 and a dying bomb
(type 37, `U` 2) at 11 units): one step with the avoid loosened to −45 took the only hit of the trip (210 → 168, f14497).
Sum 3800 → 0; **all 34 dead at f17681** → event stage 0 → 1, `gate` 3 → 0, `estage` 1 at f17682. No respawns seen. Next-floor
portal (−35, −40) by a scored search (f19133, `portal` 2), 46F f19171.

## 50F — Arma (summary; card § "Lapis 50F (observed)")

| frame | what |
|---|---|
| 27591 | arrival: party (−40, −52), Arma slot 0 at (−40, −34), state 8, **HP 1950 = 250 · 7.8** (L34; HUD "Lv35"), `bank+0x2bd13` 0 |
| 27631..27711 | stage 0: event tick 40.. ; `lock` 0 → 4 at tick 60 |
| 27732 | **event 12/6** (`sid` 12006, `dlg` 1) at stage-0 tick 141 (party walked to (−40, −40.79)) |
| 27732 → 30033 | the dialogue (A taps, gap 8): "Arma!" … "I am Avall, the President of Terme Finance." … "Give us Arma back!" … The event tick AND Arma's state-8 tick both hold at 141 |
| 30142 | state 8 → 0 (state tick 250) — event stage still 0, `lock` 0 |
| 30147 | **state 3 (spread)** at once: the player stands on her x axis 6.8 away |
| 30163 | stage 0 → **3** (event tick 270), `lock` −1 |
| 30214 | first hit (fresh: `+0xb08` 10, `+0x440` 0; 1950 → 1922) |
| 30704 | **first state 4 (fans)** |
| 30946 | **first state 2 (lob)** |
| 31071 | **first state 1 (volley)** |
| 36204 | last state 3 |
| **36253** | **HP 4 → 0 (side hit, 30), `+0x428` 1 → 2** |
| 36257 | stage 3 → **4** |
| 36596 / **36597** | stage 4 → 5 / **`bank+0x2bd13` 0 → 1** |
| 36626 → 37262 | **event 12/7** (`sid` 12007): "Arma!" / "Hm. It seems there is not enough power here to fully utilize all of this golem's functions. I actually have to retreat. How insulting. To the lowest level, then. Follow me... if you wish." / "Wait! WAIT! LEAVE ARMA HERE! ARMAAAAAA!" / "Arma..." |
| 37292 | stage 5 → **7**, `lock` −1, `gate` 3 → 0, `estage` 7 |
| 37312 | Arma `+0x428` 2 → 0 (stage-7 tick 20) |

Fight f30163 → f36253 (6,090 frames, 32 chained searches, ≈45 s each). State changes in the fight: state 3 ×38, 2 ×9, 1 ×6, 4 ×5,
**5 never** (the searches avoided `a0st == 5`). Hits: 61 fresh (`+0xb08` = 10, `+0x440` 0), ≈19 non-fresh (`+0xb08` = 30, `+0x440` 40).
HP drops per hit 22..60 (mode 30); ATK 62 vs DF 35 → base `ftol(31 − 8.75)` = 22, matching the low end. **No damage taken**.

§8 arms hit: intro, walk legs, spread, volley, lob, fans, fresh hit, non-fresh hit, death d4 (stage 5 / 7). Not hit: finisher (both sides), finisher abort, already-beaten; flee heading and random want not checked.

## The return and the day end

| frame | what |
|---|---|
| 37292..37432 | stage 7; next-floor portal at (−35, −40) (`ex ez`), the **return door at (−45, −40)** |
| 37548 | `R` (−x, `p_oct` 6) from (−43.6, −39.5) into the door → `portal` 2 (an `A` in between reset the walk; plain `R*14` works) |
| 37568 | "Return to town now?" Yes / No at `portal` 22 |
| 37569 / **37579** | `A` / **`portal` 0** = Yes |
| 37700 | results bag (`scene` 9) "20/20" (nothing picked up, no food used) |
| 37777 | `B` → town map, **`stime` 1 → 2** (departed at 0) |
| 38005..38620 | home (no change: no visit), Merchants' Guild + home → 3 (f38313), again → **4** (f38620) |
| 38683..39086 | "G'night!" → Daily Profits "Day 26" (Profits 0, Score Difference 6651 (+0)) → **Day 27** (day dword 26), shop |
| 39244 | `~` → pause (ESC answered after ≈150 ticks) |
| 39344 / 39349 / 39400 / 39461 | `D` ×4 → `pm_sel` 4 (`pm_type` 3) / `A` → slots, cursor on 020 / `A` → "Overwriting file. Are you sure?" / `A` → SCORE 6911, TIME 4:02:45; `rd endsave` → `save:26ee88a4793c` |

## Story / events

- **Event 12/6** (f27732 → f30033) and **12/7** (f36626 → f37262) on 50F; texts above. No other `dlg` 1 on the trip.
- Day 25 the Adventurers' Guild was closed all day (`wms` 7th digit 0; also day 17 in `obsidian-21f-30f`): the player-knowledge rule
  "closed every 7th day" (`player-knowledge.md`). Town Square showed 2 (an event waits) and the Pub 2 in the afternoon; neither entered.

## Food

None eaten. Bought Taiyaki ×6; packed them + Baked Yam + Apple ×11; all came back.

## Tool gaps

1. `rd text` still has no `--at-frame`: the 42F banner / enemy list were not recorded (the floor script's output was cut).
2. A pipeline `script | tail` in a backgrounded Bash call holds all output until the end — progress monitors saw nothing. Write to a
   file instead (driver-side lesson, not an `rd` bug).
3. No "sum over actors" in expressions: the 45F goal summed 40 hand-written `f32(hp)·(s32(use) == 1)` terms (~560 postfix tokens,
   just under the 598 limit) as a `define tot --expr`. An `actsum(FIELD, COND)` would make mob floors one-liners.
