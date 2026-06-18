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

    /* The in-game interaction state DAT_0438cc08 (player_ctrl_cc08): 1 free-roam,
     * 4 = the in-shop customer-service / price-haggle SELLING mode is active (the
     * player Z'd at the sell counter). Drives CUSTOMER_SERVICE_ENTER (non-4 → 4) —
     * the unambiguous "selling mode up" sync point for a haggle trace. The generic
     * {wait:LOADING_END} can't sync the haggle window: several loads fire (the
     * Continue-load, the cc08==4 d3e asset load) and that wait resolves to a
     * DIFFERENT physical load per side, so a caprange rebased on it opens at a
     * different cc08-offset on each side. 0/1 outside selling mode. */
    int32_t cc08;

    /* 1 while the in-game PAUSE menu is open (engine DAT_0438b150 != 0; set 1 by
     * scene_pause_state_init, cleared 0 on close). Drives PAUSE_OPEN (0→1) /
     * PAUSE_CLOSE (1→0) so the save/pause-menu navigation in a TAS trace re-syncs
     * to the menu's own edges instead of drifting between the coarse LOADING
     * anchors (the save→quit-to-title→reload flow). 0 outside the pause menu. */
    int pause_active;

    /* 1 while the pause menu's SAVE submenu (entry type 3) is fully open and
     * NAVIGABLE (scene mode 9, sub_anim==10, Save selected — the picker nav
     * FUN_0047f5bc runs). Drives SAVE_PICKER_READY (0→1), the robust sync point
     * for save-picker navigation: the pause OPEN ramp lands at a per-side-
     * variable point in the async pause-asset load (so PAUSE_READY+offset inputs
     * reach the picker at a DIFFERENT picker-time per side — the selected card
     * breathes sin(g_save_picker_frame·0.1) out of phase). Re-anchoring the nav
     * HERE makes the inputs picker-time-relative ⇒ cursor/scroll AND breathing
     * align. 0 outside the open Save submenu. */
    int save_picker_active;

    /* 1 while the pause menu's ENCYCLOPEDIA submenu (entry type 6) is fully open
     * and navigable (scene 9, sub_anim==10, Encyclopedia selected). Drives
     * ENCYCLOPEDIA_READY (0→1) — the same per-side-pause-load rebase the save
     * picker uses, so the hand-cursor bob + grid nav inputs align picker-time-
     * relative. 0 outside the open Encyclopedia submenu. */
    int encyclopedia_active;

    /* 1 while the pause menu's OPTIONS submenu (entry type 2) is fully open and
     * navigable (scene 9, sub_anim==10, Options selected). Drives OPTIONS_READY
     * (0→1) — the same per-side-pause-load rebase, so the config-panel nav inputs
     * align picker-time-relative AND the v3 join keys to OPTIONS_READY (which
     * fires AFTER PAUSE_OPEN on both sides, unlike PAUSE_READY which PAUSE_OPEN
     * straddles). 0 outside the open Options submenu. */
    int options_active;

    /* 1 while the pause menu's ITEMS submenu (entry type 1) is fully open and
     * navigable (scene 9, sub_anim==10, Items selected). Drives ITEMS_READY (0→1)
     * — the same per-side-pause-load rebase, so the grid nav + the shared hand-
     * cursor bob align picker-time-relative AND the v3 join keys to ITEMS_READY
     * (fires AFTER PAUSE_OPEN on both sides, unlike PAUSE_READY which PAUSE_OPEN
     * straddles). 0 outside the open Items submenu. */
    int items_active;

    /* 1 while the TITLE-screen Continue/load slot picker (submenu_state 1) is
     * fully open and navigable (scene mode 0, submenu_state == 1, cursor_anim
     * == 10). Drives TITLE_PICKER_READY (0→1). Unlike the pause submenus the
     * title picker has NO async asset load, so it fires at the same picker-
     * relative frame on both sides — the clean v3 join anchor for the title
     * Continue/load picker render (FUN_0049b556). 0 outside the open picker. */
    int title_picker_active;

    /* 1 while the TITLE-screen Options/settings submenu (submenu_state 2) is
     * fully open + navigable (scene 0, submenu_state == 2, cursor_anim == 10).
     * Drives TITLE_SETTINGS_READY (0→1) — like the picker, no async load, so a
     * clean +0-stretch v3 join for the title settings render (FUN_0049c050). 0
     * outside the open settings submenu. */
    int title_settings_active;

    /* 1 while the TITLE-screen all-banks ENCYCLOPEDIA (図鑑, submenu_state 3) is
     * fully open + navigable (scene 0, submenu_state == 3, cursor_anim == 10).
     * Drives TITLE_ENCYCLOPEDIA_READY (0→1) — like the picker/settings, no async
     * load, so a clean +0-stretch v3 join for the title encyclopedia render
     * (FUN_0049f8b8). 0 outside the open encyclopedia. */
    int title_encyclopedia_active;

    /* 1 while the TITLE-screen Records / high-score screen (submenu_state 4) is
     * fully open + navigable (scene 0, submenu_state == 4, cursor_anim == 10).
     * Drives TITLE_RECORDS_READY (0→1) — like the picker/settings/encyclopedia,
     * no async load, so a clean +0-stretch v3 join for the title records render
     * (FUN_0049c439). 0 outside the open Records screen. */
    int title_records_active;

    /* 1 while the TITLE-screen Survival difficulty selector (the code-6 overlay,
     * NOT a submenu_state) is fully open + at rest (scene 0, submenu_state == 0,
     * cursor_anim == 0, survival_state == 8). Drives TITLE_SURVIVAL_READY (0→1) —
     * no async load ⇒ a clean +0-stretch v3 join for the selector render
     * (FUN_0049c644 @ 0x49cbe8). 0 outside the open selector. */
    int title_survival_active;
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
