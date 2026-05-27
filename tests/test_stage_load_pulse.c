/*
 * test_stage_load_pulse.c — FUN_004693e3 @ 0x4693e3 (41 bytes).
 *
 * Counter clamp 0..5 keyed off the active flag. Asm verbatim:
 *
 *   if (active == 0) { if (counter > 0) counter--; }
 *   else             { if (counter < 5) counter++; }
 */
#include "t.h"
#include "stage_load_pulse.h"

int test_stage_load_pulse_reset_zeroes_state(void)
{
    stage_load_pulse_set_active(1);
    stage_load_pulse_reset_counter_to_5();
    T_ASSERT_EQ_I(stage_load_pulse_get_active(),  1);
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 5);

    stage_load_pulse_reset();
    T_ASSERT_EQ_I(stage_load_pulse_get_active(),  0);
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 0);
    return 0;
}

int test_stage_load_pulse_set_active_normalises_to_zero_one(void)
{
    stage_load_pulse_reset();
    stage_load_pulse_set_active(42);
    T_ASSERT_EQ_I(stage_load_pulse_get_active(), 1);
    stage_load_pulse_set_active(0);
    T_ASSERT_EQ_I(stage_load_pulse_get_active(), 0);
    stage_load_pulse_set_active(-1);
    T_ASSERT_EQ_I(stage_load_pulse_get_active(), 1);
    return 0;
}

int test_stage_load_pulse_tick_idle_decrements_toward_zero(void)
{
    /* active==0 + counter>0 → counter-- */
    stage_load_pulse_reset();
    stage_load_pulse_reset_counter_to_5();
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 5);

    for (int expect = 4; expect >= 0; expect--) {
        stage_load_pulse_tick();
        T_ASSERT_EQ_I(stage_load_pulse_get_counter(), expect);
    }
    return 0;
}

int test_stage_load_pulse_tick_idle_clamps_at_zero(void)
{
    /* active==0 + counter==0 → no-op (counter stays 0). */
    stage_load_pulse_reset();
    for (int i = 0; i < 10; i++) {
        stage_load_pulse_tick();
        T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 0);
    }
    return 0;
}

int test_stage_load_pulse_tick_active_increments_toward_five(void)
{
    /* active==1 + counter<5 → counter++ */
    stage_load_pulse_reset();
    stage_load_pulse_set_active(1);
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 0);

    for (int expect = 1; expect <= 5; expect++) {
        stage_load_pulse_tick();
        T_ASSERT_EQ_I(stage_load_pulse_get_counter(), expect);
    }
    return 0;
}

int test_stage_load_pulse_tick_active_clamps_at_five(void)
{
    /* active==1 + counter==5 → no-op (counter stays 5). */
    stage_load_pulse_reset();
    stage_load_pulse_set_active(1);
    stage_load_pulse_reset_counter_to_5();
    for (int i = 0; i < 10; i++) {
        stage_load_pulse_tick();
        T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 5);
    }
    return 0;
}

int test_stage_load_pulse_tick_flip_active_reverses_direction(void)
{
    /* Ramp up to 3, flip off, ramp down. Verify the per-frame direction
     * change is exactly synchronous with the flag flip (no latch). */
    stage_load_pulse_reset();
    stage_load_pulse_set_active(1);
    for (int i = 0; i < 3; i++) stage_load_pulse_tick();
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 3);

    stage_load_pulse_set_active(0);
    stage_load_pulse_tick();
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 2);
    stage_load_pulse_tick();
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 1);
    stage_load_pulse_tick();
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 0);
    stage_load_pulse_tick();  /* clamp */
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 0);
    return 0;
}

int test_stage_load_pulse_reset_counter_to_5_overrides_active(void)
{
    /* The FUN_004682e3 setter sets counter to 5 regardless of `active`
     * — both inactive→5 and active→5 are legal. Verify the helper
     * lives up to that contract. */
    stage_load_pulse_reset();
    stage_load_pulse_reset_counter_to_5();
    T_ASSERT_EQ_I(stage_load_pulse_get_active(),  0);
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 5);

    stage_load_pulse_set_active(1);
    stage_load_pulse_reset_counter_to_5();
    T_ASSERT_EQ_I(stage_load_pulse_get_active(),  1);
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 5);
    return 0;
}
