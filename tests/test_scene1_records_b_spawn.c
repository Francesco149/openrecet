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
