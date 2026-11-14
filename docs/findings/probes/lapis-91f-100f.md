# Probe: lapis-91f-100f

- **Run:** `lapis-100f` · **start (CRAFTED lineage):** `save:6809401b79a9` = `lapis-81f-90f`'s end save (shop, day dword 30 = HUD
  Day 31 morning, `stime` 0, Louie L46, slot 020, 58,580 pix). Chain: ← `8767348923a8` ← `24ab07c65610` ← `b4766a99d74b` =
  `lapis-51f-60f`'s end save with bank `+0x2bcf7` cleared by `tools/craft_pix_save.py`. So this save descends from a crafted one.
  On this lineage bank **`+0x2bcff` is already 1** (100F intro seen) and the guild's Lapis list already shows a **"96～100" row
  (00:00:00)** before the trip: the 100F long intro (events 12/8, 12/9, 12/13, 12/14, 12/15) was NOT reachable here.
  No task file. Goal: Lapis 91F → 100F in one trip, every 100F event, the Arma golem, then follow the game to control, Save.
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`). Normal floors: the `lapis-71f-80f` nav script (BFS + one scored search
  per cell, `-j 2`, `--hold 45 --hold-no-goal`, zero HP slack). 95F: 21 chained kill searches (`--hold 30`). 100F: a phase script
  keyed on the hands' beam clock (below), 0 HP slack, ~100 searches.
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`, `RD_HOST=win`). Nothing ran under wine on maximus.
- **Movie:** 73,325 frames: Day 31 Market → guild → one trip 91F → 100F → golem → events → CLEAR → town (event 12/20) → home →
  day end → Day 32 morning event → Save. Filed as `tests/scenarios/lapis-91f-100f/trace.jsonl`. **No retail golden** (next:
  `rhost.py golden lapis-91f-100f`, `rhost.py triage lapis-91f-100f`). Not triaged.
- **Verdict (2026-11-14):** `rd verify lapis-100f` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 73325 rng 36ed757b sem 30c3841b254dbccb dev 4b82a069cca0a85e  (unskipped retail)  [win]
  live session:          frame 73325 rng 36ed757b sem 30c3841b254dbccb dev 4b82a069cca0a85e  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 73325 rng 36ed757b scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:7874cefc5cb5` (slot 020 overwritten from the shop's pause menu at f73325: day dword 31 = HUD Day 32, `stime` 0,
  49,410 pix, SCORE 8414 → 8948, TIME 4:39:16 → 4:59:34). The game offered no save prompt of its own after the clear.
- **Damage taken: none** in the filed movie (`rd scan` 3529..60100: `hp` never below its max). **Level-up L46 → L47 at f17361**
  (95F): HP 224 → 228. No food eaten. SP 50 → 1 (spins on 95F), 51 after the level-up.

## Before the dungeon

| frame | what |
|---|---|
| 172 | shop, Day 31 morning (`.*60 A .*30 A .*80`); control |
| 461 | door by search → town map; `L` → Market f509 |
| 553..848 | **Market Food: Apple, Cherry (Out Of Stock), Watermelon (Out Of Stock), Walnut Bread 70, Beef Bowl (Out Of Stock), Candy, Taiyaki 770. Medicines: Ferromin / Ferromin G Out Of Stock.** Bought Taiyaki ×6 (4,620; quantity box `U` ×5, `A` = Yes): 58,580 → 53,960 |
| 932..1079 | `B B` → map; `D R U` → `wm_cur` 6 → Adventurers' Guild: Louie **L46**, fee 4,550; Charme / Caillou / Tielle L40 |
| 1141..3237 | hire `A A`. Bag slot list: `A` on a slot enters its category list; `R` / `L` change category **and skip empty ones**; the row cursor resets to 0 on a category change, otherwise stays on the last row. Packed **Iron Armor, Spiked Armband, Leather Boots, Warrior's Helm, Taiyaki ×12, Beef Bowl ×2, Watermelon ×2** (20/20). Inventory had no sword / shield |
| 3418..3522 | `U` ×20 + `A` → plates → `A` → Lapis rows (66～70 … **91～95 --:--:--, 96～100 00:00:00**), cursor on 96～100; `U` → "91～95" |
| 3529 | **91F**, fee 4,550 (53,960 → 49,410), `stime` 0 → 1 |
| 3728 / 3769 | "Equip items in bag?" (`equipq` 1) → `A` → ATK 64, DEF 84, HP 224, SP 50 |

## Per floor

Level `floor/5 + 25`: L43 (91F–95F: table HP 15 → 144), L44 (96F–100F: 15 → 147). Enemy lists = the actor table at nav start
(`type × count (HP)`; total). No floor banner captured on 91F..99F.

| floor (`floor`) | frames | enemies | grid, start → exit, legs |
|---|---|---|---|
| 91F (90) | 3529 → 4511 | 8, 9×2, 10, 11×2, 12, 14×2, 15×3, 16×4, 17, 18, 19, 20, 22×4, 36, 39, 40, 44 (115), 45 (134), 46 (173), 47 (336), 50, 51×2, 53×3; 37 | 9 × 9, (3, 3) → (3, 4), 1 leg |
| 92F (91) | 4511 → 7448 | 8×2, 9×4 (192), 10×2 (48), 11×4, 12×2, 14×4, 15×7, 16×6, 17×2, 18×6, 19×4, 20×3, 22×3, 36×4, 39×3, 40×2, 44×2, 45×4, 46×2, 47×2, 50×2, 51×3, 53×2; 75 | 9 × 11, (5, 3) → (7, 8), 7 legs |
| 93F (92) | 7448 → 9981 | 8×2, 9×4, 10×2, 11×3, 12×3, 14×6, 15×6, 16×8, 17×2, 18×2, 19×5, 20×4, 22×2, 36×2, 39×4, 40×2, 44×3, 45×2, 46×2, 47×2, 50×5, 51×2, 53×2; 75 | 9 × 11, (4, 7) → (1, 4), 6 legs |
| 94F (93) | 9981 → 12876 | 8×2, 9×5, 10×3, 11×2, 12×2, 14×9, 15×5, 16×7, 17×4, 18×4, 19×2, 20×3, 22×2, 36×2, 39×2, 40×4, 44×2, 45×3, 46×5, 47×2, 50×3, 51×6, 53×4; 83 | 11 × 11, (7, 3) → (1, 4), 7 legs |
| **95F (94)** | 12876 → 21089 | **48 ×11 (144), 45 ×6 (134), 46 ×5 (173), 44 ×4 (115), 47 ×4 (336)**; 30 | mob floor (below); 5 × 7, start (1, 3), exit cell (1, 1) |
| 96F (95) | 21089 → 23142 | 8×4 (176), 9×4 (196), 10×3 (49), 11×4, 12×4, 14×5, 15×6 (147), 16×7, 17×2, 18×2, 19×2, 20×2, 22×4, 36, 39×3, 40×2, 44×2 (118), 45 (137), 46×2, 47×2 (343), 50×2, 51×2, 53×2; 68 | 11 × 9, (5, 2) → (8, 4), 5 legs |
| 97F (96) | 23142 → 26365 | 8×2, 9×3, 10, 11×3, 12×2, 14×3, 15×5, 16×8, 17×4, 18×3, 19, 20×2, 22×3, 36×2, 39, 40×2, 44×2, 45×3, 46×2, 47×3, 50, 51×2, 53×3; 61 | 9 × 11, (3, 6) → (7, 2), 8 legs |
| 98F (97) | 26365 → 27591 | 8×4, 9×4, 10×5, 11×3, 12×3, 14×5, 15×5, 16×9, 17×4, 18×4, 19×2, 20×3, 22×3, 36×2, 39×2, 40×2, 44×2, 45×4, 46×2, 47×2, 50×2, 51×2, 53×3; 77 | 11 × 9, (3, 3) → (1, 3), 2 legs |
| **99F (98)** | 27591 → 32398 | **a different list: 5 ×3 (98), 12 ×2 (49), 14 ×4 (196), 16 ×3 (196), 17 ×7, 18 ×5, 19 ×5, 20 ×5, 35 ×5, 36 ×3, 48 ×2** (147 unless noted); 44 | 9 × 9, (2, 1) → (5, 6), 8 legs; **event 12/21 at the exit** (below) |
| **100F (99)** | 32398 → 66449 (CLEAR) | **73 body + 74 core + 75 / 76 hands**, prop 30 | boss floor, below |

## 95F: the mob floor (knight list)

ROSTER said "none (knight list)". Live: 30 actors at arrival, types 48 ×11 (HP 144 = 15 · 9.6), 45 ×6 (134), 46 ×5 (173), 44 ×4 (115),
47 ×4 (336). `gate` 3; start (1, 3) at (−40, −120), exit cell (1, 1).
- 21 chained kill searches (nearest first, `--avoid "hp < H"`, `--hold 30`), all found, **zero damage**; the other 9 slots were freed
  without being the target. L47 at f17361.
- **f19941: last actor freed → `estage` 1, `gate` 0.** The exit circle is dungeon object 0 (kind 8) at **(−40, −40)** = the exit cell,
  not in the start row as on 85F (a search to (−76.5, −120) reached the spot and nothing was there). `portal` read **2** while the
  "Go to the next floor?" box was up (85F: 1), so `portal == 1` was the wrong goal; `A` → 96F f21089.

## 99F exit: event 12/21 (before the 100F load)

`FUN_00442cef` (IDA 44889): on the exit portal of floor 98 with bank `+0x2bd01 == 0`: event 12/21, `+0x2bd01 = 1`, `be9c = 119`.
Live: `portal` counted 40..57, then at **f30958** `dlg` 1 and `portal` 120; the floor stayed 98 for the whole dialogue (to f32381),
then 100F loaded at f32398. Seen byte `+0x2d685` 0 → 1. Speakers by picture (nameplate as drawn):

| frame | x | line |
|---|---|---|
| 31167 | 218 | (Alouette) Darnit, darnit, darnit! I came here to see Recette, / so why did I end up running away from her?! |
| 31293 | 134 | Hmm. Interesting. / Now why is the dear "Young Mistress" of Big Bash in such a strange, dangerous place as this, I wonder? |
| 31428 | 218 | What th- who are you?! |
| 31473 | 134 | Forgive me for not being able to introduce myself in person. / It is still a... pleasure... to meet you, Alouette. / I am Avall, President of Terme Finance. |
| 31572 | 218 | Terme Finance?! / YOU'RE the one who bought out Big Bash! |
| 31644 | 134 | Your father is such a wonderful, honest man. / So honest with himself, in fact, that he realized he could not defy me / once I had bought his company out from under him. / He even provided me with some... fascinating information. |
| 31770 | 218 | Wh-what are you talking about? What kind of... |
| 31815 | 134 | Hm. Nothing a child need concern herself with. |
| 31896 | 68 | (nameplate "Prime"; Arma not yet on screen) Oi, don't get so far ahead of me, Your Mistressness. |
| 32022 | 280 | (nameplate "Prime"; the purple fairy now beside Alouette) Eh? Wait, who're you talkin' to? |
| 32067 | 134 | My goodness, so you have a fairy as well. |
| 32112 | 280 | (nameplate "Prime") And just who the hell are you? |
| 32157 | 134 | How perfectly serendipitous. / Even one extra fairy will advance my plans significantly. |
| 32265 | 162 | (Alouette) Extra... fairy? What do you... |
| 32381 | | banner "Enemy attack power doubled!" (the 100F banner, drawn before the load) |

The green-haired figure on the right is labelled Prime on the x 68 / 134 / 280 lines; Avall speaks through her ("not in person").

## 100F: the Arma golem

**Entry (short path, `+0x2bcff` 1):** f32398 stage 4 → f32400 stage 8 (tick 0) → **f32640 stage 10** (stage 8 = 240 ticks, `lock` 0,
no dialogue box). Party at (−40, −50); body / core at (−40, 1, −40), hands at (−32 / −48, 1, −40) at stage 10.

**Live stats** (`0x5c23f0 + type·0x68`, **s32**, +0x2c..+0x40): 73 body **HP 300, (+0x30) 25, AT 50, DF 35, MA 50, MD 35**; 74 core and
75 / 76 hands **HP 100, 25, AT 20, DF 30, MA 0, MD 30**; 77 Prime 15 / 30 / 20 / 20 / 0 / 10. Live HP: body **2940**, core 980, hands **980**
(HUD "LV45", ×9.8). The card's body HP 1 / hands 150 were wrong.

Fight f32640 → f59955 (27,315 frames). Body columns: segment `+0xa64`, break `+0xa60`, `+0x428`, stage.

| frame | seg / brk | body HP | what |
|---|---|---|---|
| 32640 | 0 / 0 | 2940 | stage 10, `lock` −1 |
| 35746 | 0 → brk 1 | 2450 | **HP gate clamp** (2940 − 490) → break |
| 36080 | 1 / 0 | 2450 | break over (334 ticks), segment 1 |
| 40040 / 40374 | brk / 2 | 1960 | break 2 |
| 46150 / 46484 | brk / 3 | 1470 | break 3 |
| 51378 / 51712 | brk / 4 | 980 | break 4 |
| 55062 / 55396 | brk / 5 | 490 | break 5 |
| **59955** | 5 / 1, `+0x428` 2 | **0** | kill: the clamp to 0 also sets `+0xa60` |
| 59959 | | | stage 13 |

- Body states entered over the fight: **hover 0 ×48, lob 1 ×23, rain 2 ×24**. Hands' cycle counter 0 → 89; neither hand was killed.
- Damage per sword hit (ATK 64..65, mostly front): **23..26**. ~100 hits. Every hit in the movie landed in hover.
- **What hits the player** (seen in branches the searches discarded; nothing in the filed movie): the hand beam (hit record type 136
  at `0x69324b0`) **133**; contact with a patrolling hand (no record) **142**. The `+0xa60` clamp / break, the core following the
  body, and the hands' lanes (x ≈ −16..−24 and −56..−64, z ≈ −36..−40) match the card.
- After the kill (`+0x2bd00..02` read `00 01 00` before, `01 01 01` after f66445):

| frame | stage / tick | what |
|---|---|---|
| 59959..60358 | 13 | fade, re-placement; party (−42, −57) |
| 60389..61027 | 20 → 21 | **event 12/16** (barrier fading; Recette / Alouette at x 190 / 144; ends "Prime! Tear!") |
| 61087..62818 | 22 tick 1 | **event 12/17** (flag 1): inside the crystal, Prime (x 170) wakes Tear (x 160); calls from outside |
| 62818..63528 | 22 | Prime 77 spawned in slot 2 at (−38, 20 → 6.45, −45); body / core freed |
| 63528..64560 | 22 tick 710 → 23 | **event 12/18** (Prime / Tear freed; Alouette, Recette) |
| 64560..64810 | 23 | `lock` 4 (walk) to tick ~100 |
| 64810..66396 | 23 tick 251 | **event 12/19** (Arma: seals the Lapis Core, gives up dungeon control, "I think... I enjoy shopping.") |
| **66445** | 23 tick 300 | `be9c` 120, `bea8` 1, `be98` 3; bank `+0x2bd00` 0 → 1, `+0x2bd02` 0 → 1 |
| 66449 | | `scene` 9: **"CLEAR!"** + the bag list, **21/20** (Toothpicks+3 picked up on the way); `A` ends it (no forced rearrange) |
| 67069 | | town map (night), `stime` 1 → 3 |
| **67077..71142** | | **event 12/20 on the town map** (`FUN_0049e163`: `+0x2bd02` → 0, `+0x2bc53` = 1, `FUN_00468d22(218112)`): back in town; Alouette and Prime leave; Arma gives Recette an Adventurer's Guild card ("not a forgery"); Tear: "I have no home now" → "Let's go back to Recettear!". Seen byte `+0x2d684` 0 → 1 |
| 71142 | | **control** on the town map (`scene` 8, `dlg` 0) |

Seen bytes `+0x2d680..+0x2d683` = 12/16..12/19 (set by f66449), `+0x2d684` = 12/20, `+0x2d685` = 12/21: base `+0x2d670` + event id.
No credits, no ending roll, no extra save prompt. The first rows of 12/16 (before f60524) were not captured by `tap --text`.

**Fight method** (`arma2.py`, one search per step, `-j 2`, `--avoid "hp < H or <within 7 of a hand>"`, `--hold 30 --hold-no-goal`):
- **hit** (hands' tick `s32(H+0xa58)` < 100 or ≥ 290): goal = body HP down (or a break / death) **and** the beam window closed; cell
  (body state, tick / 20, hand tick / 30, body − player / 2, body y · 2, `p_act`); score = −distance − 4·max(0, y − 1.2).
- **dodge** (hand tick 100..289, or a break): goal = the window closed (and the break over); cell (hand tick / 10, break, body tick / 30,
  position / 3); score = hand tick.
- On a failed search: rewind to the step before (one more step back per repeat failure at the same frame).
What did not work: per-hit goals with `--hold 30 / 60 / 120 / 240` alone (the beam sweep and hand contact land after any single-mask
tail: the shortest refined path re-entered the same doomed state each time); `rd undo` as the retreat (the undo stack also held my
diagnostic `play` / `rewind`, and one undo restored a damaged branch).

## The return and the day end

| frame | what |
|---|---|
| 71142..71199 | `D D D` from `wm_cur` 6 → 3 → 0 → home (`stime` 3) |
| 71326..71505 | shop door by search → map → `R` (3) → Merchants' Guild (f71376) → `B` → `D` (0) → home: **`stime` 3 → 4** |
| 71665..71984 | Daily Profits (Profits −9,170; score 8414 → 8948 (+534); "Found: Toothpicks") → day dword 31, `stime` 0 → "Day 32" |
| 72181..73024 | **Day 32 morning event** (Tear: "the final payment is due in a week… sizable") |
| 73038 / 73143 / 73325 | `~` → pause, `D` ×4 → `pm_sel` 4, `A` → slots (cursor on 020) / `A` / `A` → saved; `rd endsave` → `save:7874cefc5cb5` |

## Card corrections

- `arma-golem.md`: live table rows — body 73 **HP 300 / AT 50 / DF 35 / MA 50 / MD 35** (card: 1 / 20 / 30), hands 75 / 76 **HP 100**
  (card: 150), core 74 HP 100 / AT 20. Lapis 100F live HP 2940 / 980 / 980; a segment is 490 HP; breaks last 334 ticks; the killing
  hit also sets `+0xa60`. Added § "Lapis 100F (observed)".
- ROSTER d4 94 (95F): mob floor, knight list (48 ×11, 45 ×6, 46 ×5, 44 ×4, 47 ×4 at L43), exit = object 0 at the exit cell (−40, −40).
- ROSTER d4 99 (100F): HP numbers above; 12/21 at the 99F exit; 12/20 on the town map after the clear.

## Tool gaps

1. A search whose hold tail survives N ticks but ends one tick before an unavoidable hit is accepted; the refine then returns the
   same shortest path on every retry. A "doomed-state" check (`--hold` that also requires a short survival SEARCH, not one mask) or a
   `--not-path` / path-diversity option would have saved ~1 h of loops.
2. `rd undo` history mixes diagnostic commands with search applies; scripts need `rewind --to F` with their own frame list.
3. Enemy table stats are **s32**; reading them as f32 prints 0 (my first read). A named `estat(type, field)` helper would avoid it.
