# Probe: bossrush-jade-amber

- **Run:** `bossrush-12` · **start (CRAFTED lineage):** `save:1049877e95a4` = `crystal-21f-30f`'s end save (shop, day dword 34 = HUD
  Day 35 morning, `stime` 0, Louie L47 ATK 65 / DEF 85, slot 020, 40,110 pix). Upstream: `2b9e8df3acad` ← … ← `b4766a99d74b`
  (`lapis-51f-60f`'s end save with bank `+0x2bcf7` cleared by `tools/craft_pix_save.py`). Amber Garden's Boss Rush row is open only
  because the save carries crafted d2 dwords (0xb103 = 29, best time 0xb138 = 1). No task file.
- **Goal:** the Jade Way (d1) Boss Rush (3 floors) and, with a period left, the Amber Garden (d2) Boss Rush (5 floors), both on day
  dword 34 (the 500,000 payment on the night of dword 35 stays out of reach), day end → dword 35 morning → Save.
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`). Every boss by `rd search` (`-j 2`, `--hold 30`, `--apply-best`, score
  `-a0hp`, HP and relative position in the cell, aimed walk / swing / retreat / strafe / spin + 8 directed swings as actions); the crab
  by alternating flip / damage searches; Tielle's block phases by position searches (goal `estage == 6`).
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`, `RD_HOST=win`). One `rd look` (f677) ran under wine on
  maximus before `RHOST_WIN` was exported in that shell (a picture replay only; nothing of the movie).
- **Movie:** 28,957 frames. Filed as `tests/scenarios/bossrush-jade-amber/trace.jsonl`. **No retail golden** (next: `rhost.py golden
  bossrush-jade-amber`, `rhost.py triage bossrush-jade-amber`). Not triaged.
- **Verdict (2026-11-14):** `rd verify bossrush-12` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 28957 rng f1929dcd sem d85dd2d3f9fd3d46 dev d6774b6cc6a54405  (unskipped retail)  [win]
  live session:          frame 28957 rng f1929dcd sem d85dd2d3f9fd3d46 dev d6774b6cc6a54405  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 28957 rng f1929dcd scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:2e4388a2cfe2` (slot 020 from the shop's pause menu at f28957: day dword 35 = HUD Day 36, `stime` 0, 30,810 pix,
  SCORE 9227 → 9603, TIME 5:18:45 → 5:26:44). **The 500,000 payment falls on THIS day's night and cannot be paid (30,810 pix):
  the next leg from this save ends in the game over / ending branch unless it changes the money.** The morning played event
  sid 1018 "Today is the final collection day." (tapped through, not skipped).

## Before the rush (Jade)

| frame | what |
|---|---|
| 172 | shop, HUD Day 35 (`.*60 A .*30 A .*80`), control at once |
| 461 / 525 | door by search → town map; `U U R` → `wm_cur` 6 → guild (scene 11) |
| 585..2320 | Louie (cursor restored on him) → bag: Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, Taiyaki ×7, Beef Bowl ×2, Watermelon ×2 (15/20). **Bag menu:** the item list opens on the category column's last category with the cursor on row 1 the first time; **changing category (`L`/`R` in the list) resets the cursor to row 0 "Nothing"** (an `A` there packs nothing); `R` in the SLOT column pages slots by 6. Categories in order: Daggers, staves, polearms, Clothes, Robes, Light armor, Armor, Bracelets, Helmets, Boots, Treasures, Materials, Cards, Food |
| 2381 | Depart → dungeon plates; **plate cursor `s32(0x6a49b50)`** (restored from bank 0xb37f = 5): `U` from 5 → 2, `L` 2 → 1 (Jade), `R` 1 → 2 |
| 2479 | Jade rows: 1～5 00:07:17, 6～10 00:15:49, 11～15 00:13:26, **Boss Rush 00:02:53** (= dword 0xb122 = 10387). **Row cursor `s32(0x6a49b54)`** (0 on entry) → `D ×3` = 3 |
| 2538 / 2544 | `A` → fee 4,650 (40,110 → 35,460), `stime` 0 → 1, `dungeon` 5, `0x438b4d0` 1, `floor` 30, `0x438b4d4` 0 at f2544; "Equip items in bag?" → Yes; pad free f2833 |

## Per floor

`H` = Louie's HP. Boss HP = table HP·(1 + 0.2·lv) at the rush level `f(5·floor − 145)` (all eight as the code says).

| floor | `0x438b4d4` | arrival → kill → `estage` 1 | boss (live slot 0) | table HP/AT/DF/MA/MD | how | H |
|---|---|---|---|---|---|---|
| **30** | 0 | 2544 → 3532 → 3696 | 23 crown slime, L4, **180**, power 5 | 100/30/8/0/10 | search 729 frames (blue slimes in the score); 5 spins; killed at power 2.32 | 228 → 228 (SP 51 → 1) |
| **31** | 1 | 3891 → 5988 → 6532 | 24 mushroom king, L7, **360** | 150/24/42/10/42 | 3 searches (no SP: sword only, ~9 per hit, one 60-HP heal state 5 seen); death 500 ticks | → 221 |
| **32** | 2 (last) | 6737 → 7603 → 7762 | 25 Charme, L9, **280** | 100/22/20/25/30 | one search, 655 frames | → 216 |
| **33** | 0 | 10219 → 11233 → 11397 | 54 boss ahriman, L12, **340** | 100/20/20/35/20 (bats 6: 15/20/4/15/10, 7: 17/20/7/17/15) | one search, 755 frames | 228 → 209 |
| **34** | 1 | 11640 → 12628 → 12803 | 67 Ohm, L14, **570** | 150/30/25/20/25 (bee 10: 5/17/4/0/0) | one search, 777 frames | → 180 |
| **35** | 2 | 13044 → 17535 → 17710 | 68 crab, L16, **714** (not 630) | **170**/30/30/20/30 (bombs 37..40: 15/16/16..22/18..24/10..15) | 6 searches: flip (bomb blast, `a0st` 6) → damage while flipped, ×3 flips; ate 2 Beef Bowls (+55 each) + 1 Taiyaki (+32 SP) in the pause menu at f15788 (H 126) | 180 → 126 → 228 → 216 |
| **36** | 3 | 17923 → 19865 → 20029 | 23 crown slime ×2 (slots 0 / 1), L17, **440** each | as 30 | 2 Taiyaki first; one search 1333 frames kills both | → 195 |
| **37** | 4 (last) | 20240 → 26330 → stage 10 f26738 | 27 Tielle, L19, **480** | (card) 100/26/30/10/30 | phase 0 search (reach 2.5) 948 frames; phase 1 block 9 1035 frames; walk to phase 2 (search, `pz` score: the diagonal is walled); Watermelon (+63) + Taiyaki at stage 5; phase 2 block 3 1196 frames; fight 714 frames | 195 → 150 (low 151 before the melon) |

- **Floor setup:** every floor `dungeon` 5, `floor` 30..37, `0x438b4d0` 1 / 2, party at (−40, −45) (37: (−40, −54) column), boss at
  (−40, −34) (36: (−45, −34) / (−35, −34); 37: (−40, −26)) in state 8; the 240-tick pad lock (`lock` 0) ends at arrival + 241 (37: no
  dialogues, `estage` 3 at arrival + 151, pad free in stage 4 at + 202). No banners. `noexit` (`0x9643128`) 1 on every floor until the
  LAST floor's clear (32: 0 at f7762; 37: 0 at stage 8, f26339); 1 again on the cleared non-last floors.
- **Floor → floor:** the clear raises `estage` 1; the portal (object 0, kind 8, at (−40, −40)) then rises — the pad is ignored for
  ~120 ticks after `estage` 1 (a walk there moved nothing). On it: "Go to the next floor?" → Yes → `0x438b4d4` + 1 at +~230 ticks,
  `floor` + 1 16 ticks later; HP / SP carry over. Food from the pause menu works on any floor (the fight freezes).
- **Last floor (32, mode 1):** objects 0..7 are scenery (kinds 2, 3, 0 ×4, 1 ×2) and **object 8 = kind 6 at (−40, 0, −40), shut**:
  walking into it stops 1.4 out, nothing happens; **a sword hit opens it** (state 0 → 1, f8152), then "Return to town now?"
  (`pret` 1). Same on 37 (Tielle's door at (−45, 65): shut, solid, walked around it; a swing from (−44.9, 63.3) opened it, prompt
  f26943 from the −z side). No next-floor circle on either last floor.
- **After the last floor:** "Return to town now?" → `A` → `0x438be98` 2 → "CAME BACK ALIVE!" (the ordinary bag screen, scene 9;
  15/20 after Jade, 8/20 after Amber) → `B` → town map. **No results screen, no reward, no rush drop** (bag unchanged; the
  Day 35 Daily Profits "Found" lists only Walnut Bread). **The row's best time is written on the first tick of the return prompt =
  the trip counter `s32(0x438b1e8)`**: Jade **4736** (1:18.9) into dword **0xb122** (was 10387 = the row's 00:02:53, so only a better
  time is kept — not tested with a worse one), Amber **13326** (3:42.1) into dword **0xb139** (was 0). Both = `0xb10b + 20·d + cap[d]`.
- Each rush is one guild trip: fee 4,650, `stime` + 1 on departure (Jade 0 → 1; return → 2 on the map), Louie back to full HP / SP
  for the second trip (Amber f10508: 228 / 51). Amber's return at `stime` 3 → 4: straight to the shop (evening), no map.

## The day end

| frame | what |
|---|---|
| 27172 | "CAME BACK ALIVE!" → `B` → shop, `stime` 4, "Thanks for all your help today, Tear!" |
| 27236..27642 | `A` taps → Daily Profits Day 35: Found Walnut Bread, Profits −9,300 (2 fees), Score 9227 → 9603 (+376) → day dword 35 |
| 27659 | shop morning (HUD Day 36), control |
| 27856..28541 | event sid 1018 (`dlg` 1): "Good morning, Recette." … "Today is the final collection day." … "Let's get to work!" (a `~` during it asks "Do you want to skip this event?"; rewound, tapped instead) |
| 28665..28957 | `~` → pause, `D` ×4 (`pm_sel` 4, `pm_type` 3) → `A` (slot cursor 020, `t_pcur` 19) → `A` "Overwriting file. Are you sure?" → `A` → saved; `rd endsave` → `save:2e4388a2cfe2` |

## Card corrections

- **boss-rush.md:** table HP of 25 Charme is 100 → rush f32 HP **280** (not 420); crab 68's table HP is **170** → f35 HP **714** (not 630);
  boss ahriman f33 HP 340 (= the card's struck-out fix). The last floor's return portal is a **shut door (kind 6) that needs a sword
  hit**, not a walk-on portal; on 32 it is object 8 (not object 0). No results / reward after the run; the best time = the trip
  counter at the return prompt into `0xb10b + 20·d + cap[d]`. The no-exit flag is 1 on floor 32 until its clear (checklist row OK).
- **crabs.md:** type 68 table row live = HP 170, AT 30, DF 30, MA 20, MD 30.
- **tielle.md:** the rush arm (FUN_0044db3c(1)) is now observed: no dialogues, stage 3 at arrival + 151, the three phases as in the
  story; the end door at (−45, 65) is shut until a sword hit (`doorst`), in the story card it opened by walking from −z.
- **Pause Items:** Taiyaki = +32 SP, Beef Bowl = +55 HP, Watermelon = +63 HP (at L47); usable on boss floors.

## Tool gaps

1. `rd look` / `strip` fall back to wine on maximus when `RHOST_WIN` is not exported in the calling shell (each Bash call is a fresh
   shell): one picture ran under wine. A per-run host default (from `session.json`) would avoid it.
2. A goal with `actors(T == X and U == 1)` over 64 slots exceeds the 598-token limit inside `search` (the duo loop silently did
   nothing for 7 iterations because my wrapper grepped the error away): narrow to `actors(.., 0, 7)`.
3. No named vars for the guild plate / row cursors: `0x6a49b50` (dungeon), `0x6a49b54` (row) would be worth adding to vars.json.
