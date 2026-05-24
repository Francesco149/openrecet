/*
 * test_scene1_records_b_spawn.c — unit tests for the C8j.5 table B
 * allocators (FUN_0044376a + FUN_00445a8c skeleton + preamble + 3
 * minimal anchor types per allocator).
 */

#include "t.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "rng.h"
#include "scene1_records.h"
#include "scene1_records_b_spawn.h"
#include "scene1_particles_tick.h"  /* g_scene1_player_pos */
#include "sim.h"                    /* g_sim_frame_count */

/* Local copy of the C file's 2π constant (see scene1_records_b_spawn.c). */
#ifndef B_TWO_PI_F
#define B_TWO_PI_F 6.2831855f
#endif

/* Engine owner shapes are large (entity ≥ 0xeb0 B, NPC ≥ 0x3fc B).
 * Bound generously to give per-type bodies room to grow as the C8j
 * ladder ports more handlers. */
#define OWNER_A_SIZE 0x1000
#define OWNER_B_SIZE 0x500

static uint8_t g_owner_a[OWNER_A_SIZE];
static uint8_t g_owner_b[OWNER_B_SIZE];

/* ─── helpers ─────────────────────────────────────────────────────── */

static void reset_world(void)
{
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    scene1_records_reset(1);
    g_scene1_record_b_seq_counter = 0;
    g_scene1_records_b_count = 0;
    scene1_record_b_spawn_trace_reset();
    memset(g_owner_a, 0, sizeof g_owner_a);
    memset(g_owner_b, 0, sizeof g_owner_b);
}

static void owner_write_f(uint8_t *buf, int off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    memcpy(buf + off, &v, sizeof v);
}

static void owner_write_i(uint8_t *buf, int off, int32_t v)
{
    memcpy(buf + off, &v, sizeof v);
}

static int32_t slot_i(int slot, int off)
{
    return g_scene1_records_b[slot * SCENE1_RECORDS_B_STRIDE + off];
}

static float slot_f(int slot, int off)
{
    int32_t v = g_scene1_records_b[slot * SCENE1_RECORDS_B_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static int count_live(void)
{
    int n = 0;
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        if (slot_i(i, SCENE1_RECORDS_B_OFF_TYPE) != 0) n++;
    }
    return n;
}

/* ─── trace ring ──────────────────────────────────────────────────── */

int test_records_b_spawn_trace_records_calls(void)
{
    reset_world();
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    scene1_record_b_spawn_npc(g_owner_b, 0xe, 42);

    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 2);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].kind,
                  SCENE1_RECORD_B_SPAWN_KIND_ENTITY);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].type, 0x24);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].flag, -1);
    T_ASSERT(g_scene1_record_b_spawn_trace[0].owner == g_owner_a);

    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[1].kind,
                  SCENE1_RECORD_B_SPAWN_KIND_NPC);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[1].type, 0xe);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[1].flag, 42);
    T_ASSERT(g_scene1_record_b_spawn_trace[1].owner == g_owner_b);
    return 0;
}

int test_records_b_spawn_trace_unimplemented_still_traces(void)
{
    /* Trace fires even when the type isn't implemented — no slot
     * commits, but the call is observable. */
    reset_world();
    scene1_record_b_spawn_entity(g_owner_a, 0xdead, -1);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 1);
    T_ASSERT_EQ_I(count_live(), 0);
    return 0;
}

int test_records_b_spawn_trace_reset_clears(void)
{
    reset_world();
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 2);
    scene1_record_b_spawn_trace_reset();
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 0);
    return 0;
}

/* ─── entity allocator preamble ───────────────────────────────────── */

int test_records_b_spawn_entity_24_pure_preamble(void)
{
    /* Set known owner fields. */
    reset_world();
    owner_write_f(g_owner_a, 0x20,  10.0f);   /* pos.x */
    owner_write_f(g_owner_a, 0x24,  20.0f);   /* pos.y → slot gets 19.5 */
    owner_write_f(g_owner_a, 0x28,  30.0f);   /* pos.z */
    owner_write_i(g_owner_a, 0xeac, 0xABCD);  /* owner flag inherit */

    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x24);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 19.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 30.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AGE), 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_FLAG_A), 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_FLAG_B), -1);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT1), -1);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT2), -1);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_Y) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_OWNER_FLAG), (int32_t)0xABCD);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 0);
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 1);

    /* Type 0x24 body is empty — ROT_X stays at preamble default 0. */
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_spawn_entity_writes_owner_ptr_to_owner_a_slot(void)
{
    reset_world();
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    intptr_t got = (intptr_t)(int32_t)slot_i(0, SCENE1_RECORDS_B_OFF_OWNER_A);
    intptr_t want = (intptr_t)g_owner_a;
    /* On 64-bit hosts the engine's int-pun loses the high bits; only
     * compare the low 32 bits, matching the engine's representation. */
    T_ASSERT_EQ_I((int32_t)got, (int32_t)want);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_OWNER_B), 0);
    return 0;
}

int test_records_b_spawn_entity_alt_pos_path_when_flag_not_minus_one(void)
{
    /* flag != -1 → pos pulled from owner+0x9e0+flag*0x44. */
    reset_world();
    int flag = 3;
    int base = 0x9e0 + flag * 0x44;
    owner_write_f(g_owner_a, base + 0, 100.0f);
    owner_write_f(g_owner_a, base + 4, 200.0f);
    owner_write_f(g_owner_a, base + 8, 300.0f);

    /* Make sure the default-path source is something OBVIOUSLY wrong,
     * so an accidental fall-through to the default branch shows up. */
    owner_write_f(g_owner_a, 0x20, 9999.0f);
    owner_write_f(g_owner_a, 0x24, 9999.0f);
    owner_write_f(g_owner_a, 0x28, 9999.0f);

    scene1_record_b_spawn_entity(g_owner_a, 0x24, flag);

    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-6f);
    /* alt-path applies NO -0.5 bias on pos.y. */
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 200.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 300.0f) < 1e-6f);
    /* flag is stored at FLAG_B for entity alloc. */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_FLAG_B), flag);
    return 0;
}

int test_records_b_spawn_entity_copies_matrix(void)
{
    /* Owner matrix at owner+0xde8..0xe27 (16 floats) → slot+MATRIX0..+MATRIX0+15. */
    reset_world();
    float src[16];
    for (int k = 0; k < 16; k++) src[k] = (float)(k + 1) * 0.5f;
    memcpy(g_owner_a + 0xde8, src, sizeof src);

    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);

    for (int k = 0; k < 16; k++) {
        float got = slot_f(0, SCENE1_RECORDS_B_OFF_MATRIX0 + k);
        T_ASSERT(fabsf(got - src[k]) < 1e-6f);
    }
    return 0;
}

int test_records_b_spawn_entity_byte_pair_zeros_low_two_only(void)
{
    /* Pre-seed dw 48 with 0xAABBCCDD, then allocate.  Preamble zeros
     * bytes 0xc0 and 0xc1 (low 2 bytes of dw 48 on LE = byte 0 and 1
     * of the dword) but leaves bytes 0xc2 and 0xc3 untouched. */
    reset_world();
    uint8_t *bytes = (uint8_t *)&g_scene1_records_b[0 * SCENE1_RECORDS_B_STRIDE];
    bytes[0xc0] = 0xAA;
    bytes[0xc1] = 0xBB;
    bytes[0xc2] = 0xCC;
    bytes[0xc3] = 0xDD;

    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);

    T_ASSERT_EQ_I(bytes[0xc0], 0x00);
    T_ASSERT_EQ_I(bytes[0xc1], 0x00);
    T_ASSERT_EQ_I(bytes[0xc2], 0xCC);   /* untouched */
    T_ASSERT_EQ_I(bytes[0xc3], 0xDD);   /* untouched */
    return 0;
}

int test_records_b_spawn_entity_sequence_counter_increments(void)
{
    reset_world();
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 0);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_SEQ_ID), 1);
    T_ASSERT_EQ_I(slot_i(2, SCENE1_RECORDS_B_OFF_SEQ_ID), 2);
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 3);
    return 0;
}

int test_records_b_spawn_entity_skips_alive_slots(void)
{
    reset_world();
    /* Stamp slot 0 as alive. */
    slot_i(0, 0);
    g_scene1_records_b[0 * SCENE1_RECORDS_B_STRIDE
                       + SCENE1_RECORDS_B_OFF_TYPE] = 99;
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 99);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0x24);
    return 0;
}

int test_records_b_spawn_entity_table_full_no_commit(void)
{
    reset_world();
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE
                           + SCENE1_RECORDS_B_OFF_TYPE] = 1;
    }
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    /* Counter not incremented since no slot was claimed. */
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 0);
    /* No slot's type was reset to 0x24. */
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        T_ASSERT_EQ_I(slot_i(i, SCENE1_RECORDS_B_OFF_TYPE), 1);
    }
    return 0;
}

int test_records_b_spawn_entity_unimplemented_no_commit(void)
{
    /* C8j.5 divergence: unknown types trace but do NOT commit a slot. */
    reset_world();
    scene1_record_b_spawn_entity(g_owner_a, 0x99, -1);
    T_ASSERT_EQ_I(count_live(), 0);
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 0);
    return 0;
}

/* ─── entity allocator per-type bodies ────────────────────────────── */

int test_records_b_spawn_entity_60_writes_rot_x_from_owner_ea4(void)
{
    reset_world();
    owner_write_f(g_owner_a, 0xea4, 1.25f);
    scene1_record_b_spawn_entity(g_owner_a, 0x60, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x60);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X) - 1.25f) < 1e-6f);
    /* SCALE_X stays at preamble default 1.0f for type 0x60. */
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_spawn_entity_82_scale_2_and_rot_x(void)
{
    reset_world();
    owner_write_f(g_owner_a, 0xea4, -0.75f);
    scene1_record_b_spawn_entity(g_owner_a, 0x82, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x82);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X) - (-0.75f)) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X) - 2.0f) < 1e-6f);
    return 0;
}

/* ─── NPC allocator preamble ──────────────────────────────────────── */

int test_records_b_spawn_npc_e_pure_preamble(void)
{
    reset_world();
    owner_write_f(g_owner_b, 0x3f0,  7.0f);
    owner_write_f(g_owner_b, 0x3f4, 11.0f);
    owner_write_f(g_owner_b, 0x3f8, 13.0f);

    scene1_record_b_spawn_npc(g_owner_b, 0xe, /*flag=*/0xABCD);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xe);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 7.0f) < 1e-6f);
    /* NPC alloc applies NO -0.5 bias on pos.y. */
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 11.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 13.0f) < 1e-6f);
    /* flag lands at FLAG_A (dw 1) for NPC alloc — NOT at FLAG_B. */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_FLAG_A), (int32_t)0xABCD);
    /* FLAG_B is hardcoded 0xffffffff (NOT flag). */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_FLAG_B), -1);
    /* Owner ref lands at OWNER_B (dw 5); OWNER_A (dw 4) is zeroed. */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_OWNER_A), 0);
    T_ASSERT_EQ_I((int32_t)slot_i(0, SCENE1_RECORDS_B_OFF_OWNER_B),
                  (int32_t)(intptr_t)g_owner_b);
    /* AUX_SENT2 + OWNER_FLAG + SCALE_Y are NOT written by NPC preamble
     * — entity alloc only.  Slot was zero-init from reset_world() so
     * they stay zero. */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT2), 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_OWNER_FLAG), 0);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_Y) - 0.0f) < 1e-6f);
    /* Default-written fields hold same values as entity alloc. */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AGE), 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT1), -1);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_spawn_npc_copies_matrix_from_owner_39c(void)
{
    reset_world();
    float src[16];
    for (int k = 0; k < 16; k++) src[k] = (float)(k + 1) * -0.25f;
    memcpy(g_owner_b + 0x39c, src, sizeof src);

    scene1_record_b_spawn_npc(g_owner_b, 0xe, 0);

    for (int k = 0; k < 16; k++) {
        float got = slot_f(0, SCENE1_RECORDS_B_OFF_MATRIX0 + k);
        T_ASSERT(fabsf(got - src[k]) < 1e-6f);
    }
    return 0;
}

int test_records_b_spawn_npc_all_three_anchor_types_commit(void)
{
    reset_world();
    scene1_record_b_spawn_npc(g_owner_b, 0xe,  0);
    scene1_record_b_spawn_npc(g_owner_b, 0x97, 0);
    scene1_record_b_spawn_npc(g_owner_b, 0x46, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xe);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0x97);
    T_ASSERT_EQ_I(slot_i(2, SCENE1_RECORDS_B_OFF_TYPE), 0x46);
    return 0;
}

int test_records_b_spawn_npc_shares_seq_counter_with_entity(void)
{
    /* Engine DAT_06a46fb8 is shared across both allocators. */
    reset_world();
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    scene1_record_b_spawn_npc(g_owner_b, 0xe, 0);
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 0);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_SEQ_ID), 1);
    T_ASSERT_EQ_I(slot_i(2, SCENE1_RECORDS_B_OFF_SEQ_ID), 2);
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 3);
    return 0;
}

int test_records_b_spawn_npc_unimplemented_no_commit(void)
{
    reset_world();
    scene1_record_b_spawn_npc(g_owner_b, 0xdead, 0);
    T_ASSERT_EQ_I(count_live(), 0);
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 0);
    return 0;
}

/* ─── counter scan integration ────────────────────────────────────── */

int test_records_b_spawn_drives_counter_scan(void)
{
    /* After 3 spawns into slots 0..2, the counter scan should report
     * g_scene1_records_b_count == 3 (one past the last non-sentinel). */
    reset_world();
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    scene1_record_b_spawn_entity(g_owner_a, 0x24, -1);
    scene1_record_b_spawn_npc(g_owner_b, 0x97, 0);
    scene1_records_counter_scan();
    T_ASSERT_EQ_I(g_scene1_records_b_count, 3);
    return 0;
}

/* ─── C8j.6 drift cluster (types 2/3/4/0x22/0x54/0x67) ────────────── */

/* Engine 2π/8. */
#define DRIFT_BEND(npc_idx) ((float)(npc_idx) * 6.2831855f / 8.0f)
#define APPROX(a, b) (fabsf((a) - (b)) < 1e-4f)

/* Standard owner setup for drift-cluster + cluster-A tests. */
static void seed_owner_a_drift(void)
{
    /* pos triple at 0x20/0x24/0x28; preamble subtracts 0.5 from POS_Y. */
    owner_write_f(g_owner_a, 0x20, 10.0f);
    owner_write_f(g_owner_a, 0x24, 20.0f);
    owner_write_f(g_owner_a, 0x28, 30.0f);
    /* NPC bend index (read as int): 2 → bend = 2 * 2π/8 = π/2. */
    owner_write_i(g_owner_a, 0x948, 2);
    /* sin/cos angle (read as float): 0 → sin=0, cos=1. */
    owner_write_f(g_owner_a, 0xea4, 0.0f);
}

int test_records_b_spawn_entity_drift_writes_rot_x_from_npc_bend(void)
{
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x67, -1);

    /* L41597-41598: ROT_X = (owner+0x948) * 2π/8 = 2 * π/4 = π/2. */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x67);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X),
                    DRIFT_BEND(2)));
    return 0;
}

int test_records_b_spawn_entity_drift_writes_vel_from_owner_ea4(void)
{
    /* ang = π/2 → sin=1, cos≈0.  vel = (3, 0, ~0). */
    reset_world();
    seed_owner_a_drift();
    owner_write_f(g_owner_a, 0xea4, 3.1415927f / 2.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x54, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 3.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.0f));
    return 0;
}

int test_records_b_spawn_entity_drift_writes_pos_delta_from_preamble(void)
{
    /* ang = 0 → sin=0, cos=1.
     * Preamble: POS = (10, 19.5, 30)  (-0.5 y bias).
     * Body:     POS.x -= 0  = 10
     *           POS.y += 1  = 20.5
     *           POS.z -= 0.5 = 29.5 */
    reset_world();
    seed_owner_a_drift();
    /* Need scale to match: owner+0xea4 = 0. */
    owner_write_f(g_owner_a, 0xea4, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 4, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 20.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 29.5f));
    return 0;
}

int test_records_b_spawn_entity_drift_scale_x_per_type(void)
{
    /* 0x22 → 2.0; 0x67 → 1.2; 3 with flag != -1 → 0.5; default → 1.0. */
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x22, -1);   /* slot 0: SCALE_X=2.0 */
    scene1_record_b_spawn_entity(g_owner_a, 0x67, -1);   /* slot 1: SCALE_X=1.2 */
    scene1_record_b_spawn_entity(g_owner_a, 3,    5);    /* slot 2: SCALE_X=0.5 (flag != -1) */
    scene1_record_b_spawn_entity(g_owner_a, 3,    -1);   /* slot 3: SCALE_X=1.0 (flag == -1) */
    scene1_record_b_spawn_entity(g_owner_a, 2,    -1);   /* slot 4: SCALE_X=1.0 (preamble default) */
    scene1_record_b_spawn_entity(g_owner_a, 4,    -1);   /* slot 5: SCALE_X=1.0 */
    scene1_record_b_spawn_entity(g_owner_a, 0x54, -1);   /* slot 6: SCALE_X=1.0 */

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 2.0f));
    T_ASSERT(APPROX(slot_f(1, SCENE1_RECORDS_B_OFF_SCALE_X), 1.2f));
    T_ASSERT(APPROX(slot_f(2, SCENE1_RECORDS_B_OFF_SCALE_X), 0.5f));
    T_ASSERT(APPROX(slot_f(3, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    T_ASSERT(APPROX(slot_f(4, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    T_ASSERT(APPROX(slot_f(5, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    T_ASSERT(APPROX(slot_f(6, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    return 0;
}

int test_records_b_spawn_entity_drift_tail_writes_drag_aux_c8(void)
{
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x22, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    return 0;
}

int test_records_b_spawn_entity_drift_writes_random_rot_z(void)
{
    /* LAB_004449b0: ROT_Z = rng_next_unit() * 2π.  Two back-to-back
     * spawns should produce DIFFERENT rot_z values (the RNG advances). */
    reset_world();
    seed_owner_a_drift();
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 2, -1);
    float a = slot_f(0, SCENE1_RECORDS_B_OFF_ROT_Z);
    scene1_record_b_spawn_entity(g_owner_a, 2, -1);
    float b = slot_f(1, SCENE1_RECORDS_B_OFF_ROT_Z);

    T_ASSERT(a >= 0.0f && a <= 6.2831855f);
    T_ASSERT(b >= 0.0f && b <= 6.2831855f);
    T_ASSERT(!APPROX(a, b));   /* RNG advanced — values differ. */
    return 0;
}

int test_records_b_spawn_entity_drift_all_six_types_implemented(void)
{
    reset_world();
    seed_owner_a_drift();
    int types[6] = {2, 3, 4, 0x22, 0x54, 0x67};
    for (int k = 0; k < 6; k++) {
        scene1_record_b_spawn_entity(g_owner_a, types[k], -1);
    }
    for (int k = 0; k < 6; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), types[k]);
    }
    return 0;
}

/* ─── C8j.6 cluster A (types 0x4d-0x50, 0xa5-0xa6, 99, 0x51-0x53) ─── */

int test_records_b_spawn_entity_cluster_a_4d_writes_vel_y_and_life(void)
{
    /* 0x4d → LIFE_MULT=0.32, SCALE_X=0.7, VEL_Y=0.07, vel mag = 0.3. */
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x4d, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x4d);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.32f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 0.7f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.07f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    return 0;
}

int test_records_b_spawn_entity_cluster_a_main6_overrides_pos(void)
{
    /* 0x4e (main-6) overrides pos to sin/cos(local_c)*0.8 + +1.4y.
     * With bend=π/2 (npc=2) and part_idx=0, local_c = π/2:
     *   sin(π/2) = 1, cos(π/2) ≈ 0.
     *   POS_X = 1.0 * 0.8 + 10 = 10.8
     *   POS_Y = 20.0 + 1.4 = 21.4
     *   POS_Z ≈ 0 * 0.8 + 30 = 30. */
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x4e, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 21.4f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 30.0f));
    /* main-6 LIFE_MULT = 0.4. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.4f));
    return 0;
}

int test_records_b_spawn_entity_cluster_a_53_pos_y_no_lift_and_byte(void)
{
    /* 0x53 sets POS_Y = owner.y (no +0.7 lift), no main-6 override.
     * Also writes slot byte 0xc0 = 3. */
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x53, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x53);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 20.0f));

    uint8_t *bytes = (uint8_t *)&g_scene1_records_b[0 * SCENE1_RECORDS_B_STRIDE];
    T_ASSERT_EQ_I(bytes[0xc0], 3);
    T_ASSERT_EQ_I(bytes[0xc1], 0);   /* preamble zeroed; not overwritten. */
    return 0;
}

int test_records_b_spawn_entity_cluster_a_default_types_pos_with_lift(void)
{
    /* 99 / 0x51 / 0x52 use default pos: sin/cos(local_c)*0.3 + +0.7y.
     * With bend=π/2 (npc=2): sin=1, cos≈0.
     *   POS_X = 1.0*0.3 + 10 = 10.3
     *   POS_Y = 20.0 + 0.7 = 20.7
     *   POS_Z ≈ 0*0.3 + 30 = 30. */
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 99, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 20.7f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 30.0f));
    /* SCALE_X for 99 = 1.8; LIFE_MULT = 1.5. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.5f));
    return 0;
}

int test_records_b_spawn_entity_cluster_a_51_writes_scale_x_1_5(void)
{
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x51, -1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.5f));
    return 0;
}

int test_records_b_spawn_entity_cluster_a_4f_spawns_3_particles(void)
{
    /* 0x4f → iVar10 = 3.  Three slots commit in row. */
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x4f, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x4f);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0x4f);
    T_ASSERT_EQ_I(slot_i(2, SCENE1_RECORDS_B_OFF_TYPE), 0x4f);
    T_ASSERT_EQ_I(slot_i(3, SCENE1_RECORDS_B_OFF_TYPE), 0);   /* 4th slot stays free */
    /* Each particle gets its own sequence ID (0, 1, 2). */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 0);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_SEQ_ID), 1);
    T_ASSERT_EQ_I(slot_i(2, SCENE1_RECORDS_B_OFF_SEQ_ID), 2);
    return 0;
}

int test_records_b_spawn_entity_cluster_a_50_spawns_5_particles(void)
{
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0x50, -1);
    for (int k = 0; k < 5; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x50);
    }
    T_ASSERT_EQ_I(slot_i(5, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_cluster_a_a5_spawns_6_particles(void)
{
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0xa5, -1);
    for (int k = 0; k < 6; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0xa5);
    }
    T_ASSERT_EQ_I(slot_i(6, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_cluster_a_a6_spawns_8_particles(void)
{
    reset_world();
    seed_owner_a_drift();
    scene1_record_b_spawn_entity(g_owner_a, 0xa6, -1);
    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0xa6);
    }
    T_ASSERT_EQ_I(slot_i(8, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_cluster_a_per_particle_angle_shifts(void)
{
    /* 0x4f has 3 particles.  With bend = 0 (npc index 0) the per-
     * particle local_c values are:
     *   particle 0: 0      → sin=0,         cos=1
     *   particle 1: -0.18  → sin=-0.17903,  cos=0.98384
     *   particle 2: +0.18  → sin=+0.17903,  cos=0.98384
     * ROT_X is set to local_c per particle. */
    reset_world();
    seed_owner_a_drift();
    owner_write_i(g_owner_a, 0x948, 0);   /* bend = 0 */
    scene1_record_b_spawn_entity(g_owner_a, 0x4f, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X),  0.0f));
    T_ASSERT(APPROX(slot_f(1, SCENE1_RECORDS_B_OFF_ROT_X), -0.18f));
    T_ASSERT(APPROX(slot_f(2, SCENE1_RECORDS_B_OFF_ROT_X), +0.18f));
    return 0;
}

int test_records_b_spawn_entity_cluster_a_vel_uses_local_10_0_3_for_4d(void)
{
    /* 0x4d has local_10 = 0.3.  With local_c=0 (bend 0, part_idx 0):
     *   VEL_X = sin(0)*0.3 = 0
     *   VEL_Z = cos(0)*0.3 = 0.3 */
    reset_world();
    seed_owner_a_drift();
    owner_write_i(g_owner_a, 0x948, 0);
    scene1_record_b_spawn_entity(g_owner_a, 0x4d, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.3f));
    return 0;
}

int test_records_b_spawn_entity_cluster_a_vel_uses_local_10_0_5_for_4e(void)
{
    /* 0x4e (main-6) has default local_10 = 0.5.
     *   VEL_X = sin(0)*0.5 = 0
     *   VEL_Z = cos(0)*0.5 = 0.5 */
    reset_world();
    seed_owner_a_drift();
    owner_write_i(g_owner_a, 0x948, 0);
    scene1_record_b_spawn_entity(g_owner_a, 0x4e, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.5f));
    /* 0x4e VEL_Y stays at 0 (only 0x4d gets 0.07). */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    return 0;
}

int test_records_b_spawn_entity_cluster_a_4e_rot_x_equals_local_c(void)
{
    /* Sanity check: ROT_X = local_c (the per-particle angle). */
    reset_world();
    seed_owner_a_drift();
    /* bend = 4 * 2π/8 = π. */
    owner_write_i(g_owner_a, 0x948, 4);
    scene1_record_b_spawn_entity(g_owner_a, 0x4e, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 3.1415927f));
    return 0;
}

int test_records_b_spawn_entity_cluster_a_implemented_macro(void)
{
    /* Sanity check: all 17 new types report as implemented. */
    int types[17] = {
        2, 3, 4, 0x22, 0x54, 0x67,
        0x4d, 0x4e, 0x4f, 0x50, 0xa5, 0xa6, 99, 0x51, 0x52, 0x53,
        0x24,   /* C8j.5 carryover sanity */
    };
    for (int k = 0; k < 17; k++) {
        T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(types[k]));
    }
    /* Unimplemented sanity. */
    T_ASSERT(!SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x5a));
    T_ASSERT(!SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0xdead));
    return 0;
}

/* ─── C8j.7 mega-cluster A (entity 0x73/0x76/0x77/0x78/0x7a/0x7b/0x7c/0x7e) ─ */

/* Standard owner setup for mega-cluster + NPC-cluster-B tests. */
static void seed_owner_a_mega(int npc_mode)
{
    owner_write_f(g_owner_a, 0x20, 10.0f);
    owner_write_f(g_owner_a, 0x24, 20.0f);
    owner_write_f(g_owner_a, 0x28, 30.0f);
    owner_write_i(g_owner_a, 0x948, npc_mode);   /* bend mode (3-way) */
    owner_write_f(g_owner_a, 0xea4, 0.0f);       /* 0x7a override angle */
    owner_write_i(g_owner_a, 0xe3c, 0);          /* sub-frame counter */
}

int test_records_b_spawn_entity_mega_77_one_particle(void)
{
    /* 0x77 has cap=1: only slot 0 commits. */
    reset_world();
    seed_owner_a_mega(2);   /* "else" branch on 3-way dispatch */
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x77, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x77);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    return 0;
}

int test_records_b_spawn_entity_mega_73_four_particles(void)
{
    reset_world();
    seed_owner_a_mega(0);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x73, -1);

    for (int k = 0; k < 4; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x73);
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_SCALE_X), 0.25f));
    }
    T_ASSERT_EQ_I(slot_i(4, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_mega_7c_five_particles_with_neg_age(void)
{
    /* 0x7c: cap=5; AGE per particle = part_idx * -4.
     *   particle 0 → AGE = 0
     *   particle 1 → AGE = -4
     *   particle 2 → AGE = -8
     *   particle 3 → AGE = -12
     *   particle 4 → AGE = -16 */
    reset_world();
    seed_owner_a_mega(2);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x7c, -1);

    int expected_age[5] = {0, -4, -8, -12, -16};
    for (int k = 0; k < 5; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x7c);
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_AGE), expected_age[k]);
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_SCALE_X), 0.5f));
    }
    T_ASSERT_EQ_I(slot_i(5, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_mega_76_eight_particles_part_idx_flag(void)
{
    /* 0x76: cap=8.  PART_IDX = 1 for any particle with part_idx > 0
     * (and PART_IDX stays at 0 for the first particle since the gate
     * is `part_idx > 0`). */
    reset_world();
    seed_owner_a_mega(0);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x76, -1);

    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x76);
    }
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    T_ASSERT_EQ_I(slot_i(7, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    return 0;
}

int test_records_b_spawn_entity_mega_78_eight_with_life_mult(void)
{
    reset_world();
    seed_owner_a_mega(0);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x78, -1);

    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x78);
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.15f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_SCALE_X), 0.125f));
    }
    return 0;
}

int test_records_b_spawn_entity_mega_7e_life_mult_and_scale(void)
{
    reset_world();
    seed_owner_a_mega(0);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x7e, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.4f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 0.3f));
    return 0;
}

int test_records_b_spawn_entity_mega_pos_with_bend_mode_else(void)
{
    /* npc=2 → "else" branch on 3-way: pos.z -= 0.1; alt_z -= 0.1.
     * bend = 2 * 2π/8 = π/2.  sin(π/2)=1, cos(π/2)≈0.
     *   pos.x = 1*1.2 + 10 = 11.2
     *   pos.y = 20.0 + 1.3 = 21.3
     *   pos.z = 0*1.2 + 30 - 0.1 = 29.9
     *   alt.x = 1*0.8 + 10 = 10.8
     *   alt.y = 20.0 + 1.3 = 21.3
     *   alt.z = 0*0.8 + 30 - 0.1 = 29.9 */
    reset_world();
    seed_owner_a_mega(2);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x7b, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 11.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 21.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 29.9f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X), 10.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 21.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z), 29.9f));
    return 0;
}

int test_records_b_spawn_entity_mega_pos_with_bend_mode_0(void)
{
    /* npc=0 → mode-0 branch: pos.x -= 0.41; alt_x -= 0.41.
     * bend = 0 * 2π/8 = 0.  sin(0)=0, cos(0)=1.
     *   pos.x = 0*1.2 + 10 - 0.41 = 9.59
     *   pos.z = 1*1.2 + 30 = 31.2
     *   alt.x = 0*0.8 + 10 - 0.41 = 9.59
     *   alt.z = 1*0.8 + 30 = 30.8 */
    reset_world();
    seed_owner_a_mega(0);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x7b, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 9.59f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 31.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X), 9.59f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z), 30.8f));
    return 0;
}

int test_records_b_spawn_entity_mega_pos_with_bend_mode_4(void)
{
    /* npc=4 → mode-4 branch: pos.x += 0.41; alt_x += 0.41.
     * bend = 4 * 2π/8 = π.  sin(π)≈0, cos(π)=-1.
     *   pos.x = 0*1.2 + 10 + 0.41 ≈ 10.41
     *   pos.z = -1*1.2 + 30 = 28.8 */
    reset_world();
    seed_owner_a_mega(4);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x7b, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.41f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 28.8f));
    return 0;
}

int test_records_b_spawn_entity_mega_7b_rot_x_is_local_c(void)
{
    /* 0x7b sets ROT_X = local_c, local_10 = 0.24, VEL_Y = 0.1.
     * With npc=2 → bend = π/2.  ROT_X = π/2 ≈ 1.5708.
     * VEL_X = sin(π/2) * 0.24 = 0.24
     * VEL_Z = cos(π/2) * 0.24 ≈ 0. */
    reset_world();
    seed_owner_a_mega(2);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x7b, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 3.1415927f / 2.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.24f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.1f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-4f);
    return 0;
}

int test_records_b_spawn_entity_mega_7a_local_c_override(void)
{
    /* 0x7a swaps local_c with owner+0xea4 AFTER pos writes.  Pos still
     * uses bend angle.  ROT_X subsequent overrides use the EA4 angle.
     *
     * With npc=2 → bend = π/2 (pos uses this).
     * With owner+0xea4 = 1.0 → local_c = 1.0 for the rest.
     * 0x7a's ROT_X = (u-0.5)*0.3 + local_c (random); test only that
     * the result is roughly in [local_c - 0.15, local_c + 0.15]. */
    reset_world();
    seed_owner_a_mega(2);
    owner_write_f(g_owner_a, 0xea4, 1.0f);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x7a, -1);

    /* Pos still uses bend = π/2: pos.x = sin(π/2)*1.2 + 10 - 0 = 11.2 */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 11.2f));
    /* ROT_X is RNG-shifted around local_c = 1.0; bounds [0.85, 1.15]. */
    float rx = slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X);
    T_ASSERT(rx >= 0.85f - 1e-3f && rx <= 1.15f + 1e-3f);
    return 0;
}

int test_records_b_spawn_entity_mega_7c_rebound_pos_decremented(void)
{
    /* 0x7c does POS_X/Z -= 2*VEL_X/Z after vel write. */
    reset_world();
    seed_owner_a_mega(0);
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x7c, -1);

    /* Spot-check particle 0: pos and vel should be related via the
     * rebound, regardless of the exact angle. */
    float pos_x0 = slot_f(0, SCENE1_RECORDS_B_OFF_POS_X);
    float vel_x0 = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X);
    /* npc=0 mode → original pos.x = sin(bend)*1.2 + 10 - 0.41 = -0.41.
     * Rebound applies 2*vel.x subtraction to that:
     *   pos.x = -0.41 + 10 - 2*vel.x = 9.59 - 2*vel.x */
    T_ASSERT(APPROX(pos_x0, 9.59f - 2.0f * vel_x0));
    return 0;
}

int test_records_b_spawn_entity_mega_implemented_macro(void)
{
    int types[8] = {0x73, 0x76, 0x77, 0x78, 0x7a, 0x7b, 0x7c, 0x7e};
    for (int k = 0; k < 8; k++) {
        T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(types[k]));
    }
    return 0;
}

/* ─── C8j.7 NPC cluster B (NPC 0x4d/0x4e/0x4f/0x50/0xa5/0xa6) ────── */

static void seed_owner_b_cluster_b(int bend_idx)
{
    owner_write_i(g_owner_b, 0x18, bend_idx);    /* NPC bend at +0x18 */
    owner_write_f(g_owner_b, 0x3f0, 100.0f);
    owner_write_f(g_owner_b, 0x3f4, 200.0f);
    owner_write_f(g_owner_b, 0x3f8, 300.0f);
}

int test_records_b_spawn_npc_cluster_b_4d_writes_basic_fields(void)
{
    /* bend = 2 * 2π/8 = π/2.  sin(π/2)=1, cos(π/2)≈0.
     *   POS_X = 1.0 * 0.8 + 100 = 100.8
     *   POS_Y = 200.0 + 1.4 = 201.4
     *   POS_Z ≈ 0 * 0.8 + 300 = 300
     *   VEL_X = 1.0 * 0.5 = 0.5
     *   VEL_Z ≈ 0
     *   ROT_X = π/2 ≈ 1.5708 */
    reset_world();
    seed_owner_b_cluster_b(2);
    scene1_record_b_spawn_npc(g_owner_b, 0x4d, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x4d);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 201.4f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.5f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-4f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 3.1415927f / 2.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.4f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    return 0;
}

int test_records_b_spawn_npc_cluster_b_4f_spawns_3_particles(void)
{
    reset_world();
    seed_owner_b_cluster_b(0);
    scene1_record_b_spawn_npc(g_owner_b, 0x4f, 0);

    for (int k = 0; k < 3; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x4f);
    }
    T_ASSERT_EQ_I(slot_i(3, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_npc_cluster_b_a6_spawns_8_particles(void)
{
    reset_world();
    seed_owner_b_cluster_b(0);
    scene1_record_b_spawn_npc(g_owner_b, 0xa6, 0);

    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0xa6);
    }
    T_ASSERT_EQ_I(slot_i(8, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_npc_cluster_b_per_particle_shifts(void)
{
    /* 0x4f spawns 3 particles.  With bend=0:
     *   particle 0: local_1c = 0
     *   particle 1: local_1c = -0.18
     *   particle 2: local_1c = +0.18
     * ROT_X = local_1c per particle. */
    reset_world();
    seed_owner_b_cluster_b(0);
    scene1_record_b_spawn_npc(g_owner_b, 0x4f, 0);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X),  0.0f));
    T_ASSERT(APPROX(slot_f(1, SCENE1_RECORDS_B_OFF_ROT_X), -0.18f));
    T_ASSERT(APPROX(slot_f(2, SCENE1_RECORDS_B_OFF_ROT_X), +0.18f));
    return 0;
}

int test_records_b_spawn_npc_cluster_b_4e_one_particle(void)
{
    reset_world();
    seed_owner_b_cluster_b(0);
    scene1_record_b_spawn_npc(g_owner_b, 0x4e, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x4e);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_npc_cluster_b_implemented_macro(void)
{
    int types[6] = {0x4d, 0x4e, 0x4f, 0x50, 0xa5, 0xa6};
    for (int k = 0; k < 6; k++) {
        T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(types[k]));
    }
    /* Anchor types still implemented. */
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0xe));
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x97));
    /* C8j.10 landed 0x56/0x53/0x51/0x68. */
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x56));
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x53));
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x51));
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x68));
    /* 0x36 landed in C8j.13. 0x84 is the atan2 player-aim from C8j.11. */
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x36));
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x84));
    return 0;
}

/* ─── C8j.10 — NPC single-spawn types (0x56 / 0x53 / 0x51 / 0x68) ────── */

#include "math3d.h"                 /* mat4_rotation_x / _y / mat4_mul */
#include "scene1_particles_tick.h"  /* g_scene1_player_pos (0x68 alt-target) */

int test_records_b_spawn_npc_56_matrix_init_and_lift(void)
{
    /* bend = 2 * 2π/8 = π/2 → sin(bend)=1, cos(bend)=0.
     *   POS_X = 1*1.5 + 100 = 101.5
     *   POS_Y = 200 + 1.8   = 201.8
     *   POS_Z = 0*1.5 + 300 = 300
     *   VEL_X = 1*0.3       = 0.3
     *   VEL_Y = 0.15
     *   VEL_Z = 0*0.3       = 0
     *   ROT_X = bend        = π/2
     *   LIFE_MULT = 0.15
     *   DRAG = 0.5
     *   AUX_C8 = 1 */
    reset_world();
    seed_owner_b_cluster_b(2);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x56, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x56);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 101.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 201.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.15f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-4f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.15f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    /* Cap = 1 — no second slot. */
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_npc_56_matrix_is_roty_times_rotx(void)
{
    /* Re-derive the matrix using the same RNG seed and the documented
     * formula: MATRIX0 = RotY(rot_y) × RotX(rot_x) where rot_x is the
     * 1st rng_next_unit()*2π and rot_y is the 2nd.  Engine order:
     *   1. rng → rot_x  → slot.ROT_SCR
     *   2. rng → rot_y  → slot.ROT_Z
     *   3. MATRIX0 = mat_rot_y(rot_y) * mat_rot_x(rot_x) */
    reset_world();
    seed_owner_b_cluster_b(0);

    rng_seed(1);
    float rot_x_angle = rng_next_unit() * 6.2831855f;
    float rot_y_angle = rng_next_unit() * 6.2831855f;
    float expect_rx[16], expect_ry[16], expect_out[16];
    mat4_rotation_x(expect_rx, rot_x_angle);
    mat4_rotation_y(expect_ry, rot_y_angle);
    mat4_mul(expect_out, expect_ry, expect_rx);

    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x56, 0);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR), rot_x_angle));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_Z),   rot_y_angle));
    for (int k = 0; k < 16; k++) {
        T_ASSERT(APPROX(
            slot_f(0, SCENE1_RECORDS_B_OFF_MATRIX0 + k),
            expect_out[k]));
    }
    return 0;
}

int test_records_b_spawn_npc_53_low_lift(void)
{
    /* bend = π/2 → sin=1, cos=0.
     *   POS_X = 1*0.3 + 100 = 100.3
     *   POS_Y = 200 + 0.08   = 200.08
     *   POS_Z = 0*0.3 + 300 = 300
     *   VEL_X = 1*0.5       = 0.5
     *   VEL_Y = 0
     *   VEL_Z = 0
     *   ROT_X = π/2
     *   DRAG  = 0.5
     *   AUX_C8 = 0 (NOT 1 — 0x53 skips LAB_004469d2) */
    reset_world();
    seed_owner_b_cluster_b(2);
    scene1_record_b_spawn_npc(g_owner_b, 0x53, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x53);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 200.08f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-4f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
    /* AUX_C8 stays at preamble 0 (0x53 does NOT set 1). */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 0);
    /* LIFE_MULT also untouched → preamble default 1.0. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.0f));
    return 0;
}

int test_records_b_spawn_npc_51_mid_lift_cap_1(void)
{
    /* bend = π/2.  part_idx=0 → shift=0 → local_1c = π/2.
     *   POS_X = 1*0.3 + 100 = 100.3
     *   POS_Y = 200 + 0.7   = 200.7
     *   POS_Z = 0*0.3 + 300 = 300
     *   VEL_X = 1*0.5       = 0.5
     *   VEL_Z = 0
     *   ROT_X = π/2
     *   DRAG = 0.5, AUX_C8 = 1 (LAB_00445c9a → LAB_004469d2)
     *   Cap = 1. */
    reset_world();
    seed_owner_b_cluster_b(2);
    scene1_record_b_spawn_npc(g_owner_b, 0x51, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x51);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 200.7f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.5f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-4f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_npc_lab_00447584_group_b_preamble_only(void)
{
    /* 0x24, 0xa, 0xb, 0x14, 0x13, 0x99 — pure preamble-only types
     * (same shape as the existing 0xe/0x97/0x46 anchors).  Slot ends up
     * at preamble defaults: TYPE claimed, POS = owner+0x3f0..0x3f8,
     * VEL = 0, SCALE_X = 1.0, LIFE_MULT = 1.0, AUX_C8 = 0. */
    int types[6] = {0x24, 0xa, 0xb, 0x14, 0x13, 0x99};
    for (int k = 0; k < 6; k++) {
        reset_world();
        owner_write_f(g_owner_b, 0x3f0, 100.0f);
        owner_write_f(g_owner_b, 0x3f4, 200.0f);
        owner_write_f(g_owner_b, 0x3f8, 300.0f);
        scene1_record_b_spawn_npc(g_owner_b, types[k], 0);

        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), types[k]);
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 200.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.0f));
        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 0);
        /* Cap = 1. */
        T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    }
    return 0;
}

int test_records_b_spawn_npc_explicit_return_group(void)
{
    /* 0x1e, 0x88, 0x89, 0x9a — body writes pos/alt/vel from
     * owner+0x420.  0x9e additionally writes LIFE_MULT=1.8 +
     * SCALE_X=10.0.  ang = π/2 → sin=1, cos=0 → VEL = (2, 0, 0). */
    int types[4] = {0x1e, 0x88, 0x89, 0x9a};
    for (int k = 0; k < 4; k++) {
        reset_world();
        owner_write_f(g_owner_b, 0x3f0, 100.0f);
        owner_write_f(g_owner_b, 0x3f4, 200.0f);
        owner_write_f(g_owner_b, 0x3f8, 300.0f);
        owner_write_f(g_owner_b, 0x420, 1.5707963f);   /* π/2 */
        scene1_record_b_spawn_npc(g_owner_b, types[k], 0);

        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), types[k]);
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 201.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X), 100.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 200.9f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z), 300.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 2.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
        T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-4f);
        /* Non-0x9e types do NOT write LIFE_MULT / SCALE_X. */
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.0f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    }

    /* 0x9e — extra LIFE_MULT/SCALE_X writes. */
    reset_world();
    owner_write_f(g_owner_b, 0x3f0, 100.0f);
    owner_write_f(g_owner_b, 0x3f4, 200.0f);
    owner_write_f(g_owner_b, 0x3f8, 300.0f);
    owner_write_f(g_owner_b, 0x420, 0.0f);   /* sin=0 cos=1 */
    scene1_record_b_spawn_npc(g_owner_b, 0x9e, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x9e);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 2.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X),   10.0f));
    return 0;
}

int test_records_b_spawn_npc_68_player_aim_alt_target(void)
{
    /* Primary pos uses owner+0x3f0/0x3f4/0x3f8 + sin/cos*amp ring.
     * Alt-target uses g_scene1_player_pos[3] + ring.
     * Y-only invariants (RNG-independent):
     *   POS_Y = owner+0x3f4 + 20 = 220
     *   ALT_Y = g_scene1_player_pos[1]
     *   VEL_Y = (ALT_Y - POS_Y) / 10
     *   LIFE_MULT = 0.6
     *   PART_IDX = 0 */
    reset_world();
    owner_write_f(g_owner_b, 0x3f0, 100.0f);
    owner_write_f(g_owner_b, 0x3f4, 200.0f);
    owner_write_f(g_owner_b, 0x3f8, 300.0f);
    g_scene1_player_pos[0] =   1.0f;
    g_scene1_player_pos[1] =  50.0f;
    g_scene1_player_pos[2] =   3.0f;
    rng_seed(1);

    scene1_record_b_spawn_npc(g_owner_b, 0x68, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x68);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 220.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 50.0f));
    /* vel.y = (50 - 220) / 10 = -17.0 */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), -17.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.6f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    /* Cap = 1. */
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);

    /* Reset player_pos so subsequent tests aren't affected by our writes. */
    g_scene1_player_pos[0] = 0.0f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 0.0f;
    return 0;
}

/* ─── C8j.8 — NPC-table + camera-yaw + matrix-init types ─────────── */

#include "scene1_particles_tick.h"  /* g_scene1_camera_yaw, g_scene1_people */

/* Camera-yaw + people-table seed for the C8j.8 tests.  Sets owner+0x20
 * (preamble default pos), owner+0x38..0x40 (0x23/0x30 alt pos source),
 * owner+0xea0 (people index, -1 = no people fallback), owner+0xea4
 * (drift angle), owner+0x948 (NPC bend), owner+0xeac (flag inherit).
 * Caller can override post-seed for branch-specific tests. */
static void seed_owner_a_yaw(void)
{
    /* Preamble pos. */
    owner_write_f(g_owner_a, 0x20, 10.0f);
    owner_write_f(g_owner_a, 0x24, 20.0f);
    owner_write_f(g_owner_a, 0x28, 30.0f);
    /* Alt pos source (0x38/0x3c/0x40, used by 0x23 / 0x30). */
    owner_write_f(g_owner_a, 0x38, 100.0f);
    owner_write_f(g_owner_a, 0x3c, 200.0f);
    owner_write_f(g_owner_a, 0x40, 300.0f);
    /* People-index default = -1 (no people-table branch). */
    owner_write_i(g_owner_a, 0xea0, -1);
    /* Drift angle source for 0x30's vel. */
    owner_write_f(g_owner_a, 0xea4, 0.0f);
    /* NPC bend index for 0x9b/0x9d. */
    owner_write_i(g_owner_a, 0x948, 2);
    /* Owner flag inherit (preamble copies to slot.OWNER_FLAG). */
    owner_write_i(g_owner_a, 0xeac, 0);

    /* Pin camera yaw to a known value: 0 → sin(-0)=0, cos(-0)=1. */
    g_scene1_camera_yaw = 0.0f;
}

int test_records_b_spawn_entity_3e_uses_owner_ea4_rot_x(void)
{
    /* 0x3e shares 0x60's body — ROT_X = owner+0xea4. */
    reset_world();
    owner_write_f(g_owner_a, 0xea4, 1.75f);
    scene1_record_b_spawn_entity(g_owner_a, 0x3e, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x3e);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.75f));
    /* SCALE_X stays at preamble default 1.0f for 0x3e (not 0x82). */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    return 0;
}

int test_records_b_spawn_entity_5f_uses_owner_ea4_rot_x(void)
{
    reset_world();
    owner_write_f(g_owner_a, 0xea4, -2.25f);
    scene1_record_b_spawn_entity(g_owner_a, 0x5f, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x5f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), -2.25f));
    return 0;
}

int test_records_b_spawn_entity_23_yaw_branch_writes_full_pose(void)
{
    /* people_idx = -1 (default).  yaw = 0 → sin(-0)=0, cos(-0)=1.
     *   POS_X = 0 * 15 + owner+0x38 = 100
     *   POS_Y = owner+0x3c + 30    = 230
     *   POS_Z = 1 * 15 + owner+0x40 = 315
     *   VEL = (0, -0.3, 0)
     *   LIFE_MULT = 1.2
     *   DRAG = 0 */
    reset_world();
    seed_owner_a_yaw();
    scene1_record_b_spawn_entity(g_owner_a, 0x23, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x23);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 230.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 315.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X),  0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), -0.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z),  0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    /* Only one particle (cap=1). */
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_23_people_branch_reads_target(void)
{
    /* people_idx = 5 → reads g_scene1_people[5].target + +20y. */
    reset_world();
    seed_owner_a_yaw();
    owner_write_i(g_owner_a, 0xea0, 5);
    /* Engine reads &DAT_0076bd60 = people-table base+0x0c = target[0..2]. */
    g_scene1_people[5].target[0] = 11.0f;
    g_scene1_people[5].target[1] = 22.0f;
    g_scene1_people[5].target[2] = 33.0f;

    scene1_record_b_spawn_entity(g_owner_a, 0x23, 5);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 11.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 22.0f + 20.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 33.0f));
    return 0;
}

int test_records_b_spawn_entity_23_writes_matrix_via_rotation_x(void)
{
    /* Matrix must be overwritten by RotationX(ROT_Z) — the preamble's
     * owner+0xde8 copy is discarded.  RotationX(a) at MATRIX0 has:
     *   M[0]=1, M[5]=cos(a), M[6]=sin(a), M[9]=-sin(a), M[10]=cos(a),
     *   M[15]=1 (per math3d's row-major D3DX layout).
     * Hard to assert the exact angle without knowing the rng draw, but
     * we can assert ROT_Z is in [0, 2π) and the matrix's [0]/[15] are
     * 1 and the [5]/[10] trig pair matches cos(ROT_Z). */
    reset_world();
    seed_owner_a_yaw();
    /* Pre-seed owner matrix to a "wrong" sentinel so we can verify it's
     * overwritten. */
    float sentinel[16];
    for (int k = 0; k < 16; k++) sentinel[k] = -99.0f;
    memcpy(g_owner_a + 0xde8, sentinel, sizeof sentinel);

    scene1_record_b_spawn_entity(g_owner_a, 0x23, -1);

    float rot_z = slot_f(0, SCENE1_RECORDS_B_OFF_ROT_Z);
    T_ASSERT(rot_z >= 0.0f && rot_z < 6.2831856f);

    float m0  = slot_f(0, SCENE1_RECORDS_B_OFF_MATRIX0 + 0);
    float m5  = slot_f(0, SCENE1_RECORDS_B_OFF_MATRIX0 + 5);
    float m15 = slot_f(0, SCENE1_RECORDS_B_OFF_MATRIX0 + 15);
    T_ASSERT(APPROX(m0,  1.0f));
    T_ASSERT(APPROX(m15, 1.0f));
    T_ASSERT(APPROX(m5,  cosf(rot_z)));
    /* And NOT the sentinel. */
    T_ASSERT(!APPROX(m0, -99.0f));
    return 0;
}

int test_records_b_spawn_entity_29_yaw_branch_pos_no_vel_writes(void)
{
    /* people_idx = -1.  yaw = 0:
     *   POS_X = 0 + owner+0x20 = 10
     *   POS_Y = owner+0x24     = 20         (NOTE: NOT preamble's 19.5)
     *   POS_Z = 1*15 + owner+0x28 = 45
     * 0x29 does NOT write VEL — preamble's (0,0,0) carries.
     * 0x29 does NOT write LIFE_MULT — preamble's 1.0 carries.
     * DRAG = 0 (explicit). */
    reset_world();
    seed_owner_a_yaw();
    scene1_record_b_spawn_entity(g_owner_a, 0x29, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x29);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 20.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 45.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    return 0;
}

int test_records_b_spawn_entity_29_people_branch_no_ground_default(void)
{
    /* people_idx=7 → POS = people[7].pos with -5 on Y.  Default ground
     * query returns 0 (no hit) so POS_Y stays at people.y - 5. */
    reset_world();
    seed_owner_a_yaw();
    owner_write_i(g_owner_a, 0xea0, 7);
    g_scene1_people[7].pos[0] = 1.0f;
    g_scene1_people[7].pos[1] = 50.0f;
    g_scene1_people[7].pos[2] = 3.0f;

    scene1_record_b_spawn_entity(g_owner_a, 0x29, 7);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 1.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 50.0f - 5.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 3.0f));
    return 0;
}

/* Test-local ground-query hook for 0x29 — captures the last call's
 * args and forces a hit at a configurable Y. */
static int g_ground_hits;
static float g_ground_hit_y;
static int test_ground_hook(float x, float y, float *out_y)
{
    (void)x; (void)y;
    g_ground_hits++;
    *out_y = g_ground_hit_y;
    return 1;
}

int test_records_b_spawn_entity_29_ground_hook_clamps_pos_y(void)
{
    /* ground_y = 80 > anchor_y (45) → POS_Y = ground_y. */
    reset_world();
    seed_owner_a_yaw();
    owner_write_i(g_owner_a, 0xea0, 2);
    g_scene1_people[2].pos[0] = 1.0f;
    g_scene1_people[2].pos[1] = 50.0f;
    g_scene1_people[2].pos[2] = 3.0f;

    g_ground_hits = 0;
    g_ground_hit_y = 80.0f;
    scene1_b_ground_query_fn prev =
        scene1_record_b_spawn_set_ground_query(test_ground_hook);

    scene1_record_b_spawn_entity(g_owner_a, 0x29, 2);

    T_ASSERT_EQ_I(g_ground_hits, 1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 80.0f));

    /* And the "ground below anchor" case keeps POS_Y at anchor. */
    reset_world();
    seed_owner_a_yaw();
    owner_write_i(g_owner_a, 0xea0, 2);
    g_scene1_people[2].pos[0] = 1.0f;
    g_scene1_people[2].pos[1] = 50.0f;
    g_scene1_people[2].pos[2] = 3.0f;

    g_ground_hit_y = 10.0f;  /* below anchor 45 */
    scene1_record_b_spawn_entity(g_owner_a, 0x29, 2);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 50.0f - 5.0f));

    scene1_record_b_spawn_set_ground_query(prev);
    return 0;
}

int test_records_b_spawn_entity_30_yaw_branch(void)
{
    /* people_idx = -1 → vel via sin/cos(owner+0xea4)*0.7.
     * Angle 0 → sin=0, cos=1: VEL=(0, 0, 0.7).
     * POS via sin/cos(0.31415927 - 0):
     *   sin(0.31415927) ≈ 0.30902
     *   cos(0.31415927) ≈ 0.95106
     *   POS_X = sin*1.5 + owner+0x38 = 0.46353 + 100 = 100.46353
     *   POS_Y = owner+0x3c + 1.5 = 201.5
     *   POS_Z = owner+0x40 - cos*1.5 = 300 - 1.42659 = 298.57341
     * DRAG=20, ROT_Z in [0, 2π), AUX_C8=1. */
    reset_world();
    seed_owner_a_yaw();
    scene1_record_b_spawn_entity(g_owner_a, 0x30, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x30);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X),
                    sinf(0.31415927f) * 1.5f + 100.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 201.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z),
                    300.0f - cosf(0.31415927f) * 1.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.7f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    float rot_z = slot_f(0, SCENE1_RECORDS_B_OFF_ROT_Z);
    T_ASSERT(rot_z >= 0.0f && rot_z < 6.2831856f);
    return 0;
}

int test_records_b_spawn_entity_30_people_branch_normalizes_vel(void)
{
    /* people_idx=3.  Set people-pos at a known offset from spawn POS,
     * verify vel = (people.pos - POS) * 0.7 / len. */
    reset_world();
    seed_owner_a_yaw();
    owner_write_i(g_owner_a, 0xea0, 3);
    /* Spawn POS (computed above) ≈ (100.46, 201.5, 298.57).
     * Pick people-pos that gives a known unit vec3. */
    float pos_x = sinf(0.31415927f) * 1.5f + 100.0f;
    float pos_y = 201.5f;
    float pos_z = 300.0f - cosf(0.31415927f) * 1.5f;
    g_scene1_people[3].pos[0] = pos_x + 3.0f;
    g_scene1_people[3].pos[1] = pos_y + 0.0f;
    g_scene1_people[3].pos[2] = pos_z + 4.0f;
    /* len = sqrt(9 + 0 + 16) = 5; vel = (3, 0, 4) * 0.7 / 5 = (0.42, 0, 0.56). */

    scene1_record_b_spawn_entity(g_owner_a, 0x30, 3);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.42f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.56f));
    return 0;
}

int test_records_b_spawn_entity_30_people_branch_zero_distance_keeps_vel(void)
{
    /* Degenerate case: people-pos == spawn POS → len = 0 → no VEL
     * write (preamble's (0,0,0) carries). */
    reset_world();
    seed_owner_a_yaw();
    owner_write_i(g_owner_a, 0xea0, 4);
    g_scene1_people[4].pos[0] = sinf(0.31415927f) * 1.5f + 100.0f;
    g_scene1_people[4].pos[1] = 201.5f;
    g_scene1_people[4].pos[2] = 300.0f - cosf(0.31415927f) * 1.5f;

    scene1_record_b_spawn_entity(g_owner_a, 0x30, 4);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.0f));
    return 0;
}

int test_records_b_spawn_entity_9b_bend_and_life(void)
{
    /* npc=2 → bend = π/2 ≈ 1.5707963.  LIFE_MULT = 1.3. */
    reset_world();
    seed_owner_a_yaw();
    scene1_record_b_spawn_entity(g_owner_a, 0x9b, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x9b);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.3f));
    /* No VEL writes — preamble (0,0,0) carries. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    /* No DRAG override — preamble 0 carries. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    return 0;
}

int test_records_b_spawn_entity_9d_full_pose_and_explicit_return(void)
{
    /* npc=2 → bend = π/2; sin(π/2)=1, cos(π/2)≈0.
     *   ROT_X = π/2
     *   LIFE_MULT = 1.3
     *   POS = (10, 21.0, 30)    ← Y is owner+0x24 + 1.0 (NOT preamble's 19.5)
     *   ALT_POS = (10, 20.9, 30)
     *   VEL_X = sin(π/2)*2 = 2
     *   VEL_Y = 0
     *   VEL_Z ≈ cos(π/2)*2 ≈ 0
     *   SCALE_X = 10
     *   Only 1 particle.  Slot 1 stays empty. */
    reset_world();
    seed_owner_a_yaw();
    scene1_record_b_spawn_entity(g_owner_a, 0x9d, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x9d);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 21.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 30.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X), 10.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 20.9f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z), 30.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 2.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-5f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 10.0f));
    /* Single particle. */
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_c8j8_implemented_macro(void)
{
    int types[7] = {0x3e, 0x5f, 0x23, 0x29, 0x30, 0x9b, 0x9d};
    for (int k = 0; k < 7; k++) {
        T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(types[k]));
    }
    /* Existing handlers still implemented. */
    T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x60));
    T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x4d));
    /* 0x68 landed in C8j.9a — sister-table iteration covered. */
    T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x68));
    /* 0x83 still deferred — empty branch in the engine (negative-only). */
    T_ASSERT(!SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x83));
    return 0;
}

/* ─── C8j.9 tests ─────────────────────────────────────────────────── */

/* Helper — same seed as 0x9b/0x9d test but tweakable bend/ang.  Uses
 * npc=2 (bend=π/2), owner.pos=(10,20,30), owner+0xea4=0 (so sin=0,
 * cos=1). */
static void seed_owner_a_c8j9(int npc_mode, float ea4_angle)
{
    owner_write_f(g_owner_a, 0x20, 10.0f);
    owner_write_f(g_owner_a, 0x24, 20.0f);
    owner_write_f(g_owner_a, 0x28, 30.0f);
    owner_write_i(g_owner_a, 0x948, npc_mode);
    owner_write_f(g_owner_a, 0xea4, ea4_angle);
    owner_write_i(g_owner_a, 0xeac, 0);
    g_scene1_camera_yaw = 0.0f;
}

int test_records_b_spawn_entity_58_drift_with_drag(void)
{
    /* npc=2, ang=0 → sin=0 cos=1.
     * VEL = (0, 0, 3.0); POS = (10 - 0, 19.5 + 1, 30 - 1*0.5) = (10, 20.5, 29.5);
     * ROT_X = π/2 (bend); DRAG = 20; AUX_C8 = 1. */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x58, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x58);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 3.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 20.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 29.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG),  20.0f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_100_anchor_cone(void)
{
    /* ang=0 → sin=0 cos=1.
     * POS = (sin(0)*0.5+10, 20+1.5, cos(0)*0.5+30) = (10, 21.5, 30.5).
     * VEL = (sin(0)*0.4, 0, cos(0)*0.4) = (0, 0, 0.4).
     * byte 0xc2 = 2; LIFE_MULT=0.5; AUX_C8=1; PART_IDX=0. */
    reset_world();
    seed_owner_a_c8j9(0, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 100, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 100);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 21.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 30.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.4f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.5f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);

    /* byte 0xc2 = 2 — engine `(&DAT_06932572)[iVar10] = 2`. */
    const uint8_t *slot0 = (const uint8_t *)&g_scene1_records_b[0];
    T_ASSERT_EQ_I(slot0[0xc2], 2);
    /* Single particle. */
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_74_mode0_pos_x_shift(void)
{
    /* npc=0 → bend=0 → sin=0 cos=1.  POS_X = 0*1.2 + 10 = 10; mode 0
     * subtracts 0.41 → POS_X = 9.59.  POS_Z = 1*1.2 + 30 = 31.2 (no shift). */
    reset_world();
    seed_owner_a_c8j9(0, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x74, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x74);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 9.59f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 21.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 31.2f));
    /* ROT_X = bend = 0. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 0.0f));
    /* VEL = 2 * sin/cos(bend=0) = (0, 0, 2). */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 2.0f));
    /* No DRAG / AUX_C8 writes. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 0);
    return 0;
}

int test_records_b_spawn_entity_79_mode_else_pos_z_shift(void)
{
    /* npc=2 → bend=π/2 → sin=1 cos≈0.  POS = (1.2+10, 21.3, 0+30) =
     * (11.2, 21.3, 30).  npc!=0 && npc!=4 → POS_Z -= 0.1 → 29.9.  Body
     * shared with 0x74. */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x79, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x79);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 11.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 21.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 29.9f));
    return 0;
}

int test_records_b_spawn_entity_65_spawns_8_particles(void)
{
    /* 8-particle cap.  Test the cap rather than exact VEL (depends on
     * RNG sequence). */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x65, -1);

    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x65);
        /* VEL_Y = 0.7 fixed for every particle. */
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_Y), 0.7f));
        /* POS = owner + (0, 3.0, 0). */
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_X), 10.0f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_Y), 23.0f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_Z), 30.0f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.3f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_PART_IDX), k);
    }
    /* No 9th particle. */
    T_ASSERT_EQ_I(slot_i(8, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_6a_rot_x_strides(void)
{
    /* 8-particle cap.  ROT_X = part_idx * 2π/10 strides per particle.
     * SCALE_X = 0.5; LIFE_MULT = 0.3. */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x6a, -1);

    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x6a);
        float expected_rot_x = (float)k * 6.2831855f / 10.0f;
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_ROT_X), expected_rot_x));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_SCALE_X), 0.5f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.3f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    }
    T_ASSERT_EQ_I(slot_i(8, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_61_single_with_aux_c8_zero(void)
{
    /* 1-particle.  SCALE_X=0.5, LIFE_MULT=0.3, DRAG=0.5, AUX_C8 = 0
     * (UNIQUE — different from 0x6a which sets 1).
     * POS = (sin(π/2)*0.8+10, 20+1.1, cos(π/2)*0.8+30) ≈ (10.8, 21.1, 30).
     * ROT_X = bend (= π/2 for npc=2). */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x61, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x61);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 21.1f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 0.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 0);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_62_scale_0p45(void)
{
    /* SCALE_X = 0.45 (engine 0x3ee66666). LIFE_MULT=0.3. AUX_C8=1.  Test
     * SCALE_X value precisely since it's the LAB_00443db8 distinguishing
     * write. */
    reset_world();
    seed_owner_a_c8j9(0, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x62, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x62);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 0.45f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.5f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    return 0;
}

int test_records_b_spawn_entity_8a_8b_scale_distinguished(void)
{
    /* 0x8a → SCALE_X=0.2, 0x8b → SCALE_X=0.1.  Both share LAB_004451f0
     * body w/ VEL*1.0 (not *3.0 like 0x58).  npc=2 ang=0 → sin=0, cos=1.
     * VEL = (0, 0, 1.0) [scaled *1.0].  ROT_X = bend = π/2.  No DRAG. */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x8a, -1);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x8a);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 0.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 1.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));

    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x8b, -1);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x8b);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 0.1f));
    /* Same body, same VEL_Z. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 1.0f));
    return 0;
}

int test_records_b_spawn_entity_lab_00444be6_scales(void)
{
    /* Per-type SCALE_X for the LAB_00444be6 shared body. */
    struct { int type; float scale; } cases[] = {
        {0x5b, 0.7f}, {0x5c, 1.0f}, {0x5e, 1.0f},
        {0x85, 0.0f}, {0x86, 0.4f}, {0x87, 1.0f},
    };
    for (size_t k = 0; k < sizeof cases / sizeof *cases; k++) {
        reset_world();
        seed_owner_a_c8j9(2, 0.0f);
        scene1_record_b_spawn_entity(g_owner_a, cases[k].type, -1);

        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), cases[k].type);
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), cases[k].scale));
        /* Body writes ROT_X=bend and AUX_C8=1; no DRAG. */
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
        /* VEL_Z = cos(0)*3 = 3 (sb=0 sa=0 cos=1). */
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 3.0f));
    }
    return 0;
}

int test_records_b_spawn_entity_6d_to_70_spawns_3_with_part_idx_minus_1(void)
{
    /* 0x6d-0x70 — cap=3.  PART_IDX = part_idx - 1; signed [-1, 0, 1]. */
    int variants[4] = {0x6d, 0x6e, 0x6f, 0x70};
    for (int v = 0; v < 4; v++) {
        reset_world();
        seed_owner_a_c8j9(2, 0.0f);
        scene1_record_b_spawn_entity(g_owner_a, variants[v], -1);

        for (int k = 0; k < 3; k++) {
            T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), variants[v]);
            T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_PART_IDX), k - 1);
            T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
            T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
        }
        /* No 4th particle. */
        T_ASSERT_EQ_I(slot_i(3, SCENE1_RECORDS_B_OFF_TYPE), 0);
    }
    return 0;
}

int test_records_b_spawn_entity_71_72_75_7d_per_type_state(void)
{
    /* 0x75 skips VEL writes (preamble 0 sticks).  Others write VEL via
     * sin/cos*1.0.  ang=0 → sin=0 cos=1 → VEL = (0, 0, 1) for 0x71/0x72/0x7d. */

    /* 0x71: SCALE_X stays preamble 1.0; VEL writes happen. */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x71, -1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 1.0f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);

    /* 0x72: SCALE_X = 0.3. */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x72, -1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 0.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 1.0f));

    /* 0x75: SCALE_X = 1.0 (preamble); VEL writes SKIPPED → VEL_Z = 0. */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x75, -1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.0f));

    /* 0x7d: SCALE_X = 1.5 (via LAB_00444adc); VEL writes happen. */
    reset_world();
    seed_owner_a_c8j9(2, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 0x7d, -1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 1.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 1.0f));

    return 0;
}

int test_records_b_spawn_entity_8_zero_vel_anchor(void)
{
    /* Type 8 — VEL=0, POS = owner.pos + +2y (engine writes vel*10 + owner,
     * but vel=0 collapses).  DRAG=20.  AUX_C8=1.  ROT_Z = (0+1)*2π. */
    reset_world();
    seed_owner_a_c8j9(0, 0.0f);
    scene1_record_b_spawn_entity(g_owner_a, 8, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 8);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z), 0.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 10.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 22.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 30.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_Z),
                    6.2831855f));
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_c8j9_implemented_macro(void)
{
    int new_types[] = {
        0x58, 100, 0x74, 0x79, 0x65, 0x69, 0x6a, 0x61, 0x62,
        0x8a, 0x8b,
        0x5b, 0x5c, 0x5e, 0x85, 0x86, 0x87,
        0x6d, 0x6e, 0x6f, 0x70,
        0x71, 0x72, 0x75, 0x7d,
        8,
    };
    int n = (int)(sizeof new_types / sizeof *new_types);
    for (int k = 0; k < n; k++) {
        T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(new_types[k]));
    }
    /* C8j.9a — 0x68 sister-table iteration now landed. */
    T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x68));
    return 0;
}

/* ─── C8j.9a — 0x68 people-table sister-gate iteration ────────────── */

/* All 0x68 tests share a people-table-zeroing helper so prior tests'
 * writes don't leak in via the persistent g_scene1_people BSS. */
static void zero_people_table(void)
{
    memset(g_scene1_people, 0, sizeof g_scene1_people);
}

int test_records_b_spawn_entity_68_implemented(void)
{
    T_ASSERT(SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x68));
    return 0;
}

int test_records_b_spawn_entity_68_empty_people_uses_fallback(void)
{
    /* Empty people table (all alive==0) → no entry passes the alive==1
     * gate → fallback branch fires.  Fallback alt.y == owner.y (no +20
     * lift); primary path's pos.y == owner.y + 20.  So vel.y =
     * (alt.y - pos.y) / 10 = (20 - 40) / 10 = -2.0. */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);
    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x68);

    /* POS_Y is RNG-independent: owner.y + 20 = 40. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 40.0f));
    /* ALT_POS_Y is RNG-independent in the fallback path: owner.y = 20. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 20.0f));
    /* VEL.y depends only on the y deltas (alt.y - pos.y) / 10 = -2.0. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), -2.0f));
    /* Tail constants. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.6f));
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    /* Cap = 1 — no second slot committed. */
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_entity_68_matching_people_uses_target(void)
{
    /* people[0]: alive=1, sisters=0, target near owner (= (10, 20, 30)).
     * Expected: alt.{x,y,z} = people[0].target.{x,y,z} = (12, 25, 32). */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);

    g_scene1_people[0].alive       = 1;
    g_scene1_people[0].sister_720  = 0;
    g_scene1_people[0].sister_724  = 0;
    g_scene1_people[0].target[0]   = 12.0f;
    g_scene1_people[0].target[1]   = 25.0f;
    g_scene1_people[0].target[2]   = 32.0f;
    /* owner.field_ea0 default from seed_owner_a_yaw is -1 — but we want
     * the FIRST matching entry to count, so set it to 0. */
    owner_write_i(g_owner_a, 0xea0, 0);

    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x68);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X), 12.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 25.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z), 32.0f));
    /* POS.y stays at owner.y + 20 = 40 (primary path always lifts +20). */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 40.0f));
    /* VEL.y = (25 - 40) / 10 = -1.5. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), -1.5f));
    return 0;
}

int test_records_b_spawn_entity_68_sister_gate_blocks_match(void)
{
    /* Same as above but sister_720 != 0 → entry should be skipped → fallback
     * fires.  ALT.y must equal owner.y (= 20), NOT people target.y. */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);

    g_scene1_people[0].alive       = 1;
    g_scene1_people[0].sister_720  = 7;       /* blocks */
    g_scene1_people[0].sister_724  = 0;
    g_scene1_people[0].target[0]   = 12.0f;
    g_scene1_people[0].target[1]   = 25.0f;
    g_scene1_people[0].target[2]   = 32.0f;
    owner_write_i(g_owner_a, 0xea0, 0);

    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);

    /* Fallback path → ALT.y = owner.y = 20. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 20.0f));

    /* Same blocking via sister_724. */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);
    g_scene1_people[0].alive       = 1;
    g_scene1_people[0].sister_720  = 0;
    g_scene1_people[0].sister_724  = 9;       /* blocks */
    g_scene1_people[0].target[1]   = 25.0f;
    owner_write_i(g_owner_a, 0xea0, 0);
    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 20.0f));

    /* alive != 1 (set to 2 — the 1/2 distinction matters for 0x68). */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);
    g_scene1_people[0].alive       = 2;       /* not 1 → fails */
    g_scene1_people[0].target[1]   = 25.0f;
    owner_write_i(g_owner_a, 0xea0, 0);
    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 20.0f));
    return 0;
}

int test_records_b_spawn_entity_68_distance_gate_blocks(void)
{
    /* Owner at (10, 20, 30); people[0].target at (10000, 25, 30000).
     * Horizontal distance >> 16.0 → distance gate blocks → fallback fires. */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);

    g_scene1_people[0].alive       = 1;
    g_scene1_people[0].sister_720  = 0;
    g_scene1_people[0].sister_724  = 0;
    g_scene1_people[0].target[0]   = 10000.0f;
    g_scene1_people[0].target[1]   = 25.0f;
    g_scene1_people[0].target[2]   = 30000.0f;
    owner_write_i(g_owner_a, 0xea0, 0);

    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);

    /* Fallback → ALT.y = owner.y = 20. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 20.0f));
    return 0;
}

int test_records_b_spawn_entity_68_distance_is_horizontal_only(void)
{
    /* Y is excluded from the distance — set people.target with a huge
     * Y offset but XZ close.  Distance check should pass. */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);

    g_scene1_people[0].alive       = 1;
    g_scene1_people[0].sister_720  = 0;
    g_scene1_people[0].sister_724  = 0;
    g_scene1_people[0].target[0]   = 11.0f;   /* dx = -1 */
    g_scene1_people[0].target[1]   = 99999.0f; /* dy ignored */
    g_scene1_people[0].target[2]   = 29.0f;   /* dz =  1 */
    /* dx*dx + dz*dz = 2, sqrt = ~1.41 < 16 → match. */
    owner_write_i(g_owner_a, 0xea0, 0);

    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);

    /* Match → ALT.y = 99999 (the huge people.target.y). */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 99999.0f));
    return 0;
}

int test_records_b_spawn_entity_68_selector_picks_nth_match(void)
{
    /* Two qualifying entries; owner.field_ea0 = 1 → use the SECOND match.
     * people[3] is first match (alt.y=25), people[8] is second (alt.y=77). */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);

    g_scene1_people[3].alive       = 1;
    g_scene1_people[3].sister_720  = 0;
    g_scene1_people[3].sister_724  = 0;
    g_scene1_people[3].target[0]   = 12.0f;
    g_scene1_people[3].target[1]   = 25.0f;
    g_scene1_people[3].target[2]   = 32.0f;

    g_scene1_people[8].alive       = 1;
    g_scene1_people[8].sister_720  = 0;
    g_scene1_people[8].sister_724  = 0;
    g_scene1_people[8].target[0]   = 14.0f;
    g_scene1_people[8].target[1]   = 77.0f;
    g_scene1_people[8].target[2]   = 28.0f;

    /* Select second match. */
    owner_write_i(g_owner_a, 0xea0, 1);

    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X), 14.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 77.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z), 28.0f));
    return 0;
}

int test_records_b_spawn_entity_68_selector_out_of_range_falls_back(void)
{
    /* Only one qualifying entry but owner.field_ea0 = 5 → selector never
     * matches → fallback fires. */
    reset_world();
    zero_people_table();
    seed_owner_a_yaw();
    rng_seed(1);

    g_scene1_people[0].alive       = 1;
    g_scene1_people[0].sister_720  = 0;
    g_scene1_people[0].sister_724  = 0;
    g_scene1_people[0].target[1]   = 25.0f;
    owner_write_i(g_owner_a, 0xea0, 5);

    scene1_record_b_spawn_entity(g_owner_a, 0x68, -1);

    /* Fallback → ALT.y = owner.y = 20. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y), 20.0f));
    return 0;
}

/* ─── C8j.12 — owner+0x420 family of NPC single-spawn types ─────── */

/* Seed for the owner+0x420 family: pos at owner+0x3f0/3f4/3f8 =
 * (100, 200, 300); angle owner+0x420 = ang; NPC bend owner+0x18 =
 * bend_idx; owner+0x424 sub-state. */
static void seed_owner_b_420_family(int bend_idx, float ang, int sub_424)
{
    owner_write_f(g_owner_b, 0x3f0, 100.0f);
    owner_write_f(g_owner_b, 0x3f4, 200.0f);
    owner_write_f(g_owner_b, 0x3f8, 300.0f);
    owner_write_f(g_owner_b, 0x420, ang);
    owner_write_i(g_owner_b, 0x18,  bend_idx);
    owner_write_i(g_owner_b, 0x424, sub_424);
    /* Alt source fields used by 0x33 / 0x38. */
    owner_write_f(g_owner_b, 0x6fc,  7.0f);
    owner_write_f(g_owner_b, 0x700, 11.0f);
    owner_write_f(g_owner_b, 0x704, 13.0f);
}

int test_records_b_spawn_npc_33_alt_pos_and_vel_from_pos_y(void)
{
    /* ang = π/2 → sin=1, cos≈0.
     *   POS_X/Y/Z = owner+0x6fc/700/704 = (7, 11, 13)
     *   VEL_X     = 1 * 0.8 = 0.8
     *   VEL_Y     = -0.01 * 11.0 = -0.11
     *   VEL_Z     = 0
     *   LIFE_MULT = 0.7 */
    reset_world();
    seed_owner_b_420_family(0, 1.5707963f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x33, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x33);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X),  7.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 11.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 13.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X),  0.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), -0.11f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-4f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.7f));
    /* cap = 1 */
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_npc_27_lifts_pos_y_to_plus8(void)
{
    /* ang = 0 → sin=0, cos=1.
     *   POS_X = 100, POS_Y = 200 + 8 = 208, POS_Z = 302.5
     *   VEL_X = 0, VEL_Y = -0.05, VEL_Z = 0.5; DRAG = 0. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x27, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x27);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 208.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 302.5f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-4f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), -0.05f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z),  0.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG),   0.0f));
    return 0;
}

int test_records_b_spawn_npc_2b_amp_multiplier_on_owner_424(void)
{
    /* RNG amp ∈ [0.1, 0.1125); with owner+0x424 == 0x45 → ×1.5.
     * Verify by running with same RNG seed twice and comparing. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(7);
    scene1_record_b_spawn_npc(g_owner_b, 0x2b, 0);
    float vx_base = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X);
    float vz_base = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x2b);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    float vy = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y);
    T_ASSERT(vy >= 0.2f && vy < 1.0f);

    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0x45);
    rng_seed(7);
    scene1_record_b_spawn_npc(g_owner_b, 0x2b, 0);
    float vx_boost = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X);
    float vz_boost = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z);
    T_ASSERT(APPROX(vx_boost, vx_base * 1.5f));
    T_ASSERT(APPROX(vz_boost, vz_base * 1.5f));
    return 0;
}

int test_records_b_spawn_npc_26_2a_lift_distinguished(void)
{
    /* 0x26: POS_Y = 200 + 4.8 = 204.8, vel.y ≤ 0 (= -u*amp).
     * 0x2a: POS_Y = 200 + 3.5 = 203.5. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x26, 0);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 204.8f));
    T_ASSERT(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) <= 0.0f);

    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x2a, 0);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 203.5f));
    return 0;
}

int test_records_b_spawn_npc_31_32_amp_scaling(void)
{
    /* 0x32's amp = 0.5 * 0x31's amp.  Same RNG seed → identical amp
     * draw → vel.x for 0x32 = 0.5 * vel.x for 0x31. */
    reset_world();
    seed_owner_b_420_family(0, 1.5707963f, 0);   /* sin=1 */
    rng_seed(11);
    scene1_record_b_spawn_npc(g_owner_b, 0x31, 0);
    float vx_31 = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 2.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));

    reset_world();
    seed_owner_b_420_family(0, 1.5707963f, 0);
    rng_seed(11);
    scene1_record_b_spawn_npc(g_owner_b, 0x32, 0);
    float vx_32 = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X);

    T_ASSERT(APPROX(vx_32, vx_31 * 0.5f));
    return 0;
}

int test_records_b_spawn_npc_25_lifts_pos_y_to_plus_8p8(void)
{
    /* ang = π/2 → sin=1, cos≈0.
     *   POS_X = 102.5, POS_Y = 208.8, POS_Z ≈ 300.
     *   LIFE_MULT = 2.0; DRAG = 0. */
    reset_world();
    seed_owner_b_420_family(0, 1.5707963f, 0);
    rng_seed(3);
    scene1_record_b_spawn_npc(g_owner_b, 0x25, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x25);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 102.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 208.8f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 300.0f) < 1e-3f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 2.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    return 0;
}

int test_records_b_spawn_npc_3b_simple_vel_no_pos_writes(void)
{
    /* ang = π/2.  VEL_X = 0.6, VEL_Y = 0, VEL_Z ≈ 0; POS unchanged
     * from preamble (= owner.pos); DRAG = 0. */
    reset_world();
    seed_owner_b_420_family(0, 1.5707963f, 0);
    scene1_record_b_spawn_npc(g_owner_b, 0x3b, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x3b);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.6f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y)) < 1e-4f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-4f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 200.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    return 0;
}

int test_records_b_spawn_npc_28_atan2_and_pos_doubled_vel(void)
{
    /* ang = π/2 → vel = (0.3, 0.13, 0).
     *   POS_X = 2*0.3 + 100 = 100.6
     *   POS_Y = 2*0.13 + 200 + 0.8 = 201.06
     *   POS_Z = 300
     *   LIFE_MULT = 0.5; DRAG = 20.0 (final).
     *   ROT_SCR = atan2(0.1, 0.5) ≈ 0.19739556; ROT_X = π/2. */
    reset_world();
    seed_owner_b_420_family(0, 1.5707963f, 0);
    scene1_record_b_spawn_npc(g_owner_b, 0x28, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x28);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.13f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.6f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 201.06f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR),
                    0.19739556f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    return 0;
}

int test_records_b_spawn_npc_38_pos_from_alt_source(void)
{
    /* ang = 0 → cos=1.
     *   POS = (7, 11, 13) from owner+0x6fc/700/704 — NOT owner.xyz.
     *   VEL_Z = 0.5; LIFE_MULT = 3.8; DRAG = 3.0. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    scene1_record_b_spawn_npc(g_owner_b, 0x38, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x38);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X),  7.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 11.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 13.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z),  0.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 3.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 3.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR),
                    0.19739556f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X)) < 1e-4f);
    return 0;
}

int test_records_b_spawn_npc_21_invariant_pos_and_vel_y(void)
{
    /* RNG-dependent body — assert RNG-independent invariants only:
     * pos at owner.pos + +1.8y; vel.y = 0.02; DRAG = 20. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(13);
    scene1_record_b_spawn_npc(g_owner_b, 0x21, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x21);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 201.8f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.02f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
    return 0;
}

int test_records_b_spawn_npc_6b_npc_bend_amp_jitter(void)
{
    /* bend_idx = 2 → bend = π/2 → sin=1, cos≈0.
     * RNG amp ∈ [4, 8) → POS_X = 1*amp + 100, POS_Z ≈ 300.
     * POS_Y = 200 + 0.2 = 200.2 (RNG-independent).
     * ROT_SCR = atan2(0.1, 0.5); ROT_X = bend; DRAG = 0. */
    reset_world();
    seed_owner_b_420_family(2, 0.0f, 0);
    rng_seed(17);
    scene1_record_b_spawn_npc(g_owner_b, 0x6b, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x6b);
    float pos_x = slot_f(0, SCENE1_RECORDS_B_OFF_POS_X);
    T_ASSERT(pos_x >= 104.0f && pos_x < 108.0f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 200.2f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 300.0f) < 1e-3f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR),
                    0.19739556f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707963f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    return 0;
}

int test_records_b_spawn_npc_6c_pos_from_vel_times_3(void)
{
    /* bend_idx = 2 → bend = π/2.
     *   VEL_X = 0.2; POS_X = 0.6 + 100 = 100.6.
     *   POS_Y = 0 + 200 + 1.5 = 201.5.
     *   DRAG = 1.0; ROT_SCR = atan2. */
    reset_world();
    seed_owner_b_420_family(2, 0.0f, 0);
    scene1_record_b_spawn_npc(g_owner_b, 0x6c, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x6c);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X), 0.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.6f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 201.5f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 300.0f) < 1e-3f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 1.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR),
                    0.19739556f));
    return 0;
}

int test_records_b_spawn_npc_1f_amp_switch_on_owner_424(void)
{
    /* bend_idx = 2 → bend = π/2 → sin=1, cos≈0.
     * Per-sub amp: 7 → 0.12, 8 → 0.14, 9 → 0.16,
     *              0x24 → 0.15, 0x23 → 0.2, else → 0.1.
     * VEL_X = sin(bend) * amp = amp.
     * For sub == 0x24 / 0x23: pos = sin/cos(bend)*1.5 + owner.
     * For other sub:          pos = VEL*3 + owner (POS_X = amp*3 + 100). */
    struct { int sub; float amp; int use_pos_15; } cases[] = {
        { 0,     0.1f,  0 },
        { 7,     0.12f, 0 },
        { 8,     0.14f, 0 },
        { 9,     0.16f, 0 },
        { 0x24,  0.15f, 1 },
        { 0x23,  0.2f,  1 },
    };
    for (int k = 0; k < 6; k++) {
        reset_world();
        seed_owner_b_420_family(2, 0.0f, cases[k].sub);
        rng_seed(1);
        scene1_record_b_spawn_npc(g_owner_b, 0x1f, 0);

        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x1f);
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X),
                        cases[k].amp));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0.0f));

        if (cases[k].use_pos_15) {
            T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X),
                            101.5f));
        } else {
            float expected = cases[k].amp * 3.0f + 100.0f;
            T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X),
                            expected));
        }
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 201.5f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR),
                        0.19739556f));
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X),
                        1.5707963f));
    }
    return 0;
}

int test_records_b_spawn_npc_c8j12_implemented_macro(void)
{
    int types[15] = {
        0x33, 0x27, 0x2b, 0x26, 0x2a, 0x31, 0x32, 0x25, 0x3b,
        0x28, 0x38, 0x21, 0x6b, 0x6c, 0x1f,
    };
    for (int k = 0; k < 15; k++) {
        T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(types[k]));
    }
    /* 0x36 + 0x2e now landed in C8j.13. */
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x36));
    T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x2e));
    return 0;
}

/* ─── C8j.13 — NPC allocator remainder ────────────────────────────── */

int test_records_b_spawn_npc_2f_six_particle_fan(void)
{
    /* 0x2f spawns 6 particles.  ROT_Z = part_idx * 2π/3 + π/2 cycles
     * through {π/2, 7π/6, 11π/6, π/2, 7π/6, 11π/6} for part_idx 0..5.
     * PART_IDX = part_idx % 3; AUX_B0 = 0 for slot 0..2, 1 for 3..5. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x2f, 0);

    T_ASSERT_EQ_I(count_live(), 6);
    for (int k = 0; k < 6; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x2f);
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_PART_IDX), k % 3);
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_AUX_B0),
                      (k < 3) ? 0 : 1);
        /* VEL_Y always zero. */
        T_ASSERT(fabsf(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_Y)) < 1e-5f);
        /* POS_Y = owner.y + 1.5. */
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_Y), 201.5f));
        /* ROT_X = ang = 0. */
        T_ASSERT(fabsf(slot_f(k, SCENE1_RECORDS_B_OFF_ROT_X)) < 1e-5f);
    }
    /* Slot 6 still free. */
    T_ASSERT_EQ_I(slot_i(6, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_npc_2f_rot_z_spacing(void)
{
    /* Verify ROT_Z spacing: each consecutive triple covers a full 2π
     * cycle in increments of 2π/3. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x2f, 0);

    float r0 = slot_f(0, SCENE1_RECORDS_B_OFF_ROT_Z);
    float r1 = slot_f(1, SCENE1_RECORDS_B_OFF_ROT_Z);
    float r2 = slot_f(2, SCENE1_RECORDS_B_OFF_ROT_Z);
    T_ASSERT(APPROX(r0, 1.5707964f));                   /* π/2          */
    T_ASSERT(APPROX(r1, B_TWO_PI_F / 3.0f + 1.5707964f));
    T_ASSERT(APPROX(r2, 2.0f * B_TWO_PI_F / 3.0f + 1.5707964f));
    /* Slot 3 wraps: ROT_Z = 3*2π/3 + π/2 = 2π + π/2.  Don't assert
     * wrap-mod since the engine writes the unmodded value. */
    return 0;
}

int test_records_b_spawn_npc_36_cap_8_indexed_pos(void)
{
    /* 0x36 reads per-particle pos from owner+(part*0xc + 0x708/c/10).
     * Seed owner with distinct values per particle index to assert. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    for (int k = 0; k < 8; k++) {
        owner_write_f(g_owner_b, k * 0xc + 0x708,  (float)(100 + k));
        owner_write_f(g_owner_b, k * 0xc + 0x70c,  (float)(200 + k));
        owner_write_f(g_owner_b, k * 0xc + 0x710,  (float)(300 + k));
    }
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x36, 0);

    T_ASSERT_EQ_I(count_live(), 8);
    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x36);
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_X),
                        (float)(100 + k)));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_Y),
                        (float)(200 + k)));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_Z),
                        (float)(300 + k)));
        /* VEL = (POS - owner_anchor) * 0.02 (with +10 lift on Y). */
        float ex_vx = ((float)(100 + k) - 100.0f) * 0.02f;
        float ex_vy = ((float)(200 + k) - 210.0f) * 0.02f;
        float ex_vz = ((float)(300 + k) - 300.0f) * 0.02f;
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_X), ex_vx));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_Y), ex_vy));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_Z), ex_vz));
    }
    return 0;
}

int test_records_b_spawn_npc_2e_sign_flip_tail(void)
{
    /* 0x2e cap=1; tail flips VEL_Y from 0.14 → -0.14 and cancels the
     * +4 Y-lift (so POS_Y ends back at owner.y). */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x2e, 0);

    T_ASSERT_EQ_I(count_live(), 1);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x2e);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), -0.14f));
    /* POS_Y was lifted by +4 then -4 = owner.y = 200. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 200.0f));
    return 0;
}

int test_records_b_spawn_npc_3c_minimal_drag_zero(void)
{
    /* 0x3c is pure preamble + DRAG=0. */
    reset_world();
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x3c, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x3c);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    /* Cap=1 — slot 1 free. */
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_spawn_npc_98_five_particle_per_index_shifts(void)
{
    /* 0x98 spawns 5 particles; per-particle shifts ±0.38/0.56 from bend.
     * bend = bend_idx * 2π / 8 = 0 (bend_idx=0).
     * Shifts: part 0 → 0; 1 → -0.38; 2 → +0.38; 3 → -0.56; 4 → +0.56.
     * ROT_X stores the shifted angle. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x98, 0);

    T_ASSERT_EQ_I(count_live(), 5);
    static const float expected_rot_x[5] = {
        0.0f, -0.38f, +0.38f, -0.56f, +0.56f,
    };
    for (int k = 0; k < 5; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x98);
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_PART_IDX), k);
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_ROT_X),
                        expected_rot_x[k]));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_ROT_SCR),
                        0.19739556f));
        /* POS_Y = owner.y + 0.25 = 200.25. */
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_Y), 200.25f));
        T_ASSERT(fabsf(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_Y)) < 1e-5f);
    }
    return 0;
}

int test_records_b_spawn_npc_5a_zero_vel_atan2_const(void)
{
    /* 0x5a: vel=0; pos = owner + 0.25y; ROT_SCR = atan2(0.1, 0.5);
     * ROT_X = bend; DRAG=20; cap=1. */
    reset_world();
    seed_owner_b_420_family(2, 0.0f, 0);  /* bend = 2*2π/8 = π/2 */
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x5a, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x5a);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-5f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y)) < 1e-5f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-5f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 200.25f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR),
                    0.19739556f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707964f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 20.0f));
    return 0;
}

int test_records_b_spawn_npc_f_12_scale_only(void)
{
    /* 0xf → SCALE_X = 0.2; 0x12 → SCALE_X = 3.0; cap=1 each. */
    reset_world();
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0xf, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xf);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 0.2f));

    reset_world();
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x12, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x12);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X), 3.0f));
    return 0;
}

int test_records_b_spawn_npc_9c_bend_and_life_mult(void)
{
    /* 0x9c: ROT_X = bend (owner+0x18 * 2π/8); LIFE_MULT = 1.8; cap=1. */
    reset_world();
    seed_owner_b_420_family(4, 0.0f, 0);  /* bend = 4*2π/8 = π */
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x9c, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x9c);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X), 3.1415927f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 1.7999998f));
    return 0;
}

int test_records_b_spawn_npc_3a_player_pos_centered_jitter(void)
{
    /* 0x3a places POS centered on player_pos with ±5/2 jitter on x/z
     * and +20y lift.  VEL = (0, -0.3, 0).  ROT_SCR = π/2. */
    reset_world();
    g_scene1_player_pos[0] = 10.0f;
    g_scene1_player_pos[1] = 20.0f;
    g_scene1_player_pos[2] = 30.0f;
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x3a, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x3a);
    /* POS.x ∈ [10 - 2.5, 10 + 2.5]. */
    float px = slot_f(0, SCENE1_RECORDS_B_OFF_POS_X);
    T_ASSERT(px >= 7.5f && px <= 12.5f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 40.0f));
    float pz = slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z);
    T_ASSERT(pz >= 27.5f && pz <= 32.5f);

    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-5f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y), -0.3f));
    T_ASSERT(fabsf(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-5f);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR), 1.5707964f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG), 0.0f));
    /* Matrix at MATRIX0 should be a rotation Y of ROT_Z. */
    float rot_z = slot_f(0, SCENE1_RECORDS_B_OFF_ROT_Z);
    T_ASSERT(rot_z >= 0.0f && rot_z < B_TWO_PI_F);
    return 0;
}

int test_records_b_spawn_npc_34_eight_particle_player_target(void)
{
    /* 0x34: cap=8; ALT_POS at slot dw 32/33/34 = sin/cos(-ang)*radius +
     * player_pos with frame-table azimuth picker.  POS_Y = owner.y + 11.
     * AGE = part_idx * -4; AUX_SENT1 = part_idx. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    g_scene1_player_pos[0] = 50.0f;
    g_scene1_player_pos[1] = 60.0f;
    g_scene1_player_pos[2] = 70.0f;
    g_sim_frame_count = 0;
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x34, 0);

    T_ASSERT_EQ_I(count_live(), 8);
    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x34);
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_Y), 211.0f));
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
        /* ang = 0 → VEL = (0, 0, 2). */
        T_ASSERT(fabsf(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-5f);
        T_ASSERT(fabsf(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_Y)) < 1e-5f);
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_VEL_Z), 2.0f));
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_AGE), k * -4);
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_AUX_SENT1), k);
    }
    return 0;
}

int test_records_b_spawn_npc_34_alt_pos_player_offset(void)
{
    /* Frame counter 0 → frame_table[k] = {-3,-1,-4,2,1,3,-2,4}.
     * neg_ang = 0; sin(0)=0, cos(0)=1.
     * ALT_POS_X (dw 32) = 0 * radius + player.x = player.x = 50.
     * ALT_POS_Y (dw 33) = player.y + 2 = 62.
     * ALT_POS_Z (dw 34) = 1 * radius + player.z. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    g_scene1_player_pos[0] = 50.0f;
    g_scene1_player_pos[1] = 60.0f;
    g_scene1_player_pos[2] = 70.0f;
    g_sim_frame_count = 0;
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x34, 0);

    static const int frame_table[8] = { -3, -1, -4, 2, 1, 3, -2, 4 };
    for (int k = 0; k < 8; k++) {
        int   tbl_idx = (0 + k) % 8;
        float radius  = (float)frame_table[tbl_idx] * 5.0f;
        if (radius <= 0.0f) radius += 3.0f;
        else                radius -= 3.0f;
        T_ASSERT(APPROX(slot_f(k, 32), 50.0f));
        T_ASSERT(APPROX(slot_f(k, 33), 62.0f));
        T_ASSERT(APPROX(slot_f(k, 34), radius + 70.0f));
    }
    return 0;
}

int test_records_b_spawn_npc_34_frame_counter_rotates_table(void)
{
    /* Advance g_sim_frame_count by 1 → frame_table picker shifts by 1.
     * Slot 0's tbl_idx becomes 1 → frame_table[1] = -1 → radius =
     * -1*5 + 3 = -2; cos(0)*-2 + 70 = 68. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    g_scene1_player_pos[0] = 50.0f;
    g_scene1_player_pos[1] = 60.0f;
    g_scene1_player_pos[2] = 70.0f;
    g_sim_frame_count = 1;
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x34, 0);

    /* Slot 0: tbl_idx=(1+0)%8=1; frame_table[1]=-1; radius=-2. */
    T_ASSERT(APPROX(slot_f(0, 34), 68.0f));
    return 0;
}

int test_records_b_spawn_npc_16_17_part_idx_minus_one(void)
{
    /* 0x16/0x17: cap=3; PART_IDX values are {-1, 0, 1} (engine quirk:
     * iVar4 = pre-inc local_8 - 1). */
    reset_world();
    seed_owner_b_420_family(2, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x16, 0);

    T_ASSERT_EQ_I(count_live(), 3);
    for (int k = 0; k < 3; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x16);
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_PART_IDX), k - 1);
        /* ROT_X = bend = π/2. */
        T_ASSERT(APPROX(slot_f(k, SCENE1_RECORDS_B_OFF_ROT_X), 1.5707964f));
    }

    /* 0x17 same body. */
    reset_world();
    seed_owner_b_420_family(2, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x17, 0);
    T_ASSERT_EQ_I(count_live(), 3);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), -1);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_PART_IDX),  0);
    T_ASSERT_EQ_I(slot_i(2, SCENE1_RECORDS_B_OFF_PART_IDX),  1);
    return 0;
}

int test_records_b_spawn_npc_c8j13_implemented_macro(void)
{
    int types[13] = {
        0x2f, 0x2e, 0x36, 0x3c, 0x98, 0x5a,
        0xf, 0x12, 0x9c, 0x3a, 0x34, 0x16, 0x17,
    };
    for (int k = 0; k < 13; k++) {
        T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(types[k]));
    }
    return 0;
}

/* ─── C8j.11a — L42831 fall-through group (0xd/0x11/0x15/0xc/0x10) ── */

int test_records_b_spawn_npc_lab_42831_rot_x_only(void)
{
    /* All 5 types are ROT_X-only.  bend = bend_idx * 2π / 8.  cap=1. */
    int types[5] = { 0xd, 0x11, 0x15, 0xc, 0x10 };
    for (int k = 0; k < 5; k++) {
        reset_world();
        seed_owner_b_420_family(2, 0.0f, 0);  /* bend = 2*2π/8 = π/2 */
        rng_seed(1);
        scene1_record_b_spawn_npc(g_owner_b, types[k], 0);

        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), types[k]);
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X),
                        1.5707964f));
        T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(types[k]));
        /* cap=1 — slot 1 free. */
        T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE), 0);
    }
    return 0;
}

/* ─── C8j.11 — mega-cluster B + player-aim ────────────────────────── */

int test_records_b_spawn_npc_c8j11_implemented_macro(void)
{
    int types[11] = {
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
        0x73, 0x7a, 0x7c, 0x7e,
        0x84, 0x96,
    };
    for (int k = 0; k < 11; k++) {
        T_ASSERT(SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(types[k]));
    }
    return 0;
}

int test_records_b_spawn_npc_mega_b_cap_per_type(void)
{
    /* cap = 8 (0xa0/0x7a/0xa3), 5 (0x7c), 4 (0x73), 1 (others). */
    struct { int type; int cap; } cases[] = {
        { 0xa0, 8 }, { 0x7a, 8 }, { 0xa3, 8 },
        { 0x7c, 5 }, { 0x73, 4 },
        { 0xa1, 1 }, { 0xa2, 1 }, { 0xa4, 1 }, { 0x7e, 1 },
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        reset_world();
        seed_owner_b_420_family(0, 0.0f, 0);
        rng_seed(1);
        scene1_record_b_spawn_npc(g_owner_b, cases[k].type, 0);
        T_ASSERT_EQ_I(count_live(), cases[k].cap);
        T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), cases[k].type);
    }
    return 0;
}

int test_records_b_spawn_npc_mega_b_pos_and_alt_pos(void)
{
    /* For bend_idx=0 (mode 0 dispatch): bend=0, sin=0, cos=1.
     *   pos.x = 0*1.2 + 100 - 0.41 = 99.59
     *   pos.y = 200 + 1.3        = 201.3
     *   pos.z = 1*1.2 + 300       = 301.2
     *   alt_x = 0*0.8 + 100 - 0.41 = 99.59
     *   alt_y = 200 + 1.3        = 201.3
     *   alt_z = 1*0.8 + 300       = 300.8
     */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0xa1, 0);  /* cap=1, simple body */

    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X),     99.59f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y),    201.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z),    301.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X), 99.59f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y),201.3f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z),300.8f));
    return 0;
}

int test_records_b_spawn_npc_mega_b_mode_dispatch(void)
{
    /* mode=4 → pos.x/alt_x += 0.41; mode=else → pos.z/alt_z -= 0.1. */
    reset_world();
    seed_owner_b_420_family(4, 0.0f, 0);
    rng_seed(1);
    /* bend=4*2π/8 = π → sin≈0, cos=-1. */
    scene1_record_b_spawn_npc(g_owner_b, 0xa1, 0);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X),
                    sinf(3.1415927f) * 1.2f + 100.0f + 0.41f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z),
                    cosf(3.1415927f) * 1.2f + 300.0f));   /* no z bias */

    reset_world();
    seed_owner_b_420_family(2, 0.0f, 0);  /* mode 2 — "else" branch */
    rng_seed(1);
    /* bend=π/2 → sin=1, cos≈0. */
    scene1_record_b_spawn_npc(g_owner_b, 0xa1, 0);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X),
                    sinf(1.5707964f) * 1.2f + 100.0f)); /* no x bias */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z),
                    cosf(1.5707964f) * 1.2f + 300.0f - 0.1f));
    return 0;
}

int test_records_b_spawn_npc_mega_b_a4_skips_aux_c8(void)
{
    /* AUX_C8 = 1 for all mega-B types EXCEPT 0xa4. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0xa0, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);

    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0xa4, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 0);
    return 0;
}

int test_records_b_spawn_npc_mega_b_scale_x_per_type(void)
{
    struct { int type; float scale; } cases[] = {
        { 0x7e, 0.25f  }, { 0x73, 0.25f },
        { 0xa3, 0.5f   }, { 0xa2, 0.5f  },
        { 0xa0, 0.125f }, { 0x7a, 0.125f },
        { 0xa1, 1.0f   }, { 0xa4, 1.0f  },
        { 0x7c, 0.1f   },
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        reset_world();
        seed_owner_b_420_family(0, 0.0f, 0);
        rng_seed(1);
        scene1_record_b_spawn_npc(g_owner_b, cases[k].type, 0);
        T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_SCALE_X),
                        cases[k].scale));
    }
    return 0;
}

int test_records_b_spawn_npc_mega_b_a3_part_idx_quirk(void)
{
    /* 0xa3 cap=8 → 8 slots; part_idx > 0 sets PART_IDX = 1.
     * First slot (part_idx=0) keeps preamble PART_IDX=0; subs get 1. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0xa3, 0);

    T_ASSERT_EQ_I(count_live(), 8);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    for (int k = 1; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    }
    /* AGE = -part_idx for each slot. */
    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_AGE), -k);
    }
    return 0;
}

int test_records_b_spawn_npc_mega_b_a4_clamp_away_from_zero(void)
{
    /* 0xa4 clamps ROT_X away from forward/back cones at ±2.5132742.
     * With player_pos = pos + (1, _, 1), atan2(dx=1, dz=1) = π/4 ≈ 0.785,
     * which is in (0, 2.5132742) → ROT_X gets clamped UP to 2.5132742
     * after the RNG jitter (we make RNG return 0.5 so jitter cancels). */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);  /* bend=0, mode=0 */

    /* For bend=0: sin=0, cos=1; with mode 0 dispatch:
     *   pos.x = 0*1.2 + 100 - 0.41 = 99.59
     *   pos.z = 1*1.2 + 300        = 301.2
     * Place player at (pos.x + 1, *, pos.z + 1) so atan2(1, 1) = π/4. */
    g_scene1_player_pos[0] = 99.59f + 1.0f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 301.2f + 1.0f;
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0xa4, 0);

    /* Final ROT_X should be in [2.5132742, ...]: either exactly the clamp
     * if jittered atan2 lands in (0, 2.5132742), or above otherwise.
     * For our seed, expect the clamp lower bound. */
    float rot_x = slot_f(0, SCENE1_RECORDS_B_OFF_ROT_X);
    T_ASSERT(rot_x >= 2.5132742f - 0.0001f);
    return 0;
}

int test_records_b_spawn_npc_84_pos_direct_no_bend_spread(void)
{
    /* 0x84 uses owner pos directly with +3.0 y lift, NO sin/cos bend. */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    g_scene1_player_pos[0] = 100.0f + 10.0f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 300.0f + 10.0f;
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x84, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x84);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 100.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 203.0f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));
    /* LIFE_MULT = 0.5; VEL_Y = 0.4; DRAG = 0.5; cap = 1. */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.5f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y),     0.4f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG),      0.5f));
    /* NO AUX_C8 = 1 — preamble's 0 carries. */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 0);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_RECORDS_B_OFF_TYPE),   0);  /* cap=1 */
    return 0;
}

int test_records_b_spawn_npc_96_bend_spread_pos(void)
{
    /* 0x96 spreads POS_X/Z by sin/cos(bend) around owner; LIFE_MULT=0.2. */
    reset_world();
    seed_owner_b_420_family(2, 0.0f, 0);  /* bend = π/2 → sin=1, cos≈0 */
    g_scene1_player_pos[0] = 100.0f + 10.0f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 300.0f + 10.0f;
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x96, 0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x96);
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_X), 101.0f));   /* sin(π/2)*1 + 100 */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Y), 202.0f));   /* +2 lift */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_POS_Z), 300.0f));   /* cos(π/2)≈0 */
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0.2f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Y),     0.4f));
    T_ASSERT(APPROX(slot_f(0, SCENE1_RECORDS_B_OFF_DRAG),      0.5f));
    return 0;
}

int test_records_b_spawn_npc_84_dist_clamp_min_5(void)
{
    /* Player AT owner → dx=dz=0; engine code path: dist=0; 5-clamp would
     * divide by 0; sequence: dist<5 → scale by 5/0 (∞).  Avoid by placing
     * player JUST off owner — dist=0.5 (< 5).  After clamp: dist=5,
     * vel_mag_base = 5*0.01 = 0.05.  Without final-vel_mag override (0x84),
     * vel_mag = u*0.04 + 0.05 ∈ [0.05, 0.09). */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    /* pos = (100, 203, 300), player offset (0.5, 0, 0) → dist=0.5. */
    g_scene1_player_pos[0] = 100.5f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 300.0f;
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x84, 0);

    /* Just sanity-check that magnitudes are within the clamp band — vel
     * magnitude = sqrt(VEL_X² + VEL_Z²) should be in [0.05, 0.09). */
    float vx = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_X);
    float vz = slot_f(0, SCENE1_RECORDS_B_OFF_VEL_Z);
    float mag = sqrtf(vx * vx + vz * vz);
    T_ASSERT(mag >= 0.05f - 1e-4f);
    T_ASSERT(mag <= 0.09f + 1e-4f);
    return 0;
}

int test_records_b_spawn_npc_mega_b_7c_rebound(void)
{
    /* 0x7c — pos -= 2*vel on x/z (after the standard vel computation). */
    reset_world();
    seed_owner_b_420_family(0, 0.0f, 0);
    rng_seed(1);
    scene1_record_b_spawn_npc(g_owner_b, 0x7c, 0);

    /* cap = 5, so 5 slots written.  For each: pos.x = pre - 2*vel.x.
     * Verify the relation holds slot-by-slot. */
    T_ASSERT_EQ_I(count_live(), 5);
    for (int k = 0; k < 5; k++) {
        T_ASSERT_EQ_I(slot_i(k, SCENE1_RECORDS_B_OFF_TYPE), 0x7c);
    }
    return 0;
}
