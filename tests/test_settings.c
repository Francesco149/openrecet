/*
 * test_settings.c — non-audio settings slider state (settings.{c,h}).
 *
 * The audio sliders (BGM/SE-A/SE-B) are covered by test_audio_fade.c.
 * Here we just verify the two non-audio rows (slider3 + slider4) keep
 * their engine-default values, clamp to range, and round-trip via
 * set/get.
 */
#include "t.h"
#include "settings.h"
#include "audio_fade.h"
#include "save_bank.h"
#include "rng.h"

int test_settings_reset_seeds_engine_defaults(void)
{
    settings_set_slider3(2);
    settings_set_slider4(1);
    settings_reset();
    T_ASSERT_EQ_I(settings_get_slider3(), SETTINGS_SLIDER3_DEFAULT);
    T_ASSERT_EQ_I(settings_get_slider4(), SETTINGS_SLIDER4_DEFAULT);
    return 0;
}

int test_settings_slider3_clamps_to_range(void)
{
    settings_reset();
    settings_set_slider3(-5);
    T_ASSERT_EQ_I(settings_get_slider3(), 0);
    settings_set_slider3(99);
    T_ASSERT_EQ_I(settings_get_slider3(), SETTINGS_SLIDER3_MAX);
    settings_set_slider3(1);
    T_ASSERT_EQ_I(settings_get_slider3(), 1);
    return 0;
}

int test_settings_slider4_clamps_to_range(void)
{
    settings_reset();
    settings_set_slider4(-1);
    T_ASSERT_EQ_I(settings_get_slider4(), 0);
    settings_set_slider4(7);
    T_ASSERT_EQ_I(settings_get_slider4(), SETTINGS_SLIDER4_MAX);
    settings_set_slider4(1);
    T_ASSERT_EQ_I(settings_get_slider4(), 1);
    return 0;
}

int test_settings_round_trip_each_legal_value(void)
{
    settings_reset();
    for (int v = 0; v <= SETTINGS_SLIDER3_MAX; v++) {
        settings_set_slider3(v);
        T_ASSERT_EQ_I(settings_get_slider3(), v);
    }
    for (int v = 0; v <= SETTINGS_SLIDER4_MAX; v++) {
        settings_set_slider4(v);
        T_ASSERT_EQ_I(settings_get_slider4(), v);
    }
    return 0;
}

/* settings_bind_save_header: the config has one copy, the save-arena header
 * (retail DAT_056e5774 Sound / 5778 Music / 577c Voice / s16 5782 Unread Text
 * Skip / 5784 Message Speed). Bound, the accessors read what the arena holds
 * and write into it; reset() goes back to the module-local defaults. */
int test_settings_bind_save_header_is_one_copy(void)
{
    audio_fade_reset();
    settings_reset();
    save_bank_arena_clear();
    const uint32_t rng0 = g_rng_seed;           /* the bank init draws; leave the suite's stream alone */
    save_bank_init_all();                       /* FUN_004901c2: 9 / 5 / 9, speed 1 */
    g_rng_seed = rng0;
    settings_bind_save_header();
    uint8_t  *b = save_bank_header_bytes();
    uint32_t *h = (uint32_t *)(void *)b;

    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A), 9);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM),  5);   /* not audio_fade's 9 */
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B), 9);
    T_ASSERT_EQ_I(settings_get_slider3(), 1);
    T_ASSERT_EQ_I(settings_get_slider4(), 0);

    /* writes land in the header fields … */
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_A, 6);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM,  7);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_B, 4);
    settings_set_slider3(2);
    settings_set_slider4(1);
    T_ASSERT_EQ_I((int)h[1], 6);
    T_ASSERT_EQ_I((int)h[2], 7);
    T_ASSERT_EQ_I((int)h[3], 4);
    T_ASSERT_EQ_I((int)h[4], 0x00010000);       /* the s16 at +0x12; +0x10 untouched */
    T_ASSERT_EQ_I(b[0x12], 1);
    T_ASSERT_EQ_I((int)h[5], 2);
    T_ASSERT_EQ_I(save_header_get_bgm_slider(), 7);

    /* … and a changed arena (a loaded save.dat) is what the accessors read */
    h[1] = 3; h[2] = 0; h[3] = 8; h[4] = 0; h[5] = 0;
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A), 3);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM),  0);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B), 8);
    T_ASSERT_EQ_I(settings_get_slider3(), 0);
    T_ASSERT_EQ_I(settings_get_slider4(), 0);

    /* reset = unbound, module-local defaults; the header keeps its values */
    audio_fade_reset();
    settings_reset();
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 9);
    T_ASSERT_EQ_I(settings_get_slider3(), SETTINGS_SLIDER3_DEFAULT);
    settings_set_slider3(2);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 4);
    T_ASSERT_EQ_I((int)h[5], 0);
    T_ASSERT_EQ_I((int)h[2], 0);
    settings_reset();
    audio_fade_reset();
    return 0;
}
