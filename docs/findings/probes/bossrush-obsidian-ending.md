# Probe: bossrush-obsidian-ending

- **Run:** `bossrush-3` · **start (CRAFTED lineage):** `save:2f2f2203a65d` = `bossrush-jade-amber`'s end save `2e4388a2cfe2` (shop, day dword 35
  = HUD Day 36 morning, the final collection day, `stime` 0, Louie L47, slot 020) with gold set to 1,000,000 by `tools/craft_pix_save.py
  --gold 1000000`. Upstream is crafted too (`2e4388a2cfe2` ← `1049877e95a4` ← … ← `b4766a99d74b` = `lapis-51f-60f`'s end save with bank
  `+0x2bcf7` cleared). The Obsidian Boss Rush row was already open in the save (d3 `0xb104` = 49, `0xb150` = 1). No task file.
- **Goal:** the Obsidian Tower (d3) Boss Rush (8 floors, d5 floors 38..45), home, the day end → the 500,000 final payment → the ending →
  "Save the game?" → save → load it → the first Endless Mode control.
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`). Every boss by chained `rd search` (`-j 2`, score −boss HP, cell = boss HP
  bucket + position relative to the boss + own HP, actions = aimed walk / aimed swing / aimed spin / retreat / strafes / 4 directed swings /
  wait; `--hold 30..100`, avoid `hp < H − 20..35`). Two-boss floors: a summed-HP score (43F: + 100 per live bee).
- **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`). Nothing ran under wine.
- **Movie:** 57,365 frames. Filed as `tests/scenarios/bossrush-obsidian-ending/trace.jsonl`. **No retail golden** (next: `rhost.py golden
  bossrush-obsidian-ending`, `rhost.py triage bossrush-obsidian-ending`).
- **Verdict (2026-11-14):** `rd verify bossrush-3` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 57365 rng d33292b0 sem 661476539ba17b2e dev 138ffb1bb17a2a13  (unskipped retail)  [win]
  live session:          frame 57365 rng d33292b0 sem 661476539ba17b2e dev 138ffb1bb17a2a13  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 57365 rng d33292b0 scene 1  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:1aa94b103208` (`rd endsave` at f57365, shop, Endless Mode control). Slot **020** holds the ending's Endless save
  (SCORE 9943, TIME 5:41:49, tag "Endless", Day 37, 495,350 pix); no extra in-game save was needed (loading 020 is how the state was
  reached). Note: the picker opens on **slot 001** (empty) after "Save the game?", not on the last-used slot — moved to 020 by `D` ×19.

## Before the rush

| frame | what |
|---|---|
| 151 | shop, HUD Day 36, control (`.*60 A .*30 A .*80`) |
| 376 / 446 | door (search) → map; `U U R` → `wm_cur` 6 → guild |
| 490..2190 | Louie → bag (20/20): Apple ×4, Lobster, Baked Yam, Taiyaki ×5, Walnut Bread, Watermelon, Cherry ×2, Apple+3, Leather Boots, Warrior's Helm, Spiked Armband, Iron Armor. The bag list keeps the last row after a pack (clamped when the item runs out) |
| 2213 / 2358 | Depart → plates (cursor 2 → `R` → 3 Obsidian) → rows "1～5".."46～50" + **Boss Rush (row 10, `--:--:--`)** |
| 2374 | `A` → fee 4,650 (1,000,000 → 995,350), `stime` 0 → 1, `dungeon` 5, `0x438b4d0` 3, `floor` 38; "Equip items in bag?" → Yes f2415 |

## Per floor

H = Louie's HP. Table stats live at `0x5c23f0 + type*0x68` (HP / AT / DF / MA / MD). Levels = `f/4 + 16`, f = 5·floor − 145.

| floor | stage | arrival → kill → `estage` 1 | boss (slot) | table | how | H |
|---|---|---|---|---|---|---|
| **38** | 0 | 2374 → 5073 → 5237 | 55 pumpkin ghost (0), L27, **960** | **150**/20/20/35/10 | 5 searches; adds hit (228 → 169); Taiyaki + Apple at f4549 | 228 → 187 |
| **39** | 1 | 5493 → 7242 → 7401 | 70 jellyfish (0), L28, **660** | 100/30/30/20/30 | 4 searches, ~180 frames per 150 HP | 187 → 151; **Lobster** (+77 HP and SP to full) |
| **40** | 2 | 8071 → 9048 / 10487 → 10651 | 54 boss ahriman ×2 (0 / 1), L29, **680** each | 100/20/20/35/20 | 5 + 4 searches; one doomed apply (a 65-damage beam 7 ticks after the hold) undone, redone with `--hold 60` | → 189; Taiyaki |
| **41** | 3 | 11063 → 12532 → 12711 | 61 golem + 62 / 63 (0..2), L31, **1080** each | 150/30/30/0/30 | 6 searches on the body (arms freed with it) | → 166; **level-up at the clear: max HP 232, SP 52, refilled** |
| **42** | 4 | 13202 → 17371 → 17490 | 28 Griff (0), L32, **1480** | **200**/35/30/40/30 | 7 searches; Taiyaki mid-fight (pause works mid-fight) | → 212 |
| **43** | 5 | 17911 → 20221 / 20367 → 20542 | 67 Ohm ×2 (0 / 1), L33, **1140** each | 150/30/25/20/25 | the hard floor (below) | 212 → 178 |
| **44** | 6 | 20748 → 22408 → 22572 | 49 black knight (0), L34, **780** | **100/35**/20/0/20 | 6 searches | → 158; Watermelon (+68) + Cherry (+6) |
| **45** | 7 (last) | 23044 → 28677 → 28796 | 28 Griff (1), L36, **1640** + 72 hand (0), L36, **820** | 200/35/30/40/30; 72: 100/20/20/35/20 | Griff only, 10 searches (`--hold 100`, plain `--apply`); Griff's death took the hand | 232 → 232 |

- **Setup:** party at (−40, −45); bosses as the d5 code places them (45: hand at (−35, 13.6, −25) descending, Griff (−40, −34)). Pad lock
  240 ticks; floor → floor by the kind-8 portal at (−40, −40) ("Go to the next floor?" → floor + 1 ~47 ticks after `A`).
- **43F:** two Ohms (charge 43–65 dmg) + up to 8 bees. `--apply-best` kept a best cell 10–40 ticks before a committed charge three times
  (every next search: "one cell"); plain `--apply`, `--hold 60..100` with two 25-tick strafe tails, a bee term in the score and an undo after
  a failed search went through: 2,280 HP in ~2,300 frames, no damage after f18294 (one sting earlier).
- **Pause menu:** ignored for ~125 ticks after `estage` 1 (ESC posted at clear + 10 did nothing; at + 125 it opened). Items page: Use / Throw
  Away box first, `A` again uses. Lobster: +77 HP and SP to full (3 → 51); Taiyaki +30..32 SP; Apple +18; Watermelon +68 (capped); Cherry +6.
- **Last floor:** `doorobj` 0 (kind 6) at (−40, −40), shut; walk to (−40, −42.5), `U A` → `doorst` 1 at f29018 → "Return to town now?"
  f29048. **Best time 21,915 ticks (6:05.3) written to dword 0xb151** (was 0) = trip counter `0x438b1e8`. "CAME BACK ALIVE!" (scene 9, 11/20)
  f29210 → `B` → map f29287, `stime` 2. No results / reward.

## Day end, payment, ending

| frame | what |
|---|---|
| 29408..30112 | map → Town Square (scene 14): an event (dlg 1 29409..30095; Tear and Recette on a pie smell / feeling lonely / "salted porridge") → map |
| 30181 | home, `stime` 2 → 3 (night) |
| 30356..30641 | out again → Town Square "It looks like nobody's here..." → map |
| 30709 | home, `stime` 3 → 4; **no Tear day-end line**; Daily Profits (scene 9) from f30870 |
| 31155 | day dword 35 → **36** (HUD Day 37) |
| 31461..31517 | payment form: gold 995,350 → **495,350** (counting down); Score 9603 → **9943 (+340)** |
| 31632..32357 | verdict (dlg 1): "So, this is the payment for this week. Let me see..." / "Congratulations, Recette. With this payment, your father's loan has been repaid in full." / "So... it's all over, right?" … "May I... have a moment? Could we step outside, perhaps?" / "Sure!" |
| 32373 | scene 9 → **7**, `stime` 0; ending script dlg 1 32382..48698 (hilltop scene, the "…ended!" card, credits — A does nothing) |
| 48699..52110 | farewell / ledgers script |
| ≈52115 | **"Received Louie's True Card!"** |
| 52129..54060 | post-game explanation script |
| 54061 | **"Save the game?"** Yes / No |
| ≈54090..54465 | Yes → picker opens on **001 (NO-DATA)**; `D` ×19 → 020 (the start save, Day 36, 1,000,000 pix) → `A` → "Overwriting file. Are you sure?" → Yes → slot 020 = Endless, SCORE 9943, TIME 5:41:49 |
| 54557..54666 | `B` → back to "Save the game?" → `R A` (No) → title (scene 0) f54666; cursor LOAD GAME (`t_cur` 3) |
| 54848 / 54873 | `A` → picker on 020 → `A` → loads (scene 1 f54873) |
| 54874..57334 | Endless title card (dlg 1, 2,461 ticks) |
| **57335** | **control in the shop** (Recette (−0.3, 9.35), day dword 36, 495,350 pix, `stime` 0); movie ends f57365 |

A first attempt tapped `A` straight through the prompt: Yes + `A` on slot 001 wrote the Endless save into the EMPTY slot 001 with no
confirm (as `ending-week5-paid` saw); rewound to f54061 and redone into 020.

## Card corrections

- **boss-rush.md:** mode-3 roster filled (levels 27..36, live HP); table HPs 55 = 150, 28 = 200, 49 = 100 (AT 35); the d3 best dword is
  0xb151; floor 45's return door is object 0 at (−40, −40). §3c added.
- **ROSTER.md:** d3 rows' table HPs (55: 150 not 15; 54: 100 not 15; 49: 100 not 15; 28: 200 not 150) and the d5 38..45 rows (live L / HP).
- **black-knight.md §5:** the live table AT is 35, not 2 (the 1-damage reading cannot hold at these levels; 44F took one 20-HP hit, source not isolated).
- Port: boss-rush.md §6 "types 28, 41, 49, 55, 61..66, 70..76 have no AI" is stale against ROSTER (all ported).

## Tool gaps

1. `--apply-best` can keep a cell a few ticks before a committed hit (the hold only guards goal arrivals): three times on 43F and once
   on 45F the next search returned "one cell". A hold check on the best-cell path (or a `--best-hold N`) would avoid the undo dance.
2. A re-run of the same search from the same state re-finds the same path even with `--seed $RANDOM` (refine converges): a doomed
   follow-up repeats until the goal / drop size changes.
3. `rd tap A --until frame` walks through the save prompt and the picker; a stop on "a Yes/No box appeared" (text row `Save the game?`)
   would be safer.
4. No named var for the save picker's cursor (`t_pcur` read 19 the whole time while the cursor moved 001 → 020; the highlighted slot is
   only visible as the brighter `0NN` text row).
