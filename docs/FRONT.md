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
- **★ NEXT MICRO-TASK:** ST-05 capture platform (Frida post-write/TTD) or ST-06 scene-by-scene state-map expansion (Arc 3).

- **▶ ACTIVE ARC 2 — AUTONOMOUS DAY-2 PLAY-THROUGH & SUBSYSTEM INTEGRATION (LANDED — ARC 2 COMPLETE).**
  - **✅ LANDED (`tests/test_day2_playthrough.c`, `tools/test_day2_autonomous.py`):** Complete autonomous Day-2 game lifecycle verified under host ASan/UBSan (10/10 tests) and Python test runner (4/4 tests). Validates: (1) Day-1 to Day-2 transition cascade (`iv1_5`..`iv1_8` -> `iv2_3` day advance -> `iv2_5`/`iv2_6`), (2) Day-2 morning news generation and market trend classification (`FUN_00436623`, `FUN_004361b2`), (3) display grid stocking and item setup (`FUN_00461303`), (4) Day-2 customer roster candidate scan (`cs_roster_scan`, `FUN_0045f2da`), (5) all 5 customer service kind machines (Kind 2 Sell/Upgrade `FUN_00460b93`, Kind 0 Buy `FUN_00465372`, Kind 3 Booking `FUN_004639f5`, Kind 4 Pickup/Reject `FUN_00463cfb`, `FUN_00460eba`, Kind 5 Chat `FUN_00464a26`), and (6) end-to-end scenario replay validation (`house-firstcust-cutscene-day2-full`, exit=0, 867 anchors across 16,382+ frames to `CONV_POSE_END`).

- **▶ ACTIVE ARC 3 — PARITY EVIDENCE COMPILER (roadmap program).**
  - **✅ LANDED:** Wave-0 (EP-00→EP-08) complete. Save pillar (ST-00/ST-01), volatile state pillar (ST-02..05, Merkle roots + mutation consumer), pixels pillar, D3D8 capture census & opcode corpus (GX-00→GX-06) complete. First multi-pillar passing proof bundle on `house-pause-save-commit`.
  - **★ NEXT:** ST-05 capture platform (Frida post-write/TTD) + ST-06 scene-by-scene state-map expansion.
<!-- FRONT:END -->
