/*
 * test_audio.c — pure-C tests for the audio module's portable bits.
 *
 * The DirectMusic backend itself (audio_init / audio_play_track /
 * audio_shutdown) lives behind a `#ifdef _WIN32` guard and is only
 * exercised by the boot smoke test (`tools/smoke-test.py`). What we
 * cover here:
 *
 *   - The 21-entry BGM filename table — values and stability of indices
 *     the engine selector switches on.
 *   - The one-shot-track lookup (engine's `(iVar5==0x28||0x2c||0x34||0x4c)`
 *     guard in FUN_00498ef4).
 *   - The music-bridge handshake: setting g_music_swap_fn fires it from
 *     the selector on a track change, the stub remains NULL otherwise.
 */
#include "t.h"
#include "audio.h"
#include "music.h"

int test_audio_bgm_table_has_21_entries(void)
{
    T_ASSERT_EQ_I(AUDIO_BGM_TRACK_COUNT, 21);
    /* Every slot must be non-NULL and look like a relative path. */
    for (int i = 0; i < AUDIO_BGM_TRACK_COUNT; i++) {
        const char *n = audio_bgm_filenames[i];
        T_ASSERT(n != NULL);
        T_ASSERT(strncmp(n, "bgm/", 4) == 0);
    }
    return 0;
}

int test_audio_bgm_table_well_known_indices(void)
{
    /* The selector switches on these literal indices; the table must
     * carry the right file at each one. */
    T_ASSERT(strcmp(audio_bgm_filenames[ 0], "bgm/retitle2010.wav") == 0);
    T_ASSERT(strcmp(audio_bgm_filenames[ 1], "bgm/town.wav")        == 0);
    T_ASSERT(strcmp(audio_bgm_filenames[ 7], "bgm/over.wav")        == 0);
    T_ASSERT(strcmp(audio_bgm_filenames[11], "bgm/fanfare.wav")     == 0);
    T_ASSERT(strcmp(audio_bgm_filenames[20], "bgm/water.wav")       == 0);
    return 0;
}

int test_audio_bgm_filename_bounds(void)
{
    T_ASSERT(audio_bgm_filename(-1) == NULL);
    T_ASSERT(audio_bgm_filename(AUDIO_BGM_TRACK_COUNT) == NULL);
    T_ASSERT(audio_bgm_filename(0) != NULL);
    T_ASSERT(audio_bgm_filename(AUDIO_BGM_TRACK_COUNT - 1) != NULL);
    return 0;
}

int test_audio_one_shot_set_is_exact(void)
{
    /* Engine: only indices 10/11/13/19 (treasure/fanfare/clear/staff)
     * get SetRepeats(0). Everything else gets infinite-repeat. */
    for (int i = 0; i < AUDIO_BGM_TRACK_COUNT; i++) {
        int expected = (i == 10 || i == 11 || i == 13 || i == 19);
        int got = audio_is_one_shot_track(i);
        if (got != expected) {
            T_FAIL("track %d (%s): one_shot expected=%d got=%d",
                   i, audio_bgm_filenames[i], expected, got);
        }
    }
    return 0;
}

/* ─── music-bridge handshake ───────────────────────────────────────────
 *
 * Tests that with g_music_swap_fn installed, the selector's swap-
 * dispatch path actually calls it on a track change. Verifies the
 * arg passed matches the selected track. */

static int  g_test_audio_bridge_calls   = 0;
static int32_t g_test_audio_bridge_last = -999;
static void test_audio_bridge(int32_t track)
{
    g_test_audio_bridge_calls++;
    g_test_audio_bridge_last = track;
}

int test_audio_music_bridge_fires_on_swap(void)
{
    music_init();
    g_test_audio_bridge_calls = 0;
    g_test_audio_bridge_last  = -999;
    g_music_swap_fn = test_audio_bridge;

    music_state_t m = g_music;
    music_select_ctx_t c = {
        .scene_state          = 0,
        .title_frame_counter  = 0,
        .title_cursor_anim    = 0,
        .title_submenu_state  = 0,
    };
    music_step(&m, &c);

    T_ASSERT_EQ_I(g_test_audio_bridge_calls, 1);
    T_ASSERT_EQ_I(g_test_audio_bridge_last,  MUSIC_TRACK_TITLE);

    /* Same selection on next step → no extra bridge call. */
    music_step(&m, &c);
    T_ASSERT_EQ_I(g_test_audio_bridge_calls, 1);

    g_music_swap_fn = NULL;
    return 0;
}

int test_audio_music_bridge_skipped_when_null(void)
{
    music_init();
    g_test_audio_bridge_calls = 0;
    g_music_swap_fn = NULL;

    music_state_t m = g_music;
    music_select_ctx_t c = {
        .scene_state          = 0,
        .title_frame_counter  = 0,
        .title_cursor_anim    = 0,
        .title_submenu_state  = 0,
    };
    music_step(&m, &c);

    /* swap_call_count still ticks (selector internal accounting), but
     * the bridge was NULL so nothing external got called. */
    T_ASSERT_EQ_I(m.swap_call_count, 1);
    T_ASSERT_EQ_I(g_test_audio_bridge_calls, 0);
    return 0;
}
