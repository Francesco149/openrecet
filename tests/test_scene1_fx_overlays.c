/*
 * test_scene1_fx_overlays.c — scaffold-port smoke for FUN_00454191.
 *
 * The body is a STUB today (entry probe + outer-gate scaffold; inner
 * render branches deferred).  These tests pin the visible contract:
 *
 *   1. Function is callable with a NULL device.
 *   2. Default counter state (both BSS-zero) → both gates short-circuit.
 *   3. Non-zero counters take the gated branches without crashing —
 *      the deferred bodies stay as inert TODO blocks.
 *
 * Real assertions land when the counter starters port and the inner
 * draws fill in.
 */

#include "t.h"
#include "scene1_fx_overlays.h"
#include "sim.h"

int test_scene1_fx_overlays_null_device_safe(void)
{
    /* Stub body only reads counters; dev is currently unused.  Still
     * guard the contract: function returns cleanly with NULL device. */
    sim_set_counter_99c(0);
    sim_set_counter_990(0);

    scene1_fx_overlays(NULL);

    /* Counter state is untouched. */
    T_ASSERT_EQ_I(sim_get_counter_99c(), 0);
    T_ASSERT_EQ_I(sim_get_counter_990(), 0);
    return 0;
}

int test_scene1_fx_overlays_default_counters_no_op(void)
{
    /* BSS-zero starting state: both gates `1 < counter_xx` fail. */
    sim_set_counter_99c(0);
    sim_set_counter_990(0);

    scene1_fx_overlays(NULL);

    /* No state mutation. */
    T_ASSERT_EQ_I(sim_get_counter_99c(), 0);
    T_ASSERT_EQ_I(sim_get_counter_990(), 0);
    return 0;
}

int test_scene1_fx_overlays_active_counters_no_crash(void)
{
    /* Future-proofing: when the counter starters wire up, the gates
     * pass and the inner branches run.  Today the branches are
     * deferred TODO blocks — exercising them must NOT crash even though
     * the work is skipped. */
    sim_set_counter_99c(3);
    sim_set_counter_990(5);

    scene1_fx_overlays(NULL);

    /* Counters untouched — the deferred branches don't mutate them. */
    T_ASSERT_EQ_I(sim_get_counter_99c(), 3);
    T_ASSERT_EQ_I(sim_get_counter_990(), 5);

    /* Reset for downstream tests. */
    sim_set_counter_99c(0);
    sim_set_counter_990(0);
    return 0;
}
