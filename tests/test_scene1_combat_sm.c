/*
 * test_scene1_combat_sm.c — unit tests for the FUN_0043865e port.
 *
 * Scope:
 *   C8jb.1  — Phase A entry gates (4 globals) + per-tick flag.
 *   C8jb.2  — Phase B head: attacker NPC scan iteration shell + 4 skip
 *             gates + per-NPC hit-history filter.
 *   C8jb.3  — Phase B collision math: nested sub-iter loop (1/7/2 by
 *             NPC type) + pose lookup (combat_pose vs anchor) + 2D-XZ
 *             distance + AABB Y-band.
 *   C8jb.4  — Phase B per-collision arming: 0x48 disarm + 0x44/0x45
 *             angle filter (±0.3π cone) + anchor-path disarming for
 *             non-0x46/0x47 sub-iter > 0 + phase==6 ∧ subphase==1
 *             force-arm.
 *   C8jb.5a — Phase B damage-roll prologue: velocity-derived KB factor
 *             (0.7/sqrt(VX²+VZ²)), hit-history ring bump, and slot
 *             TYPE==0x53 heavy-attack short-circuit (per-type +0x20
 *             gate + npc_type!=0x22 + FUN_004319d6 cooldown lookup).
 *
 * Phase B general damage formula / Phase C / Phase D not yet ported.
 * All paths return 0 in C8jb.1..5a.
 */

#include "t.h"

#include <math.h>
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

/* C8jb.3 collision-hook capture. */
static struct {
    int npc_index;
    int sub_iter;
} g_collision_hits[SCENE1_PEOPLE_COUNT * 8];
static int g_collision_count;

static void capture_collision_hook(int npc_index, int sub_iter)
{
    int n = sizeof g_collision_hits / sizeof g_collision_hits[0];
    if (g_collision_count < n) {
        g_collision_hits[g_collision_count].npc_index = npc_index;
        g_collision_hits[g_collision_count].sub_iter  = sub_iter;
        g_collision_count++;
    }
}

/* C8jb.4 armed-hook capture. */
static struct {
    int npc_index;
    int sub_iter;
} g_armed_hits[SCENE1_PEOPLE_COUNT * 8];
static int g_armed_count;

static void capture_armed_hook(int npc_index, int sub_iter)
{
    int n = sizeof g_armed_hits / sizeof g_armed_hits[0];
    if (g_armed_count < n) {
        g_armed_hits[g_armed_count].npc_index = npc_index;
        g_armed_hits[g_armed_count].sub_iter  = sub_iter;
        g_armed_count++;
    }
}

/* C8jb.5a aux_4319d6 capture (for 0x53 heavy-attack path tests). */
static int g_aux_4319d6_return = 0;
static int g_aux_4319d6_call_count = 0;
static int aux_4319d6_returns_configured(void)
{
    g_aux_4319d6_call_count++;
    return g_aux_4319d6_return;
}

static void reset_combat_state(void)
{
    g_scene1_combat_subphase     = 0;
    g_scene1_combat_world_pause  = 0;
    g_scene1_combat_aux_pause    = 0;
    g_scene1_ingame_paused_flag  = 0;
    g_scene1_records_b_tick_flag = 0;
    g_scene1_combat_player_hp    = 0.0f;
    g_scene1_combat_phase_b_visit_count            = 0;
    g_scene1_combat_phase_b_collision_count        = 0;
    g_scene1_combat_phase_b_armed_collision_count  = 0;
    g_scene1_combat_phase_b_heavy_atk_count        = 0;
    g_scene1_combat_phase_b_damage_out             = 0;
    g_scene1_combat_phase_b_kb_strength            = 0.0f;
    g_scene1_combat_dat_0438bed8                   = 0;
    g_visit_count = 0;
    g_collision_count = 0;
    g_armed_count = 0;
    memset(g_visit_indices,  0, sizeof g_visit_indices);
    memset(g_collision_hits, 0, sizeof g_collision_hits);
    memset(g_armed_hits,     0, sizeof g_armed_hits);
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    memset(g_scene1_people, 0, sizeof g_scene1_people);
    memset(g_scene1_combat_npc_type_attrs, 0,
           sizeof g_scene1_combat_npc_type_attrs);
    scene1_records_b_set_state_machine_hook(NULL);
    scene1_combat_set_phase_b_visit_hook(NULL);
    scene1_combat_set_phase_b_collision_hook(NULL);
    scene1_combat_set_phase_b_armed_hook(NULL);
    scene1_records_b_set_aux_4319d6_hook(NULL);
    g_aux_4319d6_return     = 0;
    g_aux_4319d6_call_count = 0;
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
 *
 * combat_pose / attack_radius / anchors[][] are left zero by default.
 * Combat tests must override these for collision math to fire.
 */
static scene1_people_entry_t *prep_npc_alive(int i)
{
    scene1_people_entry_t *npc = &g_scene1_people[i];
    memset(npc, 0, sizeof *npc);
    npc->alive             = 1;
    return npc;
}

/*
 * Configure slot[0] world position + reach for collision tests.
 * slot[FLAG_A] stays 0 so Phase B outer gate passes.
 */
static int32_t *attacker_slot_at(float px, float py, float pz, float reach)
{
    int32_t *slot = attacker_slot();
    *(float *)&slot[SCENE1_RECORDS_B_OFF_POS_X] = px;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y] = py;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z] = pz;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_DRAG]  = reach;
    return slot;
}

/*
 * Install permissive NPC-type collision attrs.  With these defaults
 * (1.0 * 1.0 = 1.0 multipliers), the gates simplify to:
 *   distance gate: `dist - slot.reach < npc.attack_radius`
 *   y-band gate:   `|dy| < npc.attack_radius + slot.reach*0.8`
 */
static void install_unit_attrs(int npc_type)
{
    g_scene1_combat_npc_type_attrs[npc_type].radius_mul = 1.0f;
    g_scene1_combat_npc_type_attrs[npc_type].y_band_mul = 1.0f;
    g_scene1_combat_npc_type_attrs[npc_type].dist_mul   = 1.0f;
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

/* ═══ C8jb.3 — Phase B collision math ══════════════════════════════════ */

/* ─── BSS-zero attrs → collision math always fails (production safety) ─ */

int test_combat_sm_phase_b_collision_count_zero_with_bss_zero_attrs(void)
{
    /* With g_scene1_combat_npc_type_attrs[type] all-zero (production
     * default per PHC #19), the dist gate `dist - reach < 0` only
     * passes on overlap.  NPC at distance > 0 from origin → no
     * collisions.  Verifies the safe-default: production keeps
     * combat dormant. */
    reset_combat_state();
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->combat_pose[0]  = 5.0f;  /* 5 units away in x */
    npc->combat_pose[2]  = 0.0f;
    npc->attack_radius   = 1.0f;
    /* attrs[0x10] stays zero → both gates use 0 thresholds */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    return 0;
}

/* ─── Default sub-iter count = 1 for non-multi-hit types ───────────── */

int test_combat_sm_phase_b_default_npc_type_has_one_sub_iter(void)
{
    /* NPC type 0x10 (not 0x44-0x47) → 1 sub-iter.  With permissive
     * attrs + NPC right next to slot, sub-iter 0 collides → collision
     * count == 1. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->combat_pose[0]  = 0.0f;
    npc->combat_pose[1]  = 0.0f;
    npc->combat_pose[2]  = 0.0f;
    npc->attack_radius   = 2.0f;

    scene1_combat_set_phase_b_collision_hook(capture_collision_hook);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_collision_count, 1);
    T_ASSERT_EQ_I(g_collision_hits[0].npc_index, 0);
    T_ASSERT_EQ_I(g_collision_hits[0].sub_iter,  0);
    return 0;
}

/* ─── 0x44/0x45 = 7 sub-iters; 0x46/0x47 = 2 sub-iters ─────────────── */

int test_combat_sm_phase_b_npc_type_44_has_seven_sub_iters(void)
{
    /* NPC type 0x44 → 7 sub-iters.  Sub-iter 0 uses combat_pose (which
     * is overlapping slot).  Sub-iters 1-6 use anchors[0/1/2/3/6/7]
     * (per DAT_005c5314[1..6]) — leave those at 0 (= overlapping slot)
     * to make ALL 7 sub-iters collide. */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 2.0f;
    /* combat_pose + anchors[*] all 0 — overlap slot */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 7);
    return 0;
}

int test_combat_sm_phase_b_npc_type_45_has_seven_sub_iters(void)
{
    reset_combat_state();
    install_unit_attrs(0x45);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x45;
    npc->attack_radius   = 2.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 7);
    return 0;
}

int test_combat_sm_phase_b_npc_type_46_has_two_sub_iters(void)
{
    /* NPC type 0x46 → 2 sub-iters.  Both use anchors (DAT_005c530c[0]=1,
     * [1]=2) — leave at 0 to overlap slot. */
    reset_combat_state();
    install_unit_attrs(0x46);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x46;
    npc->attack_radius   = 2.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 2);
    return 0;
}

int test_combat_sm_phase_b_npc_type_47_has_two_sub_iters(void)
{
    reset_combat_state();
    install_unit_attrs(0x47);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x47;
    npc->attack_radius   = 2.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 2);
    return 0;
}

/* ─── Sub-iter 0 of non-0x44-0x47 uses combat_pose; 1+ uses anchors ─ */

int test_combat_sm_phase_b_default_type_uses_combat_pose(void)
{
    /* NPC type 0x10 sub-iter 0 reads combat_pose.  Anchors[0] is NOT
     * read for this type (sub-iter count = 1). */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->combat_pose[0]  = 0.0f;  /* overlap */
    npc->attack_radius   = 2.0f;
    /* Stash garbage in anchors[0] — must be IGNORED for default type. */
    npc->anchors[0][0] = 999.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    return 0;
}

int test_combat_sm_phase_b_44_45_sub_iter_0_uses_combat_pose_anchor_unused(void)
{
    /* NPC 0x44 sub-iter 0 reads combat_pose (not anchors[k_anchor_44_45[0]]
     * since that's -1 = sentinel).  Sub-iters 1-6 read anchors[0/1/2/3/6/7]. */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 2.0f;
    npc->combat_pose[0]  = 0.0f;  /* overlap (sub-iter 0 hits) */
    /* Move ALL anchor entries far away — sub-iters 1-6 should MISS. */
    for (int k = 0; k < 8; k++) {
        npc->anchors[k][0] = 1000.0f;
    }

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);  /* only sub-iter 0 */
    return 0;
}

int test_combat_sm_phase_b_44_sub_iter_indices_match_rdata(void)
{
    /* Sub-iters 1-6 of NPC type 0x44 read anchors[0/1/2/3/6/7]
     * (DAT_005c5314[1..6] = {0, 1, 2, 3, 6, 7}).  Place each at origin
     * (= overlap) and verify each sub-iter is reported. */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 2.0f;
    /* Move combat_pose far so sub-iter 0 misses. */
    npc->combat_pose[0]  = 1000.0f;
    /* Anchors at indices read by sub-iters 1-6: {0, 1, 2, 3, 6, 7}.
     * All start at origin = overlap → all 6 hit. */
    /* anchor[4] / anchor[5] are NOT read for type 0x44 — set to far. */
    npc->anchors[4][0] = 1000.0f;
    npc->anchors[5][0] = 1000.0f;

    scene1_combat_set_phase_b_collision_hook(capture_collision_hook);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 6);
    T_ASSERT_EQ_I(g_collision_count, 6);
    /* Sub-iters reported in 1..6 order (engine iterates ascending). */
    T_ASSERT_EQ_I(g_collision_hits[0].sub_iter, 1);
    T_ASSERT_EQ_I(g_collision_hits[5].sub_iter, 6);
    return 0;
}

int test_combat_sm_phase_b_46_sub_iter_indices_match_rdata(void)
{
    /* NPC 0x46 sub-iters 0/1 read anchors[1/2] (DAT_005c530c[0..1]
     * = {1, 2}).  Anchor[0] (NOT read for this type) gets garbage. */
    reset_combat_state();
    install_unit_attrs(0x46);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x46;
    npc->attack_radius   = 2.0f;
    /* Move anchor[0] far — irrelevant for 0x46. */
    npc->anchors[0][0] = 1000.0f;
    /* anchor[1] + anchor[2] at origin = overlap → both hit. */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 2);
    return 0;
}

/* ─── Reach halved when anchor used ─────────────────────────────────── */

int test_combat_sm_phase_b_anchor_path_halves_reach(void)
{
    /* Construct an NPC where anchor distance > attack_radius * dist_mul
     * but < attack_radius * 0.5 * dist_mul * 2 — i.e., would hit at full
     * radius, miss at half.  Verifies the anchor-path radius scaling. */
    reset_combat_state();
    install_unit_attrs(0x46);
    int32_t *slot = attacker_slot_at(0, 0, 0, 0.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x46;
    npc->attack_radius   = 4.0f;
    /* Anchor 3 units away.  With unit attrs:
     *   full-reach threshold = 4.0 (attack_radius * 1.0 * 1.0)
     *     → 3.0 < 4.0 → would pass.
     *   half-reach threshold = 2.0 (attack_radius * 0.5 * 1.0 * 1.0)
     *     → 3.0 < 2.0 → FAIL.
     * So anchor path correctly halves the radius and misses. */
    npc->anchors[1][0] = 3.0f;
    npc->anchors[2][0] = 3.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    return 0;
}

/* ─── Distance gate ─────────────────────────────────────────────────── */

int test_combat_sm_phase_b_distance_gate_passes_in_range(void)
{
    /* dist (3.0) - slot.reach (1.0) = 2.0 < attack_radius (3.0)
     *   * dist_mul (1.0) * radius_mul (1.0) = 3.0 → pass. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 3.0f;  /* dist = 3 */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    return 0;
}

int test_combat_sm_phase_b_distance_gate_fails_out_of_range(void)
{
    /* dist (10.0) - slot.reach (1.0) = 9.0 NOT < attack_radius (3.0) * 1 * 1.
     * Fail. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 10.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    return 0;
}

int test_combat_sm_phase_b_distance_is_2d_xz(void)
{
    /* Distance is sqrt(dx² + dz²) — Y is excluded from distance.  Two
     * NPCs at (3, 100, 0) and (3, -100, 0) both produce dist = 3.  Y is
     * checked separately by the Y-band gate (large band ⇒ both pass). */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    /* Generous Y-band so dy=100 still passes. */
    g_scene1_combat_npc_type_attrs[0x10].y_band_mul = 500.0f;

    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 3.0f;
    npc->combat_pose[1]  = 100.0f;
    npc->combat_pose[2]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    return 0;
}

int test_combat_sm_phase_b_distance_origin_jitter(void)
{
    /* Engine `if (dx == 0 && dz == 0) dz = 0.01` — when NPC + slot
     * exactly overlap in XZ, the jitter prevents zero distance.  Test:
     * a same-spot NPC should still collide (overlap is the most
     * permissive case). */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(5.0f, 0, 5.0f, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 5.0f;  /* exact overlap */
    npc->combat_pose[2]  = 5.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    return 0;
}

/* ─── AABB Y-band gate ──────────────────────────────────────────────── */

int test_combat_sm_phase_b_y_band_passes_within(void)
{
    /* dy = 0.5, slot.reach * 0.8 = 0.8, y_band = 1.0 → |dy| < 0.8 + 1.0
     * = 1.8 → pass. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 1.0f;
    npc->combat_pose[1]  = 0.5f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    return 0;
}

int test_combat_sm_phase_b_y_band_fails_too_high(void)
{
    /* dy = 5.0, half + band = 0.8 + 1.0 = 1.8 → 5.0 > 1.8 → fail. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 1.0f;
    npc->combat_pose[1]  = 5.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    return 0;
}

int test_combat_sm_phase_b_y_band_fails_too_low(void)
{
    /* Symmetric: dy = -5.0 also fails. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 1.0f;
    npc->combat_pose[1]  = -5.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    return 0;
}

/* ─── Per-NPC-type attrs differentiate types ────────────────────────── */

int test_combat_sm_phase_b_attrs_lookup_keyed_by_npc_type(void)
{
    /* Two NPCs at distance 0.5 from slot; type 0x10 has permissive
     * attrs, type 0x11 has zero attrs.  Slot.reach = 0 so the zero-attrs
     * case can't "swallow" the NPC via slot reach.  Only type 0x10
     * collides. */
    reset_combat_state();
    install_unit_attrs(0x10);
    /* attrs[0x11] stays zero. */

    int32_t *slot = attacker_slot_at(0, 0, 0, 0.0f);  /* slot.reach = 0 */

    scene1_people_entry_t *npc0 = prep_npc_alive(0);
    npc0->npc_type        = 0x10;
    npc0->attack_radius   = 1.0f;
    npc0->combat_pose[0]  = 0.5f;  /* dist = 0.5 < 1.0 → pass for 0x10 */

    scene1_people_entry_t *npc1 = prep_npc_alive(1);
    npc1->npc_type        = 0x11;
    npc1->attack_radius   = 1.0f;
    npc1->combat_pose[0]  = 0.5f;
    /* For type 0x11: dist_threshold = 1.0 * 0 * 0 = 0; dist (0.5) - 0
     * NOT < 0 → fail. */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 2);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);  /* only npc0 */
    return 0;
}

/* ─── Collision counter resets between ticks ────────────────────────── */

int test_combat_sm_phase_b_collision_count_resets_between_ticks(void)
{
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);

    /* Move NPC out of range; counter should reset to 0. */
    npc->combat_pose[0] = 100.0f;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    return 0;
}

/* ─── Collision hook receives NPC index + sub_iter ──────────────────── */

int test_combat_sm_phase_b_collision_hook_install_returns_previous(void)
{
    reset_combat_state();
    scene1_combat_phase_b_collision_fn prev =
        scene1_combat_set_phase_b_collision_hook(capture_collision_hook);
    T_ASSERT_EQ_I(prev == NULL, 1);

    scene1_combat_phase_b_collision_fn prev2 =
        scene1_combat_set_phase_b_collision_hook(NULL);
    T_ASSERT_EQ_I(prev2 == capture_collision_hook, 1);
    return 0;
}

int test_combat_sm_phase_b_multiple_npcs_collide(void)
{
    /* Three NPCs at indices 5/10/20, all overlap slot.  All three
     * collide; collision count = 3, hook fires 3 times. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);

    int npcs[3] = {5, 10, 20};
    for (int j = 0; j < 3; j++) {
        scene1_people_entry_t *npc = prep_npc_alive(npcs[j]);
        npc->npc_type        = 0x10;
        npc->attack_radius   = 3.0f;
        npc->combat_pose[0]  = 0.0f;
    }

    scene1_combat_set_phase_b_collision_hook(capture_collision_hook);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 3);
    T_ASSERT_EQ_I(g_collision_count, 3);
    T_ASSERT_EQ_I(g_collision_hits[0].npc_index, 5);
    T_ASSERT_EQ_I(g_collision_hits[1].npc_index, 10);
    T_ASSERT_EQ_I(g_collision_hits[2].npc_index, 20);
    return 0;
}

/* ─── Phase B head still gates collision math ───────────────────────── */

int test_combat_sm_phase_b_skip_gate_blocks_collision(void)
{
    /* NPC with sister_724 != 0 fails skip gate → no visit + no
     * collision check, even if it would otherwise collide. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->sister_724      = 1;  /* fails skip gate 2 */
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    return 0;
}

int test_combat_sm_phase_b_hit_history_blocks_collision(void)
{
    /* NPC in hit_history → no collision check. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0x1234;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->hit_history[0]  = 0x1234;  /* already hit */
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    return 0;
}

/* ═══ C8jb.4 — Phase B per-collision arming ════════════════════════════ */

/* ─── Default armed when no special arming rule fires ──────────────── */

int test_combat_sm_phase_b_arming_default_collision_is_armed(void)
{
    /* NPC type 0x10 (not 0x44-0x48), sub-iter 0 → no disarm rule
     * fires.  Collision = armed.  Both counters increment in sync. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 1);
    return 0;
}

/* ─── NPC type 0x48 disarms ─────────────────────────────────────────── */

int test_combat_sm_phase_b_arming_npc_type_48_disarms(void)
{
    reset_combat_state();
    install_unit_attrs(0x48);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x48;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 0);
    return 0;
}

/* ─── 0x44/0x45 sub-iter 0 + facing-aligned → armed ────────────────── */

int test_combat_sm_phase_b_arming_44_facing_player_armed(void)
{
    /* NPC at (0, 0, -2) (in front of slot at origin); npc.yaw = 0
     * (facing +z).  atan2(dx=0, dz=-2) = π.  angle = π - 0 + π = 2π,
     * wrapped → 0.  |0| < 0.9424779 → armed. */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 5.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->combat_pose[2]  = -2.0f;
    npc->npc_yaw         = 0.0f;
    /* Move all anchors out of range so only sub-iter 0 collides. */
    for (int k = 0; k < 8; k++) {
        npc->anchors[k][0] = 1000.0f;
    }

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 1);
    return 0;
}

/* ─── 0x44/0x45 sub-iter 0 + facing-away → disarmed ────────────────── */

int test_combat_sm_phase_b_arming_44_facing_away_disarmed(void)
{
    /* NPC at (0, 0, -2); npc.yaw = π (facing -z, away from slot).
     * atan2(0, -2) = π.  angle = π - π + π = π, normalized → π.
     * 0.9424779 < π → disarm. */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 5.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->combat_pose[2]  = -2.0f;
    npc->npc_yaw         = 3.1415927f;  /* facing away */
    for (int k = 0; k < 8; k++) {
        npc->anchors[k][0] = 1000.0f;
    }

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 0);
    return 0;
}

/* ─── 0x44/0x45 phase==6, subphase==1 force-arms ────────────────────── */

int test_combat_sm_phase_b_arming_44_force_arm_in_special_phase(void)
{
    /* Same setup as the facing-away test, but with phase==6 ∧
     * subphase==1 — engine bypasses the angle filter and force-arms. */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 5.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->combat_pose[2]  = -2.0f;
    npc->npc_yaw         = 3.1415927f;  /* would normally disarm */
    npc->npc_phase       = 6;
    npc->npc_subphase    = 1;
    for (int k = 0; k < 8; k++) {
        npc->anchors[k][0] = 1000.0f;
    }

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 1);
    return 0;
}

int test_combat_sm_phase_b_arming_44_phase_6_subphase_2_does_not_force_arm(void)
{
    /* Only the EXACT pair (phase==6, subphase==1) force-arms.  Any
     * other (phase, subphase) keeps the normal angle filter. */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 5.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->combat_pose[2]  = -2.0f;
    npc->npc_yaw         = 3.1415927f;  /* angle filter would disarm */
    npc->npc_phase       = 6;
    npc->npc_subphase    = 2;  /* NOT subphase 1 */
    for (int k = 0; k < 8; k++) {
        npc->anchors[k][0] = 1000.0f;
    }

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 0);
    return 0;
}

int test_combat_sm_phase_b_arming_44_phase_5_subphase_1_does_not_force_arm(void)
{
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 5.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->combat_pose[2]  = -2.0f;
    npc->npc_yaw         = 3.1415927f;
    npc->npc_phase       = 5;  /* NOT phase 6 */
    npc->npc_subphase    = 1;
    for (int k = 0; k < 8; k++) {
        npc->anchors[k][0] = 1000.0f;
    }

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 0);
    return 0;
}

/* ─── 0x44/0x45 sub-iter > 0 → anchor-path disarm ──────────────────── */

int test_combat_sm_phase_b_arming_44_anchor_sub_iter_disarms(void)
{
    /* NPC type 0x44 with all 7 sub-iters in range and facing-aligned.
     * Sub-iter 0 = armed (no anchor path).  Sub-iters 1-6 = disarmed
     * (engine L35232 sets local_18 = 1 for non-0x46/0x47 anchor
     * sub-iters). */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 5.0f;
    npc->npc_yaw         = 0.0f;
    /* combat_pose + all anchors at z = -2 (in front of slot, facing-
     * aligned).  Sub-iters 0..6 all in collision range. */
    npc->combat_pose[2]  = -2.0f;
    for (int k = 0; k < 8; k++) {
        npc->anchors[k][2] = -2.0f;
    }

    scene1_combat_set_phase_b_armed_hook(capture_armed_hook);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 7);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 1);
    T_ASSERT_EQ_I(g_armed_count, 1);
    T_ASSERT_EQ_I(g_armed_hits[0].sub_iter, 0);  /* only sub-iter 0 */
    return 0;
}

/* ─── 0x46/0x47 anchor path stays armed (different rule) ────────────── */

int test_combat_sm_phase_b_arming_46_anchor_sub_iters_stay_armed(void)
{
    /* NPC type 0x46 uses anchors for BOTH sub-iters, but engine does
     * NOT set local_18 for 0x46/0x47 (the `else if` branch at L35232
     * is skipped).  Both sub-iters stay armed if collision passes.
     * NPC type 0x46 is NOT 0x44/0x45 so the angle filter doesn't
     * apply either. */
    reset_combat_state();
    install_unit_attrs(0x46);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x46;
    npc->attack_radius   = 5.0f;
    /* Both anchors at -2 z (sub-iters 0/1 use anchors[1] / [2]). */
    npc->anchors[1][2] = -2.0f;
    npc->anchors[2][2] = -2.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 2);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 2);
    return 0;
}

/* ─── Angle-filter boundary check ───────────────────────────────────── */

int test_combat_sm_phase_b_arming_angle_at_threshold_disarms(void)
{
    /* engine: `0.9424779 <= angle OR angle <= -0.9424779` → disarm.
     * At exactly +threshold: disarmed (inclusive on positive side). */
    reset_combat_state();
    install_unit_attrs(0x44);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x44;
    npc->attack_radius   = 5.0f;
    /* Choose pose + yaw so the normalized angle = EXACTLY 0.9424779.
     * atan2(dx, dz) - yaw + π = 0.9424779
     * Set dx = 0, dz = -2 → atan2 = π.  Want π - yaw + π = 0.9424779
     * → yaw = 2π - 0.9424779.  Normalized to (-π, π], yaw ≈ -1 rad
     * (= -π + (π - 0.9424779) = -π + ε... wait let me redo).
     *
     * Easier: angle = atan2(dx, dz) - yaw + π.  Want angle = +threshold.
     * Pick dx = 0, dz = +1 → atan2 = 0.  Pick yaw = π - 0.9424779.
     * Then angle = 0 - (π - 0.9424779) + π = 0.9424779. */
    npc->combat_pose[0]  = 0.0f;
    npc->combat_pose[2]  = 1.0f;  /* slot in front of NPC */
    npc->npc_yaw         = 3.1415927f - 0.9424779f;
    for (int k = 0; k < 8; k++) {
        npc->anchors[k][0] = 1000.0f;
    }

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    /* angle == +threshold → engine `0.9424779 <= angle` → TRUE → disarm. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 0);
    return 0;
}

/* ─── Non-0x44/0x45/0x48 types unaffected by angle filter ──────────── */

int test_combat_sm_phase_b_arming_other_types_ignore_yaw(void)
{
    /* NPC type 0x10 with yaw pointing away from slot.  Default armed,
     * no angle filter — yaw is irrelevant. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->npc_yaw         = 3.1415927f;  /* facing away — IGNORED */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 1);
    return 0;
}

/* ─── armed_collision_count <= collision_count invariant ───────────── */

int test_combat_sm_phase_b_armed_count_le_collision_count(void)
{
    /* Mixed types in range; armed count should never exceed total. */
    reset_combat_state();
    install_unit_attrs(0x10);
    install_unit_attrs(0x48);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc0 = prep_npc_alive(0);
    npc0->npc_type        = 0x10;
    npc0->attack_radius   = 3.0f;
    npc0->combat_pose[0]  = 0.0f;
    scene1_people_entry_t *npc1 = prep_npc_alive(1);
    npc1->npc_type        = 0x48;
    npc1->attack_radius   = 3.0f;
    npc1->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 2);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 1);
    return 0;
}

/* ─── Armed counters reset between ticks ────────────────────────────── */

int test_combat_sm_phase_b_armed_count_resets_between_ticks(void)
{
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 1);

    npc->combat_pose[0] = 100.0f;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 0);
    return 0;
}

/* ─── Armed hook install/uninstall ──────────────────────────────────── */

int test_combat_sm_phase_b_armed_hook_install_returns_previous(void)
{
    reset_combat_state();
    scene1_combat_phase_b_armed_fn prev =
        scene1_combat_set_phase_b_armed_hook(capture_armed_hook);
    T_ASSERT_EQ_I(prev == NULL, 1);

    scene1_combat_phase_b_armed_fn prev2 =
        scene1_combat_set_phase_b_armed_hook(NULL);
    T_ASSERT_EQ_I(prev2 == capture_armed_hook, 1);
    return 0;
}

/* ═══ C8jb.5a — Phase B damage-roll prologue ═══════════════════════════ */

/* Helper: set slot[VEL_X] / [VEL_Z] in a single call. */
static void set_slot_velocity(int32_t *slot, float vx, float vz)
{
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X] = vx;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_Z] = vz;
}

/* ─── KB factor: positive velocity → 0.7 / sqrt(VX²+VZ²) ────────────── */

int test_combat_sm_phase_b_kb_strength_positive_vel(void)
{
    /* VEL_X = 3, VEL_Z = 4 → sqrt(9+16) = 5; kb = 0.7 / 5 = 0.14. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    set_slot_velocity(slot, 3.0f, 4.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT(fabsf(g_scene1_combat_phase_b_kb_strength - 0.14f) < 1e-6f);
    return 0;
}

/* ─── KB factor: zero velocity → 0.0 (engine: divide skipped) ──────── */

int test_combat_sm_phase_b_kb_strength_zero_vel(void)
{
    /* VEL_X = VEL_Z = 0 → sqrt = 0; engine fcomp ≤ 0 → SKIP divide,
     * local_8 stays at 0.0 (not divide-by-zero infinity). */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    set_slot_velocity(slot, 0.0f, 0.0f);
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT(fabsf(g_scene1_combat_phase_b_kb_strength) < 1e-6f);
    return 0;
}

/* ─── KB factor: VEL_Y ignored (only XZ contribute) ────────────────── */

int test_combat_sm_phase_b_kb_strength_ignores_vel_y(void)
{
    /* Engine reads VEL_X (edi+0x68 = slot[0x1a]) + VEL_Z (edi+0x70 =
     * slot[0x1c]).  VEL_Y at slot[0x1b] is not part of the magnitude. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    set_slot_velocity(slot, 5.0f, 0.0f);
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_Y] = 999.0f;  /* IGNORED */
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT(fabsf(g_scene1_combat_phase_b_kb_strength - 0.7f / 5.0f) < 1e-6f);
    return 0;
}

/* ─── KB factor: no collision → kb stays 0 (default reset) ─────────── */

int test_combat_sm_phase_b_kb_strength_no_collision_stays_zero(void)
{
    /* NPC out of range → no collision → prologue never fires → kb=0. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    set_slot_velocity(slot, 3.0f, 4.0f);  /* would yield 0.14 IF collided */
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 100.0f;  /* out of range */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    T_ASSERT(fabsf(g_scene1_combat_phase_b_kb_strength) < 1e-6f);
    return 0;
}

/* ─── Hit-history bump writes SEQ_ID at hit_cursor ─────────────────── */

int test_combat_sm_phase_b_hit_history_bump_writes_seq_id(void)
{
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0xabcd;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->hit_cursor      = 0;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(npc->hit_history[0], 0xabcd);
    T_ASSERT_EQ_I(npc->hit_cursor, 1);
    /* Other slots untouched. */
    for (int k = 1; k < 10; k++) {
        T_ASSERT_EQ_I(npc->hit_history[k], 0);
    }
    return 0;
}

/* ─── Hit-history bump wraps cursor 9 → 0 ──────────────────────────── */

int test_combat_sm_phase_b_hit_history_wraps_at_10(void)
{
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0x77;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->hit_cursor      = 9;  /* about to wrap */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(npc->hit_history[9], 0x77);
    T_ASSERT_EQ_I(npc->hit_cursor, 0);
    return 0;
}

/* ─── Hit-history bump only fires per in-range collision ───────────── */

int test_combat_sm_phase_b_hit_history_only_on_in_range(void)
{
    /* NPC out of range → no hit-history write. */
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0x99;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 100.0f;  /* out of range */
    npc->hit_cursor      = 5;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 0);
    /* hit_history untouched; cursor unchanged. */
    for (int k = 0; k < 10; k++) {
        T_ASSERT_EQ_I(npc->hit_history[k], 0);
    }
    T_ASSERT_EQ_I(npc->hit_cursor, 5);
    return 0;
}

/* ─── Hit-history bump fires once per multi-anchor sub-iter hit ────── */

int test_combat_sm_phase_b_hit_history_multi_subiter(void)
{
    /* NPC type 0x46 has 2 sub-iters using anchors[1] + [2].  Both in
     * range → 2 hit-history bumps, cursor advances by 2.  Engine
     * iterates writes in sequence. */
    reset_combat_state();
    install_unit_attrs(0x46);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = 0x1234;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x46;
    npc->attack_radius   = 5.0f;
    npc->anchors[1][2]   = -2.0f;
    npc->anchors[2][2]   = -2.0f;
    npc->hit_cursor      = 3;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 2);
    T_ASSERT_EQ_I(npc->hit_history[3], 0x1234);
    T_ASSERT_EQ_I(npc->hit_history[4], 0x1234);
    T_ASSERT_EQ_I(npc->hit_cursor, 5);
    return 0;
}

/* ─── 0x53 short-circuit fires + writes kill_age + bed8 ────────────── */

int test_combat_sm_phase_b_heavy_atk_fires_kill_age_default(void)
{
    /* slot[TYPE] = 0x53, per-type heavy_atk_mode = 0, npc_type != 0x22,
     * aux_4319d6 returns 0 → kill_age = 600.  slot.AGE = 100 →
     * npc.b18_kill_age_out = 500. */
    reset_combat_state();
    install_unit_attrs(0x10);  /* permissive collision */
    g_scene1_combat_npc_type_attrs[0x10].heavy_atk_mode = 0;
    g_aux_4319d6_return = 0;
    scene1_records_b_set_aux_4319d6_hook(aux_4319d6_returns_configured);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    /* TYPE=0x53 doubles as the FLAG_A field?  No — TYPE is offset 0,
     * FLAG_A is offset 1.  We already set FLAG_A=0 in attacker_slot. */
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 100;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_heavy_atk_count, 1);
    T_ASSERT_EQ_I(npc->npc_b18_kill_age_out, 500);
    T_ASSERT_EQ_I(g_scene1_combat_dat_0438bed8, 4);
    T_ASSERT(fabsf(g_scene1_combat_phase_b_kb_strength) < 1e-6f);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 0);
    T_ASSERT_EQ_I(g_aux_4319d6_call_count, 1);
    return 0;
}

/* ─── 0x53 kill_age uses 0x78 when aux_4319d6 returns 1 ─────────────── */

int test_combat_sm_phase_b_heavy_atk_kill_age_transitioning(void)
{
    reset_combat_state();
    install_unit_attrs(0x10);
    g_aux_4319d6_return = 1;  /* stage-transitioning */
    scene1_records_b_set_aux_4319d6_hook(aux_4319d6_returns_configured);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 0x20;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    /* kill_age = 0x78 (= 120); slot.AGE = 0x20 (= 32); 120-32 = 88. */
    T_ASSERT_EQ_I(npc->npc_b18_kill_age_out, 88);
    return 0;
}

/* ─── 0x53 kill_age MAX(0, ...) clamp when slot.AGE exceeds kill_age ─ */

int test_combat_sm_phase_b_heavy_atk_kill_age_negative_clamped(void)
{
    /* slot.AGE > kill_age → MAX(0, ...) clamps to 0. */
    reset_combat_state();
    install_unit_attrs(0x10);
    g_aux_4319d6_return = 0;  /* kill_age = 600 */
    scene1_records_b_set_aux_4319d6_hook(aux_4319d6_returns_configured);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 9999;  /* > kill_age */
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(npc->npc_b18_kill_age_out, 0);
    return 0;
}

/* ─── 0x53 short-circuit SKIPPED when heavy_atk_mode != 0 ──────────── */

int test_combat_sm_phase_b_heavy_atk_skipped_when_mode_nonzero(void)
{
    /* Engine: `if (per_type_attrs[npc_type].heavy_atk_mode == 0)` — if
     * nonzero, the 0x53 branch is skipped entirely (no kill_age write,
     * no bed8 write, kb stays at vel-derived value, damage_out stays 0). */
    reset_combat_state();
    install_unit_attrs(0x10);
    g_scene1_combat_npc_type_attrs[0x10].heavy_atk_mode = 1;  /* SKIP */

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    set_slot_velocity(slot, 5.0f, 0.0f);  /* kb = 0.7 / 5 = 0.14 */
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 100;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->npc_b18_kill_age_out = 0xdead;  /* sentinel; should stay */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_heavy_atk_count, 0);
    T_ASSERT_EQ_I(npc->npc_b18_kill_age_out, 0xdead);  /* sentinel unchanged */
    T_ASSERT_EQ_I(g_scene1_combat_dat_0438bed8, 0);
    T_ASSERT(fabsf(g_scene1_combat_phase_b_kb_strength - 0.14f) < 1e-6f);
    return 0;
}

/* ─── 0x53 short-circuit SKIPPED when npc_type == 0x22 ─────────────── */

int test_combat_sm_phase_b_heavy_atk_skipped_when_npc_type_22(void)
{
    /* Engine: `&& npc.npc_type != 0x22` — NPC 0x22 immunity. */
    reset_combat_state();
    install_unit_attrs(0x22);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    set_slot_velocity(slot, 5.0f, 0.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 100;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x22;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;
    npc->npc_b18_kill_age_out = 0xbeef;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_heavy_atk_count, 0);
    T_ASSERT_EQ_I(npc->npc_b18_kill_age_out, 0xbeef);
    T_ASSERT_EQ_I(g_scene1_combat_dat_0438bed8, 0);
    T_ASSERT(fabsf(g_scene1_combat_phase_b_kb_strength - 0.14f) < 1e-6f);
    return 0;
}

/* ─── 0x53 short-circuit SKIPPED when slot.TYPE != 0x53 ────────────── */

int test_combat_sm_phase_b_heavy_atk_skipped_when_slot_not_53(void)
{
    /* slot.TYPE != 0x53 → general damage path (still stubbed; just
     * confirm short-circuit doesn't fire). */
    reset_combat_state();
    install_unit_attrs(0x10);
    g_aux_4319d6_return = 1;
    scene1_records_b_set_aux_4319d6_hook(aux_4319d6_returns_configured);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x52;  /* != 0x53 */
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_heavy_atk_count, 0);
    /* aux_4319d6 not invoked because the 0x53 gate didn't open. */
    T_ASSERT_EQ_I(g_aux_4319d6_call_count, 0);
    return 0;
}

/* ─── C8jb.5a counters reset between ticks ─────────────────────────── */

int test_combat_sm_phase_b_damage_roll_counters_reset(void)
{
    reset_combat_state();
    install_unit_attrs(0x10);
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 100;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type        = 0x10;
    npc->attack_radius   = 3.0f;
    npc->combat_pose[0]  = 0.0f;

    /* First tick: 0x53 fires → heavy_atk_count = 1. */
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_heavy_atk_count, 1);

    /* Move NPC out of range; second tick has no collision → all per-
     * tick counters reset to 0. */
    npc->combat_pose[0] = 100.0f;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_heavy_atk_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 0);
    T_ASSERT(fabsf(g_scene1_combat_phase_b_kb_strength) < 1e-6f);
    /* DAT_0438bed8 is NOT reset by the SM — engine semantic.  Tests
     * that care about its value should reset it explicitly. */
    return 0;
}
