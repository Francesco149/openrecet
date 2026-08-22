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
- **★ NEXT MICRO-TASK:** Autonomous day-2 play-through test (Arc 2), or ST-05 capture platform (Arc 3).

- **▶ ACTIVE ARC 1 — REVERSE-ENGINEERING & COVERAGE ATLAS (CV-01→CV-08 LANDED — ARC 1 COMPLETE).**
  - **✅ CV-01 / CV-02 LANDED (`tools/re_index.py`, `tools/test_re_index.py`):** Deterministic SQLite index (`docs/re-index.sqlite`) covering all 2,620 functions, 6,171 call edges, 10,639 global xrefs, 1,117 string xrefs. Fast subcommands: `info`, `text` (decompiled C with `-n`), `disasm` (objdump assembly), `callers`, `callees`, `xrefs`, `tree`, `unported-callees`, `search` (with `--code`), `stats`.
  - **✅ CV-03 / CV-04 / CV-05 / CV-06 LANDED (`tools/coverage_atlas.py`, `tools/test_coverage_atlas.py`, `tools/frida/openrecet-agent.js`, `tools/frida_capture.py`):** Dynamic basic block and edge coverage collection via Frida Stalker, SQLite coverage atlas, 9 semantic coverage dimensions (`functions`, `blocks`, `vm_operations`, `transitions`, `content_ids`, `assets`, `audio_ids`, `save_ops`, `boundary_outcomes`), scenario coverage declaration validation (`validate-scenario`, `audit-scenarios`), multi-scenario deltas, and CV-06 executed-but-unimplemented/branch-gap reporting.
  - **✅ CV-07 LANDED (`tools/coverage_atlas.py prioritize`, `tools/re_index.py prioritize`):** Multi-factor candidate experiment prioritizer (`CV-07-v1.0`) scoring 7 dimensions (new code potential, new semantics, graph distance from certified frontier, port readiness, proof deficit, cost efficiency, active front affinity) across functions, edges, semantics, and scenarios with human-readable explanations.
  - **✅ CV-08 LANDED (`tools/coverage_atlas.py calibrate`, `tools/re_index.py calibrate`):** Dynamic coverage truth calibration engine (`CV-08-v1.0`) evaluating 4 weighted factors (collector integrity, cross-collector call trace agreement, static CFG structural validity, repeat-run determinism), cataloging instrumentation blind spots (thunks, short blocks, indirect calls, exception handlers), and strictly enforcing the CV-08 invariant (gating global coverage percentage claims behind passed calibration with collection mode and confidence score annotations).
  - **✅ LANDED:** Roster scan (`cs_roster_scan`, 1:1 verified vs golden), daily news list generator & trend classifier (`FUN_00436623`, `FUN_004361b2`), general display-grid item pick (`FUN_00461303`), adventurer equip upgrade (`FUN_00460b93`), all 5 customer kind machines (`cs_buy_machine` `FUN_00465372`, `cs_request_machine` `FUN_00464af0`, `cs_advance_order_book_machine` `FUN_004639f5`, `cs_advance_order_pickup_machine` `FUN_00463cfb`, `cs_chat_machine` `FUN_00464a26`, `FUN_00460eba` reject restore; retired `PORT-DEBT(cs-other-kinds)`).
  - **★ NEXT:** Live autonomous day-2 play-through test.

- **▶ ACTIVE ARC 3 — PARITY EVIDENCE COMPILER (roadmap program).**
  - **✅ LANDED:** Wave-0 (EP-00→EP-08) complete. Save pillar (ST-00/ST-01), volatile state pillar (ST-02..05, Merkle roots + mutation consumer), pixels pillar, D3D8 capture census & opcode corpus (GX-00→GX-06) complete. First multi-pillar passing proof bundle on `house-pause-save-commit`.
  - **★ NEXT:** ST-05 capture platform (Frida post-write/TTD) + ST-06 scene-by-scene state-map expansion.
<!-- FRONT:END -->
