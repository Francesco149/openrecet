<!--
  The ONE hand-edited status block.  tools/gen_port_ledger.py injects everything
  below the marker line verbatim into docs/STATUS.md's "Current front" section, so
  STATUS can never drift from reality.  Update THIS when the active front moves;
  keep it short (a 60-second read).  Everything else in STATUS is derived from code.

  RULES: keep ONLY open/forward-looking items here. When an item RESOLVES, move its
  story to PROGRESS.md / the findings doc in the same edit — do not let resolved arcs
  accumulate. Full pre-cleanup snapshots:
  - archive/FRONT-2026-06-09-full.md (world-map backlog, dialogue fixes, load-arc, shop-display chips)
  - archive/FRONT-2026-07-01-full.md (customer-service/haggle, title/pause/guild/prologue arcs, v3 build-out)
  - archive/FRONT-2026-08-22-full.md (evidence compiler Wave-0, GX-00→06, ST-00→05, day-2 transition arcs)
  Code-tagged PORT-DEBT lives in docs/port-debt.md (derived).
-->
<!-- FRONT:BEGIN -->
- **★ NEXT MICRO-TASK:** Customer service unported kind machines (`PORT-DEBT(cs-other-kinds)`: buy/about/wanted-list `FUN_00460eba`), or CV-03 dynamic block/edge coverage instrumentation.

- **▶ ACTIVE ARC 1 — REVERSE-ENGINEERING & COVERAGE ATLAS (CV-01→CV-03).**
  - **✅ CV-01 / CV-02 LANDED (`tools/re_index.py`, `tools/test_re_index.py`):** Deterministic SQLite index (`docs/re-index.sqlite`) covering all 2,620 functions, 6,171 call edges, 10,639 global xrefs, 1,117 string xrefs. Fast subcommands: `info`, `text` (decompiled C with `-n`), `disasm` (objdump assembly), `callers`, `callees`, `xrefs`, `tree`, `unported-callees`, `search` (with `--code`), `stats`.
  - **★ NEXT:** **CV-03** dynamic block/edge coverage collection (Frida Stalker bitmap capture + coverage atlas).

- **▶ ACTIVE ARC 2 — CUSTOMER SERVICE & AUTONOMOUS DAY-2 SALE.**
  - **✅ LANDED:** Roster scan (`cs_roster_scan`, 1:1 verified vs golden), daily news list generator & trend classifier (`FUN_00436623`, `FUN_004361b2`, 1:1 verified), general display-grid item pick (`FUN_00461303`, `4c62e00`), adventurer equip upgrade (`FUN_00460b93`, `a1b37a8`), closeness serve-time deltas (`FUN_004658ab`, `edc2061`), sale fanfare & coin shower (1:1 bit-exact).
  - **★ NEXT:** `PORT-DEBT(cs-other-kinds)` — the buy (kind 1) and about (kind 2) customer state machines + `FUN_00460eba` reject wanted-list; `b53c` rank-up flash renderer.

- **▶ ACTIVE ARC 3 — PARITY EVIDENCE COMPILER (roadmap program).**
  - **✅ LANDED:** Wave-0 (EP-00→EP-08) complete. Save pillar (ST-00/ST-01), volatile state pillar (ST-02..05, Merkle roots + mutation consumer), pixels pillar, D3D8 capture census & opcode corpus (GX-00→GX-06) complete. First multi-pillar passing proof bundle on `house-pause-save-commit`.
  - **★ NEXT:** ST-05 capture platform (Frida post-write/TTD) + ST-06 scene-by-scene state-map expansion.
<!-- FRONT:END -->
