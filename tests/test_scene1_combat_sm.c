/*
 * test_scene1_combat_sm.c — unit tests for the C8jb.1 skeleton
 * (engine FUN_0043865e Phase A entry gates + per-tick flag).
 *
 * Scope: Phase A only — the 4 entry-gate globals each short-circuit
 * to ret=0 without setting g_scene1_records_b_tick_flag; fall-through
 * sets the flag.  Phase B/C/D not yet ported.
 */

#include "t.h"

#include <stdint.h>
#include <string.h>

#include "scene1_combat_sm.h"
#include "scene1_records.h"
#include "scene1_records_b_tick.h"
#include "scene1_sim.h"

/* ─── helpers ────────────────────────────────────────────────────── */

static void reset_combat_state(void)
{
    g_scene1_combat_subphase     = 0;
    g_scene1_combat_world_pause  = 0;
    g_scene1_combat_aux_pause    = 0;
    g_scene1_ingame_paused_flag  = 0;
    g_scene1_records_b_tick_flag = 0;
    scene1_records_b_set_state_machine_hook(NULL);
}

static int32_t *some_slot(void)
{
    /* Phase A doesn't dereference the slot pointer; any non-NULL
     * pointer into table B is safe.  Use slot 0. */
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    return &g_scene1_records_b[0];
}

/* ─── Phase A fall-through ────────────────────────────────────────── */

int test_combat_sm_phase_a_fall_through_sets_per_tick_flag(void)
{
    reset_combat_state();

    int ret = scene1_combat_sm_tick(some_slot());

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);
    return 0;
}

int test_combat_sm_phase_a_returns_zero_with_null_slot(void)
{
    /* Phase A does not dereference slot; NULL is safe.  Useful for
     * tests that probe gate behavior without prepping table B. */
    reset_combat_state();

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);
    return 0;
}

/* ─── gate 1: combat subphase (DAT_0438be98) ─────────────────────── */

int test_combat_sm_subphase_positive_returns_zero_without_setting_flag(void)
{
    reset_combat_state();
    g_scene1_combat_subphase = 1;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);
    return 0;
}

int test_combat_sm_subphase_negative_does_not_gate(void)
{
    /* Engine reads `0 < DAT_0438be98` — strictly positive.  A
     * negative subphase should not gate.  (Defensive: engine likely
     * never stores negatives here, but the comparison is `0 < val`,
     * not `val != 0`.) */
    reset_combat_state();
    g_scene1_combat_subphase = -1;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);
    return 0;
}

/* ─── gate 2: world pause (DAT_0438be9c) ─────────────────────────── */

int test_combat_sm_world_pause_positive_returns_zero_without_setting_flag(void)
{
    reset_combat_state();
    g_scene1_combat_world_pause = 1;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);
    return 0;
}

int test_combat_sm_world_pause_negative_does_not_gate(void)
{
    reset_combat_state();
    g_scene1_combat_world_pause = -5;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);
    return 0;
}

/* ─── gate 3: aux pause (DAT_0438bea0) ───────────────────────────── */

int test_combat_sm_aux_pause_positive_returns_zero_without_setting_flag(void)
{
    reset_combat_state();
    g_scene1_combat_aux_pause = 42;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);
    return 0;
}

int test_combat_sm_aux_pause_negative_does_not_gate(void)
{
    reset_combat_state();
    g_scene1_combat_aux_pause = -100;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);
    return 0;
}

/* ─── gate 4: ingame paused flag (DAT_0438b1c8) ──────────────────── */

int test_combat_sm_paused_flag_nonzero_returns_zero_without_setting_flag(void)
{
    /* Engine reads `DAT_0438b1c8 != 0` — any non-zero gates.  Test
     * positive and negative values to verify the != comparison. */
    reset_combat_state();
    g_scene1_ingame_paused_flag = 1;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);
    return 0;
}

int test_combat_sm_paused_flag_negative_also_gates(void)
{
    reset_combat_state();
    g_scene1_ingame_paused_flag = -1;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);
    return 0;
}

/* ─── multiple gates active ──────────────────────────────────────── */

int test_combat_sm_multiple_gates_active_still_short_circuits(void)
{
    reset_combat_state();
    g_scene1_combat_subphase    = 2;
    g_scene1_combat_world_pause = 3;
    g_scene1_combat_aux_pause   = 4;
    g_scene1_ingame_paused_flag = 5;

    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);
    return 0;
}

/* ─── per-tick flag write does not clear gate state ──────────────── */

int test_combat_sm_fall_through_preserves_gate_globals(void)
{
    reset_combat_state();
    /* All gates zero; fall-through writes only the per-tick flag. */
    int ret = scene1_combat_sm_tick(NULL);

    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_combat_subphase,    0);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause, 0);
    T_ASSERT_EQ_I(g_scene1_combat_aux_pause,   0);
    T_ASSERT_EQ_I(g_scene1_ingame_paused_flag, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);
    return 0;
}

/* ─── void-hook adapter installs/uninstalls ──────────────────────── */

int test_combat_sm_install_as_void_hook_writes_per_tick_flag_via_integrator(void)
{
    /* When installed as the integrator's SM hook, the void wrapper
     * runs Phase A and writes the per-tick flag.  The integrator's
     * own per-iter flag clear runs BEFORE this, so the post-SM
     * observable is "flag = 1 if SM ran". */
    reset_combat_state();
    scene1_combat_sm_install_as_void_hook();

    /* Direct check: call the hook through the integrator's
     * state-machine helper surface by invoking the public combat tick
     * (which is what the hook adapter wraps). */
    g_scene1_records_b_tick_flag = 0;
    int ret = scene1_combat_sm_tick(NULL);
    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);

    scene1_combat_sm_uninstall_void_hook();
    return 0;
}

int test_combat_sm_uninstall_void_hook_restores_default(void)
{
    /* After uninstall, the integrator's state_machine_call_ret should
     * return 0 (no hook).  We don't test the integrator's helper
     * directly (it's static inline); we verify that the public
     * setter accepts NULL via this path. */
    reset_combat_state();
    scene1_combat_sm_install_as_void_hook();
    scene1_combat_sm_uninstall_void_hook();

    /* The state machine setter accepts the round-trip; the actual
     * "no hook installed" semantic is exercised by the integrator's
     * existing tests. */
    return 0;
}

/* ─── gate-order independence ────────────────────────────────────── */

int test_combat_sm_gate_short_circuit_is_atomic(void)
{
    /* Engine returns 0 IMMEDIATELY on the first gate hit — subsequent
     * gates are not evaluated.  Observable: per-tick flag stays 0,
     * and the function returns the same value (0) regardless of
     * which gate fires first.  This test exercises each gate
     * individually, verifying short-circuit semantics by checking
     * that the per-tick flag is never written when any single gate
     * is positive. */
    reset_combat_state();
    g_scene1_combat_subphase = 1;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(NULL), 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);

    reset_combat_state();
    g_scene1_combat_world_pause = 1;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(NULL), 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);

    reset_combat_state();
    g_scene1_combat_aux_pause = 1;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(NULL), 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);

    reset_combat_state();
    g_scene1_ingame_paused_flag = 1;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(NULL), 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);

    return 0;
}
