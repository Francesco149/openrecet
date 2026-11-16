# Probe: bossrush-lapis (IN PROGRESS — stopped on floor 58)

- **Run:** `bossrush-4` · **start (CRAFTED lineage):** `save:1aa94b103208` = `bossrush-obsidian-ending`'s Endless save (slot 020, shop,
  day dword 36, 495,350 pix, Louie L48). Upstream crafted: `2f2f2203a65d` (+1,000,000 pix) ← `2e4388a2cfe2` ← … ← `b4766a99d74b`
  (`lapis-51f-60f` with bank `+0x2bcf7` cleared). The Lapis Boss Rush row was already open in the save (d4 `0xb105` = 99, `0xb16e` = 1)
  and showed **00:04:19** (= dword `0xb16f` = 15591, a best time this lineage already carried). No task file.
- **Goal:** Endless Mode, Lapis Ruins (d4) Boss Rush (13 floors, d5 floors 46..58) → town → home → day end → Save over 020.
- **Status:** floors **46..57 cleared, zero net HP lost on most floors** (lowest HP 172 / 237 on 55F); **floor 58 (Arma golem) unfinished**:
  body 3540 → 3078 at f129219 (checkpoint `stop58`; ~19 hits), stopped there. Not filed, not verified, no end save (see "Where it stopped").
- **Driver:** subscription subagent. Every fight by chained `rd search` (`--apply`, `--hold 60..100`, strict `--avoid "hp < H"`), driven by
  scratch scripts (per-step goal = summed boss HP − 70..150; on a failed search: rewind to the previous step start, then a 150-tick
  "survive" search or a perturbation). Rat floors, crab and golem had dedicated phase loops (below).
- **Host:** Windows box (`RHOST_WIN=win-wifi`); one `-j 2` worker spilled to bunker once (f95376..95572 search). Nothing under wine.

## Before the rush

| frame | what |
|---|---|
| 137..2599 | load 020 (`.*60 A .*30 A .*80`); the **Endless title card plays again on every load** (`dlg` 1, 2,427 ticks of `A` taps) |
| 2732 / 2840 | door (search) → map; `L` → `wm_cur` 1 → Market |
| 3255..3548 | Food page (7th `R` from Necklaces): Taiyaki ×8 (4,928), Beef Bowl (1,610), Watermelon (1,232): 495,350 → 487,580. Medicines: Ferromin (1 left, 700) / Ferromin G (Out Of Stock, 1,750) = "Increases max HP" (not bought) |
| 3760 | `D R U` → `wm_cur` 6 → guild: Louie **L48**, fee **4,750**; Charme / Caillou / Tielle L40 |
| 3886..5521 | bag 20/20: Watermelon, Walnut Bread, Beef Bowl, Taiyaki ×6, Baked Yam, Apple+3, Apple ×5, Leather Boots, Warrior's Helm, Spiked Armband, Iron Armor. Packing does not advance the slot cursor (`D` between slots) |
| 5562..5796 | Depart; plate cursor 3 → `R` → 4 (Lapis); rows: `D` ×20 → row 20 "Boss Rush 00:04:19" |
| 5802 | `A` → `dungeon` 5, `0x438b4d0` 4, `floor` 46, `stime` 0 → 1, gold 487,580 → 482,830 |
| 5834..5865 | "Equip items in bag?" (first `A` at f5803 was dropped: the box takes input ~17 ticks later) → ATK 66, DEF 85, HP 232, SP 52 |

## Per floor

Levels `f/5 + 25`, f = 5·floor − 145. Table = live `0x5c23f0 + type·0x68` s32 (HP / AT / DF / MA / MD). Arrival = `floor` changes; the
240-tick pad lock ends `lock` −1; party at (−40, −45) every floor.

| floor | boss (slots) | L / HP each | table | lock −1 → last kill → `estage` 1 | how | Louie HP |
|---|---|---|---|---|---|---|
| **46** | 55 pumpkin ghost ×2 (0, 1) | 42 / **1410** | 150/20/20/35/10 | 6069 → 16412 → 16412 | 13+ searches; adds 15 / 52 / 53; spins early | 232 → 232 |
| **47** | 59 rat (0), `+0xab8` 2 | 43 / **1440** | 150/26/42/10/42 | 16878 → ~24150 → 24693 | purple windows only (state 4); one red meal healed 273 → 512; 2 Taiyaki | 232 → 204 |
| **48** | 54 boss ahriman ×3 (0..2) | 44 / **980** | 100/20/20/35/20 | 25165 → ~32950 → 33057 | 20 searches; bats not counted in the score | 204 → 204 |
| **49** | 67 Ohm ×3 (0..2) | 45 / **1500** | 150/30/25/20/25 | 33838 → 45638 → 45639 | ~85 searches; Watermelon first (204 → 232); hold tails strafing ⟂ to the charging Ohm; bees in the score (×30) | 232 → 232 |
| **50** | 23 crown slime ×3 | 46 / **1020** | 100/30/8/0/10 | 46162 → 55019 → 55093 | 900 ticks of survive (power shed), then 26 searches; blue slimes (type 2) ×60 in the score | 232 → 232 |
| **51** | 55 pumpkin ghost ×3 | 47 / **1560** | (as 46) | 55623 → 73127 → 73128 | 40 searches; **level-up L48 → L49** between f70521 and f71986 (HP 237, SP 53) | 232 → 237 |
| **52** | 71 red jellyfish | 48 / **1060** | 100/30/35/20/35 | 73632 → 75879 → 75880 | 10 searches with spins (SP 53 → 3) | 237 → 237 |
| **53** | 41 Arma | 49 / **2700** | 250/50/35/40/35 | 76326 → 84230 → 84289 | 24 searches, avoid `a0st == 5` (finisher) | 237 → 237 |
| **54** | 49 black knight ×2 | 50 / **1100** | 100/35/20/0/20 | 84813 → 88739 → 88843 | 24 searches | 237 → 237 |
| **55** | 60 rat (0), `+0xab8` 3 | 51 / **1680** | 150/28/52/10/52 | 89359 → ~112250 → 112774 | the hard floor (below); 4 Taiyaki | 237 → 172 (contact chips 5) |
| **56** | 64 dark golem + 65 / 66 | 52 / **1710** each part | 150/30/30/0/40 (all three) | 113199 → 117072 → 117147 | Beef Bowl first (172 → 224); 15 searches on the body; hold 100 + straight-run tails for the double jump-at (pattern idx 9 / 10) | 224 → 224 |
| **57** | 69 black crab | 53 / **1740** | 150/40/30/20/30 | 117588 → 122295 → 122470 | bomb → light near the crab → blast (flip + ~270 damage) → back attacks while `+0xa5c` 1 | 224 → 224 |
| **58** | 73 body (0), 74 core (1), 75 / 76 hands (2, 3) | 54 / body **3540**, core / hands **1180** | 73: 300/50/35/50/35; 74..76: 100/20/30/0/30 | 123047 → — | **unfinished**: body 3540 → 3078 at f129219 (~19 sword hits of 24–27) | 224 |

## 55F: the type-60 rat (what made it work)

- Outside state 4 (`+0xab8` 3 → DF·9) every hit does 1–2; only purple meals (state 4, DF 0) or a stale power-up give damage
  (sword back ~33–60 per hit). Red meals heal max/6 = **+280**; the first approach (accept heals) went 1271 → 1680 → … net ≈ 0.
- Read in `sub_42B6B7`: the eat pick takes the nearest mushroom of **age ≥ 600** (`*((int*)v31 − 35) >= 600`, types 13 / 14), with
  `rand & 1`. Eat tick 38: `+0xa74` := 0, then (mushroom still in use) 2 red / 1 purple; tick 48: `+0xa70` := `+0xa74`; tick 120:
  `+0xa70 != 0` → state `(+0xa74 != 1) + 4`. So **killing the target before tick 38 = no meal, no heal (`+0xa70` 0)**, and **a walk blocked for
  180 ticks (`+0xa5c`) falls through with the previous meal's `+0xa70` / `+0xa74`** → a free power-up window after a purple meal.
- Mushrooms occupy actor slots up to at least 24: an `actors(…, 1, 15)` or 16-slot scan misses them.
- Working loop: kill the king's current target / eligible reds (priority: the target in state 2, then reds of age ≥ 560 nearest the king),
  survive while purples age, avoid `a0hp > H_king or a0st == 5 or +0xa74 == 2`; damage only in state 4.

## Where it stopped (58F)

- Live: body 3540 (segment 590), core 1180 (never hittable), hands 1180, lanes x ≈ −16 / −64, z ≈ −40 (same as 100F).
- Per hit 24–27; one hit per hover window, ~300–500 frames per hit, and ~1 in 2 searches lands in a doomed state (hover landing /
  beam sweep after the hold tail). Rate since f123047: ~490 HP in ~6,800 frames, ~4 h wall. **~120 more hits needed**; stopped on the
  parent's time limit at f129219 (body 3078, Louie 224 / 237, SP 0, food left: Walnut Bread, Baked Yam, Apple+3, Apple ×5).
- Avoid used: `hp < H or within 7 of a hand or (body state 0, body tick ≥ 185, within 8 of the body)`; hit goal: body HP down and
  > 8 from the body and (body state ≠ 0 or body tick < 160) at arrival; hold 90 with straight-run tails.

### Macro attempt (2026-11-15, `tools/drive/macros/golem.py`)

- A per-tick policy expression (attack while the body hovers low, else retreat to a point below / beside the body, a fixed zone
  during the hands' sweep, under the body during rain; clamped inside the disc) played in 100-tick chunks; a chunk that loses HP is
  reloaded and retried with another of up to 11,520 parameter sets (20 random tries), then the driver steps 1..8 chunks back.
- Result: body 3078 → **3002** at f135972 (checkpoint `B3`), ~75 HP in ~30 min of good running (≈ 150 HP/h), then ~40 min with no
  surviving chunk (every 100-tick chunk after a near-doomed state loses HP: fan 182, beam 61, lob 61–173). Not clearly better than the
  search loop (~115 HP/h); at this rate the remaining 3002 HP need ~20 h. Stopped per the 1-hour check.
- Why it is not faster: the hits come only in short low-hover windows (≤ 1–2 swings per 300-tick hover, often none after rain while
  the body sinks from y 8), and the three projectile sources (fan at hover t 120, beams on the hand clock, the lob) need positions the
  policy cannot predict; random parameter retries rediscover the same dead ends.
- Next idea: model the dangers explicitly (fan lines = player's octant ±0, ±0.38, ±0.56 rad from the body at t 120, speed 0.3; beam ray
  from the hand joint at yaw ± 0.3, sweep +0.02·ramp; lob landing point ~15.7 ahead of the body along its aim) and steer to a point
  outside all of them, instead of retrying; or feed Louie more ATK (equipment) before this floor.

## Card corrections

- **boss-rush.md:** mode-4 roster filled (levels 42..54, live HP, tables 59 / 60 / 69 / 71 / 41 / 64..66 / 73..76); §3d added.
- **ROSTER.md:** d5 46..58 rows: live L / HP.
- **mushroom-king.md:** eat rules section added (eligibility age ≥ 600, target kill → no meal, walk-timeout stale power-up, slots ≥ 24).
- **arma-golem.md:** d5 58 party start (−40, −45) (code read said (−40, −60)); live damage table (fan 182, beam ~61, lob 61–173, fall 23 off the radius-~39 disc).

## Tool gaps

1. A search re-applied after `rewind` converges to the same doomed path (refine): needed a random perturbation after every rewind.
2. `actors()` slot ranges silently miss actors in higher slots (mushrooms in 17..24).
3. Each `-j 2` search replays the whole movie in the worker (~20 s at 120k frames); `-j 1` was faster late in the run.
4. Long single-floor fights (≈ 100+ hits) are too slow for chained per-hit searches; a dedicated scripted dodge macro for the golem
   (beam clock + hover-landing timing) would be needed.

## Loadout run `bossrush-lapis-r` (2026-11-15/16, complete: filed, MATCH)

- **Start: CRAFTED state** `save:efa3dcf87875` = `1aa94b103208` + `--loadout rush` (Louie L47 / L48 in the guild, own gear Holy Sword,
  Legendary Armor, Superarm Band, Legendary Helm, Iron Sandals: ATK / DEF 181 / 278, food in the inventory). Windows native
  (`RD_HOST=win`), nothing under wine. Scenario `tests/scenarios/bossrush-lapis-r` (68,508 frames); **`rd verify` MATCH, `--real` MATCH**
  (frame 68508, rng bb8fd889, sem 04f11cb343c2d276). **End save `save:ea4fa3d6ec75`** (slot 020, day dword 37 morning, 490,600 pix,
  SCORE 10449, TIME 6:00:47, Endless).
- **Status:** 46..58 cleared; golem killed **f62802**. The 58F fight was stopped once at f53041 (body 1491, 90-min rule), then resumed on the
  coordinator's 2-hour budget; the movie was cut back to f52412 (body 1770) while finding the doom (below). Food left at the end:
  Ham Sandwich ×4, Walnut Bread, Melon ×2, Taiyaki ×2, Baked Yam ×4, Mont Blanc ×2 (bag 16/20 on the CLEAR screen).
- **Before:** load 020 f137..2599 (Endless card, `A` taps); door search → map f2952; `D R` → `wm_cur` 6 → guild f3047; hire `A`, help `A`,
  `A` enters slot 1; `R` ×13 → Food (Apple ×12, Apple+3, Cherry, Melon ×2, Walnut Bread, Ham Sandwich ×4, Curry ×5, Taiyaki ×2, Baked Yam ×4,
  Mont Blanc ×2). Packed from the bottom of the list (cursor clamps to the new last row as rows empty): Mont Blanc ×2, Baked Yam ×4,
  Taiyaki ×2, Curry ×5, Ham Sandwich ×4, Walnut Bread, Melon ×2 (20/20); no Market visit. Depart → `R` (Lapis) → `D` ×20 → Boss Rush
  (00:04:19) → `A` f4890: gold 495,350 → 490,600. "Equip items in bag?" f5320 answered No (`R A`). Control f5562.
- **Method:** every fight by chained `rd search` (scratch driver: goal summed boss HP − step, avoid `hp < H` then `hp < H − 15..20`,
  hold 40..60, actions aimed at the lowest-HP boss); 55F = purple-meal → state-4 windows (avoid red meal `+0xa74 == 2`, heal state 5,
  king HP up); 57F = flip (`kst == 6`) then damage while flipped; 58F = goal clamped to the segment floor, avoid the hover landing, `--hold 160`
  (`body st 0, tick >= 180, within 8.6`). Food: Curry (pause → Items → Use → row → `A A`) at 52F start (175 → 232), 55F (127 → 237),
  56F start (183 → 237), 58F twice (130 → 237 at f55447, 107 → 237 at f59480). **Curry heals ≥ 110 (to full); Baked Yam is SP-only** ("Can't recover any further" at full SP).
- **Level-up L48 → L49** between 53F and 54F (HP 232 → 237).

| floor | boss | lock −1 → last kill → `estage` 1 | HP drops on the boss (dmg range) | Louie HP (min) | mechanics seen (`aNst`, scans) |
|---|---|---|---|---|---|
| 46 | pumpkin ghost ×2 | 5562 → 9664 → 9828 | 15 + 12 (60–202) | 232 → 232 | descent 8, hover 0, cast 1 |
| 47 | rat 59 | 10278 → 12411 → 12955 | 29 (23–144) | 232 | wander 0, swing 1, eat 2, heal 5 (red meal); **pow-2 gate broken at ATK 181** (no purple window needed) |
| 48 | ahriman ×3 | 13373 → 15813 → 15977 | 9 + 9 + 9 (16–179) | 232 | circle 0, beam 1; bats (6 / 8 / 9) crowd the floor: strict no-damage searches looped |
| 49 | Ohm ×3 | 16477 → 17933 → 18108 | 13 + 13 + 15 (41–232) | 232 → 204 | walk 1, charge 2 (**charge hit 13** at DEF 278), burst 3 |
| 50 | crown slime ×3 | 18584 → 20293 → 20459 | 10 + 14 + 12 (48–142) | 204 → 175 | **power gate BROKEN**: power 5 → **48–61 per hit** (3.66: 69–109, 2.32: 87–110, 1: 49–142); shed 5 → 3.66 → 2.32 → 1, absorb → 5 seen; one swing hit two slimes |
| 51 | pumpkin ghost ×3 | 20962 → 27323 → 27487 | 16 + 16 + 15 (41–184) | 175 | 0, 1 |
| 52 | red jellyfish | 27899 → 29306 → 29465 | 9 (36–144) | 232 (Curry first) | killed in 9 hits; phase steps not watched |
| 53 | Arma | 29911 → 32074 → 32193 | 25 (83–136) | 232 | intro 8, hunt 0, volley 1, lob 2, spread 3, fans 4; **finisher 5 never started**; step 75 (one hit) worked, step 200 looped |
| 54 | black knight ×2 | 32663 → 34126 → 34290 | 10 + 10 (40–139) | 237 → 223 | walk 0, throw 1, block 4 |
| 55 | rat 60 | 34712 → 43294 → 43838 | 25 incl. chips (1–186) | 223 → 183 (**min 127**, Curry) | 0, 1 (lunges ~30 each at DEF 278), 2, spray 3, power-up 4; **pow-3 gate HOLDS at ATK 181** (DF 52·9 = 468 → 1–2); state-4 sword 112–186; 4 purple windows; ~8,600 frames, ~45 min |
| 56 | dark golem | 44262 → 45855 → 46030 | 15 on the body (60–168) | 183 → 237 (Curry) | 0, 1, 2, 3; arms never targeted (body kill ends it) |
| 57 | black crab | 46450 → 48364 → 48539 | 19 (1–282, blasts included) | 237 | walk 0, jab 1, slam 3, bubbles 5, **flip 6 ×2** (bomb blasts); flipped sword 103–131 |
| 58 | Arma golem | 48983 → **62802** → 62980 | 41 on the body (46 clamp – 186) | 237 → 130 (**min 107**, Curry ×2) | hover 0, lob 1, rain 2; **6 clamps, exact** (`+0xa60` 0 → 1): 2950 f49906, 2360 f50853, 1770 f52102, 1180 f55919, 590 f59320, **0 f62802 = kill** (`+0x428` 2 same tick); chips taken 5 / 23 (hand beam / sweep, accepted by the 2nd search tier); one hand hit (116) |

- **58F:** ~14,000 frames, ~4 h wall in total (2 sessions). **The 168 hit was the body FAN (record 152), not a hover landing** (the earlier 173 at the same hover tick ~242: most likely the
  fan too, not scanned): hit-record scan at f53100..53103: four type-152 records 2.5–3.2 from Louie for 20+ ticks, then 232 → 64 at hover tick ~240 (fired at
  t 120; slow, they reach the player ~120 ticks later even 9–10 from the body). The "hover-landing" avoid (`st 0, t ≥ 180, within 8.6`)
  only helped indirectly; what fixed the loop was **`--hold 160`** (an arrival at hover t ≲ 100 must survive past t ~250 = the fan's
  arrival). Holds 60 / 120 converged on the same doomed hit every time. After return: door sword hit f63188 (`doorst` 1), "Return to
  town now?" f63251, CLEAR f63383, town map f63460 (`stime` 2), home f63550; Market (event) + `wm_cur` 2 (event) → night home `stime` 3,
  Market closed at night, `wm_cur` 2 again → home f67414 `stime` 4 → Daily Profits (Found Chocolate Bar) → day dword 37 f67888; save 020
  f68212..68508 (`~`, `D` ×4, `A`, `A`, Yes).
- **Card corrections:** crown-slime (gate fails at ATK 181: 48–61 at power 5; `mid` is required there, as the preset table says);
  mushroom-king (type 59 pow 2 gate broken at ATK 181; type 60 pow 3 holds; lunge ~30 at DEF 278); ohm (charge 13 at DEF 278);
  arma-golem (d5 L54 fan record 152 = **168–173 at DEF 278 / MDEF 129**, arriving ~120 ticks after t 120 — the card's "hover landing 142" on d4 is probably the same fan; all 6 clamps live = max − 590·(s+1), the 6th = the kill).
- **Tool gaps:** (1) `ck` savestates are never freed: ~110 checkpoints + undo over 41k frames → `live: err savestate pool exhausted`
  (every search / ck failed in 1 s); `rd stop` + replay (30 s) clears it — a `ck drop` / LRU would help. (2) `rd scan --watch aNtype`
  reads stale types in freed slots (a mushroom / bat slot keeps a boss type) — needs `aNuse`.
