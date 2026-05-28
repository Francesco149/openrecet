/*
 * diff_entry.h — ABI for tools/diff_test.py's ctypes consumers.
 *
 * Every target follows the same shape:
 *
 *   void engine_<target>(const Engine<Target>In *in, Engine<Target>Out *out);
 *
 * The In struct holds the inputs the orchestrator must inject on the
 * retail side (via Frida writeU32 / writeMemory / ...) before invoking
 * the retail function; the Out struct holds the observables the
 * orchestrator reads back to compare.
 *
 * Wire stability: append new fields at the end of existing structs;
 * never reorder.  diff_test.py mirrors these structs with
 * ctypes.Structure declarations, so layout changes here require
 * matching changes on the Python side.
 */

#ifndef OPENRECET_DIFF_ENTRY_H
#define OPENRECET_DIFF_ENTRY_H

#include <stdint.h>

/* ── rng_next15 (FUN_005041f6 / DAT_006023a0) ──────────────────────── */

typedef struct EngineRngIn {
    uint32_t seed;          /* injected pre-state for DAT_006023a0 */
} EngineRngIn;

typedef struct EngineRngOut {
    uint32_t post_state;    /* DAT_006023a0 after one LCG step */
    uint16_t ret_value;     /* 15-bit return of FUN_005041f6 */
    uint16_t _pad;          /* explicit, keeps layout deterministic */
} EngineRngOut;

void engine_rng_next15(const EngineRngIn *in, EngineRngOut *out);

/* ── audio_fade (FUN_00499583 / BGM cos-curve fade) ────────────────── */

typedef struct EngineFadeIn {
    int32_t slider;         /* BGM volume slider in [0, 9] */
} EngineFadeIn;

typedef struct EngineFadeOut {
    int32_t centibel;       /* audio_fade_compute(slider, 0) result */
} EngineFadeOut;

void engine_audio_fade(const EngineFadeIn *in, EngineFadeOut *out);

#endif /* OPENRECET_DIFF_ENTRY_H */
