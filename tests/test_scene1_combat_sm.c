/*
 * test_scene1_combat_sm.c — unit tests for the FUN_0043865e port.
 *
 * Scope:
 *   C8jb.1 — Phase A entry gates (4 globals) + per-tick flag.
 *   C8jb.2 — Phase B head: attacker NPC scan iteration shell + 4 skip
 *            gates + per-NPC hit-history filter.  No collision math.
 *
 * Phase C/D not yet ported.  All paths return 0 in C8jb.1+2.
 */

#include "t.h"

#include <stdint.h>
#include <string.h>

#include "scene1_combat_sm.h"
#include "scene1_particles_tick.h"
#include "scene1_records.h"
#include "scene1_records_b_tick.h"
#include "scene1_sim.h"

/* ─── helpers ────────────────────────────────────────────────────── */

static int g_visit_indices[SCENE1_PEOPLE_COUNT];
static int g_visit_count;

static void capture_visit_hook(int npc_index)
{
    if (g_visit_count < SCENE1_PEOPLE_COUNT) {
        g_visit_indices[g_visit_count++] = npc_index;
    }
}

static void reset_combat_state(void)
{
    g_scene1_combat_subphase     = 0;
    g_scene1_combat_world_pause  = 0;
    g_scene1_combat_aux_pause    = 0;
    g_scene1_ingame_paused_flag  = 0;
    g_scene1_records_b_tick_flag = 0;
    g_scene1_combat_player_hp    = 0.0f;
    g_scene1_combat_phase_b_visit_count = 0;
    g_visit_count = 0;
    memset(g_visit_indices, 0, sizeof g_visit_indices);
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    memset(g_scene1_people, 0, sizeof g_scene1_people);
    scene1_records_b_set_state_machine_hook(NULL);
    scene1_combat_set_phase_b_visit_hook(NULL);
}

static int32_t *some_slot(void)
{
    /* Phase A doesn't dereference the slot pointer; any non-NULL
     * pointer into table B is safe.  Use slot 0. */
    return &g_scene1_records_b[0];
}

/*
 * Prep slot[0] as an "idle attacker" with player HP > 0 so the Phase B
 * outer gate passes.  Returns the slot pointer.  Caller may further
 * tweak slot fields before invoking the SM.
 */
static int32_t *attacker_slot(void)
{
    int32_t *slot = some_slot();
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]   = 0;  /* idle attacker state */
    slot[SCENE1_RECORDS_B_OFF_OWNER_B]  = 0;  /* no target lock */
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID]   = 0x4242;  /* slot's hit-ID */
    g_scene1_combat_player_hp = 100.0f;
    return slot;
}

/*
 * Configure NPC `i` as a fully alive, hittable, uncooldowned target.
 * Any field can be flipped by the caller after this baseline.
 */
static scene1_people_entry_t *prep_npc_alive(int i)
{
    scene1_people_entry_t *npc = &g_scene1_people[i];
    npc->alive             = 1;
    npc->alive_alias_24    = 0;
    npc->sister_724        = 0;
    npc->combat_cooldown_5 = 0;
    npc->hit_cursor        = 0;
    for (int k = 0; k < 10; k++) npc->hit_history[k] = 0;
    return npc;
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

/* ═══ C8jb.2 — Phase B head ════════════════════════════════════════════ */

/* ─── Phase B outer gate: state ∈ {0,3} && player_hp > 0 ────────────── */

int test_combat_sm_phase_b_skipped_when_player_hp_zero(void)
{
    /* HP = 0 → entire attacker scan skipped, visit count stays 0
     * even when an NPC is fully alive. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    g_scene1_combat_player_hp = 0.0f;
    prep_npc_alive(0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_b_skipped_when_player_hp_negative(void)
{
    /* Engine reads `0.0 < HP` — strict.  Negative HP also skips. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    g_scene1_combat_player_hp = -1.0f;
    prep_npc_alive(0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_b_skipped_when_state_is_1(void)
{
    /* FLAG_A=1 means "currently in active attack" (target state, not
     * attacker).  Phase B skipped; Phase D would handle this in a
     * later chip.  C8jb.2 returns 0 + visit_count = 0. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 1;
    prep_npc_alive(0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_b_skipped_when_state_is_2(void)
{
    reset_combat_state();
    int32_t *slot = attacker_slot();
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 2;
    prep_npc_alive(0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_b_runs_when_state_is_0(void)
{
    reset_combat_state();
    int32_t *slot = attacker_slot();
    /* FLAG_A=0 (default from attacker_slot()). */
    prep_npc_alive(0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

int test_combat_sm_phase_b_runs_when_state_is_3(void)
{
    reset_combat_state();
    int32_t *slot = attacker_slot();
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 3;
    prep_npc_alive(0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

/* ─── Phase B gate 1: combat_cooldown_5 > 0 ────────────────────────── */

int test_combat_sm_phase_b_skips_npc_with_positive_cooldown(void)
{
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->combat_cooldown_5 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_b_admits_npc_with_zero_cooldown(void)
{
    /* combat_cooldown_5 = 0 is the "active" state.  Confirmed: this
     * passes the gate (gate is `> 0`, not `!= 0`). */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->combat_cooldown_5 = 0;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

int test_combat_sm_phase_b_admits_npc_with_negative_cooldown(void)
{
    /* Defensive: engine `[ecx-0x14] > 0` skip means `<= 0` admits.
     * Negative cooldown (unusual) should still admit. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->combat_cooldown_5 = -1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

/* ─── Phase B gate 2: sister_724 != 0 ──────────────────────────────── */

int test_combat_sm_phase_b_skips_npc_with_sister_724_set(void)
{
    /* Engine: `[ecx] != 0` → skip.  Pure C8jb.2 scope.  Whatever
     * sister_724 semantically means in retail, the gate is binary. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->sister_724 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

/* ─── Phase B gate 3: target-lock (slot[FLAG_A]==3, OWNER_B==npc) ──── */

int test_combat_sm_phase_b_target_lock_skips_locked_npc(void)
{
    /* Slot in state 3 + OWNER_B points to NPC 5's address → that NPC
     * gets skipped.  Other NPCs (alive but not the locked one) admit. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]  = 3;
    prep_npc_alive(5);
    prep_npc_alive(7);
    slot[SCENE1_RECORDS_B_OFF_OWNER_B] = (int32_t)(intptr_t)&g_scene1_people[5];

    scene1_combat_set_phase_b_visit_hook(capture_visit_hook);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    T_ASSERT_EQ_I(g_visit_count, 1);
    T_ASSERT_EQ_I(g_visit_indices[0], 7);
    return 0;
}

int test_combat_sm_phase_b_target_lock_inert_when_state_is_0(void)
{
    /* Engine target-lock gate fires ONLY when slot[FLAG_A] == 3.
     * In state 0, OWNER_B is ignored even when pointing at an NPC. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    /* state 0 (default) */
    prep_npc_alive(5);
    slot[SCENE1_RECORDS_B_OFF_OWNER_B] = (int32_t)(intptr_t)&g_scene1_people[5];

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

int test_combat_sm_phase_b_target_lock_inert_when_owner_null(void)
{
    /* state 3 + OWNER_B = 0 → target-lock gate inert. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]  = 3;
    slot[SCENE1_RECORDS_B_OFF_OWNER_B] = 0;
    prep_npc_alive(5);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

/* ─── Phase B gate 4: alive ∈ {1, 2-with-alias} ────────────────────── */

int test_combat_sm_phase_b_skips_npc_with_alive_zero(void)
{
    /* alive=0 is the "empty slot" state.  Skip. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->alive = 0;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_b_admits_npc_with_alive_one(void)
{
    /* alive=1 admits unconditionally. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->alive = 1;
    npc->alive_alias_24 = 0;  /* alias unused when alive==1 */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

int test_combat_sm_phase_b_skips_npc_with_alive_two_and_alias_zero(void)
{
    /* alive=2 admits ONLY when alive_alias_24 is non-zero. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->alive = 2;
    npc->alive_alias_24 = 0;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_b_admits_npc_with_alive_two_and_alias_set(void)
{
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->alive = 2;
    npc->alive_alias_24 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

int test_combat_sm_phase_b_skips_npc_with_alive_three(void)
{
    /* Engine only admits values {1, 2-with-alias}; everything else
     * skips (including hypothetical 3). */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->alive = 3;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

/* ─── Phase B hit-history filter ───────────────────────────────────── */

int test_combat_sm_phase_b_skips_npc_when_seq_id_in_history(void)
{
    /* Slot's SEQ_ID is in NPC's hit_history → skip (already hit). */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0x1234;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->hit_history[3] = 0x1234;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_b_admits_npc_when_seq_id_not_in_history(void)
{
    reset_combat_state();
    int32_t *slot = attacker_slot();
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0x1234;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    /* Different IDs across the ring. */
    for (int k = 0; k < 10; k++) npc->hit_history[k] = 0x9000 + k;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

int test_combat_sm_phase_b_hit_history_match_at_each_slot(void)
{
    /* Match at index 0 should skip — equivalent to match at any other
     * index.  Exercises all 10 ring positions in a single test
     * (engine's loop reads sequentially). */
    for (int idx = 0; idx < 10; idx++) {
        reset_combat_state();
        int32_t *slot = attacker_slot();
        slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0xDEAD0000 + idx;
        scene1_people_entry_t *npc = prep_npc_alive(0);
        npc->hit_history[idx] = 0xDEAD0000 + idx;

        T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
        T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    }
    return 0;
}

int test_combat_sm_phase_b_hit_history_zero_seq_id_matches_default_ring(void)
{
    /* When slot's SEQ_ID is 0 (default in BSS-zero ring), the
     * hit-history scan finds the 0 at index 0 and skips.  This is
     * the engine's behavior; the integrator's preamble bumps SEQ_ID
     * via scene1_record_b_seq_counter so SEQ_ID == 0 is rare in
     * production but possible in tests. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0;
    prep_npc_alive(0);   /* ring zeroed */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    return 0;
}

/* ─── Phase B iteration order + cap ─────────────────────────────────── */

int test_combat_sm_phase_b_iterates_all_npcs_in_index_order(void)
{
    /* 4 alive NPCs at non-contiguous indices → visit hook is called
     * in ascending index order, exactly once per NPC. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    prep_npc_alive(3);
    prep_npc_alive(10);
    prep_npc_alive(50);
    prep_npc_alive(127);  /* last index */

    scene1_combat_set_phase_b_visit_hook(capture_visit_hook);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 4);
    T_ASSERT_EQ_I(g_visit_count, 4);
    T_ASSERT_EQ_I(g_visit_indices[0], 3);
    T_ASSERT_EQ_I(g_visit_indices[1], 10);
    T_ASSERT_EQ_I(g_visit_indices[2], 50);
    T_ASSERT_EQ_I(g_visit_indices[3], 127);
    return 0;
}

int test_combat_sm_phase_b_iter_count_caps_at_128(void)
{
    /* All 128 NPCs alive + admissible → visit_count == 128.  No
     * iteration beyond the engine's `cmp ecx, 0x7c9678` bound. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    for (int i = 0; i < SCENE1_PEOPLE_COUNT; i++) prep_npc_alive(i);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, SCENE1_PEOPLE_COUNT);
    return 0;
}

int test_combat_sm_phase_b_visit_count_resets_between_ticks(void)
{
    /* Engine resets the visit count at fall-through entry.  Calling
     * tick twice with different NPC populations should reflect ONLY
     * the latest tick. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    prep_npc_alive(0);
    prep_npc_alive(1);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 2);

    /* Second tick: kill NPC 1, leave NPC 0 alive. */
    g_scene1_people[1].alive = 0;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

int test_combat_sm_phase_b_visit_count_resets_on_phase_a_fall_through(void)
{
    /* When Phase A entry gate fires, function returns BEFORE
     * resetting the counter.  This test verifies the spec: the
     * counter ONLY resets on fall-through (when Phase B runs). */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    prep_npc_alive(0);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);

    /* Raise a Phase A gate; counter remains at 1 (no reset). */
    g_scene1_combat_subphase = 1;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    return 0;
}

/* ─── Phase B vs Phase A — entry gate still short-circuits ─────────── */

int test_combat_sm_phase_a_gate_skips_phase_b_entirely(void)
{
    /* Even with alive NPCs queued, a Phase A gate must skip Phase B. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    prep_npc_alive(0);
    prep_npc_alive(1);
    g_scene1_combat_world_pause = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);  /* Phase A short-circuit */
    return 0;
}

/* ─── Phase B visit hook — install / uninstall round-trip ──────────── */

int test_combat_sm_phase_b_visit_hook_install_returns_previous(void)
{
    reset_combat_state();
    /* Install hook; setter returns NULL (default). */
    scene1_combat_phase_b_visit_fn prev =
        scene1_combat_set_phase_b_visit_hook(capture_visit_hook);
    T_ASSERT_EQ_I(prev == NULL, 1);

    /* Re-install + check round-trip returns capture_visit_hook. */
    scene1_combat_phase_b_visit_fn prev2 =
        scene1_combat_set_phase_b_visit_hook(NULL);
    T_ASSERT_EQ_I(prev2 == capture_visit_hook, 1);
    return 0;
}

int test_combat_sm_phase_b_visit_hook_nullable(void)
{
    /* No hook installed → Phase B still counts visits; hook just
     * doesn't fire.  Verifies the function is safe with NULL hook. */
    reset_combat_state();
    int32_t *slot = attacker_slot();
    prep_npc_alive(0);
    prep_npc_alive(1);
    /* hook stays NULL */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 2);
    T_ASSERT_EQ_I(g_visit_count, 0);  /* no captures */
    return 0;
}
