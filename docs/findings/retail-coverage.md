# Retail function coverage of the scenario corpus (2026-11-19)

**Method.** `rhost.py corpus --headless --coverage LIST` with LIST = every non-FLIRT function of
`functions_ida.tsv` (3,110): rhost arms a one-shot INT3 at each entry and logs the first frame it is hit
(`<run>/coverage.txt`). The INT3s change nothing retail does (house-idle ends on the same RNG state with and
without). 190 scenarios (batch `rhost-runs/20261119-125347-corpus`, Windows headless).

**Totals.** 1,271 functions hit, 1,839 never. Most unhit code sits at ≥ 0x4a0000 (the statically linked D3DX / CRT
that FLIRT does not flag). In the **game range (< 0x4a0000)**: ≈ 606 KB of ≈ 636 KB of code is executed by some
recording. Crossed with `docs/port-ledger.json`:

| game range | count | bytes |
|---|---:|---:|
| hit, ported / verified | 767 | 586 KB |
| hit, unported (static-init thunks 0x471xxx / 0x46bxxx, audio helpers 0x49a141.., 0x401000..0x404xxx library-like, render) | 69 | 5 KB |
| never hit, unported | 40 | 17 KB |
| never hit, **ported** (no recording verifies the port) | 39 | 11 KB |

**Never hit, unported — what they are.** Render-only (FUN_004547ab's callees 0x47e8a4, 0x451b07, 0x41ef91 the staff
roll), the debug paths (FUN_0041375a + its 0x412c73 / 0x413376 / 0x413898 / 0x413cbd subtree runs only when
DAT_073dddb4 != 0, FUN_0048670f; FUN_004518a3 is a debug menu's `v5 == 2` in FUN_004536cb), boot / window code
(0x401000, 0x4010b4, 0x404xxx), input config (0x47b255, 0x499e40 / 0x499f52). **No game-sim function outside the
debug paths is unreached.**

**Never hit, ported — coverage targets** (each is a probe task; ported, unverified):
- ✅ FUN_0049511d / FUN_0049508c — Charme's skill states 5 / 6 → `charme-skills` (identical, 2026-11-19)
- ✅ FUN_00464a26 — the customer machine's `b5a8 == 5` arm → `order-late-pickup` (identical, 2026-11-19)
- ✅ FUN_0045bd36 — the adventurers' guild's unpause hook → `guild-unpause-equip-buzz` (identical, 2026-11-19)
- FUN_0045e2dd / FUN_0045e353 — town mode 16 (no writer of DAT_0438b1c0 = 16 found yet; maybe dead)
- ✅ FUN_0046940c (display-menu confirm cancel → `guild-unpause-equip-buzz`), FUN_0048486f (an encyclopedia helper), FUN_00433fa6 / 00434198
  (party move's box overlap), FUN_0048a348 (the chest roll's miss log), FUN_0040c90e (spawn kind 0x24's -1 path),
  FUN_0045bd31
- scene states 5 and 10 look unreachable: FUN_0047e711 (state 10, an 8,360-tick sequence with `OPENCOUNT` debug text,
  then INGAME; its reset FUN_0047e709 has no caller) and FUN_0046c039 (state 5, a dialogue-only scene); no store of 5
  or 10 into DAT_0438b1c0 found in the disassembly — debug / leftover paths
- FUN_0044b635 — a dungeon-flow stub (PORT-DEBT) that retail never executes in the corpus either
- dead in retail (ported for structure): FUN_0041ee24 / 0041edf1 / 004736bd / 00473760 (the staff roll, quirk 170),
  FUN_0040cf88 (BGM loop), font / input / init paths the harness does not exercise

Re-run: `rhost.py corpus --headless -j 8 --coverage <list>`; the per-scenario `coverage.txt` names the first frame.

## Basic blocks (2026-11-19, later)

`tools/rhost/where.py blocks` lists the block starts of the game code by **recursive descent** from each function's
entry (30,183 blocks; a linear sweep also "decodes" the switch tables inside function ranges — an INT3 there is read as
data, never executed / restored, and two test runs diverged: `scene=15` game exit at f1343). With the descent list the
corpus (194 scenarios, batch `rhost-runs/20261119-135920-corpus`) ends every scenario on the same frame + RNG as the
plain run.

**24,844 / 30,183 blocks hit (82.3 %).** 772 ported functions have unhit blocks (4,201 in all); the most:
FUN_004176ff (render pass, 322), FUN_0043ae20 (records B tick, 258 — record types / states no recording spawns),
FUN_00475270 (the stage loader, 200), FUN_0040fb3a (particles, 147), FUN_00462403 (the customer master tick, 133),
FUN_0044376a (124), FUN_0045edaa (the customer roster, 114), FUN_0043a5d9 (dungeon objects, 68), FUN_00463cfb (orders, 64),
FUN_00461792 (the seller, 61), FUN_004986ee (61), FUN_0043865e (the record state machine, 53).

Query: `where.py FUN_x` (each block: how many scenarios hit it, the first scenario @ frame), `where.py FUN_x --unhit`
(only the unhit blocks with their first instruction), `where.py --top N`. Index: `/mnt/ephemeral/openrecet/coverage/
bb-latest.json.gz` (rebuild: `where.py blocks F`, `rhost.py corpus --headless -j 8 --coverage F`, `where.py index BATCH F`).

## Basic blocks, 2026-11-24 (216 scenarios)

After the day's probes (the shop story scenes, the order delivery, Euria, Survival, Arma's weapons, the charge attacks,
the item boom): **25,543 / 30,183 blocks hit (84.6 %)**, batch `rhost-runs/20261124-111453-corpus` (210 / 216 complete;
the 6 others are the legacy stalled / no-flat traces). The customer master tick `FUN_00462403` fell from 133 to 41 unhit
blocks, `FUN_0045edaa` 114 → 61, `FUN_00463cfb` 64 → under the top 25, `FUN_00461792` off the list. Top now (`where.py
--top`): render `FUN_004176ff` 298, stage loader `FUN_00475270` 200, records B `FUN_0043ae20` 174, particles `FUN_0040fb3a`
143, dungeon objects `FUN_0043a5d9` 68 / 114.

**Dead in retail: `FUN_0043a5d9`'s 68 unhit blocks** (code read 2026-11-24). Only `sub_44AD81` (direct or via
`sub_44B257`) writes an object's kind, every call passes a fixed kind, and the only kinds ever created are {0..6, 8}
(`sub_432473`: collision triangle types 8..11 → kinds 0..3, 16 → 8; floor flow / post-load: 4, 5, 6, 8). Kinds 7, 9..22
and 30 (the slides, spinners, the collapsing floor, fades, levers — leftovers, never placed) and the default case's
state-1 arm (kinds 0..3 never reach state 1: their hit counter is −1) are unreachable. The live kinds' blocks are all hit.

**Dead in retail: records / particles** (type-flow over the disasm + every spawn call's pushed type, 2026-11-24;
`FUN_0044376a` party allocator, `FUN_00445a8c` enemy allocator `(owner, type, flag)`, `FUN_004458c4` traps (0x2c / 0x8c
only), `FUN_00447f4f` particles — none is reached through a pointer):
- records B with no spawner (tick body and enemy branch): 0x23, 0x24, 0x27, 0x28, 0x29, 0x2e, 0x2f, 0x30, 0x31, 0x32,
  0x33, 0x34, 0x36, 0x38, 0x3a, 0x3b, 0x3c, 0x65; 0x25 (the mushroom king's spawn at 0x42bba1 needs `+0xa70 == 1`, but
  the spray state writes 4 every tick first); 0x2a's enemy branch; the enemy branches of 0x50 / 0x51 / 0xa5 / 0xa6 and the
  weapon arms inside 0xa0..0xa4;
- dead branches in live types: 0xe / 0x12 at 0x43e228 (AGE < 0), 0x84 phase 1 (PART_IDX never nonzero); float-equality /
  table-full corners 0x440930, 0x4409fa, 0x4478ee, 0x447cd4, 0x44acef;
- particles with no call site: 0x6..0x9, 0x12, 0x1a, 0x1b, 0x1d, 0x20, 0x22, 0x24, 0x34, 0x3b..0x3e, 0x40, 0x42, 0x4a..0x4c,
  0x50, 0x57, 0x59, 0x5a, 0x5d, 0x65..0x69, 0x6c, 0x6e, 0x71, 0x74, 0x76, 0x78, 0x79 and the default group; called only
  from dead code: 0x21, 0x35, 0x36, 0x52, 0x53, 0x54, 0x6d; 0x10 / 0x91 with count < 10 (every caller passes 12);
- **`FUN_0044b635` is dead** (its gate `DAT_0438bebc` is only ever zeroed or decremented).

Reachable, unrecorded (probe tasks 2026-11-24): Arma's finisher 0x9c → 0x9e (`arma-finisher`), Arma's weapon edges
(`arma-weapon-edges`), Nagi's dash hit / Elan's wall + hurt kills (`nagi-elan-edges`); also Caillou's finishing blows
(hit value < 10), Charme's skill 6 on a boss floor (0x53 lifetime 120), the Tielle-boss arrow guarded by Charme's counter,
attack records on stage objects of kind 0 / 2 / 3 (particles 0x15 / 0x16), the water drops (0x11, stage palette +0x1b38 == 0).

