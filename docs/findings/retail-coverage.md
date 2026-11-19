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
