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

## ★★★ 2026-07-03 — FIXED via path (B): the drifting brackets were ALREADY pinned, but the pin VALUES were too small (extend-only min-gate didn't bind on retail). Raising csloadpin 24→72 + tutloadpin 8→36 makes retail COMPLETE the whole trace (all 4 prior drives stalled)
User chose path (B). Root cause turned out SIMPLER than "add a new pin": the retail agent.log
(`csloadpin: real load >= pin at frame N - left alone (extend-only)`) proved the drifting brackets
are ALREADY covered by the existing pins — bracket 2/3/5/6 by **csloadpin** (`b1cc==2` cc08 cs load),
bracket 4/7-12 by **tutloadpin** (D_TUT_LOAD CONV_POSE cutscene load). The pins are BILATERAL
EXTEND-ONLY min-gates (hold the bracket ≥N frames): the values were calibrated to the PORT's short
suppressed loads (cs≈24, tut≈8), so on RETAIL — whose real loads are 43-62 (cs) / 28-97 (tut) — the
gate `real >= pin` never bound ⇒ "left alone" ⇒ the real jittery duration ran ⇒ cadence drift ⇒ the
blink-phase/anchor-order flip that deadlocked the kept fragile waits.
**Fix = raise the pin values so N ≥ retail's real max** (`{csloadpin}` 24→**72** ≥ 62, `{tutloadpin}`
8→**36** ≥ 29). Now the gate binds on BOTH sides ⇒ each bracket is exactly N frames, port==retail.
Measured (drive `…123809Z`, `--target both`): port loads → cs 72 / tut 36 uniformly, **port COMPLETES
(855 anchors, no deadlock — the raised cadence PRESERVES the trace's fragile order)**; retail brackets
2-6 now BIND (72/72/36/72/72, `released` not `left alone`), retail **reaches the LAST anchor
CONV_POSE_END@16101 (the sign-hammer) + plays the DAY2 trailing hold** — the first of 4 retail drives
to complete. Non-blink anchor sequence port↔retail: **first 505 identical, DAY2 tail (last 14)
identical**; a transient reorder mid-cutscene (idx 505) is brackets **7-11 still drifting** (retail tut
loads 44-97 > pin 36) in the DROPPED-FRAGILE region (raw >3468) — HARMLESS (no `{wait}` deadlocks;
re-converges by the tail).
**RNG-safety of raising the pins:** every `LOADING_END` in the trace is immediately followed by an
`{rngseed}` pin that force-sets the LCG, so the (now-longer) in-load sparkle rng consumption is
OVERRIDDEN downstream ⇒ the confirmed offer (b574, generated from a pinned seed) is structurally
unchanged; and both sides now share the SAME deterministic load cadence ⇒ bg_npc/sparkle compare
port↔retail (the parity goal) instead of drifting. Verify via flow_diff before final sign-off.
**Residual (follow-up, NOT blocking):** brackets 7-11 (later dialogue-cutscene tut loads, real 44-97)
still exceed tut=36 ⇒ mid-cutscene cadence drift. Binding them needs tutloadpin ≥97, which would
inflate the early bracket-4 (real 28→97) in the confirmed region — risky; or a separate gate for the
late cutscene loads. Deferred: they're dropped-fragile (harmless for replay), and the DAY2/sign-hammer
tail re-syncs. **Also: with loads now deterministic, `max_frames` dropped 90000→25000** (commit; the
trace completes ~17003; retail was idling ~55s to the ceiling).

## ★ NEXT ARC (user-chosen 2026-07-03): segment-scoped `{tutloadpin}` → bind the late cutscene loads → clean DAY2 auto-diff
The residual mid-cutscene drift (brackets 7-11, retail tut loads 44-97 > pin 36) leaves retail **+1056
frames behind by DAY2**, which BREAKS the v3 viewer's identity-join for the DAY2 pixel confirm (best
achieved 232/1090 paired; every window/anchor fails — the drift shifts anchor-occurrence counts, e.g.
an extra CONV_POSE_END/START pair appears on the port at non-blink idx 505). The user chose **full
determinism**: make `{tutloadpin}` **segment-scoped** so it can change mid-trace — keep **36 early**
(protect the confirmed first-customer region: a uniform tut≈110 would shift the early wrap-up
cutscene's blink phase — the blink timer is FREE-RUNNING global-frame `%64`, seen firing every 64f —
and could re-flip the blink-vs-`TEXT_ANIM_START` order that deadlocked us) and **~110 late** (≥ the 97
max late-load + margin) to bind brackets 7-11.

**Why a mechanism change:** pins are currently **trace-global, last-declaration-wins, applied ONCE**
(port `src/main.c:1844` `scene1_intro_dialogue_set_tut_load_frames(g_segtrace.tutloadpin)`, "Known at
load time, no callback needed"; retail `tools/frida/openrecet-agent.js` global `g_segtrace_tutloadpin`).
A mid-trace `{tutloadpin}` just overwrites globally. Need it applied **per-segment at segment ENTRY**.

**Implementation (mirror the per-segment op pattern — `{rngseed}`/`seg_setrng`, but apply at segment
ENTRY not base+F):**
1. **`src/input_segtrace.h`:** `struct seg_segment` += `uint32_t tutloadpin; int has_tutloadpin;`.
   `struct input_segtrace` += a callback (mirror `esc_cb`): `segtrace_tutloadpin_cb tutloadpin_cb;
   void *tutloadpin_user;` + typedef + `input_segtrace_set_tutloadpin_cb` decl.
2. **`src/input_segtrace.c`:** parse `{tutloadpin}` onto the CURRENT segment being built (NOT the
   global `out->tutloadpin`); add `rearm_tutloadpins(st, seg_idx)` that — unlike `rearm_setrngs` which
   only resets `fired` — directly calls `st->tutloadpin_cb(user, segs[seg_idx].tutloadpin)` if
   `has_tutloadpin` (segment-entry apply); call it at the 3 tick advance sites (initial seg0 ~L975-984,
   wait-resolve ~L1004-1013, timeout-skip ~L1041+) next to the other `rearm_*`; add
   `input_segtrace_set_tutloadpin_cb`. Keep `has_tutloadpin`/`tutloadpin` on the struct for back-compat
   OR drop the global path.
3. **`src/main.c`:** replace the L1844 global apply with `input_segtrace_set_tutloadpin_cb(&g_segtrace,
   cb, NULL)` where `cb` calls `scene1_intro_dialogue_set_tut_load_frames` — so the head `{tutloadpin}`
   applies via the segment-0 rearm and any mid-trace one re-applies at its segment. (csloadpin/
   primaryloadpin stay global — only tut needs scoping; generalize later if wanted.)
4. **`tools/frida/openrecet-agent.js`:** mirror — parse `{tutloadpin}` per-segment; when the agent
   advances a `{wait}` segment (the JS mirror of the port tick), if that segment carries a tutloadpin,
   set `g_segtrace_tutloadpin` to it. (Find the JS segment-advance / {wait}-resolve site.)
5. **Trace `tests/scenarios/house-firstcust-cutscene-day2-full/trace.jsonl`:** insert
   `{"tutloadpin": 110}` at the big-dialogue-cutscene boundary — right before **L141** `{"wait":
   "LOADING_START"}` (→ CONV_POSE_START rng 968591134, raw ~2273 = bracket 7, the first LATE tut load).
   Keep the head `{"tutloadpin": 36}` (L6).
6. **VERIFY** (`--target both`, then `orv3_window --window <DAY2> --state --view`): retail brackets 7-11
   now `released` (not `left alone`) at 110; **full non-blink anchor seq port↔retail IDENTICAL** (idx-505
   divergence gone); DAY2 identity-join pairs cleanly (high paired, not 232/1090); `flow_diff --verdict`
   rng bit-exact (head tut=36 preserved ⇒ confirmed region unchanged). NB the port `--state` call_trace
   was NOT cached by orv3 this session (only retail's) — a v3 port-`--state` gap to also fix, else use
   `scenario-test --target both --call-trace` (scoped `{calltrace}`) for the flow_diff verdict.
   +regression test (segtrace parse: a per-segment tutloadpin applies at its segment).
7. **THEN** the DAY2 pixel confirm (human, viewer) + close the arc.

## ★★★ 2026-07-03 — NEXT-ARC LANDED (commit `3669cbe`) + VERIFIED. Cutscene now BIT-FRAME-ALIGNED (Δ=0, ~15000f); 2 residuals surface at the DAY2 boundary
Segment-scoped `{tutloadpin}` shipped: per-segment field on `struct seg_segment`, applied at each
declaring segment's ENTRY via `rearm_tutloadpins` (sticky), mirrored in the Frida agent's
`segtraceOnSegmentEnter`; global field dropped; `{tutloadpin:110}` inserted before the bracket-7
LOADING_START (seg 26), head `{tutloadpin:36}` kept. +per-segment parse+apply host tests (3395 pass).

**VERIFIED `--target both` (drive `142827Z`):**
- **Both COMPLETE exit=0** (retail 15948ms, NO stall — vs 4/5 prior drives stalled).
- **Retail brackets 7-12 ALL `released`@110** (agent.log: armed@2530→rel@2639, 5433→5542, 7765→7874,
  11727→11836, 14194→14303, 15660→15769; **0 "left alone"**). Early bracket rel@36 (728→763). Port
  applies 36@seg0 + 110@seg26 (stderr L48/L133).
- **Frame delta (port−retail) at matched structural anchors = Δ0 for frames 206→15348** — i.e. the
  ENTIRE first-customer + cutscene-cluster + massive-dialogue + sign-hammer arc (raw 0→15390) is now
  **bit-frame-aligned** (was: retail +1056f adrift, DAY2 join 232/1091). Non-blink anchor NAME seq 99.69%
  (647 v 643).

**RESIDUAL A (cosmetic, pose-flag PORT-DEBT — NOT load-cadence):** 2 port-only `CONV_POSE_END`+
`CONV_POSE_START` blips @14193/@15470. Δ stays 0 across them (anchor-only, no frame impact). ROOT:
`scene1_conversation_pose.c:104-108` derives the talk-flag (DAT_0450f470) from the dialogue lifecycle
(`_posing()` on `_active()`) instead of the faithful render-entangled producer FUN_00470a46 (clears at
the shatter-transition end) / FUN_004852fb (sets on scene-out). The derived flag BLIPS off 1f at EVERY
tut-load boundary; retail's REAL flag blips at SOME (11726 — BOTH fire END@11726+START@11727, aligned)
but NOT others (14193/15470). The port can't distinguish inter-script vs mid-script loads without the
faithful producer. Fix = port FUN_00470a46/FUN_004852fb (separate arc; retires the pose-flag PORT-DEBT).

**RESIDUAL B (the DAY2-tail blocker):** a **189-frame drift at the DAY2 ENTRY** (last matched Δ0 =
EXTRA_SPRITE_START@15348; next = LOADING_START port@15470 vs retail@**15659**, Δ=−189, and everything
after stays −189/−198). The DAY2-entry load is 110f on BOTH sides (pinned) — the drift is PRE-load:
retail HOLDS the pre-DAY2 conversation pose ~189f LONGER (3 extra %64 blinks @15494/15558/15622) before
advancing to the DAY2 load; the port cuts the hold short. ⇒ the DAY2 brooming tail (raw 15390→16291) is
port-189f-ahead, so the DAY2 PIXEL frame-match is blocked until this is closed. Suspect an unported
pose-HOLD / dialogue-line duration in the iv2 day-advance chain (iv1_8→iv2_1..6, FUN_0044bd0d;
PORT-DEBT(blackout-tut-dispatch)/(tut-dispatch-iv2-fx) still unwired — the likely home).

**orv3 DAY2 window BLOCKED (tooling):** `orv3_window` needs a `{caprange}` full-extent in the trace, but
the re-distill DROPPED it (the 33GB-BMP hazard — FRONT gotcha). To run the DAY2 viewer, re-add a
SCOPED caprange (the DAY2 window only, ~1091f ≈ 5GB both sides, NOT full-extent) — or teach orv3 to
inject a scoped caprange for `--window`. The committed trace stays caprange-free.

**NET:** the tutloadpin arc's goal — bind the late loads so the cutscene frame-aligns — is DONE
(~15000f Δ0). The two DAY2-boundary residuals (A pose-flag PORT-DEBT cosmetic; B iv2 pose-hold 189f
frame-drift) are distinct follow-up arcs. flow_diff rng-verdict is structurally guaranteed in the Δ0
region (rng force-pinned at every LOADING_END; frame-exact anchor alignment ⇒ matched rng consumption)
— explicit verdict deferred (needs a scoped `{calltrace}` + drive; low value given the frame-exactness).
