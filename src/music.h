/*
 * music.h — sim_b music-track selector (FUN_0049966a).
 *
 * The engine's "sim_b" callback in the tick scheduler picks a music
 * track to play based on the current scene state + game globals, then
 * hands it to the DirectMusic backend at FUN_00499200.
 *
 * What this port covers:
 *
 *   - The full FUN_0049966a control flow (preamble + selector + swap
 *     dispatch + volume-anim tail), structured so that the selector
 *     is a pure-C function callable from tests.
 *   - The 110-slot SE stop-pending sweep at the top (DAT_0964308c[0..0x6e)).
 *   - The forced-track override path (DAT_005d1968).
 *   - The title-screen bare path (state==0, frame_counter < 0x1ba7),
 *     including the volume fade-out band at frame_counter [0x1b6d, 0x1ba7)
 *     and the stop-sentinel at frame_counter == 0x1ba7.
 *   - The "no pause modal" paths for states 7/8/6/0xb/0xd/0xe/0xf/0x10.
 *   - The "boss" / state-9 quest-pending → fanfare path.
 *   - The pause-modal-overrides path (state ∉ {0,7,8,…} + DAT_0438b1c8
 *     ≠ 0 + DAT_005c7a2c == 0 + DAT_005c7a30 == 1 → track=7 "over.wma").
 *
 * What's stubbed (and why):
 *
 *   - The actual track-swap call (FUN_00499200) and SE-stop call
 *     (FUN_00499c63) and volume-apply call (FUN_00499583) — no music
 *     backend yet. Instead, sim_b records the requested track + swap
 *     count in `music_state_t` so tests can verify selector behavior.
 *   - The per-stage music lookup at `&DAT_068dd3fc[stage * 0x6cf]` —
 *     that's the dungeon stage descriptor; we don't have the table
 *     loader ported. Stubbed to return -1, which feeds the selector's
 *     "default to track from switch" fallback. Lands when stage scenes
 *     port.
 *   - The volume-animation tail (FUN_00499583 + FUN_00451874 "VOL %d"
 *     MCI command) — uses DAT_09643108 (music interface pointer) which
 *     is NULL today; the body is short-circuited to a no-op exactly
 *     the way the engine does it.
 *
 * Title-screen quirk #45 documented in docs/findings/music-selector.md:
 *   FUN_0049a558 ("title BGM lookup") only returns a non-(-1) track when
 *   `cursor_anim == 10` *and* `submenu_state == 4`. In the bare title
 *   path neither is true, so the selector returns -1; the engine then
 *   masks `-1` to `0` via `-(uint)(uVar5 != -1) & uVar5`, defaulting
 *   to track 0 (retitle2010.wma). Faithfully reproduced.
 *
 * Engine sources:
 *   - FUN_0049966a @ 0x49966a (1412 bytes) — the whole selector
 *   - FUN_0049a558 @ 0x49a558 (35 bytes)   — title-music table lookup
 *   - FUN_00499c63 @ 0x499c63              — SE stop (stubbed)
 *   - FUN_00499200 @ 0x499200              — track-swap (stubbed)
 *   - FUN_00499583 @ 0x499583              — volume apply (stubbed)
 */
#ifndef OPENRECET_MUSIC_H
#define OPENRECET_MUSIC_H

#include <stdint.h>

/* ─── track ID constants (from rdata at 0x5d1ae4..0x5d1b98) ──────────── */
/* The filename pointers are stored in case-stride blocks the engine
 * uses for debug-log formatting; we list them here for documentation.
 * In a real-backend build, the swap fn would look these up. */
enum {
    MUSIC_TRACK_NONE     = -1,        /* keep current */
    MUSIC_TRACK_STOP     = -2,        /* explicit stop sentinel */
    MUSIC_TRACK_TITLE    = 0,         /* bgm/retitle2010.wma */
    MUSIC_TRACK_TOWN     = 1,         /* bgm/town.wma   — town overworld + shop idle */
    MUSIC_TRACK_CAVE     = 3,         /* bgm/cave.wma */
    MUSIC_TRACK_FOREST   = 4,         /* bgm/forest.wma */
    MUSIC_TRACK_RUINS    = 5,         /* bgm/ruins.wma */
    MUSIC_TRACK_BOSS     = 6,         /* bgm/boss.wma */
    MUSIC_TRACK_OVER     = 7,         /* bgm/over.wma   — game over / pause modal */
    MUSIC_TRACK_OPEN     = 8,         /* bgm/open.wma   — shop open */
    MUSIC_TRACK_CLOSE    = 9,         /* bgm/close.wma  — shop close */
    MUSIC_TRACK_FANFARE  = 0xb,       /* bgm/fanfare.wma */
    MUSIC_TRACK_CLEAR    = 0xd,       /* bgm/clear.wma */
    MUSIC_TRACK_RIVAL    = 0xf,       /* bgm/rival.wma */
};

#define MUSIC_SE_STOP_SLOTS  0x6e  /* 110 — sweep length at top of selector */

/* Per-engine-global music state.
 *
 * Field names are loosely descriptive but the // comment on each line
 * pins down the underlying engine global (DAT_xxxx_xxxx). Memory-mapped
 * exactly the way the engine carries them — no struct-to-engine packing
 * is implied, this is just our carrier for sim_b. */
typedef struct music_state {
    /* Track selection. -1 = none playing, -2 = stop sentinel. */
    int32_t  current_track;        /* DAT_005d1960 — init -1                */
    int32_t  forced_track;         /* DAT_005d1968 — init -1; non-(-1) wins */

    /* Fade-in/out animation (only relevant once backend lands). */
    int32_t  fade_phase;           /* DAT_09643114 — 0 idle, 1 in, 2 out    */
    int32_t  fade_progress;        /* DAT_09643130 — counts 0..fade_duration */
    int32_t  fade_duration;        /* DAT_005d1964 — init 0x258 (600)       */
    int32_t  pending_fade_phase;   /* DAT_0964311c — applied at top of step  */
    int32_t  pending_swap_clear;   /* DAT_005d196c — 0 after swap completes */

    int32_t  frame_count;          /* DAT_09643124 — bumps every step       */
    float    target_volume;        /* _DAT_005d1580 — init 1.0f             */
    float    music_speed;          /* _DAT_005d157c — 1.0 default, 0.75 @ state 10 */

    int32_t  language;             /* DAT_005d1bd8 — title-table index; init -1 */
    int32_t  quest_pending;        /* DAT_0964312c — state-9 → fanfare */
    int32_t  pause_modal_state;    /* DAT_0438b1c8 — 0 none, !=0 modal active */
    int32_t  pause_modal_a;        /* DAT_005c7a2c — 0 + b==1 → modal music override */
    int32_t  pause_modal_b;        /* DAT_005c7a30                          */
    int32_t  paused_b;             /* DAT_09643120 — second pause flag      */
    int32_t  combat_a;             /* DAT_0438be94 — fade-in counter (>0 → boss/fanfare branch) */
    int32_t  combat_b;             /* DAT_0438beb0 — fade-out counter (>0 → boss/close branch) */
    int32_t  combat_subphase;      /* DAT_0438be98                          */
    int32_t  global_pause;         /* DAT_06a49954 — non-zero blocks dispatch */
    int32_t  music_engine_debug;   /* DAT_0438ccb4 — 0 = real backend       */

    /* SE stop-pending flags (DAT_0964308c[0..0x6e)). Set elsewhere when a
     * sound effect needs to be silenced; sim_b sweeps them at the top. */
    uint8_t  se_stop_pending[MUSIC_SE_STOP_SLOTS];

    /* ── test introspection (not engine-mapped) ────────────────────────
     * These count what the stubbed call-outs *would* have done. Real
     * engine has no equivalent — the work happens inside the audio
     * backend instead. */
    int32_t  se_stops_fired;       /* sum of SE-stop call-outs this step    */
    int32_t  swap_call_count;      /* number of track-swaps this run        */
    int32_t  last_requested_track; /* track passed to the (stubbed) swap fn */
} music_state_t;

extern music_state_t g_music;

/* Inputs to the pure-C selector. Mirrors the globals that
 * FUN_0049966a's big switch reads; bundled here so tests can drive
 * arbitrary state without populating the engine globals directly. */
typedef struct music_select_ctx {
    int32_t  scene_state;          /* DAT_0438b1c0 */
    int32_t  title_frame_counter;  /* DAT_09643518 — relevant only when scene==0 */
    int32_t  title_cursor_anim;    /* DAT_09643520 — needed for FUN_0049a558 */
    int32_t  title_submenu_state;  /* DAT_09643524 — needed for FUN_0049a558 */
} music_select_ctx_t;

/* Reset g_music to the engine's BSS-zero values + the documented
 * .data-section initializers (current/forced/lang/duration/volume/speed
 * + the pending_swap_clear = 1). Idempotent. */
void music_init(void);

/* Pure-C selector. Returns the desired track index (or MUSIC_TRACK_NONE
 * to keep the current track, or MUSIC_TRACK_STOP to explicitly stop).
 *
 * Mirrors the big switch in FUN_0049966a at LAB_00499834 → LAB_0049992d.
 * Does NOT mutate `m` — caller decides whether to dispatch a swap.
 *
 * Title-bare-path caveat: FUN_0049a558 (the engine's title lookup) is
 * inlined here. It returns -1 when (cursor_anim != 10 || submenu != 4),
 * and the engine then masks -1 → 0 via `(-(uint)(x != -1) & x)`. We
 * reproduce that masking; the selector reports track 0 on bare title
 * unless the cursor has fully folded out *and* a submenu is open. */
int32_t music_select_track(const music_state_t      *m,
                           const music_select_ctx_t *ctx);

/* Full sim_b body. Mirrors FUN_0049966a end-to-end with the documented
 * stubs (no real audio I/O). `ctx` describes the engine state the
 * selector reads. Increments `m->frame_count`, sweeps the SE-stop
 * array, updates fade/volume state, runs the selector, and dispatches
 * a (stubbed) swap when the track changes. */
void music_step(music_state_t            *m,
                const music_select_ctx_t *ctx);

/* Tick-scheduler entry point: reads `g_music` + the title-scene globals
 * (`g_scene_title_anim`) and calls `music_step`. Wired into
 * `tick_callbacks.sim_b` from main.c. Scene state is pinned to 0 since
 * only the title scene exists today; when a second scene ports, a
 * carrier global lands and this wrapper updates accordingly. */
void music_step_default(void);

/* ─── audio-backend bridge ────────────────────────────────────────────
 * When the DirectMusic backend (src/audio.c) is wired in, it installs
 * its `audio_play_track` adapter here. The selector's swap-dispatch
 * branch calls this pointer (when non-NULL) on each track change so
 * the real audio backend fires.
 *
 * Lives in music.c rather than audio.c so the test build (which doesn't
 * link audio.c) keeps the stubbed swap behavior: pointer stays NULL,
 * the engine call-out is skipped, but swap_call_count / current_track
 * still update for assertions. */
typedef void (*music_swap_fn_t)(int32_t track);
extern music_swap_fn_t g_music_swap_fn;

#endif /* OPENRECET_MUSIC_H */
