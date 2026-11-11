# Probe: obsidian-41f-50f

- **Run:** `obs-50f` · **start:** `save:8d209cc3acd7` (end save of `obs-40f`: town map, day 19 `stime` 2) · no task file (goal: Obsidian
  Tower 41F → 50F in one trip, the 45F Black Knight, the 50F Griff + Archdevil's Arm finale, back to town, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`); legs `rd search -j 2`, fights `-j 3` / `-j 4`
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`; `rd ls` → `win`).
- **Movie:** 38,892 frames (one trip, 41F → 50F). **Not filed.**
- **Verdict (2026-11-11):** `rd verify obs-50f` (headless, Windows):
  ```
  fresh headless replay: frame 38892 rng 5788e767 sem 908db3bde2e93c0e dev b624fd0c7e28452b  (unskipped retail)  [win]
  live session:          frame 38892 rng 5788e767 sem 908db3bde2e93c0e dev b624fd0c7e28452b  (skips 414ee2: dev not compared)  [win]
  MATCH
  ```
  Session rebuild = 38,892 frames in 16.8 s.
- **End save:** `save:3cf6ae8d5fac` (slot 020 overwritten from the town map's pause menu at f38892, day 20 `stime` 2; 0xaf39 =
  0x01010101, 0xaf3e = 0). No intermediate save.
- **Cards:** `docs/findings/bosses/black-knight.md` §10, `griff.md` §12 "50F", `demon-hand.md` §8 (arms marked **[obs fNNNN]**, stats
  corrected from retail).

## Before the dungeon

- Load `.*60 A .*30 A .*120` → town map f212 (gold 421,100, `wms` 1121211, 0xaf39 = 0x00010101: bank+0x2bce4 / e5 / e6 = 1, **e7 = 0**,
  0xaf3e = 0).
- **Market** f240: Food stock Apple, Cherry (1 left), Watermelon (1 left), Walnut Bread, Beef Bowl (1 left), Candy, Taiyaki; Medicines
  only Ferromin (max HP up) / Ferromin G (out of stock); no weapons / armour categories. Bought Cherry, Watermelon, Beef Bowl, Taiyaki ×3,
  Apple ×4 (421,100 → 414,170). Leaving the Market closed the guild again (`wms` 1121210, as on day 18).
- Burned day 19: home (`stime` 3, f1439) → shop door by search → Merchants' Guild (f1607, `B` leaves) → home → night → "Day 21" banner
  f2201 (day dword 20, `stime` 0). The shop gave control after the news ticker (~f2440); door by search f2604.
- Day 20 map `wms` 1121011, `R U` → 6, guild f2667. Louie **L41** (cursor default), equipment shown Longsword / Scrap Plate / Wooden
  Shield / Old Muffler. Hire = `A`; the pack screen needs a second `A` (f2758 + 25) before the category list takes focus (the first is
  eaten). The category list remembers its category per slot and drifts; read `rd text` (header at y 40, description at y 368) to know
  where the cursor is.
- Storage held equipment: Tough Clothes, Thick Robe, Shell Chestpiece, **Iron Armor**, **Spiked Armband**, Warrior's Helm, Leather Boots.
  Packed Iron Armor + Spiked Armband + 18 food (Cherry ×7, Beef Bowl, Taiyaki ×3, Candy Apple, Baked Yam, Watermelon ×3, Apple ×2), 20/20.
- Depart → Obsidian → rows: cursor remembered on "31~35"; `D D` → **"41~45"**; depart f4920 (fee 4,050, `stime` 0 → 1).
  "Equip items in bag?" f5341 → `A`: **ATK 57 → 60, DEF 59 → 68, MDEF 33 → 41** (MATK 22); the replaced Scrap Plate and Wooden Shield go
  into the bag (2 slots).

## Per floor

Enemy lists = the actor table after the banner (`type × count (max HP)`). Route = BFS of the maze grid (`0x73dfd08`, start / exit cells
`0x438b434..440`) + one scored search per 40-unit cell (`-j 2`, 60 s). Object 0 (`ex ez`) is again a drop (kind 2 on 41F), not the exit.
Levels `floor/4 + 16`: L26 (41F–44F), L27 (45F–48F), L28 (49F–50F).

| floor (`floor`) | frames | banner | enemies | grid / route | HP |
|---|---|---|---|---|---|
| 41F (40) | 4920 → 8131 (control f5353) | "Attack power doubled for everyone!" (to f5704) | 63: 0×6, 2×7, 4×4 (217), 17×7, 18×5, 19×6, **20×5** (93), 44×3 (74), 45×4 (87), 46×5 (112), 47×4 (217), 52×4 (124), 53×3 (93) | 11 × 9, (3, 2) → (7, 5), 9 legs + a kill | 202 |
| 42F (41) | 8131 → 11724 | none | 57: 0×4, 2×3, 4×4, 17×4, 18×5, 19×7, 20×7, 44×3, 45×5, 46×6, 47×3, 52×3, 53×3 | 9 × 11, (2, 1) → (4, 7), 10 legs | 202 |
| 43F (42) | 11724 → 13310 | "The map of this level has been revealed!" | 61: 0×10, 2×3, 4×4, 17×5, 18×4, 19×4, 20×3, 44×4, 45×3, 46×6, 47×5, 52×5, 53×5 | 9 × 11, (3, 3) → (6, 4), 4 legs | 202 |
| 44F (43) | 13310 → 15713 | none | 77: 0×7, 2×6, 4×8, 17×6, 18×5, 19×4, 20×7, 44×4, 45×6, 46×8, 47×4, 52×5, 53×7 | 11 × 11, (6, 1) → (5, 8), 8 legs | 202 |
| **45F (44)** | 15713 → 17655 | none | Black Knight (type 49, L27, HP 640); knights 44..48 kept at 3 | boss floor, below | 202 → 121 → 206 (level 42 at f17366) |
| 46F (45) | 17655 → 22911 | "Attack power doubled for everyone!" | 38: **16×7** (128), 18, 19×2, 20, **21×2** (160), 44×4 (77), 45×3 (90), 46×2 (115), 47×4 (224), **48** (96), 50×4, **51×3** (96), 52×2 (128), 53×2 (L27) | 9 × 9, (5, 1) → (4, 6), 7 legs + 3 kill detours | 206 → 174 → 143 (type 16, 32 + 31) |
| 47F (46) | 22911 → 23880 | "Adventurer defense power doubled!" | 56: 16×10, 18×2, 19×2, 20×3, 21×4, 29 (96), 44×6, 45×3, 46×2, 47×3, 48×4, 50×2, 51×3, 52×5, 53×6 | 9 × 9, (3, 5) → (3, 7), 2 legs | 143 |
| 48F (47) | 23880 → 27024 | "Enemy movement speed increased!" | 86: 16×13, 18×4, 19×4, 20×4, 21×7, 44×6, 45×6, 46×4, 47×4, 48×7, 50×7, 51×8, 52×6, 53×6 | 11 × 11, (7, 1) → (5, 9), 10 legs | 143 → 118 (type 20, 25) |
| 49F (48) | 27024 → 30170 | "SP consumption halved!" | 66: 16×10 (132), 18×3 (99), 19×4, 20×7, 21×3 (165), 44×3 (79), 45×5 (92), 46×5, 47×7 (231), 48×3, 50×5, 51×4, 52×4 (132), 53×3 (L28) | 9 × 11, (3, 2) → (6, 7), 8 legs | 118 → 206 (Watermelon +60, Cherry) |
| **50F (49)** | 30170 → 38499 (town map) | none | Griff (type 28, L28, HP 1320) + the Arm (type 72, HP 660); swarm bats type 6 | boss floor, below | 206 → 131 → 195 → 145 |

Floor change = `portal > 0` on the exit cell's centre, then `rd tap A --gap 10 --until "floor == N"`: 38 ticks. Banners start ~1 tick after
`lock` −1 and run ~350 ticks. All three hits on 42F..49F came from a search applied on its goal frame next to an enemy whose attack was
already committed (one cell found for 70–130 s afterwards; the hit had to be taken).

## New enemy types (not in `obsidian-1f` … `31f-40f`)

Stats at `0x5c23f0 + type*0x68` (+0x2c HP, EXP, AT, DF, MA, MD; rf[0..3] at +0x44).

| type | name (ROSTER family) | HP | EXP | AT | DF | MA | MD | rf | kill |
|---|---|---|---|---|---|---|---|---|---|
| 20 | (knight-row enemy, d3) | 15 | 25 | 22 | 4 | 0 | 0 | 0.8, 1.5, 2, 2 | 41F idx 4, f6066 (one spin, 93 → 0; attack-doubled floor). Hit taken on 48F: 25 (f≈259xx) |
| 16 | — | 20 | 9 | 17 | 4 | 0 | 0 | 0.8, 1.5, 2, 2 | 46F idx 23, f18908. Its attack: 32 and 31 on 46F (ftol(17·3.7/2 − 17) = 14, ×2 attack-doubled floor = 28, +0..5) |
| 51 | skeleton (second variant) | 15 | 20 | 22 | 18 | 0 | 20 | 0.9, 1.5, 2, 2 | 46F idx 20: **killed three times** f19967 / f20161 / f20333 (`+0xadc` 2 → 1 → 0; revives at full HP 96 ≈ 94 ticks after each kill, f20061 / f20263); freed at once after the third (death tick 2). First sword hit 96 → 4 (92, back + attack-doubled) |
| 21 | — | 25 | 30 | 24 | 8 | 0 | 0 | 0.8, 1.5, 2, 2 | 46F idx 37, f22694 |
| 48 | knight (red?) | 15 | 30 | 27 | 30 | 0 | 10 | 0.92, 1.5, 2, 2 | 45F idx 1 (BK summon), f16290 (spin); hit Louie 202 → 168 (34) f16100 |
| 45 | knight | 14 | 15 | 19 | 20 | 0 | 0 | 0.95, 1.5, 2, 2 | 45F idx 6 (BK summon), f17199 — first type-45 kill of the Obsidian probes |
| 49 | Black Knight | 100 | 30 | 35 | 20 | 0 | 20 | 1.2, 1.5, 2, 2 | 45F f17289 (card) |
| 72 | Archdevil's Arm | 100 | 30 | 20 | 20 | 35 | 20 | 0.8, 4, 3, 4.5 (flags 1) | cannot be hit for damage; dies with Griff (f34763) |

Knights 44 (12 / 10 / 17 / 18 / 0 / 0, rf 0.8), 46 (18 / 20 / 21 / 23), 47 (35 / 25 / 24 / 27, rf 0.9): read, not new kills (44 killed on 1F).

## 45F — the Black Knight (card: `docs/findings/bosses/black-knight.md` §10)

| frame | what |
|---|---|
| 15713 | on 45F: `gate` 3; BK (type 49) at (−40, 0, −34), state 8, **HP 640** (L27); party (−40, −45); `kn` 0 |
| 15953 | `etick` 240: state 8 → 0, `lock` −1 |
| 15954..15956 | knights 48, 44, 46 drop in, one per tick |
| **16008** | **frontal block**: sword at `a0rel` 0.56, HP 640 unchanged, state → 4 t 0; → 0 at t 20 (f16032) |
| **16048** | side hit with the shield up (`a0rel` 1.27): 640 → 606 |
| 16080 | state 1 (throw); `bksh` 0 from t 21 (f16105) |
| 16100 | knight 48 hit: 202 → 168 (34) |
| 16174 | state 1 → 0 after t 90 |
| 16193 | side hit 606 → 571 |
| 16224 / **16225** | 2nd throw: record 81 at t 25 / **Louie guards it** (`p_inv` 15, no damage) |
| **16239** | frontal hit in state 1 t 36, shield down: 571 → 538 (counter) |
| 16290 | one spin: knight 48 killed, knight 46 83 → 20, BK 538 → 477; knight 48 freed f16296 → new type-46 knight f16297 |
| **16405** | **wave hit taken**: state 1 t 25, facing away: 168 → 121 (47) |
| **16519 → 16539** | frontal block → **state 2 burst** (player 2.29 away): untouchable t 1..100, record 14 at t 40 (f16579), player 6 away; → 0 f16640 |
| 16689 / 16769 / 16851 | damage searches (spins from behind): 477 → 376 → 275 → 136 |
| 17199 | type-45 knight killed (BK 136 → 69 on the way) |
| **17289** | **BK dies**; `p_inv` 1200; tick 60 (f17393) knights → dying; f17366 Louie L42 |
| 17453 | BK freed, `estage` 1, `gate` 0 |
| 17617 | "Go to the next floor?" at (−35.86, −40.44) → 46F f17655 |

## 50F — Griff + the Archdevil's Arm (cards: `griff.md` §12, `demon-hand.md` §8)

Names (run `vars.json`): Griff = actor 0 (`a0*`, `gst gt gctr ghc gpc gguard gunt gph ghp gb54 gstag` = `B+0xa54 a58 a60 a64 a68 a6c b08
ae4 42c b54 440`); the Arm = actor 1 (`htype huse hst ht hsub hcnt hsw hx hy hz hhp hyaw ha00` = `H+0x424 428 a54 a58 a5c a60 a64 3f0 3f4
3f8 42c 420 a00`, H = 0x76c514); `r14 r22 r23` = records of that type in table-B slots 0..39. Bank 0xaf39 at entry 0x00010101
(bank+0x2bce6 = 1: no event 7/7; 0x2bce7 = 0: the fight is on).

| frame | what |
|---|---|
| 30170 | on 50F: Griff (−40, 0, −35) state 8, HP 1320 (L28); party (−40, −56); no Arm yet; `gate` 0, `estage` 0 |
| 30200 | `etick` 30: **the Arm spawns in slot 1** at (−35, 20, −25), state 8, **HP 660** (100·6.6) |
| 30215..30320 | Arm descends 0.16/tick to y 3.0; `lock` 4 (scripted walk, party z −56 → −40.4) ≈ `etick` 50..130, then `lock` 0; no banner, no dialogue |
| 30451 / **30452** | `etick` 280 → `estage` 3 / **both released** (Griff and Arm state 8 → 0), `lock` −1; Griff `ghc` 282 (counted through the intro) |
| 30466 | Griff slash (state 1); none of the phase-0/1 slashes hit |
| 30487 | first hit (fresh, spin): 1320 → 1245 |
| **30612** | **counter**: second hit inside 60 (`ghc` 55): `gctr` 1, `gstag` 40; again f30788, f30942. No counter burst hit Louie |
| **30664** | **Arm sweep 1** (count 1): state 2; sub 1 f30873 (`hsw` 1); sub 101..299 f30981..31542: z −40 → −27 (toward 0, stops at the wall); hover f31681 |
| 30941 / 31460 | Griff fan (state 3) / dash (state 2) |
| **31068** | **sword hit on the Arm for 0**: the swing that took Griff 960 → 912 also hit the Arm (y 2.0, sweeping): `H+0xb5c` −1 → 323, HP 660 unchanged |
| **31073** | **phase 1** (`gph` 1; 912 < 924 = 0.7·1320) |
| 31470 | pause: Baked Yam SP 1 → 46 |
| **31889** | **Arm sweep 2**: sub 1 f32102 (`hsw` 2), sub 101 f32202 → z −53 by f32405 (toward −80); hover f32540 |
| **32155** | **swarm (phase 1)** at `gpc` 240: 48 bats f32226..32273, state 0 f32284; 46 expire f32368..32372, **2 stay** (the 0-flag bats) |
| 32446 / **32576** | fan; **eruption hit 206 → 131 (75)** (ftol(40·3.8/2 − 41/4) = 66, +0..13): the search's goal frame sat among 5 rings of age 114 |
| 32589 | dash; 32613..33029 pause: Watermelon +64 (→ 195), Taiyaki SP 3 → 32 |
| **33085** | **phase 2** (`gph` 2, 382 < 396 = 0.3·1320): `gb54` 0 → 1 |
| **33123** | **detonate** (state 4, `gpc` 540 = the phase-2 threshold; phase 1's 660 never came): the 2 kept bats → 0 at f33124, no damage |
| 33146 | hit in phase 2 during state 4: 382 → 320, **`gstag` stays 0**, state 4 not cancelled |
| **33157** | **Arm sweep 3** |
| **33364 / 33371 / 33376** | **phase-2 slash**: state 1; **3 records 23** at t 7; hit at t 12: **195 → 145 (50)** (ftol(35·3.8/2 − 68/4) = 49, +0..9) |
| **33467** | **swarm (phase 2)** at `gpc` 120, side 2 (pz −37.95 ≥ −40): Griff springs to z −50; 48 bats f33538..; state 0 f33596; **5 stay** (f33684) — `+0x9fc` 0, `+0xb08` 0 (hittable), ages > 600 |
| 33792 | Arm hover after sweep 3 |
| **33992 → 34152** | **Arm slam** (count 4): state 1; rises to y > 15 (sub 1, f34122, 130 ticks); falls (`ha00` 0); **record 14 at y ≈ 1.1 (f34149, sub 2)**; on the floor f34152 (sub 3); player 12.4 away, no damage |
| 34322 / 34501 | damage chunks: 320 → 175 → 56; 2 kept bats killed by the swings (f34328, f34431); Arm sweep 5 (count 5) at ≈f34501 |
| **34608** | **detonate (phase 2)**: 3 bats → 0, no damage |
| **34659** | **Griff dies** (HP 0, `+0x428` 2) |
| **34763** | Griff death tick 60: **the Arm goes dying** → **`estage` 4 the same tick, bank+0x2bce7 = 1 (0xaf39 0x00010101 → 0x01010101)**; Arm freed f34764; Griff stays dying (death timer 359 at f35062) |
| 35062 | `estage` 5 (stage-4 tick 180 + fade); party (−40, −45) |
| **35092..36934** | **event 7/8** (`dlg` 1), lines below |
| **36949** | stage-5 `etick` 46: **bank+0x2bcf8 = 1 (0xaf3e 0 → 1)**, `floor` 0 |
| **36965** | **stage record 18 → 19** (the "stage type 20" cutscene stage; `loading` 1), `estage` 0, party (−40, −40) |
| **37166..38228** | **event 7/14** (`dlg` 1, `lock` 0) |
| 38328 | **bank+0x2bcf8 cleared (0xaf3e → 0)** |
| 38331 | scene 9: "CLEAR!" screen (picture `runs/obs-50f/view/f038391.png`) |
| 38499 | results / bag (15/20) → **town map**, `stime` 1 → 2, `wms` 1121211 |
| 38529..38892 | pause → Save (`pm_sel` 4) → slot 020 → "Overwriting file. Are you sure?" → Yes; `rd endsave` → `save:3cf6ae8d5fac` |

Event 7/8 (f35092): "Uwagh! / Griff... / HeuWHA?! / No!... Run, quickly! Go! / But!... / The Archdevil is going berserk! / At this rate, I
won't be able to control him at all! / If you want to live... FLEE! NOW! / You have to come with us, Griff! / ... / You... are a unique girl,
Recette Lemongrass. / Forgive me, but I must stay! I'm going to re-seal everything left here! / WHAT?! / Don't misunderstand me! I simply have
no use for a power I cannot control. / That's all! I still intend to bring power to demonkind! / Just... not this way. / You, fairy... no.
Tear! / I leave the rest in your hands! Do you understand? / I do! / Wait, Griff! / Now hurry! GO! / Wait! / Remember - you promised you'd come
to the store! / ... Hm. / Come, Recette! We must hurry!"

Event 7/14 (f37166, stage 19): "... Griff... / Hey. Tear? / Do you... think I had the right to oppose what Griff was doing? / You were rather
blunt with him. / Aww, I knew it. / However. / I do not believe he was seriously attempting to kill us at any point. / He would have been far
more... brutal... if he truly wanted us dead. / And yet... he was not. / I can not help but think that he wanted us to stop him, on some
level. / Tear... / Let's go back to the store, okay? / All we can do now is work as hard as we can! / Indeed. Let us go home."

Damage on 50F: Louie (L42, ATK 61) sword on Griff `ftol(61/2 − 30/4)` = 23; most chunks were spins from the side / back. Taken: fan
eruption 75, phase-2 slash 50 (both search goal-frame traps). Food: Baked Yam, Watermelon, Taiyaki. Griff 1320 → 0 in f30452..34659
(4,207 frames, 14 damage searches of ≤ 150 s, `-j 4`).

## Tool gaps

1. **`rd scan` worked** (≈40 scans): from the live session's checkpoints it is instant (0.2–0.5 s); otherwise a fresh replay from boot
   (8–16 s for 16k–35k frames). It sometimes chose the fresh replay although a checkpoint ≤ F existed (e.g. `burst0` at the scan's F, after
   a search re-anchor). `--changes` on wide rows needs awk post-filtering to be readable (no "changes in column K only").
2. `rd play --changes` does not exist (only `scan` has it) — the error is easy to hit.
3. **Goal-frame traps** remain the main damage source: 5 of 6 hits taken this run were committed before the applied goal frame (a knight
   lunge, two type-16 / type-20 hits, the fan rings, the phase-2 slash). A search whose start state has an unavoidable hit reports "one cell"
   after its full time limit (70–165 s each). A goal tail (`--goal-hold N`: the goal and the avoid must hold N ticks later) would fix it;
   a cheap pre-check "is a hit unavoidable from here" would save the minutes.
4. `rd undo` after a failed search (nothing applied) stayed on the same frame twice — the failed search seems to occupy an undo slot.
5. The 45F knight count needs `actors(..)`; in `--avoid` it must be narrowed (`actors(COND, 1, 8)`); the error only appears when the search
   starts (Python traceback "max() iterable argument is empty" when all workers failed, the real message only with `-j 1`).
6. No named state for the pause Items list cursor / item under it: the description text at y 368 was the only way to know the row.
7. No name for "records of type K": `sum(s32(..) == K, ...)` over 40–48 slots, defined per run (`r14 r22 r23 r81`).
