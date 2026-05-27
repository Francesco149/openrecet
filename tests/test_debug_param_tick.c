/*
 * test_debug_param_tick.c — FUN_00405552 @ 0x405552 (498 bytes).
 *
 * This chip ports the gate + function-call boundary only; the eight-
 * way switch body is deferred (see debug_param_tick.h). Tests cover
 * the ported surface: gate semantics + reset.
 */
#include "t.h"
#include "debug_param_tick.h"

int test_debug_param_tick_reset_zeroes_gate(void)
{
    debug_param_tick_set_gate(1);
    T_ASSERT_EQ_I(debug_param_tick_get_gate(), 1);
    debug_param_tick_reset();
    T_ASSERT_EQ_I(debug_param_tick_get_gate(), 0);
    return 0;
}

int test_debug_param_tick_set_gate_normalises_to_zero_one(void)
{
    debug_param_tick_reset();
    debug_param_tick_set_gate(42);
    T_ASSERT_EQ_I(debug_param_tick_get_gate(), 1);
    debug_param_tick_set_gate(0);
    T_ASSERT_EQ_I(debug_param_tick_get_gate(), 0);
    debug_param_tick_set_gate(-1);
    T_ASSERT_EQ_I(debug_param_tick_get_gate(), 1);
    return 0;
}

int test_debug_param_tick_gate_zero_is_idempotent(void)
{
    /* Gate==0 → early return path. Function has no side effects we can
     * observe in this chip (body deferred); the contract reduces to
     * "doesn't crash, doesn't flip its own gate". */
    debug_param_tick_reset();
    for (int i = 0; i < 100; i++) debug_param_tick();
    T_ASSERT_EQ_I(debug_param_tick_get_gate(), 0);
    return 0;
}

int test_debug_param_tick_gate_one_is_also_a_noop_body_deferred(void)
{
    /* Gate==1 → falls through into the deferred body, which is also a
     * no-op in this chip. Verify the function doesn't flip the gate or
     * crash. When the body lands, this test gets replaced with one
     * that asserts the eight-way switch behaviour. */
    debug_param_tick_reset();
    debug_param_tick_set_gate(1);
    for (int i = 0; i < 100; i++) debug_param_tick();
    T_ASSERT_EQ_I(debug_param_tick_get_gate(), 1);
    return 0;
}
