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
