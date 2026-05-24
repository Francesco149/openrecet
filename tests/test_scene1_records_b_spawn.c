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
    /* Other types remain unimplemented. */
    T_ASSERT(!SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x33));
    T_ASSERT(!SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(0x68));
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
    /* Other types still unimplemented. */
    T_ASSERT(!SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x58));
    T_ASSERT(!SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(0x6a));
    return 0;
}
