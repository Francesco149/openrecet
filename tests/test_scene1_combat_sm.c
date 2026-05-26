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
 *   C8jb.5b — Phase B general damage formula (two-pass + combo + scene).
 *   C8jb.5c — Phase B post-damage clamps (npc_phase, quadrant atan2,
 *             charge-attack disarm, final < 1 / >= 5 RNG clamps).
 *   C8jb.6  — Phase B hit-effect emit cluster (per-TYPE kb_strength
 *             scale, KB-vector write to NPC, spawn + SE branch tables,
 *             return 1 contract).  C8jb.6 introduces the SM's first
 *             non-zero return: every in-range collision now fires the
 *             emit cluster and the SM returns 1 immediately.  Tests
 *             that exercise an in-range collision now expect ret=1.
 *
 * Phase C / Phase D not yet ported.  C8jb.1..5/6 returns 0 if no
 * in-range collision; 1 if a hit emitted.
 */

#include "t.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "rng.h"
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
    /* C8jb.5b globals. */
    g_scene1_combat_damage_base_idle  = 0;
    g_scene1_combat_damage_base_idle2 = 0;
    g_scene1_combat_scene_mul_014     = 0;
    g_scene1_combat_scene_mul_01c     = 0;
    g_scene1_combat_dat_056da1b8      = 0;
    g_scene1_combat_owner_b_npc_type  = 0;
    /* C8jb.5c globals. */
    g_scene1_combat_phase_b_local_1c_bits = 0;
    g_scene1_combat_owner_a_ce4           = 0;
    g_scene1_combat_owner_a_cec           = 0;
    /* C8jb.8d global. */
    g_scene1_combat_owner_a_2bc82         = 0;
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
    scene1_combat_set_combo_held_hook(NULL);
    scene1_combat_set_rng_damage_scale_hook(NULL);
    scene1_combat_set_rng_unsigned_hook(NULL);
    scene1_records_b_set_aux_482a51_hook(NULL);
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
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision → only sub-iter 0
     * fires before the emit cluster returns 1. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision → only sub-iter 1
     * fires (sub-iter 0 misses because combat_pose is far).  Engine
     * iterates ascending, so the first reported hit is sub_iter 1. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_collision_count, 1);
    T_ASSERT_EQ_I(g_collision_hits[0].sub_iter, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: outer scan returns on first hit → visit stops at npc0. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: outer scan returns on the first hit → only npc 5 (the
     * lowest index) is visited and collides. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_collision_count, 1);
    T_ASSERT_EQ_I(g_collision_hits[0].npc_index, 5);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision → only sub-iter 0
     * fires (which is the armed one). */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision → only sub-iter 0
     * (anchor[1]) fires; both would have been armed without the break. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: outer scan returns on first hit (npc 0, type 0x10, armed). */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* C8jb.6: early-break on first in-range collision → only sub-iter 0
     * (anchor[1]) fires; only one hit-history bump from cursor 3 → 4. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(npc->hit_history[3], 0x1234);
    T_ASSERT_EQ_I(npc->hit_cursor, 4);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_heavy_atk_count, 0);
    T_ASSERT_EQ_I(npc->npc_b18_kill_age_out, 0xdead);  /* sentinel unchanged */
    /* C8jb.6: emit_scale_kb_strength always writes bed8=4 per in-range
     * collision (not just the 0x53 short-circuit). */
    T_ASSERT_EQ_I(g_scene1_combat_dat_0438bed8, 4);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_heavy_atk_count, 0);
    T_ASSERT_EQ_I(npc->npc_b18_kill_age_out, 0xbeef);
    /* C8jb.6: emit_scale_kb_strength always writes bed8=4 per in-range
     * collision (not just the 0x53 short-circuit). */
    T_ASSERT_EQ_I(g_scene1_combat_dat_0438bed8, 4);
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

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
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

/* ═══ C8jb.5b — Phase B general damage formula ═════════════════════════ */
/*
 * Asm verification: see scene1_combat_sm.c::phase_b_damage_roll_general
 * doc block for the line-by-line trace of engine asm 0x438c1c..0x438eaa.
 */

/* combo_held hook: returns 1 for a single configured button. */
static int g_combo_held_button = -1;
static int g_combo_held_call_count = 0;
static int g_combo_held_buttons_seen[16];
static int g_combo_held_buttons_seen_n = 0;

static int combo_held_one_button(int btn)
{
    if (g_combo_held_buttons_seen_n
        < (int)(sizeof g_combo_held_buttons_seen
                / sizeof g_combo_held_buttons_seen[0])) {
        g_combo_held_buttons_seen[g_combo_held_buttons_seen_n++] = btn;
    }
    g_combo_held_call_count++;
    return (btn == g_combo_held_button) ? 1 : 0;
}

/* rng_damage_scale hook: returns a fixed value (default 1.0) and
 * records the arg passed by the SM. */
static float g_rng_damage_scale_return = 1.0f;
static int   g_rng_damage_scale_args[8];
static int   g_rng_damage_scale_args_n = 0;

static float rng_damage_scale_const(int arg)
{
    if (g_rng_damage_scale_args_n
        < (int)(sizeof g_rng_damage_scale_args
                / sizeof g_rng_damage_scale_args[0])) {
        g_rng_damage_scale_args[g_rng_damage_scale_args_n++] = arg;
    }
    return g_rng_damage_scale_return;
}

static void reset_combat_5b_capture(void)
{
    g_combo_held_button         = -1;
    g_combo_held_call_count     = 0;
    g_combo_held_buttons_seen_n = 0;
    memset(g_combo_held_buttons_seen, 0, sizeof g_combo_held_buttons_seen);
    g_rng_damage_scale_return = 1.0f;
    g_rng_damage_scale_args_n = 0;
    memset(g_rng_damage_scale_args, 0, sizeof g_rng_damage_scale_args);
}

/*
 * Bring a slot into the per-collision damage-roll body of phase_b_scan.
 * NPC type defaults to 0x10 (boring type — not 0x44/0x45 multi-hit, not
 * 0x46/0x47 paired-hit, not 0x48 disarm).  Returns the npc pointer.
 */
static scene1_people_entry_t *
arm_collision_at(int32_t *slot, int npc_index, int npc_type)
{
    install_unit_attrs(npc_type);
    scene1_people_entry_t *npc = prep_npc_alive(npc_index);
    npc->npc_type       = npc_type;
    npc->attack_radius  = 3.0f;
    npc->combat_pose[0] = 0.0f;
    npc->combat_pose[1] = 0.0f;
    /* pose Z < 0 with yaw=0 gives the C8jb.5c quadrant atan2 a FRONT-hit
     * angle (no *1.2 / *1.5 scaling, no bit set) — preserves the C8jb.5b
     * damage int through the C8jb.5c clamps for tests that calibrate the
     * pre-clamp damage value. */
    npc->combat_pose[2] = -0.5f;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_SCALE_X] = 1.0f;  /* unit scale */
    (void)slot;
    return npc;
}

/* ─── Idle base damage selects pass-1 in TYPE 0x12 path ────────────── */

int test_combat_sm_phase_b_general_idle_base_negation_via_type_0x12(void)
{
    /* TYPE 0x12 takes the negation path: damage = -first_damage.
     * Idle pass 1: first_damage = (int)(0 - DAT_056db0b4/2) = -5 for
     * DAT_056db0b4 = 10.  damage = -(-5) = 5.  SCALE_X = 1.0. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x12;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;  /* idle */
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);
    return 0;
}

/* ─── Default-branch TYPE picks pass-2 result ─────────────────────── */

int test_combat_sm_phase_b_general_idle_default_picks_second_damage(void)
{
    /* TYPE 0x10 (not in either special set) → default branch: damage =
     * second_damage = (int)(DAT_056db0ac/2 - 0/4) = 10/2 = 5. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);
    return 0;
}

/* ─── Average path (TYPE 0x3e) ────────────────────────────────────── */

int test_combat_sm_phase_b_general_idle_average_path_type_0x3e(void)
{
    /* TYPE 0x3e → damage = (second + -first) / 2 (signed).
     * Idle with DAT_056db0b4=20, DAT_056db0ac=40:
     *   first  = (int)(0 - 20/2) = -10  →  -first = 10
     *   second = (int)(40/2 - 0/4) = 20
     *   damage = (20 + 10) / 2 = 15. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle  = 20;
    g_scene1_combat_damage_base_idle2 = 40;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x3e;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 15);
    return 0;
}

/* ─── SCALE_X scales the result ─────────────────────────────────── */

int test_combat_sm_phase_b_general_scales_by_slot_scale_x(void)
{
    /* same as default-second test but SCALE_X = 2.0 → damage = 10. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_SCALE_X] = 2.0f;
    arm_collision_at(slot, 0, 0x10);
    /* arm_collision_at sets SCALE_X to 1.0; override AFTER the helper. */
    *(float *)&slot[SCENE1_RECORDS_B_OFF_SCALE_X] = 2.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);
    return 0;
}

/* ─── Idle combo 5 doubles damage ────────────────────────────────── */

int test_combat_sm_phase_b_general_idle_combo_button_5_doubles(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_combo_held_button = 5;
    scene1_combat_set_combo_held_hook(combo_held_one_button);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);  /* 5 * 2 */
    return 0;
}

/* ─── Idle combo 3 also doubles (paired alternative) ─────────────── */

int test_combat_sm_phase_b_general_idle_combo_button_3_doubles(void)
{
    /* Engine 0x438e25-0x438e3b: combo_held(5) || combo_held(3) → *2. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_combo_held_button = 3;
    scene1_combat_set_combo_held_hook(combo_held_one_button);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);
    return 0;
}

/* ─── Scene mul gate doubles damage (idle only) ───────────────── */

int test_combat_sm_phase_b_general_idle_scene_mul_014_doubles(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_scene1_combat_scene_mul_014     = 1;  /* > 0 → LAB_00438e6d */

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);
    return 0;
}

int test_combat_sm_phase_b_general_idle_scene_mul_01c_also_doubles(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_scene1_combat_scene_mul_01c     = 1;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);
    return 0;
}

int test_combat_sm_phase_b_general_idle_combo_and_scene_stack(void)
{
    /* Combo 5 + scene_mul_014 → *2 * *2 = *4. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_scene1_combat_scene_mul_014     = 1;
    g_combo_held_button = 5;
    scene1_combat_set_combo_held_hook(combo_held_one_button);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 20);  /* 5*4 */
    return 0;
}

/* ─── Common combo 7/6 halves ──────────────────────────────────── */

int test_combat_sm_phase_b_general_combo_button_7_halves(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 20;  /* second_damage = 10 */
    g_combo_held_button = 7;
    scene1_combat_set_combo_held_hook(combo_held_one_button);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);  /* 10/2 */
    return 0;
}

int test_combat_sm_phase_b_general_combo_button_6_halves(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    g_combo_held_button = 6;
    scene1_combat_set_combo_held_hook(combo_held_one_button);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);
    return 0;
}

/* ─── Block/dodge counter halves ──────────────────────────────── */

int test_combat_sm_phase_b_general_block_dodge_b38_halves(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->block_dodge_b38 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);  /* 10/2 */
    return 0;
}

/* ─── Sound bus bit set on every general-formula collision ────── */

int test_combat_sm_phase_b_general_sets_dat_056da1b8_bit_1(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_dat_056da1b8 = 0;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I((g_scene1_combat_dat_056da1b8 & 2), 2);
    return 0;
}

/* ─── Slot TYPE == 0x53 SKIPS general formula ─────────────────── */

int test_combat_sm_phase_b_general_skipped_when_slot_type_0x53(void)
{
    /* Engine 0x438bc0 `jne 0x438c1c`: TYPE == 0x53 jumps to LAB_004392a7,
     * never reaching the general formula. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_scene1_combat_dat_056da1b8      = 0;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x53;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* damage_out stays at the C8jb.5a prologue's 0; bit 1 of da1b8 NOT
     * set because the general formula didn't run. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 0);
    T_ASSERT_EQ_I((g_scene1_combat_dat_056da1b8 & 2), 0);
    return 0;
}

/* ─── Attacker branch (FLAG_A=3) uses RNG + per-attacker table ──── */

int test_combat_sm_phase_b_general_attacker_owner_b_null_uses_attrs_1a(void)
{
    /* Attacker with OWNER_B == 0: uses attrs[0x1a] + rng_arg=0x1a.
     * attrs[0x1a].attrs_int_3c=20 + rng=1.0 → damage_base=20.
     * attrs[0x1a].attrs_int_34=10 + rng=1.0 → base2=10.
     * first_damage = (int)(0/4 - 20/2) = -10
     * second_damage = (int)(10/2 - 0/4) = 5
     * default branch → damage = 5. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_rng_damage_scale_return = 1.0f;
    scene1_combat_set_rng_damage_scale_hook(rng_damage_scale_const);
    g_scene1_combat_npc_type_attrs[0x1a].attrs_int_3c = 20;
    g_scene1_combat_npc_type_attrs[0x1a].attrs_int_34 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]    = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]  = 3;     /* attacker (hit-recovery) */
    slot[SCENE1_RECORDS_B_OFF_OWNER_B] = 0;     /* NULL */
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);
    /* RNG hook called twice (pass 1 + pass 2), both with arg=0x1a. */
    T_ASSERT_EQ_I(g_rng_damage_scale_args_n, 2);
    T_ASSERT_EQ_I(g_rng_damage_scale_args[0], 0x1a);
    T_ASSERT_EQ_I(g_rng_damage_scale_args[1], 0x1a);
    return 0;
}

int test_combat_sm_phase_b_general_attacker_owner_b_uses_configured_npc_type(void)
{
    /* When slot.OWNER_B != 0, the SM uses g_scene1_combat_owner_b_npc_type
     * to look up per-attacker attrs.  Set it to 0x07 + populate the
     * table entry. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_rng_damage_scale_return = 1.0f;
    scene1_combat_set_rng_damage_scale_hook(rng_damage_scale_const);
    g_scene1_combat_owner_b_npc_type = 0x07;
    g_scene1_combat_npc_type_attrs[0x07].attrs_int_3c = 40;
    g_scene1_combat_npc_type_attrs[0x07].attrs_int_34 = 14;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]    = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]  = 3;
    slot[SCENE1_RECORDS_B_OFF_OWNER_B] = 0xfeedf00d;  /* non-NULL */
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage = (int)(14/2 - 0/4) = 7.  TYPE=0x10 → default branch. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 7);
    T_ASSERT_EQ_I(g_rng_damage_scale_args_n, 2);
    T_ASSERT_EQ_I(g_rng_damage_scale_args[0], 0x07);
    T_ASSERT_EQ_I(g_rng_damage_scale_args[1], 0x07);
    return 0;
}

/* ─── Attacker combo 4/3 doubles ──────────────────────────────── */

int test_combat_sm_phase_b_general_attacker_combo_button_4_doubles(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_rng_damage_scale_return = 1.0f;
    scene1_combat_set_rng_damage_scale_hook(rng_damage_scale_const);
    g_scene1_combat_npc_type_attrs[0x1a].attrs_int_34 = 10;
    g_combo_held_button = 4;
    scene1_combat_set_combo_held_hook(combo_held_one_button);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]    = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]  = 3;
    slot[SCENE1_RECORDS_B_OFF_OWNER_B] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);  /* 5 * 2 */
    return 0;
}

int test_combat_sm_phase_b_general_attacker_skips_scene_mul_gate(void)
{
    /* Attacker branch goes through LAB_00438e6d via combo only; the
     * scene_mul_014/01c gate is reachable only from the idle branch.
     * Verify scene_mul_014 = 1 has NO effect when FLAG_A != 0. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_rng_damage_scale_return = 1.0f;
    scene1_combat_set_rng_damage_scale_hook(rng_damage_scale_const);
    g_scene1_combat_npc_type_attrs[0x1a].attrs_int_34 = 10;
    g_scene1_combat_scene_mul_014 = 1;  /* would *2 in idle branch */

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]    = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]  = 3;
    slot[SCENE1_RECORDS_B_OFF_OWNER_B] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);  /* no *2 */
    return 0;
}

/* ─── Damage quirk disable b28 zeros the npc_quirk contribution ── */

int test_combat_sm_phase_b_general_quirk_disable_b28_zeros_quirk(void)
{
    /* With damage_quirk_disable_b28 != 0, the npc_quirk_mul²-derived
     * terms (attrs_int_38 / 0x40) get zeroed in both passes.  Set up a
     * non-zero quirk and verify it has no effect. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_scene1_combat_npc_type_attrs[0x10].attrs_int_38 = 100;
    g_scene1_combat_npc_type_attrs[0x10].attrs_int_40 = 100;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->damage_quirk_mul_ab8       = 2.0f;  /* would scale by 4 */
    npc->damage_quirk_disable_b28   = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* quirk is zeroed → second_damage = 10/2 = 5. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);
    return 0;
}

int test_combat_sm_phase_b_general_quirk_mul_ab8_squared_applied(void)
{
    /* damage_quirk_mul_ab8 = 2 → squared = 4.  attrs_int_38=10 →
     * npc_quirk2 = 40.  second_damage = (int)(0/2 - 40/4) = -10.
     * TYPE 0x10 → default branch → C8jb.5b damage = -10.  C8jb.5c
     * final clamp: NPC type 0x10 (!= 5) → `if damage < 1: damage = 1`,
     * then `damage < 5` → += rng & 1 = 0 → damage = 1. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_npc_type_attrs[0x10].attrs_int_38 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->damage_quirk_mul_ab8 = 2.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 1);
    return 0;
}

/* ─── damage_out resets between ticks ─────────────────────────── */

int test_combat_sm_phase_b_general_damage_out_resets_when_no_collision(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);

    /* Move NPC out of range; damage_out resets to 0. */
    npc->combat_pose[0] = 100.0f;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 0);
    return 0;
}

/* ─── combo_held button sequence on idle branch ──────────────── */

int test_combat_sm_phase_b_general_idle_combo_button_sequence_is_5_3_7_6(void)
{
    /* Engine call order for idle without scene_mul: combo_held(5),
     * (if 0) combo_held(3), then common tail combo_held(7), (if 0)
     * combo_held(6).  When all return 0 we see all 4 calls. */
    reset_combat_state();
    reset_combat_5b_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_combo_held_button = -1;  /* nothing held */
    scene1_combat_set_combo_held_hook(combo_held_one_button);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen_n, 4);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen[0], 5);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen[1], 3);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen[2], 7);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen[3], 6);
    return 0;
}

int test_combat_sm_phase_b_general_attacker_combo_button_sequence_is_4_3_7_6(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    g_rng_damage_scale_return = 1.0f;
    scene1_combat_set_rng_damage_scale_hook(rng_damage_scale_const);
    g_scene1_combat_npc_type_attrs[0x1a].attrs_int_34 = 10;
    g_combo_held_button = -1;
    scene1_combat_set_combo_held_hook(combo_held_one_button);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]    = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]  = 3;
    slot[SCENE1_RECORDS_B_OFF_OWNER_B] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen_n, 4);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen[0], 4);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen[1], 3);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen[2], 7);
    T_ASSERT_EQ_I(g_combo_held_buttons_seen[3], 6);
    return 0;
}

/* ═══ C8jb.5c — Phase B post-damage clamps ═════════════════════════════ */
/*
 * Asm verification: see scene1_combat_sm.c::phase_b_damage_roll_clamps
 * doc block for the line-by-line trace of engine asm 0x438eab..0x4390d3.
 */

static uint32_t g_rng_unsigned_return = 0;
static int      g_rng_unsigned_call_count = 0;

static uint32_t rng_unsigned_const(void)
{
    g_rng_unsigned_call_count++;
    return g_rng_unsigned_return;
}

static int g_aux_482a51_arg1_capture = 0;
static int g_aux_482a51_arg2_capture = 0;
static int g_aux_482a51_call_count = 0;

static void aux_482a51_capture(int32_t a1, int32_t a2)
{
    g_aux_482a51_arg1_capture = a1;
    g_aux_482a51_arg2_capture = a2;
    g_aux_482a51_call_count++;
}

static void reset_combat_5c_capture(void)
{
    g_rng_unsigned_return = 0;
    g_rng_unsigned_call_count = 0;
    g_aux_482a51_arg1_capture = 0;
    g_aux_482a51_arg2_capture = 0;
    g_aux_482a51_call_count = 0;
}

/*
 * Position the NPC for a "rear hit" (atan2(dx,dz) - npc_yaw + π wraps to
 * ±π): pose.Z > 0 with npc_yaw=0 gives ang = π.  Used to verify quadrant
 * scaling.  Caller must pass an attack_radius large enough that 0.5 < r.
 */
static scene1_people_entry_t *
arm_collision_rear_hit(int32_t *slot, int npc_index, int npc_type)
{
    install_unit_attrs(npc_type);
    scene1_people_entry_t *npc = prep_npc_alive(npc_index);
    npc->npc_type       = npc_type;
    npc->attack_radius  = 3.0f;
    npc->combat_pose[0] = 0.0f;
    npc->combat_pose[1] = 0.0f;
    npc->combat_pose[2] = 0.5f;  /* +Z → atan2 = 0 → ang = π → rear */
    npc->npc_yaw        = 0.0f;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_SCALE_X] = 1.0f;
    (void)slot;
    return npc;
}

/*
 * Position the NPC for a "side hit": pose at (+X, 0, 0), npc_yaw=0 →
 * atan2(dx=+0.5, dz=0) = π/2 → ang = π/2 + π = 3π/2 wrap → -π/2.  |angle|
 * = π/2 ∈ [π/4, 3π/4] → side hit.
 */
static scene1_people_entry_t *
arm_collision_side_hit(int32_t *slot, int npc_index, int npc_type)
{
    install_unit_attrs(npc_type);
    scene1_people_entry_t *npc = prep_npc_alive(npc_index);
    npc->npc_type       = npc_type;
    npc->attack_radius  = 3.0f;
    npc->combat_pose[0] = 0.5f;
    npc->combat_pose[1] = 0.0f;
    npc->combat_pose[2] = 0.0f;
    npc->npc_yaw        = 0.0f;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_SCALE_X] = 1.0f;
    (void)slot;
    return npc;
}

/* ─── Front-hit baseline: no quadrant bit set, no quadrant scale ───── */

int test_combat_sm_phase_b_clamp_front_hit_no_bit_set(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage=5, front hit, no scaling → 5. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 6, 0);
    return 0;
}

/* ─── Rear hit: bit 1 set, damage *= 1.5 ──────────────────────────── */

int test_combat_sm_phase_b_clamp_rear_hit_sets_bit_1_and_scales_1_5(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_rear_hit(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage = 10, *1.5 = 15.  damage_out = 15 (15>=5, += 0%(15/5)=0). */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 15);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 2, 2);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 4, 0);
    return 0;
}

/* ─── Side hit: bit 2 set, damage *= 1.2 ──────────────────────────── */

int test_combat_sm_phase_b_clamp_side_hit_sets_bit_2_and_scales_1_2(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_side_hit(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage = 10, *1.2 = 12.  damage_out = 12 (>=5, += 0%(12/5)=0). */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 12);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 4, 4);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 2, 0);
    return 0;
}

/* ─── npc_phase 1..6 → bit 3 + damage *= 1.2 ───────────────────────── */

int test_combat_sm_phase_b_clamp_npc_phase_in_range_scales_1_2(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_phase = 3;  /* in [1, 6] */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage=10, *1.2=12 (npc_phase), front hit (no quadrant). */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 12);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 8, 8);
    return 0;
}

int test_combat_sm_phase_b_clamp_npc_phase_zero_no_scaling(void)
{
    /* npc_phase = 0 (out of [1, 6]) → no *1.2 + no bit 3. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);  /* npc_phase = 0 by default */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage=10, no npc_phase scale → 10. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 8, 0);
    return 0;
}

int test_combat_sm_phase_b_clamp_npc_phase_7_no_scaling(void)
{
    /* npc_phase = 7 (out of [1, 6]) → no *1.2. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_phase = 7;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 8, 0);
    return 0;
}

/* ─── Idle pass-2 IS_PLAYER → bit 0 + *2 ──────────────────────────── */

int test_combat_sm_phase_b_clamp_idle_is_player_sets_bit_0_and_doubles(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 1;  /* IS_PLAYER */
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage=10, *2 (IS_PLAYER) → 20.  bit 0 set. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 20);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 1, 1);
    return 0;
}

/* ─── Idle OWNER_A flag → *1.5 ────────────────────────────────────── */

int test_combat_sm_phase_b_clamp_idle_owner_a_ce4_scales_1_5(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    g_scene1_combat_owner_a_ce4       = 1;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage=10, *1.5 (OWNER_A) → 15. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 15);
    return 0;
}

int test_combat_sm_phase_b_clamp_idle_owner_a_cec_also_scales(void)
{
    /* Sibling flag at OWNER_A+0xcec also triggers. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    g_scene1_combat_owner_a_cec       = 1;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 15);
    return 0;
}

/* ─── Disarm → damage = 0 (NPC 0x48 always disarms in range) ──────── */

int test_combat_sm_phase_b_clamp_disarm_via_npc_type_48_zeros_damage(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x48);  /* 0x48 disarms */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 0);
    /* armed counter stays 0. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 0);
    return 0;
}

/* ─── NPC 0x44/0x45 + slot.TYPE 0x12 + sub_iter==0 NPC reset ───────── */

int test_combat_sm_phase_b_clamp_npc_44_slot_12_sub0_resets_phase(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    /* Need attrs for NPC 0x44 (sub_iter count = 7).  We only want sub_iter 0
     * to fire collision; place anchors out of range. */
    install_unit_attrs(0x44);
    g_scene1_combat_damage_base_idle2 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x12;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type       = 0x44;
    npc->attack_radius  = 3.0f;
    npc->combat_pose[0] = 0.0f;
    npc->combat_pose[2] = -0.5f;  /* front-hit angle */
    npc->npc_yaw        = 0.0f;
    /* phase 6 + subphase 1 force-arms 0x44 (so it doesn't disarm via
     * angle filter).  Pre-set both to allow reset to fire (npc_phase != 6 →
     * triggers reset; here we set npc_phase != 6 to verify reset). */
    npc->npc_phase    = 3;
    npc->npc_subphase = 5;
    npc->npc_phase_counter1 = 0xabcd;
    npc->npc_phase_counter2 = 0xdef0;
    /* Anchors out of range so only sub_iter 0 collides. */
    for (int i = 0; i < 8; i++) {
        npc->anchors[i][0] = 1000.0f;
        npc->anchors[i][1] = 1000.0f;
        npc->anchors[i][2] = 1000.0f;
    }
    *(float *)&slot[SCENE1_RECORDS_B_OFF_SCALE_X] = 1.0f;
    /* C8jb.6: gate off emit_kb_vector_write so its `npc_combat_phase_b40
     * == 0` clear of npc_phase doesn't overwrite the clamp's reset.  We
     * set npc_stun_b20 != 0 → gate_a fails; gate_b also fails (blocking=0),
     * so emit_kb_vector_write returns early without touching npc_phase. */
    npc->npc_stun_b20 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* Reset fired at sub_iter 0: */
    T_ASSERT_EQ_I(npc->npc_phase, 6);
    T_ASSERT_EQ_I(npc->npc_subphase, 0);
    T_ASSERT_EQ_I(npc->npc_phase_counter1, 0);
    T_ASSERT_EQ_I(npc->npc_phase_counter2, 0);
    return 0;
}

int test_combat_sm_phase_b_clamp_npc_44_slot_12_phase_6_no_reset(void)
{
    /* npc_phase already 6 → no reset (gate condition fails). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    install_unit_attrs(0x44);
    g_scene1_combat_damage_base_idle2 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x12;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = prep_npc_alive(0);
    npc->npc_type       = 0x44;
    npc->attack_radius  = 3.0f;
    npc->combat_pose[2] = -0.5f;
    npc->npc_yaw        = 0.0f;
    npc->npc_phase      = 6;        /* already 6 */
    npc->npc_subphase   = 1;        /* force-arm */
    npc->npc_phase_counter1 = 0xabcd;
    for (int i = 0; i < 8; i++) {
        npc->anchors[i][0] = 1000.0f;
        npc->anchors[i][1] = 1000.0f;
        npc->anchors[i][2] = 1000.0f;
    }
    *(float *)&slot[SCENE1_RECORDS_B_OFF_SCALE_X] = 1.0f;
    /* C8jb.6: gate off emit_kb_vector_write so its `npc_combat_phase_b40
     * == 0` clear of npc_phase_counter1 doesn't overwrite the preserved
     * value.  npc_stun_b20 != 0 → gate_a fails; gate_b also fails. */
    npc->npc_stun_b20 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* Reset did NOT fire — counters preserved. */
    T_ASSERT_EQ_I(npc->npc_phase_counter1, 0xabcd);
    return 0;
}

/* ─── Charge-attack disarm path ─────────────────────────────────── */

int test_combat_sm_phase_b_clamp_charge_attack_disarms_when_facing(void)
{
    /* npc.charge_flag != 0 AND npc.npc_b18_kill_age_out == 0 AND
     * |atan2(dx,dz) - npc_yaw + π wrap| < 0.3π → disarm. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    scene1_records_b_set_aux_482a51_hook(aux_482a51_capture);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    /* arm_collision_at uses pose=(0,0,-0.5) → ang ≈ 0 → within ±0.3π. */
    npc->charge_flag             = 1;
    npc->npc_b18_kill_age_out    = 0;
    npc->npc_yaw                 = 0.0f;
    npc->npc_phase               = 2;
    npc->npc_phase_counter1      = 0xdead;
    /* C8jb.6: gate off emit_kb_vector_write so its `npc_combat_phase_b40
     * == 0` clear of npc_phase + its aux_482a51(npc, 2) call don't
     * overwrite the clamp's npc_phase=4 / single arg2=4 call. */
    npc->npc_stun_b20 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* Disarm fires → damage = 0; npc_phase = 4; counter1 = 0; aux hook
     * called with arg2 = 4. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 0);
    T_ASSERT_EQ_I(npc->npc_phase, 4);
    T_ASSERT_EQ_I(npc->npc_phase_counter1, 0);
    T_ASSERT_EQ_I(g_aux_482a51_call_count, 1);
    T_ASSERT_EQ_I(g_aux_482a51_arg2_capture, 4);
    return 0;
}

int test_combat_sm_phase_b_clamp_charge_attack_skipped_when_kill_age_nonzero(void)
{
    /* npc_b18_kill_age_out != 0 → charge-attack path is gated off. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    scene1_records_b_set_aux_482a51_hook(aux_482a51_capture);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->charge_flag           = 1;
    npc->npc_b18_kill_age_out  = 42;  /* != 0 */
    npc->npc_yaw               = 0.0f;
    /* C8jb.6: gate off emit_kb_vector_write so it doesn't fire
     * aux_482a51(npc, 0) from the kill_age_out > 0 branch (line ~1213).
     * npc_stun_b20 != 0 → gate_a fails; gate_b also fails. */
    npc->npc_stun_b20 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* No charge-attack fire → no disarm → damage stays at 10. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 10);
    T_ASSERT_EQ_I(g_aux_482a51_call_count, 0);
    return 0;
}

/* ─── Final clamp: NPC type 5 keeps zero for negative ────────────── */

int test_combat_sm_phase_b_clamp_npc_type_5_negative_clamped_to_zero(void)
{
    /* NPC type 5 branch: negative damage → 0 (no min-1 floor). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_npc_type_attrs[5].attrs_int_38 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 5);
    npc->damage_quirk_mul_ab8 = 2.0f;
    /* second_damage = (int)(0/2 - 40/4) = -10.  NPC type 5 → clamp to 0. */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 0);
    return 0;
}

int test_combat_sm_phase_b_clamp_npc_type_5_positive_unchanged(void)
{
    /* Type 5 with positive damage: no min-1 floor + no RNG jitter
     * (skips the < 1 + RNG paths). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 10;
    g_rng_unsigned_return = 99;
    scene1_combat_set_rng_unsigned_hook(rng_unsigned_const);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 5);  /* NPC type 5 */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage=5.  NPC type 5 → no min-1, no jitter → 5. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);
    T_ASSERT_EQ_I(g_rng_unsigned_call_count, 0);  /* never called */
    return 0;
}

/* ─── Final clamp: < 1 floor + RNG jitter ─────────────────────────── */

int test_combat_sm_phase_b_clamp_floor_minimum_damage_to_1(void)
{
    /* damage = 0 (default everything) → < 1 → 1.  Then < 5 → += 0 & 1 = 0
     * → 1.  Test default front-hit + no scaling → damage = 0 → 1. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 1);
    return 0;
}

int test_combat_sm_phase_b_clamp_small_damage_rng_jitter_adds_bit(void)
{
    /* damage in [1, 5) → += rng & 1.  Inject rng = odd → +1. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 6;  /* second_damage=3 */
    g_rng_unsigned_return = 0xdeadbeef | 1;  /* low bit set */
    scene1_combat_set_rng_unsigned_hook(rng_unsigned_const);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* second_damage=3, < 5 → += 1 → 4. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 4);
    T_ASSERT_EQ_I(g_rng_unsigned_call_count, 1);
    return 0;
}

int test_combat_sm_phase_b_clamp_large_damage_rng_jitter_proportional(void)
{
    /* damage >= 5 → += rng % (damage / 5).  damage=15 → /5=3 → rng%3.
     * Inject rng=7 → 7%3=1 → damage=16. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 30;  /* second_damage=15 */
    g_rng_unsigned_return = 7;
    scene1_combat_set_rng_unsigned_hook(rng_unsigned_const);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* 15 + 7%3 = 15 + 1 = 16. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 16);
    return 0;
}

/* ─── local_1c bits reset between ticks ────────────────────────── */

int test_combat_sm_phase_b_clamp_local_1c_bits_reset_between_ticks(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_rear_hit(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits & 2, 2);

    /* Second tick: move NPC out of range — no collision → bits reset. */
    npc->combat_pose[0] = 100.0f;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_local_1c_bits, 0);
    return 0;
}

/* ════════════════════════════════════════════════════════════════════
 * C8jb.6 — Phase B hit-effect emit cluster tests
 * ════════════════════════════════════════════════════════════════════ */

/* Local convenience macros — t.h only provides T_ASSERT_EQ_I/_U; we add
 * float-near + integer not-equal as test-file-locals. */
#define T_ASSERT_EQ_F(a, b, eps) \
    T_ASSERT(fabsf((float)(a) - (float)(b)) < (float)(eps))
#define T_ASSERT_NE_I(a, b) do { \
    long long _a = (long long)(a); long long _b = (long long)(b); \
    if (_a == _b) T_FAIL("expected %s != %s (both %lld)", #a, #b, _a); \
} while (0)

/* ─── per-emit hook capture ──────────────────────────────────────── */

typedef struct {
    int     call_index;
    int32_t template;
    float   x, y, z;
    float   scale;
    int32_t param7;
} emit_spawn_record_t;

#define EMIT_SPAWN_CAP 16   /* up from 8 — C8jb.8c scatter emits 10/hit */
static emit_spawn_record_t g_emit_spawn_records[EMIT_SPAWN_CAP];
static int g_emit_spawn_count;

static void capture_emit_spawn(int call_index, int32_t template,
                               float x, float y, float z,
                               float scale, int32_t param7)
{
    if (g_emit_spawn_count < EMIT_SPAWN_CAP) {
        g_emit_spawn_records[g_emit_spawn_count].call_index = call_index;
        g_emit_spawn_records[g_emit_spawn_count].template   = template;
        g_emit_spawn_records[g_emit_spawn_count].x          = x;
        g_emit_spawn_records[g_emit_spawn_count].y          = y;
        g_emit_spawn_records[g_emit_spawn_count].z          = z;
        g_emit_spawn_records[g_emit_spawn_count].scale      = scale;
        g_emit_spawn_records[g_emit_spawn_count].param7     = param7;
        g_emit_spawn_count++;
    }
}

typedef struct {
    int32_t template;
    float   scale;
    int32_t override_dur;
    float   override_rot_y;
    int32_t mode;
} emit_overlay_record_t;

static emit_overlay_record_t g_emit_overlay_record;
static int g_emit_overlay_count;

static void capture_emit_overlay(int32_t template,
                                 float x, float y, float z,
                                 float scale,
                                 int32_t override_dur,
                                 float override_rot_y,
                                 int32_t mode)
{
    (void)x; (void)y; (void)z;
    g_emit_overlay_record.template       = template;
    g_emit_overlay_record.scale          = scale;
    g_emit_overlay_record.override_dur   = override_dur;
    g_emit_overlay_record.override_rot_y = override_rot_y;
    g_emit_overlay_record.mode           = mode;
    g_emit_overlay_count++;
}

static int32_t g_emit_se_last_id;
static int     g_emit_se_count;
static void capture_emit_se(int32_t se_id)
{
    g_emit_se_last_id = se_id;
    g_emit_se_count++;
}

typedef struct {
    int32_t npc_int;
    int32_t damage;
    int32_t armed;
    int32_t flag;
} emit_aux_42e791_record_t;

static emit_aux_42e791_record_t g_emit_aux_42e791_record;
static int g_emit_aux_42e791_calls;
static void capture_emit_aux_42e791(int32_t npc_int, int32_t damage,
                                    int32_t armed, int32_t flag)
{
    g_emit_aux_42e791_record.npc_int = npc_int;
    g_emit_aux_42e791_record.damage  = damage;
    g_emit_aux_42e791_record.armed   = armed;
    g_emit_aux_42e791_record.flag    = flag;
    g_emit_aux_42e791_calls++;
}

static void reset_combat_6_capture(void)
{
    memset(g_emit_spawn_records, 0, sizeof g_emit_spawn_records);
    g_emit_spawn_count = 0;
    memset(&g_emit_overlay_record, 0, sizeof g_emit_overlay_record);
    g_emit_overlay_count = 0;
    g_emit_se_last_id = 0;
    g_emit_se_count   = 0;
    memset(&g_emit_aux_42e791_record, 0, sizeof g_emit_aux_42e791_record);
    g_emit_aux_42e791_calls = 0;
    scene1_combat_set_emit_spawn_hook(capture_emit_spawn);
    scene1_combat_set_emit_overlay_spawn_hook(capture_emit_overlay);
    scene1_combat_set_emit_se_hook(capture_emit_se);
    scene1_combat_set_emit_aux_42e791_hook(capture_emit_aux_42e791);
}

/* ─── ret=1 contract: hit emit fires and SM returns 1 ─────────────── */

int test_combat_sm_phase_b_emit_returns_one_on_hit(void)
{
    /* Idle armed in-range collision: SM returns 1 + emit_count = 1. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_count, 1);
    /* Two spawn calls + one SE play. */
    T_ASSERT_EQ_I(g_emit_spawn_count, 2);
    T_ASSERT_EQ_I(g_emit_se_count, 1);
    return 0;
}

int test_combat_sm_phase_b_emit_breaks_iteration_on_first_hit(void)
{
    /* Two NPCs both in collision range — only the FIRST emits. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);
    arm_collision_at(slot, 1, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_collision_count, 1);
    return 0;
}

int test_combat_sm_phase_b_emit_observables_reset_at_tick_top(void)
{
    /* Run one hit-emit, then move out of range; observables reset. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_NE_I(g_scene1_combat_phase_b_emit_se_id, 0);

    /* Move out of range; observables reset. */
    npc->combat_pose[0] = 100.0f;
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_se_id, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_templates[0], 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_templates[1], 0);
    return 0;
}

/* ─── Spawn templates: armed + idle + is_player → (3, 0xf) ────────── */

int test_combat_sm_phase_b_emit_armed_idle_player_templates_3_f(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 1;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_spawn_count, 2);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template, 3);
    T_ASSERT_EQ_I(g_emit_spawn_records[1].template, 0xf);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_templates[0], 3);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_templates[1], 0xf);
    return 0;
}

/* ─── Spawn templates: disarmed → (0x29, 0x2a) ────────────────────── */

int test_combat_sm_phase_b_emit_disarmed_templates_29_2a(void)
{
    /* NPC 0x48 → always disarms; idle attacker → disarmed-idle branch. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x48);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_armed_collision_count, 0);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template, 0x29);
    T_ASSERT_EQ_I(g_emit_spawn_records[1].template, 0x2a);
    return 0;
}

/* ─── Spawn templates: armed + idle + !is_player → (1, 0x19) LAB_0043949c */

int test_combat_sm_phase_b_emit_armed_idle_npc_templates_1_19(void)
{
    /* Default OWNER_FLAG = 0 (not player) → LAB_0043949c → (1, 0x19). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template, 1);
    T_ASSERT_EQ_I(g_emit_spawn_records[1].template, 0x19);
    return 0;
}

/* ─── Spawn templates: !idle (state=3) → (1, 0x19) LAB_0043949c ───── */

int test_combat_sm_phase_b_emit_not_idle_templates_1_19(void)
{
    /* slot.FLAG_A = 3 (hit-recovery, still in Phase B gate). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 3;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 1;  /* is_player gate ignored */
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template, 1);
    T_ASSERT_EQ_I(g_emit_spawn_records[1].template, 0x19);
    return 0;
}

/* ─── OWNER_CEC overlay branch: 1 overlay_spawn, no scene1_spawns ─── */

int test_combat_sm_phase_b_emit_owner_cec_overlay_branch(void)
{
    /* idle + owner_a_cec != 0 → overlay_spawn(0x19, 1.0, -1, 0, 0). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    g_scene1_combat_owner_a_cec = 1;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]    = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]  = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_A] = (int32_t)(intptr_t)slot; /* != 0 */
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_spawn_count, 0);
    T_ASSERT_EQ_I(g_emit_overlay_count, 1);
    T_ASSERT_EQ_I(g_emit_overlay_record.template, 0x19);
    T_ASSERT_EQ_F(g_emit_overlay_record.scale, 1.0f, 1e-6f);
    T_ASSERT_EQ_I(g_emit_overlay_record.override_dur, -1);
    T_ASSERT_EQ_F(g_emit_overlay_record.override_rot_y, 0.0f, 1e-6f);
    T_ASSERT_EQ_I(g_emit_overlay_record.mode, 0);
    return 0;
}

/* ─── Extra spawn: slot.TYPE 4/0x52 + damage > 0 → 3rd spawn (template 0x98) */

int test_combat_sm_phase_b_emit_type_4_damage_extra_spawn_0x98(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x04;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_spawn_count, 3);
    T_ASSERT_EQ_I(g_emit_spawn_records[2].call_index, 2);
    T_ASSERT_EQ_I(g_emit_spawn_records[2].template, 0x98);
    return 0;
}

int test_combat_sm_phase_b_emit_type_52_damage_extra_spawn_0x98(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x52;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_spawn_count, 3);
    T_ASSERT_EQ_I(g_emit_spawn_records[2].template, 0x98);
    return 0;
}

int test_combat_sm_phase_b_emit_type_4_zero_damage_no_extra_spawn(void)
{
    /* damage_out = 0 (no base damage configured) → no 3rd spawn. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    /* leave damage_base_idle2 = 0 → final damage will be RNG-jittered up
     * to 1 by the clamp.  Use NPC type 5 to keep the negative damage
     * unchanged → 0. */
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x04;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 5);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 0);
    /* Only 2 spawns + the extra spawn is SKIPPED because damage <= 0. */
    T_ASSERT_EQ_I(g_emit_spawn_count, 2);
    return 0;
}

/* ─── SE branch table ────────────────────────────────────────────── */

int test_combat_sm_phase_b_emit_se_type_8_picks_0x179(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x08;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x179);
    return 0;
}

int test_combat_sm_phase_b_emit_se_type_53_picks_0x2af(void)
{
    /* Slot.TYPE 0x53 → heavy-attack short-circuit fires in C8jb.5a
     * AND C8jb.6 still runs the emit with SE 0x2af.  C8jb.5a sets
     * damage = 0 + kb_strength = 0, but the emit cluster fires
     * unconditionally per in-range collision. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x53;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x2af);
    return 0;
}

int test_combat_sm_phase_b_emit_se_disarmed_picks_0x167(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x48);  /* NPC 0x48 always disarms */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x167);
    return 0;
}

int test_combat_sm_phase_b_emit_se_armed_idle_player_picks_0x148(void)
{
    /* Armed + idle + IS_PLAYER, no other matching type → SE 0x148. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 1;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x148);
    return 0;
}

int test_combat_sm_phase_b_emit_se_type_5b_picks_default_0x13f(void)
{
    /* armed + idle + !is_player + slot.TYPE 0x5b → default 0x13f. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x5b;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x13f);
    return 0;
}

int test_combat_sm_phase_b_emit_se_type_5c_picks_0x2a7(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x5c;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x2a7);
    return 0;
}

int test_combat_sm_phase_b_emit_se_cluster_2_3_6d_6f_70_picks_0x153(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x6d;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x153);
    return 0;
}

static uint32_t g_emit_test_rng_value;
static uint32_t emit_test_rng_const(void)
{
    return g_emit_test_rng_value;
}

int test_combat_sm_phase_b_emit_se_type_85_rng_bit0_set_picks_default(void)
{
    /* {0x85, 0x86, 0x87}: rng & 1 → 0x13f else 0x2a7.  rng=1 (bit0 set)
     * → 0x13f. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    g_emit_test_rng_value = 1;
    scene1_combat_set_rng_unsigned_hook(emit_test_rng_const);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x85;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x13f);
    return 0;
}

int test_combat_sm_phase_b_emit_se_type_86_rng_bit0_clear_picks_0x2a7(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    g_emit_test_rng_value = 0;
    scene1_combat_set_rng_unsigned_hook(emit_test_rng_const);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x86;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_se_last_id, 0x2a7);
    return 0;
}

/* ─── Per-TYPE kb_strength scaling ───────────────────────────────── */

int test_combat_sm_phase_b_emit_kb_strength_type_86_zeros(void)
{
    /* slot.TYPE 0x86 → kb_strength = 0 + npc.npc_kb_type_ba0 = 0x1e. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x86;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 0;
    /* Velocity makes kb_strength non-zero in the prologue. */
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X] = 0.5f;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_F(g_scene1_combat_phase_b_kb_strength, 0.0f, 1e-6f);
    T_ASSERT_EQ_I(npc->npc_kb_type_ba0, 0x1e);
    return 0;
}

int test_combat_sm_phase_b_emit_kb_strength_type_0x82_zero_and_field_28(void)
{
    /* slot.TYPE 0x82 → kb=0, npc_field_28=1, npc_kbcd_440=0x3c. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x82;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X] = 0.5f;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_F(g_scene1_combat_phase_b_kb_strength, 0.0f, 1e-6f);
    T_ASSERT_EQ_I(npc->npc_field_28, 1);
    T_ASSERT_EQ_I(npc->npc_kbcd_440, 0x3c);
    return 0;
}

int test_combat_sm_phase_b_emit_kbcd_default_28(void)
{
    /* slot.TYPE 0x10 → kb_strength scaler writes kbcd=0x28; KB-write
     * gate must pass (stun=0, armed, block_dodge!=1) for the value to
     * survive — else the engine's gate-failed `else` branch zeroes it. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    /* Default gate-pass state: npc_stun_b20=0, npc_block_dodge_b54=0. */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(npc->npc_kbcd_440, 0x28);
    return 0;
}

int test_combat_sm_phase_b_emit_kbcd_type_60_uses_3c(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x60;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(npc->npc_kbcd_440, 0x3c);
    return 0;
}

int test_combat_sm_phase_b_emit_kbcd_zeroed_when_kb_gate_fails(void)
{
    /* Engine 0x4393b9: when KB-write gate fails (npc_stun_b20 > 0 in
     * this case), npc_kbcd_440 is set to 0 — overwriting the 0x28
     * value written by the scale-kb body. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_stun_b20 = 5;  /* gate fails */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(npc->npc_kbcd_440, 0);
    return 0;
}

/* ─── KB vector write to NPC ──────────────────────────────────────── */

int test_combat_sm_phase_b_emit_kb_vector_written_on_armed_hit(void)
{
    /* Armed + npc_stun_b20=0 + block_dodge_b54!=1 → KB vec written.
     * Default branch: kb_vec = (kb_strength * vel_x, 0.3, kb_strength * vel_z). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X] = 1.0f;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_Z] = 0.0f;
    /* C8jb.5a kb_strength = 0.7 / sqrt(1²+0²) = 0.7. */
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[0], 0.7f, 1e-6f);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[1], 0.3f, 1e-6f);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[2], 0.0f, 1e-6f);
    return 0;
}

int test_combat_sm_phase_b_emit_kb_vector_skipped_when_stunned(void)
{
    /* npc_stun_b20 > 0 → gate fails → npc_kbcd_440 = 0, kb_vec unchanged. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X] = 1.0f;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_stun_b20 = 5;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[0], 0.0f, 1e-6f);
    T_ASSERT_EQ_I(npc->npc_kbcd_440, 0);   /* engine "else" branch */
    return 0;
}

int test_combat_sm_phase_b_emit_kb_vector_blocking_uses_damage_and_0_5(void)
{
    /* npc_blocking_b98=1 + (hp - damage > 0) → kb_vec uses (damage * vel,
     * 0.5, damage * vel).  Damage from C8jb.5b base_idle2=10 → second=5. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 10;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X] = 2.0f;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_Z] = 0.0f;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_blocking_b98 = 1;
    npc->npc_hp_curr_42c  = 100.0f;  /* hp - 5 > 0 → blocking branch */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    /* damage_out after clamps = 5 + RNG-jitter (default 0 → +1 → 6) ?
     * Actually default rng_unsigned is 0u → damage<5 path → +(0u & 1u) =
     * +0 — but damage is 5 so it hits the >= 5 branch → += rng % (5/5) =
     * rng % 1 = 0.  damage stays 5. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_damage_out, 5);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[0], 10.0f, 1e-6f);  /* 5 * 2.0 */
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[1], 0.5f, 1e-6f);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[2], 0.0f, 1e-6f);
    return 0;
}

int test_combat_sm_phase_b_emit_kb_vector_b18_kill_age_clears(void)
{
    /* npc_b18_kill_age_out > 0 → clear kb_vec + aux_482a51(npc, 0). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;
    scene1_records_b_set_aux_482a51_hook(aux_482a51_capture);

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X] = 1.0f;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_b18_kill_age_out = 5;
    npc->npc_kb_vec_3fc[0] = 99.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[0], 0.0f, 1e-6f);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[1], 0.0f, 1e-6f);
    T_ASSERT_EQ_F(npc->npc_kb_vec_3fc[2], 0.0f, 1e-6f);
    /* aux_482a51 called with arg2 = 0 (b18-kill-age branch). */
    T_ASSERT_EQ_I(g_aux_482a51_arg2_capture, 0);
    return 0;
}

/* ─── DAT writes by emit cluster ─────────────────────────────────── */

int test_combat_sm_phase_b_emit_writes_dat_0438b904_b908(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_dat_0438b908, 0xb4);
    /* dat_0438b904 = (dist - slot_reach); precise value depends on the
     * arm_collision_at pose (0, 0, -0.5) and slot reach 1.0:
     * dist = 0.5, dist - 1 = -0.5. */
    T_ASSERT_EQ_F(g_scene1_combat_dat_0438b904, -0.5f, 1e-6f);
    return 0;
}

int test_combat_sm_phase_b_emit_dat_06a46f94_is_min_damage_hp(void)
{
    /* dat_06a46f94 = min(damage, (int)npc.npc_hp_curr_42c). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;  /* second_damage=10 → damage=10 */

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_hp_curr_42c = 3.0f;  /* HP < damage → dat = 3 */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_dat_06a46f94, 3);
    return 0;
}

int test_combat_sm_phase_b_emit_dat_06a46f94_uses_damage_when_hp_high(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_hp_curr_42c = 100.0f;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_dat_06a46f94, 10);  /* damage */
    return 0;
}

int test_combat_sm_phase_b_emit_idle_player_dat_0438bed8_is_8(void)
{
    /* idle + is_player → dat_0438bed8 = 8 (overrides the per-TYPE 4). */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]       = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A]     = 0;
    slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] = 1;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_dat_0438bed8, 8);
    return 0;
}

/* ─── FUN_0042e791 hook (gated) ──────────────────────────────────── */

int test_combat_sm_phase_b_emit_aux_42e791_fires_when_gate_open(void)
{
    /* slot.TYPE != 0x53 + npc.npc_extra_gate_428 == 1 → hook called. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_extra_gate_428 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_aux_42e791_calls, 1);
    T_ASSERT_EQ_I(g_scene1_combat_emit_aux_42e791_call_count, 1);
    /* armed = 1 → engine local_18 = 0 → 3rd arg = 0 (armed). */
    T_ASSERT_EQ_I(g_emit_aux_42e791_record.armed, 0);
    T_ASSERT_EQ_I(g_emit_aux_42e791_record.flag, 0);
    return 0;
}

int test_combat_sm_phase_b_emit_aux_42e791_skipped_when_gate_closed(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_extra_gate_428 = 0;  /* gate closed */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_aux_42e791_calls, 0);
    return 0;
}

int test_combat_sm_phase_b_emit_aux_42e791_skipped_for_type_53(void)
{
    /* slot.TYPE == 0x53 → gate fails even with npc_extra_gate_428 == 1. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x53;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);
    npc->npc_extra_gate_428 = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_emit_aux_42e791_calls, 0);
    return 0;
}

/* ─── npc.npc_postdmg_ab4 = 1.0 at end of emit ───────────────────── */

int test_combat_sm_phase_b_emit_writes_postdmg_ab4_to_one(void)
{
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    scene1_people_entry_t *npc = arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_F(npc->npc_postdmg_ab4, 1.0f, 1e-6f);
    return 0;
}

/* ─── Spawn pose midpoint ────────────────────────────────────────── */

int test_combat_sm_phase_b_emit_spawn_pose_midpoint(void)
{
    /* Engine pose: x = npc.x - fVar3 * dx / dist; y = dy * 0.85 + slot.y;
     * z = npc.z - fVar3 * dz / dist.  With NPC at (0,0,-0.5), slot at
     * (0,0,0), reach=1, dist_mul=1, radius_mul=1, attack_radius=3:
     * dx=0, dz=-0.5, dist=0.5, fVar3=3.
     * x = 0 - (3 * 0)/0.5 = 0;  y = 0*0.85 + 0 = 0;
     * z = -0.5 - (3 * -0.5)/0.5 = -0.5 + 3 = 2.5. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_F(g_scene1_combat_phase_b_emit_pose[0],  0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_scene1_combat_phase_b_emit_pose[1],  0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_scene1_combat_phase_b_emit_pose[2],  2.5f, 1e-6f);
    /* Spawn calls share the same pose. */
    T_ASSERT_EQ_F(g_emit_spawn_records[0].x, 0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].z, 2.5f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].scale, 0.2f, 1e-6f);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].param7, 1);
    return 0;
}

/* ─── C8jb.7 — Phase C projectile-table scan ─────────────────────────── */

static int g_phase_c_visit_indices[SCENE1_PROJ_COUNT];
static int g_phase_c_visit_count;

static int g_phase_c_hit_indices[SCENE1_PROJ_COUNT];
static int g_phase_c_hit_call_count;

static void capture_phase_c_visit_hook(int proj_index)
{
    if (g_phase_c_visit_count < SCENE1_PROJ_COUNT) {
        g_phase_c_visit_indices[g_phase_c_visit_count++] = proj_index;
    }
}

static void capture_phase_c_hit_hook(int proj_index)
{
    if (g_phase_c_hit_call_count < SCENE1_PROJ_COUNT) {
        g_phase_c_hit_indices[g_phase_c_hit_call_count++] = proj_index;
    }
}

static void reset_combat_7_capture(void)
{
    /* Zero, then sentinel-fill TYPE=-1.  Engine relies on an (unported)
     * init routine to seed the projectile table this way; without the
     * sentinel, BSS-zero records pass the 10-entry skip cascade (TYPE=0
     * and AUX=0 are NOT in the disqualifying set) and would all fire
     * AABB hits whenever the slot is at origin.  Sentinel-filling here
     * isolates each test to the projectiles it explicitly configures. */
    memset(g_scene1_projectiles, 0, sizeof g_scene1_projectiles);
    for (int i = 0; i < SCENE1_PROJ_COUNT; i++) {
        g_scene1_projectiles[i * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_TYPE] = -1;
    }
    memset(g_scene1_combat_proj_type_attrs, 0,
           sizeof g_scene1_combat_proj_type_attrs);
    g_scene1_combat_phase_c_visit_count = 0;
    g_scene1_combat_phase_c_hit_count   = 0;
    g_phase_c_visit_count = 0;
    g_phase_c_hit_call_count = 0;
    memset(g_phase_c_visit_indices, 0, sizeof g_phase_c_visit_indices);
    memset(g_phase_c_hit_indices,   0, sizeof g_phase_c_hit_indices);
    scene1_combat_set_phase_c_visit_hook(NULL);
    scene1_combat_set_phase_c_hit_hook(NULL);
}

/* Set the per-projectile-type radii at index `type`. */
static void install_proj_radii(int type, float x_radius, float z_radius)
{
    g_scene1_combat_proj_type_attrs[type].x_radius = x_radius;
    g_scene1_combat_proj_type_attrs[type].z_radius = z_radius;
}

/* Configure projectile `idx` with TYPE/AUX/POS/SCALE.  Other fields stay
 * BSS-zero. */
static int32_t *setup_proj(int idx, int32_t type, int32_t aux,
                           float x, float y, float z, float scale)
{
    int32_t *proj = &g_scene1_projectiles[idx * SCENE1_PROJ_STRIDE];
    proj[SCENE1_PROJ_OFF_TYPE]  = type;
    proj[SCENE1_PROJ_OFF_AUX]   = aux;
    *(float *)&proj[SCENE1_PROJ_OFF_POS_X] = x;
    *(float *)&proj[SCENE1_PROJ_OFF_POS_Y] = y;
    *(float *)&proj[SCENE1_PROJ_OFF_POS_Z] = z;
    *(float *)&proj[SCENE1_PROJ_OFF_SCALE] = scale;
    return proj;
}

/* Prep a slot susceptible to incoming hits (FLAG_A == 0) at the origin,
 * with a known SEQ_ID and reach.  Caller can mutate. */
static int32_t *target_slot_at(int32_t seq_id, float px, float py, float pz,
                               float reach)
{
    int32_t *slot = some_slot();
    memset(slot, 0, SCENE1_RECORDS_B_STRIDE * sizeof(int32_t));
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10; /* arbitrary non-sound-eligible */
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;    /* idle target */
    slot[SCENE1_RECORDS_B_OFF_SEQ_ID] = seq_id;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_POS_X] = px;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y] = py;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z] = pz;
    *(float *)&slot[SCENE1_RECORDS_B_OFF_DRAG]  = reach;
    return slot;
}

/* ─── Phase C outer gate ─────────────────────────────────────────────── */

int test_combat_sm_phase_c_skipped_when_state_is_1(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   0);
    return 0;
}

int test_combat_sm_phase_c_skipped_when_state_is_3(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 3;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   0);
    return 0;
}

int test_combat_sm_phase_c_runs_when_state_is_0(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;

    /* Phase C runs and fires a hit (visit + hit counters go to 1). */
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   1);
    return 0;
}

int test_combat_sm_phase_c_runs_when_state_is_2(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 2;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   1);
    return 0;
}

/* ─── Phase C skip cascade (TYPE + AUX) ──────────────────────────────── */

int test_combat_sm_phase_c_skips_type_minus_one(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    setup_proj(0, -1, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    install_proj_radii(0, 1000.0f, 1000.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_c_skips_disqualifying_types(void)
{
    static const int32_t skip_types[] = {
        0x16, 0x1e, 9, 0xa, 0x12, 0x13, 0xc, 0xd, 0xb, 8
    };
    for (size_t k = 0; k < sizeof skip_types / sizeof skip_types[0]; k++) {
        reset_combat_state();
        reset_combat_7_capture();
        setup_proj(0, skip_types[k], 0, 0.0f, 0.0f, 0.0f, 1.0f);
        install_proj_radii(skip_types[k], 1000.0f, 1000.0f);
        int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

        T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
        T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    }
    return 0;
}

int test_combat_sm_phase_c_skips_aux_three(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 3, 0.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_c_skips_aux_seven(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 7, 0.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_c_skips_aux_nonzero_other_than_three_seven(void)
{
    /* Engine final gate: proj_aux must be == 0.  Values like 1, 2, 4, 5,
     * 6, 8 also skip even though they're not in the explicit 3/7 list. */
    static const int32_t aux_vals[] = {1, 2, 4, 5, 6, 8, 0x80, -1};
    for (size_t k = 0; k < sizeof aux_vals / sizeof aux_vals[0]; k++) {
        reset_combat_state();
        reset_combat_7_capture();
        install_proj_radii(5, 1000.0f, 1000.0f);
        setup_proj(0, 5, aux_vals[k], 0.0f, 0.0f, 0.0f, 1.0f);
        int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

        T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
        T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    }
    return 0;
}

int test_combat_sm_phase_c_admits_aux_zero(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 1);
    return 0;
}

/* ─── Phase C subtype/hit-history filter ──────────────────────────────── */

int test_combat_sm_phase_c_skips_when_seq_id_in_ring(void)
{
    /* slot.SEQ_ID matches an entry in proj.RING — skip without visit. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    int32_t *proj = setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    proj[SCENE1_PROJ_OFF_RING + 3] = 0x1234;
    int32_t *slot = target_slot_at(0x1234, 0, 0, 0, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    return 0;
}

int test_combat_sm_phase_c_skips_match_at_each_ring_slot(void)
{
    /* Match at any of the 10 ring slots should skip. */
    for (int idx = 0; idx < 10; idx++) {
        reset_combat_state();
        reset_combat_7_capture();
        install_proj_radii(5, 1000.0f, 1000.0f);
        int32_t *proj = setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
        proj[SCENE1_PROJ_OFF_RING + idx] = 0xDEAD0000 + idx;
        int32_t *slot = target_slot_at(0xDEAD0000 + idx, 0, 0, 0, 1.0f);

        T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
        T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    }
    return 0;
}

int test_combat_sm_phase_c_admits_when_seq_id_absent(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    int32_t *proj = setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    for (int k = 0; k < 10; k++) proj[SCENE1_PROJ_OFF_RING + k] = 0x9000 + k;
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 1);
    return 0;
}

/* ─── Phase C iteration order + counter ──────────────────────────────── */

int test_combat_sm_phase_c_visit_count_zero_with_sentinel_table(void)
{
    /* Sentinel-init projectile table (TYPE=-1) + BSS-zero radii: every
     * record is caught by the cascade's first gate (TYPE==-1 skip) and
     * none reach the AABB.  This matches the engine's expected production
     * state after the (unported) init routine seeds the table. */
    reset_combat_state();
    reset_combat_7_capture();
    int32_t *slot = target_slot_at(0x1111, 100.0f, 0, 100.0f, 0.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   0);
    return 0;
}

int test_combat_sm_phase_c_visit_hook_called_in_index_order(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    /* Live records at indices 5, 7, 12; others remain sentinel (TYPE=-1).
     * All three pass the cascade + filter; AABB miss (radii=0) so no hit
     * fires and iteration runs to completion.  Hook receives indices in
     * ascending order. */
    install_proj_radii(5, 0.0f, 0.0f);
    setup_proj(5,  5, 0, 100.0f, 0, 100.0f, 1.0f);
    setup_proj(7,  5, 0, 100.0f, 0, 100.0f, 1.0f);
    setup_proj(12, 5, 0, 100.0f, 0, 100.0f, 1.0f);
    scene1_combat_set_phase_c_visit_hook(capture_phase_c_visit_hook);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_phase_c_visit_count, 3);
    T_ASSERT_EQ_I(g_phase_c_visit_indices[0], 5);
    T_ASSERT_EQ_I(g_phase_c_visit_indices[1], 7);
    T_ASSERT_EQ_I(g_phase_c_visit_indices[2], 12);
    return 0;
}

/* ─── Phase C AABB gates ──────────────────────────────────────────────── */

int test_combat_sm_phase_c_hits_when_aabb_passes(void)
{
    /* Slot at (0,0,0) reach=0.5, projectile at (1,0,0) scale=1, type 5
     * radii (3, 3).  dx=1, dz=0 → jitter dz to 0.01 (since not (dx==0 &&
     * dz==0)).  Actually dx=1, dz=0 doesn't trigger jitter because the
     * "both zero" check is AND.  dist = sqrt(1+0) = 1.  dist-reach = 0.5.
     * x_radius = 3 * 1 = 3.  0.5 < 3 ✓.  dy = 0 < reach=0.5 ✓.
     * z_radius = 3.  -(3+0.5) = -3.5 < 0 ✓.  HIT. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 1);
    return 0;
}

int test_combat_sm_phase_c_misses_when_dist_too_far(void)
{
    /* Same as above but proj at (100, 0, 0): dist=100, dist-reach=99.5,
     * x_radius=3 → 99.5 < 3 false → miss. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 100.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 0);
    return 0;
}

int test_combat_sm_phase_c_misses_when_dy_too_high(void)
{
    /* dy = proj_y - slot_y = 1.0 - 0 = 1.0; slot_reach = 0.5.
     * Gate `dy < slot_reach` → 1.0 < 0.5 false → miss. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 1.0f, 1.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 0);
    return 0;
}

int test_combat_sm_phase_c_misses_when_dy_too_low(void)
{
    /* dy = -10; -(z_radius + reach) = -(3+0.5) = -3.5.
     * Gate `-(z+reach) < dy` → -3.5 < -10 false → miss. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 1.0f, -10.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 0);
    return 0;
}

int test_combat_sm_phase_c_dy_dz_jitter(void)
{
    /* slot at (5,0,5), proj at (5,0,5) → dx=0, dz=0 → jitter dz to 0.01.
     * dist = sqrt(0 + 0.0001) ≈ 0.01.  dist - reach (0.5) = -0.49.
     * x_radius = 3 → -0.49 < 3 ✓.  Hit. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 5.0f, 0.0f, 5.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 5.0f, 0.0f, 5.0f, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 1);
    return 0;
}

int test_combat_sm_phase_c_proj_scale_scales_radii(void)
{
    /* type-5 x_radius = 1.0, z_radius = 1.0; proj_scale = 10 →
     * effective radii = 10.  Slot at (0,0,0), proj at (5,0,0) reach=0.1
     * (need >0 because gate is strict `dy < reach`).
     * dist = 5, dist-reach=4.9, x_radius_scaled = 10 → 4.9 < 10 ✓ HIT.
     *
     * Same but proj_scale = 0.1 → effective radii = 0.1.  4.9 < 0.1
     * false → miss. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1.0f, 1.0f);
    setup_proj(0, 5, 0, 5.0f, 0.0f, 0.0f, 10.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 0.1f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 1);

    /* Now retest with scale=0.1. */
    reset_combat_7_capture();
    install_proj_radii(5, 1.0f, 1.0f);
    setup_proj(0, 5, 0, 5.0f, 0.0f, 0.0f, 0.1f);
    slot = target_slot_at(0x2222, 0, 0, 0, 0.1f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 0);
    return 0;
}

int test_combat_sm_phase_c_per_type_radii_used(void)
{
    /* Two projectiles, types 5 and 7.  Type-5 radii=3, type-7 radii=0.
     * Both at same pose → only the type-5 hits. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    install_proj_radii(7, 0.0f, 0.0f);
    setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    setup_proj(1, 7, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    /* Only one hit (Phase C breaks after first); but the loop runs proj
     * 0 first and that one passes → exactly 1 hit. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 1);
    return 0;
}

/* ─── Phase C on-hit side effects ─────────────────────────────────────── */

int test_combat_sm_phase_c_on_hit_ring_bump(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    int32_t *proj = setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    proj[SCENE1_PROJ_OFF_CURSOR] = 0;
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 1);
    T_ASSERT_EQ_I(proj[SCENE1_PROJ_OFF_RING + 0], 0x9999);
    T_ASSERT_EQ_I(proj[SCENE1_PROJ_OFF_CURSOR],   1);
    return 0;
}

int test_combat_sm_phase_c_on_hit_cursor_wraps(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    int32_t *proj = setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    proj[SCENE1_PROJ_OFF_CURSOR] = 9;  /* about to wrap */
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(proj[SCENE1_PROJ_OFF_RING + 9], 0x9999);
    T_ASSERT_EQ_I(proj[SCENE1_PROJ_OFF_CURSOR],   0);  /* wrapped */
    return 0;
}

int test_combat_sm_phase_c_on_hit_state_set_to_five(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    int32_t *proj = setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    proj[SCENE1_PROJ_OFF_STATE] = -1;
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(proj[SCENE1_PROJ_OFF_STATE], 5);
    return 0;
}

int test_combat_sm_phase_c_on_hit_sound_flag_for_type_2(void)
{
    /* slot.TYPE == 2 → set bit 1 (= 2) in dat_056da1b8. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 2;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_dat_056da1b8 & 2, 2);
    return 0;
}

int test_combat_sm_phase_c_on_hit_sound_flag_for_sound_eligible_types(void)
{
    static const int32_t types[] = {2, 0x54, 0x6d, 0x6f, 0x70};
    for (size_t k = 0; k < sizeof types / sizeof types[0]; k++) {
        reset_combat_state();
        reset_combat_7_capture();
        install_proj_radii(5, 3.0f, 3.0f);
        setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
        int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = types[k];

        T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
        T_ASSERT_EQ_I(g_scene1_combat_dat_056da1b8 & 2, 2);
    }
    return 0;
}

int test_combat_sm_phase_c_on_hit_no_sound_flag_for_other_types(void)
{
    static const int32_t types[] = {1, 3, 4, 5, 0x10, 0x53, 0x6e, 0x71};
    for (size_t k = 0; k < sizeof types / sizeof types[0]; k++) {
        reset_combat_state();
        reset_combat_7_capture();
        install_proj_radii(5, 3.0f, 3.0f);
        setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
        int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = types[k];

        T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
        T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 1);
        T_ASSERT_EQ_I(g_scene1_combat_dat_056da1b8 & 2, 0);
    }
    return 0;
}

int test_combat_sm_phase_c_on_hit_or_preserves_existing_bits(void)
{
    /* The OR shouldn't clear bits already set by earlier code. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    g_scene1_combat_dat_056da1b8 = 0x55;  /* bits 0,2,4,6 */
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 2;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_dat_056da1b8, 0x57);  /* + bit 1 */
    return 0;
}

/* ─── Phase C loop break + hit-once semantics ─────────────────────────── */

int test_combat_sm_phase_c_breaks_on_first_hit(void)
{
    /* Two projectiles, both at hit-range.  Only the first should fire. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    int32_t *proj0 = setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    int32_t *proj1 = setup_proj(1, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count, 1);
    T_ASSERT_EQ_I(proj0[SCENE1_PROJ_OFF_STATE], 5);
    T_ASSERT_EQ_I(proj1[SCENE1_PROJ_OFF_STATE], 0);   /* untouched */
    return 0;
}

int test_combat_sm_phase_c_hit_hook_fires_with_index(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    /* Records 0..3 are sentinel (TYPE=-1, skipped by cascade); record 4
     * is the only live one + AABB-eligible.  Hook fires with index 4. */
    setup_proj(4, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    scene1_combat_set_phase_c_hit_hook(capture_phase_c_hit_hook);
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_phase_c_hit_call_count, 1);
    T_ASSERT_EQ_I(g_phase_c_hit_indices[0], 4);
    return 0;
}

/* ─── Phase C interaction with Phase B ────────────────────────────────── */

int test_combat_sm_phase_c_skipped_when_phase_b_fired(void)
{
    /* When Phase B returns 1 (hit fired), Phase C should NOT run. */
    reset_combat_state();
    reset_combat_5b_capture();
    reset_combat_5c_capture();
    reset_combat_6_capture();
    reset_combat_7_capture();
    g_scene1_combat_damage_base_idle2 = 20;

    /* Phase B fires via arm_collision_at (helper from earlier in file). */
    int32_t *slot = attacker_slot_at(0, 0, 0, 1.0f);
    slot[SCENE1_RECORDS_B_OFF_TYPE]   = 0x10;
    slot[SCENE1_RECORDS_B_OFF_FLAG_A] = 0;
    arm_collision_at(slot, 0, 0x10);

    /* Also set up a Phase C hit-eligible projectile.  If Phase C ran,
     * its visit_count would increment. */
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);

    /* Phase B returns 1; Phase C should not have run. */
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   0);
    return 0;
}

int test_combat_sm_phase_c_runs_when_phase_b_returns_zero(void)
{
    /* Phase B doesn't fire (player HP = 0 → outer gate fails); Phase C
     * runs and fires a hit. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 0.5f);
    g_scene1_combat_player_hp = 0.0f;  /* Phase B disabled */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   1);
    return 0;
}

int test_combat_sm_phase_c_skipped_when_phase_a_gates(void)
{
    /* Any Phase A gate fail short-circuits before Phase C. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 1000.0f, 1000.0f);
    setup_proj(0, 5, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);
    g_scene1_combat_world_pause = 1;  /* Phase A short-circuits */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   0);
    return 0;
}

/* ─── Phase C observable resets at tick top ───────────────────────────── */

int test_combat_sm_phase_c_counters_reset_between_ticks(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);

    /* Tick 1: hit fires. */
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   1);

    /* Tick 2 with the same slot.SEQ_ID: the projectile's ring now
     * contains 0x9999, so the subtype filter skips → visit_count back
     * to 0.  Hit_count also 0.  This proves the counters are reset
     * at tick top. */
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   0);
    return 0;
}

int test_combat_sm_phase_c_counters_reset_on_phase_a_short_circuit(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    g_scene1_combat_phase_c_visit_count = 99;
    g_scene1_combat_phase_c_hit_count   = 77;
    /* Phase A short-circuit returns 0 BEFORE the reset (engine semantics
     * — see C8jb.5/6 prior tests for the analogous behavior on Phase B).
     * Counters keep their pre-call value. */
    g_scene1_combat_subphase = 1;
    int32_t *slot = target_slot_at(0x1111, 0, 0, 0, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    /* Confirm Phase A short-circuit didn't reset the counters. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 99);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   77);
    return 0;
}

/* ─── Phase C hook install/uninstall round-trip ───────────────────────── */

int test_combat_sm_phase_c_visit_hook_install_returns_previous(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    scene1_combat_phase_c_visit_fn prev1 =
        scene1_combat_set_phase_c_visit_hook(capture_phase_c_visit_hook);
    T_ASSERT(prev1 == NULL);
    scene1_combat_phase_c_visit_fn prev2 =
        scene1_combat_set_phase_c_visit_hook(NULL);
    T_ASSERT(prev2 == capture_phase_c_visit_hook);
    return 0;
}

int test_combat_sm_phase_c_hit_hook_install_returns_previous(void)
{
    reset_combat_state();
    reset_combat_7_capture();
    scene1_combat_phase_c_hit_fn prev1 =
        scene1_combat_set_phase_c_hit_hook(capture_phase_c_hit_hook);
    T_ASSERT(prev1 == NULL);
    scene1_combat_phase_c_hit_fn prev2 =
        scene1_combat_set_phase_c_hit_hook(NULL);
    T_ASSERT(prev2 == capture_phase_c_hit_hook);
    return 0;
}

int test_combat_sm_phase_c_hooks_nullable(void)
{
    /* NULL-hook path must not crash; only counters increment. */
    reset_combat_state();
    reset_combat_7_capture();
    install_proj_radii(5, 3.0f, 3.0f);
    setup_proj(0, 5, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    scene1_combat_set_phase_c_visit_hook(NULL);
    scene1_combat_set_phase_c_hit_hook(NULL);
    int32_t *slot = target_slot_at(0x9999, 0, 0, 0, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_visit_count, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,   1);
    return 0;
}

/* ─── C8jb.8a — Phase C TYPE-dispatched sound + spawn cluster ──────── */

/* Set the projectile's per-emit OFFSET_Y bias (proj[-2] field). */
static void set_proj_offset_y(int idx, float offset_y)
{
    int32_t *proj = &g_scene1_projectiles[idx * SCENE1_PROJ_STRIDE];
    *(float *)&proj[SCENE1_PROJ_OFF_OFFSET_Y] = offset_y;
}

/* Setup helper: prep one projectile of TYPE `type` at fixed pose + reach
 * + sentinel-fill the rest, then arm radii so a target at the origin
 * (with reach 10.0 — big enough for all the test poses below to land
 * an AABB hit; the dy < reach gate matters too) will land an AABB hit.
 * Sets LIFETIME to -1 (sentinel "no countdown") so C8jb.8b's path-a
 * applies: TYPE in {2, 3} → no extra spawn (C8jb.8a leaf-only); other
 * TYPEs → extra spawn template 1.  Tests inspect g_emit_spawn_records[0]
 * (the FIRST spawn from C8jb.8a's hook chain) for template/pose
 * verification, since the observable `g_scene1_combat_phase_c_emit_template`
 * holds the LAST spawn (overwritten by C8jb.8b when applicable). */
static int32_t *setup_phase_c_hit(int32_t proj_type,
                                  float proj_x, float proj_y, float proj_z,
                                  float proj_scale, float offset_y)
{
    install_proj_radii(proj_type & 0xff, 3.0f, 3.0f);
    setup_proj(0, proj_type, 0, proj_x, proj_y, proj_z, proj_scale);
    set_proj_offset_y(0, offset_y);
    int32_t *proj = &g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE];
    proj[SCENE1_PROJ_OFF_LIFETIME] = -1;
    return target_slot_at(0x4242, 0.0f, 0.0f, 0.0f, 10.0f);
}

int test_combat_sm_phase_c8a_type_2_fires_0x15_spawn(void)
{
    /* TYPE 2 → spawn template 0x15 + SE 0x159 + STATE cleared to 0. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();

    int32_t *slot = setup_phase_c_hit(/*type=*/2,
                                      /*pos=*/1.0f, 0.5f, 0.0f,
                                      /*scale=*/1.0f, /*offset_y=*/4.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,         1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_template,     0x15);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,        0x159);
    /* STATE was set to 5 by C8jb.7 then cleared to 0 by C8jb.8a. */
    int32_t proj_state =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_STATE];
    T_ASSERT_EQ_I(proj_state, 0);
    return 0;
}

int test_combat_sm_phase_c8a_type_3_fires_0x15_spawn(void)
{
    /* TYPE 3 shares the TYPE 2 branch. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();

    int32_t *slot = setup_phase_c_hit(/*type=*/3,
                                      1.0f, 0.5f, 0.0f, 1.0f, 4.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_template,     0x15);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,        0x159);
    return 0;
}

int test_combat_sm_phase_c8a_type_0_fires_0x16_spawn(void)
{
    /* TYPE 0 → SE 0x169 (else branch of C8jb.8a Block 1) + 0x16 spawn
     * (Block 2) + template 1 spawn (C8jb.8b path-a non-{2,3}).  emit_count
     * is 2; first spawn record is C8jb.8a's 0x16. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();

    int32_t *slot = setup_phase_c_hit(/*type=*/0,
                                      1.0f, 0.5f, 0.0f, 1.0f, 2.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  2);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,          0x16);
    T_ASSERT_EQ_I(g_emit_spawn_records[1].template,          1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,        0x169);
    /* TYPE 0 path does NOT clear STATE in C8jb.8a; it stays at C8jb.7's
     * 5.  C8jb.8b path-a doesn't touch STATE either. */
    int32_t proj_state =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_STATE];
    T_ASSERT_EQ_I(proj_state, 5);
    return 0;
}

int test_combat_sm_phase_c8a_type_0x15_se_only(void)
{
    /* TYPE 0x15 + LIFETIME=-1: C8jb.8a plays SE 0x180 without spawning;
     * C8jb.8b path-a non-{2,3} fires template 1.  Single spawn (template 1). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();

    int32_t *slot = setup_phase_c_hit(/*type=*/0x15,
                                      1.0f, 0.5f, 0.0f, 1.0f, 2.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,          1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,        0x180);
    return 0;
}

int test_combat_sm_phase_c8a_default_type_se_only(void)
{
    /* TYPE 5 + LIFETIME=-1: C8jb.8a plays SE 0x169 (else branch);
     * C8jb.8b path-a non-{2,3} fires template 1.  Single spawn (template 1).
     * Note: with LIFETIME=0 (not -1), TYPE 5 would hit C8jb.8b path-c which
     * is DEFERRED for {4,5,8} (no-op) — but our setup uses LIFETIME=-1. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();

    int32_t *slot = setup_phase_c_hit(/*type=*/5,
                                      1.0f, 0.5f, 0.0f, 1.0f, 2.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,          1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,        0x169);
    return 0;
}

int test_combat_sm_phase_c8a_type_2_spawn_pose_verbatim(void)
{
    /* Verify the spawn pose: mid_x = (proj_x + slot_x)/2 = 1.5,
     * mid_z = 0.5, y = proj_y + offset_y * 0.5 = 0.5 + 4.0*0.5 = 2.5.
     * Slot at (0,0,0), proj at (3, 0.5, 1), offset_y = 4.0. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();

    int32_t *slot = setup_phase_c_hit(/*type=*/2,
                                      /*pos=*/3.0f, 0.5f, 1.0f,
                                      /*scale=*/1.5f, /*offset_y=*/4.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_F(g_scene1_combat_phase_c_emit_pose[0], 1.5f, 1e-6f);   /* mid_x */
    T_ASSERT_EQ_F(g_scene1_combat_phase_c_emit_pose[1], 2.5f, 1e-6f);   /* y */
    T_ASSERT_EQ_F(g_scene1_combat_phase_c_emit_pose[2], 0.5f, 1e-6f);   /* mid_z */
    T_ASSERT_EQ_F(g_scene1_combat_phase_c_emit_scale,   1.5f, 1e-6f);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_param7,  6);
    return 0;
}

int test_combat_sm_phase_c8a_type_0_spawn_pose_verbatim(void)
{
    /* TYPE 0 uses 20.5 multiplier on offset_y, not 0.5.
     * y = proj_y + offset_y * 20.5 = 0.0 + 0.1 * 20.5 = 2.05.
     * C8jb.8b also fires template 1 (LIFETIME=-1 + TYPE!={2,3}); check
     * the FIRST spawn record (C8jb.8a's 0x16) for pose verification. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();

    int32_t *slot = setup_phase_c_hit(/*type=*/0,
                                      /*pos=*/3.0f, 0.0f, 1.0f,
                                      /*scale=*/2.0f, /*offset_y=*/0.1f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template, 0x16);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].x,        1.5f,  1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].y,        2.05f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].z,        0.5f,  1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].scale,    2.0f,  1e-6f);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].param7,   6);
    return 0;
}

int test_combat_sm_phase_c8a_no_emit_when_no_hit(void)
{
    /* No AABB pass → no Phase C emit observables change. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    /* Radii are 1.0 but proj is 100 units away → far OOB. */
    install_proj_radii(2, 1.0f, 1.0f);
    setup_proj(0, 2, 0, 100.0f, 0.0f, 100.0f, 1.0f);
    set_proj_offset_y(0, 4.0f);
    int32_t *slot = target_slot_at(0x4242, 0.0f, 0.0f, 0.0f, 0.5f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,        0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,       0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_template,    0);
    return 0;
}

int test_combat_sm_phase_c8a_observables_reset_per_tick(void)
{
    /* Tick 1 hits TYPE 2 and sets observables; tick 2 sentinels the
     * table and observables should reset to 0. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(2, 1.0f, 0.5f, 0.0f, 1.0f, 4.0f);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_template, 0x15);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,    0x159);

    /* Tick 2: re-sentinel the projectile table (so no hits this tick). */
    reset_combat_7_capture();
    /* Slot SEQ_ID changed → bumped to avoid ring-history skip; here we
     * just sentinel the projectile so the cascade catches the first
     * gate before AABB. */
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_template,    0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,       0);
    T_ASSERT_EQ_F(g_scene1_combat_phase_c_emit_pose[0],     0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_scene1_combat_phase_c_emit_pose[1],     0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_scene1_combat_phase_c_emit_pose[2],     0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_scene1_combat_phase_c_emit_scale,       0.0f, 1e-6f);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_param7,      0);
    return 0;
}

int test_combat_sm_phase_c8a_se_hook_receives_id(void)
{
    /* Shared emit_se hook fires with the Phase C SE id. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(2, 1.0f, 0.5f, 0.0f, 1.0f, 4.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_emit_se_count,    1);
    T_ASSERT_EQ_I(g_emit_se_last_id,  0x159);
    return 0;
}

int test_combat_sm_phase_c8a_spawn_hook_receives_args(void)
{
    /* Shared emit_spawn hook fires with call_index=0 + verbatim args. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(/*type=*/3,
                                      /*pos=*/3.0f, 0.5f, 1.0f,
                                      /*scale=*/1.5f, /*offset_y=*/4.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_emit_spawn_count, 1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].call_index, 0);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,   0x15);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].x,          1.5f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].y,          2.5f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].z,          0.5f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].scale,      1.5f, 1e-6f);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].param7,     6);
    return 0;
}

int test_combat_sm_phase_c8a_emit_hooks_nullable(void)
{
    /* Null emit hooks must not crash; observables still latch. */
    reset_combat_state();
    reset_combat_7_capture();
    scene1_combat_set_emit_spawn_hook(NULL);
    scene1_combat_set_emit_se_hook(NULL);
    int32_t *slot = setup_phase_c_hit(2, 1.0f, 0.5f, 0.0f, 1.0f, 4.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_template, 0x15);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,    0x159);
    return 0;
}

int test_combat_sm_phase_c8a_type_2_phase_b_se_id_untouched(void)
{
    /* Phase C SE id latch must not clobber the Phase B observable. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(2, 1.0f, 0.5f, 0.0f, 1.0f, 4.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id, 0x159);
    T_ASSERT_EQ_I(g_scene1_combat_phase_b_emit_se_id, 0);
    return 0;
}

/* ─── C8jb.8b — Phase C LIFETIME + TYPE 6/default ───────────────────── */

/* Force LIFETIME on the projectile at idx (post-setup hook). */
static void set_proj_lifetime(int idx, int32_t lifetime)
{
    int32_t *proj = &g_scene1_projectiles[idx * SCENE1_PROJ_STRIDE];
    proj[SCENE1_PROJ_OFF_LIFETIME] = lifetime;
}

int test_combat_sm_phase_c8b_lifetime_minus_one_type_2_no_spawn(void)
{
    /* LIFETIME==-1 + TYPE 2: C8jb.8b path-a {2,3} leaf — NO extra spawn,
     * NO latch.  Only the C8jb.8a 0x15 spawn fires. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(2, 1.0f, 0.0f, 0.0f, 1.0f, 4.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count, 1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,          0x15);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after,    -1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       0);
    return 0;
}

int test_combat_sm_phase_c8b_lifetime_minus_one_non_2_3_fires_template_1(void)
{
    /* LIFETIME==-1 + TYPE 7 (not in {2,3}): fires template 1 at SLOT pose
     * (scale 0.2, param_7=1).  No latch (engine jmps to 0x43a5d2 directly). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(7, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,           1);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].x,                  0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].y,                  0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].z,                  0.0f, 1e-6f);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].scale,              0.2f, 1e-6f);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].param7,             1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,        0);
    return 0;
}

int test_combat_sm_phase_c8b_lifetime_positive_decrements(void)
{
    /* LIFETIME==5 + TYPE 7: dec to 4 (still > 0), fires template 1, no
     * latch.  LIFETIME field updated to 4 in storage. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(7, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 5);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after,    4);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,           1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,        0);
    /* Storage actually decremented. */
    int32_t lifetime_storage =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_LIFETIME];
    T_ASSERT_EQ_I(lifetime_storage, 4);
    return 0;
}

int test_combat_sm_phase_c8b_lifetime_one_falls_to_zero_default(void)
{
    /* LIFETIME==1 + TYPE 7: dec to 0, falls to path-c default → spawn
     * template 2, AUX=1, latch. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(7, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 1);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after,    0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,           2);
    T_ASSERT_EQ_F(g_emit_spawn_records[0].scale,              0.2f, 1e-6f);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].param7,             1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,          1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,        1);
    int32_t latch =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_FIRST_HIT_LATCH];
    T_ASSERT_EQ_I(latch, 1);
    return 0;
}

int test_combat_sm_phase_c8b_lifetime_zero_type_6_aux_one(void)
{
    /* LIFETIME==0 + TYPE 6: path-c TYPE 6 → AUX=1, latch.  No spawn. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(6, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after,    0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       1);
    /* TYPE 6 path doesn't go through C8jb.8a Block 1 spawn either (not
     * in {2,3}), so no 0x15.  And TYPE != 0 so no 0x16.  Net: 0 spawns. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  0);
    return 0;
}

int test_combat_sm_phase_c8b_lifetime_zero_type_4_deferred(void)
{
    /* LIFETIME==0 + TYPE 4 + OWNER_A+0x2bc82==0: C8jb.8d's gate==0 defer
     * path runs (DEFERRED to C8jb.8e/f — the FUN_004412b6 / FUN_0043824b
     * cascade).  No AUX write, no spawn, no latch.  reset_combat_state()
     * zeros the gate, so production HOUSE always lands here. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(4, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  0);
    /* world_pause stays at the reset value (0) — the gate didn't fire. */
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,               0);
    return 0;
}

int test_combat_sm_phase_c8b_lifetime_zero_type_0x15_fires_scatter(void)
{
    /* LIFETIME==0 + TYPE 0x15: C8jb.8c 5-shot scatter fires.  Engine
     * sets proj.TYPE=-1, fires 10 scene1_spawn calls (5 iters × {template
     * 2, template 0xf}) at PROJ pose with per-spawn RNG Y jitter + angle
     * offset, then END-WITH-LATCH (no AUX write).  SE 0x180 still fires
     * from C8jb.8a. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_se_id,        0x180);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count,     10);
    /* C8jb.8c spawns 10 (5 iters × 2 templates). */
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  10);
    /* proj.TYPE flipped to -1 by scatter prologue. */
    int32_t proj_type =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_TYPE];
    T_ASSERT_EQ_I(proj_type, -1);
    /* Latch storage actually set. */
    int32_t latch =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_FIRST_HIT_LATCH];
    T_ASSERT_EQ_I(latch, 1);
    return 0;
}

int test_combat_sm_phase_c8b_lifetime_negative_other_than_minus_one_forces_zero(void)
{
    /* LIFETIME==-5 (negative but not -1): engine path-b skips the dec
     * (LIFETIME <= 0), then path-c forces LIFETIME=0 + falls into TYPE
     * dispatch.  For TYPE 7 (default) that means spawn template 2, AUX=1,
     * latch. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(7, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, -5);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after,    0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       1);
    /* Storage was -5, forced to 0. */
    int32_t lifetime_storage =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_LIFETIME];
    T_ASSERT_EQ_I(lifetime_storage, 0);
    return 0;
}

int test_combat_sm_phase_c8b_latch_idempotent(void)
{
    /* FIRST_HIT_LATCH is only set to 1 if currently 0.  If already 1, the
     * latch_fired observable still bumps (the engine still executes the
     * cmp branch). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(7, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);
    /* Pre-set the latch to verify idempotence. */
    g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_FIRST_HIT_LATCH]
        = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,        1);
    int32_t latch =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_FIRST_HIT_LATCH];
    T_ASSERT_EQ_I(latch, 1);
    return 0;
}

int test_combat_sm_phase_c8b_observables_reset_per_tick(void)
{
    /* Tick 1: hit fires C8jb.8b path-c default + latch.  Tick 2: sentinel
     * the projectile (no hit).  Observables reset. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(7, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,    1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,  1);

    /* Tick 2: re-sentinel (TYPE=-1 in all records → no hit). */
    reset_combat_7_capture();
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,        0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,      0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after,   0);
    return 0;
}

int test_combat_sm_phase_c8b_observables_reset_on_no_hit(void)
{
    /* No AABB pass → C8jb.8b doesn't run, observables stay at the
     * tick-top reset values. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    install_proj_radii(7, 1.0f, 1.0f);
    setup_proj(0, 7, 0, 100.0f, 0.0f, 100.0f, 1.0f);
    set_proj_offset_y(0, 0.0f);
    set_proj_lifetime(0, 5);  /* would have decremented but no hit fires */
    int32_t *slot = target_slot_at(0x4242, 0.0f, 0.0f, 0.0f, 1.0f);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,        0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,      0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after,   0);
    /* Storage LIFETIME unchanged (no dec happened). */
    int32_t lifetime_storage =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_LIFETIME];
    T_ASSERT_EQ_I(lifetime_storage, 5);
    return 0;
}

/* ─── C8jb.8c — Phase C TYPE 0x15 5-shot scatter ─────────────────────── */

int test_combat_sm_phase_c8c_scatter_emits_10_spawns(void)
{
    /* Scatter fires exactly 10 spawns: 5 iters of (template 2 then 0xf).
     * scatter_count + emit_spawn_count both report 10. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count,    10);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count, 10);
    return 0;
}

int test_combat_sm_phase_c8c_scatter_template_alternation(void)
{
    /* Per-iter pair: template 2, then template 0xf.  Check all 10 records. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    for (int i = 0; i < 5; i++) {
        T_ASSERT_EQ_I(g_emit_spawn_records[i*2 + 0].template, 0x2);
        T_ASSERT_EQ_I(g_emit_spawn_records[i*2 + 1].template, 0xf);
    }
    return 0;
}

int test_combat_sm_phase_c8c_scatter_scale_per_template(void)
{
    /* Template 2 → scale 0.4 (.rdata 0x5195c8).
     * Template 0xf → scale 0.8 (.rdata 0x519470). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    for (int i = 0; i < 5; i++) {
        T_ASSERT_EQ_F(g_emit_spawn_records[i*2 + 0].scale, 0.4f, 1e-6f);
        T_ASSERT_EQ_F(g_emit_spawn_records[i*2 + 1].scale, 0.8f, 1e-6f);
    }
    return 0;
}

int test_combat_sm_phase_c8c_scatter_param7_one(void)
{
    /* All 10 spawns use param_7 = 1 (engine pushes `ebx = 1` for each). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    for (int i = 0; i < 10; i++) {
        T_ASSERT_EQ_I(g_emit_spawn_records[i].param7, 1);
    }
    return 0;
}

int test_combat_sm_phase_c8c_scatter_xz_from_proj_pose(void)
{
    /* X / Z are always proj.POS_X / proj.POS_Z (no jitter, no mid-point
     * with slot like C8jb.8a).  Use proj at (3.0, 0, 1.0). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 3.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    for (int i = 0; i < 10; i++) {
        T_ASSERT_EQ_F(g_emit_spawn_records[i].x, 3.0f, 1e-6f);
        T_ASSERT_EQ_F(g_emit_spawn_records[i].z, 1.0f, 1e-6f);
    }
    return 0;
}

int test_combat_sm_phase_c8c_scatter_y_has_angle_offset(void)
{
    /* Y = proj.Y + rng_unit() + iter * 4.0.  With proj.Y = 0.5
     * (must satisfy `dy < reach` = `0.5 < 10` for the C8jb.7 AABB):
     *   iter 0 (records 0, 1): Y ∈ [0.5,  1.5)
     *   iter 1 (records 2, 3): Y ∈ [4.5,  5.5)
     *   iter 2 (records 4, 5): Y ∈ [8.5,  9.5)
     *   iter 3 (records 6, 7): Y ∈ [12.5, 13.5)
     *   iter 4 (records 8, 9): Y ∈ [16.5, 17.5)
     * Verify band membership without depending on the exact RNG sequence. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count, 10);
    for (int iter = 0; iter < 5; iter++) {
        float base = 0.5f + (float)iter * 4.0f;
        for (int sub = 0; sub < 2; sub++) {
            float y = g_emit_spawn_records[iter*2 + sub].y;
            T_ASSERT(y >= base && y < base + 1.0f);
        }
    }
    return 0;
}

int test_combat_sm_phase_c8c_scatter_y_matches_rng_sequence(void)
{
    /* Reseed, run the scatter, then re-seed and replay the RNG sequence
     * manually — the 10 spawns' Y values must be `proj.Y + rng_unit() +
     * iter*4` in call order.  proves engine's `fadd POS_Y; fadd angle`
     * ordering and that the second-per-iter rng is a separate draw
     * (not reused from the first). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(42);
    int32_t *slot = setup_phase_c_hit(0x15, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count, 10);

    /* Replay the RNG sequence to derive expected Y values. */
    rng_seed(42);
    for (int iter = 0; iter < 5; iter++) {
        float angle = (float)iter * 4.0f;
        for (int sub = 0; sub < 2; sub++) {
            float expected_y = 0.5f + rng_next_unit() + angle;
            T_ASSERT_EQ_F(g_emit_spawn_records[iter*2 + sub].y,
                          expected_y, 1e-6f);
        }
    }
    return 0;
}

int test_combat_sm_phase_c8c_scatter_proj_type_set_to_minus_one(void)
{
    /* Prologue `proj.TYPE = -1` ran (engine `or [esi], 0xffffffff`). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    int32_t proj_type =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_TYPE];
    T_ASSERT_EQ_I(proj_type, -1);
    return 0;
}

int test_combat_sm_phase_c8c_scatter_no_aux_write(void)
{
    /* Engine TYPE 0x15 path does NOT set proj.AUX (the AUX=1 write only
     * fires in the TYPE 6 and default branches; the scatter falls
     * directly from the spawn loop into the latch).
     *
     * AUX defaults to 0 — proves no AUX=1 write fired (which is what
     * TYPE 6 / default would have done).  Note: we can't pre-set AUX to
     * a non-zero sentinel because the C8jb.7 skip cascade catches any
     * non-zero AUX → no hit fires → no scatter to verify. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    /* Scatter fired (10 spawns), latch set, but AUX stayed at 0. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count, 10);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,      0);
    int32_t aux =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_AUX];
    T_ASSERT_EQ_I(aux, 0);
    return 0;
}

int test_combat_sm_phase_c8c_scatter_fires_latch(void)
{
    /* Engine jmps to 0x43a5c4 after the spawn loop — END-WITH-LATCH.
     * FIRST_HIT_LATCH is set to 1 idempotently. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired, 1);
    int32_t latch =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_FIRST_HIT_LATCH];
    T_ASSERT_EQ_I(latch, 1);
    return 0;
}

int test_combat_sm_phase_c8c_lifetime_minus_one_no_scatter(void)
{
    /* LIFETIME == -1 + TYPE 0x15: path-a (non-{2,3}) fires template 1,
     * NOT the scatter.  Verify scatter_count stays 0. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    /* setup_phase_c_hit defaults LIFETIME=-1. */

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count, 0);
    /* Spawn count = 1 (just C8jb.8b's template 1 emit). */
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count, 1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template, 1);
    /* proj.TYPE unchanged (still 0x15). */
    int32_t proj_type =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_TYPE];
    T_ASSERT_EQ_I(proj_type, 0x15);
    return 0;
}

int test_combat_sm_phase_c8c_lifetime_positive_no_scatter(void)
{
    /* LIFETIME == 3 + TYPE 0x15: path-b decrements to 2 (still > 0),
     * fires template 1 — not the scatter. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 3);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count, 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after, 2);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count, 1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template, 1);
    /* proj.TYPE unchanged. */
    int32_t proj_type =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_TYPE];
    T_ASSERT_EQ_I(proj_type, 0x15);
    return 0;
}

int test_combat_sm_phase_c8c_scatter_observable_resets_per_tick(void)
{
    /* scatter_count resets at tick top.  Tick 1 with TYPE 0x15 fires
     * scatter; tick 2 (TYPE became -1 → skip cascade catches it) leaves
     * scatter_count at 0. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count, 10);

    /* Tick 2: proj.TYPE is now -1; first-row skip cascade entry catches
     * it, no scan candidates → no scatter. */
    reset_combat_7_capture();
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count, 0);
    return 0;
}

int test_combat_sm_phase_c8c_scatter_latch_idempotent(void)
{
    /* If FIRST_HIT_LATCH was already 1 pre-tick, scatter still fires +
     * latch observable bumps to 1, but storage unchanged.  Mirrors the
     * engine's `if ([esi+4] == 0) [esi+4] = 1` check. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    rng_seed(1);
    int32_t *slot = setup_phase_c_hit(0x15, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);
    g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_FIRST_HIT_LATCH] = 1;

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired, 1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_scatter_count, 10);
    int32_t latch =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_FIRST_HIT_LATCH];
    T_ASSERT_EQ_I(latch, 1);
    return 0;
}

/* ─── C8jb.8d — Phase C TYPE 4/5/8 owner-flag short-circuit arm ──────── */

static int test_combat_sm_phase_c8d_type_n_arms(int proj_type)
{
    /* Common helper: with OWNER_A+0x2bc82 gate set, TYPE 4/5/8 short-
     * circuits to AUX=2 + world_pause=1 + latch.  No spawn fires.  See
     * each per-TYPE test for the wrapper. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    g_scene1_combat_owner_a_2bc82 = 1;
    int32_t *slot = setup_phase_c_hit(proj_type, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         2);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       1);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,               1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  0);
    /* AUX storage actually set. */
    int32_t aux_storage =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_AUX];
    T_ASSERT_EQ_I(aux_storage, 2);
    /* Latch storage actually set. */
    int32_t latch =
        g_scene1_projectiles[0 * SCENE1_PROJ_STRIDE + SCENE1_PROJ_OFF_FIRST_HIT_LATCH];
    T_ASSERT_EQ_I(latch, 1);
    return 0;
}

int test_combat_sm_phase_c8d_type_4_owner_gate_arms(void)
{
    return test_combat_sm_phase_c8d_type_n_arms(4);
}

int test_combat_sm_phase_c8d_type_5_owner_gate_arms(void)
{
    return test_combat_sm_phase_c8d_type_n_arms(5);
}

int test_combat_sm_phase_c8d_type_8_unreachable_via_skip_cascade(void)
{
    /* Engine asm 0x43a272 has a `cmp eax, 0x8; je 0x43a365` branch — TYPE 8
     * would dispatch into the C8jb.8d arm.  BUT the C8jb.7 skip cascade
     * (scene1_combat_sm.c:1931, engine 0x439f44..0x439fcb) filters TYPE 8
     * BEFORE the AABB even runs (`if (proj_type == 8) continue;`).  So
     * the TYPE 8 arm is dead code in production — engine compiled it in
     * but no projectile with TYPE 8 ever reaches the dispatch.
     *
     * We keep `proj_type == 8` in the C8jb.8d port for engine fidelity
     * (asm has the branch) and document the unreachability here.  This
     * test verifies the skip-cascade gate fires: TYPE 8 + armed owner
     * gate produces NO state change (no hit, no arm, no latch). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    g_scene1_combat_owner_a_2bc82 = 1;
    int32_t *slot = setup_phase_c_hit(8, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    /* C8jb.7 skip cascade caught TYPE 8 → no hit. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_hit_count,         0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       0);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,               0);
    return 0;
}

int test_combat_sm_phase_c8d_type_5_owner_gate_zero_defers(void)
{
    /* OWNER_A+0x2bc82==0 (default): TYPE 5 defers (cascade no-op).
     * Mirror test for TYPE 4 already in C8jb.8b's deferred test. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    int32_t *slot = setup_phase_c_hit(5, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  0);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,               0);
    return 0;
}

int test_combat_sm_phase_c8d_owner_gate_negative_byte_arms(void)
{
    /* Engine `cmp BYTE [edi+0x2bc82], 0` + `jne` trips on ANY non-zero
     * byte — including negative bit patterns (e.g. 0xff = -1).  Our
     * stand-in is int32_t but the engine read is a single byte; verify
     * that a negative int still arms (because the byte image is non-
     * zero).  Pick -1 (= 0xffffffff in two's complement, low byte
     * 0xff). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    g_scene1_combat_owner_a_2bc82 = -1;
    int32_t *slot = setup_phase_c_hit(4, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after, 2);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,       1);
    return 0;
}

int test_combat_sm_phase_c8d_type_6_unaffected_by_owner_gate(void)
{
    /* TYPE 6 falls through to its own AUX=1 path BEFORE the 4/5/8 cluster
     * dispatches (engine `cmp eax, 0x6; jne 0x43a260` at 0x43a24d).  The
     * owner-flag gate has no influence on TYPE 6 — verify AUX stays at 1
     * and world_pause stays 0 even with the gate armed. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    g_scene1_combat_owner_a_2bc82 = 1;
    int32_t *slot = setup_phase_c_hit(6, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,    1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,  1);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,          0);
    return 0;
}

int test_combat_sm_phase_c8d_default_type_unaffected_by_owner_gate(void)
{
    /* Default TYPE (e.g. 7) falls through to spawn template 2 + AUX=1.
     * The owner-flag gate has no influence on the default branch. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    g_scene1_combat_owner_a_2bc82 = 1;
    int32_t *slot = setup_phase_c_hit(7, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       1);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,               0);
    /* Default branch spawns template 2. */
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,           2);
    return 0;
}

int test_combat_sm_phase_c8d_type_4_lifetime_positive_unaffected(void)
{
    /* LIFETIME==5 + TYPE 4 + gate==1: lifetime path-b "still alive"
     * fires template 1 FIRST (the C8jb.8b path-b branch), not the C8jb.8d
     * arm.  Owner gate only matters when lifetime falls to 0 and the
     * TYPE dispatch is reached. */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    g_scene1_combat_owner_a_2bc82 = 1;
    int32_t *slot = setup_phase_c_hit(4, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 5);

    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot), 0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_lifetime_after,    4);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_emit_spawn_count,  1);
    T_ASSERT_EQ_I(g_emit_spawn_records[0].template,           1);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_aux_after,         0);
    T_ASSERT_EQ_I(g_scene1_combat_phase_c_latch_fired,       0);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,               0);
    return 0;
}

int test_combat_sm_phase_c8d_armed_blocks_next_tick_phase_a(void)
{
    /* The world_pause=1 side effect of arming gates Phase A on the NEXT
     * tick.  Tick 1: arm fires, world_pause=1.  Tick 2: Phase A short-
     * circuits (no per-tick flag write). */
    reset_combat_state();
    reset_combat_7_capture();
    reset_combat_6_capture();
    g_scene1_combat_owner_a_2bc82 = 1;
    int32_t *slot = setup_phase_c_hit(4, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    set_proj_lifetime(0, 0);

    /* Tick 1: arm fires. */
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot),    0);
    T_ASSERT_EQ_I(g_scene1_combat_world_pause,    1);
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag,   1);

    /* Reset only the per-tick flag (Phase A's observable).  world_pause
     * is the LATCHED state we want to test against. */
    g_scene1_records_b_tick_flag = 0;
    reset_combat_7_capture();
    T_ASSERT_EQ_I(scene1_combat_sm_tick(slot),    0);
    /* Phase A short-circuited — per-tick flag stays 0. */
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag,   0);
    return 0;
}
