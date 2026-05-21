/*
 * audio_fade.c — see audio_fade.h.
 *
 * The engine's FUN_00499583 trampolines a frame counter (DAT_056e5778)
 * through a cos-based attenuation curve and calls
 * IDirectMusicAudioPath8::SetVolume on the BGM path. We split that
 * into:
 *
 *   audio_fade_compute(frame, target)  — pure math (this commit).
 *   audio_fade_apply(channel)          — SetVolume hookup
 *                                        (no-op stub for now;
 *                                        wires in with the SE port).
 */

#include "audio_fade.h"

#include <math.h>

/* 2π/5 ≈ 1.2566371 rad ≈ 72°. The engine's constant at &DAT_00519ff8
 * (verified via tools/analyze/pe.py bytes 0x00519ff0 32). Same value
 * MSVC would emit for a `(float)(2 * M_PI / 5)` literal — fwiw the
 * canonical double 2π/5 is 1.2566370614359172. */
#define AUDIO_FADE_ANGLE_MAX  1.2566370964050293

/* Divisor at &DAT_005196b4. */
#define AUDIO_FADE_FRAME_DIVISOR  9.0

/* +9600 / *9600 conversion constant at &DAT_00519ff4. Note that this
 * is also the centibel floor of the math curve (cos→0 ⇒ result→
 * -9600), which is *not* the same as AUDIO_FADE_SILENCE_CENTIBEL. */
#define AUDIO_FADE_SCALE  9600.0

int32_t audio_fade_compute(int frame_counter, int32_t target_centibel)
{
    /* Clamp first — engine never sees out-of-range, but a defined
     * result is friendlier than UB on a stray caller. */
    if (frame_counter <= 0) {
        return AUDIO_FADE_SILENCE_CENTIBEL;
    }
    if (frame_counter >= AUDIO_FADE_FRAME_COUNT - 1) {
        return target_centibel;
    }

    /* angle = (9 - frame) * 2π/5 / 9 */
    double frames_from_target = (double)((AUDIO_FADE_FRAME_COUNT - 1) - frame_counter);
    double angle = (frames_from_target * AUDIO_FADE_ANGLE_MAX) / AUDIO_FADE_FRAME_DIVISOR;

    /* FUN_00503994 (== cos in disguise) returns cos(angle) via ST(0). */
    double ratio = cos(angle);

    /* result = cos(angle) * (target + 9600) - 9600
     *
     * Algebraic rearrangement of the engine's
     *   cos(angle) * (target_norm) * 9600 - 9600
     * with target_norm = (target_centibel + 9600) / 9600 so frame 9
     * returns target_centibel unchanged.
     *
     * MSVC's __ftol truncates toward zero. The engine's converted
     * intermediate is a float — we use double here then cast. The
     * absolute error vs the bit-exact engine path is sub-centibel
     * across the 1..8 range, which is well below DirectMusic's
     * volume granularity (centibels are 1/100 dB). */
    double centibel = ratio * ((double)target_centibel + AUDIO_FADE_SCALE)
                      - AUDIO_FADE_SCALE;
    return (int32_t)centibel;
}

void audio_fade_apply(int channel)
{
    /* No caller wires this yet. Once the SE port lands we'll add the
     * IDirectMusicAudioPath8::SetVolume hookup behind #ifdef _WIN32
     * here, matching the engine's vtable+0x14 call. (void) the param
     * so -Wunused-parameter stays quiet. */
    (void)channel;
}
