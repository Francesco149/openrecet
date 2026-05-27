/*
 * test_audio_fade.c — BGM fade-in curve.
 *
 * The fade ramp is a cos arc from -9600 centibels at frame 1 up to
 * `target` at frame 9, with a hard -10000 at frame 0. Pins:
 *
 *   - frame 0  → silence (-10000)
 *   - frame 9  → target_centibel unchanged
 *   - frames 1..8 → strictly increasing
 *   - one spot value against a hand-computed reference
 *
 * We don't try to bit-match the engine's __ftol result here; cosf
 * and cos differ in the last ULP. Allow a 5-centibel slack on the
 * spot value (well below DirectMusic's 1-centibel granularity).
 */
#include "t.h"
#include "audio_fade.h"

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int test_audio_fade_frame_zero_is_hard_silence(void)
{
    /* Engine quirk: frame 0 doesn't pass through the math at all —
     * it's a hard -10000 regardless of target. */
    T_ASSERT_EQ_I(audio_fade_compute(0, 0),      -10000);
    T_ASSERT_EQ_I(audio_fade_compute(0, -5000),  -10000);
    T_ASSERT_EQ_I(audio_fade_compute(0, -9600),  -10000);
    return 0;
}

int test_audio_fade_frame_nine_is_target(void)
{
    /* At the end of the ramp the function returns the unattenuated
     * target. Cover positive (engine never sends one, but check it's
     * sane), zero, and large-negative targets. */
    T_ASSERT_EQ_I(audio_fade_compute(9, 0),      0);
    T_ASSERT_EQ_I(audio_fade_compute(9, -5000),  -5000);
    T_ASSERT_EQ_I(audio_fade_compute(9, -9600),  -9600);
    return 0;
}

int test_audio_fade_frames_one_to_eight_monotonic_increasing(void)
{
    /* The cos arc from angle=8/9·(2π/5) down to angle=1/9·(2π/5) is
     * monotonically increasing for any fixed target. */
    int32_t prev = audio_fade_compute(1, 0);
    for (int f = 2; f <= 8; f++) {
        int32_t cur = audio_fade_compute(f, 0);
        if (cur <= prev) {
            T_FAIL("frame %d (%d) not strictly > frame %d (%d)",
                   f, cur, f - 1, prev);
        }
        prev = cur;
    }
    /* And the same with a non-zero target. */
    prev = audio_fade_compute(1, -3000);
    for (int f = 2; f <= 8; f++) {
        int32_t cur = audio_fade_compute(f, -3000);
        if (cur <= prev) {
            T_FAIL("frame %d (%d) not > frame %d (%d) with target -3000",
                   f, cur, f - 1, prev);
        }
        prev = cur;
    }
    return 0;
}

int test_audio_fade_intermediate_value_matches_reference(void)
{
    /* Hand-computed reference at frame=4, target=0:
     *   angle = (9 - 4) * 2π/5 / 9 = 5/9 * 0.4π ≈ 0.6981...
     *   cos(angle) ≈ 0.766044
     *   result    = 0.766044 * 9600 - 9600 ≈ -2358 */
    double angle = (9.0 - 4.0) * (2.0 * M_PI / 5.0) / 9.0;
    int32_t expected = (int32_t)(cos(angle) * 9600.0 - 9600.0);
    int32_t got = audio_fade_compute(4, 0);
    if (got < expected - 5 || got > expected + 5) {
        T_FAIL("frame 4 target 0: expected ~%d, got %d (Δ %d)",
               expected, got, got - expected);
    }
    /* Sanity: cos(2π/9)*9600 - 9600 ≈ 0.766*9600 - 9600 ≈ -2246. */
    T_ASSERT(got > -2300);
    T_ASSERT(got < -2200);
    return 0;
}

int test_audio_fade_target_threading_at_frame_five(void)
{
    /* Frame 5 with target=-2000: angle=4/9·2π/5 = 8π/45 ≈ 0.5585
     *   cos(angle) ≈ 0.84805
     *   result = 0.84805 * (-2000 + 9600) - 9600
     *          = 0.84805 * 7600 - 9600
     *          ≈ 6445.2 - 9600
     *          ≈ -3155 */
    int32_t got = audio_fade_compute(5, -2000);
    T_ASSERT(got > -3200);
    T_ASSERT(got < -3100);
    /* Same frame with target=0 (louder target) should produce a less-
     * attenuated centibel than target=-2000. */
    int32_t got_zero = audio_fade_compute(5, 0);
    T_ASSERT(got_zero > got);
    return 0;
}

int test_audio_fade_out_of_range_clamps(void)
{
    /* Negative frame → silence. Frame > 9 → target unchanged. */
    T_ASSERT_EQ_I(audio_fade_compute(-3, -1000), -10000);
    T_ASSERT_EQ_I(audio_fade_compute(50, -1000), -1000);
    return 0;
}

/* ─── per-channel slider + apply hook ──────────────────────────────── */

int test_audio_fade_slider_defaults_to_nine(void)
{
    /* Defaults are 9/9/9 (see audio_fade.h header for why BGM=9 not the
     * engine's 5). Out-of-range channel returns 0. */
    audio_fade_reset();
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM),  9);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A), 9);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B), 9);
    T_ASSERT_EQ_I(audio_fade_get_slider(-1), 0);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_COUNT), 0);
    return 0;
}

int test_audio_fade_slider_set_clamps_to_0_9(void)
{
    audio_fade_reset();
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 3);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 3);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, -2);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 0);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 99);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 9);
    /* Per-channel independence. */
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_A, 5);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A), 5);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B), 9);
    /* Out-of-range channel is a no-op (asan-clean). */
    audio_fade_set_slider(-1, 4);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_COUNT, 4);
    return 0;
}

int test_audio_fade_channel_centibel_matches_compute(void)
{
    audio_fade_reset();
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM,  9);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_A, 4);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_B, 0);
    T_ASSERT_EQ_I(audio_fade_channel_centibel(AUDIO_FADE_CHANNEL_BGM),
                  audio_fade_compute(9, 0));
    T_ASSERT_EQ_I(audio_fade_channel_centibel(AUDIO_FADE_CHANNEL_SE_A),
                  audio_fade_compute(4, 0));
    T_ASSERT_EQ_I(audio_fade_channel_centibel(AUDIO_FADE_CHANNEL_SE_B),
                  AUDIO_FADE_SILENCE_CENTIBEL);
    return 0;
}

/* Apply-hook capture: a tiny test hook that records the most recent
 * (channel, centibel) pair for assertion. */
static int     g_apply_hook_calls = 0;
static int     g_apply_hook_last_channel = -1;
static int32_t g_apply_hook_last_centibel = 0;
static void test_apply_hook(int channel, int32_t centibel)
{
    g_apply_hook_calls++;
    g_apply_hook_last_channel  = channel;
    g_apply_hook_last_centibel = centibel;
}

int test_audio_fade_apply_calls_hook_with_centibel(void)
{
    audio_fade_reset();
    audio_fade_set_apply_hook(test_apply_hook);
    g_apply_hook_calls = 0;
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 9);
    int32_t got = audio_fade_apply(AUDIO_FADE_CHANNEL_BGM);
    T_ASSERT_EQ_I(g_apply_hook_calls, 1);
    T_ASSERT_EQ_I(g_apply_hook_last_channel, AUDIO_FADE_CHANNEL_BGM);
    T_ASSERT_EQ_I(g_apply_hook_last_centibel, 0);   /* full target */
    T_ASSERT_EQ_I(got, 0);

    /* Lower slider → more attenuation. */
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_A, 0);
    got = audio_fade_apply(AUDIO_FADE_CHANNEL_SE_A);
    T_ASSERT_EQ_I(g_apply_hook_calls, 2);
    T_ASSERT_EQ_I(g_apply_hook_last_channel, AUDIO_FADE_CHANNEL_SE_A);
    T_ASSERT_EQ_I(got, AUDIO_FADE_SILENCE_CENTIBEL);

    audio_fade_reset();
    return 0;
}

int test_audio_fade_apply_skips_hook_when_unset(void)
{
    audio_fade_reset();   /* clears hook + sliders */
    g_apply_hook_calls = 0;
    int32_t got = audio_fade_apply(AUDIO_FADE_CHANNEL_BGM);
    T_ASSERT_EQ_I(g_apply_hook_calls, 0);
    T_ASSERT_EQ_I(got, 0);   /* slider 9 → 0 dB */
    return 0;
}

int test_audio_fade_apply_rejects_invalid_channel(void)
{
    audio_fade_reset();
    audio_fade_set_apply_hook(test_apply_hook);
    g_apply_hook_calls = 0;
    audio_fade_apply(-1);
    audio_fade_apply(AUDIO_FADE_CHANNEL_COUNT);
    audio_fade_apply(99);
    T_ASSERT_EQ_I(g_apply_hook_calls, 0);
    audio_fade_reset();
    return 0;
}

/* ─── per-tick BGM apply (engine FUN_00499583) ──────────────────────── */

int test_audio_fade_apply_bgm_tick_full_target_matches_apply(void)
{
    /* target_volume == 1.0 should produce the same centibel as
     * audio_fade_apply(BGM) — both feed target_centibel=0 into the
     * cos curve. */
    audio_fade_reset();
    audio_fade_set_apply_hook(test_apply_hook);
    g_apply_hook_calls = 0;

    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 9);
    int32_t got = audio_fade_apply_bgm_tick(1.0f);
    T_ASSERT_EQ_I(g_apply_hook_calls, 1);
    T_ASSERT_EQ_I(g_apply_hook_last_channel, AUDIO_FADE_CHANNEL_BGM);
    T_ASSERT_EQ_I(g_apply_hook_last_centibel, 0);    /* slider 9 + full = 0 dB */
    T_ASSERT_EQ_I(got, 0);

    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 5);
    int32_t got_slider5 = audio_fade_apply_bgm_tick(1.0f);
    int32_t want_slider5 = audio_fade_compute(5, 0);
    T_ASSERT_EQ_I(got_slider5, want_slider5);

    audio_fade_reset();
    return 0;
}

int test_audio_fade_apply_bgm_tick_zero_slider_is_hard_silence(void)
{
    /* Engine fast path at FUN_00499583 L13-17: slider==0 → SetVolume
     * fires with the constant -10000, bypassing the math curve. */
    audio_fade_reset();
    audio_fade_set_apply_hook(test_apply_hook);
    g_apply_hook_calls = 0;

    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 0);
    int32_t got = audio_fade_apply_bgm_tick(1.0f);
    T_ASSERT_EQ_I(g_apply_hook_calls, 1);
    T_ASSERT_EQ_I(g_apply_hook_last_centibel, AUDIO_FADE_SILENCE_CENTIBEL);
    T_ASSERT_EQ_I(got, AUDIO_FADE_SILENCE_CENTIBEL);

    /* Even with full target, slider-0 stays at -10000. */
    g_apply_hook_calls = 0;
    got = audio_fade_apply_bgm_tick(0.0f);
    T_ASSERT_EQ_I(got, AUDIO_FADE_SILENCE_CENTIBEL);

    audio_fade_reset();
    return 0;
}

int test_audio_fade_apply_bgm_tick_lower_target_attenuates(void)
{
    /* target_volume < 1.0 produces a lower target_centibel, which lowers
     * the apply result through the cos curve. At slider 9 (cos=1), the
     * formula collapses to:
     *   centibel = 1.0 * (target_centibel + 9600) - 9600
     *            = target_centibel
     * so target_volume == 0.5 → target_centibel == -4800. */
    audio_fade_reset();
    audio_fade_set_apply_hook(test_apply_hook);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 9);

    int32_t at_full = audio_fade_apply_bgm_tick(1.0f);
    int32_t at_half = audio_fade_apply_bgm_tick(0.5f);
    int32_t at_zero = audio_fade_apply_bgm_tick(0.0f);

    T_ASSERT_EQ_I(at_full, 0);
    T_ASSERT_EQ_I(at_half, -4800);
    T_ASSERT_EQ_I(at_zero, -9600);

    audio_fade_reset();
    return 0;
}

int test_audio_fade_apply_bgm_tick_skips_hook_when_unset(void)
{
    /* No hook installed → audio_fade_apply_bgm_tick returns the centibel
     * but doesn't call out. */
    audio_fade_reset();   /* clears hook + sets sliders to 9 */
    g_apply_hook_calls = 0;
    int32_t got = audio_fade_apply_bgm_tick(1.0f);
    T_ASSERT_EQ_I(g_apply_hook_calls, 0);
    T_ASSERT_EQ_I(got, 0);
    return 0;
}

int test_audio_fade_apply_bgm_tick_clamps_target_volume(void)
{
    /* Defensive clamping: negative target_volume → 0.0 (silence floor);
     * over-1.0 → 1.0 (full target). */
    audio_fade_reset();
    audio_fade_set_apply_hook(test_apply_hook);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 9);

    int32_t below = audio_fade_apply_bgm_tick(-0.5f);
    int32_t above = audio_fade_apply_bgm_tick( 1.5f);
    T_ASSERT_EQ_I(below, -9600);    /* clamped to 0.0 → curve floor */
    T_ASSERT_EQ_I(above, 0);        /* clamped to 1.0 → full target */

    audio_fade_reset();
    return 0;
}

/* ─── per-tick fade animation (FUN_0049966a tail) ──────────────────────
 *
 * Phase 1 (fade-OUT): cos(angle_progress) = cos(progress*π/2/duration),
 *   so at progress=0 the cos is 1.0 (loud) and at progress=duration it's
 *   0.0 (silence). With slider=9 (cos(0)=1), result spans
 *   [0, -9600].
 * Phase 2 (fade-IN): cos(angle_progress) = cos((duration-progress)*π/2/
 *   duration), so at progress=0 cos is 0.0 (silence) and at
 *   progress=duration cos is 1.0 (loud). */

int test_audio_fade_progress_phase1_starts_loud(void)
{
    /* Phase 1, progress=0, slider=9: cos(0)*cos(0)*9600 - 9600 = 0 (loud). */
    int32_t got = audio_fade_progress_centibel(1, 0, 600, 9);
    T_ASSERT_EQ_I(got, 0);
    return 0;
}

int test_audio_fade_progress_phase1_ends_silent(void)
{
    /* Phase 1, progress=duration, slider=9: cos(π/2)*cos(0)*9600 - 9600
     * ≈ 0 * 1 * 9600 - 9600 = -9600 (math-floor silence). */
    int32_t got = audio_fade_progress_centibel(1, 600, 600, 9);
    /* Allow 2-centibel slack on the cosine asymptote. */
    T_ASSERT(got <= -9598);
    T_ASSERT(got >= -9600);
    return 0;
}

int test_audio_fade_progress_phase1_monotonic_decreasing(void)
{
    /* Phase 1 with slider=9 — cos(progress) is monotonically decreasing
     * over [0, π/2], so the centibel walks loud → silent. Sample every
     * 50 frames over duration=600. */
    int32_t prev = audio_fade_progress_centibel(1, 0, 600, 9);
    for (int32_t p = 50; p <= 600; p += 50) {
        int32_t cur = audio_fade_progress_centibel(1, p, 600, 9);
        if (cur >= prev) {
            T_FAIL("progress %d centibel %d not strictly < prev %d",
                   (int)p, (int)cur, (int)prev);
        }
        prev = cur;
    }
    return 0;
}

int test_audio_fade_progress_phase2_starts_silent(void)
{
    /* Phase 2, progress=0: angle_progress = (duration-0)*π/2/duration =
     * π/2, cos=0, centibel = 0*cos(slider_angle)*9600 - 9600 = -9600. */
    int32_t got = audio_fade_progress_centibel(2, 0, 600, 9);
    T_ASSERT(got <= -9598);
    T_ASSERT(got >= -9600);
    return 0;
}

int test_audio_fade_progress_phase2_ends_loud(void)
{
    /* Phase 2, progress=duration: angle_progress=0, cos=1, centibel=0. */
    int32_t got = audio_fade_progress_centibel(2, 600, 600, 9);
    T_ASSERT_EQ_I(got, 0);
    return 0;
}

int test_audio_fade_progress_phase2_monotonic_increasing(void)
{
    int32_t prev = audio_fade_progress_centibel(2, 0, 600, 9);
    for (int32_t p = 50; p <= 600; p += 50) {
        int32_t cur = audio_fade_progress_centibel(2, p, 600, 9);
        if (cur <= prev) {
            T_FAIL("progress %d centibel %d not > prev %d",
                   (int)p, (int)cur, (int)prev);
        }
        prev = cur;
    }
    return 0;
}

int test_audio_fade_progress_lower_slider_attenuates_peak(void)
{
    /* Phase 2 at progress=duration is the loudest point of the fade-in.
     * Lower slider → more attenuation even at the peak.
     *
     * slider=9: peak = 0
     * slider=0: peak = cos(0)*cos(2π/5)*9600 - 9600
     *                 ≈ 1 * 0.309 * 9600 - 9600 ≈ -6633 */
    int32_t loud   = audio_fade_progress_centibel(2, 600, 600, 9);
    int32_t quiet0 = audio_fade_progress_centibel(2, 600, 600, 0);
    T_ASSERT(loud > quiet0);
    T_ASSERT(quiet0 < -6600);
    T_ASSERT(quiet0 > -6700);

    /* Same for phase 1 at progress=0 (the loudest point of fade-OUT). */
    loud   = audio_fade_progress_centibel(1, 0, 600, 9);
    quiet0 = audio_fade_progress_centibel(1, 0, 600, 0);
    T_ASSERT(loud > quiet0);
    return 0;
}

int test_audio_fade_progress_slider_clamped(void)
{
    /* slider <0 clamps to 0; slider >9 clamps to 9. */
    int32_t a = audio_fade_progress_centibel(1, 0, 600, -5);
    int32_t b = audio_fade_progress_centibel(1, 0, 600,  0);
    T_ASSERT_EQ_I(a, b);
    a = audio_fade_progress_centibel(1, 0, 600, 50);
    b = audio_fade_progress_centibel(1, 0, 600,  9);
    T_ASSERT_EQ_I(a, b);
    return 0;
}

int test_audio_fade_progress_progress_clamped_and_overshoot(void)
{
    /* progress > duration clamps to duration. */
    int32_t at_end       = audio_fade_progress_centibel(1, 600, 600, 9);
    int32_t at_overshoot = audio_fade_progress_centibel(1, 999, 600, 9);
    T_ASSERT_EQ_I(at_end, at_overshoot);

    /* progress < 0 clamps to 0. */
    int32_t at_start     = audio_fade_progress_centibel(2, 0, 600, 9);
    int32_t at_neg       = audio_fade_progress_centibel(2, -42, 600, 9);
    T_ASSERT_EQ_I(at_start, at_neg);
    return 0;
}

int test_audio_fade_progress_degenerate_duration_falls_back_to_slider(void)
{
    /* duration<=0 returns the slider-only baseline. */
    T_ASSERT_EQ_I(audio_fade_progress_centibel(1, 0,  0, 9),
                  audio_fade_compute(9, 0));
    T_ASSERT_EQ_I(audio_fade_progress_centibel(2, 0, -1, 4),
                  audio_fade_compute(4, 0));
    /* phase==0 likewise falls back. */
    T_ASSERT_EQ_I(audio_fade_progress_centibel(0, 100, 600, 5),
                  audio_fade_compute(5, 0));
    return 0;
}

int test_audio_fade_apply_progress_drives_hook(void)
{
    audio_fade_reset();
    audio_fade_set_apply_hook(test_apply_hook);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 9);
    g_apply_hook_calls = 0;

    /* Phase 1, progress=0, slider=9 → centibel 0. */
    int32_t got = audio_fade_apply_progress(AUDIO_FADE_CHANNEL_BGM, 1, 0, 600);
    T_ASSERT_EQ_I(g_apply_hook_calls, 1);
    T_ASSERT_EQ_I(g_apply_hook_last_channel, AUDIO_FADE_CHANNEL_BGM);
    T_ASSERT_EQ_I(g_apply_hook_last_centibel, 0);
    T_ASSERT_EQ_I(got, 0);

    /* Phase 1, progress=duration → silence-ish. */
    got = audio_fade_apply_progress(AUDIO_FADE_CHANNEL_BGM, 1, 600, 600);
    T_ASSERT_EQ_I(g_apply_hook_calls, 2);
    T_ASSERT(g_apply_hook_last_centibel <= -9598);

    audio_fade_reset();
    return 0;
}

int test_audio_fade_apply_progress_rejects_invalid_channel(void)
{
    audio_fade_reset();
    audio_fade_set_apply_hook(test_apply_hook);
    g_apply_hook_calls = 0;
    audio_fade_apply_progress(-1, 1, 100, 600);
    audio_fade_apply_progress(AUDIO_FADE_CHANNEL_COUNT, 1, 100, 600);
    T_ASSERT_EQ_I(g_apply_hook_calls, 0);
    audio_fade_reset();
    return 0;
}
