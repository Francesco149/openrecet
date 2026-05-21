/*
 * test_music.c — unit tests for music.{c,h} (sim_b music selector).
 *
 * Covers the pure-C selector + the full step body, especially the
 * title-screen bare path that ships in this commit. Stage-state paths
 * are smoke-tested (current behavior: returns "no change") and will
 * grow real assertions when the stage-data port lands.
 */

#include "t.h"
#include "audio_fade.h"
#include "music.h"

/* Construct a default-engine-state context with state==0 (title) and
 * the title-anim values that the bare-boot path sees. */
static music_select_ctx_t ctx_title_bare(int frame_counter)
{
    music_select_ctx_t c = {
        .scene_state          = 0,
        .title_frame_counter  = frame_counter,
        .title_cursor_anim    = 0,   /* still fully unfolded → BG menu shown */
        .title_submenu_state  = 0,   /* no submenu open */
    };
    return c;
}

/* ─── lifecycle ──────────────────────────────────────────────────────── */

int test_music_init_engine_data_defaults(void)
{
    music_state_t m;
    memset(&m, 0xAA, sizeof m);   /* poison to ensure init clears */

    /* music_init writes to g_music — temporarily borrow it. */
    music_init();
    T_ASSERT_EQ_I(g_music.current_track,       MUSIC_TRACK_NONE);
    T_ASSERT_EQ_I(g_music.forced_track,        MUSIC_TRACK_NONE);
    T_ASSERT_EQ_I(g_music.fade_duration,       0x258);
    T_ASSERT_EQ_I(g_music.pending_swap_clear,  1);
    T_ASSERT_EQ_I(g_music.language,            -1);
    T_ASSERT(g_music.music_speed   == 1.0f);
    T_ASSERT(g_music.target_volume == 1.0f);
    T_ASSERT_EQ_I(g_music.frame_count,         0);
    T_ASSERT_EQ_I(g_music.swap_call_count,     0);
    /* SE-stop array all zero. */
    for (int i = 0; i < MUSIC_SE_STOP_SLOTS; i++) {
        T_ASSERT_EQ_I(g_music.se_stop_pending[i], 0);
    }
    /* Silence -Wunused-variable for `m`. */
    (void)m;
    return 0;
}

/* ─── selector: title bare path ──────────────────────────────────────── */

int test_music_select_title_bare_returns_track_zero(void)
{
    /* Engine quirk: title lookup returns -1 (cursor_anim=0, sub_state=0
     * — gate fails), engine masks -1→0, so the bare title boots to
     * track 0 (retitle2010.wma). */
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0);
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_TITLE);
    return 0;
}

int test_music_select_title_bare_holds_until_fade_band(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0x1b6c);   /* last bare frame */
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_TITLE);
    return 0;
}

int test_music_select_title_fade_band_returns_none(void)
{
    /* Frames 0x1b6d .. 0x1ba6: fade-out band. Selector returns NONE
     * (keep current) — the volume animation handles the actual fade. */
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c;

    c = ctx_title_bare(0x1b6d);
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_NONE);

    c = ctx_title_bare(0x1ba6);
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_NONE);
    return 0;
}

int test_music_select_title_stop_sentinel(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0x1ba7);
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_STOP);
    return 0;
}

int test_music_select_title_post_stop_no_change(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0x1ba8);
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_NONE);
    return 0;
}

int test_music_select_title_submenu_open_uses_table_lookup(void)
{
    /* cursor_anim==10 && submenu_state==4 + a valid language → table
     * lookup returns the per-language track. */
    music_state_t m; music_init(); m = g_music;
    m.language = 1;   /* table[1] = town (1) */

    music_select_ctx_t c = {
        .scene_state          = 0,
        .title_frame_counter  = 0,
        .title_cursor_anim    = 10,
        .title_submenu_state  = 4,
    };
    /* Selector returns the table value verbatim (no -1 masking when
     * the gate passes). */
    T_ASSERT_EQ_I(music_select_track(&m, &c), 1 /* town */);
    return 0;
}

int test_music_select_title_invalid_language_falls_back_to_zero(void)
{
    /* Gate passes (cursor_anim==10, submenu_state==4), but language is
     * out of range → table lookup returns -1 → masked to 0. */
    music_state_t m; music_init(); m = g_music;
    /* language is -1 from init; no need to set. */
    music_select_ctx_t c = {
        .scene_state          = 0,
        .title_frame_counter  = 0,
        .title_cursor_anim    = 10,
        .title_submenu_state  = 4,
    };
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_TITLE);
    return 0;
}

/* ─── selector: forced-override + pause-modal ────────────────────────── */

int test_music_select_forced_override_wins(void)
{
    music_state_t m; music_init(); m = g_music;
    m.forced_track = MUSIC_TRACK_BOSS;
    music_select_ctx_t c = ctx_title_bare(0);
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_BOSS);
    return 0;
}

int test_music_select_pause_modal_routes_to_over(void)
{
    music_state_t m; music_init(); m = g_music;
    m.pause_modal_state = 1;
    m.pause_modal_a     = 0;
    m.pause_modal_b     = 1;
    /* Use a state ∉ {0, 7, 9} so the modal-override path is the one
     * that wins. State 1 (any non-special) works. */
    music_select_ctx_t c = ctx_title_bare(0);
    c.scene_state = 1;
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_OVER);
    return 0;
}

int test_music_select_pause_modal_other_b_not_one_skips_override(void)
{
    music_state_t m; music_init(); m = g_music;
    m.pause_modal_state = 1;
    m.pause_modal_a     = 0;
    m.pause_modal_b     = 2;          /* not 1 → engine `else` branch */
    music_select_ctx_t c = ctx_title_bare(0);
    c.scene_state = 8;                /* state 8 → town fallback */
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_TOWN);
    return 0;
}

/* ─── selector: per-state simple cases ───────────────────────────────── */

int test_music_select_state_7_returns_none(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0);
    c.scene_state = 7;
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_NONE);
    return 0;
}

int test_music_select_state_9_no_quest_returns_none(void)
{
    music_state_t m; music_init(); m = g_music;
    m.quest_pending = 0;
    music_select_ctx_t c = ctx_title_bare(0);
    c.scene_state = 9;
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_NONE);
    return 0;
}

int test_music_select_state_9_quest_pending_returns_fanfare(void)
{
    music_state_t m; music_init(); m = g_music;
    m.quest_pending = 1;
    music_select_ctx_t c = ctx_title_bare(0);
    c.scene_state = 9;
    T_ASSERT_EQ_I(music_select_track(&m, &c), MUSIC_TRACK_FANFARE);
    return 0;
}

int test_music_select_town_states_return_track_one(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0);
    const int town_states[] = { 6, 8, 0xb, 0xd, 0xe, 0xf, 0x10 };
    for (size_t i = 0; i < sizeof(town_states)/sizeof(town_states[0]); i++) {
        c.scene_state = town_states[i];
        const int32_t got = music_select_track(&m, &c);
        if (got != MUSIC_TRACK_TOWN) {
            T_FAIL("state 0x%x: expected TOWN (1), got %d",
                   town_states[i], got);
        }
    }
    return 0;
}

/* ─── step: title bare path mutations ────────────────────────────────── */

int test_music_step_increments_frame_count(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0);
    music_step(&m, &c);
    T_ASSERT_EQ_I(m.frame_count, 1);
    music_step(&m, &c);
    music_step(&m, &c);
    T_ASSERT_EQ_I(m.frame_count, 3);
    return 0;
}

int test_music_step_title_bare_dispatches_track_zero_once(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    /* First step: current_track was -1, selector picks 0 → swap fires. */
    T_ASSERT_EQ_I(m.current_track,        MUSIC_TRACK_TITLE);
    T_ASSERT_EQ_I(m.last_requested_track, MUSIC_TRACK_TITLE);
    T_ASSERT_EQ_I(m.swap_call_count,      1);

    /* Subsequent steps: same selection, no extra swap. */
    music_step(&m, &c);
    music_step(&m, &c);
    T_ASSERT_EQ_I(m.swap_call_count,      1);
    T_ASSERT_EQ_I(m.current_track,        MUSIC_TRACK_TITLE);
    return 0;
}

int test_music_step_speed_drops_to_0_75_at_state_10(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    T_ASSERT(m.music_speed == 1.0f);

    c.scene_state = 10;
    music_step(&m, &c);
    T_ASSERT(m.music_speed == 0.75f);

    c.scene_state = 0;
    music_step(&m, &c);
    T_ASSERT(m.music_speed == 1.0f);
    return 0;
}

int test_music_step_global_pause_blocks_dispatch(void)
{
    music_state_t m; music_init(); m = g_music;
    m.global_pause = 1;
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    /* Frame counter still ticks. */
    T_ASSERT_EQ_I(m.frame_count, 1);
    /* But no swap dispatched. */
    T_ASSERT_EQ_I(m.swap_call_count, 0);
    T_ASSERT_EQ_I(m.current_track,   MUSIC_TRACK_NONE);
    return 0;
}

int test_music_step_se_stop_pending_sweeps_and_clears(void)
{
    music_state_t m; music_init(); m = g_music;
    m.se_stop_pending[3]   = 1;
    m.se_stop_pending[42]  = 1;
    m.se_stop_pending[109] = 1;
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    T_ASSERT_EQ_I(m.se_stops_fired,        3);
    T_ASSERT_EQ_I(m.se_stop_pending[3],    0);
    T_ASSERT_EQ_I(m.se_stop_pending[42],   0);
    T_ASSERT_EQ_I(m.se_stop_pending[109],  0);

    /* Second step: nothing pending → no new fires. */
    music_step(&m, &c);
    T_ASSERT_EQ_I(m.se_stops_fired, 3);
    return 0;
}

int test_music_step_pending_fade_phase_latches(void)
{
    music_state_t m; music_init(); m = g_music;
    m.pending_fade_phase = 1;
    m.pause_modal_state  = 1;   /* keep fade from getting cleared */
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    T_ASSERT_EQ_I(m.fade_phase,           1);
    T_ASSERT_EQ_I(m.pending_fade_phase,   0);
    return 0;
}

int test_music_step_no_modal_clears_fade_and_forced(void)
{
    music_state_t m; music_init(); m = g_music;
    m.fade_phase    = 2;
    m.forced_track  = MUSIC_TRACK_RIVAL;
    m.pause_modal_state = 0;
    m.paused_b      = 0;
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    T_ASSERT_EQ_I(m.fade_phase,    0);
    T_ASSERT_EQ_I(m.forced_track,  MUSIC_TRACK_NONE);
    return 0;
}

int test_music_step_paused_b_keeps_forced_track(void)
{
    /* When pause_modal_state==0 but paused_b!=0, fade clears but the
     * forced override is preserved. */
    music_state_t m; music_init(); m = g_music;
    m.fade_phase        = 2;
    m.forced_track      = MUSIC_TRACK_RIVAL;
    m.pause_modal_state = 0;
    m.paused_b          = 1;
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    T_ASSERT_EQ_I(m.fade_phase,    0);
    T_ASSERT_EQ_I(m.forced_track,  MUSIC_TRACK_RIVAL);
    return 0;
}

/* ─── step: title fade-volume ramp ───────────────────────────────────── */

int test_music_step_target_volume_default_is_one(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0);
    music_step(&m, &c);
    T_ASSERT(m.target_volume == 1.0f);
    return 0;
}

int test_music_step_target_volume_fade_band_decreases(void)
{
    /* At f == 0x1b6c (last bare frame), volume == 1.0.
     * At f == 0x1b6d (first fade frame), volume == 1.0 - 1/600 ≈ 0.998.
     * At f == 0x1ba6 (last fade frame, offset 58), volume ≈ 0.903. */
    music_state_t m; music_init(); m = g_music;

    music_select_ctx_t c = ctx_title_bare(0x1b6c);
    music_step(&m, &c);
    T_ASSERT(m.target_volume == 1.0f);

    /* Reset frame counter; advance to 0x1b6d. */
    music_init(); m = g_music;
    c = ctx_title_bare(0x1b6d);
    music_step(&m, &c);
    /* Expect ~0.99833. Allow a tiny float tolerance. */
    T_ASSERT(m.target_volume < 1.0f);
    T_ASSERT(m.target_volume > 0.998f);

    music_init(); m = g_music;
    c = ctx_title_bare(0x1ba6);
    music_step(&m, &c);
    T_ASSERT(m.target_volume > 0.902f);
    T_ASSERT(m.target_volume < 0.904f);
    return 0;
}

int test_music_step_stop_sentinel_dispatches_track_minus_two(void)
{
    music_state_t m; music_init(); m = g_music;
    music_select_ctx_t c = ctx_title_bare(0x1ba7);

    music_step(&m, &c);
    /* current_track went from -1 to -2; one swap fired. */
    T_ASSERT_EQ_I(m.current_track,        MUSIC_TRACK_STOP);
    T_ASSERT_EQ_I(m.last_requested_track, MUSIC_TRACK_STOP);
    T_ASSERT_EQ_I(m.swap_call_count,      1);
    return 0;
}

int test_music_step_forced_override_dispatches_overridden_track(void)
{
    /* With pause_modal_state != 0, the forced override is preserved
     * across the step, then dispatched. */
    music_state_t m; music_init(); m = g_music;
    m.forced_track      = MUSIC_TRACK_BOSS;
    m.pause_modal_state = 1;
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    T_ASSERT_EQ_I(m.current_track,        MUSIC_TRACK_BOSS);
    T_ASSERT_EQ_I(m.last_requested_track, MUSIC_TRACK_BOSS);
    T_ASSERT_EQ_I(m.swap_call_count,      1);
    /* Forced override is NOT cleared while modal is active. */
    T_ASSERT_EQ_I(m.forced_track,         MUSIC_TRACK_BOSS);
    return 0;
}

/* ─── step: per-tick fade animation (FUN_0049966a tail) ──────────────── */

/* Hook capture shared by the music-step fade tests. Lives in this TU so
 * test_audio_fade.c's identically-named statics don't clash. */
static int     g_fade_apply_calls   = 0;
static int32_t g_fade_apply_last_cb = 0;
static void music_test_apply_hook(int channel, int32_t centibel)
{
    (void)channel;
    g_fade_apply_calls++;
    g_fade_apply_last_cb = centibel;
}

int test_music_step_fade_phase_advances_progress(void)
{
    /* fade_phase != 0 + pause_modal_state != 0 (so step-5 doesn't reset
     * the phase) → each step increments fade_progress, apply hook fires
     * once per step. */
    audio_fade_reset();
    audio_fade_set_apply_hook(music_test_apply_hook);
    g_fade_apply_calls = 0;

    music_state_t m; music_init(); m = g_music;
    m.fade_phase         = 1;         /* fade-OUT */
    m.fade_duration      = 600;
    m.pause_modal_state  = 1;         /* keep step-5 from clearing phase */
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    T_ASSERT_EQ_I(m.fade_progress, 1);
    T_ASSERT_EQ_I(m.fade_apply_count, 1);
    T_ASSERT_EQ_I(g_fade_apply_calls, 1);

    music_step(&m, &c);
    music_step(&m, &c);
    T_ASSERT_EQ_I(m.fade_progress, 3);
    T_ASSERT_EQ_I(g_fade_apply_calls, 3);

    audio_fade_reset();
    return 0;
}

int test_music_step_fade_phase_one_walks_to_silence(void)
{
    /* Short duration so we can run the fade to completion in a unit
     * test (default 600 would be slow). Phase 1 = fade-OUT → centibel
     * trends downward from 0 toward the math floor. */
    audio_fade_reset();
    audio_fade_set_apply_hook(music_test_apply_hook);
    g_fade_apply_calls = 0;

    music_state_t m; music_init(); m = g_music;
    m.fade_phase         = 1;
    m.fade_duration      = 10;
    m.pause_modal_state  = 1;
    m.pending_swap_clear = 0;
    music_select_ctx_t c = ctx_title_bare(0);

    /* First step: progress 0→1, centibel near full target (0). */
    music_step(&m, &c);
    int32_t cb_first = m.last_fade_centibel;

    /* Run to completion. */
    for (int i = 0; i < 12; i++) music_step(&m, &c);

    /* Last computed centibel was at progress=duration → near -9600.
     * Then phase cleared & progress reset; pending_swap_clear set. */
    T_ASSERT(cb_first > -3000);                   /* started loud */
    T_ASSERT_EQ_I(m.fade_phase, 0);
    T_ASSERT_EQ_I(m.fade_progress, 0);
    T_ASSERT_EQ_I(m.pending_swap_clear, 1);
    T_ASSERT(m.last_fade_centibel <= -9598);      /* ended silent */

    audio_fade_reset();
    return 0;
}

int test_music_step_fade_phase_two_walks_to_loud(void)
{
    /* Phase 2 = fade-IN → centibel trends UP from silence toward target. */
    audio_fade_reset();
    audio_fade_set_apply_hook(music_test_apply_hook);
    g_fade_apply_calls = 0;

    music_state_t m; music_init(); m = g_music;
    m.fade_phase         = 2;
    m.fade_duration      = 10;
    m.pause_modal_state  = 1;
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    int32_t cb_first = m.last_fade_centibel;

    for (int i = 0; i < 12; i++) music_step(&m, &c);

    T_ASSERT(cb_first < -6000);                   /* started silent */
    T_ASSERT_EQ_I(m.fade_phase, 0);
    T_ASSERT_EQ_I(m.fade_progress, 0);
    T_ASSERT_EQ_I(m.pending_swap_clear, 1);
    T_ASSERT_EQ_I(m.last_fade_centibel, 0);       /* ended at full target */

    audio_fade_reset();
    return 0;
}

int test_music_step_no_fade_skips_apply_hook(void)
{
    /* fade_phase == 0 → tail is a no-op, no hook calls, no progress
     * advance. */
    audio_fade_reset();
    audio_fade_set_apply_hook(music_test_apply_hook);
    g_fade_apply_calls = 0;

    music_state_t m; music_init(); m = g_music;
    /* fade_phase already 0 from init. */
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    music_step(&m, &c);
    T_ASSERT_EQ_I(g_fade_apply_calls, 0);
    T_ASSERT_EQ_I(m.fade_apply_count, 0);
    T_ASSERT_EQ_I(m.fade_progress,    0);

    audio_fade_reset();
    return 0;
}

int test_music_step_pending_fade_phase_drives_animation(void)
{
    /* Engine pattern: external code sets pending_fade_phase + duration
     * via FUN_00499538/c, sim_b latches it on the next step, and the
     * tail begins applying volumes. */
    audio_fade_reset();
    audio_fade_set_apply_hook(music_test_apply_hook);
    g_fade_apply_calls = 0;

    music_state_t m; music_init(); m = g_music;
    m.pending_fade_phase = 1;
    m.fade_duration      = 10;
    m.pause_modal_state  = 1;        /* keep step-5 from clearing */
    music_select_ctx_t c = ctx_title_bare(0);

    music_step(&m, &c);
    /* pending was latched, phase active, first frame applied. */
    T_ASSERT_EQ_I(m.pending_fade_phase, 0);
    T_ASSERT_EQ_I(m.fade_phase,         1);
    T_ASSERT_EQ_I(g_fade_apply_calls,   1);

    audio_fade_reset();
    return 0;
}
