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
- FUN_0049511d / FUN_0049508c — Charme's skill states 5 / 6 (Charme leading, a skill used) → `tasks/charme-skills.md`
- FUN_00464a26 — the customer machine's `b5a8 == 5` arm (an order came due and was missed, the customer's visit)
- FUN_0045bd36 — the adventurers' guild's unpause hook (the pause opened in the guild with the item window up)
- FUN_0045e2dd / FUN_0045e353 — town mode 16 (no writer of DAT_0438b1c0 = 16 found yet; maybe dead)
- FUN_0046940c (display-menu confirm cancel), FUN_0048486f (an encyclopedia helper), FUN_00433fa6 / 00434198
  (party move's box overlap), FUN_0048a348 (the chest roll's miss log), FUN_0040c90e (spawn kind 0x24's -1 path),
  FUN_0045bd31, FUN_0047e711 (pause state 0xa), FUN_0046c039 (state 5, world map)
- FUN_0044b635 — a dungeon-flow stub (PORT-DEBT) that retail never executes in the corpus either
- dead in retail (ported for structure): FUN_0041ee24 / 0041edf1 / 004736bd / 00473760 (the staff roll, quirk 170),
  FUN_0040cf88 (BGM loop), font / input / init paths the harness does not exercise

Re-run: `rhost.py corpus --headless -j 8 --coverage <list>`; the per-scenario `coverage.txt` names the first frame.
