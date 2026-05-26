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
