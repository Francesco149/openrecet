# Probe: bossrush-crystal — Crystal Nightmare's own "Boss Rush" row (IN PROGRESS: stopped on floor 55)

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

## Card corrections

- **boss-rush.md:** §1b added (the row is the native mode `0x438b4d0 = −1`, floors 30..58 + rest floor 59; levels 50..97; best time
  0xb172); the levels row is confirmed live 30..55.
- **tielle.md:** at L61 (Crystal row) HP 1320 (= 100 · 13.2), lobs ~38 at DEF 279; block grids / indices as the card (phase 1: her
  block 1 at (−32.6, 25.25); phase 2: block 10 at (−28.8, 67.9)); 37F's portal in mode −1 is a next-floor circle at (−45, 65).
- **jellyfish (type 70):** live L65 HP 1400 → table HP 100.
- **crabs.md:** type 68 bombs at L58 HP 189; a dying bomb 10 from the crab never flips it; kill it within ~5.5 (it drifts toward the
  player — stand behind the crab).
- **SAVES.md:** `672d7fbd4b1b` = `ea4fa3d6ec75` + `--loadout rush --keep-level` (Louie L49) — add to the loadout table.

## Tool gaps

1. `rd rewind --frames N` crosses floor boundaries: a rewind from 55F landed on 54F's end, the fight script read "boss gone" and the
   campaign took the portal again (the movie is consistent, but the boss-gone test must use `floor`). Scripts now rewind with `--to max(F0, …)`.
2. A rewind into an earlier pause menu replays the meal → food counters kept outside the game drift; read the bag (`~` → Items) instead.
3. `--hold-actions` / `--acts-extra` split on `|` collided with `{… | A}` masks (scratch-script bug, not rd).
4. Searches in states where the player cannot act (pumpkin cast, knock-down) report "1–4 cells": a time term in `--cell` fixes it.
