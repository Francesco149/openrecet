/*
 * test_save_work.c — coverage for the working (live) save arena port
 * (src/save_work.{c,h} = FUN_00490259 + FUN_004902aa middle step).
 *
 * The working arena is the live game state gameplay reads; "continue /
 * load a save" copies a SAVE bank (save_bank.c) into the active WORKING
 * slot. We exercise that copy + the inventory-count recompute, plus the
 * whole-arena sync, all in-process under ASan/UBSan.
 */
#include "t.h"

#include <string.h>

#include "save_bank.h"
#include "save_work.h"

/* Build a known item table in SAVE bank `b`: `n` real items (IDs
 * 100..100+n-1) followed by 0xFFFFFFFF padding to the table end. */
static void seed_save_items(int b, int n)
{
    uint32_t *bank = save_bank_dwords_at(b);
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++) {
        bank[SAVE_BANK_ITEM_TABLE_DWORD + i] =
            (i < n) ? (uint32_t)(100 + i) : 0xFFFFFFFFu;
    }
}

int test_save_work_arena_geometry(void)
{
    /* Working arena mirrors the save arena's geometry exactly. */
    uint8_t *base = save_work_base();
    T_ASSERT(base != NULL);
    T_ASSERT(save_work_bank_at(0)  == base + SAVE_BANK_HEADER_BYTES);
    T_ASSERT(save_work_bank_at(1)  == base + SAVE_BANK_HEADER_BYTES
                                          + SAVE_BANK_STRIDE_BYTES);
    T_ASSERT(save_work_bank_at(99) == base + SAVE_BANK_HEADER_BYTES
                                          + 99 * (size_t)SAVE_BANK_STRIDE_BYTES);
    /* Out-of-range slots return NULL. */
    T_ASSERT(save_work_bank_at(-1)  == NULL);
    T_ASSERT(save_work_bank_at(100) == NULL);
    return 0;
}

int test_save_work_active_slot_default_and_set(void)
{
    save_work_clear();
    T_ASSERT_EQ_I(save_work_active_slot(), 0);
    save_work_set_active_slot(7);
    T_ASSERT_EQ_I(save_work_active_slot(), 7);
    save_work_clear();
    T_ASSERT_EQ_I(save_work_active_slot(), 0);
    return 0;
}

int test_save_work_load_slot_copies_bank(void)
{
    save_bank_arena_clear();
    save_work_clear();

    /* Distinct content in save bank 3; load it into working slot 0. */
    uint32_t *src = save_bank_dwords_at(3);
    src[1]  = 0xDEADBEEFu;            /* magic field */
    src[SAVE_BANK_FIELD_GOLD] = 5000;
    seed_save_items(3, 12);
    /* Pre-set the source count to what the load will recompute (12) so
     * the verbatim-copy assertion below holds: the engine copies the
     * whole bank, THEN overwrites only the count field. */
    src[SAVE_BANK_FIELD_ITEM_COUNT] = 12;

    save_work_set_active_slot(0);
    save_work_load_slot(3);

    uint32_t *dst = save_work_dwords_at(0);
    T_ASSERT_EQ_U(dst[1], 0xDEADBEEFu);
    T_ASSERT_EQ_U(dst[SAVE_BANK_FIELD_GOLD], 5000u);
    T_ASSERT_EQ_I(save_work_item_count(0), 12);
    /* Whole bank copied verbatim (count field now matches too). */
    T_ASSERT_MEM_EQ(dst, src, SAVE_BANK_STRIDE_DWORDS * 4);
    return 0;
}

int test_save_work_load_slot_recomputes_item_count(void)
{
    save_bank_arena_clear();
    save_work_clear();

    seed_save_items(5, 12);
    /* Stale count in the source — load must overwrite it. */
    save_bank_dwords_at(5)[SAVE_BANK_FIELD_ITEM_COUNT] = 999;

    save_work_set_active_slot(0);
    save_work_load_slot(5);

    T_ASSERT_EQ_I(save_work_item_count(0), 12);
    return 0;
}

int test_save_work_load_slot_full_table_keeps_copied_count(void)
{
    save_bank_arena_clear();
    save_work_clear();

    /* No empty slot ⇒ engine leaves the count field as copied. */
    seed_save_items(7, SAVE_BANK_ITEM_TABLE_COUNT);
    save_bank_dwords_at(7)[SAVE_BANK_FIELD_ITEM_COUNT] = 20000;

    save_work_set_active_slot(0);
    save_work_load_slot(7);

    T_ASSERT_EQ_I(save_work_item_count(0), 20000);
    return 0;
}

int test_save_work_load_slot_honours_active_slot(void)
{
    save_bank_arena_clear();
    save_work_clear();

    save_bank_dwords_at(2)[SAVE_BANK_FIELD_GOLD] = 1234;
    seed_save_items(2, 3);

    /* Load into working slot 4, not 0. */
    save_work_set_active_slot(4);
    save_work_load_slot(2);

    T_ASSERT_EQ_U(save_work_dwords_at(4)[SAVE_BANK_FIELD_GOLD], 1234u);
    T_ASSERT_EQ_I(save_work_item_count(4), 3);
    /* Slot 0 untouched. */
    T_ASSERT_EQ_U(save_work_dwords_at(0)[SAVE_BANK_FIELD_GOLD], 0u);
    return 0;
}

int test_save_work_load_slot_bad_source_is_noop(void)
{
    save_bank_arena_clear();
    save_work_clear();
    save_work_dwords_at(0)[SAVE_BANK_FIELD_GOLD] = 42;

    save_work_set_active_slot(0);
    save_work_load_slot(-1);    /* bad source bank */
    save_work_load_slot(100);

    T_ASSERT_EQ_U(save_work_dwords_at(0)[SAVE_BANK_FIELD_GOLD], 42u);
    return 0;
}

int test_save_work_sync_from_save_copies_whole_arena(void)
{
    save_bank_arena_clear();
    save_work_clear();

    /* Touch header + two distant banks in the save arena. */
    ((uint32_t *)save_arena_base())[0] = 0x341944dau;
    save_bank_dwords_at(0)[SAVE_BANK_FIELD_GOLD]  = 11;
    save_bank_dwords_at(99)[SAVE_BANK_FIELD_GOLD] = 22;

    save_work_sync_from_save();

    T_ASSERT_EQ_U(((uint32_t *)save_work_base())[0], 0x341944dau);
    T_ASSERT_EQ_U(save_work_dwords_at(0)[SAVE_BANK_FIELD_GOLD],  11u);
    T_ASSERT_EQ_U(save_work_dwords_at(99)[SAVE_BANK_FIELD_GOLD], 22u);
    T_ASSERT_MEM_EQ(save_work_base(), save_arena_base(),
                    SAVE_BANK_ARENA_BYTES);
    return 0;
}
