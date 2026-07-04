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
- **RETAIL DETERMINISTIC across ≥2 drives** (`142827Z` vs `144555Z`): bit-identical anchor streams
  (865 anchors each) AND identical bracket timings (pin 36→110; every armed/released frame matches) ⇒
  the CreateThread-race load drift is GONE — the Δ0 alignment + 189f DAY2 drift below are REPRODUCIBLE,
  not lucky single-drive luck (the project's ≥2-drive rule).
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
**★★★ 2026-07-03 — RESIDUAL B ROOT CAUSE CONFIRMED (scoped `{calltrace}` `--target both`, drive `160204Z`
+ retail-only `161316Z` with b924/b928/b1c8/f470 added to the `0x48670f` retail hook).** The 189f is a
**scripted ~190-frame idle BEAT the port omits between iv2_5 and iv2_6** — NOT a dialogue-length gap.
- **Both sides' iv2_5 dialogue (bracket 11 = scene 2/sub 5) ends bit-identical @~15469** (0x46c320 box/row/
  reveal Δ0 the whole way; last line shown@15379 cleared@15397). The 6 tut brackets 7-12 map to the 6
  dispatch entries iv1_8→iv2_1→iv2_2→iv2_3(day-adv)→**iv2_5(11@14194)**→**iv2_6(12@15659)**.
- **PORT** fires iv2_6 the NEXT frame (@15470); **RETAIL** free-roams 189f (15470→15659) then fires iv2_6.
- During retail's hold: `b1c8=0` (dialogue done), `f470=0` (no scene-out; `FUN_00470a46`/`0x4708f7` never
  fire → the b924-scene-out is a RED HERRING here + needs day-8), **`b928=1` and `b924` counts 0→189**
  (15470→15659), `f412=1`/`f413=0` (iv2_6 armed, not done), `b1e0=0` (slot-0 flag reads valid).
- **Exact gate (`FUN_0044bd0d` @0x44bd0d):** iv2_5 arms `DAT_0438b928=1, DAT_0438b924=0` (all.c:45798-99).
  `b924` increments EVERY free-roam frame in the master tick (`FUN_0048670f` all.c:86801, unconditional in
  the f470/f485/f488-clear path). The dispatcher line 45489 sets `bVar1=false` while `(b1c0==1 &&
  *068dd2f0==0 && b928==1 && b924 < 0xbe(190))`, and line 45507 `if(!bVar1) return` — so it RETURNS before
  the iv2 chain while `b924<190`; at `b924>=190` it falls through to fire iv2_6. ⇒ the beat is `b924`
  0→190 = the 189f drift. The actors stay in the conversation pose (panim=6/canim=4, the "Recette looks
  up at Tear" `b928` beat) → the blinks; the port RELEASES them at iv2_5-end (canim 4→0 @15471, the
  built-in state-diff), which is ALSO Residual A's port-only CONV_POSE_END/START blip. So porting the beat
  fixes A + B together.
- **✅ FIX LANDED (commit `d064cf0`).** Ported the `b928/b924` beat into `scene1_tutorial_dispatch`:
  iv2_5's branch arms `g_iv2_beat_active=1, g_iv2_beat_ctr=0`; a gate at the top of the iv2 cascade
  increments the counter each free-roam dispatch tick (= retail's master-tick 86801, which only runs in
  the default/free-roam arm — a dialogue takes the event arm, so neither counts, matching retail's
  cadence exactly) and `return`s while `< IV2_BEAT_FRAMES(0xbe)` (mirrors `if(!bVar1) return` @45507).
  The pose is held across the beat by ORing `scene1_tutorial_dispatch_iv2_beat_active()` into BOTH the
  conversation-pose gate (`scene1_conversation_pose.c`) AND the player walk-arm suppression
  (`scene1_player_ctrl.c:2453` — the freeroam arm otherwise resets Recette's anim 6→idle every frame, the
  documented pcnt-stuck-at-1 symptom). Reset on scene entry (`scene1_postload.c`).
  **VERIFIED `--target both`:** DAY2 `LOADING_START` now **15659 both sides** (was port 15470); DAY2-tail
  anchors **Δ0 (50/51** — blinks 15494/15558/15622 now match, day2 dialogue lines/sprites all frame-exact);
  the port-only `CONV_POSE_END/START` blip @15470 is GONE (Residual A @15470 closed too). Host tests 3394/0.
- **Two smaller residuals remain (separate arcs, both pose-flag PORT-DEBT class):**
  - **@14193** — 2 port-only `CONV_POSE_END`+`CONV_POSE_START` at iv2_5's OWN load bracket (retail's
    talk-flag doesn't blip there; the port's derived flag does). This is the ORIGINAL Residual A @14193
    (the beat fix only closed @15470). Root/fix = port the faithful flag producer FUN_00470a46/FUN_004852fb.
  - **@16380 vs 16389** — the FINAL day2-brooming `CONV_POSE_END` is Δ−9 (port ends the pose 9f early;
    the day2 dialogue itself is Δ0 through DLG_LINE_CLEAR@16332). A small day2-tail pose-end hold, likely
    the same pose-flag class; low-priority.
- Probe scaffolding this session: a temp `{calltrace:[15250,560]}` on the head seg (REVERTED — committed
  trace stays calltrace-free); the `0x48670f` retail hook gained b1c8/b924/b928/scene+sub selectors (KEPT,
  commit after `d064cf0` — state-panel enrichment).

**★★ 2026-07-03 — DAY2 PIXEL CONFIRM (side-by-side, scenario-test caprange @15490-15910, since orv3_window
is caprange/occurrence-blocked on this drop-fragile trace). The BEAT TIMING is confirmed correct, but the
confirm REVEALED 4 pre-existing DAY2 render/state gaps the anchor-Δ0 could not see (anchors track sim
events, not overlays/HUD text) — the classic "chase render even when the sim matches" payoff:**
1. **The ~190f beat IS a black "Day 2" DAY-TRANSITION TITLE CARD** (retail: full-screen black + centered
   "Day 2" text for the whole beat 15470→15659; the port shows the LIVE house behind the held pose). ROOT
   = the screen-blackout `FUN_00452809` (DAT_0438bf74) that iv2_5 arms + the "Day N" title glyphs —
   `PORT-DEBT(blackout-tut-dispatch)`, explicitly left unwired in the beat fix. THE headline DAY2 render gap.
2. **HUD day counter port "Day 1" vs retail "Day 2".** `g_hud_day` is CACHED at scene load (scene.c:95 →
   `scene1_top_hud_set_day(work[CARD_DAY])`); retail reads DAT_0450fb84 LIVE at render (scene1_top_hud.c:87
   comment). iv2_3's mid-scene fb84 0→1 advance shows on retail immediately but not the cached port. Fix =
   read fb84 live at render, OR push `scene1_top_hud_set_day` at the iv2_3 day-advance (one-liner).
3. **"Now Loading…" disc** (bottom-right) during the DAY2 load bracket — retail draws it, the port
   suppresses it (LOADING-SCREEN FIDELITY, user direction 2026-06-11).
4. **ACTOR POSITIONS mirror-SWAPPED (user-spotted).** Retail: Recette px=**−0.30** (the pose_house_standing
   spot) facing oct6, Tear cx eases **0.60→1.00** (spring-follow to Recette's RIGHT) facing oct2. Port:
   Recette px=**+0.80** oct2, Tear cx=**−0.69** (FROZEN, wrong side) oct6. The pose FACING is correctly
   derived from the positions on both sides (`tear_x<=player_x`), so the root is the POSITIONS: the port
   never re-places the actors for day2 (iv2_5/iv2_6 are modeled as dialogue-loads, not the scene reload +
   pose_house_standing retail runs at the day advance), so they keep their stale cutscene positions.
   **PRE-EXISTING, not the beat fix** — the pre-fix port (drive `160204Z`) shows the identical px=+0.80/
   cx=−0.69 at its lone free-roam frame 15470. (Also the port's Tear is frozen where retail's spring-follows
   — a consequence of the same missing re-placement + the beat walk suppression.)
5. Minor: a Tear wing-sparkle present on retail at 15769/15795 (companion db054%4 phase — likely the known
   sparkle-phase class).
**⇒ Residual B (the frame-drift BLOCKER) is CLOSED and its TIMING is confirmed; the anchor-Δ0 MASKED a
pre-existing cluster of unported DAY2 day-transition RENDER (the classic pixels-over-anchors lesson).
Full DAY2 pixel PARITY is a substantial "day-transition" arc: the Day-2 blackout card (#1) + the actor
re-placement/scene-reload (#4) are the big ones; HUD day-counter (#2, one-liner) + Now-Loading disc (#3)
are smaller. Montage on the llm-feed.**

**★★ 2026-07-03 (later) — DAY2 render gaps #2 + #1 LANDED (commits `c63ee20`, `a77c46b`).**
- **#2 HUD day-counter DONE** (`c63ee20`): `g_hud_day` was cached once at scene load; now refreshed live
  from `working[CARD_DAY]` every INGAME frame (sim_step_a, beside money_tick/clock-ease). VERIFIED port
  @f15820: HUD reads "Day 2". The faithful match to retail's "reads DAT_0450fb84 live at render".
- **#1 "Day 2" CARD DONE** (`a77c46b`) — **RE CORRECTION: the card is NOT the bf74 blackout + "Day N"
  glyphs (the FRONT/above guess). It's a self-contained block in FUN_0040a765 (all.c:7500-7559 / objdump
  0x40c209), gated `b928==1 && b924<0x8c`, driven by the SAME b928/b924 beat already ported for Residual B:**
  `b924<0x7a` → opaque-black backdrop (0xff000000, system.bmp) + centred "Day %d" (`save[0x2c3ec]+1`) at
  (320,208) scale 2.2, white alpha `min(b924*8,255)`; `b924>0x5a` → WHITE exit-fade quad, alpha
  `b924*8-0x2d0 (+(0x7a-b924)*0x10 past 122)`. Ported as `scene1_day_card_render` (scene1_fx_overlays.c),
  called from the HOUSE free-roam tail after scene1_hud_render (covers the HUD, per retail draw order).
  **The port does NOT need to arm the bf74 blackout** (PORT-DEBT(blackout-tut-dispatch) stays) — retail's
  bf74 (FUN_00453d9c) is a redundant opaque black UNDER this card during the async load; THIS card's own
  backdrop blacks the transition. VERIFIED `--target both` @ raw 15470-15612: **BIT-EXACT (0px)** at the
  opaque hold (b924~40) + mid white-exit (b924~108); the "Day 2" card matches retail.
  - **✅ b924 fade-transition seam CLOSED 2026-07-04 (commit PENDING) — the "counts ahead" HYPOTHESIS was
    DISPROVEN by its own prescribed probe.** Ran the scoped `{calltrace}` capturing **port b924 vs retail
    DAT_0438b924 per frame** over the beat: **port b924 == retail b924 FRAME-FOR-FRAME, 155/155, MAX|Δ|=0,
    0 mismatches** — INCLUDING the exact fade-in region (b924 0→17 where "brighter" was reported: both
    0,1,2,…,17). The port counter was NEVER ahead. Combined with the alpha formula being bit-identical in
    code (block1 `b924*8` cap 255; block2 `b924*8-0x2d0 +(0x7a-b924)*0x10`), the day-number/position/scale/
    font all identical, and the already-verified BIT-EXACT hold+mid-exit (same font+blend at α=255) ⇒ the
    card is bit-exact over the WHOLE fade (in/hold/exit) by counter+formula, no free variable left. The
    pre-#4 "brighter fade-in" note was a measurement/pairing artifact (the fade-in frames weren't perfectly
    frame-paired before the #4/#4b beat frame-alignment), NOT a real counter offset. The omitted card gates
    (`DAT_0438b1c0==1 && *DAT_068dd2f0==0`) are HOUSE-free-roam-render + no-blocking-menu — both hold across
    the auto-play beat (the port's card is only called from the HOUSE free-roam render tail), so they can't
    diverge here. TOOLING landed en route: b924/b928 now emitted continuously at the always-ON **0x4536cb**
    (sim.c) + retail_fields.json 0x4536cb mirror (flow_diff auto-diff). **★ Why 0x4536cb, not 0x48670f (where
    retail reads b924):** post-#4b the port's scene1_player_ctrl_tick EARLY-RETURNS before the 0x48670f
    emission on the pose-held beat (the companion now routes through the conversation-pose driver ⇒
    posing()==true) — relaxing the emission gate did NOT help (proved: only 1/155 beat frames emitted), so
    0x48670f (px/poct/cx AND b924) is DARK on the whole day-2 beat. 0x4536cb fires every sim frame regardless
    of arm. **Latent lead (not blocking — #4/#4b already confirmed beat positions):** if future beat-POSITION
    parity is needed, route 0x48670f around the pose early-return or add px/poct/cx to 0x4536cb.
  - **Still OPEN: #3 Now-Loading disc, #4 actor re-placement (both big), #5 wing-sparkle (minor).** #3/#4
    share a root with #1: the iv2 chain is modeled as `start_single` dialogue-loads, not retail's real
    scene-reload day-advance (so #3's `nowloading_set_active(1)` never fires on the iv2 load, and #4's
    `pose_house_standing` re-placement never runs).

**★★ 2026-07-03 (later) — #4 ACTOR RE-PLACEMENT investigated; MECHANISM mapped + sim-fix works, RENDER
BLOCKER found (reverted, NOT committed — the render step is the real work).** Agent-mapped RE (all.c refs):
- **It is a TARGETED in-scene re-place, NOT a scene reload.** The HOUSE (scene-1) stays loaded through the
  whole iv2 chain (the iv2 loads are async DIALOGUE overlays, FUN_00452d07). Retail re-runs **FUN_00436f97
  @0x436f97** (= the port's `scene1_postload_pose_house_standing` / `player_ctrl_pose_house_standing`:
  player→px≈-0.30 oct6, companion actor2 re-seed→(0.6,3.0,9.35)) via **FUN_0048526d @0x48526d** (FUN_00436f97
  + FUN_004851e2(1) + facing + db048=0xf + **b924=300**), triggered by the HOUSE controller poll
  `if(DAT_0438b4e0==1) FUN_0048526d();` at **all.c:86750**. `DAT_0438b4e0=1` is set at shop/day-end→HOUSE
  transitions (sites 40766/60378/86746(fb88>=4)/92368/45712(gated fb88>3, but iv2_3 sets fb88=0)); the
  EXACT site for this trace's iv2_6→day2 boundary is **not statically determinable** (data-dependent tut-flag
  threading) — pin with a `{calltrace}` probe adding **DAT_0438b4e0 + DAT_0438b1c0** (+ FUN_0044bb1a's
  DAT_0438b770 "snap to door" countdown) to the 0x48670f hook. FUN_004852fb (iv2_5's call) sets ONLY facing
  (oct 2/6 from `_DAT_056da1f0<=DAT_056da1d8`) — NO position (correct: the finding said facing is derived).
- **Port first-cut (wired, VERIFIED at SIM level, then REVERTED):** a one-shot `g_day2_replace_pending`
  armed at the iv2_6 fire, consumed the first `!scene1_intro_dialogue_busy()` frame after the DAY2 load
  (= day2 entry), calling `scene1_postload_pose_house_standing()`. Debug-confirmed: it FIRES (at beat_ctr=190)
  and the SIM positions re-place correctly — `g_scene1_player_pos[0]` 0.796→**-0.300** (stable), companion
  `g_scene1_actor_pos[2][0]` -0.694→**0.600** (stable). So the sim re-place is CORRECT.
- **★ BLOCKER — the RENDER does NOT reflect the sim re-place.** `--target both` @ day2 free-roam (raw
  15799-15838): the port SPRITE still draws Recette at the STALE +0.80 (right) and Tear on the LEFT — the
  mirror-swap PERSISTS on screen even though the sim says player=-0.30 / companion=0.6 (right). ⇒ the
  player/companion SPRITES render from a source the `g_scene1_actor_pos` re-place doesn't propagate to
  (likely a per-actor chr sprite RECORD updated by the player controller, or a conversation-pose snapshot,
  or the camera). **NEXT: find the player/companion sprite render position source + the record↔actor_pos
  relationship; the complete #4 fix must update THAT (not just g_scene1_actor_pos).** Also: the companion is
  FROZEN at 0.6 in the sim too (should spring-follow-ease 0.6→1.0; FUN_0048a4d1) — its follow is gated off
  during day2 (the beat walk-suppression class), a 2nd sub-issue. `player_ctrl_pose_house_standing` seeds
  the record's anim/octant but the POSITION propagation is the gap. **Re-doing the one-shot re-place is
  trivial (documented above); the render-source + frozen-follow are the real remaining work.**

**★★★ 2026-07-04 — #4 ACTOR RE-PLACEMENT FIXED + PIXEL-CONFIRMED (commit PENDING). The prior session's
"RENDER BLOCKER" was REFUTED — it was a flawed first-cut, not a real render-source gap.** Ground truth from
the CACHED call-traces (port `162817Z` vs retail `161316Z`, day2 beat 15470→15659):
- Body sprites render position DIRECTLY from `g_scene1_actor_pos[i]` — `scene1_shop_walker.c:779-801`
  (`sw_pass_light`), NO snapshot/separate buffer (Explore-agent-mapped). `g_scene1_player_pos ==
  g_scene1_actor_pos[0]` (aliased, `scene1_particles_tick.h:83`). So a sim re-place IS the render source; the
  "state re-placed but render stale" claim is physically impossible → the first-cut never actually held the
  state (wrong trigger frame / overwritten). **THE ONLY divergence was positions:** port px=**0.796**/poct=2,
  cx=**-0.694**(frozen)/coct=6 vs retail px=**-0.30**/poct=6, cx=**0.6→1.0**/coct=2 — `cc08=1`, `panim=6`,
  `canim=4` ALREADY matched both sides. The stale +0.80 = the customer-service COUNTER x (`customer_service.c:1918`);
  the port never runs retail's day-advance scene-entry re-place so the actors keep it into day2.
- **FIX (positions-only, RNG-neutral):** `scene1_postload_day2_actor_replace()` re-seats the two live actors at
  the house-standing pose (player -0.30/0/9.35, companion 0.6/3.0/9.35 — same door-placement seeds as
  pose_house_standing, NOT the full reset which would zero the beat). Armed as a one-shot at the iv2_5 beat-arm
  (`g_day2_replace_pending`), consumed at the `scene1_ingame_default_arm_tick` TOP (before the conversation-pose
  tick derives facing, before render) — the default arm runs only on non-dialogue free-roam frames, so it fires
  on the FIRST post-iv2_5 free-roam frame = the beat's first counting frame = retail's day2 @15470. The pose
  driver re-derives the face-each-other octants (player 6 / companion 2, since tear_x 0.6 > player_x -0.30);
  the free-roam law holds the actors (dist 0.9 < CO_THRESHOLD 1.5).
- **VERIFIED (drive `215022Z`):** re-place fires EXACTLY @15470; port px=**-0.30**/poct=**6**, cx=**0.6**/coct=**2**,
  panim 6 / canim 4 — bit-matches retail; holds -0.30 through the beat + broom to 16380. RNG **0 diffs** vs the
  pre-fix trace. Host 3394/0. **PIXEL-CONFIRMED (caprange @15799-15838 vs retail PNGs, feed montage):** before-fix
  = actors mirror-SWAPPED (Tear left/Recette right); after-fix = Recette left/Tear right, MATCHES retail @15799 +
  @15815 (chibi placement + facing pixel-1:1).
- **RESIDUALS (both post-#4, separate follow-ups):**
  - **✅ (4b) companion cx EASE FIXED + BIT-EXACT (commit PENDING).** Retail eases cx 0.6→1.0 over the beat via
    `FUN_0048a833`'s ELSE-branch (all.c:89434-89473, taken while `b928==1 && b924<200`): Tear springs to a FIXED
    **±1.3 X offset** on the player's side (target_z=player_z, target_y=bob) at factor **0.1**, NO
    CO_THRESHOLD/bearing, NO vel clamp — target = player_x -0.30 + 1.3 = **1.0**. The port modeled ONLY the
    free-roam branch (`FUN_0048a4d1`, CO_THRESHOLD 1.5 / 0.15), so cx stayed 0.6 (within threshold). FIX: gate
    a beat-branch on `scene1_tutorial_dispatch_iv2_beat_active()` in `scene1_companion_ctrl_tick` — spring all 3
    axes at CO_INTRO_SPRING(0.1) toward (player_x±1.3, bob, player_z); anim(4)/facing(2) still owned by the
    conversation-pose driver (matches retail's dab54=4/dab58=2 in the same branch). **VERIFIED: MAX|Δcx|=0.0 over
    the FULL 190-frame beat** (port≡retail 0.6→0.64→…→1.0, settles 1.0 @15600, holds), RNG **0-diff** over 25000f,
    host 3394/0. NB the retail phase (cx=0.6 @15470, first ease @15471) is reproduced exactly — no 1-frame offset.
  - **(4c) @15838 retail dialogue PORTRAIT** slides in from the right; the port lacks it there. The port HAS
    this day2 dialogue — box opens @15878 (dlg_cmd 23 setup 15831→15877 → box_open ramp + text reveal); retail's
    portrait is ~40f ahead. So EITHER retail renders the portrait during the dlg_cmd-23 slide-in/setup phase the
    port renders empty, OR the port's day2 dialogue starts ~40f late (would need retail's dlg_cmd @15838, not in
    the beat-window trace). PRE-EXISTING (RNG-neutral fix ⇒ identical dialogue lifecycle), NOT a #4 regression;
    a distinct day2-dialogue arc.

**orv3 DAY2 window BLOCKED (tooling):** `orv3_window` needs a `{caprange}` full-extent in the trace, but
the re-distill DROPPED it (the 33GB-BMP hazard — FRONT gotcha). To run the DAY2 viewer, re-add a
SCOPED caprange (the DAY2 window only, ~1091f ≈ 5GB both sides, NOT full-extent) — or teach orv3 to
inject a scoped caprange for `--window`. The committed trace stays caprange-free.

**NET:** the tutloadpin arc's goal — bind the late loads so the cutscene frame-aligns — is DONE
(~15000f Δ0). The two DAY2-boundary residuals (A pose-flag PORT-DEBT cosmetic; B iv2 pose-hold 189f
frame-drift) are distinct follow-up arcs. flow_diff rng-verdict is structurally guaranteed in the Δ0
region (rng force-pinned at every LOADING_END; frame-exact anchor alignment ⇒ matched rng consumption)
— explicit verdict deferred (needs a scoped `{calltrace}` + drive; low value given the frame-exactness).
