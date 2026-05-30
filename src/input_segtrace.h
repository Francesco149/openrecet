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
 *   {"calltrace":...}    retail-side behavioral-probe op; PARSED BUT IGNORED on
 *                        the port (the port's call tracer is the compile-time
 *                        CALL_TRACE_ENTER system, not a runtime VA window). Kept
 *                        in the grammar so one trace file loads on both targets.
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

/* A maximal run of entries terminated by a `wait` (or the trace end). */
struct seg_segment {
    struct seg_entry *entries;
    size_t            n_entries, cap_entries;
    uint32_t         *captures;     /* base-relative capture frames (N) */
    size_t            n_captures, cap_captures;
    char              wait[24];     /* terminating anchor name; "" if none */
    int               has_wait;
};

#define SEGTRACE_MAX_FIRED 24       /* distinct anchor names we can track */

struct input_segtrace {
    struct seg_segment *segs;
    size_t              n_segs, cap_segs;

    /* Runtime state (advanced by input_segtrace_tick). */
    int      started;
    size_t   cur_seg;
    size_t   cur_entry;
    uint32_t base;       /* absolute frame of the current segment's frame 0 */
    uint32_t base_arm;   /* frame the current segment was entered (wait guard) */
    uint16_t sticky;     /* last applied mask (held between entries) */

    /* Anchor fire-frame map (latest-wins), fed by input_segtrace_on_anchor. */
    struct { char name[24]; uint32_t frame; int set; } fired[SEGTRACE_MAX_FIRED];
};

/* Capture callback: invoked once per scheduled `{capture:N}` with the resolved
 * absolute frame (base+N) when its segment becomes active. */
typedef void (*segtrace_capture_fn)(uint32_t frame, void *user);

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
