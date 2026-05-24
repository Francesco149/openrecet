/*
 * test_scene1_records_b_spawn.c — unit tests for the C8j.5 table B
 * allocators (FUN_0044376a + FUN_00445a8c skeleton + preamble + 3
 * minimal anchor types per allocator).
 */

#include "t.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

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
