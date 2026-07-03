# Cutscene-replay anchor-ordering drift (trace tooling)

**TL;DR:** an anchor-segmented trace of an AUTO-PLAY cutscene deadlocks under turbo
because the RELATIVE ORDER of cosmetic/FX anchors (character blinks, sprite fades,
text-anim) differs between the frame-dropped REAL-TIME recording and the
deterministic TURBO replay. The rigid `{wait: NAME}` chain then waits for an anchor
that already fired (in the past) → deadlock. Fix: **don't sync on cosmetic/FX
anchors in auto-play cutscenes — drop them, keep only reliable scene boundaries.**

## Symptom
`house-firstcust-cutscene-day2` (the post-first-customer prologue: Tear meets
Recette → debt story → shop setup → day 2).  With the caprange extended past the
first customer, the replay FROZE for ~14000 frames on the first prologue line
("Recette: Let's see..."), identical on BOTH port and retail (line_row stuck 0,
text fully revealed, no advance).  Not a render gap — a replay stall.

## Root cause (seg-probe pinned it)
The port emits every anchor correctly.  The deadlock was the anchor-segmented
harness parked on **`{wait: EXTRA_SPRITE_FADED_IN}`** (the sprite fade-in of the
EXTRA_SPRITE cutscene).  The recording's fade-in spanned ~4 character blinks (real
time, frame-dropped), so the trace ordered it `START → 4×CONV_POSE_BLINK →
FADED_IN`.  Under turbo (no drops) the fade-in completes FASTER — FADED_IN fires
after ~2 blinks.  By the time the harness resolves the 4th blink-wait and arms
`{wait: FADED_IN}` (which requires `af ≥ base_arm`, i.e. a FUTURE firing),
FADED_IN is already in the past → the wait can never resolve → the harness sits
there feeding idle input, starving all downstream input (the held-X that
fast-forwards the dialogue never lands).

**Timeouts don't fix it** — a timed-out wait waits N frames then skips, but the
accumulated latency makes the harness fall further behind and MISS the *next*
anchor (which then also times out), a runaway.  Verified: with timeouts the
harness crept 131→150 then re-stalled on the next boundary (LOADING_START, which
the port collapses and fires early).

## Fix — drop fragile syncs in auto-play cutscene regions
Cosmetic/FX/collapse-prone anchors are NOT reliable sync points under turbo:
`CONV_POSE_BLINK`, `EXTRA_SPRITE_{START,FADED_IN,FADEOUT,END}`,
`TEXT_ANIM_{START,END}`, `DLG_LINE_{SHOW,CLEAR}`, `LOADING_{START,END}`.  In an
auto-play cutscene (deterministic reveal/anim, driven by a held button or a timer)
they aren't NEEDED for input timing either — the input is just "hold X" carried
across them.  So **drop them as `{wait}` sync points, keep only the reliable
scene/state boundaries** (`CONV_POSE_START/END`, `HOUSE_FREEROAM`, `PAUSE_*`,
`FREEROAM_START`).  The deterministic auto-play + the held input carry between the
sparse boundaries; the boundaries re-sync (+ re-pin RNG) at each scene change.

Applied surgically to this trace (`drop_fragile.py`, line 465+ = past the confirmed
first-customer region): dropped **586** fragile waits, kept 3 boundaries
(2 CONV_POSE_START + 1 CONV_POSE_END across the whole prologue).  NB the port +
retail still EMIT the dropped anchors (the v3 viewer still labels them by identity);
the trace just no longer WAITS on them.

## Validation (PARITY)
- Trace replays the whole prologue END-TO-END: line_row 0→157, held-X applied,
  reaches CONV_POSE_END (raw 15390).
- **Port vs retail line_row sequence BYTE-EQUAL** (85 transitions, both reach 157).
- **RNG BIT-EXACT through the entire cutscene** (constant +3520 warmup delta at
  every sample) — the `{rngseed}` pins at the kept boundaries re-sync scene changes;
  between them the deterministic auto-play keeps both sides aligned.
- Studio join: 13863 paired, **0 port-only gaps** (no stall).
- Confirmed first-customer region (line <465) untouched → the day-end customer-
  despawn fix (RE §21.33) intact.

## Follow-ups
- **✅ DONE 2026-07-03 — folded into `distill_trace.py`** (commit `59c4124`).
  `FRAGILE_ANCHORS` + `--drop-fragile-after FRAME` / `--drop-fragile-region LO:HI`
  for `--anchor-segments`: inside an auto-play region only reliable scene boundaries
  survive as `{wait}` syncs; dropping a sync is loss-free (the next kept segment's
  window spans it, rebasing its inputs). `_suggest_autoplay_boundary` prints a HINT
  where dense taps give way to sparse held input. Regression: `tools/test_distill_trace.py`.
  Retires `PORT-DEBT(distill-drop-fragile)`.

## ★ Re-distill gotcha — hand-tuned PIN ops are NOT in the raw (2026-07-03)
A NAKED re-distill of `rec-20260622-182618` (`--anchor-segments --drop-fragile-after N`)
**stalls at the post-first-customer PAUSE region (~raw frame 2994)** — the replay resolves
~35 waits then sits idle to `max_frames`. Cause is NOT the drop-fragile: the trace is
byte-identical to the committed one through the pause region; the naked re-distill is just
**missing the hand-added load pins** the committed trace carries at its head
(`csloadpin:24`, `primaryloadpin:16`, `tutloadpin:8`, `bgnpcseed`) + the rng-anchored mid
`bgnpcpin` (after the first-customer `CONV_POSE_END`, rng `807420856`). These are calibrated
against retail (the async `CreateThread` load-bracket races) and are **not recoverable from
the raw** — without them the load timing desyncs and the next anchor never fires. Proof: the
committed trace (same ops, +pins) sails through the pause region to ≥3126 on the SAME exe;
the naked one stalls at 2994.
- **Fix / tool:** `--carry-pins-from TRACE` (commit pending) copies the head load pins +
  re-anchors the mid pins (`bgnpcpin`, `caprange`) by their segment's rng value, so a
  re-distill reproduces the working pin set in one command. **Any re-distill of a
  hand-pinned trace MUST pass `--carry-pins-from <the old trace>`.**
- **Principled auto-play boundary for THIS trace = raw frame 3468** (last interactive tap;
  then a 4008-frame input gap). `--drop-fragile-after 2195` is TOO aggressive — 2195-3468
  still has interactive input (3rd pause, ESC skips, taps) whose timing needs the fragile
  re-syncs, so it stalls. The human's original conservative cut (raw ~6300) also works;
  3468 is the clean minimum.
- **DAY 2 brooming** (raw 15390-16291) — ✅ PORT-SIDE DONE 2026-07-03. Candidate scenario
  `house-firstcust-cutscene-day2-full` = `--drop-fragile-after 3468 --carry-pins-from <committed>`,
  full 16291 range. Port drive replays END-TO-END (exit=0, ~100s, 855 anchors): past the pause
  region, through the fast-forward/deadlock zone, sign-hammer at CONV_POSE_END@15390, then the DAY2
  brooming tail. NEEDS (human): `--target both` RNG/line_row parity + viewer confirm. Two more
  distiller fixes were needed for this:
  - **caprange must NOT carry** (commit `86d4a0d`): `--carry-pins-from` pulled the committed trace's
    `{caprange:[0,14000]}` into the re-distill ⇒ a `--bless` drive dumped ~33GB of per-frame BMPs and
    hit the wall-clock ceiling. `caprange`/`capture`/`calltrace` are viewer directives, never pins.
  - **anchor-segment TRAILING HOLD** (commit `ef28140`): `emit_anchor_segments` exhausted the segtrace
    at the LAST anchor ⇒ the exe exited early and silently TRIMMED any post-last-anchor idle. This is
    *why DAY 2 was trimmed in the first place* — the tail has no anchors (companion-AV brooming). A
    trailing `{frame: total-last_lo}` hold now runs the final segment to the recording's end.
- The **`seg` call-trace probe** (input_segtrace `g_segtrace_dbg_seg` → the segment
  the harness is parked on) is the tool that cracks trace-replay stalls in one drive
  — kept as permanent debug tooling.

## ★★ 2026-07-03 — the FRONT's ≥2-drive retail verification DONE. VERDICT: retail replay is LOAD-FLAKY on this trace (3/3 stall, WANDERING point) — a pillar-B load-determinism gap, NOT a port gap
Ran the ≥2-drive `--target both`/`--target retail` verification the FRONT asked for. Result: **retail
stalls to `max_frames@90000` on EVERY drive, at a load-phase-dependent point that WANDERS run-to-run:**
- **drive-1 (`--target both`, run `…120220Z`):** stalled @ raw≈837, the **first wrap-up skip** —
  `TEXT_ANIM_START@837` fired BEFORE the 2nd `{wait CONV_POSE_BLINK}` (line 57) resolved (@839) ⇒ the
  harness armed `{wait TEXT_ANIM_START}` (line 60) AFTER TEXT_ANIM_START already fired ⇒ unresolvable ⇒
  parked ⇒ the confirming X (line 67) never injected ⇒ box armed (`wrapup_dbg box=1 latched=1`, driver
  WAS on) but skip never confirmed ⇒ **1396 free-running blinks**. This is the classic anchor-drift
  REORDER on a KEPT fragile wait.
- **drive-2 (`--target retail`, run `…121355Z`):** got PAST the wrap-up, stalled @ `PAUSE_CLOSE@1600`
  (forced rng **2246047975** = trace line 108) — the harness parked on the NEXT `{wait PAUSE_OPEN}`
  (line 126, rng 2875783614); the pause-menu nav taps (frames 82…351 relative to PAUSE_CLOSE) didn't
  land on retail's drifted cadence ⇒ PAUSE_OPEN never fired (4 blinks only — pure input-timing miss, a
  DIFFERENT mechanism from drive-1). **This is EXACTLY the FRONT's originally-reported stall.**
- **the FRONT's earlier single drive:** `PAUSE_CLOSE@1603` / rng 2246047975 — same point as drive-2.

**Port is DETERMINISTIC and completes 855/855 both drives** (turbo = fixed cadence). So this is
purely a RETAIL non-determinism problem: completion-based (`CreateThread`-race) loads drift retail's
frame cadence run-to-run; the INTERACTIVE first-customer/pause region (raw <3468) syncs on FRAGILE
anchors (`CONV_POSE_BLINK`, `TEXT_ANIM_*`, `PAUSE_OPEN`-via-menu-nav) whose relative order/timing
flips under that drift ⇒ the anchor-segment harness parks on an already-fired or never-firing anchor.
`--drop-fragile` de-fragilizes only AUTO-PLAY regions (raw >3468); the interactive region KEEPS its
fragile syncs because its taps need them for timing (dropping them rebases the taps → more accumulated
drift → worse), so it stays vulnerable. The committed sibling's past `--target both` pass was
load-phase LUCK (the ordering didn't flip that run), same as §21.6's "1 OK, 1 stall was load-phase
luck". This is the pillar-B load-determinism FOUNDATION issue (`customer-service-haggle-RE.md` §21.5/.6,
memory `openrecet_bgnpc_nondeterministic`), which §21.6 line 2336-2340 explicitly deferred as "the
future day-2 brooming work — generalise the ARM-ONLY driver to re-arm at each post-entry skippable
cutscene".

**⇒ a lucky-clean retail re-roll is NOT a solid basis; the real fix is load-determinism/harness work
(needs direction — see FRONT).** Candidate paths: (A) generalise the Frida ARM-ONLY skip driver to
also INJECT the confirming X (not just arm the box) so the skip completes independent of harness
parking, + re-arm past the f406 entry; (B) a `csloadpin`-analogue LOAD-BRACKET pin for the
first-customer/pause cutscene loads so retail's cadence is deterministic (the ordering stops flipping);
(C) verify the DAY2 content off a DAY2-PROXIMATE save (short robust trace that skips the fragile
region entirely — needs such a save); (D) re-roll retail until a clean pass (fragile, unbounded).
