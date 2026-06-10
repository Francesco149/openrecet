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
  **Text reveal gradient (#4) ✅ DONE + USER-CONFIRMED 1:1 2026-06-10** (ledger):
  per-CHAR law `alpha·clamp((budget−i)·0.2, ≤1.0)` (a278101's per-row read was a
  loop-counter misread, gotcha #18); `font_draw_text_fade`, text strip gt8=0
  across the reveal, session over-threshold 1212→916.
  **iv1_5-tail pose-release slip (was queue #1) ✅ DONE 2026-06-10** (`c8a40df` →
  PROGRESS): the port armed iv1_6 the SAME frame iv1_5 completed (last
  `CONV_POSE_BLINK`→`CONV_POSE_END` 8f vs retail 9f, d=−1/−2 across iv1_6). Retail
  defers the re-arm 1 frame — its gate `DAT_0438b1c8` clears 1→0 in `FUN_004536cb`'s
  tail AFTER `FUN_0044bd0d` ran (call-trace: done@f15933, load-spawn@f15934). Fixed
  with a 1-frame `D_TUT_DONE` settle latch; recapture #7 (`--only port`): iv1_5-tail
  8f→9f, iv1_6 anchors bit-aligned (+733/+734/+1166), `problems: []`, **over-thr
  861→529**, rngcalls +26→+12. **✅ USER-CONFIRMED 1:1 2026-06-10** ("the 2nd
  dialogue is 1:1 aligned now") — parity ledger; closed the standee@~1448 +
  NPC@~1844 seam artifacts (RE #8).
  **Remaining (user-listed 2026-06-10 + triage), the next-session queue:**
  5. **Item-Details sub-view (now the session's WORST frame — label 181, gt8≈185k)**
     (`pressed & 0x40` path, all.c:65451, PORT-DEBT) — retail shows the narrow-right
     detail panel, port the plain wide-bottom description. **Full RE map ready in
     `findings/shop-display-menu-RE.md` #8b** (state `DAT_0734b96c` via FUN_004681d3/
     db/e6; render `FUN_0046b00a` tail layers `FUN_0046a336` over the bottom panel;
     port `FUN_0046a336` — 0x46a336, 2722 B — next to display_menu_render). No Frida
     needed. Plus **description-panel line layout** (price / "Number possessed" X
     positions) · (C) slide-in check · (D) row flash.
  6. **menu-boundary residuals:** rngcalls ±31 (one wing emit per %4==0-frozen
     pause boundary + load-bracket seams) · companion cx/cz/canim/cframe + pcnt
     micro-DRIFT around open/close frames · retail menu-window consumption is the
     WING through the hooked thunk (`0xcf05d33`), NOT an unknown menu consumer ·
     hand-cursor snap drawn 1 frame EARLIER by retail at menu open (label 587,
     ~109px, found verifying gap B) · **menu-close camera pan-out whole-frame
     offset** (label 441, ~160k px>8 — pre-exists the carry chip, attributed by
     stash-rebuild-recapture; the scene shifts ⇒ camera, not UI).
  7. **Item-display SOUND — the menuing SFX are missing** (audio-trace diff,
     **USER-CONFIRMED 2026-06-10**: voices play, menu SFX don't). `audio_diff
     --session item-display-2` (now in `triage`): port misses **14 triggers over
     6 sounds** — cursor tick `se_039_id0166` ×5 (ALL), confirm `se_007_id0143`
     ×3 (in-house placement), + 6 `00re_sys*` system menu SEs; dialogue voices
     match. So the item-placement menu drives no `audio_play_se` /
     `audio_play_se_file` — find + port the SE-trigger call sites in the
     display-menu interaction (around `FUN_0046b00a` / display_menu).
     Tooling/RE: `findings/audio-trace-diff.md`.
  *(Tooling owed here — recorder `save_capture` clobber + the session kept-count
  mismatch — both CLOSED 2026-06-10: `9a7bf63` stops the save clobber, and
  `{tutloadpin}` equalized the brackets so the kept-count PROBLEM is gone
  (`problems: []`). Story → PROGRESS; label-pairing fix folded into Tooling below.)*
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
- **Audio-trace diff (new parity pillar, 2026-06-10):** detect sound divergences
  from traces alone, no booting the port — `tools/audio_diff.py` compares port↔
  retail sound triggers by identity+count (phase/load-skew-immune). Foundation:
  frame-stamped port `audio.jsonl`, retail voice/file-SE hook + `se_NNN_idXXXX`
  names, studio sessions now carry `port/audio.jsonl`, folded into `trace_studio
  triage`. First catch = item-display queue #7. `findings/audio-trace-diff.md`.
- **Authoritative parity facts:** `findings/confirmed-parity-ledger.md`. A tooling
  "divergence" on a human-confirmed-1:1 item is a lead to investigate, NOT an
  assumed regression.
<!-- FRONT:END -->
