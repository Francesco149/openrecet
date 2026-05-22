/*
 * test_sim.c — unit tests for sim.{c,h}.
 *
 * Covers the button-state ring (sim_button_ring_update) and the
 * sim_step_a frame dispatcher.
 */

#include "t.h"
#include "sim.h"
#include "input.h"
#include "nowloading.h"
#include "scene.h"
#include "scene_title.h"
#include "worker_load.h"

/* ─── button-state ring ──────────────────────────────────────────────── */

int test_sim_button_ring_first_press_sets_pressed(void)
{
    uint16_t prev = 0;
    int16_t  rep[16] = {0};
    uint16_t pressed = 0xffff, held = 0;

    /* Cold start: prev=0, cur=0x10 (A). All set bits are rising
     * edges → pressed has every set bit, held mirrors cur. */
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(pressed, 0x0010u);
    T_ASSERT_EQ_U(held,    0x0010u);
    T_ASSERT_EQ_U(prev,    0x0010u);   /* prev latched to cur */
    T_ASSERT_EQ_I(rep[4],  0x0c);      /* bit 4 (A) — latched repeat */
    /* Other repeat slots stay at the cold-start "1->4" reload because
     * those bits were 0 in both prev and cur — they hit the unchanged
     * branch with counter < 1 → 4. */
    T_ASSERT_EQ_I(rep[0],  4);
    T_ASSERT_EQ_I(rep[15], 4);
    return 0;
}

int test_sim_button_ring_held_clears_pressed_next_frame(void)
{
    uint16_t prev = 0;
    int16_t  rep[16] = {0};
    uint16_t pressed = 0, held = 0;

    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    /* Now hold same key one more frame: pressed should clear, held
     * stays set (counter goes 0xc → 0xb, but the bit gating only
     * removes bits when counter > 0 — wait, that's the auto-repeat
     * logic. After one frame with counter=12 the bit IS gated out. */
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(pressed, 0x0000u);     /* no longer rising */
    T_ASSERT_EQ_U(held,    0x0000u);     /* gated by repeat counter */
    T_ASSERT_EQ_I(rep[4],  0x0b);        /* counter decremented */
    return 0;
}

int test_sim_button_ring_repeat_pulses_after_settle(void)
{
    /* Drive the ring with a held A for many frames. Track which frames
     * surface the bit in `held`. The engine clamps the repeat counter
     * to 4 when it hits 0 (via a `< 1 → 4` reload that runs BEFORE the
     * decrement and is mutually exclusive with it). Net effect: the
     * bit re-fires on the frame that hits rep=0, AND on the next frame
     * (the reload-to-4 path skips the gate), then 3 frames of gating
     * before the next 2-frame fire window. */
    uint16_t prev = 0;
    int16_t  rep[16] = {0};
    uint16_t pressed = 0, held = 0;

    /* Frame 1: rising edge → fire. */
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(held, 0x10u);
    T_ASSERT_EQ_I(rep[4], 0x0c);

    /* Frames 2..12 (11 frames): counter decrements 12→11→...→2.
     * After each, the new value is > 0 so the bit is gated out. */
    for (int i = 0; i < 11; i++) {
        sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
        T_ASSERT_EQ_U(held, 0x00u);
    }
    T_ASSERT_EQ_I(rep[4], 1);

    /* Frame 13: counter goes 1→0 — the `else` decrement leaves rep=0
     * and the `0 > 0` check fails so the bit is *not* gated. Fire. */
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(held,   0x10u);
    T_ASSERT_EQ_I(rep[4], 0);

    /* Frame 14: counter is 0 entering → hits the `< 1` reload path
     * which sets rep=4 WITHOUT decrementing, so the gate branch never
     * runs. Bit fires again. (Engine quirk.) */
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(held,   0x10u);
    T_ASSERT_EQ_I(rep[4], 4);

    /* Frames 15..17: counter 4→3→2→1 — bit gated each frame. */
    for (int i = 0; i < 3; i++) {
        sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
        T_ASSERT_EQ_U(held, 0x00u);
    }
    T_ASSERT_EQ_I(rep[4], 1);

    /* Frame 18: counter 1→0 — fire. */
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(held, 0x10u);

    /* Frame 19: 0 → reload to 4 without gating — fire again. */
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(held,   0x10u);
    T_ASSERT_EQ_I(rep[4], 4);
    return 0;
}

int test_sim_button_ring_release_drops_held(void)
{
    uint16_t prev = 0;
    int16_t  rep[16] = {0};
    uint16_t pressed = 0, held = 0;

    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    /* Now release. cur=0, prev=0x10 → falling edge. */
    sim_button_ring_update(0x0000, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(pressed, 0x0000u);
    T_ASSERT_EQ_U(held,    0x0000u);   /* held tracks cur=0 */
    T_ASSERT_EQ_U(prev,    0x0000u);
    T_ASSERT_EQ_I(rep[4],  0x0c);      /* edge re-latched */
    return 0;
}

int test_sim_button_ring_multiple_bits_independent(void)
{
    uint16_t prev = 0;
    int16_t  rep[16] = {0};
    uint16_t pressed = 0, held = 0;

    /* Press UP (0x04) and A (0x10) on the same frame. */
    sim_button_ring_update(0x0014, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(pressed, 0x0014u);
    T_ASSERT_EQ_U(held,    0x0014u);
    T_ASSERT_EQ_I(rep[2],  0x0c);
    T_ASSERT_EQ_I(rep[4],  0x0c);

    /* Next frame: release UP, keep A. */
    sim_button_ring_update(0x0010, &prev, rep, &pressed, &held);
    T_ASSERT_EQ_U(pressed, 0x0000u);
    T_ASSERT_EQ_U(held,    0x0000u);     /* A is auto-gated; UP is cleared */
    T_ASSERT_EQ_I(rep[2],  0x0c);        /* UP edge re-latched */
    T_ASSERT_EQ_I(rep[4],  0x0b);        /* A repeat decremented */
    return 0;
}

/* ─── sim_step_a / sim_init ──────────────────────────────────────────── */

int test_sim_init_zeros_state(void)
{
    /* Pre-dirty everything. */
    g_sim_buttons[0].cur     = 0xffff;
    g_sim_buttons[0].prev    = 0xffff;
    g_sim_buttons[0].pressed = 0xffff;
    g_sim_buttons[0].held    = 0xffff;
    for (int i = 0; i < 16; i++) g_sim_buttons[0].repeat[i] = 9;
    g_sim_buttons[1].cur     = 0xffff;
    g_sim_frame_count        = 99;

    sim_init();

    T_ASSERT_EQ_U(g_sim_buttons[0].cur,     0u);
    T_ASSERT_EQ_U(g_sim_buttons[0].prev,    0u);
    T_ASSERT_EQ_U(g_sim_buttons[0].pressed, 0u);
    T_ASSERT_EQ_U(g_sim_buttons[0].held,    0u);
    T_ASSERT_EQ_U(g_sim_buttons[1].cur,     0u);
    T_ASSERT_EQ_I(g_sim_buttons[0].repeat[7], 0);
    T_ASSERT_EQ_U(g_sim_frame_count,        0u);
    return 0;
}

int test_sim_step_a_advances_frame_count(void)
{
    sim_init();
    /* sim_step_a now short-circuits on `worker_load_busy()` — reset
     * the worker module so any prior test that raised the gate
     * doesn't poison this assertion. */
    worker_load_reset();
    g_input_state[0].buttons = 0;
    g_input_state[1].buttons = 0;
    scene_title_menu_init_fresh(&g_scene_title_menu);
    scene_title_anim_init_fresh(&g_scene_title_anim);

    sim_step_a();
    T_ASSERT_EQ_U(g_sim_frame_count, 1u);
    sim_step_a();
    sim_step_a();
    T_ASSERT_EQ_U(g_sim_frame_count, 3u);
    return 0;
}

int test_sim_step_a_pipes_input_into_ring(void)
{
    sim_init();
    worker_load_reset();
    scene_title_menu_init_fresh(&g_scene_title_menu);
    scene_title_anim_init_fresh(&g_scene_title_anim);

    /* Player 0 holds A this frame. */
    g_input_state[0].buttons = 0x0010;
    g_input_state[1].buttons = 0;

    sim_step_a();
    T_ASSERT_EQ_U(g_sim_buttons[0].cur,     0x0010u);
    T_ASSERT_EQ_U(g_sim_buttons[0].prev,    0x0010u);  /* latched */
    T_ASSERT_EQ_U(g_sim_buttons[0].pressed, 0x0010u);  /* rising */
    T_ASSERT_EQ_U(g_sim_buttons[0].held,    0x0010u);
    return 0;
}

/* ─── FUN_004532df pump ──────────────────────────────────────────────── */

int test_sim_loading_pump_pure_cold_start_is_noop(void)
{
    /* Engine cold-start: all counters BSS-zero, threshold BSS-zero.
     * Each counter advance is gated on `> 0` so nothing moves. */
    int32_t c990 = 0, c994 = 0, c998 = 0;
    sim_loading_pump_pure(&c990, &c994, &c998, /*mode=*/0, /*threshold=*/0);
    T_ASSERT_EQ_I(c990, 0);
    T_ASSERT_EQ_I(c994, 0);
    T_ASSERT_EQ_I(c998, 0);
    sim_loading_pump_pure(&c990, &c994, &c998, /*mode=*/1, /*threshold=*/0xff);
    T_ASSERT_EQ_I(c990, 0);
    T_ASSERT_EQ_I(c994, 0);
    T_ASSERT_EQ_I(c998, 0);
    return 0;
}

int test_sim_loading_pump_pure_990_cycles_to_1f_then_wraps(void)
{
    /* Counter 990: gate on > 0 → start at 1 → advance to 0x1f, then
     * the post-increment-equals-0x20 check wraps to 0. Engine
     * L50124-50126. */
    int32_t c990 = 1, c994 = 0, c998 = 0;
    for (int expected = 2; expected <= 0x1f; expected++) {
        sim_loading_pump_pure(&c990, &c994, &c998, 0, 0);
        T_ASSERT_EQ_I(c990, expected);
    }
    /* Next pump: 0x1f + 1 == 0x20 → wrap to 0. */
    sim_loading_pump_pure(&c990, &c994, &c998, 0, 0);
    T_ASSERT_EQ_I(c990, 0);
    /* Once at 0 the gate freezes — sits dormant. */
    sim_loading_pump_pure(&c990, &c994, &c998, 0, 0);
    T_ASSERT_EQ_I(c990, 0);
    return 0;
}

int test_sim_loading_pump_pure_994_wraps_at_threshold(void)
{
    /* Counter 994: cycles 1..(threshold-1), wraps at threshold. Engine
     * L50127-50129 uses `threshold <= value` so the wrap happens the
     * tick `value` reaches `threshold`. */
    int32_t c990 = 0, c994 = 1, c998 = 0;
    /* threshold=4 → counter takes values 1,2,3, then 4 wraps to 0. */
    sim_loading_pump_pure(&c990, &c994, &c998, 0, /*threshold=*/4);
    T_ASSERT_EQ_I(c994, 2);
    sim_loading_pump_pure(&c990, &c994, &c998, 0, 4);
    T_ASSERT_EQ_I(c994, 3);
    sim_loading_pump_pure(&c990, &c994, &c998, 0, 4);
    T_ASSERT_EQ_I(c994, 0);                  /* 3+1==4 → threshold hit → wrap */
    sim_loading_pump_pure(&c990, &c994, &c998, 0, 4);
    T_ASSERT_EQ_I(c994, 0);                  /* dormant once 0 */
    return 0;
}

int test_sim_loading_pump_pure_994_threshold_zero_wraps_immediately(void)
{
    /* Engine quirk: if FUN_004532bc never latched a threshold, the
     * cold-start 0 is compared `0 <= 2` → true → wrap on the very
     * first tick after a starter writes 994=1. Dormant in vendor data
     * because all callers latch the threshold before the counter
     * advances, but the math is what it is. */
    int32_t c990 = 0, c994 = 1, c998 = 0;
    sim_loading_pump_pure(&c990, &c994, &c998, 0, /*threshold=*/0);
    T_ASSERT_EQ_I(c994, 0);
    return 0;
}

int test_sim_loading_pump_pure_998_mode0_cycles_to_13_then_wraps(void)
{
    /* Counter 998 mode==0: cyclic 1..0x13, wraps to 0 at 0x14. */
    int32_t c990 = 0, c994 = 0, c998 = 1;
    for (int expected = 2; expected <= 0x13; expected++) {
        sim_loading_pump_pure(&c990, &c994, &c998, /*mode=*/0, 0);
        T_ASSERT_EQ_I(c998, expected);
    }
    sim_loading_pump_pure(&c990, &c994, &c998, 0, 0);
    T_ASSERT_EQ_I(c998, 0);
    return 0;
}

int test_sim_loading_pump_pure_998_mode1_clamps_at_0xc(void)
{
    /* Counter 998 mode!=0: monotone with ceiling 0xc. Engine
     * L50136-50140: `if (0xc < v) v = 0xc` — runs ON the increment
     * tick. So 1 → 2 → ... → 0xc → 0xc → 0xc forever. */
    int32_t c990 = 0, c994 = 0, c998 = 1;
    for (int expected = 2; expected <= 0xc; expected++) {
        sim_loading_pump_pure(&c990, &c994, &c998, /*mode=*/1, 0);
        T_ASSERT_EQ_I(c998, expected);
    }
    /* At 0xc → 0xd → clamped back to 0xc. */
    sim_loading_pump_pure(&c990, &c994, &c998, 1, 0);
    T_ASSERT_EQ_I(c998, 0xc);
    sim_loading_pump_pure(&c990, &c994, &c998, 1, 0);
    T_ASSERT_EQ_I(c998, 0xc);
    return 0;
}

int test_sim_loading_pump_module_globals(void)
{
    /* The void-arg pump drives the module-level counters via the
     * setters. Smoke-check that 990 advances + threshold latches. */
    sim_init();
    sim_set_counter_990(1);
    sim_set_counter_994(1, /*threshold94=*/3);
    sim_loading_pump();
    T_ASSERT_EQ_I(sim_get_counter_990(), 2);
    T_ASSERT_EQ_I(sim_get_counter_994(), 2);
    sim_loading_pump();
    T_ASSERT_EQ_I(sim_get_counter_990(), 3);
    T_ASSERT_EQ_I(sim_get_counter_994(), 0);   /* hit threshold → wrap */
    return 0;
}

int test_sim_init_zeros_counter_state(void)
{
    /* sim_init clears every pump counter + the mode flag + the
     * latched threshold. */
    sim_set_counter_990(7);
    sim_set_counter_994(11, 42);
    sim_set_counter_998(5);
    sim_set_counter_99c(9);
    sim_set_mode_9a0(1);

    sim_init();

    T_ASSERT_EQ_I(sim_get_counter_990(), 0);
    T_ASSERT_EQ_I(sim_get_counter_994(), 0);
    T_ASSERT_EQ_I(sim_get_counter_998(), 0);
    T_ASSERT_EQ_I(sim_get_counter_99c(), 0);
    T_ASSERT_EQ_I(sim_get_mode_9a0(),    0);
    T_ASSERT_EQ_I(sim_get_threshold94(), 0);
    return 0;
}

/* ─── sim_step_a busy-guard wiring ──────────────────────────────────── */

int test_sim_step_a_busy_freezes_input_and_dispatch(void)
{
    /* When the primary worker is busy, sim_step_a:
     *   - pumps the scene-effect counters,
     *   - does NOT touch the button ring or advance frame count,
     *   - does NOT clear the nowloading gate. */
    sim_init();
    worker_load_reset();
    scene_state_set_title();
    scene_title_menu_init_fresh(&g_scene_title_menu);
    scene_title_anim_init_fresh(&g_scene_title_anim);

    /* Drive a button to make the ring observable. */
    g_input_state[0].buttons = 0x0010;
    g_input_state[1].buttons = 0;

    /* Raise busy + nowloading (mirrors worker_load_spawn's gates-only
     * non-Win32 path). */
    worker_load_begin();
    T_ASSERT_EQ_I(worker_load_busy(),    1);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);

    /* Pre-load counter 990 so we can observe the pump fired. */
    sim_set_counter_990(5);

    sim_step_a();

    /* Pump advanced 990. */
    T_ASSERT_EQ_I(sim_get_counter_990(), 6);
    /* Ring frozen — button never made it into g_sim_buttons. */
    T_ASSERT_EQ_U(g_sim_buttons[0].cur,     0u);
    T_ASSERT_EQ_U(g_sim_buttons[0].pressed, 0u);
    /* Frame count NOT advanced. */
    T_ASSERT_EQ_U(g_sim_frame_count, 0u);
    /* Nowloading gate stays raised. */
    T_ASSERT_EQ_I(nowloading_is_active(), 1);

    worker_load_reset();
    nowloading_reset();
    return 0;
}

int test_sim_step_a_idle_clears_nowloading_gate(void)
{
    /* When the worker is NOT busy, sim_step_a's per-tick gate clear
     * drops the nowloading overlay — even if a prior worker spawn
     * raised it. This is what makes "Now Loading…" disappear the
     * tick after the load thread finishes. */
    sim_init();
    worker_load_reset();
    scene_state_set_title();
    scene_title_menu_init_fresh(&g_scene_title_menu);
    scene_title_anim_init_fresh(&g_scene_title_anim);

    /* Raise the gate as if a worker had been spawned, then immediately
     * drop its busy flag (the spawn path the engine takes on Win32
     * after the thread proc cleans up). */
    worker_load_begin();
    worker_load_end();
    T_ASSERT_EQ_I(worker_load_busy(),     0);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);

    sim_step_a();

    /* Gate cleared on this tick. */
    T_ASSERT_EQ_I(nowloading_is_active(), 0);
    /* Sim ran normally — frame count advanced. */
    T_ASSERT_EQ_U(g_sim_frame_count, 1u);

    worker_load_reset();
    nowloading_reset();
    return 0;
}

