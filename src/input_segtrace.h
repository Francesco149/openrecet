/*
 * input_segtrace.h — anchor-segmented input forcing (port side).
 *
 * The C mirror of the Frida agent's segtrace (tools/frida/openrecet-agent.js),
 * the deterministic replacement for absolute-frame input replay. A trace is a
 * JSONL superset of the sparse input trace (input_trace.h): on top of the
 * `{"frame":k,"buttons":"0xNN"}` entries it adds segment ops —
 *
 *   {"wait":"ANCHOR"}    segment break. The next segment's frame 0 is the frame
 *                        ANCHOR fires (strictly AFTER this segment was entered,
 *                        so a repeated anchor — e.g. HOUSE_FREEROAM firing twice
 *                        across a hidden load — resolves on the NEXT firing).
 *                        "Spam-until-anchor": a segment's entries may run
 *                        arbitrarily long; the wait short-circuits the instant
 *                        its anchor fires, abandoning the remaining entries.
 *   {"capture":N}        screenshot the deterministic frame base+N (N frames
 *                        after this segment's anchor) — emitted via the
 *                        capture callback so main.c routes it to its capture set.
 *   {"calltrace":[S,L]}  arm the call tracer for the L-frame window starting at
 *                        base+S (S frames after this segment's anchor) — the
 *                        port mirror of the Frida agent's window mode.  Resolved
 *                        like {capture} and reported via the calltrace callback
 *                        so main.c routes it to call_trace_arm_window().  A bare
 *                        scalar {"calltrace":N} means [0, N] (N frames from base).
 *   {"caprange":[S,C]}   schedule a CONTIGUOUS capture of the C deterministic
 *                        frames base+S .. base+S+C-1 (C frames from base+S) —
 *                        the frame-by-frame trace-export op.  Resolved like
 *                        {capture} when its segment activates, but routed via a
 *                        separate caprange callback that drives a lo/hi window
 *                        test in the host (NOT the bounded capture list), so a
 *                        single op can span hundreds of frames.
 *   {"capstride":N}      two-tier capture cadence (Trace Studio v2 D3): within a
 *                        {caprange} window, capture only every Nth frame measured
 *                        from the window start — base+S, base+S+N, base+S+2N, …
 *                        Lets a LONG trace be scrubbed cheaply at a coarse stride
 *                        (an OVERVIEW); a dense {caprange} (N=1, the default) stays
 *                        a superset (the DRILL).  Because the stride is measured
 *                        anchor-relative on BOTH targets (the Frida agent strides
 *                        its g_capture_pending fill identically), the port and
 *                        retail keep the identical kept-set, ordinal-paired.
 *                        Trace-global (last declaration wins), NOT segment-scoped —
 *                        it gates ONLY caprange membership; explicit {capture}
 *                        points always capture.  N<=1 means every frame.
 *   {"esc":N}            synthesise an ESC keypress at frame base+N (fires once,
 *                        before that frame's sim) by routing to the engine's
 *                        real esc_pressed() dispatch — so a recorded
 *                        dialogue-skip (ESC arms the skip-event prompt) replays
 *                        faithfully.  The live recorder drops ESC (it's a WndProc
 *                        VK_ESCAPE, not in the button mask); this op is how it
 *                        round-trips.  The retail Frida agent mirrors it by
 *                        posting WM_KEYDOWN(VK_ESCAPE) at the same frame.
 *   {"rngseed":[F,V]}    force the global LCG state to V (a bare uint32) at the
 *                        instant frame base+F is reached — BEFORE that frame's
 *                        sim RNG consumers run (it fires in the per-frame tick,
 *                        which the port runs in input_poll, ahead of sim).  Lets
 *                        a recorded segment reproduce its RNG-driven behaviour
 *                        (foot-dust jitter, NPC motion) regardless of how much
 *                        RNG the prepended boot/intro consumed: the distiller
 *                        snapshots the live LCG at record-start and re-injects it
 *                        at the recorded segment's first frame.  Fires once.  The
 *                        retail Frida agent mirrors it onto DAT_006023a0 at the
 *                        same frame, so both targets share one LCG stream from
 *                        the anchor (cross-target RNG parity).
 *
 *   {"gsimpin":[F,V]}    force g_sim_frame_count (DAT_0438b8cc) to V at base+F
 *                        (array form, like {rngseed}; fires once, pre-sim).
 *                        Pins the 目玉 display-sparkle %8 phase
 *                        (player_ctrl_display_sparkle_emit gates on
 *                        g_sim_frame_count%8==3): the port's counter ORIGIN
 *                        differs from retail's because the port skips the intro,
 *                        so the sparkle fires one frame off and shifts every
 *                        OTHER per-frame RNG consumer's LCG values.  Unlike
 *                        {phasepin} (which zeros g_sim but ALSO re-seeds the
 *                        bg-NPC LCG — that stalls the wrap-up cutscene), this
 *                        touches ONLY g_sim_frame_count.  V is retail's recorded
 *                        counter at the anchor, so forcing retail to it is a
 *                        no-op (preserves its natural sparkle phase) while the
 *                        port snaps to match.  The retail Frida agent mirrors it
 *                        onto DAT_0438b8cc at the same frame.  RE §21.
 *
 *   {"playtimepin":[F,V]} force the ACTIVE working slot's total-playtime frame
 *                        accumulator (bank dword SAVE_BANK_FIELD_PLAYTIME, engine
 *                        working DAT_044e37a0[slot]) to V at base+F (array form,
 *                        like {gsimpin}; fires once, pre-sim).  Playtime ticks +1
 *                        every live-scene frame (sim.c / engine FUN_004536cb head,
 *                        BEFORE the worker-load gate), so it counts the two
 *                        completion-based async-load brackets (house + pause menu)
 *                        whose duration is a wall-clock CreateThread race under
 *                        turbo — the port's swings ~4000 frames run-to-run while
 *                        retail (deterministic intro-video load) holds ~40.  On a
 *                        save COMMIT this drive-variable, phase-origin count is the
 *                        occupied_playtime bytes; a bilateral pin AFTER the last
 *                        variable load (SAVE_PICKER_READY) makes it deterministic +
 *                        equal on both targets so save.dat compares byte-exact.
 *                        Unlike {gsimpin} (V = retail's natural, a retail no-op),
 *                        V is a CHOSEN canonical origin forced on BOTH sides — but
 *                        the fire point is identical (input_poll, ahead of the sim
 *                        playtime tick) on port and agent, so both land on V+K after
 *                        K deterministic ticks to the commit snapshot: no off-by-one.
 *                        The retail Frida agent mirrors it onto DAT_044e37a0[slot].
 *
 *   {"bgnpcpin":[F,[d0..d149]]}
 *                        overwrite the background-window NPC SoA (DAT_073a7f80)
 *                        from SCENE1_BG_NPC_COUNT raw engine records captured from
 *                        retail's NATURAL state, at base+F (fires once, pre-sim).
 *                        The inner array is SEG_BGNPCPIN_DWORDS little-endian
 *                        engine dwords (6 records x 0x64).  PORT-ONLY: it pins the
 *                        port to the RECORDING's window-NPC positions so the shared
 *                        LCG draws in lockstep with retail from the anchor (the
 *                        rng-consumer-survey foundation — the warmup seeded the 6
 *                        NPCs off a different LCG origin, so boundary-respawns
 *                        crossed on different frames and drifted the whole stream).
 *                        Unlike {phasepin}'s synthetic 19937 re-seed, this is
 *                        retail's REAL drifted layout, so it matches the captured
 *                        trace.  The retail Frida agent SKIPS it (retail is the
 *                        un-pinned SOURCE of the capture).  RE §21.1.
 *
 *   {"memsnap":N}        dump the process's writable PE sections (.data/.bss)
 *                        to the capture dir at the deterministic frame base+N
 *                        (fires once, pre-sim, like {phasepin}) — the raw input
 *                        of the phase-state census (tools/phase_census.py): two
 *                        same-side runs with different pre-anchor timing diff
 *                        their dumps to enumerate ALL load-timing-dependent
 *                        state. The retail Frida agent mirrors it by dumping
 *                        the retail exe's writable sections at the same frame.
 *
 *   {"tutloadpin":N}     pin the tutorial-dialogue LOAD-BRACKET length to N
 *                        frames on BOTH targets (SEGMENT-SCOPED: applied at the
 *                        ENTRY of the segment that declares it and STICKY until
 *                        a later segment re-declares it; comparison
 *                        normalization, like {phasepin}).  Segment scope lets
 *                        the head pin stay SMALL (protecting a confirmed early
 *                        region's blink phase) while a late segment binds the
 *                        LONG dialogue-cutscene loads — a uniform large pin
 *                        would shift the early free-running %64 blink cadence
 *                        (DAY2 arc, RE §21 / cutscene-replay-anchor-drift).  The
 *                        retail bracket is a worker THREAD's wall-time (engine-
 *                        quirks §119: 2f and 5f for two activations on the SAME
 *                        capture), so every tutorial-load crossing shifts the
 *                        post-seam label axis by the bracket-length difference.
 *                        Port: overrides IVE_TUT_LOAD_FRAMES (the synthetic
 *                        D_TUT_LOAD length).  Retail (Frida agent): EXTEND-only —
 *                        BLOCKS the load worker at its tail until N frames past
 *                        the bracket start (the tail itself performs the whole
 *                        bracket-end handoff, quirk §119), so the engine idles
 *                        the extra frames exactly like a slow load (same
 *                        db054++/wing-emit consumption); a real load LONGER
 *                        than N is left alone (can't shorten a thread), so pick
 *                        N ≥ any plausible real bracket (≥ 8 recommended).
 *
 *   {"bgnpcseed":V}, {"bgnpcseed":[V,C]}, or {"bgnpcseed":[V,C,[d0..d(25*C-1)]]}
 *                        trace-global (RE §21.21/§21.22): seed the bg-NPC
 *                        warmup's LCG origin to V, its spawn cursor to C
 *                        (default 0 in scalar form), and the C dead slots'
 *                        [0,C) leftover engine-record state to the optional
 *                        3rd array (C {bgnpcpin}-format records, 25 dwords
 *                        each) right before its NATURAL first-ever tick
 *                        (scene1_bg_npc_seed_pin).  A narrower alternative to
 *                        {phasepin}, which ALSO zeros db054/anim/b154/rmb and
 *                        stalls the skip-path wrap-up cutscene.  Needed
 *                        because the warmup fires on the SAME frame the
 *                        primary-load busy gate releases, one frame before the
 *                        earliest a base-relative {rngseed} can mechanically
 *                        apply (anchors are detected post-sim, so a pin tied
 *                        to one can only fire starting the NEXT tick) — the
 *                        generic per-frame op is structurally always one frame
 *                        late for this same-frame consumer.  V is retail's
 *                        captured natural LCG state at the FUN_0046f621 entry
 *                        (NOT the {rngseed}-at-LOADING_END value, which is
 *                        already past this point).  C matters because the
 *                        cursor is not always 0 at that entry either: earlier
 *                        activity (title-screen bg render?) can leave slot 0
 *                        spawned-and-frozen (dir==0, the "unspawned"/dead
 *                        sentinel) before scene1's own warmup ever runs, so
 *                        the REAL spawn sequence starts at a later slot.  The
 *                        dead-slot array matters because dir==0 makes
 *                        bg_npc_tick's position update AND the sprite
 *                        renderer skip a dead slot, but the SHADOW renderer
 *                        only checks visible==-1 — so it still draws a dead
 *                        slot at whatever x/y/z it holds; left at the port's
 *                        BSS-zero default (no 3rd array) that is the world
 *                        origin, a stray shadow retail doesn't show.  The
 *                        retail Frida agent mirrors all three in
 *                        installBgNpcPinHook, gated on the warmup latch
 *                        (DAT_073a8bb8) still being 0.  Last declaration
 *                        wins; fires once, ever.
 *
 *   {"savefile":"<relpath>"} declare the save the trace booted with — a path
 *                        (relative to the trace file's directory) to a
 *                        content-addressed, gzip-compressed save blob (usually
 *                        tests/scenarios/_saves/<sha256>.sav.gz). On replay the
 *                        Python harness decompresses it and overrides whatever
 *                        save.dat is on disk via `--save-override`, so the trace
 *                        reproduces its exact save state regardless of the live
 *                        game's save. The port itself only records the ref (it
 *                        can't gunzip); loading is harness-driven. Not segment-
 *                        scoped — a trace-global declaration.
 *
 * Within a segment, frames are relative to that segment's base (the anchor
 * frame; base 0 for the boot segment). A trace with NO `wait` ops is a single
 * segment with base 0 — identical to an absolute input_trace replay.
 *
 * Because timing is keyed to anchors, the SAME trace drives the port and retail
 * to the same semantic instants despite the loading-screen frame jitter that
 * makes absolute frame numbers meaningless across targets (see
 * docs/plans/tas-framework.md).
 *
 * Pure C, no Win32 — host-testable. Frame counter is the caller's sim frame
 * (the port passes g_tick.frame_count, the same index anchors/captures use).
 */
#ifndef OPENRECET_INPUT_SEGTRACE_H
#define OPENRECET_INPUT_SEGTRACE_H

#include <stddef.h>
#include <stdint.h>

/* One within-segment input change-point (segment-relative frame). */
struct seg_entry {
    uint32_t frame;   /* relative to the segment base */
    uint16_t mask;    /* 14-bit button mask */
};

/* One base-relative call-trace window (resolved to [base+start, base+start+len)). */
struct seg_calltrace {
    uint32_t start;   /* relative to the segment base */
    uint32_t len;     /* window length in frames */
};

/* One base-relative contiguous capture range (resolved to the half-open frame
 * window [base+start, base+start+count)).  Unlike {capture:N} (which appends a
 * single frame to the bounded g_capture_frames[] list), a {caprange} drives a
 * lo/hi window test, so it can span hundreds of frames for a frame-by-frame
 * trace export without overflowing the capture-list cap (see {caprange} doc). */
struct seg_caprange {
    uint32_t start;   /* relative to the segment base */
    uint32_t count;   /* number of consecutive frames to capture */
};

/* One base-relative LCG-state force: set the global RNG state to `value` when
 * absolute frame base+frame is reached (fires once; see {rngseed} in the doc). */
struct seg_setrng {
    uint32_t frame;   /* relative to the segment base */
    uint32_t value;   /* LCG state to install */
    int      fired;   /* runtime: cleared on segment activation, set on fire */
};

/* One base-relative ESC synthesis: fire the engine's ESC dispatch when absolute
 * frame base+frame is reached (fires once; see {esc} in the doc). */
struct seg_esc {
    uint32_t frame;   /* relative to the segment base */
    int      fired;   /* runtime: cleared on segment activation, set on fire */
};

/* One base-relative global-frame-counter force: set g_tick.frame_count to `value`
 * when absolute frame base+frame is reached (fires once; see {gframe} in the
 * doc). EXPERIMENTAL — pins frame-count-derived state (e.g. time-of-day HUD
 * clock) so an anchor-rebased trace reproduces it across runs. */
struct seg_gframe {
    uint32_t frame;   /* relative to the segment base */
    uint32_t value;   /* global frame counter to install */
    int      fired;   /* runtime: cleared on segment activation, set on fire */
};

/* One base-relative sim-frame-counter force: set g_sim_frame_count to `value`
 * when absolute frame base+frame is reached (fires once; see {gsimpin} in the
 * doc).  Pins the 目玉 display-sparkle %8 phase WITHOUT the bg-NPC LCG re-seed
 * that {phasepin} bundles (which stalls the wrap-up cutscene). */
struct seg_gsimpin {
    uint32_t frame;   /* relative to the segment base */
    uint32_t value;   /* g_sim_frame_count to install */
    int      fired;   /* runtime: cleared on segment activation, set on fire */
};

/* One base-relative playtime-accumulator force: set the active working slot's
 * total-playtime frame count (SAVE_BANK_FIELD_PLAYTIME) to `value` when absolute
 * frame base+frame is reached (fires once; see {playtimepin} in the doc).
 * Normalizes the drive-variable async-load-bracket phase origin so a save COMMIT
 * writes a deterministic, cross-target-equal occupied_playtime. */
struct seg_playtimepin {
    uint32_t frame;   /* relative to the segment base */
    uint32_t value;   /* working-slot playtime frame count to install */
    int      fired;   /* runtime: cleared on segment activation, set on fire */
};

/* One engine record's dword count (BG_NPC_ENGINE_DWORDS = 25 = 0x64/4).
 * Kept as a literal (like SEG_BGNPCPIN_DWORDS below) so this module need not
 * include scene1_bg_npc.h. */
#define SEG_BGNPC_RECORD_DWORDS 25

/* Payload size of one {bgnpcpin} op: SCENE1_BG_NPC_COUNT(6) engine records of
 * BG_NPC_ENGINE_DWORDS(25 = 0x64/4) dwords each.  Kept as a literal so this
 * module need not include scene1_bg_npc.h; the consumer (scene1_bg_npc_pin)
 * re-derives the record count from n_dwords and asserts the layout. */
#define SEG_BGNPCPIN_DWORDS (SEG_BGNPC_RECORD_DWORDS * 6)

/* One base-relative background-NPC SoA pin: at absolute frame base+frame,
 * overwrite the live DAT_073a7f80 records from `values` (retail's captured
 * natural layout) so the port's window NPCs match the recording (fires once;
 * see {bgnpcpin} in the doc).  PORT-ONLY — the retail agent skips it. */
struct seg_bgnpcpin {
    uint32_t frame;                       /* relative to the segment base */
    uint32_t values[SEG_BGNPCPIN_DWORDS]; /* raw engine SoA dwords (LE) */
    int      fired;   /* runtime: cleared on segment activation, set on fire */
};

/* One base-relative phase normalization: reset the companion's load-time-
 * dependent free-roam phase (db054 bob/sparkle counter + sprite anim cycle) to a
 * canonical zero when absolute frame base+frame is reached (fires once; see
 * {phasepin} in the doc).  Trace-comparison ONLY — factors out the intro-length
 * phase offset (engine-quirks §94) so port↔retail diffs are phase-clean. */
struct seg_phasepin {
    uint32_t frame;   /* relative to the segment base */
    int      fired;   /* runtime: cleared on segment activation, set on fire */
};

/* One base-relative memory snapshot: dump the writable PE sections when
 * absolute frame base+frame is reached (fires once; see {memsnap} in the doc).
 * Phase-census input — see tools/phase_census.py. */
struct seg_memsnap {
    uint32_t frame;   /* relative to the segment base */
    int      fired;   /* runtime: cleared on segment activation, set on fire */
};

/* A maximal run of entries terminated by a `wait` (or the trace end). */
struct seg_segment {
    struct seg_entry *entries;
    size_t            n_entries, cap_entries;
    uint32_t         *captures;     /* base-relative capture frames (N) */
    size_t            n_captures, cap_captures;
    struct seg_calltrace *calltraces;   /* base-relative call-trace windows */
    size_t            n_calltraces, cap_calltraces;
    struct seg_caprange *capranges;     /* base-relative contiguous capture windows */
    size_t            n_capranges, cap_capranges;
    struct seg_setrng *setrngs;     /* base-relative LCG-state forces */
    size_t            n_setrngs, cap_setrngs;
    struct seg_esc   *escs;         /* base-relative ESC synthesis points */
    size_t            n_escs, cap_escs;
    struct seg_gframe *gframes;     /* base-relative global-frame-counter forces */
    size_t            n_gframes, cap_gframes;
    struct seg_gsimpin *gsimpins;   /* base-relative sim-frame-counter forces */
    size_t            n_gsimpins, cap_gsimpins;
    struct seg_playtimepin *playtimepins; /* base-relative playtime-accum forces */
    size_t            n_playtimepins, cap_playtimepins;
    struct seg_bgnpcpin *bgnpcpins; /* base-relative bg-NPC SoA pins */
    size_t            n_bgnpcpins, cap_bgnpcpins;
    struct seg_phasepin *phasepins; /* base-relative companion-phase normalizers */
    size_t            n_phasepins, cap_phasepins;
    struct seg_memsnap *memsnaps;   /* base-relative writable-section dumps */
    size_t            n_memsnaps, cap_memsnaps;
    /* Segment-scoped {tutloadpin} (applied at this segment's ENTRY via the
     * tutloadpin callback; sticky until a later segment re-declares one).
     * has_tutloadpin==0 leaves the current pin unchanged. */
    uint32_t          tutloadpin;
    int               has_tutloadpin;
    char              wait[24];     /* terminating anchor name; "" if none */
    int               has_wait;
    /* Optional {wait} timeout (frames since the segment was entered). 0 = wait
     * forever (default).  When >0 and the anchor has not fired within this many
     * frames, the segtrace SKIPS the wait and advances WITHOUT adopting a new
     * base — so the next segment's frames/caprange stay relative to the last
     * RESOLVED anchor.  This bridges a CROSS-TARGET load-structure mismatch: a
     * recording captured on retail can carry load-cycle anchors (LOADING_START/
     * END burst) that the PORT collapses into fewer loads (the "port loads
     * faster" phase pillar); the port skips the load-cycle waits it never
     * reproduces and still lands the tutorial inputs on the post-load free-roam.
     * Port-only: the Frida retail agent ignores the field and follows every
     * anchor (it DOES reproduce all the loads), so the same trace drives both. */
    uint32_t          wait_timeout;
};

#define SEGTRACE_MAX_FIRED 24       /* distinct anchor names we can track */

struct input_segtrace {
    struct seg_segment *segs;
    size_t              n_segs, cap_segs;

    /* Optional embedded-save reference, from a `{"savefile":"<relpath>"}` op
     * (see input_segtrace.h doc). Path is relative to the trace file's own
     * directory; the value points at the save blob a recorded trace booted
     * with (usually a content-addressed `.sav.gz` under tests/scenarios/_saves).
     *
     * The port does NOT load this directly — the blob is gzip-compressed and
     * decompression lives in the Python harness, which resolves the ref and
     * passes the decompressed raw via `--save-override`. This field is parsed
     * and stored only so (a) the C parser doesn't reject the op, and (b) the
     * ref is inspectable/loggable. `has_savefile` is 0 when no op was seen. */
    char     savefile[256];
    int      has_savefile;

    /* Optional two-tier capture cadence, from a `{"capstride":N}` op (Trace
     * Studio v2 D3). Trace-global (last declaration wins). When >1, a {caprange}
     * window captures only every Nth frame from its start (base+S, base+S+N, …) —
     * a coarse OVERVIEW. The host (main.c) applies it to its lo/hi range test; the
     * Frida agent strides its capture-pending fill the same way, so both targets
     * keep the identical anchor-relative kept-set. `has_capstride` is 0 when no op
     * was seen; `capstride` is then unset (treat as 1 = every frame). */
    uint32_t capstride;
    int      has_capstride;

    /* {tutloadpin:N} is SEGMENT-SCOPED — stored on struct seg_segment
     * (tutloadpin/has_tutloadpin) and applied at each declaring segment's
     * entry via the tutloadpin callback (see rearm_tutloadpins).  There is no
     * trace-global tutloadpin field; the head pin lives on segment 0. */

    /* Optional cc08==4 d3e load-bracket pin, from a `{"csloadpin":N}` op (trace-
     * global, last declaration wins).  When set, the host holds b1cc==2 for N
     * frames (customer_service_set_load_pin); the Frida agent mirrors it on the
     * retail d3e worker tail.  `has_csloadpin` is 0 when no op was seen. */
    uint32_t csloadpin;
    int      has_csloadpin;

    /* Optional cad868 PRIMARY-worker load-duration pin, from a
     * `{"primaryloadpin":N}` op (trace-global, last declaration wins).  When set,
     * the host drains the primary worker (worker_load_set_primary_pin) so the
     * initial Continue-load / scene reload lasts a deterministic N frames; the
     * Frida agent mirrors it by draining the retail primary worker to the same N
     * (a bilateral pin — RE §21.19(b)).  `has_primaryloadpin` is 0 when no op was
     * seen. */
    uint32_t primaryloadpin;
    int      has_primaryloadpin;

    /* Optional bg-NPC warmup seed pin, from a `{"bgnpcseed":V}`,
     * `{"bgnpcseed":[V,C]}`, or `{"bgnpcseed":[V,C,[d0..]]}` op (trace-global,
     * last declaration wins; RE §21.21/§21.22).  When set, the host seeds the
     * shared LCG to V, the spawn cursor to C, and the `bgnpcseed_dead_n`
     * dwords of raw engine records (dead-slot leftover state — the shadow
     * pass draws them regardless of dir==0) right before the bg-NPC warmup's
     * NATURAL first-ever tick (scene1_bg_npc_seed_pin); the Frida agent
     * mirrors all three at the FUN_0046f621 entry.  `has_bgnpcseed` is 0 when
     * no op was seen; `bgnpcseed_cursor`/`bgnpcseed_dead_n` are 0 (the
     * scalar-form / two-element-array default) unless set explicitly. */
    uint32_t bgnpcseed;
    int      bgnpcseed_cursor;
    uint32_t bgnpcseed_dead[SEG_BGNPCPIN_DWORDS];
    int      bgnpcseed_dead_n;
    int      has_bgnpcseed;

    /* Runtime state (advanced by input_segtrace_tick). */
    int      started;
    size_t   cur_seg;
    size_t   cur_entry;
    uint32_t base;       /* absolute frame of the current segment's frame 0 */
    uint32_t base_arm;   /* frame the current segment was entered (wait guard) */
    char     base_anchor[24]; /* anchor name that entered the current segment */
    uint16_t sticky;     /* last applied mask (held between entries) */

    /* Anchor fire-frame map (latest-wins), fed by input_segtrace_on_anchor. */
    struct { char name[24]; uint32_t frame; int set; } fired[SEGTRACE_MAX_FIRED];

    /* Call-trace window callback (set once via input_segtrace_set_calltrace_cb);
     * fired per resolved {calltrace} op when its segment becomes active. */
    void (*ct_cb)(uint32_t lo, uint32_t hi, void *user);
    void  *ct_user;

    /* RNG-state force callback (set once via input_segtrace_set_rngseed_cb);
     * fired per {rngseed} op when its frame base+frame is reached (in-tick,
     * before sim).  Kept a callback so this module stays free of rng.h. */
    void (*rng_cb)(uint32_t value, void *user);
    void  *rng_user;

    /* Capture-range callback (set once via input_segtrace_set_caprange_cb);
     * fired per {caprange} op when its segment becomes active, with the resolved
     * half-open window [base+start, base+start+count).  Kept a callback so the
     * host owns the lo/hi window state. */
    void (*cr_cb)(uint32_t lo, uint32_t hi, void *user);
    void  *cr_user;

    /* ESC-synthesis callback (set once via input_segtrace_set_esc_cb); fired per
     * {esc} op when its frame base+frame is reached (in-tick, before sim).  Kept
     * a callback so this module stays free of the engine's WndProc/dispatch. */
    void (*esc_cb)(void *user);
    void  *esc_user;

    /* Global-frame-counter force callback (set once via
     * input_segtrace_set_gframe_cb); fired per {gframe} op when its frame
     * base+frame is reached.  Kept a callback so this module stays free of
     * tick.h.  EXPERIMENTAL — see struct seg_gframe. */
    void (*gf_cb)(uint32_t value, void *user);
    void  *gf_user;

    /* Sim-frame-counter force callback (set once via
     * input_segtrace_set_gsimpin_cb); fired per {gsimpin} op when its frame
     * base+frame is reached.  Pins g_sim_frame_count (the 目玉-sparkle %8 phase)
     * without the {phasepin} bg-NPC re-seed.  Kept a callback so this module
     * stays free of sim.h. */
    void (*gp_cb)(uint32_t value, void *user);
    void  *gp_user;

    /* Playtime-accumulator force callback (set once via
     * input_segtrace_set_playtimepin_cb); fired per {playtimepin} op when its
     * frame base+frame is reached.  Writes the active working slot's playtime
     * frame count (normalizes the async-load-bracket phase origin for a save
     * COMMIT).  Kept a callback so this module stays free of save_work.h. */
    void (*ptp_cb)(uint32_t value, void *user);
    void  *ptp_user;

    /* Background-NPC SoA pin callback (set once via input_segtrace_set_bgnpcpin_cb);
     * fired per {bgnpcpin} op when its frame base+frame is reached (in-tick,
     * before sim).  Gets the captured engine-record dwords + their count.  Kept a
     * callback so this module stays free of scene1_bg_npc.h. */
    void (*bnp_cb)(const uint32_t *values, size_t n, void *user);
    void  *bnp_user;

    /* Phase-normalization callback (set once via input_segtrace_set_phasepin_cb);
     * fired per {phasepin} op when its frame base+frame is reached.  Kept a
     * callback so this module stays free of the companion controller. */
    void (*pp_cb)(void *user);
    void  *pp_user;

    /* Memory-snapshot callback (set once via input_segtrace_set_memsnap_cb);
     * fired per {memsnap} op at frame base+N with the RESOLVED frame base+N
     * (stable dump filenames across runs).  Kept a callback so this module
     * stays free of memsnap.h/Win32. */
    void (*ms_cb)(uint32_t frame, void *user);
    void  *ms_user;

    /* Tutorial-load-bracket pin callback (set once via
     * input_segtrace_set_tutloadpin_cb); fired at the ENTRY of each segment
     * that declares a {tutloadpin} with that segment's N (segment-scoped,
     * sticky — see rearm_tutloadpins).  Kept a callback so this module stays
     * free of the intro-dialogue controller. */
    void (*tlp_cb)(uint32_t value, void *user);
    void  *tlp_user;
};

/* Capture callback: invoked once per scheduled `{capture:N}` with the resolved
 * absolute frame (base+N) when its segment becomes active. */
typedef void (*segtrace_capture_fn)(uint32_t frame, void *user);

/* Call-trace window callback: invoked once per scheduled `{calltrace:[S,L]}`
 * with the resolved absolute half-open window [base+S, base+S+L). */
typedef void (*segtrace_calltrace_fn)(uint32_t lo, uint32_t hi, void *user);

/* RNG-state force callback: invoked once per `{rngseed:[F,V]}` op with the
 * LCG state V, at the frame base+F (before that frame's sim consumers). */
typedef void (*segtrace_rngseed_fn)(uint32_t value, void *user);

/* Capture-range callback: invoked once per `{caprange:[S,C]}` op with the
 * resolved half-open window [base+S, base+S+C) when its segment becomes active. */
typedef void (*segtrace_caprange_fn)(uint32_t lo, uint32_t hi, void *user);

/* ESC-synthesis callback: invoked once per `{esc:N}` op at frame base+N (before
 * that frame's sim consumers). */
typedef void (*segtrace_esc_fn)(void *user);

/* Set the call-trace window callback (and its user ptr).  Resolved windows fire
 * through it as their segments become active, same timing as captures. */
void input_segtrace_set_calltrace_cb(struct input_segtrace *st,
                                     segtrace_calltrace_fn cb, void *user);

/* Set the RNG-state force callback (and its user ptr).  Fires per {rngseed} op
 * when its frame is reached during input_segtrace_tick. */
void input_segtrace_set_rngseed_cb(struct input_segtrace *st,
                                   segtrace_rngseed_fn cb, void *user);

/* Global-frame-counter force callback: invoked once per `{gframe:[F,V]}` op with
 * the value V, at the frame base+F.  EXPERIMENTAL (see struct seg_gframe). */
typedef void (*segtrace_gframe_fn)(uint32_t value, void *user);

/* Set the global-frame-counter force callback (and its user ptr).  Fires per
 * {gframe} op when its frame is reached during input_segtrace_tick. */
void input_segtrace_set_gframe_cb(struct input_segtrace *st,
                                  segtrace_gframe_fn cb, void *user);

/* Sim-frame-counter force callback: invoked once per `{gsimpin:[F,V]}` op with
 * the value V, at the frame base+F.  Pins the 目玉-sparkle %8 phase. */
typedef void (*segtrace_gsimpin_fn)(uint32_t value, void *user);

/* Set the sim-frame-counter force callback (and its user ptr).  Fires per
 * {gsimpin} op when its frame is reached during input_segtrace_tick. */
void input_segtrace_set_gsimpin_cb(struct input_segtrace *st,
                                   segtrace_gsimpin_fn cb, void *user);

/* Playtime-accumulator force callback: invoked once per `{playtimepin:[F,V]}` op
 * with the value V, at the frame base+F.  Sets the active working slot's total
 * playtime frame count (normalizes the async-load-bracket phase origin). */
typedef void (*segtrace_playtimepin_fn)(uint32_t value, void *user);

/* Set the playtime-accumulator force callback (and its user ptr).  Fires per
 * {playtimepin} op when its frame is reached during input_segtrace_tick. */
void input_segtrace_set_playtimepin_cb(struct input_segtrace *st,
                                       segtrace_playtimepin_fn cb, void *user);

/* Background-NPC SoA pin callback: invoked once per `{bgnpcpin:[F,[...]]}` op
 * with the captured engine-record dwords (length n), at the frame base+F. */
typedef void (*segtrace_bgnpcpin_fn)(const uint32_t *values, size_t n,
                                     void *user);

/* Set the background-NPC SoA pin callback (and its user ptr).  Fires per
 * {bgnpcpin} op when its frame is reached during input_segtrace_tick. */
void input_segtrace_set_bgnpcpin_cb(struct input_segtrace *st,
                                    segtrace_bgnpcpin_fn cb, void *user);

/* Phase-normalization callback: invoked once per `{phasepin:N}` op at frame
 * base+N (before that frame's sim consumers).  Resets the companion's
 * load-dependent free-roam phase so a trace comparison is phase-clean. */
typedef void (*segtrace_phasepin_fn)(void *user);

/* Set the phase-normalization callback (and its user ptr).  Fires per {phasepin}
 * op when its frame is reached during input_segtrace_tick. */
void input_segtrace_set_phasepin_cb(struct input_segtrace *st,
                                    segtrace_phasepin_fn cb, void *user);

/* Memory-snapshot callback: invoked once per `{memsnap:N}` op with the
 * RESOLVED frame base+N (pre-sim, same window as {phasepin}). */
typedef void (*segtrace_memsnap_fn)(uint32_t frame, void *user);

/* Set the memory-snapshot callback (and its user ptr).  Fires per {memsnap}
 * op when its frame is reached during input_segtrace_tick. */
void input_segtrace_set_memsnap_cb(struct input_segtrace *st,
                                   segtrace_memsnap_fn cb, void *user);

/* Tutorial-load-bracket pin callback: invoked at the ENTRY of each segment that
 * declares a {tutloadpin:N} with that segment's N (segment-scoped, sticky). */
typedef void (*segtrace_tutloadpin_fn)(uint32_t value, void *user);

/* Set the tutorial-load-bracket pin callback (and its user ptr).  Fires at each
 * declaring segment's entry during input_segtrace_tick (segment-scoped). */
void input_segtrace_set_tutloadpin_cb(struct input_segtrace *st,
                                      segtrace_tutloadpin_fn cb, void *user);

/* Set the capture-range callback (and its user ptr).  Resolved windows fire
 * through it as their segments become active, same timing as captures. */
void input_segtrace_set_caprange_cb(struct input_segtrace *st,
                                    segtrace_caprange_fn cb, void *user);

/* Set the ESC-synthesis callback (and its user ptr).  Fires per {esc} op when
 * its frame is reached during input_segtrace_tick. */
void input_segtrace_set_esc_cb(struct input_segtrace *st,
                               segtrace_esc_fn cb, void *user);

/* True if the loaded trace declares ≥1 {calltrace} op (any segment).  Lets the
 * harness auto-enable call-tracing from the trace alone. */
int  input_segtrace_has_calltrace(const struct input_segtrace *st);

/* True if the loaded trace declares ≥1 {bgnpcpin} op (the f406 first-customer
 * marker).  Auto-arms the wrap-up skip driver, mirroring the retail capture
 * (viewer note #3, RE §21.5/§21.6). */
int  input_segtrace_has_bgnpcpin(const struct input_segtrace *st);

/* Parse a trace from an in-memory buffer / file into `out` (cleared first).
 * Returns 1 on success, 0 on malformed input or OOM. */
int  input_segtrace_parse_buf(const char *buf, size_t len, struct input_segtrace *out);
int  input_segtrace_load(const char *path, struct input_segtrace *out);
void input_segtrace_free(struct input_segtrace *st);

/* Record an anchor's fire frame (call from the anchor sink each time one
 * fires). Latest firing wins so a repeated anchor resolves the next `wait`. */
void input_segtrace_on_anchor(struct input_segtrace *st,
                              const char *name, uint32_t frame);

/* Resolve the input mask for absolute sim frame `frame`, advancing the segment
 * state machine. When a segment becomes active its `{capture:N}` ops are
 * resolved to base+N and reported via `capture_cb` (may be NULL). */
uint16_t input_segtrace_tick(struct input_segtrace *st, uint32_t frame,
                             segtrace_capture_fn capture_cb, void *user);

#endif /* OPENRECET_INPUT_SEGTRACE_H */
