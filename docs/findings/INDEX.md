# Findings index

Table of contents for the durable reverse-engineering writeups in `docs/findings/`.

## How the docs layer

- **`memory/MEMORY.md`** (auto-memory) — *pointers*. Short index entries that
  link out to the durable docs; survives across sessions but holds no detail.
- **`docs/findings/*.md`** (this dir) — *durable RE*. The long-lived
  per-subsystem / per-`FUN_`-cluster writeups: survey, call graph, offsets,
  corrections. Cite these, don't re-derive them.
- **`docs/PROGRESS.md`** — *narrative*. Chronological, dated log of what landed
  when and why; the running story.
- **`docs/STATUS.md`** — *headline*. The current one-screen "where are we"
  snapshot.

Freshness = last git commit that touched the file (`git log -1 --format=%cs`).
A survey-only doc may be stale-by-date yet still current if nobody has revisited
that subsystem.

## Agent probes (retail observed live through `rd`)

Reports of agent-authored traces (`tools/drive/README.md`): each has a scenario under `tests/scenarios/`, a task file under
`tools/drive/tasks/`, `rd check` PASS, a real-device replay MATCH and a retail golden. They are the driver's words: facts
observed in the running game; anything read from code only is a lead. Named state they validated: `tools/drive/vars.json`.

| Doc | Covers |
|-----|--------|
| [bosses/combat-core.md](bosses/combat-core.md) | Dungeon combat, observed live: the sword / spin timings, the damage formula (`ftol(ATK/2 − DF·power²/4)`), hit labels by the angle of the hit record to the enemy's yaw (Side ×1.2, Back ×1.5, Counter ×1.2 in the enemy's attack states), invulnerability, how to fight by search. |
| [bosses/crown-slime.md](bosses/crown-slime.md) | Type 23: power (`+0xab8`, DF = 8·power²) sheds 5 → 1 over 900 ticks and resets on absorbing a blue slime aged ≥ 600; the leap + landing shockwave; the 25F pair; kill recipe (done live, 703 frames); arms checklist. |
| [bosses/ROSTER.md](bosses/ROSTER.md) · [UNLOCKS.md](bosses/UNLOCKS.md) | Every dungeon's boss floors → actor types → AI fn → port status; the bank dwords that open dungeons, floor rows and hires (crafted save `824053720e01`). |
| [bosses/](bosses/) boss cards | Code-read state machines, damage windows, kill recipes, arms checklists: mushroom-king, charme, boss-ahriman, ohm, crabs, tielle, dungeon2-15f, boss-rush, boss-floor-common; unported: golem, griff, demon-hand, jellyfish, arma, arma-golem, pumpkin-ghost, black-knight. [player-knowledge.md](bosses/player-knowledge.md) = what players document (unverified). |
| [probes/tear-news-summary.md](probes/tear-news-summary.md) | Tear's menu (`cc08` 15) → News Summary (entry 8; `cc08` 16 → 17): the page, its inputs, exit; the menu's entry list at level 6. |
| [probes/tear-outstanding-orders.md](probes/tear-outstanding-orders.md) | Tear's menu → Outstanding Orders (entry 9, absent on the level-6 save; crafted level 10): the "Advance Order Summary" panel, empty. |
| [probes/survival-hell-start.md](probes/survival-hell-start.md) | Survival Hell: what differs from Normal (687-dword wipe: gold, level, items, adventurer levels; marker bank dword 0xb78d; opening script 19), the 20,000 payment on day 7 missed, the game over back to the title. |
| [probes/survival-start.md](probes/survival-start.md) | Title → SURVIVAL MODE → Normal Survival: the mode list, New Game +'s picker reused, the bank diff of the start (68 dwords; day starts at 0; candidate mode dword 0xb759), the opening, two days passed, the first-morning save. |
| [probes/tear-menu-pages.md](probes/tear-menu-pages.md) | The rest of Tear's menu: Merchant Level page (scroll, per-level ability texts), Store Atmosphere, lectures, Time Lecture. |
| [probes/counter-move-tables.md](probes/counter-move-tables.md) | Counter menu → Move Tables (`cc08` 2, level dword ≥ 9): pick, move, collide, confirm / cancel. First probe that split the port (ported 749a4e37). |
| [probes/move-tables.md](probes/move-tables.md) | The first Move Tables attempt on the level-6 save: the entry is absent below level 10 (HUD). |
| [probes/decor-defaults.md](probes/decor-defaults.md) | Counter menu → wall / floor / carpet editors with only the default items owned. |
| [probes/decor-variants.md](probes/decor-variants.md) | The decor editor with crafted variants of all four kinds: preview, confirm, cancel, what the bank keeps. |
| [probes/guild-fusion-confirm.md](probes/guild-fusion-confirm.md) | Merchants' Guild → Fusion: recipe list, ingredient check (bag only), confirm (mode 5), result (mode 6). |
| [probes/guild-sell-expansion.md](probes/guild-sell-expansion.md) | Merchants' Guild → Sell (mode 0 → 8, the money roll) and the store Expansion purchase (tables re-laid, items returned). |
| [probes/market-buy-display.md](probes/market-buy-display.md) | Market (= guild variant 1) buying, then a table's display menu: put / take / replace. |
| [probes/payday-week2-paid.md](probes/payday-week2-paid.md) | The week-2 payment paid: the night, the collection form, the verdict script, next-morning scenes; leaves the week-3 save. |
| [probes/item-storage-l17.md](probes/item-storage-l17.md) | Merchant level 17 "Item Storage" = the dungeon bag grows to 25 slots (`DAT_005c5a98`); L16–L18 ability texts; the guild's bag screen (rows, buttons, picker), the bag's memory (0x438b5ec + 4i, twin at 0x438b684). |
| [probes/gameover-loop-dungeon0.md](probes/gameover-loop-dungeon0.md) | A payment missed → the loop restart (level + items kept) → guild intro → Hall of Trials 1F with Louie. |
| [probes/pause-options-saved.md](probes/pause-options-saved.md) | Pause → Options: sliders / toggles are save-header fields edited in place; the dirty exit writes save.dat. |
| [probes/pause-status-items.md](probes/pause-status-items.md) | Pause → Status (party list) and Items (tabs, scrolling, detail box) in week 3. |
| [probes/town-rounds-week3.md](probes/town-rounds-week3.md) | Every town destination in week 3: Market, Town Square event 11/1, Chapel event 16/5, both guilds; map enable states. |
| [probes/pub-evening-week3.md](probes/pub-evening-week3.md) | The Pub (scene 15; enabled in the evening, `stime` 2): the first-visit event, map states before / after, visits cost no period. |
| [probes/shop-session-week3.md](probes/shop-session-week3.md) | A whole shop session: sales at 105% / 130% / walk-out, a customer selling, price entry, what ends the session. |
| [probes/newgame-plus.md](probes/newgame-plus.md) | The title after the ending (8 entries: NEW GAME +, SURVIVAL MODE, SURVIVAL SCORE, …; `t_cur` / `t_item` table), the New Game + picker (cleared saves only), what the bank resets (86 dwords: gold, day, score, story flags) and keeps, the short NG+ opening (script 2/6). |
| [probes/order-customer-week3.md](probes/order-customer-week3.md) | An advance order from the customer's request to Tear's Outstanding Orders row and the order served. |
| [probes/calendar-week3.md](probes/calendar-week3.md) | Days 15–22 walked morning by morning (a save per day): map states per period, events met, the week-3 payment. |
| [probes/calendar-week4.md](probes/calendar-week4.md) | Days 22–29: the guild chain 6/2–6/4, the Girl events 18/1 + 17/21, day-24 closures, the 200,000 payment paid; saves for days 23–29. |
| [probes/change-tables-l16.md](probes/change-tables-l16.md) | Counter menu → Change Tables (level 14+): the Counters editor restyles every table at once (bank dword 0xb37c), live preview, confirm / cancel, the bag swap; Market Counters tab prices. |
| [probes/pause-encyclopedia.md](probes/pause-encyclopedia.md) | Pause → Encyclopedia: 33 categories, the 3×3 grid, scroll / slide / detail state, every button, the detail fields, completion (seen / 538), entry-slot layout. |
| [probes/endless-first-days.md](probes/endless-first-days.md) | Endless Mode's first two days from the Endless save: the title card 12/1, the debt-free calendar (display restarts at day 1), Daily Profits without a collection form, map states and events per period, guild lists. |
| [probes/ending-week5-paid.md](probes/ending-week5-paid.md) | Days 29–35 (events per period, a save per morning), the final 500,000 payment, the ending scenes and credits, True Card, the "Save the game?" prompt, the title afterwards, Endless Mode's first control (crafted gold). |
| [probes/week5-gameover.md](probes/week5-gameover.md) | The last payment missed (198,930 of 500,000): the collection form, the game-over screens (scene 2), the loop back to day 1 with 1,000 pix; what the loop keeps. |
| [probes/order-deadline-missed.md](probes/order-deadline-missed.md) | An advance order never filled: Tear's Outstanding Orders row per day, the orderer's visit on the due day, what a missed order does. |
| [probes/title-extras.md](probes/title-extras.md) | The title's pages after the ending: ITEM ENCYCLOPEDIA, SURVIVAL SCORE, OPTIONS, LOAD GAME's slot list; `t_sub` per page (never cleared on close: `t_fade` 0 = main menu). |
| [probes/guild-talk-topics.md](probes/guild-talk-topics.md) | Full transcripts of the Merchants' Guild's six Talk topics (guild, services, merchant levels, town, unknown items, fusion), the "New" flags; the Market has no Talk. |
| [probes/vending-level16.md](probes/vending-level16.md) | Merchant level 14–17 ability texts; vending machines: bought at the Market, placed from a table's display menu, the machine menu, stocking. Splits the port at f5500. |
| [probes/vender-sales-l16.md](probes/vender-sales-l16.md) | A shop session with a stocked vending machine: stocking (one unit per slot, any category), the 6 slot dwords, two background sales at exactly base price, the counter customers of the session. |
| [probes/vender-revert-sell.md](probes/vender-revert-sell.md) | "Revert To Table" with and without stock (no prompt; items + machine back to the bag; the bank diff), selling the machine at the Merchants' Guild (9,000 = 30% of base), the Venders category gone afterwards. |
| [probes/dungeon2-21f-25f.md](probes/dungeon2-21f-25f.md) | Dungeon 2 floors 21F–25F by search: enemy mixes per floor, exit / ticker / lock state, the actor table, the double crown slime; search settings that worked and failed. |
| [probes/lapis-31f-40f.md](probes/lapis-31f-40f.md) | Lapis Ruins 31F–40F in one trip (no damage): per-floor banners / enemy mixes / grids, new types 12 / 71 stats, the 35F pumpkin-ghost trio (1,080 HP each), the 40F red jellyfish (type 71: phase starts at 1, waves of 4 at phase 3), the return door, the day end, Save. |
| [probes/lapis-41f-50f.md](probes/lapis-41f-50f.md) | Lapis Ruins 41F–50F in one trip: the 45F mob floor (34 enemies, gate 3), Arma on 50F (type 41: live stats HP 250 → 1,950; every attack state but the finisher; fresh / non-fresh hits; her AI frozen during event 12/6), event 12/7, `bank+0x2bd13`, the return, Save. |
| [probes/lapis-51f-60f.md](probes/lapis-51f-60f.md) | Lapis Ruins 51F–60F in one trip: the 55F black-knight pair (live HP 800 each, knights refilled 2 per tick, 60 / 1200 invulnerability at the two deaths), the 60F type-60 rat (HP 1230, `+0xab8` 3, two-swing end 114, three purple windows), event 12/4 skipped (bank+0x2bcf7 already set in this lineage), the 60F return door answers walking +x, Save. |
| [probes/lapis-61f-70f.md](probes/lapis-61f-70f.md) | Lapis Ruins 61F–70F in one trip: the 65F mob floor (39 skeletons / ghosts, skeletons down 3× each, exit hidden until all freed), the 70F dark golem (64 + 65 + 66, live HP 1290, AT 30 / MD 40; adds type 35 `need` 2, back-off −60 / −120, cap 9; every non-† arm), the return door opens only after a sword hit, zero damage, Save. |
| [probes/lapis-60f-event-12-4.md](probes/lapis-60f-event-12-4.md) | Event 12/4 (Alouette + Prime on Lapis 60F) from a CRAFTED save (`lapis-51f-60f`'s end save with bank+0x2bcf7 cleared): 56F–59F, the type-60 rat, floor open at death tick 500 with `lock` 0, the scene at etick 120 (`sid` 12004, flag set the same tick), full text, `lock` −1 the tick after, return door, Save. |
| [probes/lapis-71f-80f.md](probes/lapis-71f-80f.md) | Lapis Ruins 71F–80F in one trip on the crafted-save lineage (`24ab07c65610`): 75F mob floor (39 actors incl. 6 yellow bombs; exit hidden until all freed, portals rise in the start room), 80F events 12/10..12/12 tick for tick (props 42 Hydra + 43 Papa, `lock` 4 auto-walk, trigger within 4 of the 43, seen bytes `+0x2d67a..c`, `+0x2bd14` never set), full text, return door, Week-4 repayment, Save. |
| [probes/lapis-81f-90f.md](probes/lapis-81f-90f.md) | Lapis Ruins 81F–90F in one trip on the crafted-save lineage (`8767348923a8`): Market food stock, the 85F mob floor (30 actors, roper list; exit hidden until all freed, portals in the start row), the 90F black crab (type 69: live HP 1410, table AT 40 / MA 20 / MD 30; 4 bomb flips, blasts DAMAGE it −179..−290; all arms but spray), zero damage, return door, Save (`6809401b79a9`). |
| [probes/lapis-91f-100f.md](probes/lapis-91f-100f.md) | Lapis Ruins 91F–100F in one trip on the crafted-save lineage (`6809401b79a9`; `+0x2bcff` already 1 → 100F short intro): the 95F knight mob floor (30), event 12/21 at the 99F exit, the 100F Arma golem (live body 2940 / hands 980; table body HP 300 AT 50 DF 35, hands 100; 5 breaks at 490-HP steps; f32640..59955, ~100 hits, zero damage), events 12/16..12/19, CLEAR, event 12/20 on the town map, Day 32 morning event, Save (`7874cefc5cb5`). |
| [probes/crystal-1f-20f.md](probes/crystal-1f-20f.md) | Crystal Nightmare 1F–20F in one trip on the crafted-save lineage (`7874cefc5cb5`): guild closed day dword 31 (day burned), roster incl. Arma L40, d5 rows; plain random floors (levels 50..82, three enemy lists, banners, live table stats), 10F / 20F exits = next-floor circle + return portal 5 units apart (prompt writes the row time 15591 / 19822), return from 20F, zero damage, Save (`2b9e8df3acad`). |
| [probes/crystal-21f-30f.md](probes/crystal-21f-30f.md) | Crystal Nightmare 21F–30F in one trip (`2b9e8df3acad` → `1049877e95a4`, crafted lineage): plain floors L84..97, list 3 + 40 then a fourth list (5, 48 new; live table stats), 27F banner "Consumables now half as effective!"; the 30F rest floor = 3 × 3 room, no actors, return DOOR (sword hit) + 1 treasure chest (1..3 by start floor, no warp plates), row-2 time 11710; `0x438bea8` = (kind == 6) bool, not an index; zero damage, day end, Save. |
| [probes/bossrush-jade-amber.md](probes/bossrush-jade-amber.md) | Jade Way + Amber Garden Boss Rush in one day (`1049877e95a4` → `2e4388a2cfe2`, crafted lineage): d5 floors 30..32 (23 L4 180, 24 L7 360, 25 L9 280) and 33..37 (54 L12 340, 67 L14 570, 68 L16 714 (table HP 170), 23 ×2 L17 440, 27 L19 480); live table stats; floor → floor via the kind-8 portal at (−40, −40); the last floor's return portal is a shut kind-6 door (sword hit); no results / reward; best time = trip counter at the return prompt into `0xb10b + 20·d + cap` (Jade 4736, Amber 13326); food in the pause menu; Day 35 end, Save. |
| [probes/ahriman-exhaust.md](probes/ahriman-exhaust.md) | Boss ahriman (54, L12 340) on Amber rush f33 kept alive through all 19 bats: slowdowns at the 9th / 17th (−60 / −120), gaps 30 / 90 / 150 + 4 per hit-stop, counter 20..29 with no spawn; beam 27..31 = MA·(1 + 0.1·lv)/2 − MDEF/4, bat shots 5..7; the descent waits for "Equip items in bag?". |
| [probes/elan-flurry-kill.md](probes/elan-flurry-kill.md) | Elan's flurry (record 0x86) holds enemies at HP 0 with `+0xba0` = 30; the final blast 0x87 kills them all; the actor tick's own kill (FUN_0042e1a6 when `+0xba0` runs out) not reached, enemy poison `+0xb10` dead in retail. |
| [probes/wisp-hurry-up.md](probes/wisp-hurry-up.md) | The hurry-up wisps: floor timer `0x438c85c` (rooms × 3600 / 2400 / 2100 / 1800 / 1200 / 900) runs out → type 34 every 240 ticks up to 5 (FUN_0048b850, AI FUN_0042ac16), event 19/3 at `0x438cbe0` = 60 (bank `+0x2bd16`); they home and hit ~20 at L40, Elan down in ~860 ticks. |
| [probes/obsidian-first-trip.md](probes/obsidian-first-trip.md) | The Obsidian first-trip cutscene: Depart to d3 row 0 with `+0x2bce3` = 0 → `+0x2bce2`, stage id 20 = **index 19**, event 7/6 at tick 200, the curtains part 260..400, at 460 the flag swaps and 1F loads; FUN_0042df55's past-the-table read not reached. |
| [probes/easydisp-levelup.md](probes/easydisp-levelup.md) | recet.ini `s_easydisp` = 1 (the first scenario with an `{"ini"}` op): kills pay +10 EXP per would-be orb straight to the leader (orbs grow with the chain counter `DAT_0438b90c`), hits +1, no table C orbs; the level-up FUN_0048a383 on the killing tick (f6703, LV 1 → 2, max HP 20 → 24); the hit arm never levels up. |
| [probes/guild-item-limit.md](probes/guild-item-limit.md) | The Merchants' Guild's "Item limit reached, cannot buy any more." (crafted 15,032 items held): A on an item in the Buy list (not the Buy option) opens FUN_00434ceb's box; A or B closes it, the list stays open, no purchase. |
| [probes/title-options-save-clear.md](probes/title-options-save-clear.md) | The title Options' save paths: a slider change + exit writes the save (FUN_004905a8(-1)); "Clear Save Data" (cursor starts on No) → FUN_004902aa re-seeds every bank (draws RNG) and the exit's FUN_0049a43d rebuilds the menu without Continue. |
| [retail-coverage.md](retail-coverage.md) | Retail function coverage of the corpus (INT3 first-hit, 2026-11-19): ≈ 606 of 636 KB of game code executed; no non-debug sim function unreached; 39 ported functions no recording verifies (the probe targets). |
| [shop-story-scenes.md](shop-story-scenes.md) | The adventurers' shop story scenes (FUN_00462403's story probe, 2026-11-24): per arm the precondition bytes, retail writers, eligibility, closeness slot map, start saves; port logic identical. |
| [probes/charme-skills.md](probes/charme-skills.md) | Charme's three skills (slots 0 / 1 / 2 → states 4 / 5 / 6, 5 SP each): the after-image, the rush (hit record 62, invincible, a Back Attack kill), the spider web (hit record 83, binds without damage). First recording of FUN_0049511d / FUN_0049508c; port identical on the first triage. |
| [probes/order-late-pickup.md](probes/order-late-pickup.md) | The late order pickup: an order left past due (shop closed days 15..18) stays active with days −1, is queued as kind 4 when the shop opens, and the orderer comes in (b5a8 5: states 2 → 7 → 11 → 13), complains and leaves — no gold / item / reputation change, the combo reset. First recording of FUN_00464a26; port identical. |
| [probes/order-return-sale.md](probes/order-return-sale.md) | An advance order brought back with goods (FUN_00463cfb): pick rows, detail card, pushback, accept; + the visit-ending branches 17 (row empty) / 16 (wrong item). Port identical. |
| [probes/shop-story-nagi.md](probes/shop-story-nagi.md) | Nagi's shop scenes 8/5 and 8/6 (she joins) and Caillou's reminder 5/2 — the story probe's first non-Charme arms. Port identical. |
| [probes/shop-story-arma.md](probes/shop-story-arma.md) | Arma's first shop visit, scene 9/2 (story probe). Port identical. |
| [probes/shop-story-tielle.md](probes/shop-story-tielle.md) | Tielle's arc after Amber 30F: 4/4 arriving home, 4/5 next morning, 4/6 in the shop (she joins). Port identical. |
| [probes/shop-story-elan.md](probes/shop-story-elan.md) | Elan's 6/5 (the roster scan's sweet gate FUN_0045ED12, live) and 6/6 (he joins). Port identical. |
| [probes/shop-story-alouette.md](probes/shop-story-alouette.md) | Alouette's shop scenes 17/3 and 17/40 (the story probe's customer-19 table). Port identical. |
| [probes/shop-story-alouette-2.md](probes/shop-story-alouette-2.md) | Alouette's 17/49 and 18/9 (closeness > 10 / > 14). Port identical. |
| [probes/shop-story-griff.md](probes/shop-story-griff.md) | Griff's arc: wake-up 7/1, plaza nights 7-2 / 7-3, shop 7/4, Louie's 7/5 (Obsidian opens); one plaza / market event per day. Port identical. |
| [probes/shop-story-tielle-market.md](probes/shop-story-tielle-market.md) | Tielle's market events 17-27, 17-41 and the shop scene 17/42 (her sister's letter). Port identical. |
| [probes/euria-seller.md](probes/euria-seller.md) | Euria (customer 18) selling: a weapon pick, a relic (scan path), a band-3 pick; the pick fixed by the rng at the queue advance. Port identical. |
| [probes/shop-story-griff-joins.md](probes/shop-story-griff-joins.md) | Griff's 7/9 and 7/10 (he joins), crafted Obsidian-50F byte. Port identical. |
| [probes/arma-weapons.md](probes/arma-weapons.md) | All 15 of Arma's weapons (FUN_004986ee's kinds, loops, gauge); found + fixed 2 port bugs (record 0x75's x87 angle step, record 0x74's smoke shape mode). |
| [probes/arma-finisher.md](probes/arma-finisher.md) | Boss Arma's finisher: the lob blast at the wall arms state 5, records 156 (orb) / 158 (beam, 980 damage, party wipe). Port identical. |
| [probes/arma-weapon-edges.md](probes/arma-weapon-edges.md) | Arma's weapon record edges: the Hammer boost / bounces, Omega homing / landing, wall tails, hurt kills; `p_oct` is only ever even. Port identical. |
| [probes/charge-attacks.md](probes/charge-attacks.md) | Tielle's draw bands (records 77..166, crit) and Caillou's short / charged swing (record 82 projectile). Port identical. |
| [probes/shop-story-caillou.md](probes/shop-story-caillou.md) | Caillou's reagent quest 5/4 (reagents from Jade Way bosses) and 5/5 (he joins); ported the quest sale arm b534 30 + FUN_004607f3 whole. |
| [probes/caillou-quest-variants.md](probes/caillou-quest-variants.md) | Caillou's quest arms 5/7 (×3) and 5/3 grades 2 / 3 (×2 / ×1.5), crafted. Port identical. |
| [probes/survival-shop-days.md](probes/survival-shop-days.md) | Three shop days in Normal Survival (the roster's Survival gates; Tear's 19/1 lecture); fixed the escalation's frame return (no LABEL_675 tail). |
| [probes/survival-week1.md](probes/survival-week1.md) | Normal Survival's first week: 10 order pickups, the 20,000 payment, the score dword. Port identical. |
| [probes/item-boom.md](probes/item-boom.md) | The player-driven boom news (FUN_00436623 steps 4 / 6): 9 Apple sales → the ALT boom (a price collapse); the day-start news update skipped after a payment. Port identical. |
| [probes/guild-unpause-equip-buzz.md](probes/guild-unpause-equip-buzz.md) | The pause over the Adventurers' Guild's bag and back: the hand snaps to the bag row at once (FUN_0045bd36); the dungeon pause's Equip page refuses a wrong-kind item (Beef Bowl for the weapon: FUN_00469414 → 3, SE 362, FUN_0046940c) — nothing changes. Port identical. |
| [probes/ohm-exhaust.md](probes/ohm-exhaust.md) | Ohm (67, L14 570) on Amber rush f34 kept alive through all 39 bees: rush cap 8 (IDA `sub_422ED6` reads the dungeon dword only; task predicates fixed), slowdowns at the 9th / 17th (−200 / −400), gaps 120 / 320 / 520 + 4 per hit-stop, counter 40..43 with no spawn; charge 14..15 = AT·(1 + 0.1·lv)/2 − DEF/4; death sweep of the last bee at tick 60, freed tick 131. |
| [probes/bossrush-obsidian-ending.md](probes/bossrush-obsidian-ending.md) | Obsidian Boss Rush + the final payment + ending → Endless (`2f2f2203a65d` = `2e4388a2cfe2` + 1,000,000 pix, crafted lineage → `1aa94b103208`): d5 floors 38..45 live (55 L27 960, 70 L28 660, 54 ×2 L29 680, 61..63 L31 1080, 28 L32 1480, 67 ×2 L33 1140, 49 L34 780, 28 L36 1640 + 72 L36 820; table HP 55 = 150, 28 = 200, 49 = 100 / AT 35); 45F door object 0; best time 21915 into 0xb151; pause blocked ~125 ticks after a clear; payment 995,350 → 495,350, day 36, ending, Louie's True Card, save prompt picker opens on slot 001 (saved to 020), Endless card 2,461 ticks, control f57335. |
| [probes/bossrush-lapis.md](probes/bossrush-lapis.md) | Lapis Boss Rush from the Endless save (`1aa94b103208`, crafted lineage), **in progress**: d5 floors 46..57 cleared live (levels 42..53: 55 ×2 1410, 59 1440, 54 ×3 980, 67 ×3 1500, 23 ×3 1020, 55 ×3 1560, 71 1060, 41 2700, 49 ×2 1100, 60 1680, 64..66 1710, 69 1740) with live tables; the rat's eat rules (age ≥ 600, target-kill = no meal, blocked walk = stale power-up); 58F Arma golem (body 3540 / hands 1180, L54) unfinished at body 3078 (f129219). |
| [probes/bossrush-crystal.md](probes/bossrush-crystal.md) | Crystal Nightmare's own Boss Rush row (row 3) from `ea4fa3d6ec75` + `--loadout rush --keep-level` (`672d7fbd4b1b`, crafted), **in progress**: the row is the native mode `0x438b4d0 = −1` (floor 30, portals all next-floor, rest floor 59, levels `ftol((floor − 30)·1.7 + 50)` = 50..97, best time 0xb172 — boss-rush.md §1b); d5 30..54 cleared live (25 bosses, L50..90, HP = table·(1 + 0.2·lv) incl. Tielle 1320, jellyfish 1400, Arma 4700); 55F type-60 rat (L92, 2910) stopped at 1962 (f132500, `stop55`); `rd verify` MATCH on the partial movie. |
| [probes/lapis-100f-long-intro.md](probes/lapis-100f-long-intro.md) | Lapis Ruins 96F–100F from a CRAFTED save (`15b20dc3c34e` = `6809401b79a9` with `+0x2bcff` cleared): the 100F LONG intro run live — stages 0 → 1 → 2 → 3 → 4 → 8, events 12/8, 12/9, 12/13, 12/14, 12/15 (frames, full text + speakers, seen bytes `+0x2d678..7f`, `+0x2bcff` 0 → 1 with 12/15; 12/15 = captions, invisible to `rd text`), the golem (f27250..61463, ~120 hits, zero damage; the hover-landing hit at body tick ≈ 238), 12/16..12/20, CLEAR, day end, Save (`b50b09b8aa6e`). |

## Boot / init

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [parity-save-producer.md](parity-save-producer.md) | Byte-exact port↔retail `save.dat` compare, region-localized (now `tools/save_compare.py` + `tools/save_arena_map.json`, from `save_bank.h`). Caught 2 real port bugs: encyclopedia-store re-init across banks 1–99, the 0xb37d house-camera flag on continue; `occupied_playtime` = load-timing phase origin. | 2026-10-13 |
| [headless-savestates.md](headless-savestates.md) | rhost `--headless` (fake D3D8 / DirectMusic / DirectShow / DirectInput), in-process savestates, `--live` sessions + search: design, retail facts met, the equivalence evidence (100/100 vs real-device goldens, snap-check 99/99), speed profile. | 2026-11-10 |
| [winmain-and-bootstrap.md](winmain-and-bootstrap.md) | WinMain (`FUN_0047bfb3`) + engine bootstrap / subsystem init chain; PE entry, WndProc, window class. | 2026-05-22 |
| [imports-and-layout.md](imports-and-layout.md) | `recettear.unpacked.exe` static DLL imports + on-disk asset layout. | 2026-05-19 |
| [title-fade-out.md](title-fade-out.md) | Title → NEW GAME black fade-out mechanism (supersedes an earlier wrong PROGRESS entry). | 2026-05-22 |
| [title-settings-submenu.md](title-settings-submenu.md) | Title "Options" submenu (`DAT_09643524==2`) inside `FUN_0049a59e`; volume-slider state machine. | 2026-05-21 |

## Scene-1 render (the Mt. Everest ladder)

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [scene1-render.md](scene1-render.md) | C7+ master ladder doc — scene-1 render path end-to-end; chip plan. | 2026-05-23 |
| [scene1-postload-init.md](scene1-postload-init.md) | `FUN_00436f97` (Cf.0) — INGAME state-entry init; BSS reset + spawn + 200-iter ambient loop. | 2026-05-23 |
| [scene1-camera-helpers.md](scene1-camera-helpers.md) | `FUN_00441c3e` + `FUN_004424e7` + `FUN_0040120c` (Cc) — camera pose / view-matrix builder; eye-vs-lookat erratum. | 2026-05-23 |
| [scene1-spawn.md](scene1-spawn.md) | `FUN_00447f4f` (C8i) — table-A particle spawn API (~134 per-type handlers, landed). | 2026-05-23 |
| [scene1-particles-tick.md](scene1-particles-tick.md) | `FUN_0040fb3a` (C8h) — table-A per-tick particle integrator. | 2026-05-23 |
| [scene1-per-frame-open.md](scene1-per-frame-open.md) | `FUN_00414929` (PFO) — per-frame open; table-A/B tick bodies + allocators + template tables. | 2026-05-25 |
| [scene1-overlay-dispatcher.md](scene1-overlay-dispatcher.md) | `FUN_00414ee2` (O.1) — 2D screen-space overlay particle dispatcher (sparkle/smoke/hud). | 2026-05-24 |
| [scene1-record-populators.md](scene1-record-populators.md) | Table-B / table-C render-record populators survey; base addrs + sentinels + count globals. | 2026-05-24 |
| [scene1-table-b-allocators.md](scene1-table-b-allocators.md) | `FUN_0044376a` + `FUN_00445a8c` (C8j) — table-B initial-state allocators. | 2026-05-24 |
| [scene1-records-b-tick.md](scene1-records-b-tick.md) | `FUN_0043ae20` — 25.7 KB table-B per-frame integrator (consumer of the C8j allocators); §Wall-tail fall-through: which type bodies fall into LAB_00440dc1 (all but 9 conditional `continue`s). | 2026-11-11 |
| [scene1-records-b-state-machine.md](scene1-records-b-state-machine.md) | `FUN_0043865e` — per-record combat state machine (NPC scan, attack input, damage, knockback, hit FX). | 2026-05-25 |
| [scene1-walker.md](scene1-walker.md) | `FUN_0040a765` (C7i) — scene-1 HUD walker survey; corrects the "3D mesh walker" mislabel + the Pass-7 "chr render" mislabel (it's shop menus). | 2026-05-23 |
| [scene1-chr-walker.md](scene1-chr-walker.md) | `FUN_004176ff` (30 KB, `scene1_walk_chr_TODO`) — character/entity 3D record walker. **⚠️ premise corrected by Cchr.0** — it does NOT draw the player; see scene1-char-sprite-trace.md. | 2026-05-29 |
| [scene1-char-sprite-trace.md](scene1-char-sprite-trace.md) | Cchr.0 + **Cchr.1 (RESOLVED)** retail Frida traces. Cchr.0: records_b + people empty, records_a only particles. Cchr.1 (`--quad-hist` world-transform trace): player/Tear/object sprites = world billboards from **`FUN_0045a56f`** (sprite-sheet→multi-quad→DrawPrimitiveUP), walker **`FUN_00456f56`/`FUN_0045672a`** reading the **`DAT_056da1b8` actor table** (player pos = `g_player_pos`), dispatched from `scene1_render_meshes`. Names the Cchr.2 port path. | 2026-05-29 |
| [scene1-char-sprite-render.md](scene1-char-sprite-render.md) | **Cchr.2** port dependency map + chip ladder for the `FUN_0045a56f` character sprite subsystem. Descriptor block layout (`.idx` grammar resolved), actor sprite-state struct, the leaf frame-LUT stride open question, two MVP strategies. **Cchr.2a LANDED**: `chr_sprite_meta.{c,h}` (formdata + `.idx` descriptor loaders). | 2026-05-29 |
| [scene1-bg-npc.md](scene1-bg-npc.md) | **Background-window NPCs** (`scene1_bg_npc`, formerly misnamed "ambient motes") — 6 townsfolk drifting past the shop's back window. `FUN_0046f2a3` sim + `FUN_0046f648` dark contact-shadow + **`FUN_0046f737` bright character sprite (LANDED 2026-06-02, un-stubbed)**. type→sheet `DAT_005c7ce0[type*2]` → chr{10,35..39}. User-verified rendering. | 2026-06-02 |
| [freeroam-rng-consumption.md](freeroam-rng-consumption.md) | **Free-roam RNG-consumption desync + the missing ambient emitter** (2026-06-04). `phase_probe.py --drill` localized: port under-consumes ~40 LCG/window via `scene1_spawn` → a missing **every-16-frame ambient emitter** (Lead A). Also: Recette (player) anim not phase-normalized (Lead B). **Open leads / next-session pickup.** | 2026-06-04 |
| [conversation-pose-driver.md](conversation-pose-driver.md) | **`FUN_0048407f` conversation-pose branch + the talk-event flag `DAT_0450f470`** — RE'd. During iv1_2 (any face-to-face talk) the HOUSE freeroam actors strike conversation poses: Recette anim **6** (「ティアの話を聞くよ」, frames 38/39 = look-up + **blink**), Tear anim **4** (「ルセットと会話」), facing each other. Port spec for the `intro-iv2-gap` gap (`opening-prologue.md` #4). | 2026-06-02 |
| [scene1-walker-pass-init.md](scene1-walker-pass-init.md) | `FUN_00457714` — per-NPC mesh walker = the HOUSE shop_table furniture renderer (relabel of pass-init stub). | 2026-05-26 |
| [scene1-house-render-gaps.md](scene1-house-render-gaps.md) | HOUSE render diffs vs retail post-PII.3c — floor/wall/rug textures (fixed) + god-ray/blinds lighting gaps (deferred to a scene-1 lighting chip). | 2026-05-29 |
| [study-toggles.md](study-toggles.md) | Filming tool (not RE): SHIFT+1..6 / `--study-off` kill-switches for the six HOUSE lighting tricks (mod2x/keylight/ambient/fog/hikari/blob), `src/study_toggles.{c,h}` — hook-site map + measured verification. | 2026-07-06 |
| [scene1-wide-followup.md](scene1-wide-followup.md) | `FUN_004161c7` — wide-frustum followup draw (z_far=2000) survey. | 2026-05-23 |
| [scene1-leaf-chain.md](scene1-leaf-chain.md) | Mesh-emit leaf chain (5 fns in `FUN_00459dfd`); flat-`mesh_t` adapter notes. | 2026-05-23 |
| [scene1-people-table.md](scene1-people-table.md) | `DAT_0076bd54` — 128-entry × 2980 B in-shop "people"/NPC table layout. | 2026-05-23 |
| [sim-step-a-dispatch.md](sim-step-a-dispatch.md) | `FUN_004536cb` / `sim_step_a` — full survey + chip ladder for the INGAME sim caller. | 2026-05-23 |

## Town / world map (mode 8)

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [town-map-RE.md](town-map-RE.md) | **Shop-exit → TOWN/WORLD MAP transition + the mode-8 scene** (Phase 0). Door-Z exit chain in `house_update` (`all.c:87637`) → tile-dissolve fade `FUN_004526f5` → `DAT_0438b1c0=8`. World map: preload `FUN_004735ad` (slot-10 worldmap day/eve/night + mappoint), init `FUN_0049de20` (7 dests + **tutorial state gating `DAT_09643588[]`**), sim `FUN_0049e163`, cursor-nav `FUN_0049dfc1` (3×5 grid), render `FUN_0049e3a3`. Dest→mode table; loader arg-2 = load-step count (not a map id). Chip plan T1–T5. | 2026-06-07 |
| [merchant-guild-RE.md](merchant-guild-RE.md) | **Merchant's Guild = engine mode 6 (Market), fully stubbed in the port** (cyan/blank). World-map dest 3 → `FUN_00490e16(0)` → mode 6. First-visit cutscene = per-location event tick `FUN_004922c0` → `FUN_0044ba2c(1,3,1)` → `iv/iv1_3.ivt` (run via `scene1_intro_dialogue_start_single(1,3)`), gated by first-visit flag `DAT_0450f3f4` (working-arena `0x2bc5c`). Render chain `FUN_00490e35`→`FUN_00494a73` (561 B 2D bg). Incremental port plan + the planned bread/Tear follow-on cutscenes. | 2026-06-10 |
| [adventurers-guild-RE.md](adventurers-guild-RE.md) | **Adventurers' Guild = engine mode 11** (world-map dest 6). Loader `FUN_0045bdc2` + `FUN_00473874`, update `FUN_0045c051` (intro `FUN_0044c7c3` iv2_7/iv2_8, adventurer list, bag + item picker, dungeon map, floor list, depart/leave commit), render `FUN_0045cc85`/`FUN_0045cc1e`, party list `FUN_0047f1ce`, fee table `DAT_005cc680`. Ported 2026-10-14; `buy-from-seller-guild-intro` save- and RNG-identical. | 2026-10-14 |

## Input / UI

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [day-close-RE.md](day-close-RE.md) | Shop day close: the 4th-period arms (cs leave / town return), FUN_0048526d in-scene re-layout, the b4e0 night exchange + actor-say bubbles (FUN_00485861 / FUN_00406159), the day-end summary entry (pause action 1). | 2026-10-14 |
| [vending-machines-RE.md](vending-machines-RE.md) | Vending machines: a table whose flag dword (work +0x2c750) holds a vender handle; placing (cc04 1 confirm's 5201..5299 arm, level / count limits), the machine's menu (cc04 2: Revert To Table, Done Stocking, 6 / 8 / 10 slots), stock list, render, the 0x438b67c quirk. |
| [shop-counter-menu-RE.md](shop-counter-menu-RE.md) | Counter Z → "Open Store / Cancel" menu (cc08 0 → 10 → 4, FUN_004850fe lists, b74c/b754 timing, render, "Opening the shop…" help band); root cause of the port's early shop entry. | 2026-10-14 |
| [esc-skip-event.md](esc-skip-event.md) | Context-sensitive ESC dispatch (WndProc `FUN_0047b2e7`) + the skip-event yes/no prompt (`FUN_00453384`/`454191`); pause-menu gate. Phase A (dispatch) landed. | 2026-06-02 |

## Audio

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [audio-backend.md](audio-backend.md) | DirectMusic 8 backend — init, BGM track-swap, sin-curve volume fade, per-tick fade animation. | 2026-05-21 |
| [audio-trace-diff.md](audio-trace-diff.md) | Detecting sound divergences from traces — port/retail audio.jsonl schema, the three sound-trigger hooks, `tools/audio_diff.py` (identity+count, phase/load-skew-immune). | 2026-06-10 |

## Loaders / formats

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [tables-loader.md](tables-loader.md) | `FUN_00475270` (`tables_load_all`) — outer "index file" gameplay-table dispatcher. | 2026-05-20 |
| [item-table.md](item-table.md) | `data/item.txt` parser — the ~600-record master item table; gates the item-name resolver. | 2026-05-20 |
| [texture-loader.md](texture-loader.md) | `FUN_0047193c` — texture loader; disk/storage lookup + BMP/TGA dispatch. | 2026-05-20 |
| [mesh-loader.md](mesh-loader.md) | `xfile/*.x` mesh loader (`FUN_00472836` chain) — parser + build + D3D8 upload + orchestrator. | 2026-05-23 |
| [skinned-mesh-render.md](skinned-mesh-render.md) | Skinned models: D3DINDEXEDVS with eight runtime-assembled vs.1.1 shaders (unlit = vertex colour, lit = colour · N·L); the port plan (CPU skinning). | 2026-10-17 |
| [save-working-arena.md](save-working-arena.md) | The TWO save arenas: disk-mirror (`save_bank.c`) vs live **working** arena (`save_work.c`, `DAT_044e3798`). `FUN_00490259` per-slot load, the title→load→game flow, the continue-picker dispatch. The "load a save" data path. | 2026-06-04 |
| [shop-item-display-RE-status.md](shop-item-display-RE-status.md) | **Items-on-display renderer — MAPPED** (porting spec). FUN_00415fab (per-item billboard) + FUN_004161c7 (15×20 grid driver) + display grid @ working-bank dword 0x4e26. Found via d3d-trace caller analysis. Read before attempting the shop merchandise render. | 2026-06-04 |
| [roster-scan-RE.md](roster-scan-RE.md) | **Customer roster-scan — MAPPED** (porting spec for `PORT-DEBT(cs-roster-scan)`). FUN_0045edaa WHO-walks-in + WHAT-item; branch structure, the DATA-DEPENDENT rng-draw sequence (shuffle FUN_0045e505, 1-100 rejection loop), helper deps, output tables. **Answers the closeness/decoration question:** decoration→customer weight (FUN_0045e55c, live-confirmed) + closeness `DAT_045109a8`→budget/item-quality (FUN_0045ecc0/e80f) — NEW vs §22. | 2026-07-10 |

## Diff / trace harness

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [pure-function-diff.md](pure-function-diff.md) | Phase D.1 — pure-function differential testing port-vs-retail via Frida + ctypes. | 2026-05-26 |
| [d3d-trace.md](d3d-trace.md) | Phase D.4 — Frida-side `IDirect3DDevice8` vtable state-trace → JSONL. | 2026-05-26 |
| [render-diff.md](render-diff.md) | Phase D.6 — render-diff orchestrator; surfaces per-frame state-trace divergences. | 2026-05-26 |
| [phase-state-census.md](phase-state-census.md) | Audit T3 — `phase_census.py` enumerates ALL load-timing-dependent state ({memsnap} two timing-shifted runs); HOUSE result + the pin-completeness gate. | 2026-06-09 |

## Parity evidence compiler (retired 2026-10-13)

The 2026-07 EP/GX/ST proof-compiler findings (M0 review, EP-06/07/08, pixels/state
producers, GX-00/03/06) were removed with their tooling; recover them via
`../archive/2026-10-13-agent-era-cleanup.md`. The save compare survives above.

## Misc / reference

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [engine-quirks.md](engine-quirks.md) | Curated tour of weird/charming engine behaviours found while RE-ing "Azumanga". | 2026-05-25 |
| [cross-references.md](cross-references.md) | The three prior Recettear RE projects cloned as siblings; what each contributes (archive format etc.). | 2026-05-19 |
