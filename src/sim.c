/*
 * sim.c — per-frame simulation halves.
 *
 * Engine source for sim_a: FUN_004536cb @ 0x4536cb (1745 bytes).
 *
 * What's actually ported in this commit:
 *   - The button-state ring at the top of FUN_004536cb (lines 42-70):
 *     current/prev/pressed/held-with-repeat masks for two players,
 *     plus the 16-short per-bit auto-repeat counters. Pure-C helper
 *     `sim_button_ring_update` exposes the math for tests.
 *   - The state-dispatch tail at LAB_00453cfb: `DAT_0438b8cc++`.
 *     The time-dilation float math and the `FUN_004526ab` post-frame
 *     helper are stubbed (no consumers yet in our skeleton).
 *   - Scene dispatch for state == 0 → `scene_title_sim_default`.
 *     Other scene states (1..16) and the mode-escape sub-blocks
 *     (DAT_06a49954/DAT_06a499c4/DAT_06a49964/DAT_06a49998) are
 *     omitted — they unlock as scenes get ported.
 *
 * Not yet ported:
 *   - FUN_0049966a (sim_b) entirely — the scheduler tolerates NULL.
 *   - Side-effect callouts inside the ring loop (DAT_0438b1e0 visit
 *     counter, FUN_0047c29d) — both empty at boot.
 *
 * See docs/findings/winmain-and-bootstrap.md §"Sim halves" (TBD) and
 * docs/decompiled/by-address/4536cb.c.
 */

#include "sim.h"

#include <string.h>

#include "input.h"        /* g_input_state for cur-buttons read */
#include "scene_title.h"  /* scene_title_sim_default + g_scene_title_* */

struct sim_player_buttons g_sim_buttons[SIM_NUM_PLAYERS];
uint32_t                  g_sim_frame_count;

/* ─── pure-C button-state ring (FUN_004536cb lines 42-70) ────────────── */

void sim_button_ring_update(uint16_t cur,
                            uint16_t *prev,
                            int16_t   repeat[16],
                            uint16_t *out_pressed,
                            uint16_t *out_held)
{
    const uint16_t p = *prev;

    /* `pressed` and the initial `held` are derived first, then the
     * per-bit repeat loop masks specific bits out of `held`. */
    uint16_t pressed = (uint16_t)(~p & cur);
    uint16_t held    = cur;

    for (int i = 0; i < 16; i++) {
        const int bit = 1 << i;
        const int changed = ((cur ^ p) & bit) != 0;

        if (changed) {
            /* Rising or falling edge: reset the repeat counter so the
             * next 12 frames of unchanged state are a "settle" window. */
            repeat[i] = 0xc;
        } else {
            /* Bit's value matches the previous frame. The engine first
             * clamps the counter into [1, 0xc] (after possibly being
             * disturbed by an outside writer), then on the "else"
             * branch decrements; while counter > 0 the bit is gated
             * out of `held`. */
            if (repeat[i] > 0xc) {
                repeat[i] = 0xc;
            }
            if (repeat[i] < 1) {
                repeat[i] = 4;
            } else {
                repeat[i] = (int16_t)(repeat[i] - 1);
                if (repeat[i] > 0) {
                    held = (uint16_t)(held & ~bit);
                }
            }
        }
    }

    *prev        = cur;
    *out_pressed = pressed;
    *out_held    = held;
}

/* ─── lifecycle + frame entry ────────────────────────────────────────── */

void sim_init(void)
{
    memset(g_sim_buttons, 0, sizeof g_sim_buttons);
    g_sim_frame_count = 0;
}

void sim_step_a(void)
{
    /* Run the button-state ring for every player. Player N's `cur`
     * comes from g_input_state[N].buttons (written by input_poll).
     *
     * Engine quirk: the ring loop in FUN_004536cb walks both blocks
     * unconditionally, even though FUN_0047b73c only writes player 0
     * (see input.{c,h} note on "DAT_073dddd0 / DAT_073dddfa"). We
     * mirror that — block 1 stays cur=0 and the math degenerates to
     * `pressed=0, held=0, repeat clears` automatically. */
    for (int i = 0; i < SIM_NUM_PLAYERS; i++) {
        const uint16_t cur = g_input_state[i].buttons;
        sim_button_ring_update(cur,
                               &g_sim_buttons[i].prev,
                               g_sim_buttons[i].repeat,
                               &g_sim_buttons[i].pressed,
                               &g_sim_buttons[i].held);
        g_sim_buttons[i].cur = cur;
    }

    /* Scene dispatch. Only state 0 (title) is wired up.
     *
     * The engine global `DAT_0438b1c0` is the active scene index. We
     * haven't introduced a global for it yet — the title is the only
     * scene that exists in this build, and main.c never transitions
     * out of it. When a second scene ports, the carrier global lands
     * in scene.h alongside the dispatcher case-table. */
    scene_title_sim_default();

    g_sim_frame_count++;
}
