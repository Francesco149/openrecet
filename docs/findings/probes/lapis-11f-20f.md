# Probe: lapis-11f-20f

- **Run:** `lapis-20f` · **start:** `save:40a0058f2b53` (end save of `lapis-10f`: shop, day dword 21 = HUD Day 22, `stime` 0) · no task file
  (goal: Lapis Ruins 11F → 20F in one trip, the 15F ahriman trio, the 20F Ohm trio, back, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`); legs `rd search -j 2` `--hold 30`; fights `-j 3`, every damage search `--hold 30`
- **Host:** every session on the Windows box (`win`, `RHOST_WIN=win-wifi`); verify on `win`.
- **Movie:** 30,864 frames (one trip 11F → 20F, door → town map, Save). **Not filed.**
- **Verdict (2026-11-12):** `rd verify lapis-20f` (headless, win):
  ```
  fresh headless replay: frame 30864 rng d392e315 sem 54a012c0ae392b74 dev 0791851aa0696678  (unskipped retail)  [win]
  live session:          frame 30864 rng d392e315 sem 54a012c0ae392b74 dev 0791851aa0696678  (skips 414ee2: dev not compared)  [win]
  MATCH
  ```
- **End save:** `save:303f7bcf95ea` (slot 020 overwritten from the town map's pause menu at f≈30700, day dword 21 = HUD Day 22, `stime` 3;
  TIME 3:20:09 → 3:28:37 / 3:28:39 — a second A re-opened "Overwriting file" and saved again).
- **Cards:** `docs/findings/bosses/boss-ahriman.md` §8 "Lapis 15F trio", `docs/findings/bosses/ohm.md` §8 "Lapis 20F trio".

## Before the dungeon

- Load `.*60 A .*30 A` → shop f137 (Recette at (−0.3, 9.35)). The pad did nothing for ~450 ticks (no text, no movement); a search with goal
  `scene == 8` found the door (f776; path starts after ~f600).
- Map `wms` 1121011, `stime` 0, `L` → Market (`wm_cur` 1). **Market Food: Cherry, Watermelon, Beef Bowl Out Of Stock**; Medicines: Ferromin,
  Ferromin G Out Of Stock. Bought Baked Yam ×3 (4,200, all), Candy Apple ×4 (4,480, all), Apple ×4 (840): 405,970 → 396,450. **No HP food
  better than Apple (15) / Walnut Bread (10) on sale** — the Watermelon plan was impossible on this lineage.
  - Pitfall: `rd tap A --n 12` on the map entered the Market and bought Treeseed Necklaces (undone). Market buy box: `A` on the item, `U`
    raises the count, `A` = Yes.
- Guild still open after the Market at `stime` 0 (`D R U` → `wm_cur` 6). One `A` = Louie hired (L42) → pack screen directly.
  Pack screen facts (corrections to `lapis-1f-10f`): the help scroll needs one `A`; **`L`/`R` on the slot list pages the slots (5 per page),
  not the category**; `A` on a slot gives the category list the focus, there `L`/`R` cycle the category; after a pick the focus returns to
  the slot list on the same slot, the category stays (it jumped to the next one only when the picked category emptied: Armor → Bracelets
  → Helms). The item cursor stays on its row index.
  Packed Iron Armor, Spiked Armband, Cherry ×3, Candy Apple ×4, Baked Yam ×3, Apple ×8 (20/20; Taiyaki left in storage).
- Depart → plate cursor already on Lapis Ruins → rows: **cursor on "Floor 11～15"** (picture `view/f002962.png` after `D D` — so it opened on
  "1～5"); best times 1～5 00:04:58, 6～10 00:02:51. Depart f2979 (fee 4,150: 396,450 → 392,300; `stime` 0 → 1).
- Equip box f≈3284 → `A`: ATK 61, DEF 68, MATK 22, MDEF 42, HP 206 / 206, SP 46. Control f3315. No first-entry event on 11F.

## Per floor

Level `floor/5 + 25`: L27 (11F–15F), L28 (16F–20F). Enemy lists = types in the actor table at entry (`type × count (HP)`).
Route = BFS of the maze grid + one scored search per 40-unit cell (`--hold 30`, `-j 2`).

| floor (`floor`) | frames | banner | enemies | grid, start → exit, legs | HP |
|---|---|---|---|---|---|
| 11F (10) | 2979 → 4780 | "Defense power doubled for everyone!" | 0×4 (96), 1, 6×2, 10 (32), 13×3 (64), 15, 16×6 (128), 17×2, 22×2, 35×2, 37×2, 44×3 (77), 50×3, 52 | 7 × 7, (2, 3) → (5, 3), 3 legs | 206 |
| 12F (11) | 4780 → 7548 | "Movement speed increased for everyone!" | 0×2, 1×3, 6×4, 10×4, 13×7, 15×2, 16×10, 17×2, 22×2, 35×2, 37×4, 44×2, 50×4, 52×2 | 9 × 9, (1, 5) → (7, 3), 8 legs | 206 |
| 13F (12) | 7548 → 9047 | **"Consumables now half as effective!"** | 0×2, 1×3, 6×5, 10×2, 13×7, 15×2, 16×9, 17×3, 22×5, 35×3, 37×2, 44×4, 50×5, 52×4 | 9 × 9, (5, 4) → (7, 3), 3 legs | 206 |
| 14F (13) | 9047 → 10549 | "Enemy defense power doubled!" | 0×4, 1×5, 6×2, 10×3, 13×4, 15×2, 16×8, 17×4, 22×3, 35×2, 37×4, 44×4, 50×2, 52×2 | 9 × 9, (3, 5) → (6, 5), 3 legs | 206 |
| **15F (14)** | 10549 → 14756 | none | **54 ×3 (640 each)** + bats 6 / 7 / 8 / 9 | boss floor, below | 206 → 145 → 87 → (food) 155 → 95 |
| 16F (15) | 14756 → 17921 | **"Will-o'-Wisps will appear soon. Enjoy!"** — no type 34 (or any new type) appeared in 2,770 ticks | 0×2 (99), 1×3 (132), **2** (99), **4×2 (231)**, 6, **8×2 (119)**, 10×2 (33), **11 (33)**, 13×3, 15×3, 16×6, 17×2, **18×6 (99)**, 22×2, 35, 37, 44 (79), 50×3, 52×2 | 9 × 9, (1, 2) → (2, 7), 6 legs | 95 |
| 17F (16) | 17921 → 19032 | "SP consumption halved!" | 0×2, 1×3, 2×3, 4×2, 6, 8×3, 10×2, 11×2, 13×4, 15×7, 16×4, 17, 18×2, 22, 35×4, 37×2, 44, 50, 52 | 9 × 9, (5, 4) → (6, 5), 2 legs | 95 |
| 18F (17) | 19032 → 21956 | none | 0×2, 1×2, 2×2, 4×2, 6×3, 8×2, 10×2, 11×5, 13×4, 15×9, 16×7, 17×3, 18×3, 22, 35×3, 37, 44×2, 50×2, 52×2 | 9 × 9, (4, 1) → (3, 6), 8 legs | 95 |
| 19F (18) | 21956 → 24115 | "Adventurer movement speed increased!" | 0, 1×3, 2, 4, 6×2, 8×3, 10, 11, 13×6, 15×2, 16×5, 17×2, 18×2, 22, 35, 37, 44×4, 50, 52×3 | 9 × 9, (5, 5) → (4, 2), 4 legs (one leg failed, the next leg covered it) | 95 → (food) 190 |
| **20F (19)** | 24115 → 30346 (town map) | none | **67 ×3 (990 each)** + bees type 10 (33) | boss floor, below | 190 → 146 → 99 → 53 → (food) 103 flat |

Floor change: `{dpad(ex - px, ez - pz)}*200 --until "portal > 0"` (4–9 ticks from the leg's end), `rd tap A --gap 10 --until "floor == N"`:
38 ticks every time. Object 0 was the exit portal on 11F–19F. 16F's list switches to the dungeon-3 mix (slimes 2 / 4, bats 8, bee 11, roper
18 join).

Leg hazards met: 16F (−80, −240) and 18F (−200, −80) host roper clusters (type 17 / 18, 3–4 within 2 units). 16F: a roper hit (23, undone)
and a search with no route until roper idx 39 died. 18F: a leg's `A .*7` action killed a bomb (type 37, idx 57) amid the ropers; its
explosion left a state no action survived (`found: false`, 1 cell × 6 searches; idle → hit 95 → 78 next tick). Undone; legs re-run
without the sword action (`legna`). Lesson: drop `A` from walking actions near bombs.

## New enemy types (not killed in any earlier `docs/findings/probes/*.md`)

Stats at `0x5c23f0 + type*0x68` (+0x2c HP, EXP, AT, DF, MA, MD; rf[0..3] at +0x44; +0x20 flags):

| type | HP | EXP | AT | DF | MA | MD | rf | flags | kill |
|---|---|---|---|---|---|---|---|---|---|
| 7 bat (ahriman's) | 17 | 14 | 20 | 7 | 17 | 15 | 0.8, 1.5, 4, 2 | 0 | 15F (kept branch): spins (`p_act` 4) 3 → 2 f13831, 2 → 1 f13984, 1 → 0 f14361 (the spin that killed h0) |
| 8 bat (ahriman's) | 18 | 18 | 20 | 10 | 20 | 20 | 0.8, 1.5, 4, 2 | 0 | 15F: 2 → 1 at f11554 and f12711, 1 → 0 f13984 (spins); no bat shot hit the player all fight |
| 18 roper | 15 | 15 | 18 | 4 | 0 | 0 | 0.8, 1.5, 2, 2 | 0 | 16F idx 40 (99 HP, L28): sword 99 → 23 (76, labels not read) f17258, → 0 f17316, freed f17322; idx 39: 99 → 0 f17496 (one spin), freed f17506 |
| 54 boss ahriman | 100 | 30 | 20 | 20 | 35 | 20 | 0.9, 1.5, 2, 2 | 0 | card (killed before: obsidian 15F) |
| 67 Ohm | 150 | 25 | 30 | 25 | 20 | 25 | 1.3, 4, 3, 4.5 | 1 | card (killed before: obsidian 40F) |
| 10 bee | 5 | 10 | 17 | 4 | 0 | 0 | 0.4, 1.5, 4, 4 | 0 | 20F, by spins (killed before) |

Bat kills by kind on 15F (live counts, `--changes` scans f10790..13760 and f13760..14525 of the kept movie): type 9 3 → 2 → 1
(f11543 / 11548), 1 → 0 (f11921); type 8 above; type 6 4 → 3 (f13984), 4 → 3 (f14031); type 7 above. Not killed here (killed before):
0, 1, 2, 4, 9 (also killed here), 10, 11, 13, 15, 16, 17, 22, 35, 37, 44, 50, 52.
**Type 34 wisp: not met** (16F banner announced it; 0 actors of type 34 by f17877).

## Story / events

- **None.** `dlg` 0 for the whole movie (`rd scan --watch dlg` f1..30864): no event on 11F–20F, the boss floors, the door, or the return.
- Results bag on return: 8/20 — Iron Armor, Spiked Armband, Candy Apple ×2, Baked Yam, Apple (+ the two adventurer's items). No loot kept.
- Trip departed at `stime` 1 and returned at **`stime` 3** (town map, Day 22 still; the map says "If you return now no time will pass").

## 15F — three boss ahrimans (card `boss-ahriman.md` §8)

Names (run `vars.json`): `hNc hNhold hNn hNoct` = actor N +0xa5c / a60 / a68 / +0x18; `hNal hNlat` (the obsidian definitions on actor N);
`hNdist = hypot(hNlat, max(0, -hNal, hNal - 19))` (distance to the 20-probe strip); `bats`; `aNdt` = +0xaf0; `slowmo` = 0x438becc.

| frame | what |
|---|---|
| 10549 | on 15F: 54 ×3 at (−50, 15, −38) / (−40, 15, −34) / (−30, 15, −38), state 8, HP 640 each (L27), `gate` 3; party (−40, −45) |
| 10549..10789 | descent in lockstep, y 12.8 at t 40 … 0.3 at t 240; `lock` 0; state 0 + `lock` −1 at f10790 |
| 10792 | holds 36 / 47 / 5 (actor 0 / 1 / 2): first beams at c 336 / 347 / 305 — all three overlap |
| 10819..10879 | bats: 3 at once (one per boss, same tick), then every 30: 8 live by f10879 = **the shared cap 8** (`h0n h1n h2n` 3 / 3 / 2) |
| **11194** | **h2 beam hit** (h2 `a2t` 99, `h2al` 5.86 / `h2lat` 1.29; h0 `a0t` 68, h1 `a1t` 57 = not live): 206 → 145 (**61**), `p_hurt` 30, `p_inv` 30 |
| 11351..12346 | damage searches: one spin hit all three (640 → 576 / 577 / 558, f≈11400, counter); SP 46 → 1; sum 1711 → 709 |
| 13357 | all three in state 1 (`a0t` 58, `a1t` / `a2t` 69); HP 145 |
| **13391** | **h1 beam hit** (`a1t` 99, `h1lat` −3.05; h2 `a2t` 99 at `h2lat` −4.81 = outside, h0 not live): 145 → 87 (**58**) |
| 13391..13759 | pause: Cherry +31, Cherry +37, Candy Apple → SP 41 (HP 155) |
| 13829 (undone) | an "h0" hit (155 → 95) with h2's strip also covering the player (`h2dist` 4.26): not attributable — undone, redone below |
| 13895 | all three beams outlived (survival search); 9 / 97 / 43 HP |
| **14031 / 14036** | **h1 dies first** (h0, h2 alive): tick 1 f14036 **`p_inv` 60**, slow-mo 60; tick 60 ≈f14139 bats untouched (8); freed f14243 |
| **14161 / 14166** | **h2 dies second** (h0 alive): tick 1 **`p_inv` 60**; tick 60 f14265 bats untouched; freed f14329 |
| **14266** | **h0 beam hit**, only h0 alive (`a0t` 132, `h0dist` 1.0): 155 → 95 (**60**) |
| **14361 / 14366** | **h0 dies last**: tick 1 **`p_inv` 1200**, slow-mo 60; **tick 60 f14465 / 14466: bats 7 → 0**; freed f14525 = `estage` 1, `gate` 0 |
| 14718 | (−35.9, −40.4): "Go to the next floor?" → 16F f14756 |

## 20F — three Ohms (card `ohm.md` §8)

Names: `oNbt oNbn` (+0xa64 / a68), `oNdisc` = distance to the point 2 ahead along the yaw, `oNdt` (+0xaf0), `bees`.

| frame | what |
|---|---|
| 24115 | on 20F: 67 ×3 at (−50, −38) / (−40, −34) / (−30, −38), state 8, HP 990 each (L28) |
| 24355 | all three state 0 after 240 intro ticks; `lock` −1 |
| **24470** | **Ohm 1 charge hit** (`a1t` 70 = record age 0, `o1disc` 5.40): 190 → 146 (**44**); Ohm 0 walking, Ohm 2 charge t 17 (wind-up) |
| **24578** | **Ohm 2 charge hit** (`a2t` 121, `o2disc` 5.49; Ohm 1 t 174 = recovery): 146 → 99 (**47**) |
| 24578 | first bees: 3 at once (one per Ohm, same tick, `bn` 1 / 1 / 1) |
| **24749** | **Ohm 0 charge hit** (`a0t` 70, `o0disc` 5.14; Ohm 1 t 97 at `o1disc` 7.04 = out of reach): 99 → 53 (**46**); **bees 8 = shared cap** (first reached f24723) |
| 24749..25239 | pause: Apple ×3 (+16 / +16 / +18) → 103 |
| 25410..25548 (undone) | damage searches applied best cells while a charge was committed: Ohm 0 at `o0disc` 0.56, player wedged between bodies (`U*60` moved 0.7) — unsurvivable from f25457 (dodge searches 64–65 cells) |
| 25630..29554 | survive-and-damage searches: goal "≥ 200 ticks later, no charge record live, sum ≤ cur − 110..120", `--hold 30` (230 frames each, all found). Spins did the damage (SP via Candy Apple f27510, Baked Yam f29324) |
| **28163 / 28168** | **Ohm 2 dies first**: tick 1 **`p_inv` 1200** (both others alive); tick 60 f28267: **no sweep** (bees 4); freed f28338 |
| **28601 / 28606** | **Ohm 0 dies second**: tick 1 **`p_inv` 1200**; tick 60 f28709 no sweep (bees 5 → 4 = a kill); freed f28784 |
| **29483 / 29488** | **Ohm 1 dies last**: tick 1 `p_inv` 1200; **tick 61 f29588: bees 1 → 0** (sweep); `estage` 1, `gate` 0 at f29658 |
| 30153 | return door: "Return to town now?" at (−44.27, −40.44) facing +z (`p_oct` 0) → town map f30346 |

## Food

Cherry ×3 (+31 / +37 / (eaten on 19F) +31), Apple ×7 (+15 / +18 / +15 / +16 / +16 / +16 / +18), Candy Apple ×2 (SP), Baked Yam ×2 (SP).
Left: Candy Apple ×2, Baked Yam, Apple. 13F's "Consumables now half as effective!" was not tested (no food eaten there).

## Tool gaps

1. `rd eval R "a + b"` prints `a + b = N`: a script taking `$3` got `+` (zsh/awk), so `$((cur-150))` became −150 and three damage searches
   ran with an impossible goal; `--apply-best` applied their best cells — two of which were doomed (no hold on a best cell). A `--value`
   / bare-number eval output would avoid it.
2. `--apply-best` keeps a best cell that the `--hold` never checked: after it the next search can be doomed from the start. A flag to
   apply only hold-verified cells would help.
3. The pause-menu Items list has no named cursor; Status is entry 0 (`A` there opens the controls page). Items = `~ .*30 D .*15 A`.
4. ESC on the town map right after a dungeon return is ignored for ≥ 60 ticks (no hint); `.*60 ~` worked.
5. 18F/16F: `found: false, 1 cell` on a leg means a doomed state (bomb explosion / roper hit) — the leg helper could say "the start state
   is doomed" (every action hits the avoid within N ticks).
