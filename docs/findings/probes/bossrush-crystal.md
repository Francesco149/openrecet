# Probe: bossrush-crystal — Crystal Nightmare's own "Boss Rush" row (run 1 stopped on 55; loadout run `bossrush-crystal-c` complete)

- **Run:** `bossrush-crystal` · **start (CRAFTED):** `save:672d7fbd4b1b` = `ea4fa3d6ec75` (the `bossrush-lapis-r` end save: Endless,
  slot 020, day dword 37 morning, 490,600 pix) + `tools/craft_pix_save.py --loadout rush --keep-level` (Louie's level dword 48 = L49 kept,
  the played one; plain `--loadout rush` would lower it to 47 → sha `b5e055073b8b`, not used). Gear Holy Sword, Legendary Armor, Superarm
  Band, Legendary Helm, Iron Sandals → **ATK / DEF / MATK / MDEF 181 / 279 / 67 / 129**, HP 237, SP 53. No task file.
- **Host:** Windows box only (`RHOST_WIN=win-wifi`, `RD_HOST=win`). One early `-j 2` search spilled a worker to bunker (35F, f13900,
  nothing applied: "one cell"); `-j 1` after that. Nothing under wine.
- **Status:** floors **30..54 cleared** (25 bosses), **55F (ねずみマグロ, type 60, L92, 2910) unfinished**: king 1962 at f132500, checkpoint
  **`stop55`** (Louie L51, HP 140 / 246, food left: Melon ×1, Cherry, Apple+3, Walnut Bread (Melons eaten f111761, f131147)). Stopped on the 90-min-per-boss rule
  (55F 09:40 → 11:05 wall). Not filed, no end save. **`rd verify` (headless) of the 132,500-frame movie: MATCH** (rng 359c1c2e,
  sem 32923ffc5d04a2aa). `--real` not run.

## RE answer (`../bosses/boss-rush.md` §1b)

Row 3 is **not an alias** of modes 1..4: the commit sets `dungeon` 5, `floor` = `0x438b4d8` = 10·row = **30**, and leaves
`0x438b4d0` / `0x438b4d4` at **−1** → `sub_45C025` = 0, every portal on 30..58 is next-floor (kind 8), floor 59 (60F) = the rest floor
(3 chests + the return door), levels = Crystal's own `ftol((floor − 30)·1.7 + 50)`, drops = dungeon 5's tables, best time → `0xb172`.

## Live confirmation

| frame | what |
|---|---|
| 0..173 | `rd new … --from save:672d7fbd4b1b`; `.*60 A .*30 A .*80` → house, control at once (**no Endless card on this load**, unlike `bossrush-lapis`) |
| 448 / 556 | door search → map; `D R` → `wm_cur` 6 → guild (Louie **L49, fee 4,850**) |
| 598..1750 | hire `A`, help `A`, `A` slot 1, `R` ×13 → Food. Packed: **Curry ×5, Ham Sandwich ×8, Melon ×4, Cherry, Apple+3, Walnut Bread** (20/20). Picker: `A` picks, the cursor stays on the item and the bag row; `D` (next row) `A` (reopen) `A` (pick) per item |
| 1891..1973 | row 0 Depart → plates: cursor on Lapis (4); **`D` → 5 (Crystal)**; `A` → rows "1～10 00:04:19", "11～20 00:05:30", "21～30 00:03:15", **"Boss Rush --:--:--"**; `D` ×3 → row 3 (checkpoint `guildrow`) |
| 1990 | `A` → **`dungeon` 5, `0x438b4d0` −1, `0x438b4d4` −1, `0x438b4d8` 30, `floor` 30**, gold 490,600 → 485,750, `stime` 0 → 1 |
| 2590 / 2606 | "Equip items in bag?" → No (`R A`) |
| 2830 | `lock` −1; 31F crown slime **L50 HP 1100**; `0x9643128` = 1 |

Row unlock as code-read: this save's Crystal deepest dword `0xb106` = 58 (≥ 14 → 4 rows). Each floor: 240-tick pad lock (arrival + 240),
party at (−40, −45), kind-8 portal at (−40, −40) after `estage` 1 (+~180 ticks), "Go to the next floor?" → `floor + 1`. 37F's portal is
Tielle's spot (−45, 65), as a **next-floor** circle (`pret` 0), reached from (−45, 63.5).

## Per floor

Levels = the §1b table on every floor (`+0xae0` live = 50, 51, 53, 55, 56, 58, 60, 61, 63, 65, 67, 68, 70, 72, 73, 75, 77, 78, 80, 82,
84, 85, 87, 89, 90, 92). HP = the first boss's live HP 10 ticks before the lock ends (`rd scan --where "s32(0x438b8a4) == 230"`).

| floor | boss | L / HP each | lock −1 → `estage` 1 | Louie HP (min seen) | how |
|---|---|---|---|---|---|
| 30 | 23 crown slime | 50 / 1100 | 2830 → 3776 | 237 | 6 searches, no damage; gate broken at ATK 181 |
| 31 | 24 mushroom king | 51 / 1680 | 4240 → 7402 | 237 → 221 | ~14 searches; one red meal (199 → 277) |
| 32 | 25 Charme | 53 / 1160 | 7877 → 8819 | 221 | |
| 33 | 54 boss ahriman | 55 / 1200 | 9323 → 10521 | 221 | |
| 34 | 67 Ohm | 56 / 1830 | 11097 → 12178 | 221 | |
| 35 | 68 crab | 58 / 2142 | 12662 → 18817 | 221 | bomb (type 37, HP 189) killed within 5.5 of the crab → blast flip → back hits (~150 / search); last 88 HP by a blast |
| 36 | 23 crown slime ×2 | 60 / 1300 | 19387 → 21351 | 221 | |
| 37 | 27 Tielle | 61 / 1320 | 21835 → `estage` 10 27666 | 221 → 127 (lobs ~38) | phase 0: break blocks 26 / 18 / 12 (col x −40.2) and reach 2.5; phase 1: blocks 26 / 18 / 13 then her block 1 (−32.6, 25.25) from (−36.4, 25.25); phase 2: block 15 then her block 10 (−28.8, 67.9) from (−25, 67.9); fight `estage` 7 f26575 → kill f27266 |
| 38 | 55 pumpkin ghost | 63 / 2040 | 28199 → 33179 | 130 | |
| 39 | 70 jellyfish | 65 / 1400 | 33688 → 35264 | 130 | |
| 40 | 54 ahriman ×2 | 67 / 1440 | 35831 → 39525 | 130 | |
| 41 | 61 golem (+ 62 / 63) | 68 / 2190 each part | 40096 → 42489 | 126 → 241 | body only; **level-up L49 → L50 f40632** |
| 42 | 28 griffon | 70 / 3000 | 43017 → 46791 | 241 | |
| 43 | 67 Ohm ×2 | 72 / 2310 | 47387 → 51114 | 241 → **59** | charge ~96 at DEF 279 |
| 44 | 49 black knight | 73 / 1560 | 51678 → 52884 | 159 | |
| 45 | 28 griffon (slot 1) + 72 hand (slot 0) | 75 / 3200 + 1600 | 53415 → 58167 | 159 → 212 | griff kill takes the hand |
| 46 | 55 pumpkin ×2 | 77 / 2460 | 58709 → 65785 | 212 → 231 | |
| 47 | 59 rat | 78 / 2490 | 66332 → 74529 | 231 → 91 | red meals heal max/6 (+415); progress only once reds of age > 420 were killed first (`rat` loop below); purple windows then |
| 48 | 54 ahriman ×3 | 80 / 1700 | 75075 → 80292 | 216 → 166 | bats crowd; a 9-HP ahriman stayed out of reach ~20 min |
| 49 | 67 Ohm ×3 | 82 / 2610 | 80825 → 85768 | 166 → **98** | hold tails strafing ⟂ to each Ohm (`{dpad(aNz − pz, px − aNx)}*1`, both signs, N = 0..2); **level-up L50 → L51 f85072** |
| 50 | 23 crown ×3 | 84 / 1780 | 86509 → ~90300 | 246 | no food |
| 51 | 55 pumpkin ×3 | 85 / 2700 | 90944 → ~101700 | 246 | `frame // 40` in the search cell unblocked it ("1–4 cells" before) |
| 52 | 71 red jellyfish | 87 / 1840 | 102217 → ~105500 | 246 → 137 | |
| 53 | 41 Arma | 89 / 4700 | 106033 → ~113000 | 137 → 246 | |
| 54 | 49 black knight ×2 | 90 / 1900 | 113647 → ~117500 | 246 | |
| 55 | 60 rat | 92 / 2910 | 118284 → — | 246 → 140 | **unfinished**: 1962 at f132500 (`stop55`) |

(`estage` 1 frames for 50..54 from the fight logs, not scanned.)

## 55F: why it stalled

- `+0xab8` 3 → DF 52·9 = 468: outside state 4 every sword hit is 1–2. Damage only in purple windows (state 4, 120 ticks): one
  window search (goal −500, score `−a0hp`, `--apply-best`, avoid `a0hp` up / state 5) took **2892 → 2392** in 490 frames.
- Red meals heal max/6 = **485**. The king's lunges (state 1) hit ~100 at L92 vs DEF 279. Killing reds of age > 420 before they reach
  600 works (each 194 HP, 2–3 hits), but the loop thrashed: the state after a window is often a committed lunge, the tiered searches
  fail, and the rewinds (200..500 frames) step back over the window's damage. A red heal after a window reset 2105 → 2908 once.
- Net: 2910 → ~1960 in ~85 min wall (14,200 frames).

## Projection (why the run was not pushed to 58)

- 56F dark golem L94: body 2970 (and arms), 57F black crab L95 3000 (bomb blasts), **58F Arma golem L97: body 6120, core / hands
  2040**, attack ×12.64 → the body fan (record 152, 168–173 at L54 / DEF 278) ≈ **350 at L97 > Louie's 246 max HP**: every fan is a kill.
  `bossrush-lapis-r` needed ~4 h wall for the L54 golem (3540) with fans survivable; here food is nearly gone (Melon ×1).
- A finish needs a stronger Louie (level / gear beyond `rush`) or many more heals (Curry / Melon from the Market) — a new loadout.

## Loadout run `bossrush-crystal-c` (2026-11-16, complete: filed, MATCH)

- **Start: CRAFTED state** `save:e13e408068f1` = `ea4fa3d6ec75` + `tools/craft_pix_save.py --loadout crystal` (Louie dword 98 = L99, own gear
  Holy Sword, Invincible Armor, TH Shield, Asuran Helm, Iron Sandals → **221 / 519 / 95 / 211, HP 457, SP 98**; Heaven's Blade, Durian ×13,
  Filet Mignon ×6 in the inventory). Not a played state. Windows native only (`RHOST_WIN=win-wifi`, `RD_HOST=win`, every search `-j 1`).
- **Scenario `tests/scenarios/bossrush-crystal-c`, 142,542 frames. `rd verify` MATCH, `rd verify --real` MATCH** (frame 142542, rng 4919566c,
  sem cf739a977cf9594e). **End save `save:e85ade90b178`** (slot 020, day dword 38 morning, 480,750 pix, SCORE 10889, TIME 6:40:17, Endless).
- **Best time:** `bank·0xb172` 0 → **123,420** (= trip counter `0x438b1e8`, written on the "Return to town now?" `A` at f138711: 34:17 at
  60 ticks/s). §1b confirmed.
- **Before (f0..4152):** load 020 `.*60 A .*30 A .*80` (house f137, no Endless card on load); door search → map f743; `D R` → `wm_cur` 6 →
  guild f832. Hire `A` (fee **9,850** at L99); the picker opens on **Swords**: `A` on Heaven's Blade (slot 1); slot 2 → tabs `R` to Food
  (order …, Treasures, Ingredients, Cards, **Food**); Durian ×13 then Filet Mignon ×6 by `A` + (`D A A`)×N (20/20). Depart → plate `D` (Lapis →
  Crystal), rows `D` ×3 → "Boss Rush --:--:--" → `A` f3102: `dungeon` 5, `0x438b4d0` / `b4d4` −1, `b4d8` 30, gold 490,600 → 480,750.
  "Equip items in bag?" (f3906) **No** (`R A`): ATK 221 at control f4152 (the blade stayed in the bag).
- **Method:** the run-1 scratch drivers (chained `rd search`, goal summed boss HP − 200, `--hold 50`, HP tiers); 35 / 57 bomb-flip loop;
  37F lanes by block searches; 55F a new window loop (below); 58F goal clamped to the segment floor, `--hold 160`, HP tiers −120.
  Food: Durian = HP to full, SP to 0. Six eaten (49F f64902; 58F f117927, f125307, f128286, f132286, f133909). Eat from the dungeon pause
  menu: `~ .*40 D .*10 A .*30 D .*10 A .*20 A .*30 B .*15 B .*15 B .*30` (the `D` before the use skips bag row 1 = the blade / Holy Sword).

| floor | boss | lock −1 → `estage` 1 (frames) | sword hits on the boss (damage) | Louie HP (min) | mechanics seen |
|---|---|---|---|---|---|
| 30 | crown slime | 4152 → 4918 (766) | 11 (45–147) | 457 | **power gate skipped**: power 5 → 73–132 per hit (DF 200 vs ATK/2 110); shed 5 → 3.66; no absorb seen |
| 31 | mushroom king (24) | 5411 → 7649 (2238) | 15 (103–186) | 457 | 0, 1, eat 2, purple 4 (f6660), **red heal 5 +280** (f6338) |
| 32 | Charme | 8210 → 8942 (732) | 9 (85–150) | 457 | 0, 1 |
| 33 | boss ahriman | 9478 → 10654 (1176) | 7 (111–258) | 457 | 0, 1 |
| 34 | Ohm | 11208 → 12155 (947) | 13 (109–171) | 457 | walk 1, charge 2 |
| 35 | crab (68) | 12682 → 18212 (5530) | 13, all flipped (150–240) | 457 → 455 | **5 flips** (state 6) by bomb blasts; states 0..6 all seen (spray 4 f17371); the flips did 0 damage; every sword hit landed flipped |
| 36 | crown slime ×2 | 18772 → 19958 (1186) | 12 + 12 (61–167) | 455 | gate skipped (61–140 at power 5); shed to 1 |
| 37 | Tielle | 20451 → `estage` 10 24354 (3903) | 9 (110–279) | 455 → 445 | phase 0 lane 26 / 18 / 12 + reach; phase 1 her block 2 (−40.2, 25.25) by 27 / 19 / 7 / 2; phase 2 her block **5** (−32.6, 71.7) by 24 / 11 / 5; fight stage 7 f23182 → kill f24010 |
| 38 | pumpkin ghost | 24886 → 28312 (3426) | 16 (13–261) | 445 | 0, cast 1 |
| 39 | jellyfish (70) | 28889 → 30027 (1138) | 10 (97–183) | 445 | **both phase steps**: 0 → 1 at 927 (< 933.3), 1 → 2 at 316 (< 466.7); no hit near a third |
| 40 | ahriman ×2 | 30543 → 33003 (2460) | 10 + 10 (94–268) | 445 → 420 | 0, 1 |
| 41 | golem (61) | 33570 → 35325 (1755) | 16 on the body (93–255) | 420 | 0..3; parts never targeted |
| 42 | griffon | 35841 → 38580 (2739) | 19 (35–273) | 420 | 0, 1, 2, 3, 5 |
| 43 | Ohm ×2 | 39176 → 40953 (1777) | 18 + 17 (28–160) | 420 → 298 | walk, charge, burst 3; charges taken |
| 44 | black knight | 41557 → 42635 (1078) | 9 (99–295) | 298 | 0, 1 |
| 45 | griffon (slot 1) + hand (slot 0) | 43139 → 45850 (2711) | 19 on Griff (104–405), hand 0 | 298 | griff 0..3; the hand died with Griff |
| 46 | pumpkin ×2 | 46426 → 52742 (6316) | 17 + 20 (71–251) | 298 → 296 | 0, 1 |
| 47 | rat (59) | 53233 → 58648 (5415) | 24 (8–221) | 296 → 280 | 0..4; 4 purple windows; **no red meal eaten**; pow-2 gate broken (hits outside 4) |
| 48 | ahriman ×3 | 59178 → 63724 (4546) | 11 + 11 + 9 (102–238) | 280 → 254 | 0, 1 |
| 49 | Ohm ×3 | 64314 → 66520 (2206) | 20 + 19 + 20 (35–170) | 254 → **187** → 351 | 0..3; Durian f64902 |
| 50 | crown ×3 | 67029 → 69286 (2257) | 16 + 17 + 15 (34–180) | 351 → 314 | gate skipped (61–97 at power 5); **absorb seen** (power back to 5 on slots 1 / 2) |
| 51 | pumpkin ×3 | 69916 → 80170 (10254) | 22 + 22 + 21 (21–221) | 314 | 0, 1 |
| 52 | red jellyfish (71) | 80726 → 82769 (2043) | 10 (72–343) | 314 → 457 | **phase 1 → 2 at 1105** (< 1226.7), **2 → 3 at 583** (< 613.3); HP + SP full the kill tick f82739 (level 98 kept; source not read) |
| 53 | Arma | 83282 → 86986 (3704) | 33 (61–312) | 457 | 0..4; finisher 5 not seen |
| 54 | black knight ×2 | 87567 → 90239 (2672) | 10 + 13 (24–347) | 457 | 0, 1, 2, block 4 |
| 55 | rat (60) | 90748 → 108959 (18211) | 57 incl. chips (1–234) | 457 → 288 | **16 purple windows** (state 4), **3 red heals of 485** (f97956, f100026, f106348); gate holds outside 4 (1–2); kill f108415 |
| 56 | dark golem | 109462 → 111709 (2247) | 19 on the body (74–346) | 288 | 0..3 |
| 57 | black crab (69) | 112315 → 116989 (4674) | 23 (1–575; one blast 575) | 288 | **5 flips** (one blast on the flipped crab −575); states 0..6 (spray 4 f115525); sword unflipped 1–2, flipped 119–175 |
| 58 | Arma golem | 117584 → 136542 (18958) | 41 on the body (15 clamp – 323) | 288 → **171** (Durian ×5) | hover 0, lob 1, rain 2; **6 clamps exact**: 5100 f120403, 4080 f123321, 3060 f126290, 2040 f129844, 1020 f133633, 0 f136364 (kill) |
| 59 (60F) | rest floor | 137392 | — | 363 | 3 chests opened by sword (state 2): Fur Robe, Pot Stand, Chocolate Bar; door hit f138651 (`doorst` 1), prompt f138705 |

- **Wall time:** 30..54 ≈ 2 h 15; 55F 56 min; 56..57 ≈ 30 min; 58F ≈ 45 min. No floor near the 90-min rule.
- **55F window loop (what fixed run 1's stall):** order of branches: state 5 (heal) → pass; state 4 → chained one-hit searches (goal
  `a0hp <= B − 100`, avoid `hp < H − 40` / king HP up / state 5, cell `(int(px*2), int(pz*2), int(a0hp/20), frame // 6)`, 20 s, `--apply`, until
  the window ends; none found → a pass search to `state != 4`); state 2 → **pre-position**: red target (`+0xa50` slot, type 13) → kill it,
  else walk to < 3 of the king and hold < 3.5 until the state changes; states 0 / 1 / 3 → kill reds older than 420, else wait for 4.
  Windows then gave 3–4 hits each (≈ 500): 2904 → 2391, 2523 → 1283 (two windows chained), 1763 → 1190, 521 → 0. The losses: 3 red heals
  (+485 each), mostly while a red-kill search ran into the king's walk to another red.
- **58F blade swap (first live use; works):** on the floor, before the first hit, `~ .*40 D .*10 D .*10 A .*30` (pause entry 2 = the Equip
  page, `pm_type` 5: "Optimum Equipment", the 5 slots, the bag) → `D .*12` (weapon slot) → `A .*25` (bag list; preview "Attack 221 → 301,
  Magic 95 → 69, MDef 211 → 186") → `A .*30` on Heaven's Blade (the Holy Sword goes to the bag as "Adventurer's Possession"). `s32(0x56db0ac)`
  reads 301 after the menu closes (still 221 while it is open), MDEF 186. **Trap:** close with `B` until `scene == 1`, not a fixed count —
  this run sent three `B`s: the menu closed after two, the third was a **skill** (SP 98 → 93, `p_act` 4, f117791) whose hit took the golem
  body 6120 → 5797 (−323, f117801) before the fight. That is the only skill use in the movie (SP scan). On return the blade is back in the
  bag and the Holy Sword back on Louie (CLEAR screen bag list).
- **Arms the gear skipped:** crown slime power gate (30 / 36 / 50: power 5 does 61–140 at ATK 221); mushroom-king type 59 pow-2 gate (47).
  **Held:** rat 60 pow-3 gate (55: 1–2 outside state 4 at ATK 221); 58F body HP gate (all 6 clamps); jellyfish thirds (39 / 52: no hit
  reached a third). **Not seen:** Arma finisher 5 (53).
- **After:** return `A` f138711 → CLEAR → town map f138896 (`stime` 2); `wm_cur` 2 (scene 14, an event, `A` taps) → map → home f140140
  (`stime` 3); door → map → `wm_cur` 2 again → home f140865 (`stime` 4) → "You look tired" → Daily Profits (−9,850, score +440) → day 38
  f141322 → Endless card (`A` taps until the ticker) → save: `~`, `D` ×4 (`pm_type` 3), `A`, `A` on 020, `A` Yes → f142542; `rd endsave`.
- **Map cursor (`wm_cur`) moves seen:** 0 −L→ 1, 1 −R→ 2, 1 −U→ 4, 4 −D→ 1, 1 −D→ 0, 0 −R→ 3, 6 −L→ 5, 5 −U→ 0. Home = 0, guild = 6.

## Card corrections (both runs; folded into the cards 2026-11-16)

- **boss-rush.md:** §1b (native mode `0x438b4d0 = −1`, floors 30..58 + rest floor 59, levels 50..97, best time 0xb172) — now live end to end:
  levels / HP on all 29 floors, every portal next-floor, rest floor 3 chests + door, 0xb172 = trip counter at the return prompt.
- **tielle.md:** L61 (Crystal row) HP 1320 (= 100 · 13.2); lobs ~38 at DEF 279; block grids / indices as the card; her block = `+0xa64`
  (run 1: 1 then 10; run c: 2 then 5) — lanes per run in §8; 37F's portal in mode −1 is a next-floor circle at (−45, 65).
- **jellyfish.md:** type 70 live L65 HP 1400 (table 100); type 71 live L87 HP 1840 (table 100); phase steps at the thirds on both (39: 927 / 316,
  52: 1105 / 583).
- **crabs.md:** type 68 bombs at L58 HP 189 (run 1); type 69 at L95 HP 3000, bomb type 38 HP 300 (= 15 · 20); a dying bomb 10 from the crab
  never flips it; a kill within ~5.5 (run 1) / < 8 (run c, type 69) of the crab's centre did; the bomb drifts toward the player — stand
  behind the crab. Five flips on each of 35 / 57; one blast on the flipped type 69 did 575.
- **SAVES.md:** `672d7fbd4b1b` (run 1) and `e13e408068f1` (run c) are in the loadout table; the crystal 58F blade swap now has its key
  sequence and the `B`-count trap; end save `e85ade90b178` to add.

## Tool gaps

1. `rd rewind --frames N` crosses floor boundaries: a rewind from 55F landed on 54F's end, the fight script read "boss gone" and the
   campaign took the portal again (the movie is consistent, but the boss-gone test must use `floor`). Scripts now rewind with `--to max(F0, …)`.
2. A rewind into an earlier pause menu replays the meal → food counters kept outside the game drift; read the bag (`~` → Items) instead.
3. `--hold-actions` / `--acts-extra` split on `|` collided with `{… | A}` masks (scratch-script bug, not rd).
4. Searches in states where the player cannot act (pumpkin cast, knock-down) report "1–4 cells": a time term in `--cell` fixes it.
5. (run c) A driver's rewind floor `F0` defaulted to the frame the script started: after a restart inside a doomed state (58F, a fan in
   flight) every rewind stayed in it. Scripts now take `F0` from the environment (the floor's start).
6. (run c) Fixed `B` counts to close a pause menu over-run into the dungeon: the extra `B` is a skill. Close with `B` until `scene == 1`.
