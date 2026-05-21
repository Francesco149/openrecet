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

int test_audio_fade_apply_is_noop(void)
{
    /* Stub for now — must not crash, must not assert. */
    audio_fade_apply(0);
    audio_fade_apply(7);
    return 0;
}
