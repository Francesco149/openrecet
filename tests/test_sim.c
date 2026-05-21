/*
 * test_sim.c — unit tests for sim.{c,h}.
 *
 * Covers the button-state ring (sim_button_ring_update) and the
 * sim_step_a frame dispatcher.
 */

#include "t.h"
#include "sim.h"
#include "input.h"
#include "scene_title.h"

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
