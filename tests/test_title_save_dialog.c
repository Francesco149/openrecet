/*
 * test_title_save_dialog.c — five engine functions clustered around
 * the title-scene save/load dialog (engine VAs 0x434d6a, 0x4356cd,
 * 0x435117, 0x43537e, 0x435747).
 *
 * Covers the two fully-ported state ticks (gate + anim) and verifies
 * the three stub renders are safe at any module state.
 */
#include "t.h"
#include "title_save_dialog.h"
#include "sim.h"

static void clean_state(void)
{
    title_save_dialog_reset();
    /* Clear the just-pressed mask too — gate_tick reads it. */
    g_sim_buttons[0].pressed = 0;
}

/* ─── reset / accessor smoke tests ──────────────────────────────────── */

int test_title_save_dialog_reset_zeroes_state(void)
{
    title_save_dialog_set_active_counter(7);
    title_save_dialog_set_closing_mode(1);
    title_save_dialog_set_shake_counter(5);
    title_save_dialog_set_anim_counter(100);
    title_save_dialog_set_shake_delta(2.0f, -1.0f);

    title_save_dialog_reset();
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 0);
    T_ASSERT_EQ_I(title_save_dialog_get_closing_mode(),   0);
    T_ASSERT_EQ_I(title_save_dialog_get_shake_counter(),  0);
    T_ASSERT_EQ_I(title_save_dialog_get_anim_counter(),   0);
    T_ASSERT((int)(title_save_dialog_get_shake_pos_x() * 1000.0f) == 0);
    T_ASSERT((int)(title_save_dialog_get_shake_pos_y() * 1000.0f) == 0);
    return 0;
}

/* ─── FUN_00434d6a — gate_tick ─────────────────────────────────────── */

int test_title_save_dialog_gate_tick_closed_returns_zero(void)
{
    /* counter < 1 → return 0, no state change. */
    clean_state();
    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), 0);
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 0);
    T_ASSERT_EQ_I(title_save_dialog_get_closing_mode(),   0);
    return 0;
}

int test_title_save_dialog_gate_tick_opening_ramps_to_8(void)
{
    /* counter==1, mode==0 → ramps 1→2→…→8 over 7 ticks, then clamps
     * at 8 (no press, so the mode-flip branch doesn't fire). */
    clean_state();
    title_save_dialog_set_active_counter(1);
    for (int expect = 2; expect <= 8; expect++) {
        T_ASSERT_EQ_I(title_save_dialog_gate_tick(), -1);
        T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), expect);
    }
    /* At counter==8 without a press, gate stays at 8 and returns -1. */
    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), -1);
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 8);
    return 0;
}

int test_title_save_dialog_gate_tick_press_at_8_flips_to_closing(void)
{
    /* counter==8, mode==0, pressed bit 0x10 (Z) → mode flips to 1. */
    clean_state();
    title_save_dialog_set_active_counter(8);
    g_sim_buttons[0].pressed = 0x10;

    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), -1);
    T_ASSERT_EQ_I(title_save_dialog_get_closing_mode(), 1);
    /* Counter should NOT have advanced (already at 8). */
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 8);
    return 0;
}

int test_title_save_dialog_gate_tick_press_x_at_8_also_flips(void)
{
    /* Bit 0x20 (X) is the other half of the (0x30) mask. */
    clean_state();
    title_save_dialog_set_active_counter(8);
    g_sim_buttons[0].pressed = 0x20;

    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), -1);
    T_ASSERT_EQ_I(title_save_dialog_get_closing_mode(), 1);
    return 0;
}

int test_title_save_dialog_gate_tick_press_below_8_does_not_flip(void)
{
    /* The Z/X→close-mode flip only happens at counter==8. */
    clean_state();
    title_save_dialog_set_active_counter(5);
    g_sim_buttons[0].pressed = 0x10;

    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), -1);
    T_ASSERT_EQ_I(title_save_dialog_get_closing_mode(), 0);
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 6);
    return 0;
}

int test_title_save_dialog_gate_tick_closing_ramps_down_and_signals(void)
{
    /* Mode==1 → counter--; when it hits 0, return 1. */
    clean_state();
    title_save_dialog_set_active_counter(3);
    title_save_dialog_set_closing_mode(1);

    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), -1);
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 2);
    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), -1);
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 1);
    /* Counter drops to 0 — return 1 signals "just closed". */
    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), 1);
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 0);
    /* Subsequent tick: counter==0 → return 0 (back to "closed"). */
    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), 0);
    return 0;
}

/* ─── FUN_004356cd — anim_tick ─────────────────────────────────────── */

int test_title_save_dialog_anim_tick_increments_anim_counter_when_closed(void)
{
    /* active_counter==0 → anim_counter increments every tick. */
    clean_state();
    for (int i = 1; i <= 10; i++) {
        title_save_dialog_anim_tick();
        T_ASSERT_EQ_I(title_save_dialog_get_anim_counter(), i);
    }
    return 0;
}

int test_title_save_dialog_anim_tick_skips_when_dialog_open(void)
{
    /* active_counter > 0 → anim_counter does NOT advance. */
    clean_state();
    title_save_dialog_set_active_counter(4);
    for (int i = 0; i < 10; i++) {
        title_save_dialog_anim_tick();
        T_ASSERT_EQ_I(title_save_dialog_get_anim_counter(), 0);
    }
    return 0;
}

int test_title_save_dialog_anim_tick_shake_interpolates(void)
{
    /* shake_counter > 0 → position += delta per tick, counter--. */
    clean_state();
    title_save_dialog_set_shake_counter(3);
    title_save_dialog_set_shake_delta(2.0f, -1.5f);

    title_save_dialog_anim_tick();
    T_ASSERT_EQ_I(title_save_dialog_get_shake_counter(), 2);
    T_ASSERT((int)(title_save_dialog_get_shake_pos_x() * 1000.0f) == 2000);
    T_ASSERT((int)(title_save_dialog_get_shake_pos_y() * 1000.0f) == -1500);

    title_save_dialog_anim_tick();
    T_ASSERT_EQ_I(title_save_dialog_get_shake_counter(), 1);
    T_ASSERT((int)(title_save_dialog_get_shake_pos_x() * 1000.0f) == 4000);

    title_save_dialog_anim_tick();
    T_ASSERT_EQ_I(title_save_dialog_get_shake_counter(), 0);

    /* One more tick — counter clamped at 0, no further interp. */
    float px_before = title_save_dialog_get_shake_pos_x();
    title_save_dialog_anim_tick();
    T_ASSERT((int)(title_save_dialog_get_shake_pos_x() * 1000.0f)
             == (int)(px_before * 1000.0f));
    return 0;
}

/* ─── stub renders — safety smoke ──────────────────────────────────── */

int test_title_save_dialog_stub_renders_dont_crash(void)
{
    /* Run all three render stubs in a few states. They have no
     * observable side effects in this chip; just verify the calls
     * are safe and don't trip ASan/UBSan. */
    clean_state();
    title_save_dialog_render();
    title_save_dialog_secondary_render();
    title_save_dialog_cursor_render();

    title_save_dialog_set_active_counter(4);
    title_save_dialog_render();
    title_save_dialog_secondary_render();
    title_save_dialog_cursor_render();
    return 0;
}
