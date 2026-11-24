# The adventurers' shop story scenes (FUN_00462403's story probe) — 2026-11-24

Code read (IDA `all_ida.c` 65597–65928 the probe, 62160–62810 the roster scan `sub_45EDAA`, 61575 `sub_45E6E0`,
`sub_45ED12`, every writer of the precondition bytes, `data/event.txt`, `data/kyaku.txt`). Port: `cs_story_probe` /
`cs_roster_scan`, `src/customer_service.c` — **logic identical to retail** (arm order, offsets, else-if structure,
quest multipliers, the 19-table, every roster gate). The coverage index (`where.py FUN_00462403 --unhit`) shows no
recording plays any arm but Charme's 3/4 / 3/5 and Caillou's first beat: these are how the adventurers come to the
shop and join. Live verification: the probes below.

## Mechanism

- On a shop session's **first customer** (`DAT_0730b528 == 0`) the idle's `b524 == 50` step plays at most one scene
  (`FUN_0044ba2c(group, n)`), chosen by work-bank story bytes, closeness, and whether the customer id is in the
  session's eligible list `DAT_06a5d450` (20 dwords, built by the roster scan when the store opens).
- Skipped only in **mode 2 = Survival** (bank dword 46937; `aSurvivalMode` IDA 7953, set by the title at 111821;
  Endless = mode 3, 114046). Runs in normal, New Game+, Endless.
- Generic story flags: bank byte `0x2bc48 + N` = flag N; `event.txt` town events set them through `FUN_0045de68`
  (IDA 61121): the row's first hex field is the flag set, the other three the required flags.
- **Closeness index = candidate slot, not customer id.** Slots go in id order over the active `kyaku.txt` records;
  ids 13–17 take 3 slots each (while `0x2bd09` = 0):

  | id | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 11 | 13 | 14 | 15 | 17 | 18 | 19 | 20 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9–11 | 12–14 | 15–17 | 18–20 | 21 | 22 | 23 |

  Closeness of slot i = int16 at bank `0x2d210 + 4i` (dword `0xb484 + i`, low half). The probe's tests are all
  "> 2" (a sale gives +1 / +2 / +5).
- Eligibility (normal / NG+ / Endless): band ≥ 0 and the `1 << tod` bit of the kyaku time mask; status 1 (forced)
  always enters; status 2 only with score ≥ 55; a news-event session (`DAT_0730b5e8`) rebuilds the list around the
  news target (forced customers can drop out → no scene that session).
- Names (`kyaku.txt`): 2 Louie, 3 Charme, 4 Caillou, 5 Tielle, 6 Elan, 7 Nagi, 8 Griff, 9 Arma, 18 Euria,
  19 Alouette, 20 Prime. Scene group 7 is Griff's (Louie's 7/5 is a joint scene).

## Arms (offsets hex = decimal)

| arm | customer (id → slot) | precondition | retail writer | eligibility |
|---|---|---|---|---|
| 3/4 (65612) | Charme 3 | `0x2bc8d`=179341 ≠ 0, `0x2bc8e`=179342 == 0 | 179341: Charme beaten, d1 15F, stage 6 tick 30, scene 3/8 (`sub_44D47D` 50714) | status 2 (noon / evening / night) |
| 3/5 (65619) | Charme 3 → 1 | 179341, 179342 ≠ 0, `0x2bc8f`=179343 == 0, slot 1 > 2 | probe only; sets 179343 + `0x2bc4d` (joins) | same |
| 3/7 (65627) | Charme 3 | `0x2bcb8`=179384 == 1, `0x2bcb9`=179385 == 0, 179277 ≠ 0, 3/4 / 3/5 not firing | 179384 = flag 0x70, town event 3-6 (plaza, morning / noon, days 4–33, 36+). Sets 179385 = Amber Garden open | same |
| 7/5 (65643) | Louie 2 | `0x2bce0`=179424 == 1, `0x2bce1`=179425 == 0 | 179424: Griff's 7/4. Sets 179425 = Obsidian open | forced while 179424 && !179425 (`0x2bc83`=179331 == 0 → status 0; written by scene 2/10, `sub_442CEF` 45069) |
| 8/5 (65659) | Nagi 7 | `0x2bcc3`=179395 == 1, `0x2bcc4`=179396 == 0 | 179395: 4th dungeon meeting with actor type 29 (`sub_42DA1D` 29286–29312, scenes 8/1–8/4 via `0x2bcc0..c3`, distance 3.0); it spawns (32660) non-Survival, non-boss floor, `0x2bc81` ≠ 0, 179395 == 0, `0x2bd0f` == 0, day ≥ 8, rand % 10 == 0 | forced; after 179396 status 2 (morning / noon / evening) |
| 8/6 (65666) | Nagi 7 → 5 | 179396 == 1, `0x2bcc5`=179397 == 0, slot 5 > 2 | probe only; sets 179397 + `0x2bc51` (joins) | status 2 |
| 7/4 (65682) | Griff 8 | `0x2bcdf`=179423 == 1, 179424 == 0 | flag 0x97, town event 7-3 (plaza night; needs 0x96 from 7-2 ← 0x95 = 179421, wake-up scene 7/1 `sub_44BD0D` 49477: Tielle met (179390 ≠ 0), tod 0, day ≠ 8) | forced (evening / night) |
| 7/9 (65688) | Griff 8 | `0x2bce7`=179431 == 1, `0x2bcea`=179434 == 0 | Obsidian 50F all actors gone (`sub_4427F1` 44273) | forced |
| 7/10 (65695) | Griff 8 → 6 | 179434 == 1, (`0x2bc52` == 0 or `0x2bd15`=179477 == 1), slot 6 > 2 | 179477: the NG+ carry when Griff had joined (113779) → 7/10 replays in NG+ | status 2 |
| 9/2 (65711) | Arma 9 | `0x2bcee`=179438 == 1, `0x2bcef`=179439 == 0 | day dword 15, tod 0 → `0x2bced` (`sub_4852FB` 94815); the wake-up tick 300 plays 9/1, sets 179438 (49458) | forced; 179439 → status 2; with `0x2bcf3`: status 2 only if `0x2bd00`, else 0 |
| 4/6 (65742) | Tielle 5 → 3 | `0x2bcbe`=179390 == 1, `0x2bcbf`=179391 == 0, slot 3 > 2 | Tielle beaten, Amber 30F → `0x2bcbb` (`sub_44DB3C` 50974); town → `0x2bcbc` (102797); arriving home 4/4 → `0x2bcbd` (live: `amber-26f-30f-tielle` f34706); the next morning (tod 0) 4/5 → 179390 (49661). 4/6 sets 179391 + `0x2bc4f` (joins) | status 2 (morning / noon / evening) |
| 17/42 (65734) | Tielle 5 | not (179390 == 1 && 179391 == 0); 179391, `0x2bd88`=179592, `0x2bd8a`=179594 ≠ 0; `0x2bd8b`=179595 == 0 | flags 0x140 (market 17-14), 0x142 (17-41 via 17-27's 0x141); market, morning / noon, Tielle joined + card 0x77 | same |
| 6/5 (65755) | Elan 6 | `0x2bcd6`=179414 == 0, `0x2bcd5`=179413 == 1 | 179413 set by the roster scan itself (62476) when `0x2bcd4`=179412 ≠ 0, 179414 == 0 and `FUN_0045ED12` passes; 179412 = Elan's 4th guild visit (`sub_4922C0` 104915: 6/2, 6/3, 6/4), needs flag 0x88 = 179408 (tavern 6-1: Charme joined + card) | forced in the scan that sets 179413 |
| 6/6 (65762) | Elan 6 → 4 | `0x2bcd7`=179415 == 0, 179414 == 1, slot 4 > 2 | probe only; sets `0x2bc50` (joins) | status 2 |
| 5/1, 5/7 (65815) | Caillou 4 | `0x2bcc9`=179401 == 0 | sets 179401, `0x2bcca` = day low byte; `FUN_0045E6E0` 1 → 5/1, 2 / 3 / 4 → 5/7 + the quest at ×3 | merchant level dword ≥ 6 and `0x2bc81` → `0x2bcc8` → forced |
| 5/2–5/4 (65780) | Caillou 4 | 179401 ≠ 0, `0x2bccb`=179403 == 0 | `FUN_0045E6E0` (61575): 0 if 179403; 1 unless items 3101 Charred Lizard, 3110 Bat Wing, 3106 Slime Liver are all in the bag / display grid; else d = day − 179402: d < 2 → 2 (5/3, ×2), 2–3 → 3 (5/3, ×1.5), ≥ 4 → 4 (5/4, ×1). The quest removes the items, sets 179403 | 1 → forced only if d ≥ 2 and rand & 3 == 0; 2 / 3 / 4 forced; after 179403 status 2 |
| 5/5 (65899) | Caillou 4 → 2 | 179403 == 1, `0x2bccc`=179404 == 0, slot 2 > 2 | probe only; sets `0x2bc4e` (joins) | status 2 |
| 17/3, 17/40, 17/49, 18/9 (65912; table 0x5c6c7c) | Alouette 19 → 22 | `0x2bdfb..fe` (179707..710) == 0, slot 22 > 2 / 6 / 10 / 14; one per session in table order | probe only (event.txt shop events 1b3–1b6) | `0x2bce8`=179432 ≠ 0 → status 2 (179432: the day-8 morning cutscene, `sub_48670F` 96109) |

**Elan's gate quirk** (`FUN_0045ED12`): looks at display row 0 only; tests occupancy at counter column
`{1,2,3,4,11,12,13}[j]` but reads the item from column j; passes when that item is a sweet (4001–4006, 4011, 4016,
4025–4034). The port replicates it.

## Start saves per arm

Played lineage first (`tools/drive/SAVES.md`):
- 3/4 `2e356e0e8224`, `39ffce34bc40`; 3/5 `148fa529a313`; 8/5 `a23d9e447eb4`, `fc2e4d81f26d` (Nagi forced);
  5/2 `a23d…`, `fc2e…`, `bc9e123be2b1`; 6/5 `a23d…` (179412 = 1: needs a sweet on display row 0 under the quirk);
  9/2 `64d43650865f` (17264a73 + gold, day 15 morning).
- Crafted lines (from the analysis, untested): 3/7 `a23d --set 0xaf2e=0x00000001`; 7/5 `a23d --set 0xaf38=0x00000001`;
  8/6 `a23d --set 0xaf31=0x00000001 --set 0xb489=3`; 7/4 `a23d --set 0xaf37=0x01010100`; 7/10 `a23d --set
  0xaf3a=0x01010101 --set 0xb48a=3`; 4/6 played: `shop-story-tielle` (one
  perfect-price sale = +5); 17/42 `fc2e --set 0xaf2f=0x01010000 --set 0xaf62=0x00010001`; 6/6 `a23d --set
  0xaf35=0x00010101 --set 0xb488=3`; 5/4 `a23d --add-item 3101`; 5/5 `a23d --set 0xaf32=0x01110101 --set 0xb486=3`;
  17/3 `a23d --set 0xb49a=3` (15 lines up all four).
- The crafted hire lineage (`824053720e01` → …) has the join bytes crafted (e.g. `0x2bc52` = 1), so 7/10 can never
  fire there.
