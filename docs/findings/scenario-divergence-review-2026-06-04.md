# Scenario divergence review — 2026-06-04 (user walkthrough of the regen page)

User-reviewed the full `runs/comparisons/index.html` after the capture fixes
(CAPTURE_FRAMES_MAX 32→64, HOUSE `max_frames` 16k→22k, legacy-trace `{savefile}`
skip). All scenarios now capture on both targets. This is the punch-list of
remaining port↔retail divergences the user called out. **FIXED** items were
addressed this session; the rest are deferred (mostly the dust / NPC-RNG /
Tear-wing / animation-phase classes already tracked).

Anchoring is honest (no `{gframe}`/`{phasepin}`/`{rngseed}`, no frame-shift) —
these are real diffs, not alignment artifacts.

## FIXED this session
- **title-down-press / title-options — cursor didn't move on port.** Root cause:
  the `{savefile}` embed broke the LEGACY (absolute-frame) `input_trace` parser
  → trace load failed → replay disabled → no input. Fixed: parser skips the
  `{savefile}` op (commit 86752ce). Cursor now moves NEW GAME→ITEM ENCYCLOPEDIA
  like retail.
- **title-z-press — "port never reaches the fade-out".** Same root cause (the
  Z-press was dropped). Now reaches fade-out. NOTE residual: **retail starts the
  fade slightly EARLIER than port** (timing/phase) — deferred.

## Deferred — title / menu
- **Main-menu OPTIONS text slightly brighter on port** (title-options). Likely a
  font/alpha or additive-blend nuance on the menu label.

## Deferred — HOUSE free-roam (dust / Tear / NPC-RNG class)
- **Idle animation frame mismatch** — very small (the idle anim is subtle).
- **house-idle**: first and last 2 frames diverge (anim phase at window edges).
- **house-movement**: idle mismatch on frame 0.
- **Tear position mismatch on moving frames** (confirmed-off per the ledger).
- **Tear animation frame mismatch on the first (idle) frame.**
- **house-walk-down-dense**: Tear animation-frame mismatch + **wings animation-
  frame mismatch**.
- **house-walk-tables cap_07**: many tiny dots — possibly a **trail of dissipating
  Tear-wing particles** (uncertain; flagged for confirmation).

These are the dust / NPC-movement-RNG / Tear-wing-particle classes the user has
explicitly deferred; see [[project_freeroam_smoke_effect]] and
`confirmed-parity-ledger.md` (Tear pos CONFIRMED off).

## Deferred — intro / dialogue (see opening-prologue.md "Remaining real deltas")
- **intro-opening**: fade-in mismatch in the first few frames; **standee slide-in
  is slightly EARLY on port**.
- **intro-fade**: first frame has a slight diff in the fade of the event-skip
  text; all other frames bit-identical.
- **Dialogue speech-bubble** (the prominent one):
  - **intro-dialogue-lines cap_01 is the clearest repro — the port bubble is
    SQUISHED HORIZONTALLY (too narrow) vs retail.** This is a bubble width/scale
    bug, possibly distinct from (or compounding) the bounce-phase issue below.
  - **intro-skip-prompt**: halo of diff around the bubble — possibly a slight
    bounce-animation phase diff.
  - **intro-iv2-gap**: the bubble-bounce diff halo persists even though the
    standee slide-in is 1:1 here.
  - **Dialogue text reveal has no gradient-to-transparent on port** — retail fades
    the leading glyphs; port reveals hard-edged (already tracked as delta #5 in
    opening-prologue.md).
- **intro-iv2-gap — CONFIRMED 1:1 except**: Tear animation frame, radial-lines
  phase, wings phase, wings particles, NPCs outside the window, and faint ambient
  particles (~2 dots). Standees slide-in is 1:1 in this scenario. (Good baseline —
  the remaining diffs are the known freeroam-anim + particle gaps.)

## Cross-refs
- Animation-PHASE class (bubble bounce, standee slide, wings, radial lines):
  probe with `tools/phase_probe.py` + the phase-divergence method. Likely one
  shared load-dependent origin-pin addresses several.
- The squished-bubble (cap_01) is the best single repro to start the bubble work.
