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
 *                        frames on BOTH targets (trace-global, last declaration
 *                        wins; comparison normalization, like {phasepin}).  The
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
    struct seg_phasepin *phasepins; /* base-relative companion-phase normalizers */
    size_t            n_phasepins, cap_phasepins;
    struct seg_memsnap *memsnaps;   /* base-relative writable-section dumps */
    size_t            n_memsnaps, cap_memsnaps;
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

    /* Optional tutorial-load-bracket pin, from a `{"tutloadpin":N}` op (trace-
     * global, last declaration wins).  When set, the host overrides the
     * tutorial dialogue's synthetic load-bracket length (IVE_TUT_LOAD_FRAMES)
     * with N; the Frida agent mirrors it by holding the retail load gate to N
     * frames (extend-only).  `has_tutloadpin` is 0 when no op was seen. */
    uint32_t tutloadpin;
    int      has_tutloadpin;

    /* Optional cc08==4 d3e load-bracket pin, from a `{"csloadpin":N}` op (trace-
     * global, last declaration wins).  When set, the host holds b1cc==2 for N
     * frames (customer_service_set_load_pin); the Frida agent mirrors it on the
     * retail d3e worker tail.  `has_csloadpin` is 0 when no op was seen. */
    uint32_t csloadpin;
    int      has_csloadpin;

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
