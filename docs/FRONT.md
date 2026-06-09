<!--
  The ONE hand-edited status block.  tools/gen_port_ledger.py injects everything
  below the marker line verbatim into docs/STATUS.md's "Current front" section, so
  STATUS can never drift from reality.  Update THIS when the active front moves;
  keep it short (a 60-second read).  Everything else in STATUS is derived from code.

  RULES (the 2026-06-09 cleanup): keep ONLY open/forward-looking items here. When
  an item RESOLVES, move its story to PROGRESS.md / the findings doc in the same
  edit — do not let resolved arcs accumulate (the file had grown to 743 lines;
  that snapshot lives in archive/FRONT-2026-06-09-full.md, where all pre-cleanup
  arc history — world-map backlog, dialogue fixes, load-arc, shop-display chips —
  can still be read).
-->
<!-- FRONT:BEGIN -->
- **Phase:** frame-by-frame 1:1 parity sweep along the player path (title →
  prologue → HOUSE → shop loop → world map → dungeon). Strategy + tooling roadmap:
  **`audits/2026-06-09-methodology-audit.md`** (settled verdicts — behavioral-vs-
  byte-exact CLOSED, x87 invariant, T1–T12 tooling roadmap, milestone-ladder KPI).
  Read it before re-litigating strategy or building new parity tooling.
- **ACTIVE ARC → ITEM-DISPLAY interaction flow** on trace-studio session
  **`item-display-2`** (`http://localhost:8778/?session=item-display-2`; load slot 2
  → place 3 items → 2 Tear tutorial dialogues; pinned + call-traced). Landed so far:
  C3a slot-glow, C3b item tooltip, both tutorial dialogues frame-exact (anchors Δ=0
  incl. the CONV_POSE_BLINK cadence fix `a8269f6`), text-reveal gradient. Full RE +
  history: `findings/shop-display-menu-RE.md`. **Open follow-ups** (mechanisms in
  that doc's "Open follow-ups"):
  1. **bg-NPC desync after dialogue start** (shared-LCG / standee-shake — re-check
     now that the cadence fix no longer shifts per-frame RNG).
  2. **placed-item ids wrong on the place path** (`FUN_00469a9f` returns
     64/64064/256512 — the cc04 confirm was written for `sel==-1` removal only).
  3. **missing bread tooltip** during dialogue (retail ord 854).
  4. **dialogue box/portrait pixel-parity** polish.
  **Interaction-flow gaps (B–F):** (B) "What will you place?" placement-MENU prompt
  @f391 · (C) menu panel slide-in anim @f122 · (D) selected-row flash @f172 ·
  (E) placement-dust desync @f272 · (F) carry-pose/held-item — the verdict's
  px/py/pz DRIFT (triage names it: `house_update.px` DRIFT @202); held item
  red-vs-gold. **Tooling fix owed:** the recorder's `save_capture` overwrites
  `<name>.save.bin` unconditionally (clobbered this session's boot save twice).
- **NEXT ARCS:** finish item-display gaps → **merchant's guild screen** → town
  scenes off the world map (world-map backlog itself CLOSED 2026-06-08, bit-clean
  f16→638). Trace-studio v2 **Phase 5** (New-Game cross-replay: retail intro-video
  force-skip D4 + the prologue mid-load actor-spawn gap,
  `findings/conversation-pose-driver.md`) stays queued — hardest, last.
- **Deferred (polish pass):** faint ambient particle dots (user ref-crops
  2026-06-05); next-line "book" arrow anim frame (draw from per-script-reset
  `rt->blink`, add to `{phasepin}`).
- **Tooling (2026-06-09 cleanup, audit T1/T2/T8/T11 — all landed):**
  `trace_studio triage <session>` = one-command divergence report (diff curve
  gt8 metric → first/worst ordinal → state row → verdict → field-timeline);
  working-trace **lint + canonical auto-pin** at capture (pins are now mechanism,
  not prose — `--no-auto-pin` for deliberate unpinned studies); session
  **coordinate contract unified** (frames on BOTH sides label-named, diff
  label-keyed, state ordinal-keyed — the C3a abs-vs-ordinal trap is dead); CI
  **x87 FP guard** (`tools/ci/no_sse_math.py`). Sessions captured before today
  lack the `gt8` diff stat until recaptured.
- **Authoritative parity facts:** `findings/confirmed-parity-ledger.md`. A tooling
  "divergence" on a human-confirmed-1:1 item is a lead to investigate, NOT an
  assumed regression.
<!-- FRONT:END -->
