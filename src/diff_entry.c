/*
 * diff_entry.c — host-side ABI shims for tools/diff_test.py.
 *
 * Each entry wraps one of our ported pure-math functions in a stable
 * C struct interface so the diff orchestrator can call it via ctypes
 * (libengine_diff.so) and compare its output against the same call
 * dispatched into the retail binary via Frida.
 *
 * Why a separate TU instead of having diff_test.py call rng_next15
 * directly?  Two reasons:
 *
 *   1. ctypes binding stability — the wire is a struct-in/struct-out
 *      pair per target.  Adding fields to the engine port (e.g. adding
 *      a second state global later) won't break the wire as long as
 *      the EngineXxxIn/Out structs stay backward-compatible.
 *   2. ABI clarity — every target has the same call shape, so the
 *      orchestrator's dispatch loop is a flat dict, not a per-target
 *      argument-marshalling block.
 *
 * Built into tests/build/libengine_diff.so by `make -C tests diff`.
 * No sanitizers (the diff path doesn't need ASan; unit tests still
 * cover that).
 *
 * Build expectations: the shared lib only links the C files it needs.
 * For new targets that pull in additional .c, extend tests/Makefile's
 * DIFF_SRCS rather than chaining headers here.
 */

#include "diff_entry.h"

#include "rng.h"
#include "audio_fade.h"

/* NOTE: audio_fade.c references audio_trace_emit_fade_start (audio.c) and
 * call_trace_enter (call_trace.c) from code paths we never invoke here.
 * Those no-op stubs live in tests/diff_stubs.c (NOT here) — this file is
 * globbed into the exe by src/Makefile, so defining them here would
 * duplicate-define the real symbols at link time. The stubs link only into
 * libengine_diff.so via tests/Makefile DIFF_SRCS. */

void engine_rng_next15(const EngineRngIn *in, EngineRngOut *out)
{
    /* The engine RNG is FUN_005041f6 reading/writing DAT_006023a0.
     * Our port mirrors it exactly: rng_seed_set the state, run one
     * LCG step, observe both the 15-bit return value AND the new
     * state.  Retail's runRetailRngNext15 RPC observes the same pair
     * via writeU32 + callU32NoArgs + readU32 over the same global. */
    rng_seed(in->seed);
    out->ret_value = rng_next15();
    out->post_state = g_rng_seed;
}

void engine_audio_fade(const EngineFadeIn *in, EngineFadeOut *out)
{
    /* The engine BGM fade is FUN_00499583: it reads the BGM slider,
     * runs the cos-curve against a full target, and applies the result
     * via IDirectMusicAudioPath::SetVolume.  Our port mirrors the math
     * in audio_fade_compute(slider, 0).  Retail's captureFadeCentibel
     * RPC observes the applied centibel via a fake AudioPath whose
     * SetVolume slot records lVolume — the same observable, with target
     * pinned to 0 (full target) on both sides. */
    out->centibel = audio_fade_compute(in->slider, 0);
}
