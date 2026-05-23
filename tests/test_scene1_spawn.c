/*
 * test_scene1_spawn.c — unit tests for the scene1_spawn stub.
 *
 * The stub stands in for FUN_00447f4f (C8i, unported).  Verifies:
 *   - First call writes to slot 0.
 *   - Trace counter increments.
 *   - Ring buffer wraps at SCENE1_SPAWN_TRACE_CAPACITY.
 *   - Reset zeroes the trace.
 */

#include "t.h"
#include "scene1_spawn.h"

int test_scene1_spawn_first_call_recorded(void)
{
    scene1_spawn_trace_reset();
    T_ASSERT(g_scene1_spawn_trace_count == 0);

    scene1_spawn(0, 1.0f, 2.0f, 3.0f, 0x21, 1.0f, 0);

    T_ASSERT(g_scene1_spawn_trace_count == 1);
    T_ASSERT(g_scene1_spawn_trace[0].slot_hint == 0);
    T_ASSERT(g_scene1_spawn_trace[0].x == 1.0f);
    T_ASSERT(g_scene1_spawn_trace[0].y == 2.0f);
    T_ASSERT(g_scene1_spawn_trace[0].z == 3.0f);
    T_ASSERT(g_scene1_spawn_trace[0].type == 0x21);
    T_ASSERT(g_scene1_spawn_trace[0].scale == 1.0f);
    T_ASSERT(g_scene1_spawn_trace[0].param7 == 0);
    return 0;
}

int test_scene1_spawn_ring_wraps(void)
{
    scene1_spawn_trace_reset();
    /* Fill the ring + one extra; the extra overwrites slot 0. */
    for (int i = 0; i < SCENE1_SPAWN_TRACE_CAPACITY + 1; i++) {
        scene1_spawn(i, (float)i, 0.0f, 0.0f, i, 1.0f, 0);
    }
    T_ASSERT(g_scene1_spawn_trace_count == SCENE1_SPAWN_TRACE_CAPACITY + 1);
    /* Slot 0 was overwritten by the wrapping call (i == capacity). */
    T_ASSERT(g_scene1_spawn_trace[0].slot_hint == SCENE1_SPAWN_TRACE_CAPACITY);
    /* Slot 1 still holds the original i==1 call. */
    T_ASSERT(g_scene1_spawn_trace[1].slot_hint == 1);
    return 0;
}

int test_scene1_spawn_reset_clears(void)
{
    scene1_spawn(0, 99.0f, 99.0f, 99.0f, 0x99, 9.0f, 9);
    scene1_spawn_trace_reset();
    T_ASSERT(g_scene1_spawn_trace_count == 0);
    /* Trace slot 0 should be zeroed too. */
    T_ASSERT(g_scene1_spawn_trace[0].slot_hint == 0);
    T_ASSERT(g_scene1_spawn_trace[0].x == 0.0f);
    T_ASSERT(g_scene1_spawn_trace[0].type == 0);
    return 0;
}
