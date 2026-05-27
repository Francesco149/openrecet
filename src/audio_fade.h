/*
 * audio_fade.h — BGM/SE volume curve + per-channel slider state.
 *
 * Faithful port of FUN_00499583 (231 bytes). The engine's
 * volume curve ramps from "silence" up to a target volume across
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
 * ── Per-channel slider state ──
 *
 * The engine tracks three independent volume sliders in the save-data
 * arena at:
 *   DAT_056e5778 — BGM   (engine init: 5, save-data persisted)
 *   DAT_056e5774 — SE-A  (engine init: 9)
 *   DAT_056e577c — SE-B  (engine init: 9, dead in vendor data per
 *                         engine-quirks #46)
 *
 * The settings menu (FUN_0047fc44) and the title sound-config submenu
 * (FUN_0049a59e tail) bump these counters via player input. Each bump
 * re-applies the corresponding SetVolume immediately via FUN_00499583
 * (BGM) or FUN_00499c63 (SE).
 *
 * The port keeps the three slider values as module-local state with
 * `audio_fade_set_slider` / `audio_fade_get_slider` accessors. Defaults
 * are 9/9/9 (full volume); main.c overwrites BGM/SE-A from recet.ini's
 * `mu`/`se` keys immediately after audio_init. The engine's BGM=5
 * save-arena default is intentionally NOT mirrored: it only takes
 * effect on save-load (FUN_004902fe, not ported yet); until save-load
 * lands, recet.ini is the user-configurable source of truth and
 * SE-B stays at the default 9 (dormant per engine-quirks #46).
 *
 * Engine sources:
 *   docs/decompiled/by-address/499583.c  — fade dispatcher
 *   docs/decompiled/by-address/4901c2.c  — save-arena init (5/9/9/1)
 *   docs/decompiled/by-address/47fc44.c  — settings-menu slider producer
 *   docs/decompiled/by-address/503994.c  — cos() wrapper
 *
 * Module shape: pure computation + tiny state, no DirectMusic
 * dependency. The apply-to-audiopath step (`SetVolume`) is decoupled
 * via a hook function pointer that the Win32 backend (audio.c)
 * installs at init time.
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

/* Volume-slider channels. Order matches the engine's per-counter
 * arrangement (BGM = DAT_056e5778, SE-A = _5774, SE-B = _577c) but as
 * a stable indexed enum so callers don't need to know the offsets. */
#define AUDIO_FADE_CHANNEL_BGM    0
#define AUDIO_FADE_CHANNEL_SE_A   1
#define AUDIO_FADE_CHANNEL_SE_B   2
#define AUDIO_FADE_CHANNEL_COUNT  3

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

/* Slider get/set for a channel in [0, AUDIO_FADE_CHANNEL_COUNT).
 * Set clamps `value` to [0, 9]; out-of-range channel is a no-op (set)
 * or returns 0 (get). Default value at first read is 9 (full target). */
void audio_fade_set_slider(int channel, int value);
int  audio_fade_get_slider(int channel);

/* Compute the centibel for `channel`'s current slider at full target
 * (target_centibel = 0). Equivalent to
 * `audio_fade_compute(audio_fade_get_slider(channel), 0)` but stays in
 * one place so future per-tick fade animation (driven by music_step's
 * target_volume ramp) can be wired here without touching every call
 * site. */
int32_t audio_fade_channel_centibel(int channel);

/* Per-tick fade animation centibel (FUN_0049966a LAB_00499a00 — the
 * two-axis cos product). Computes one frame's volume during an
 * in-progress fade:
 *
 *   angle_progress = (phase == 1 ? progress : duration - progress)
 *                    * π/2 / duration
 *   angle_slider   = (9 - slider) * 2π/5 / 9
 *   centibel       = cos(angle_progress) * cos(angle_slider) * 9600 - 9600
 *
 * Phase semantics (per the assembly — the two branches at 0x499a2b /
 * 0x499a9e of FUN_0049966a):
 *   phase == 1: cos(angle_progress) goes 1.0 → 0.0 as progress
 *               advances 0 → duration  ⇒ audible fade-OUT
 *   phase != 1 (and != 0): cos(angle_progress) goes 0.0 → 1.0
 *                          ⇒ audible fade-IN
 *
 * Clamping (defensive — engine never sees out-of-range, but a defined
 * result is friendlier than UB):
 *   - slider     clamped to [0, 9]
 *   - progress   clamped to [0, duration]
 *   - duration <= 0 returns the slider's baseline centibel (i.e.,
 *     audio_fade_compute(slider, 0) — equivalent to "fade complete"
 *     for fade-in, "fade just started" for fade-out).
 *   - phase == 0 returns the slider's baseline centibel; callers should
 *     not invoke this with phase==0 (it's the "idle" sentinel).
 */
int32_t audio_fade_progress_centibel(int phase,
                                     int32_t progress,
                                     int32_t duration,
                                     int slider);

/* Per-tick fade-application: combines `audio_fade_progress_centibel`
 * with the channel's current slider, then forwards the result to the
 * apply hook (no trace event — the per-tick fade fires up to
 * `duration` times so per-frame tracing would swamp the JSONL log).
 *
 * Returns the centibel that was sent to the hook (or would have been
 * sent if no hook is installed). Invalid channel returns 0 + no-op.
 */
int32_t audio_fade_apply_progress(int channel,
                                  int phase,
                                  int32_t progress,
                                  int32_t duration);

/* Apply `channel`'s current slider to whatever output that channel
 * drives. Pure C — emits a `fade_start` trace event (audio.h) and then
 * delegates the platform call (IDirectMusicAudioPath::SetVolume on
 * Win32) to a hook function pointer the audio backend installs.
 *
 * Returns the centibel that was applied (or would have been applied if
 * no hook is installed). */
int32_t audio_fade_apply(int channel);

/* Per-tick BGM volume apply (engine FUN_00499583 entry).
 *
 * Called once per sim_b tick from music_step in the title bare-play band
 * (counter < 0x1b6d) and the title fade band (counter in [0x1b6d, 0x1ba7)).
 * Reads the CURRENT BGM slider, combines with `target_volume` (in [0,1] —
 * 1.0 = full target, 0.0 = silence) to derive a target centibel, then
 * forwards (channel, centibel) through the apply hook.
 *
 * Does NOT emit the audio-trace fade_start event (firing per-frame
 * during the title fade band would spam the JSONL log). The
 * CALL_TRACE_ENTER probe for engine VA 0x499583 fires here for
 * port/retail call-trace diffing.
 *
 * Behaviour quirk matching the engine: when the BGM slider is 0 the
 * engine hard-pins the hook arg to AUDIO_FADE_SILENCE_CENTIBEL
 * (-10000), bypassing the math curve (which would otherwise produce
 * the curve floor at -9600). Same fast path here. */
int32_t audio_fade_apply_bgm_tick(float target_volume);

/* Hook installer. The Win32 audio backend registers a function that
 * receives (channel, centibel) and calls SetVolume on the matching
 * AudioPath. Pass NULL to clear. Tests use this to capture applied
 * volumes without touching DirectMusic. */
typedef void (*audio_fade_apply_hook_t)(int channel, int32_t centibel);
void audio_fade_set_apply_hook(audio_fade_apply_hook_t hook);

/* Reset slider state + clear the apply hook. Used by tests to keep
 * each case isolated; audio_init also calls this so a re-init starts
 * from a known baseline. */
void audio_fade_reset(void);

#endif /* OPENRECET_AUDIO_FADE_H */
