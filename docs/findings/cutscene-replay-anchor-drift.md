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
- **Fold into `distill_trace.py`** (region-aware): when a button is held across a
  run of cosmetic/FX anchors (auto-play cutscene), emit only the boundary syncs.
  Currently a surgical `drop_fragile.py` on the one trace — `PORT-DEBT(distill-drop-fragile)`.
- **DAY 2 brooming** (raw 15390-16291, idle) is trimmed from the distilled trace;
  re-distill from `rec-20260622-182618` to include it.
- The **`seg` call-trace probe** (input_segtrace `g_segtrace_dbg_seg` → the segment
  the harness is parked on) is the tool that cracks trace-replay stalls in one drive
  — kept as permanent debug tooling.
