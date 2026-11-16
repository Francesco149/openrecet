# Start saves of the agent probes: lineage and how to rebuild them

Save blobs live in `tests/scenarios/_saves/<sha256>.sav.gz` — user data, gitignored, never pushed. Everything below
is DERIVED from two played saves and can be made again from them: a crafted save by its `craft_pix_save.py` line
(same input → same sha), a probe's save by replaying its scenario (`./rd new X --from SCENARIO@FRAME`, then
`./rd endsave X`; `--from end:SCENARIO` for the last one). A task names its start as `start: save:<sha12>`.

Crafted states are not states the game produced (a level without the sales that earn it, gold from nowhere). Every
probe report says which it used. Played saves from the owner's free-play capture replace them as they arrive.

## Played (the roots)

| sha12 | what | from |
|---|---|---|
| `17264a73…` | day 14 (HUD 15), level dword 5, 9,086 pix, slot 18 | `end:dungeon3-run2-save` (recorded play) |
| `a23d9e447eb4` | day 22 night, level dword 6, 9.69 M pix, slot 20 | `end:dungeon3-deep` (recorded play; a crafted-gold start) |

## Crafted (verified: each differs from its base in exactly the dwords named + the checksum)

| sha12 | base | `tools/craft_pix_save.py --base <base>` … | used by |
|---|---|---|---|
| `1a5a45e631b4` | `17264a73` | `--set 0xb100=9` (merchant level 10 on the HUD) | tear-orders-l10, move-tables-l10, tear-menu-pages |
| `175794a5f61c` | `17264a73` | `--gold 9999999 --set 0xb100=13` | decor-all |
| `a8fac0798453` | `175794a5f61c` | `--add-item 5002 5003 5102 5103 5402 5403 5302 5303` (wall / floor / carpet / counter variants) | decor-variants |
| `4c1259b71fd7` | `17264a73` | `--gold 500000` | payday-week2 |
| `2afe7fb42617` | `64d43650865f` | `--set 0xb100=15` (HUD 16) | vending-level16, change-tables-l16, vender-sales-l16 (through the scenario) |
| `352c146ab865` | `2afe7fb42617` | `--set 0xb100=16` (HUD 17) | item-storage-l17 |
| `b4766a99d74b` | `78c391141547` | `--set 0xaf3d=0x00010101` (bank byte `+0x2bcf7` = 0: event 12/4 not yet seen) | lapis-60f-event-12-4 |
| `15b20dc3c34e` | `6809401b79a9` | `--set 0xaf3f=0` (bank byte `+0x2bcff` = 0: Lapis 100F's long intro not yet seen) | lapis-100f-long-intro |
| `2f2f2203a65d` | `2e4388a2cfe2` | `--gold 1000000` (the day-35 final payment can be paid → the ending → Endless) | bossrush-obsidian |
| `67e22cc643bf` | `89190665bb8f` | `--gold 900000` | ending-week5-paid |

## Boss-test loadouts (crafted gear presets)

`tools/craft_pix_save.py --base <save> --loadout early|mid|late|rush [--keep-level]` (table + sources:
`tools/craft_loadouts.py`; `--list-loadouts` prints hits / worst hit per boss). It writes Louie's level dword (0xb3ac,
+ the AT DF MA MD it gives), his 5 equipped slots `id << 6 | 0x20` (record dwords +1..+5 and the copy +6..+10) and the
preset's Market food into the inventory (pack it at the guild). 0x20 = his own gear: it stays on him after a trip (the
state a player reaches by selling him the gear in the shop); bag gear equipped at "Equip items in bag?" goes back to the
bag on return. **Crafted:** say so in the task; real items (drops / guild stock / fusion), +0, at a level the band sees.

| preset | Louie | gear | ATK / DEF / MATK / MDEF | for | biggest boss |
|---|---|---|---|---|---|
| early | L15 | Steel Sword, Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots | 44 / 57 / 10 / 23 | Jade Way, Amber Garden, rush d5 30..37 | crabs 20F ~38 hits; crown power gate held |
| mid | L32 | Breaker Sword, Silver Armor, Spiked Armband, Silver Helm, Hard Boots | 84 / 105 / 18 / 44 | Obsidian 1..50F, rush 38..45, crown trios (Lapis 25F, rush 50F) | Griff 50F ~36, rush 45F ~44 |
| late | L48 | Holy Sword, Guardian Armor, Superarm Band, Alleus, Battle Boots | 169 / 215 / 48 / 121 | Lapis 1..100F, Crystal 1..30F | Arma golem 100F ~36 (break 490 vs worst sword 324) |
| rush | L48 | Holy Sword, Legendary Armor, Superarm Band, Legendary Helm, Iron Sandals | 181 / 278 / 66 / 129 | Lapis Boss Rush d5 46..58 | Arma golem 58F ~40 (break 590, red jellyfish third 353 vs worst sword 349) |

Use the sword. The spin (`B`, ×2) can pass the smaller arm intervals.

| sha12 | base | line | verified |
|---|---|---|---|
| `efa3dcf87875` | `1aa94b103208` (Endless, slot 020) | `--loadout rush` (level unchanged: 47) | 2026-11-15, run `loadout-rush` (Windows): guild panel lists the 5 items; Depart (d5 38F) → `s32(0x56db0ac..b8)` = 181 / 278 / 66 / 129, HP 232 |
| `672d7fbd4b1b` | `ea4fa3d6ec75` (`bossrush-lapis-r` end save, Endless, day 37) | `--loadout rush --keep-level` (L49 played) | 2026-11-16, run `bossrush-crystal`: 181 / 279 / 67 / 129; Crystal's own rush 30F..54F cleared, stalled 55F (L92 rat) — too weak past ~L90 |

## Made by probes (replay the scenario to the frame after the in-game Save, `rd endsave`)

| sha12 | state | scenario |
|---|---|---|
| `64d43650865f` | day 15 morning (week 3), 470,000 pix, slot 19 | payday-week2-paid (end) |
| `fe0c2efd7af8` | day 15, `stime` 2, 478,930 pix (after two shop sessions) | shop-session-week3 (end) |
| days 16–22: `a263daa7feb2` `0e58e63f7582` `3351a6e87a30` `36d4804b3bc0` `c184ba7bfdbd` `dcf2fe8486e1` `59e71a0bcb65` | one per morning; day 22 = after the week-3 payment | calendar-week3 (frames: its report) |
| days 23–29: `c5527143e016` `707a7abf3b06` `0309ff7e867a` `a58abeba37d2` `54bd221da407` `717ab3b7efd4` `89190665bb8f` | one per morning; day 29 = after the 200,000 payment, 198,930 pix | calendar-week4 (frames: its report) |
| days 30–35: `7a7206319a8c` `d4162d20b10f` `b3c5345004f6` `3bd18e8e9a50` `9431e13ba1f0` `7705df41d668` | one per morning (900,000 pix lineage) | ending-week5-paid |
| `5f038738193e` | day 35 morning (the payment day) with 198,930 pix | week5-gameover (f18035) |
| `67129e910529` | level 16, day 19, the scarf order's deadline passed | order-deadline-missed (end) |
| `197409ade35a` | week 3 morning, the guild's Talk topics read | guild-talk-topics (end) |
| `c062daff8848` | the Endless save (slot 001, "Cleared", day 36, 400,000 pix) | ending-week5-paid (the save prompt, f54483) |
| `71c8e3d35d69` `1e52630934d5` | Endless Mode, days 37 and 38 (mornings) | endless-first-days (f7043, end) |
| `f3dd0ba9294b` | New Game + day 1 (items + level kept, 1,000 pix) | newgame-plus (end) |
| `099af5b31445` | Normal Survival, the first morning (day dword 1, slot 007, LOOP 2) | survival-start (end) |
| `44e9baea3987` | level 16, a placed vender with 4/6 stock, an order outstanding | vender-sales-l16 (end) |
| `435d876bf8c8` | the same after revert + selling the machine | vender-revert-sell (end) |
| `dea37b7a7519` | level 16, Barrel tables, Wooden + Legendary in the bag | change-tables-l16 (end) |
| `cbb3056eeb1e` | level 17 (25-slot dungeon bag), week 3 | item-storage-l17 (end) |
| `1aa94b103208` | Endless Mode, first shop control (day dword 36, 495,350 pix, slot 020 = the ending's Endless save; crafted lineage `2f2f2203a65d`) | bossrush-obsidian-ending (end) |

Check a lineage: the bank is `HEADER 0x0b10 + slot·0x2dfc8` in the unpacked save, the newest slot in header dword
7; gold dword 3, day 0xb0fb, `stime` 0xb0fc, merchant level 0xb100, checksum 0xb7f1 (`tools/save_arena_map.json`).
