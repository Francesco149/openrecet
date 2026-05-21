/*
 * audio_fade.h — BGM volume fade-in curve.
 *
 * Faithful port of FUN_00499583 (231 bytes). The engine's
 * BGM-volume-fade ramps from "silence" up to a target volume across
 * 10 ticks (frame counter 9 → 0). The ramp curve is a cosine arc over
 * angles [0, 2π/5]:
 *
 *   angle = (9 - frame) * 2π/5 / 9
 *   ratio = cos(angle)
 *
 * giving cos(0)=1 at frame 9 (full target) and cos(2π/5)≈0.309 at
 * frame 1. The engine then converts to a centibel volume:
 *
 *   centibel = cos(angle) * (target_centibel + 9600) - 9600
 *
 * The two 9600 constants live at &DAT_005196b4 (=9.0 — divisor) and
 * &DAT_00519ff4 (=9600.0). Note the funny asymmetry: frame 0 *skips*
 * the math entirely and just calls SetVolume(-10000, 0) directly,
 * even though the formula at frame 0 would produce
 *   cos(2π/5) * (target+9600) - 9600 ≈ 0.309*target - 6633
 * — i.e. not silence. The engine's frame-0 branch hard-pins to
 * -10000 ("hard silence") regardless of target, which is louder
 * attenuation than the math curve's asymptote of -9600.
 *
 * The trig leg is FUN_00503994 — a CRT-style cos() wrapper with FPU
 * control-word juggling; behaviorally just `cosf(angle)`, so we use
 * libc cos() directly here.
 *
 * Engine sources:
 *   docs/decompiled/by-address/499583.c  — fade dispatcher
 *   docs/decompiled/by-address/503994.c  — cos() wrapper
 *
 * Module shape: pure computation, no DirectMusic dependency. The
 * apply-to-audiopath step (`SetVolume`) is a separate function that
 * the upcoming SE/audio commit will wire into the live audio module.
 */

#ifndef OPENRECET_AUDIO_FADE_H
#define OPENRECET_AUDIO_FADE_H

#include <stdint.h>

/* Centibels. -10000 = total silence (engine's frame-0 fast path). */
#define AUDIO_FADE_SILENCE_CENTIBEL  (-10000)

/* The fade math's silence asymptote (different from the hard
 * AUDIO_FADE_SILENCE_CENTIBEL — see header comment). */
#define AUDIO_FADE_MATH_FLOOR_CENTIBEL  (-9600)

/* The engine's fixed ramp length: frame counter values 0..9 inclusive,
 * so 10 distinct steps. */
#define AUDIO_FADE_FRAME_COUNT  10

/* Map a frame counter in [0, 9] to a centibel volume.
 *
 *   frame 0    → AUDIO_FADE_SILENCE_CENTIBEL  (-10000, hard silence;
 *                engine's early-return path, ignores target).
 *   frame 9    → target_centibel  (no attenuation).
 *   frame 1..8 → cos(angle) * (target_centibel + 9600) - 9600
 *                where angle = (9 - frame) * 2π/5 / 9.
 *                Monotonically increasing from frame 1 → 9.
 *
 * Out-of-range frame_counter (negative or > 9) is clamped to the
 * nearest valid value. This mirrors the engine's *behavior*: it never
 * sees out-of-range inputs in normal operation (caller ranges 0..9),
 * but giving a defined result is safer than crashing on a typo. */
int32_t audio_fade_compute(int frame_counter, int32_t target_centibel);

/* Apply the computed volume to the BGM audiopath. For now this is a
 * no-op stub: no caller wires it yet, and the live audiopath call
 * (`(*vt[5])(path, centibel, 0)` — IDirectMusicAudioPath8::SetVolume
 * via vtable +0x14) needs the same #ifdef _WIN32 guard the rest of
 * audio.c uses. The math + curve plot ride ahead of that. */
void audio_fade_apply(int channel);

#endif /* OPENRECET_AUDIO_FADE_H */
