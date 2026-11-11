# Probe: lapis-1f-10f

- **Run:** `lapis-10f` · **start:** `save:3cf6ae8d5fac` (end save of `obs-50f`: town map, day 20 `stime` 2; CRAFTED lineage, Lapis opened by
  crafted bytes) · no task file (goal: Lapis Ruins 1F → 10F in one trip, the 5F pumpkin-ghost pair, the 10F type-59 king, back, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`); legs `rd search -j 2`, fights `-j 3` / `-j 4`, every fight search `--hold 30`
  (one chunk `--hold 70`)
- **Host:** sessions on the Windows box (`win`) until ≈f21000, then on **bunker** (`rd ls` / search lines `@bunker`); verify on the session's host.
- **Movie:** 40,626 frames (one trip 1F → 10F, door → town; the day ended → morning → Save in the house). **Not filed.**
- **Verdict (2026-11-12):** `rd verify lapis-10f` (headless, bunker):
  ```
  fresh headless replay: frame 40626 rng dd99ff7b sem 95efe149f8ceead9 dev 8c6e14089deb05de  (unskipped retail)  [bunker]
  live session:          frame 40626 rng dd99ff7b sem 95efe149f8ceead9 dev 8c6e14089deb05de  (skips 414ee2: dev not compared)  [bunker]
  MATCH
  ```
- **End save:** `save:40a0058f2b53` (slot 020 overwritten from the house's pause menu at f40564, day dword 21 = HUD Day 22, `stime` 0;
  TIME 3:08:57 → 3:20:09).
- **Cards:** `docs/findings/bosses/pumpkin-ghost.md` §10 "Lapis 5F pair", `docs/findings/bosses/mushroom-king.md` §8 "type 59".

## Before the dungeon (how Lapis was reached)

- Load `.*60 A .*30 A .*120` → town map f212 (gold 410,120, `wms` 1121211, `stime` 2). Save bytes read live: 0xaf3c = **0x01010000**
  (bank+0x2bcf2 / f3 = 1: d4 open), 0xb105 = 99 (all Lapis rows), 0xb16e = 1, **0xaf3d = 0x01000101** (bank+0x2bcf4 = 1: event 10/4
  OFF; 0x2bcf5 = 1, 0x2bcf7 = 1), 0xaf3f = 0x01000000 (0x2bcff = 1), 0xaf40 = 0xaf44 = 0xaf45 = 0, mode dword 0xb759 = 0, 0xaf15 = 0.
  Louie level dword 41 (L42).
- `R .*6 U .*6` → `wm_cur` 6, `A` → guild f243 (open at `stime` 2). Louie on the cursor; hire `A`; the pack screen needs the 2nd `A`.
- Storage: Clothes (Tough Clothes), Robes (Thick Robe), Breastplates (Shell Chestpiece), Armor (Iron Armor), Bracelets (Spiked Armband),
  Helms (Warrior's Helm), Shoes (Leather Boots), Treasures, Ingredients, Food (Apple ×4, Cherry ×6, Watermelon, Beef Bowl, Taiyaki ×2,
  Candy Apple); no Weapons category. Packed Iron Armor, Spiked Armband + all 15 food (17/20). Pack-screen facts: `R` cycles the category
  while the slot list has focus; after a pick the focus returns to the slot list on the same slot and the category jumps to the next one.
- Depart → **the dungeon picker lists 5 plates: Hall of Trials, Jade Way, Amber Garden, Obsidian Tower, Lapis Ruins** (`rd text` f2912).
  `R` from Obsidian → Lapis, `A` → rows "Floor 1～5" … (20 rows, best times "--:--:--"); row cursor on "1～5" (picture
  `runs/lapis-10f/view/f002969.png`) although bank dword 0xb380 read 8. Depart f2986 (fee 4,150: gold 410,120 → 405,970; `stime` 2 → 3).
- Dungeon entry: `stage` 11, `dungeon` 4, `floor` 0; `lock` −1 one tick (f3001), equip box f3186..3206 ("Equip items in bag?") → `A`:
  **ATK 58 → 61, DEF 59 → 68, MDEF 34 → 42** (MATK 22), HP 206 / 206, SP 46 / 46. Control f3217.
- **No first-entry event on Lapis 1F** (no stage-20 cutscene, `dlg` 0 all floor).

## Per floor

Level `floor/5 + 25`: L25 (1F–5F), L26 (6F–10F). Enemy lists = types in the actor table at control (`type × count (max HP)`).
Route = BFS of the maze grid + one scored search per 40-unit cell (`-j 2`, 60 s): **every leg found on the first try**.

| floor (`floor`) | frames | banner | enemies | grid, start → exit, legs | HP |
|---|---|---|---|---|---|
| 1F (0) | 3217 → 8569 | none | 0, 1, 6, 10, 13, 15, 16, 17, 22, 35, 37, 44, 50, 52 (16 ×6+ HP 120; 52 120; 22 / 37 / 15 / 0 / 50 / 17 / 35 90; 13 60; 44 72; 10 30) | 7 × 7, (4, 3) → (5, 3) adjacent; kills took the detours, then 5 legs | 206 flat |
| 2F (1) | 8569 → 13718 | "Movement speed increased for everyone!" (to f8935) | 0×2, 1, 6, 10×2, 13×2, 15, 16×6, 17×2, **29** (90), 35×3, 37, 44, 50, 52 | 7 × 9, (3, 1) → (3, 6), 9 legs | 206 flat |
| 3F (2) | 13718 → 15301 | "Enemy defense power doubled!" (to f14052) | 0×2, 1, 6×3, 10×2, 13×2, 15×3, 16×5, 17×2, 22×2, 35×5, 37, 44×3, 50, 52 | 7 × 9, (3, 3) → (4, 5), 3 legs | 206 flat |
| 4F (3) | 15301 → 17431 | "Movement speed increased for everyone!" (to f15667) | 0, 1×2, 6, 10, 13×2, 15, 16×10, 17, 22, 35×2, 37, 44×2, 50, 52 | 9 × 7, (4, 1) → (3, 4), 6 legs | 206 flat |
| **5F (4)** | 17431 → 25622 | none | **55 ×2 (900 each)** + adds 52 / 53 / 15 | boss floor, below | 206 → 104 → (food) 205 → 163 → 102 → (food) 193 → 157 |
| 6F (5) | 25622 → 26065 | none | 0, 1, 6×4, 10, 13×3, 15, 16×5, 17, 22×2, 35, 37, 44, 50, 52×4 (L26: 93 / 124 / 62 / 31 / 74) | 7 × 7, (3, 3) → (4, 3), 1 leg | 157 flat |
| 7F (6) | 26065 → 27726 | "This level is full of traps. Enjoy!" (to f26409) | 0×2, 1, 6, 10×2, 13×2, 15, 16×4, 22, 35, 37, 44, 50, 52 | 7 × 7, (3, 3) → (5, 2), 3 legs; no trap hit | 157 flat |
| 8F (7) | 27726 → 29238 | none | 0, 1, 6×2, 10, 13×6, 15, 16×3, 17×2, 22×3, 35×3, 37, 44×2, 50, 52×2 | 9 × 7, (5, 2) → (3, 4), 4 legs | 157 flat |
| 9F (8) | 29238 → 31212 | "Adventurer movement speed increased!" (to f29598) | 0, 1, 6, 10, 13, 15, 16×3, 17×2, 35, 37, 44, 50×2, 52 | 7 × 9, (2, 3) → (4, 2), 3 legs | 157 → (food) 194 |
| **10F (9)** | 31212 → 38376 (door) | none | **59 (930)** + mushrooms 13 / 14 | boss floor, below | 194 flat |

Floor change: `{dpad(ex - px, ez - pz)}*200 --until "portal > 0"` (10–19 ticks from the leg's end), then `rd tap A --gap 10 --until
"floor == N"`: 38 ticks every time. Object 0 (`ex ez`) WAS the exit portal on every Lapis floor 1F–9F (unlike Obsidian 41F+).

## New enemy types (not killed in any earlier `docs/findings/probes/*.md`)

Stats at `0x5c23f0 + type*0x68` (+0x2c HP, EXP, AT, DF, MA, MD; rf[0..3] at +0x44; +0x20 flags):

| type | HP | EXP | AT | DF | MA | MD | rf | flags | kill |
|---|---|---|---|---|---|---|---|---|---|
| 15 pumpkin | 15 | 20 | 20 | 4 | 0 | 0 | 1.0, 1.5, 2, 2 | 0 | 1F idx 10: one spin 90 → 0 at f3700 (killed as adds before, never filed as a kill) |
| 22 rabbit | 15 | 5 | 16 | 8 | 0 | 0 | 0.8, 1.5, 2, 2 | 0 | 1F idx 8: 90 → 47 (43) f4482, killed f4621, freed f4627 |
| 13 red mushroom | 10 | 7 | 14 | 0 | 0 | 0 | 0.8, 1.5, 5, 2 | 0 | 1F idx 5: 60 → 0 in one sword hit f5123, freed f5129 |
| 37 bomb | 15 | 10 | 16 | 16 | 18 | 10 | 0.9, 1.5, 2, 2 | 0 | 1F idx 1: 90 → 47 (43) f6348 (its state 1), killed f6457 (`+0x428` 2, player 2.0 away), **freed 9 ticks later f6466 with `+0xaf0` = 240, no damage taken** (cf. type 39: fuse 360 → 540, 83 damage on a branch, obsidian-21f-30f) |
| 29 "lost girl" | 15 | 30 | 20 | 20 | 0 | 10 | 0.6, 1.5, 2, 2 | 0 | **not killable**: `+0xb08` = 1 throughout (2F idx 5, 90 HP). It is the scallion lady's encounter (below) |
| 59 king (boss) | 150 | 25 | 26 | 42 | 10 | 42 | 0.8, 4, 3, 4.5 | 1 | 10F, card |
| 60 (read only) | 150 | 25 | 28 | 52 | 10 | 52 | 0.8, 4, 3, 4.5 | 1 | not met |

Pumpkin ghost 55 and adds 52 / 53 / 15: `pumpkin-ghost.md`. Not killed on Lapis (already killed in earlier probes): 0, 1, 6, 10, 16, 17, 35,
44, 50, 52.

## Story / first-entry events Lapis played

- **2F, f9962: the "lost girl" encounter (type 29)**: walking within 2.5 of the type-29 actor (state 0 → 1, `+0xb08` 1) set `dlg` 1;
  ~2,000 ticks of `A`: "Mmm... / Ah... / ... Mhmm. / Uhhh... hi, again. / Oh, my. Hello, have we met somewhere previously? / Uh... yeah, in
  a way that's giving me a whole lot of deja vu... / Is that right! I'm sorry that I don't remember. / I take it you are again here by
  mistake while attempting to get home. / I'm not quite sure I... wait, am I... / ... I am somehow not surprised that you did not realize
  where you were. / This is a dungeon. Again. / Ah... Well, I was just heading home from the market with dinner. / See, I even have my
  scallions with me. / Wow, those look great! So perfectly green and tasty! / I must say, it was quite a good purchase... / This is not the
  time to be chewing on onions! / Merde, how are you not concerned about your situation? / I think something more than your sense of
  direction is off... / I think I'll have sukiyaki for dinner tonight. / Sukiyaki? I dunno what that is, but it sounds good! / Tear! Can we
  fix sukiyummy for dinner tonight? / Grain porridge. Ever and anon. / Aww... / Heehee... I enjoy your company, you two. I must get home,
  however. / Stay safe!" — `dlg` 0 at f11998. The actor stayed (state 1, untouchable) afterwards. No item, no gold change.
- 10F: **event 10/4 did not play** (bank+0x2bcf4 = 1 in this crafted lineage); `estage` went 0 → **2** directly when the king's slot freed.
- No other dialogue on 1F–10F, the door, or the return.

## 5F — two pumpkin ghosts (card `pumpkin-ghost.md` §10)

Names (run `vars.json`): `g0* / g1*` = actor 0 / 1 (`x y z hp st t clk hold fade b08 alpha use dt` at +0x3f0 / 3f4 / 3f8 / 42c / a54 / a58 /
a5c / a60 / a64 / b08 / 3dc / 428 / af0), `nadd` = live 52 / 53 / 15, `r90` = records of type 90 in table-B slots 0..63, `pinv`.

| frame | what |
|---|---|
| 17431 | on 5F: ghosts at (−45, 0, −34) / (−35, 0, −34), state 8, HP 900 / 900 (L25), `gate` 3 |
| 17451..17671 | descent in lockstep: y 13.05 at t 20 … 0.55 at t 220, `+0xb08` 1, `lock` 0; `s32(0x9643128)` 1 from event tick 31 |
| 17671 / 17672 | both state 0 (`clk` 1); `lock` −1 |
| 17741 / 17881 | spins 900 → 847 → 789 (53, 58) |
| **18009** | ghost 0 cast (t 20, n 0): **3 adds** |
| **18084** | ghost 1 cast (n 3): **3 adds → 6 = the cap (d4 floor < 10)**; slot 7 = type 53 with **`+0xae4` 0 (ghost variant 0)** |
| **18515 / 18550** | ghost 1 / ghost 0 cast at the cap → **record 90 ×2** (n 6) |
| 18567..18675 | both shots dodged: closest 0.85 (age 75, no hit); **freed at age 120** (f18645 / f18675) without a hit |
| 18398 | ghost-52 record 108: 206 → 182 (24 = ftol(20·3.5/2 − 42/4)) |
| 18687 | `pinv` 15 with no HP change: a guard (projectile deflected) |
| 19014..20279 | chunks on ghost 0; hits taken 24–27 each (avoid `hp < H − 30`) → HP 104 |
| 20279..20597 | pause: Watermelon +69, Cherry +32 → 205 |
| 21331 | hit (163) |
| **21397** | **ghost 0 dies** (HP 0 in state 0); hit-stop to f21401; **death tick 1 f21402: `pinv` 60** (ghost 1 alive), slow-mo 60 |
| 21501 | ghost 0 death tick 60: **adds stay (n 6)** |
| 21561 | ghost 0 freed (`+0xaf0` 120) |
| 21591..25204 | ghost 1: 900 → 0 (pause f23070..23232: Beef Bowl, Cherry → 193); one doomed chunk (`--hold 30` goal, a record-90 hit 54 ticks after) undone; `--hold 70` chunks; hit 36 at ≈f25038 accepted |
| **25204** | **ghost 1 dies** (state 1); **death tick 1 f25209: `pinv` 1200**, slow-mo 60 |
| **25308 / 25309** | death tick 60: **all 6 adds dying → gone** |
| 25368 | ghost 1 freed (tick 120): `estage` 1, `gate` 0, exit circle (−35, −40) |
| 25584 / 25622 | "Go to the next floor?" → 6F |

## 10F — the type-59 king (card `mushroom-king.md` §8)

Names: `khp kx kz kyaw kst kt ksub k50 k60 k64 k70 k74 kb28 kdt kuse kpow` (B+0x42c 3f0 3f8 420 a54 a58 a5c a50 a60 a64 a70 a74 b28 af0 428 ab8),
`kd krel`, `nmush` / `npurp` = `actors(T == 13 or 14 / T == 14, 1, 10)`.

| frame | what |
|---|---|
| 31212 | on 10F: king type 59 at (−40, 0, −34), state 8, **HP 930** (L26), `+0xab8` 1 on the entry tick, **2.0 from the next** |
| 31242..31452 | intro: `lock` 0, state 8 t 0..239; `s32(0x9643128)` 1 by event tick 60; state 0 at f31452, `lock` −1 f31453 |
| ≈32300..32434 | eat **red** (`k74` 2) at full HP → state 5 heal f32434 (HP stays 930) |
| 32722 | eat purple (`k74` 1, eat tick 70) → **state 4 at ≈f32772** (positioning missed most of it): one sword from behind f32890: 930 → 875 (55) |
| f32890..33478 | **heal: 875 → 930** (a red meal inside the meal search, state 5: max/6 = 155, capped at 930; frame not isolated) |
| 33528 / 33769 → **33940** | purple meal #3 → state 4: spins from behind (back + counter) **f33954 120, f33994 114, f34034 109** → 587; a 4th spin at kt ≈ 112 landed after the window: 2 |
| 34763 → 34830..34890 | purple meal → state 4 (positioned at kt 67): spins → **371** |
| 35164 | pause: Candy Apple / Taiyaki → SP 46 |
| ≈35720..35863 | purple meal → state 4 (kt 11): spins → **32** |
| **36028..36126** | **two swings (`ksub` 1)**: state 1 kt 1 f36028; record 14 live kt 25..38 (f36052..36065) and 56..69 (f36083..36096); **state 0 at kt 99 (f36126)** — 59's two-swing end = 99 (24: 114); player 16–21 away, no hit |
| 36840 | purple meal → state 4 kt 0, behind at 4.7 |
| **36852** | **king dies** (spin, 32 → 0, state 4 kt 12) |
| 36857 | death tick 1: slow-mo 60, `pinv` ≈1200 |
| **36956 / 36957** | death tick 60: 3 mushrooms → dying → gone |
| **37396 / 37397** | death tick 500: slot freed (`kuse` 0); **`estage` 0 → 2** (not 1), `gate` 3 → 0, `s32(0x9643128)` 0; event tick restarts |
| 37895..37965 | door at (−45, −40): blocked at x −43.46 walking −x; the door object (0x695f218, kind 6, state +0x268) goes 0 → 1 on contact; prompt "Return to town now?" at (−44.08, −39.86) |
| ≈38600..38797 | Yes → `stime` 3 → 4 (f38629): **the day ends** (departed at `stime` 2) → "Daily Profits" (Day 21, profits −8200) → Day 22 |
| 39094 | house, day dword 21, `stime` 0; news ticker until ≈f40270 (ESC ignored meanwhile) |
| 40280..40564 | pause → Save (`pm_sel` 4, `pm_type` 3) → slot 020 → "Overwriting file. Are you sure?" → Yes |

## Food

1 Watermelon +69, Beef Bowl ≈+57, Cherry ×3 +32 / +34 / +37, Candy Apple (SP → 46), Taiyaki (+30 SP); left in the bag: Taiyaki, Cherry ×3,
Apple ×4.

## `--hold` (the new option)

- Legs: 43 legs `--hold 30`, all found first try, 0 HP lost walking.
- Ghost pair: `--hold 30` chunks rejected 0..23 arrivals each and kept HP intact on most chunks; **one goal frame was still doomed**: a
  record-90 shot (tracks, lives 120 ticks) hit ≈54 ticks after the arrival — the next two searches found one cell / 7 cells (130 s / 150 s).
  Undo + `--hold 70` worked (11–48 rejected). A second doomed state (36 damage) was accepted.
- King: positioning searches have no hold need (the king is passive in states 2 / 4).

## Tool gaps

1. In zsh, `for t in "-120 -40"; leg.sh $t` does not split → the script got TX "-120 -40", TZ / H / TL shifted (time limit 2 s): a run of
   junk wandering applied with `--apply-best` (undone by `ck load`). `bash -c` fixed it (memory note `bash-tool-is-zsh`).
2. `rd search -j 4` on bunker twice ended with "the live session ended during this command: the game exited or crashed" and no result
   (positioning search at f34713, `--cell "(int(px), int(pz), kt//5, kst)"`); `-j 2` / `-j 3` with the same goal ran fine.
3. Pause Items cursor: no named state; after `~ D A` the list opens on the first item (an "Adventurer's Possession" row); `B` closes the list
   and then `D A` opens Status, not Items.
4. `actors(..)` names used inside `--cell` must be narrowed (`actors(C, 1, 10)`), as before.
5. A positioning goal with "kt ≤ 6" was unreachable (the player needs ~90 ticks to walk 16 units; the window starts 50 ticks after the meal's
   tick 70): no hint that the goal is time-infeasible; ran its full limit.
