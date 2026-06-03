/*
 * anchor_trace.h — deterministic *event* stream for the TAS framework.
 *
 * Companion to input_trace. Where input_trace is the *input* source of
 * truth (what the player pressed each frame), anchor_trace is the
 * *event* stream the harness aligns on: named, edge-triggered markers
 * keyed off the sim-frame counter.
 *
 *   {"anchor":"NEW_GAME","frame":3200}
 *   {"anchor":"LOADING_START","frame":3210}
 *   {"anchor":"LOADING_END","frame":3258}
 *   {"anchor":"HOUSE_FREEROAM","frame":3258}
 *
 * Why this exists (the determinism story — see docs/plans/tas-framework.md):
 * the engine reaches a given game state at a *non-deterministic frame
 * number*. The asset load between "new game" and the playable HOUSE runs
 * on a worker thread (src/worker_load.c `CreateThread`); the main loop
 * spins a variable number of `nowloading` frames while it completes, so
 * the absolute frame at which HOUSE becomes playable drifts with load
 * wall-time (cold vs warm cache, host contention) — and differs *wildly*
 * between the port and retail. The *sim itself* is bit-exact given the
 * same inputs + clock + RNG seed (verified: a trace double-replayed on
 * the port is byte-identical frame-for-frame), but the per-boot loading
 * duration is not. Absolute frame numbers therefore can't align two runs.
 *
 * Anchors fix that: both targets emit the *same* named events, and the
 * harness captures/diffs *relative to an anchor* ("HOUSE_FREEROAM + k"),
 * so port-frame-3258 and retail-frame-9000 are recognised as the same
 * instant. `freeze_count_until: ANCHOR` means "stop counting frames
 * through the non-deterministic load; resume at the anchor."
 *
 * The wire format is shared with the retail Frida agent's `kind:"anchor"`
 * message (same anchor NAMES, same `{anchor,frame}` shape) so one spec
 * drives both sides.
 *
 * This module is pure C — no Win32, no engine globals. The caller builds
 * a `struct anchor_world` snapshot from whatever globals it tracks and
 * passes it in each frame; the module computes rising edges against the
 * previous snapshot and emits via a sink callback. That keeps the edge
 * logic unit-testable with no engine in the loop.
 */
#ifndef OPENRECET_ANCHOR_TRACE_H
#define OPENRECET_ANCHOR_TRACE_H

#include <stdint.h>
#include <stdio.h>

/* Per-frame observable snapshot. Extend as more anchor sources are
 * needed (the TAS plan lists first-3D-draw, state-stable, menu cursor,
 * …); add a field here + an edge rule in anchor_trace.c, nothing else. */
struct anchor_world {
    int32_t scene_state;     /* g_scene_state: 0 TITLE / 1 INGAME / 8 LOADING */
    int     loading_active;  /* nowloading_is_active() — the worker-load gate */

    /* Opening-prologue dialogue reveal state (TEXT_ANIM_START/END — see
     * docs/findings/opening-prologue.md §RESOLVED). Sourced from the dialogue
     * engine globals once it's ported (DAT_0438b1c8 / DAT_073a3e00 /
     * DAT_073a3e04); zero until then, so the text edges never fire. */
    int     dlg_active;      /* DAT_0438b1c8 == 1 — dialogue running */
    int32_t text_reveal;     /* DAT_073a3e00 — per-char reveal counter (1..0x800) */
    int     text_revealed;   /* DAT_073a3e04 != 0 — current line fully revealed */

    /* Catch-all dialogue "extra/effect sprite" visibility (the sigh / zzz /
     * sweat-drop etc. pop-ups + the kuro fade-from-black) — max alpha (0-255)
     * over the active non-character standees (index >= 2; chr 0/1 are the
     * persistent speakers). 0 outside dialogue. Drives the four EXTRA_SPRITE_*
     * lifecycle anchors so a TAS trace can frame the fade in/out of any effect
     * sprite without hard-coding which standee/line it is. */
    int32_t fx_alpha;

    /* 1 while a dialogue line is shown (DAT_073a6a38 >= 0); 0 between lines (box
     * closing/gone) or outside dialogue. Drives DLG_LINE_CLEAR (1→0, the
     * box-dismissed edge) / DLG_LINE_SHOW (0→1) so a trace can frame exactly the
     * between-lines gap (after a line is dismissed, before the next appears). */
    int dlg_line_present;

    /* Player actor state-machine field (record dword 5 = engine DAT_056daafc;
     * CHR_ACTOR_STATE). 6 = the iv1_2 face-to-face conversation pose (Recette
     * looking up at Tear); 0 = free-roam. Drives CONV_POSE_START/END so a TAS
     * trace can anchor to the pose's OWN edge (the blink resets on it) instead
     * of a fixed HOUSE_FREEROAM+N — see engine-quirks §85/§86. 0 pre-HOUSE. */
    int conv_pose_state;

    /* 1 while Recette is mid-blink (eyes closed, cell 39) during the pose; 0
     * otherwise. Drives CONV_POSE_BLINK — a clean post-load sync point (the
     * pose-entry edge lands in the load fade; the blink does not). */
    int conv_pose_blink;

    /* 1 once the opening prologue dialogue sequence has fully ended (iv1_1 →
     * iv1_2 both complete or skipped → the intro state machine reaches D_DONE),
     * i.e. the player has just gained free control. 0 while any intro script is
     * still running (incl. the inter-script load) and 0 pre-HOUSE / dormant.
     * Drives FREEROAM_START — the canonical "player-controllable free roam
     * begins" sync point, which lands AFTER the 2nd ESC→confirm skip (unlike
     * HOUSE_FREEROAM, which fires when the load overlay drops, still mid-iv1_2).
     * This is the anchor to rebase a recorded free-roam walk onto. */
    int intro_done;
};

/* Sink for one emitted anchor. `name` is a stable UPPER_SNAKE token;
 * `frame` is the sim-frame index it fired on. `user` is the opaque
 * pointer passed to anchor_trace_tick (e.g. a FILE* or a context
 * struct). Called zero-or-more times per tick, in table order. */
typedef void (*anchor_sink_fn)(const char *name, uint32_t frame, void *user);

/* Opaque-ish edge-detector state. Zero-initialize before first use
 * (`struct anchor_trace_state st = {0};`) — the first tick then emits
 * the BOOT anchor and seeds the baseline with no spurious edges. */
struct anchor_trace_state {
    int                 initialized;
    struct anchor_world prev;
};

/* Evaluate `cur` against the stored previous snapshot and emit every
 * anchor whose rising edge fired this frame via `sink`. Updates the
 * stored snapshot. On the very first call (state zero-initialized) it
 * emits BOOT and records the baseline; no other anchor can fire on the
 * first tick (there is no "previous" to edge against). */
void anchor_trace_tick(struct anchor_trace_state *st, uint32_t frame,
                       struct anchor_world cur,
                       anchor_sink_fn sink, void *user);

/* Convenience sink: writes one JSONL line `{"anchor":"NAME","frame":N}\n`
 * to the FILE* in `user`. Used for both the --anchor-trace-record file
 * and (via a tee) the stderr echo. */
void anchor_trace_sink_jsonl(const char *name, uint32_t frame, void *user);

#endif /* OPENRECET_ANCHOR_TRACE_H */
