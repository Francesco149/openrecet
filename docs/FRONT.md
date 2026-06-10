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
  incl. the CONV_POSE_BLINK cadence fix `a8269f6`), text-reveal gradient, **the
  event-arm routing chip `843b6f1` (2026-06-09): busy frames now dispatch to
  FUN_004427d3 like retail — killed the dialogue-window rngcalls desync (+1375 →
  +31), the db054 menu-close off-by-one, and with them the phantom px/py DRIFT @202
  (gap F sim half) + the placement-dust desync (gap E); px/py/dust/db054/cbfc now
  ALIGNED/bit-exact on the verdict.** Full RE + history (incl. the ±31 residual
  boundary-frame breakdown): `findings/shop-display-menu-RE.md` "Open follow-ups".
  **2026-06-09 PM triage of the recapture also CLOSED two follow-ups without code:**
  placed-item ids (#4) were never wrong — correct raw `id<<6` dwords, placements
  pixel-identical at all 3 confirms; the bread tooltip (#5) is present on both
  sides through the dialogue (old "ord 854" pointer was dead pre-unification
  numbering). Evidence + frame refs: the RE doc's follow-ups #4/#5.
  **NPC desync through the dialogues: user-confirmed GONE 2026-06-10** (parity
  ledger entry; the scare was a stale ordinal-paired diff — fixed, see Tooling).
  **Gap B ("What will you place?" bubble) ✅ DONE 2026-06-10** (`36a8ab2`): NOT a
  string — all 3 prompts are BAKED item_win sprites drawn by FUN_0046b00a at
  dst(menu_x-128,48,191,63), sliding with the panel; flag DAT_0734b990 set by the
  arm (occupied cell → "Exchange with what?", Vender highlight → "Place Vending
  Machine"). 0-1px at labels 439/588-594. RE: `findings/shop-display-menu-RE.md`.
  **Carry pose (queue #2) ✅ DONE 2026-06-10** (`4bc8a0b`): db048==0xc state ported
  (r==3 arm + 26-frame free-roam hold + interaction/impulse gates); carry-window
  frames drop ~2,290→2-65 px each, pose pixel-exact, residue = sparkle phase.
  **Standee horizontal offset (#3) + portrait-outline polish (#7) + NPC note @1844
  ✅ DIAGNOSED 2026-06-10 — all ONE cause, the iv1_6 LOAD-BRACKET length; no logic
  gap.** Slide logic measured 1:1 (template-matched trajectories: same path, speed
  8px/f entry / 16px/f exit, ±1 frame vs script start on both sides); the 4-label
  lead = retail's inter-dialogue bracket 5f (async CreateThread worker = wall-time,
  quirk #119) vs port's 2f + a 1-frame iv1_5-tail slip. Worst frame 1792, the
  1735-81 bursts, note @1448, note @1844 walker offset: all this seam. Settled
  frames are gt8≈2 ⇒ no filtering/sub-pixel residue (#7 dissolved). Don't tune
  `IVE_TUT_LOAD_FRAMES` to 5 (one-run wall-time). Full measurement + corrected
  label↔frame mapping: `findings/shop-display-menu-RE.md` follow-up #8.
  **Remaining (user-listed 2026-06-10 + triage), the next-session queue:**
  4. **Dialogue text gradient-to-transparent STILL not visible** (user re-flag).
     Commit `a278101` is the right RE (per-row alpha = clamp(budget·0.2, 1.0),
     objdump-verified) and the alpha provably flows into the glyph quad diffuse
     (`font_draw_text` → `render_quad_add(…, argb)`), so do NOT blind-revert —
     the prime suspect is the texture-stage ALPHA pipeline at the glyph draw
     eating vertex alpha (ALPHAOP/ALPHAARG inherited state — the same class as
     the white-UI COLORARG leak). Drill with `d3d_state_at_draw.py` on a
     reveal-window glyph draw, both sides; check ALPHABLENDENABLE/SRCBLEND too.
     If retail shows a per-CHAR edge gradient (not a flat row fade), re-read
     FUN_0047d464's inner loop before trusting the per-row conclusion.
  5. **Item-Details sub-view** (`pressed & 0x40` path, all.c:65451, PORT-DEBT) —
     label 181: retail shows the narrow-right detail panel, port the plain
     wide-bottom description. Plus **description-panel line layout** (price /
     "Number possessed" X positions) · (C) slide-in check · (D) row flash.
  6. **menu-boundary residuals:** rngcalls ±31 (one wing emit per %4==0-frozen
     pause boundary + load-bracket seams) · companion cx/cz/canim/cframe + pcnt
     micro-DRIFT around open/close frames · retail menu-window consumption is the
     WING through the hooked thunk (`0xcf05d33`), NOT an unknown menu consumer ·
     hand-cursor snap drawn 1 frame EARLIER by retail at menu open (label 587,
     ~109px, found verifying gap B) · **menu-close camera pan-out whole-frame
     offset** (label 441, ~160k px>8 — pre-exists the carry chip, attributed by
     stash-rebuild-recapture; the scene shifts ⇒ camera, not UI).
  **Tooling fix owed:** the recorder's `save_capture` overwrites `<name>.save.bin`
  unconditionally (clobbered this session's boot save twice). Also: session
  kept-count mismatch (port 1845 vs retail 1842) — **RESOLVED 2026-06-10 as the
  inter-dialogue load bracket** (labels 1379-84 = port abs 1824-26 bracket vs
  retail's 5f bracket; the earlier "mid-dialogue-1, NOT the load brackets" reading
  used a wrong label↔frame mapping — truth: label = port_abs − 445). Expected-
  benign on any tutorial-load crossing; triage's kept_count_mismatch PROBLEM on
  this session is accepted-known. (`build_diff` ordinal-pairing bug
  fixed 2026-06-10: it ghosted the bg-NPC movers on every post-seam diff frame
  while the same-label sides were 1:1 — diffs now pair by label with honest
  unmatched-label holes; pre-unification sessions fall back to ordinal.)
- **NEXT ARCS:** finish item-display gaps → **merchant's guild screen** → town
  scenes off the world map (world-map backlog itself CLOSED 2026-06-08, bit-clean
  f16→638). Trace-studio v2 **Phase 5** (New-Game cross-replay: retail intro-video
  force-skip D4 + the prologue mid-load actor-spawn gap,
  `findings/conversation-pose-driver.md`) stays queued — hardest, last.
- **Deferred (polish pass):** faint ambient particle dots (user ref-crops
  2026-06-05); next-line "book" arrow anim frame (draw from per-script-reset
  `rt->blink`, add to `{phasepin}`).
- **Tooling (2026-06-09 cleanup, audit T1/T2/T3/T8/T11 — all landed):**
  `trace_studio triage <session>` = one-command divergence report (diff curve
  gt8 metric → first/worst ordinal → state row → verdict → field-timeline);
  working-trace **lint + canonical auto-pin** at capture (pins are now mechanism,
  not prose — `--no-auto-pin` for deliberate unpinned studies); session
  **coordinate contract unified** (frames on BOTH sides label-named, diff
  label-keyed, state ordinal-keyed — the C3a abs-vs-ordinal trap is dead); CI
  **x87 FP guard** (`tools/ci/no_sse_math.py`); **phase-state census**
  (`tools/phase_census.py` + the `{memsnap}` op) — the pin-completeness gate.
  Sessions captured before today lack the `gt8` diff stat until recaptured.
  **Census's first lead:** the 目玉 sparkle overlay-slot array
  (`g_scene1_overlay_slots`) carries load-dependent particle residue even under
  the canonical pin (`{phasepin}` re-seeds RNG + zeroes sim_frame but doesn't
  clear pre-pin particles) — sub-visible, accepted-known; fold into `{phasepin}`
  in a sparkle-parity pass if it's shown to matter. `findings/phase-state-census.md`.
  TODO: run the pinned RETAIL census (Frida host) + census other scenes.
- **Authoritative parity facts:** `findings/confirmed-parity-ledger.md`. A tooling
  "divergence" on a human-confirmed-1:1 item is a lead to investigate, NOT an
  assumed regression.
<!-- FRONT:END -->
