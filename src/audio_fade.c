/*
 * audio_fade.c — see audio_fade.h.
 *
 * Three responsibilities:
 *
 *   1. audio_fade_compute(frame, target) — pure cos-curve math from
 *      FUN_00499583. Bit-perfect (within a centibel) vs the engine's
 *      __ftol output across the valid range.
 *   2. Per-channel slider state (BGM, SE-A, SE-B). Mirrors the engine's
 *      save-data counters; defaults to 9 (full target) for all three
 *      until save-load lands. See audio_fade.h header comment for the
 *      "BGM=9 not 5" deviation note.
 *   3. audio_fade_apply(channel) — emits a `fade_start` trace event
 *      and forwards (channel, centibel) to a hook the Win32 audio
 *      backend installs at init time. Decoupling via the hook keeps
 *      audio_fade.c free of dmusici.h so it stays in the test build.
 */

#include "audio_fade.h"
#include "audio.h"     /* audio_trace_emit_fade_start */

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

/* ─── per-channel slider state ─────────────────────────────────────────
 *
 * Three [0..9] sliders, defaults 9. Cleared via audio_fade_reset(). */

static int g_sliders[AUDIO_FADE_CHANNEL_COUNT] = { 9, 9, 9 };
static audio_fade_apply_hook_t g_apply_hook = NULL;

void audio_fade_set_slider(int channel, int value)
{
    if (channel < 0 || channel >= AUDIO_FADE_CHANNEL_COUNT) return;
    if (value < 0) value = 0;
    if (value > 9) value = 9;
    g_sliders[channel] = value;
}

int audio_fade_get_slider(int channel)
{
    if (channel < 0 || channel >= AUDIO_FADE_CHANNEL_COUNT) return 0;
    return g_sliders[channel];
}

int32_t audio_fade_channel_centibel(int channel)
{
    /* target_centibel = 0 for now (full target). Once music_step's
     * target_volume ramp wires in, this becomes
     *   audio_fade_compute(slider, (target_volume - 1.0f) * 9600).
     * For BGM that's the title-exit fade band; SE has no equivalent. */
    return audio_fade_compute(audio_fade_get_slider(channel), 0);
}

/* ─── per-tick fade animation ───────────────────────────────────────── */

#define AUDIO_FADE_HALF_PI  1.5707963267948966   /* &DAT_00519434 */

int32_t audio_fade_progress_centibel(int phase,
                                     int32_t progress,
                                     int32_t duration,
                                     int slider)
{
    if (slider < 0) slider = 0;
    if (slider > 9) slider = 9;

    /* Idle / degenerate duration → fall back to the slider-only ramp's
     * full-target value. Defensive: callers gate phase != 0 + duration > 0
     * before calling. */
    if (phase == 0 || duration <= 0) {
        return audio_fade_compute(slider, 0);
    }

    if (progress < 0) progress = 0;
    if (progress > duration) progress = duration;

    /* angle_slider = (9 - slider) * 2π/5 / 9  — same as the slider-only
     * fade's angle for frame=slider. */
    double angle_slider = ((double)(9 - slider) * AUDIO_FADE_ANGLE_MAX)
                          / AUDIO_FADE_FRAME_DIVISOR;

    /* angle_progress depends on phase. For phase 1 the engine reads
     * `progress` directly (cos starts at 1.0, decays to 0 → fade-OUT).
     * For any other non-zero phase it reads `(duration - progress)`
     * (cos starts at 0, grows to 1.0 → fade-IN). */
    double prog_arg = (phase == 1)
                        ? (double)progress
                        : (double)(duration - progress);
    double angle_progress = (prog_arg * AUDIO_FADE_HALF_PI) / (double)duration;

    double cos_product = cos(angle_progress) * cos(angle_slider);
    double centibel = cos_product * AUDIO_FADE_SCALE - AUDIO_FADE_SCALE;
    return (int32_t)centibel;
}

int32_t audio_fade_apply_progress(int channel,
                                  int phase,
                                  int32_t progress,
                                  int32_t duration)
{
    if (channel < 0 || channel >= AUDIO_FADE_CHANNEL_COUNT) return 0;
    int32_t centibel = audio_fade_progress_centibel(
        phase, progress, duration, g_sliders[channel]);
    if (g_apply_hook) {
        g_apply_hook(channel, centibel);
    }
    return centibel;
}

void audio_fade_set_apply_hook(audio_fade_apply_hook_t hook)
{
    g_apply_hook = hook;
}

int32_t audio_fade_apply(int channel)
{
    if (channel < 0 || channel >= AUDIO_FADE_CHANNEL_COUNT) return 0;
    int32_t centibel = audio_fade_compute(g_sliders[channel], 0);
    audio_trace_emit_fade_start(channel, g_sliders[channel], centibel);
    if (g_apply_hook) {
        g_apply_hook(channel, centibel);
    }
    return centibel;
}

void audio_fade_reset(void)
{
    for (int i = 0; i < AUDIO_FADE_CHANNEL_COUNT; i++) {
        g_sliders[i] = 9;
    }
    g_apply_hook = NULL;
}
