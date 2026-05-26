/*
 * test_scene1_postload.c — Cf.1 MVP coverage for the FUN_00436f97 tail
 * port.  See `docs/findings/scene1-postload-init.md` for the chip scope.
 *
 * Covers:
 *   - Stage default player pos init (the -40 / 0 / -60 FUN_0044f13d literal)
 *   - Pose-player copy from stage default → g_scene1_player_pos
 *   - Ambient-spawn gate: NULL palette + flag=0 are no-ops
 *   - Ambient-spawn flag=1: 200 spawn+tick iterations populate table A
 *   - Spawned slots are type 0x4f with PARAM2=100 (the C8i.5c handler's
 *     anchor-back fingerprint) and AGE staggered by integrator passes
 *   - Force-helper writes the palette field, NULL-safe
 *   - Player-pos pose propagates: spawn anchor is centered on
 *     (player.x, player.y + 2, player.z) per the engine asm
 *
 * Resets g_scene1_records_a between tests so slot state is clean.
 */

#include "t.h"

#include <string.h>

#include "rng.h"
#include "scene1_particles_tick.h"
#include "scene1_postload.h"
#include "scene1_records.h"
#include "scene1_records_b_spawn.h"
#include "scene1_records_c_spawn.h"
#include "scene1_records_c_tick.h"
#include "scene1_spawn.h"
#include "scene1_walker_pass_init.h"
#include "stage_palette.h"

static void reset_world(void)
{
    memset(g_scene1_records_a, 0, sizeof g_scene1_records_a);
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    memset(g_scene1_records_c, 0, sizeof g_scene1_records_c);
    scene1_records_reset(1);
    scene1_spawn_trace_reset();
    scene1_record_b_spawn_trace_reset();
    g_scene1_record_b_seq_counter = 0;
    stage_palette_init_house();
    /* RNG seed deterministically so spawn handlers that consume the
     * rng_next15 stream stay reproducible across runs. */
    rng_seed(0xC0FFEEu);
    g_scene1_player_pos[0] = 0.0f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 0.0f;
    g_scene1_camera_yaw     = 0.0f;
    g_scene1_camera_yaw_alt = 0.0f;
    scene1_postload_init_stage_defaults();
    /* Clear CLI overrides so prior tests' force/type-override state
     * doesn't leak into the next. */
    scene1_postload_set_force_ambient(0);
    scene1_postload_set_ambient_type_override(-1);
    scene1_postload_set_ambient_pose_override(0, 0.0f, 0.0f, 0.0f);
    scene1_postload_set_force_c_pickup_type(-1);
    scene1_postload_set_force_c_world_drop_type(-1);
    scene1_postload_set_force_c_world_drop_count(8);
    scene1_postload_set_force_c_world_drop_mag(1.0f);
    scene1_postload_set_force_b_npc_type(-1);
    scene1_postload_set_force_b_entity_type(-1);
    scene1_postload_set_walker_phase2_scene_type(-1);
    scene1_postload_set_walker_phase2_ivar8(0);
    scene1_postload_set_walker_phase2_stage_positions(NULL);
    scene1_walker_phase2_reset();
}

static int32_t slot_b_read_i(int slot, int off)
{
    return g_scene1_records_b[slot * SCENE1_RECORDS_B_STRIDE + off];
}

static float slot_b_read_f(int slot, int off)
{
    int32_t v = g_scene1_records_b[slot * SCENE1_RECORDS_B_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static int count_b_live(void)
{
    int n = 0;
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        if (slot_b_read_i(i, SCENE1_RECORDS_B_OFF_TYPE) != 0) n++;
    }
    return n;
}

static int32_t slot_c_read_i(int slot, int off)
{
    return g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off];
}

static float slot_c_read_f(int slot, int off)
{
    int32_t v = g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static int count_c_live(void)
{
    int n = 0;
    for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
        if (slot_c_read_i(i, SCENE1_RECORDS_C_OFF_TYPE) != -1) n++;
    }
    return n;
}

static int32_t slot_read_i(int i, int off)
{
    return g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off];
}

static float slot_read_f(int i, int off)
{
    int32_t v = g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static int count_committed_slots_with_type(int type)
{
    int count = 0;
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        if (slot_read_i(i, SCENE1_RECORDS_A_OFF_TYPE) == type) {
            count++;
        }
    }
    return count;
}

/* ─── stage default + pose-player ─────────────────────────────────── */

int test_scene1_postload_stage_defaults_match_fun_0044f13d(void)
{
    reset_world();
    /* FUN_0044f13d:35-38 literals: 0xc2200000 / 0 / 0xc2700000 =
     * (-40.0f, 0.0f, -60.0f).  Pending-human-check #9: validate
     * via Frida that engine writes the same value here at the time
     * FUN_00436f97 actually runs. */
    T_ASSERT(g_scene1_stage_player_default_pos[0] == -40.0f);
    T_ASSERT(g_scene1_stage_player_default_pos[1] ==   0.0f);
    T_ASSERT(g_scene1_stage_player_default_pos[2] == -60.0f);
    return 0;
}

int test_scene1_postload_init_stage_defaults_is_idempotent(void)
{
    reset_world();
    g_scene1_stage_player_default_pos[0] = 99.0f;
    g_scene1_stage_player_default_pos[1] = 99.0f;
    g_scene1_stage_player_default_pos[2] = 99.0f;
    scene1_postload_init_stage_defaults();
    T_ASSERT(g_scene1_stage_player_default_pos[0] == -40.0f);
    T_ASSERT(g_scene1_stage_player_default_pos[2] == -60.0f);
    return 0;
}

int test_scene1_postload_pose_player_copies_defaults(void)
{
    reset_world();
    g_scene1_player_pos[0] = 999.0f;
    g_scene1_player_pos[1] = 999.0f;
    g_scene1_player_pos[2] = 999.0f;

    scene1_postload_pose_player();

    T_ASSERT(g_scene1_player_pos[0] == -40.0f);
    T_ASSERT(g_scene1_player_pos[1] ==   0.0f);
    T_ASSERT(g_scene1_player_pos[2] == -60.0f);
    return 0;
}

/* ─── ambient_spawn gate ──────────────────────────────────────────── */

int test_scene1_postload_ambient_spawn_no_palette_is_noop(void)
{
    reset_world();
    g_stage_palette = NULL;
    scene1_postload_ambient_spawn();
    /* No spawn calls recorded — trace ring should be empty. */
    T_ASSERT(g_scene1_spawn_trace_count == 0);
    /* Restore palette so other tests aren't poisoned. */
    stage_palette_init_house();
    return 0;
}

int test_scene1_postload_ambient_spawn_flag_zero_is_noop(void)
{
    reset_world();
    T_ASSERT(g_stage_palette != NULL);
    T_ASSERT(g_stage_palette->ambient_spawn_flag == 0);
    scene1_postload_ambient_spawn();
    T_ASSERT(g_scene1_spawn_trace_count == 0);
    return 0;
}

int test_scene1_postload_force_ambient_flag_writes_palette(void)
{
    reset_world();
    scene1_postload_force_ambient_flag(7);
    T_ASSERT(g_stage_palette->ambient_spawn_flag == 7);
    scene1_postload_force_ambient_flag(0);
    T_ASSERT(g_stage_palette->ambient_spawn_flag == 0);
    return 0;
}

int test_scene1_postload_force_ambient_flag_null_safe(void)
{
    reset_world();
    g_stage_palette = NULL;
    /* Should not crash; the helper exits early on NULL palette. */
    scene1_postload_force_ambient_flag(1);
    stage_palette_init_house();
    return 0;
}

/* ─── ambient_spawn loop body ─────────────────────────────────────── */

int test_scene1_postload_ambient_spawn_runs_200_iterations(void)
{
    reset_world();
    scene1_postload_force_ambient_flag(1);
    scene1_postload_ambient_spawn();
    /* The trace ring records every scene1_spawn call regardless of
     * commit; the loop runs 200 iterations unconditionally. */
    T_ASSERT(g_scene1_spawn_trace_count == 200);
    return 0;
}

int test_scene1_postload_ambient_spawn_records_type_4f(void)
{
    reset_world();
    scene1_postload_force_ambient_flag(1);
    scene1_postload_ambient_spawn();

    /* Type 0x4f is implemented in C8i.5c (param_7-count via
     * LAB_0044aa47); count_index=1 ⇒ 1 particle per call.  The
     * integrator's handle_type_4f kills the slot when AGE reaches
     * exactly 0x8c (140) — which is the same constant the spawn
     * handler writes to PARAM2.
     *
     * For iter k of 1..200: the particle is ticked (200-k+1) times,
     * reaching final age 201-k.  Particles with k <= 61 hit age=140
     * mid-loop and get killed, leaving 139 surviving slots.  This
     * matches the engine's intent: the 200-iter loop is designed to
     * "fill" the ambient layer to its steady-state population
     * (=PARAM2-ish slots alive at any moment). */
    T_ASSERT_EQ_I(count_committed_slots_with_type(0x4f), 139);
    return 0;
}

int test_scene1_postload_ambient_spawn_param2_is_100(void)
{
    reset_world();
    scene1_postload_force_ambient_flag(1);
    scene1_postload_ambient_spawn();
    /* C8i.5c's type 0x4f handler writes PARAM2 = 100 (the anchor-
     * back distance constant) and AGE = -count_index = -1 at spawn.
     * The integrator's handle_type_4f kills slots when AGE reaches
     * 0x8c (140) on the SAME constant, so 60 of the 200 spawns get
     * killed mid-loop and their slots are re-used for later spawns.
     * Slot 100 is in the [60..139] "never-killed" range so it still
     * holds its original first-fit spawn data. */
    int late_slot = 100;
    T_ASSERT_EQ_I(slot_read_i(late_slot, SCENE1_RECORDS_A_OFF_TYPE), 0x4f);
    T_ASSERT_EQ_I(slot_read_i(late_slot, SCENE1_RECORDS_A_OFF_PARAM2), 100);
    return 0;
}

int test_scene1_postload_ambient_spawn_uses_player_pos_y_plus_2(void)
{
    reset_world();
    /* Stage default pose is (-40, 0, -60); after pose_player() the
     * player sits there.  Engine asm at 0x4381d5 adds 2.0 to player.y
     * once outside the loop. */
    scene1_postload_pose_player();
    scene1_postload_force_ambient_flag(1);
    scene1_postload_ambient_spawn();

    T_ASSERT(g_scene1_spawn_trace_count == 200);
    /* Every traced call should be centered on (player.x, player.y+2,
     * player.z) — the loop hoists the y+2 add out of the loop and
     * reuses constants every iteration.  The trace is a 32-slot
     * ring; after 200 calls every slot has been overwritten by a
     * later call with identical args, so checking 0..31 covers the
     * invariant. */
    for (int i = 0; i < SCENE1_SPAWN_TRACE_CAPACITY; i++) {
        T_ASSERT(g_scene1_spawn_trace[i].x == -40.0f);
        T_ASSERT(g_scene1_spawn_trace[i].y ==   2.0f);
        T_ASSERT(g_scene1_spawn_trace[i].z == -60.0f);
        T_ASSERT(g_scene1_spawn_trace[i].type == 0x4f);
        T_ASSERT(g_scene1_spawn_trace[i].scale == 1.0f);
        T_ASSERT(g_scene1_spawn_trace[i].param7 == 1);
        T_ASSERT(g_scene1_spawn_trace[i].slot_hint == 0);
    }
    return 0;
}

/* ─── CLI override setters ────────────────────────────────────────── */

int test_scene1_postload_set_force_ambient_bypasses_palette_gate(void)
{
    reset_world();
    /* Palette flag is zero (HOUSE default), but force override should
     * still drive the 200-iter loop. */
    T_ASSERT(g_stage_palette->ambient_spawn_flag == 0);
    scene1_postload_set_force_ambient(1);
    scene1_postload_ambient_spawn();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 200);
    return 0;
}

int test_scene1_postload_set_force_ambient_zero_restores_gate(void)
{
    reset_world();
    scene1_postload_set_force_ambient(1);
    scene1_postload_set_force_ambient(0);
    scene1_postload_ambient_spawn();
    /* Both palette flag and override are zero — no spawns. */
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    return 0;
}

int test_scene1_postload_set_force_ambient_null_palette_still_noop(void)
{
    reset_world();
    g_stage_palette = NULL;
    scene1_postload_set_force_ambient(1);
    scene1_postload_ambient_spawn();
    /* NULL palette is an unconditional bail — force override doesn't
     * override the NULL check (matches engine which derefs the pointer
     * for the gate check itself; we hoist the NULL guard out). */
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    stage_palette_init_house();
    return 0;
}

int test_scene1_postload_type_override_replaces_4f(void)
{
    reset_world();
    scene1_postload_set_force_ambient(1);
    scene1_postload_set_ambient_type_override(0x92);
    scene1_postload_ambient_spawn();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 200);
    /* Every traced spawn call should use the override type — the
     * trace ring overwrites older entries so all 32 slots end up
     * with the same type. */
    for (int i = 0; i < SCENE1_SPAWN_TRACE_CAPACITY; i++) {
        T_ASSERT_EQ_I(g_scene1_spawn_trace[i].type, 0x92);
    }
    /* Type 0x92 is a 1-spawn color-cycle billboard burst (C8i.2);
     * each spawn commits a slot.  Unlike 0x4f, 0x92 doesn't get
     * killed mid-loop by AGE==0x8c, so all 200 spawns land — but
     * table A is 4096 slots wide and the spawn API first-fit-fills
     * from the start, so the first 200 slots should be type 0x92. */
    T_ASSERT_EQ_I(count_committed_slots_with_type(0x92), 200);
    return 0;
}

int test_scene1_postload_type_override_minus_one_restores_default(void)
{
    reset_world();
    scene1_postload_set_force_ambient(1);
    scene1_postload_set_ambient_type_override(0x92);
    scene1_postload_set_ambient_type_override(-1);
    scene1_postload_ambient_spawn();
    /* Trace should be type 0x4f (engine default) on every entry. */
    for (int i = 0; i < SCENE1_SPAWN_TRACE_CAPACITY; i++) {
        T_ASSERT_EQ_I(g_scene1_spawn_trace[i].type, 0x4f);
    }
    return 0;
}

int test_scene1_postload_ambient_spawn_tick_advances_records(void)
{
    reset_world();
    scene1_postload_force_ambient_flag(1);
    scene1_postload_ambient_spawn();

    /* Type 0x4f's position is anchor-back: pos = (anchor) - vel*100.
     * Since vel is per-particle randomized (sin/cos of a random
     * angle), no two slots should have exactly the same pos.  Slots
     * 100 and 130 are both in the [60..139] "never-killed" range —
     * slot 100 spawned ~iter 101 (~99 ticks of integration), slot
     * 130 spawned ~iter 131 (~69 ticks).  Both alive, both have
     * advanced along their (different) random vel vectors, so y
     * must differ. */
    float y100 = slot_read_f(100, SCENE1_RECORDS_A_OFF_POS_Y);
    float y130 = slot_read_f(130, SCENE1_RECORDS_A_OFF_POS_Y);
    T_ASSERT(slot_read_i(100, SCENE1_RECORDS_A_OFF_TYPE) == 0x4f);
    T_ASSERT(slot_read_i(130, SCENE1_RECORDS_A_OFF_TYPE) == 0x4f);
    T_ASSERT(y100 != y130);
    return 0;
}

/* ─── C8j.fin.c — table C smoke wiring ────────────────────────────── */

int test_scene1_postload_smoke_c_default_is_noop(void)
{
    reset_world();
    /* Defaults: both type overrides at -1 → smoke runner exits early. */
    scene1_postload_smoke_c_spawn();
    T_ASSERT_EQ_I(count_c_live(), 0);
    return 0;
}

int test_scene1_postload_smoke_c_pickup_writes_first_slot(void)
{
    reset_world();
    /* Anchor the spawn so we can assert the pos triplet without
     * depending on player-pose state. */
    scene1_postload_set_ambient_pose_override(1, 1.5f, 2.5f, 3.5f);
    scene1_postload_set_force_c_pickup_type(0x42);
    scene1_postload_smoke_c_spawn();

    T_ASSERT_EQ_I(count_c_live(), 1);
    T_ASSERT_EQ_I(slot_c_read_i(0, SCENE1_RECORDS_C_OFF_TYPE), 0x42);
    T_ASSERT(slot_c_read_f(0, SCENE1_RECORDS_C_OFF_POS_X) == 1.5f);
    T_ASSERT(slot_c_read_f(0, SCENE1_RECORDS_C_OFF_POS_Y) == 2.5f);
    T_ASSERT(slot_c_read_f(0, SCENE1_RECORDS_C_OFF_POS_Z) == 3.5f);
    /* Allocator default state=2 (pickup-bob), scale=1.0. */
    T_ASSERT_EQ_I(slot_c_read_i(0, SCENE1_RECORDS_C_OFF_STATE), 2);
    T_ASSERT(slot_c_read_f(0, SCENE1_RECORDS_C_OFF_SCALE) == 1.0f);
    return 0;
}

int test_scene1_postload_smoke_c_world_drop_commits_count(void)
{
    reset_world();
    scene1_postload_set_ambient_pose_override(1, 0.0f, 0.0f, 0.0f);
    /* Type > 6 to dodge the 4-color RNG-ramp window and exercise the
     * full 200-slot scan cap. */
    scene1_postload_set_force_c_world_drop_type(0x10);
    scene1_postload_set_force_c_world_drop_count(5);
    scene1_postload_smoke_c_spawn();

    T_ASSERT_EQ_I(count_c_live(), 5);
    for (int i = 0; i < 5; i++) {
        T_ASSERT_EQ_I(slot_c_read_i(i, SCENE1_RECORDS_C_OFF_TYPE), 0x10);
        /* World-drop allocator sets state=0 (physics). */
        T_ASSERT_EQ_I(slot_c_read_i(i, SCENE1_RECORDS_C_OFF_STATE), 0);
    }
    return 0;
}

int test_scene1_postload_smoke_c_pickup_plus_world_drop_both_fire(void)
{
    reset_world();
    scene1_postload_set_ambient_pose_override(1, 0.0f, 0.0f, 0.0f);
    scene1_postload_set_force_c_pickup_type(0x42);
    scene1_postload_set_force_c_world_drop_type(0x10);
    scene1_postload_set_force_c_world_drop_count(3);
    scene1_postload_smoke_c_spawn();

    /* Pickup lands in slot 0 (single-slot, scan finds the first
     * sentinel first); world-drop fills slots 1..3.  Same first-fit
     * scan order so the layout is deterministic. */
    T_ASSERT_EQ_I(count_c_live(), 4);
    T_ASSERT_EQ_I(slot_c_read_i(0, SCENE1_RECORDS_C_OFF_TYPE), 0x42);
    T_ASSERT_EQ_I(slot_c_read_i(0, SCENE1_RECORDS_C_OFF_STATE), 2);
    for (int i = 1; i <= 3; i++) {
        T_ASSERT_EQ_I(slot_c_read_i(i, SCENE1_RECORDS_C_OFF_TYPE), 0x10);
        T_ASSERT_EQ_I(slot_c_read_i(i, SCENE1_RECORDS_C_OFF_STATE), 0);
    }
    return 0;
}

int test_scene1_postload_smoke_c_uses_player_pos_y_plus_2_when_no_override(void)
{
    reset_world();
    g_scene1_player_pos[0] =  3.0f;
    g_scene1_player_pos[1] =  4.0f;
    g_scene1_player_pos[2] = -7.0f;
    /* No pose override → smoke spawn anchors on (px, py+2, pz). */
    scene1_postload_set_force_c_pickup_type(0x42);
    scene1_postload_smoke_c_spawn();

    T_ASSERT_EQ_I(count_c_live(), 1);
    T_ASSERT(slot_c_read_f(0, SCENE1_RECORDS_C_OFF_POS_X) ==  3.0f);
    T_ASSERT(slot_c_read_f(0, SCENE1_RECORDS_C_OFF_POS_Y) ==  6.0f);
    T_ASSERT(slot_c_read_f(0, SCENE1_RECORDS_C_OFF_POS_Z) == -7.0f);
    return 0;
}

int test_scene1_postload_smoke_c_setter_minus_one_restores_default(void)
{
    reset_world();
    scene1_postload_set_force_c_pickup_type(0x42);
    scene1_postload_set_force_c_pickup_type(-1);
    scene1_postload_set_force_c_world_drop_type(0x10);
    scene1_postload_set_force_c_world_drop_type(-1);
    scene1_postload_smoke_c_spawn();
    T_ASSERT_EQ_I(count_c_live(), 0);
    return 0;
}

int test_scene1_postload_smoke_c_world_drop_count_zero_is_skip(void)
{
    reset_world();
    scene1_postload_set_force_c_world_drop_type(0x10);
    scene1_postload_set_force_c_world_drop_count(0);
    scene1_postload_smoke_c_spawn();
    /* count=0 → world-drop branch short-circuits.  No pickup either. */
    T_ASSERT_EQ_I(count_c_live(), 0);
    return 0;
}

/* ─── C8j.fin.b — table B smoke wiring ────────────────────────────── */

int test_scene1_postload_smoke_b_default_is_noop(void)
{
    reset_world();
    scene1_postload_smoke_b_spawn();
    T_ASSERT_EQ_I(count_b_live(), 0);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 0);
    return 0;
}

int test_scene1_postload_smoke_b_npc_writes_first_slot(void)
{
    reset_world();
    scene1_postload_set_ambient_pose_override(1, 1.5f, 2.5f, 3.5f);
    /* Anchor type 0xe = LAB_00447584 trivial tail (preamble-only). */
    scene1_postload_set_force_b_npc_type(0xe);
    scene1_postload_smoke_b_spawn();

    T_ASSERT_EQ_I(count_b_live(), 1);
    T_ASSERT_EQ_I(slot_b_read_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xe);
    /* NPC preamble copies pos from owner+0x3f0 verbatim (no -0.5 bias). */
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_X) == 1.5f);
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_Y) == 2.5f);
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_Z) == 3.5f);
    /* Trace ring records the call w/ KIND_NPC. */
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 1);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].kind,
                  SCENE1_RECORD_B_SPAWN_KIND_NPC);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].type, 0xe);
    return 0;
}

int test_scene1_postload_smoke_b_entity_writes_first_slot(void)
{
    reset_world();
    scene1_postload_set_ambient_pose_override(1, 4.0f, 5.0f, 6.0f);
    /* Anchor type 0x24 = pure preamble (LAB_004457e7 tail). */
    scene1_postload_set_force_b_entity_type(0x24);
    scene1_postload_smoke_b_spawn();

    T_ASSERT_EQ_I(count_b_live(), 1);
    T_ASSERT_EQ_I(slot_b_read_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x24);
    /* Entity preamble pulls pos from owner+0x20 + applies the -0.5y
     * bias when flag==-1.  Smoke uses flag=-1. */
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_X) ==  4.0f);
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_Y) ==  4.5f);  /* 5.0 - 0.5 */
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_Z) ==  6.0f);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 1);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].kind,
                  SCENE1_RECORD_B_SPAWN_KIND_ENTITY);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].type, 0x24);
    return 0;
}

int test_scene1_postload_smoke_b_npc_plus_entity_both_fire(void)
{
    reset_world();
    scene1_postload_set_ambient_pose_override(1, 0.0f, 1.0f, 0.0f);
    scene1_postload_set_force_b_npc_type(0xe);
    scene1_postload_set_force_b_entity_type(0x24);
    scene1_postload_smoke_b_spawn();

    /* Both allocators scan from slot 0 sentinel-first.  NPC fires
     * first and claims slot 0; entity fires second and claims slot 1. */
    T_ASSERT_EQ_I(count_b_live(), 2);
    T_ASSERT_EQ_I(slot_b_read_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xe);
    T_ASSERT_EQ_I(slot_b_read_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0x24);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 2);
    return 0;
}

int test_scene1_postload_smoke_b_uses_player_pos_y_plus_2_when_no_override(void)
{
    reset_world();
    g_scene1_player_pos[0] =  3.0f;
    g_scene1_player_pos[1] =  4.0f;
    g_scene1_player_pos[2] = -7.0f;
    /* No pose override → smoke anchors on (px, py+2, pz).  Use NPC
     * (no -0.5 bias) so the assertion is exact. */
    scene1_postload_set_force_b_npc_type(0xe);
    scene1_postload_smoke_b_spawn();

    T_ASSERT_EQ_I(count_b_live(), 1);
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_X) ==  3.0f);
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_Y) ==  6.0f);
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_POS_Z) == -7.0f);
    return 0;
}

int test_scene1_postload_smoke_b_setter_minus_one_restores_default(void)
{
    reset_world();
    scene1_postload_set_force_b_npc_type(0xe);
    scene1_postload_set_force_b_npc_type(-1);
    scene1_postload_set_force_b_entity_type(0x24);
    scene1_postload_set_force_b_entity_type(-1);
    scene1_postload_smoke_b_spawn();
    T_ASSERT_EQ_I(count_b_live(), 0);
    return 0;
}

int test_scene1_postload_smoke_b_blob_persists_matrix_across_calls(void)
{
    /* The fake-owner blob's identity matrix is seeded once and reused
     * across HOUSE entries — verify the second call still writes a
     * valid matrix into the slot (not zero) so the per-type body that
     * later reads slot+MATRIX0 gets a sane row. */
    reset_world();
    scene1_postload_set_ambient_pose_override(1, 0.0f, 0.0f, 0.0f);
    scene1_postload_set_force_b_npc_type(0xe);
    scene1_postload_smoke_b_spawn();
    scene1_postload_smoke_b_spawn();

    T_ASSERT_EQ_I(count_b_live(), 2);
    /* Both slots' MATRIX0 row should hold the identity-matrix first
     * float (1.0f).  Decode via slot_b_read_f at the MATRIX0 offset. */
    T_ASSERT(slot_b_read_f(0, SCENE1_RECORDS_B_OFF_MATRIX0) == 1.0f);
    T_ASSERT(slot_b_read_f(1, SCENE1_RECORDS_B_OFF_MATRIX0) == 1.0f);
    return 0;
}

/* ─── Cf.minimal — FUN_00436f97 alt-stage arm writer chunk ──────────── */

int test_scene1_postload_walker_phase2_default_is_disabled(void)
{
    /* Default scene_type = -1; writer no-ops, all walker phase-2 fields
     * stay at their reset BSS-zero defaults. */
    reset_world();
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 0);
    for (int i = 0; i < SCENE1_WALKER_PHASE2_MAX; i++) {
        T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[i], 0);
        T_ASSERT(g_scene1_walker_phase2_rot_y[i] == 0.0f);
        T_ASSERT(g_scene1_walker_phase2_pos_x[i] == 0.0f);
        T_ASSERT(g_scene1_walker_phase2_pos_y[i] == 0.0f);
        T_ASSERT(g_scene1_walker_phase2_pos_z[i] == 0.0f);
    }
    return 0;
}

int test_scene1_postload_walker_phase2_scene_type_out_of_range_disables(void)
{
    /* scene_type values < 0 or > 4 → writer doesn't fire. */
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(5);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 0);

    reset_world();
    scene1_postload_set_walker_phase2_scene_type(-5);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 0);
    return 0;
}

int test_scene1_postload_walker_phase2_scene_type_0_count_uses_ivar8(void)
{
    /* scene_type 0 → phase2_count = ivar8.  Default ivar8 = 0 → count = 0. */
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(0);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 0);

    /* Set ivar8 = 3 → count = 3. */
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(0);
    scene1_postload_set_walker_phase2_ivar8(3);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 3);
    return 0;
}

int test_scene1_postload_walker_phase2_scene_type_1_count_4(void)
{
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(1);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 4);
    return 0;
}

int test_scene1_postload_walker_phase2_scene_type_2_count_6(void)
{
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(2);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 6);
    return 0;
}

int test_scene1_postload_walker_phase2_scene_type_3_count_10(void)
{
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(3);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 10);
    return 0;
}

int test_scene1_postload_walker_phase2_scene_type_4_count_10(void)
{
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(4);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 10);
    return 0;
}

int test_scene1_postload_walker_phase2_mesh_type_pattern(void)
{
    /* Engine asm-verified: slots {0,4,6,7,8,9} = ivar8; slots {1,2,3,5} = 4. */
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(3);
    scene1_postload_set_walker_phase2_ivar8(7);
    scene1_postload_walker_phase2_init();
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[0], 7);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[1], 4);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[2], 4);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[3], 4);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[4], 7);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[5], 4);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[6], 7);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[7], 7);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[8], 7);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[9], 7);
    /* Slot 10..19 stay at reset BSS-zero (untouched by Cf.minimal). */
    for (int i = 10; i < SCENE1_WALKER_PHASE2_MAX; i++) {
        T_ASSERT_EQ_I(g_scene1_walker_phase2_mesh_type[i], 0);
    }
    return 0;
}

int test_scene1_postload_walker_phase2_rot_y_pattern(void)
{
    /* Engine asm-verified: rot_y[1] = 0, rot_y[2] = π/2, rot_y[3] = -π/2.
     * Other slots stay at their BSS-zero default. */
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(1);
    scene1_postload_walker_phase2_init();
    T_ASSERT(g_scene1_walker_phase2_rot_y[0] == 0.0f);  /* BSS untouched */
    T_ASSERT(g_scene1_walker_phase2_rot_y[1] == 0.0f);
    T_ASSERT(g_scene1_walker_phase2_rot_y[2] == 1.5707964f);
    T_ASSERT(g_scene1_walker_phase2_rot_y[3] == -1.5707964f);
    for (int i = 4; i < SCENE1_WALKER_PHASE2_MAX; i++) {
        T_ASSERT(g_scene1_walker_phase2_rot_y[i] == 0.0f);
    }
    return 0;
}

int test_scene1_postload_walker_phase2_positions_default_anchor_subtract(void)
{
    /* Default stage_positions = (0, 0) for all 10 slots.
     * scene_type 0 anchors: {(4,3),(3,4),(5,2),(4,2),(4,3),(3,4),(4,3),(4,3),(4,3),(4,3)}.
     * Output: pos_x[i] = 2 * (0 - anchor.x) = -2 * anchor.x.
     *         pos_z[i] = 2 * (0 - anchor.z) = -2 * anchor.z.
     *         pos_y[i] = 0. */
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(0);
    scene1_postload_set_walker_phase2_ivar8(10);  /* count = 10 */
    scene1_postload_walker_phase2_init();

    T_ASSERT(g_scene1_walker_phase2_pos_x[0] == -8.0f);  /* -2 * 4 */
    T_ASSERT(g_scene1_walker_phase2_pos_z[0] == -6.0f);  /* -2 * 3 */
    T_ASSERT(g_scene1_walker_phase2_pos_y[0] == 0.0f);

    T_ASSERT(g_scene1_walker_phase2_pos_x[1] == -6.0f);  /* -2 * 3 */
    T_ASSERT(g_scene1_walker_phase2_pos_z[1] == -8.0f);  /* -2 * 4 */

    T_ASSERT(g_scene1_walker_phase2_pos_x[2] == -10.0f); /* -2 * 5 */
    T_ASSERT(g_scene1_walker_phase2_pos_z[2] == -4.0f);  /* -2 * 2 */

    T_ASSERT(g_scene1_walker_phase2_pos_x[3] == -8.0f);  /* -2 * 4 */
    T_ASSERT(g_scene1_walker_phase2_pos_z[3] == -4.0f);  /* -2 * 2 */
    return 0;
}

int test_scene1_postload_walker_phase2_positions_with_stage_override(void)
{
    /* Set stage_positions to non-zero values, scene_type 1.
     * Expected: pos_x[i] = 2 * (stage_pos.x - anchor.x). */
    reset_world();
    int32_t stage_pos[10][2] = {
        {10, 20}, {15, 25}, {20, 10}, {0, 0}, {7, 7},
        {5, 5},   {3, 3},   {0, 0},   {0, 0}, {0, 0},
    };
    scene1_postload_set_walker_phase2_scene_type(1);
    scene1_postload_set_walker_phase2_stage_positions(stage_pos);
    scene1_postload_walker_phase2_init();

    /* Anchor[1][0] = (4, 3) → pos_x = 2*(10-4) = 12; pos_z = 2*(20-3) = 34. */
    T_ASSERT(g_scene1_walker_phase2_pos_x[0] == 12.0f);
    T_ASSERT(g_scene1_walker_phase2_pos_z[0] == 34.0f);
    /* Anchor[1][1] = (3, 4) → pos_x = 2*(15-3) = 24; pos_z = 2*(25-4) = 42. */
    T_ASSERT(g_scene1_walker_phase2_pos_x[1] == 24.0f);
    T_ASSERT(g_scene1_walker_phase2_pos_z[1] == 42.0f);
    /* Anchor[1][2] = (5, 2) → pos_x = 2*(20-5) = 30; pos_z = 2*(10-2) = 16. */
    T_ASSERT(g_scene1_walker_phase2_pos_x[2] == 30.0f);
    T_ASSERT(g_scene1_walker_phase2_pos_z[2] == 16.0f);
    /* Anchor[1][3] = (4, 2) → pos_x = 2*(0-4) = -8; pos_z = 2*(0-2) = -4. */
    T_ASSERT(g_scene1_walker_phase2_pos_x[3] == -8.0f);
    T_ASSERT(g_scene1_walker_phase2_pos_z[3] == -4.0f);
    return 0;
}

int test_scene1_postload_walker_phase2_scene_type_4_distinct_anchors(void)
{
    /* scene_type 4 has a distinct anchor row with larger values.
     * Anchor[4][0] = (12, 111).  With default stage_pos = (0, 0):
     *   pos_x[0] = 2 * (0 - 12) = -24
     *   pos_z[0] = 2 * (0 - 111) = -222 */
    reset_world();
    scene1_postload_set_walker_phase2_scene_type(4);
    scene1_postload_walker_phase2_init();
    T_ASSERT(g_scene1_walker_phase2_pos_x[0] == -24.0f);
    T_ASSERT(g_scene1_walker_phase2_pos_z[0] == -222.0f);
    /* Anchor[4][9] = (2107, 2207) → pos_x[9] = -4214; pos_z[9] = -4414. */
    T_ASSERT(g_scene1_walker_phase2_pos_x[9] == -4214.0f);
    T_ASSERT(g_scene1_walker_phase2_pos_z[9] == -4414.0f);
    return 0;
}

int test_scene1_postload_walker_phase2_setter_round_trip(void)
{
    /* Setting + re-firing is idempotent: same inputs → same outputs. */
    reset_world();
    int32_t pos[10][2] = {{1,2},{3,4},{5,6},{7,8},{9,10},
                          {11,12},{13,14},{15,16},{17,18},{19,20}};
    scene1_postload_set_walker_phase2_scene_type(3);
    scene1_postload_set_walker_phase2_stage_positions(pos);
    scene1_postload_walker_phase2_init();

    float snap_x[10], snap_y[10], snap_z[10];
    for (int i = 0; i < 10; i++) {
        snap_x[i] = g_scene1_walker_phase2_pos_x[i];
        snap_y[i] = g_scene1_walker_phase2_pos_y[i];
        snap_z[i] = g_scene1_walker_phase2_pos_z[i];
    }

    /* Reset walker state, re-fire — outputs match snapshot. */
    scene1_walker_phase2_reset();
    scene1_postload_walker_phase2_init();
    for (int i = 0; i < 10; i++) {
        T_ASSERT(g_scene1_walker_phase2_pos_x[i] == snap_x[i]);
        T_ASSERT(g_scene1_walker_phase2_pos_y[i] == snap_y[i]);
        T_ASSERT(g_scene1_walker_phase2_pos_z[i] == snap_z[i]);
    }
    return 0;
}

int test_scene1_postload_walker_phase2_set_positions_null_clears(void)
{
    /* Setting positions to a non-zero pattern, then NULL → resets to BSS-zero. */
    reset_world();
    int32_t pos[10][2] = {{10,10},{10,10},{10,10},{10,10},{10,10},
                          {10,10},{10,10},{10,10},{10,10},{10,10}};
    scene1_postload_set_walker_phase2_stage_positions(pos);
    /* Now clear */
    scene1_postload_set_walker_phase2_stage_positions(NULL);
    scene1_postload_set_walker_phase2_scene_type(0);
    scene1_postload_set_walker_phase2_ivar8(1);  /* count = 1 */
    scene1_postload_walker_phase2_init();
    /* With stage_pos cleared to 0, pos_x[0] = 2 * (0 - 4) = -8. */
    T_ASSERT(g_scene1_walker_phase2_pos_x[0] == -8.0f);
    return 0;
}

int test_scene1_postload_walker_phase2_drives_walker_compute_translation(void)
{
    /* End-to-end: writer populates phase-2 fields → walker compute
     * builds matrices using those fields → final matrix translation
     * row reflects engine-semantic placement at (pos_x, pos_y, pos_z).
     *
     * Use scene_type=1 (count=4), stage_pos=(10,0),(11,0),(12,0),(13,0).
     * Anchor[1] = (4,3),(3,4),(5,2),(4,2).
     * Expected pos_x[1] = 2*(11-3) = 16; pos_z[1] = 2*(0-4) = -8.
     *
     * Matrix composition (PII.3a chain): world = S × R × T(pos).  In
     * D3D row-major convention `vertex' = vertex × world` the
     * translation row M[12..14] equals (pos_x, pos_y, pos_z) verbatim
     * since S and R have (0, 0, 0, 1) in their column 4. */
    reset_world();
    int32_t stage_pos[10][2] = {
        {10, 0}, {11, 0}, {12, 0}, {13, 0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}
    };
    scene1_postload_set_walker_phase2_scene_type(1);
    scene1_postload_set_walker_phase2_stage_positions(stage_pos);
    scene1_postload_walker_phase2_init();

    float matrices[SCENE1_WALKER_PHASE2_MAX * 16];
    int n = scene1_walker_phase2_compute(matrices);
    T_ASSERT_EQ_I(n, 4);

    /* Slot 1 (mesh_type=4 but no flip-chain hook installed → no flip). */
    float *m1 = matrices + 1 * 16;
    T_ASSERT(m1[12] == 16.0f);
    T_ASSERT(m1[13] == 0.0f);
    T_ASSERT(m1[14] == -8.0f);

    /* Slot 0 (mesh_type=ivar8=0 → no mesh_type==4 path).
     * Anchor[1][0] = (4, 3) → pos_x = 2*(10-4) = 12; pos_z = 2*(0-3) = -6. */
    float *m0 = matrices + 0 * 16;
    T_ASSERT(m0[12] == 12.0f);
    T_ASSERT(m0[13] == 0.0f);
    T_ASSERT(m0[14] == -6.0f);
    return 0;
}
