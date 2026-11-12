# Probe: lapis-30f-story

- **Run:** `lapis-30s` · **start:** `save:709c83a9f531` (CRAFTED: `lapis-30f` end save `3c2a6b9c8b1d` with `bank+0x2bcf5` 1 → 0; town map,
  day dword 22, `stime` 3) · no task file (goal: Lapis 30F story exit + the town scene after it, back, Save)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`); legs `rd search -j 2 --hold 30`, fights `-j 3 --hold 30`.
- **Host:** every session on `win` (`RHOST_WIN=win-wifi`); verify on `win`.
- **Movie:** 18,386 frames. **Not filed.**
- **Verdict (2026-11-12):** `rd verify lapis-30s` (headless, win):
  ```
  fresh headless replay: frame 18386 rng 9e964e53 sem ac2701d2e41a7b23 dev dbe697600fab9f4c  (unskipped retail)  [win]
  live session:          frame 18386 rng 9e964e53 sem ac2701d2e41a7b23 dev dbe697600fab9f4c  (skips 414ee2: dev not compared)  [win]
  MATCH
  ```
- **End save:** `save:68c43a00466e` (slot 020 overwritten from the town map's pause menu at f18364; day dword 23 = HUD Day 24, `stime` 2,
  SCORE 6270; `bank+0x2bcf5` 1, `+0x2d5fe` 1).

## Before the dungeon

| frame | what |
|---|---|
| 137 | town map (`scene` 8), `wms` 1011200, `0x2bcf5` 0, `0x2bcfd` 0, `s32(0x438b1c0)` 8 (= `scene`) |
| 240 / 362 | Merchants' Guild (`R` from Home → `wm_cur` 3) in / `B` out |
| 410 | Home (`D` → 0): `stime` 3 → 4, Daily Profits "Day 23" (no weekly payment), score 6104 → 6270 |
| 865 | Day 24 morning, shop, `stime` 0. **Pad dead until ≈f1295** (430 ticks; a 90 s `-j 2` door search from f865 found "1 cell") |
| 1610 | door → town map, `wms` 1121011 |
| 1915 | map: `R` (→ 3) `U` (→ 6) = Adventurers' Guild |
| 2140 | Louie (L43) hired with one `A` → pack screen |
| 2140..3793 | pack: Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, slot 5 empty (an `A` on "Nothing"), Apple ×13 + Taiyaki + Candy Apple (slots 6..20) |
| 3834 | Lapis rows: cursor remembered on "21～25" (the 26～30 row shows a time 00:01:54); `D A` |
| 3867 | 26F, gold 304,090 → 299,840 (fee 4,250), `stime` 0 → 1. Equip box f4078 → `A`; banner "Consumables now half as effective!" |

Pack screen facts: in a slot, `R`/`L` change the category; after a pick the category jumps to the next non-empty one when the
current one emptied, the item cursor keeps its row index; a category change puts the item cursor on row 0 ("Nothing").

## 26F..29F (no damage kept; HP 210 / 210 throughout)

| floor | frames | banner | grid, start → exit |
|---|---|---|---|
| 26F (25) | 3867 → 6711 | "Consumables now half as effective!" | 9 × 11, (3, 2) → (3, 7); 7 cells |
| 27F (26) | 6711 → 10405 | "SP consumption halved!" | 9 × 9, (3, 5) → (7, 2). **A pig pack (4 × type 16, 140 HP) + boulders** caught me at f7396 (a leg's hold passed, the hit came later: every later leg search "1 cell"); rewound to f7121, one HP-sum-scored fight search killed the four (f7509, `B` spins) |
| 28F (27) | 10405 → 11692 | "Adventurer movement speed increased!" | 9 × 9, (5, 5) → (6, 2) |
| 29F (28) | 11692 → 13832 | "Defense power doubled for everyone!" | 9 × 9, (5, 4) → (3, 7) |

Mazes differ from `lapis-30f` (generated per entry). The exit cell centre was the portal on every floor; 38 ticks portal → next floor.

## 30F — bombs, then the story exit

| frame | `estage` / `etick` | what |
|---|---|---|
| 13832 | 0 / 0 | on 30F: 20 bombs (types 37..40); **`gate` 0** (lapis-30f read 3 with the flag set: `FUN_00450443` case 29 creates the two portal objects at setup only when `+0x2bcf5 == 1`); `0x2bcf5` 0, `0x2bcfe` 0 |
| 14073 | 0 / 241 | `lock` −1 (control) |
| 14073 → 14171 | | one search: `B .*12` ×5 at the start point → live 20 → 9 (9 lit corpses), HP intact |
| 14171 → 14299 | | second search: live 9 → 0 (more spins), HP intact |
| 14299 → 14543 | | survival search: corpses blow on their fuses, player away |
| **14544** | 0 → **1** / 1 | no actor left → stage 1; **`lock` 0** (control taken), no text |
| ≈14663 | 1 / 120 | fade starts (`FUN_004526f5(0, 120)`, per the code; no state name for the fade) |
| **14783** | 1 → (2) → **0** / 0 | in ONE tick: `0x2bcf5` 0 → **1**, `0x2bcfd` 0 → **1**, `s32(0x438b4dc)` 15 → **0**, `loading` 1, `stage` 15 → **0**, player (2, −3.24), `lock` −1. `scene` / `s32(0x438b1c0)` stay 1, `floor` / `0x438b4cc` stay 29, day 23 / `stime` 1 unchanged |
| 14784..15083 | | stage 0 = **Recette's shop** (picture), no text, `dlg` 0 for 300 ticks |
| **15084** | | `dlg` 1: the town scene (ESC = "Event Skip" offered) |
| 15222..16730 | | dialogue (below), 21 lines, `A` gap 12 |
| **16910** | | `dlg` 0; **`bank+0x2d5fe` 0 → 1** (dword 0x2d5fc byte 2) |
| **16990** | 0 / 0 | back on **30F** (`stage` 15, `loading` 1, player (−40, −45)); `0x2bcfd` 1 → **0**, `0x2bcfe` 0 → **1**, `gate` 0 → **3**, `s32(0x438b4dc)` 15 |
| **17051** | 0 → **2** / 60 → 0 | the `+0x2bcfe` arm: both portals rise, `0x2bcfe` → 0, `gate` 3 → 0 |
| 17789 | 2 | return door "Return to town now?" at (−45.77, −40.55), `portal` 2 (door answers from the −x side only) |
| 17973 | | results bag "21/20" (Rearrange, Iron Armor, Spiked Armband, Warrior's Helm, Apple ×…) |
| 17990 | | **town map** (`scene` 8), day 23 (HUD Day 24), **`stime` 1 → 2**, `wms` 1121211 |
| 18364 / 18386 | | Save slot 020 (pause entry 4 = type 3) / `rd endsave` → `save:68c43a00466e` |

So the story exit does NOT end the trip: fade (120 ticks) → shop scene → back on 30F with the portals up → normal return door.
The scene runs on the 30F floor state (`floor` 29 throughout); `+0x2bcfd` is the "town scene active" flag, `+0x2bcfe` the "came back from it"
flag that `FUN_00450877`'s first arm consumes at `etick` 60.

## Transcript (town scene, Recette's shop; x 144 = Alouette, x 190 = Arma — pictures f15240, f15520)

```
f15222  Alouette: ... Hmph.
f15261  Alouette: She isn't here.
f15378  Alouette: Why did I even come / here?...
f15495  Arma:     Ah, mistress. Yeah, I / figured you'd come here.
f15599  Alouette: Whu?!... What are you / talking about?! I merely...
f15638  Alouette: I just wanted to tease / Recette, that's all.
f15703  Arma:     You never were that / good at bein' honest.
f15742  Arma:     Hm. If I had to guess, / they're probably out in a / dungeon or something.
f15807  Alouette: Wh-why should I care / where they've gone?!
f15846  Alouette: She's probably out... / out WORKING, somewhere! / WORKING! Like... like a...
f15911  Arma:     ... So whatcha thinkin' / of doing?
f15950  Arma:     Gonna go look for Recette / and the bookworm?
f16002  Alouette: I...
f16093  Alouette: Well, if you really must / see Recette that badly,
f16132  Alouette: then I suppose I could / deign to accompany you.
f16197  Arma:     Oi, you gotta be...
f16314  Arma:     Right, right, I get the / picture...
f16353  Arma:     Please, mistress! I simply / must see Recette!
f16418  Alouette: Well, I suppose there's / nothing for it, then!
f16457  Alouette: Come on, let's be off!
f16613  Arma:     Vei...
f16730  Arma:     Pops... I owe you my / freedom, but your kid / tries me, sometimes...
```

Pictures: `view/f015240.png` (Alouette alone at the shop counter, name box "Alouette"), `view/f015520.png` (Arma and Alouette).

## Bank bytes (mark f14554 → f17390, the whole slot)

- `+0x2bcf5` 0 → 1 (f14783) — "Lapis 30F story exit done".
- `+0x2d5fe` 0 → 1 (f16910, the dialogue's end) — the script-viewed byte of iv10_6: `byte_4510B38` = bank `+0x2d3a0[60·scene + sub]`
  (`SAVE_WORK_SCRIPT_VIEWED_BYTE_OFF`), written by `FUN_0046c320` at the script's end and read by it (the skip gate).
- `+0x8` play counter. Transient: `+0x2bcfd` 0 → 1 (f14783) → 0 (f16990); `+0x2bcfe` 0 → 1 (f16990) → 0 (f17051).

## Port notes (`dungeon4_event_tick`, `PORT-DEBT(stub, FUN_00450877)`)

The port's arm matches what retail did at f14783 (flag 1, `+0x2bcfd` 1, `b4dc` = `+0x2cde0` (= 0 here: stage 0, the shop), worker load,
fade-out 17) and the return arm at f16990 / f17051. Retail's `s32(0x438b1c0)` (= `scene`) read 1 at every sample f13832..16990 (10-tick samples
f14783..15083, the f14783 tick itself): the DAT_0438b1c0 = 1 write changes nothing visible here (already 1). The stub's missing `FUN_00474d92` teardown is not visible in state at this granularity.

## Tool gaps

1. A `--hold 30` leg can arrive "safe" while a pack is closing in: the hit landed ~25 ticks after the hold window (27F); later searches
   reported "1 cell" with no hint that every action was doomed. A rejection count for "avoid true on the start state" would say so.
2. `rd play --watch` has no `--changes` (only `scan` does).
3. Pause-menu entries are not in a fixed order on the town map (sel 0 = status page, 4 = Save); `pm_type` exposes it, but the type of
   sel 0 reads 0.
4. No named fade state (`FUN_004528b3`'s flag) for "fade started / done".

## Port (2026-11-12): identical + save

The town scene is `FUN_0048670f`'s `+0x2bcfd` arm (after the +0x2bcd8 / +0x2bced arms): b924++, cc68 = 1, `FUN_0046f621`,
`DAT_056da1cc..d4` = −1 (no party tick, so no Tear sparkle), b4e8 1 + the camera ease, b924 301 `FUN_0044ba2c(10, 6, 0)`, 302
`FUN_004526f5(0, 80)`, once done `FUN_0045281c(0, 17)`, `FUN_00474d92`, `FUN_0045bcea`, `FUN_00452cde`, `+0x2bcfd` 0, `+0x2bcfe` 1.
During the dialogue `FUN_0048407f` (via `FUN_004427d3`) still runs the wing sparkle (Tear's scale > 0 with no actor).
