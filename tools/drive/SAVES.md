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
| `67e22cc643bf` | `89190665bb8f` | `--gold 900000` | ending-week5-paid |

## Made by probes (replay the scenario to the frame after the in-game Save, `rd endsave`)

| sha12 | state | scenario |
|---|---|---|
| `64d43650865f` | day 15 morning (week 3), 470,000 pix, slot 19 | payday-week2-paid (end) |
| `fe0c2efd7af8` | day 15, `stime` 2, 478,930 pix (after two shop sessions) | shop-session-week3 (end) |
| days 16–22: `a263daa7feb2` `0e58e63f7582` `3351a6e87a30` `36d4804b3bc0` `c184ba7bfdbd` `dcf2fe8486e1` `59e71a0bcb65` | one per morning; day 22 = after the week-3 payment | calendar-week3 (frames: its report) |
| days 23–29: `c5527143e016` `707a7abf3b06` `0309ff7e867a` `a58abeba37d2` `54bd221da407` `717ab3b7efd4` `89190665bb8f` | one per morning; day 29 = after the 200,000 payment, 198,930 pix | calendar-week4 (frames: its report) |
| days 30–35: `7a7206319a8c` `d4162d20b10f` `b3c5345004f6` `3bd18e8e9a50` `9431e13ba1f0` `7705df41d668` | one per morning (900,000 pix lineage) | ending-week5-paid |
| `c062daff8848` | the Endless save (slot 001, "Cleared", day 36, 400,000 pix) | ending-week5-paid (the save prompt, f54483) |
| `71c8e3d35d69` `1e52630934d5` | Endless Mode, days 37 and 38 (mornings) | endless-first-days (f7043, end) |
| `f3dd0ba9294b` | New Game + day 1 (items + level kept, 1,000 pix) | newgame-plus (end) |
| `099af5b31445` | Normal Survival, the first morning (day dword 1, slot 007, LOOP 2) | survival-start (end) |
| `44e9baea3987` | level 16, a placed vender with 4/6 stock, an order outstanding | vender-sales-l16 (end) |
| `435d876bf8c8` | the same after revert + selling the machine | vender-revert-sell (end) |
| `dea37b7a7519` | level 16, Barrel tables, Wooden + Legendary in the bag | change-tables-l16 (end) |
| `cbb3056eeb1e` | level 17 (25-slot dungeon bag), week 3 | item-storage-l17 (end) |

Check a lineage: the bank is `HEADER 0x0b10 + slot·0x2dfc8` in the unpacked save, the newest slot in header dword
7; gold dword 3, day 0xb0fb, `stime` 0xb0fc, merchant level 0xb100, checksum 0xb7f1 (`tools/save_arena_map.json`).
