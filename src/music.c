/*
 * music.c — sim_b music-track selector (FUN_0049966a).
 *
 * See header for the full ported-vs-stubbed surface. Quick recap of
 * what runs each frame:
 *
 *   1. Sweep the 110-slot SE-stop pending array; each set slot is
 *      cleared and would (stubbed) trigger FUN_00499c63 to silence
 *      the corresponding SE channel.
 *   2. If pending_fade_phase is non-zero, latch it into fade_phase and
 *      clear pending.
 *   3. music_speed ← 1.0 (or 0.75 if scene state == 10).
 *   4. frame_count++.
 *   5. If pause_modal_state == 0: fade_phase ← 0; if also paused_b == 0,
 *      forced_track ← -1.
 *   6. music_select_track() picks the desired track index.
 *   7. If global_pause is non-zero (FUN_00452911 returns DAT_06a49954),
 *      skip dispatch entirely.
 *   8. Otherwise, if desired != current, dispatch a (stubbed) swap.
 *   9. Volume-animation tail (stubbed — no audio backend).
 *
 * Engine source: docs/decompiled/by-address/49966a.c.
 */

#include "music.h"

#include <stdio.h>
#include <string.h>

#include "audio_fade.h"    /* audio_fade_apply_progress for the per-tick fade tail */
#include "audio.h"         /* audio_se_flush — the FUN_0049966a SE-flag pump head */
#include "scene_title.h"   /* g_scene_title_anim — for music_step_default */
#include "scene.h"         /* g_scene_state — live scene mode (DAT_0438b1c0) */
#include "worker_load.h"   /* worker_load_busy — swap-suppression while loading */
#include "scene1_player_ctrl.h"     /* player_ctrl_cc08 — shop open/closed state */
#include "call_trace.h"

music_state_t g_music;

/* Audio-backend swap bridge. NULL in the test build (host gcc, no
 * audio.c) and at boot before audio_init runs. audio_init installs
 * its own adapter here once the DirectMusic backend is ready. */
music_swap_fn_t g_music_swap_fn = NULL;

/* ── titles BGM table at .rdata 0x5d1be0 ────────────────────────────────
 * 8 entries × 8 bytes (low dword = track id, high dword = unused/flag).
 * Indexed by `language` (DAT_005d1bd8). Init value of `language` is -1,
 * so the table is unreached at boot — the engine bypasses this when
 * the cursor-anim+submenu gate fails (almost always at boot). */
static const int32_t k_title_bgm_by_lang[8] = {
    MUSIC_TRACK_TITLE,     /* 0 → bgm/retitle2010.wma */
    1,                     /* 1 → bgm/town.wma        */
    3, 4, 5, 6, 7, 8,
};

/* ── FUN_0049a558 inlined (title-music lookup) ──────────────────────────
 * Returns the title-table track for the current language if the cursor
 * has folded fully out AND a submenu (sub-state 4) is open. Otherwise
 * returns -1.
 *
 * Caller in the engine masks the -1 to 0 via two's-complement masking:
 *   `(-(uint)(x != -1) & x)` — preserves x when valid, yields 0 on -1.
 * We perform the mask in the selector so the caller can use the result
 * directly. */
static int32_t title_bgm_select(const music_select_ctx_t *ctx,
                                int32_t language)
{
    /* Engine FUN_0049a558 entry. Body is 35 bytes verbatim — return
     * k_title_bgm_by_lang[language] when (cursor_anim == 10 && submenu == 4),
     * else -1. The probe fires here so port/retail call-trace diff aligns;
     * music_step's selector calls us only when state == 0 + counter < 0x1b6d,
     * mirroring the engine's FUN_0049966a L60 call site. */
    CALL_TRACE_ENTER(0x49a558u);

    if (ctx->title_cursor_anim != 10 || ctx->title_submenu_state != 4) {
        return -1;
    }
    /* Engine OOBs when language ∉ [0, 8) (init -1 reads at 0x5d1bd8,
     * which is the language global itself — random garbage). We guard
     * the array index defensively; in the engine, the gate above
     * almost always blocks this branch before lang is even consulted. */
    if (language < 0 || language >= 8) {
        return -1;
    }
    return k_title_bgm_by_lang[language];
}

/* ─── lifecycle ──────────────────────────────────────────────────────── */

void music_init(void)
{
    memset(&g_music, 0, sizeof g_music);

    /* Engine .data initializers (extracted from the unpacked binary):
     *   0x5d1960 (current_track):   -1
     *   0x5d1968 (forced_track):    -1
     *   0x5d1964 (fade_duration):   0x258 = 600
     *   0x5d196c (pending_swap_clr): 1
     *   0x5d157c (music_speed):     1.0f
     *   0x5d1580 (target_volume):   1.0f
     *   0x5d1bd8 (language):        -1 */
    g_music.current_track       = MUSIC_TRACK_NONE;
    g_music.forced_track        = MUSIC_TRACK_NONE;
    g_music.fade_duration       = 0x258;
    g_music.pending_swap_clear  = 1;
    g_music.music_speed         = 1.0f;
    g_music.target_volume       = 1.0f;
    g_music.language            = -1;
}

/* FUN_00499560 @ 0x499560 (15 B) — `DAT_005d1968 = -1; DAT_09643120 = 0;`.
 * Release the forced-track override and clear the second pause flag. */
void music_clear_forced_track(void)
{
    g_music.forced_track = MUSIC_TRACK_NONE;   /* DAT_005d1968 = 0xffffffff */
    g_music.paused_b     = 0;                   /* DAT_09643120 = 0 */
}

/* ─── per-stage BGM (engine state-1 stage switch, 49966a.c:86-150) ──────
 *
 * Maps a stage's `scene_type` (DAT_068dd3fc[stage*0x6cf]) to its BGM track.
 * Types 0..4 are the town/home stages: when the shop is open they play
 * open(8) / fever(18), otherwise the closed-shop / home theme close(9).
 * Types 5+ are the dungeon fields. Unknown types → NONE (keep current). */
int32_t music_stage_track(int32_t scene_type, int shop_open, int shop_fever)
{
    switch (scene_type) {
    case 0: case 1: case 2: case 3: case 4:
        if (shop_open) {
            return shop_fever ? MUSIC_TRACK_FEVER : MUSIC_TRACK_OPEN;  /* 18 / 8 */
        }
        return MUSIC_TRACK_CLOSE;             /* 9  — bgm/close.wav (home) */
    case 5: case 9:
        return MUSIC_TRACK_RUINS;             /* 5  — bgm/ruins.wav */
    case 6: case 0xe: case 0xf:
        return MUSIC_TRACK_FOREST;            /* 4  — bgm/forest.wav */
    case 7: case 0xc:
        return MUSIC_TRACK_WATER;             /* 0x14 — bgm/water.wav */
    case 10: case 0xb:
        return MUSIC_TRACK_CAVE;              /* 3  — bgm/cave.wav */
    case 0xd: case 0x10:
        return MUSIC_TRACK_SOUGEN;            /* 2  — bgm/sougen.wav */
    case 0x11: case 0x12: case 0x13: case 0x14:
        return MUSIC_TRACK_LASTD;             /* 0x11 — bgm/lastd01.wav */
    default:
        return MUSIC_TRACK_NONE;              /* engine leaves uVar5 = -1 */
    }
}

/* ─── pure-C selector ────────────────────────────────────────────────── */

int32_t music_select_track(const music_state_t      *m,
                           const music_select_ctx_t *ctx)
{
    /* Forced override path: a non-(-1) forced_track wins outright. */
    if (m->forced_track != MUSIC_TRACK_NONE) {
        return m->forced_track;
    }

    const int32_t state = ctx->scene_state;

    /* State 9 + quest_pending → fanfare (the "boss-cleared" stinger). */
    if (state == 9) {
        if (m->quest_pending == 0) {
            return MUSIC_TRACK_NONE;          /* engine `return;` — no change */
        }
        return MUSIC_TRACK_FANFARE;           /* 0xb */
    }

    /* Pause-modal override: when a modal is active (pause_modal_state≠0)
     * AND it's not a "skip music override" type (pause_modal_a==0 +
     * pause_modal_b==1), play the "over" track. */
    const int modal_blocks_default =
        (m->pause_modal_state != 0) &&
        (m->pause_modal_a == 0)     &&
        (m->pause_modal_b == 1);
    if (modal_blocks_default) {
        return MUSIC_TRACK_OVER;              /* 7 */
    }

    /* State 7 → don't change track (early return in engine). */
    if (state == 7) {
        return MUSIC_TRACK_NONE;
    }

    /* State 0 (title): fade band + stop sentinel + selector fallback. */
    if (state == 0) {
        const int32_t f = ctx->title_frame_counter;
        if (f >= 0x1ba7) {
            if (f == 0x1ba7) {
                return MUSIC_TRACK_STOP;      /* -2 */
            }
            return MUSIC_TRACK_NONE;          /* engine `return;` — no change */
        }
        /* f < 0x1ba7: either bare-play or volume fade band. Volume is
         * mutated by music_step, not the selector — selector just picks
         * the track. The fade-band selector still calls FUN_00499583
         * (stubbed) but does NOT call the title lookup (path goes
         * through `else` branch with no track change). */
        if (f >= 0x1b6d) {
            return MUSIC_TRACK_NONE;          /* fade band — keep current */
        }
        /* Bare-play: lookup + mask -1→0. */
        const int32_t pick = title_bgm_select(ctx, m->language);
        return (pick == -1) ? MUSIC_TRACK_TITLE : pick;
    }

    /* States 6/8/0xb/0xd/0xe/0xf/0x10 → track 1 (town). */
    switch (state) {
    case 6: case 8: case 0xb: case 0xd: case 0xe: case 0xf: case 0x10:
        return MUSIC_TRACK_TOWN;              /* 1 */
    }

    /* Other states (1..5, 10, 11, 12…) — in-game free-roam / combat /
     * dungeon paths. The engine (FUN_0049966a) reads `quest_pending`, the
     * combat counters, then the per-stage music type
     * `&DAT_068dd3fc[stage * 0x6cf]` (= ctx->scene_type). */
    if (m->quest_pending != 0) {
        return MUSIC_TRACK_FANFARE;           /* 0xb */
    }
    if (m->combat_a > 0) {
        /* (DAT_0438be98 != 1 ? 6 : 7) + 7 → 7 if subphase==1 else 0xd */
        return (m->combat_subphase != 1) ? MUSIC_TRACK_CLEAR : MUSIC_TRACK_OVER;
    }
    if (m->combat_b > 0) {
        return MUSIC_TRACK_CLOSE;             /* 9 — fade-out path */
    }
    /* Per-stage BGM by scene_type (engine 49966a.c:86-150). HOUSE = type 0:
     * shop open → open(8)/fever(18), else close(9 — the home/closed-shop
     * theme, which is what plays through the opening + initial free-roam). */
    return music_stage_track(ctx->scene_type, ctx->shop_open, ctx->shop_fever);
}

/* ─── full step (FUN_0049966a body) ──────────────────────────────────── */

void music_step(music_state_t            *m,
                const music_select_ctx_t *ctx)
{
    /* 1. Sweep SE-stop pending flags. Each set slot fires a stubbed
     *    SE stop and is cleared. Mirrors the do-while loop at the top
     *    of FUN_0049966a (line 21-29 of decomp). */
    for (int i = 0; i < MUSIC_SE_STOP_SLOTS; i++) {
        if (m->se_stop_pending[i] != 0) {
            m->se_stop_pending[i] = 0;
            m->se_stops_fired++;
            /* TODO: FUN_00499c63(i) — kill SE channel `i`. */
        }
    }

    /* 2. Latch pending fade phase if non-zero. */
    if (m->pending_fade_phase != 0) {
        const int32_t latched = m->pending_fade_phase;
        m->pending_fade_phase  = 0;
        m->fade_phase          = latched;
    }

    /* 3. Music speed default 1.0; 0.75 only when scene state == 10. */
    m->music_speed = (ctx->scene_state == 10) ? 0.75f : 1.0f;

    /* 4. Frame counter advance. */
    m->frame_count++;

    /* 5. Pause-modal-clear path: clear fade and override when no modal. */
    if (m->pause_modal_state == 0) {
        m->fade_phase = 0;
        if (m->paused_b == 0) {
            m->forced_track = MUSIC_TRACK_NONE;
        }
    }

    /* 6+7. Update target_volume for the title fade band before the
     *      selector runs (selector returns NONE in the band but the
     *      volume needs to track the fade).  Engine FUN_0049966a calls
     *      FUN_00499583 (audio_fade_apply_bgm_tick) at the tail of both
     *      bands so the backend sees the new target every frame. */
    if (ctx->scene_state == 0 && m->forced_track == MUSIC_TRACK_NONE) {
        const int32_t f = ctx->title_frame_counter;
        if (f >= 0x1b6d && f < 0x1ba7) {
            /* Engine L64-68: linear ramp v = 1.0 - (f - 0x1b6c)/600.0,
             * clamp >= 0. */
            float ramp = 1.0f - ((float)(f - 0x1b6c)) / 600.0f;
            if (ramp < 0.0f) ramp = 0.0f;
            m->target_volume = ramp;
            audio_fade_apply_bgm_tick(ramp);                /* engine L69 */
        } else if (f < 0x1b6d) {
            /* Engine L58: target = 1.0, apply, then fall through to the
             * title-bgm lookup (which we run inside music_select_track). */
            m->target_volume = 1.0f;
            audio_fade_apply_bgm_tick(1.0f);                /* engine L59 */
        }
        /* f >= 0x1ba7: volume already 0; selector returns STOP — engine
         * skips the volume-apply call here too (L72-77 early-returns). */
    }

    /* 8. Selector + dispatch. */
    const int32_t desired = music_select_track(m, ctx);

    if (desired == m->current_track) {
        /* No-op. Fall through to volume tail. */
    } else if (m->global_pause != 0) {
        /* FUN_00452911() returned non-zero — skip dispatch. */
    } else if (desired == MUSIC_TRACK_NONE) {
        m->current_track = MUSIC_TRACK_NONE;
        m->pending_swap_clear = 0;
    } else if (m->music_engine_debug == 0) {
        /* Real-backend path: call FUN_00499200 via the audio bridge. */
        m->swap_call_count++;
        m->last_requested_track = desired;
        fprintf(stderr, "music: swap #%d → track %d (frame %d)\n",
                (int)m->swap_call_count, (int)desired, (int)m->frame_count);
        m->current_track = desired;
        m->pending_swap_clear = 0;
        if (g_music_swap_fn) {
            g_music_swap_fn(desired);
        }
    } else {
        /* Debug-log path (DAT_0438ccb4 != 0): just record + log via
         * FUN_0040cf88 (noop stub). The current_track update happens
         * inside the switch in the engine; we mirror it. */
        m->swap_call_count++;
        m->last_requested_track = desired;
        fprintf(stderr, "music: swap #%d → track %d (frame %d, debug-path)\n",
                (int)m->swap_call_count, (int)desired, (int)m->frame_count);
        m->current_track = desired;
        m->pending_swap_clear = 0;
    }

    /* 9. Volume-animation tail (FUN_0049966a LAB_00499a00).
     *    When fade_phase != 0, advance fade_progress by one frame
     *    (clamped to fade_duration), compute the two-axis cos-product
     *    centibel via audio_fade_apply_progress, and let the backend
     *    apply hook drive the BGM AudioPath's SetVolume.
     *
     *    When progress reaches duration, clear the phase and signal
     *    pending_swap_clear = 1. The engine gates the phase-clear on
     *    DAT_0438cd70 ("carry-over" flag, currently un-modeled — it's
     *    BSS-zero in all observed boot/play traces, so the port pins
     *    it to "always clear"). If a future scene flips that flag,
     *    add a port-side mirror. */
    if (m->fade_phase != 0) {
        if (m->fade_progress < m->fade_duration) {
            m->fade_progress++;
        }
        if (m->fade_progress > m->fade_duration) {
            m->fade_progress = m->fade_duration;
        }

        m->last_fade_centibel = audio_fade_apply_progress(
            AUDIO_FADE_CHANNEL_BGM,
            m->fade_phase,
            m->fade_progress,
            m->fade_duration);
        m->fade_apply_count++;

        if (m->fade_progress >= m->fade_duration) {
            /* DAT_0438cd70 == 0 in vendor: clear phase. */
            m->fade_phase         = 0;
            m->pending_swap_clear = 1;
        }
        if (m->fade_phase == 0) {
            m->fade_progress = 0;
        }
    }
}

/* ─── tick-scheduler entry ───────────────────────────────────────────── */

void music_step_default(void)
{
    /* E.2 probe — FUN_0049966a @ 0x49966a (engine sim_b music selector). */
    CALL_TRACE_ENTER(0x49966au);

    /* FUN_0049966a HEAD: flush the SE-A request flags (DAT_0964308c walk)
     * — plays each SE flagged this frame ONCE (same-frame dedup). */
    audio_se_flush();

    /* Live scene state (DAT_0438b1c0): 0 = title, 1 = INGAME (HOUSE). The
     * selector reads it to leave the title track once the game starts. */
    const int32_t scene_state = g_scene_state;

    /* HOUSE is the only ported in-game stage: stage 0, scene_type 0 (the
     * shop/home). cc08 == 4 means the shop is open for business; in the
     * opening + initial free-roam it's the closed/home state, so the
     * close theme (track 9) plays. The fever variant isn't ported yet. */
    const music_select_ctx_t ctx = {
        .scene_state          = scene_state,
        .title_frame_counter  = (int32_t)g_scene_title_anim.frame_counter,
        .title_cursor_anim    = (int32_t)g_scene_title_anim.cursor_anim,
        .title_submenu_state  = 0,  /* DAT_09643524 — submenu sub-state.
                                     * scene_title sim doesn't write this
                                     * yet (press-dispatch branches not
                                     * ported), so it's always 0 → title
                                     * lookup returns -1 → masked to 0. */
        .scene_type           = 0,                       /* HOUSE */
        .shop_open            = (player_ctrl_cc08() == 4),
        .shop_fever           = 0,                       /* deferred */
    };

    /* Swap-suppression gate. The engine gates the dispatch on FUN_00452911(),
     * which returns DAT_06a49954 — and that global is set to 1 ONLY by the two
     * scene-load worker spawns (FUN_00452cde / FUN_00452eed, each a
     * CreateThread) and cleared when the load thread finishes. So it is purely
     * the loader-busy flag: the BGM swap is held *only* while the scene is
     * loading, and fires the instant the load completes.
     *
     * The opening cutscene therefore does NOT keep the title theme — the
     * selector returns the HOUSE track (9) all through it (the selector's only
     * "keep current" skip is for dialogue script (5c7a2c==0,5c7a30==1), and the
     * opening is (1,1)/(1,2), so that branch never fires here). The title theme
     * plays during the title screen + the new-game scene load; the home theme
     * lands the moment that load finishes — i.e. at the cutscene start — and
     * plays under the whole prologue. Verified against the decompile
     * (FUN_0049966a dispatch + FUN_00452911); see audio-backend.md. */
    g_music.global_pause = worker_load_busy() ? 1 : 0;

    music_step(&g_music, &ctx);
}
