/*
 * test_chara_skills.c — tests for src/chara_skills.{c,h}.
 *
 * Covers FUN_004360b6's per-chara state writes:
 *   - skill-slot alive flag bytes at chara record +0x60..+0x64
 *   - default skill-slot list {0, 1, 2, 3, 4}
 *   - threshold-pass count per chara level
 *   - per-chara isolation (level-1 of chara 0 doesn't change chara 1)
 *   - tail scratch DAT writes (0, 2, 1)
 *   - second-pass slot rewrite (semantic no-op for vanilla RDATA)
 */

#include "t.h"

#include "chara_equip.h"
#include "chara_skills.h"

static void setup_clean(void)
{
    chara_equip_reset_for_test();
    chara_skills_reset_for_test();
}

/* ─── per-chara alive-flag bytes ──────────────────────────────────── */

int test_chara_skills_writes_alive_flags(void)
{
    setup_clean();
    chara_skills_init_at_stage_load();
    for (int chara = 0; chara < CHARA_SKILLS_CHARA_COUNT; chara++) {
        for (int b = 0; b < 5; b++) {
            T_ASSERT_EQ_I(
                chara_equip_get_record_byte(0, chara, 0x60 + b), 1);
        }
    }
    return 0;
}

int test_chara_skills_alive_flags_do_not_leak_outside_window(void)
{
    setup_clean();
    chara_skills_init_at_stage_load();
    /* +0x5f and +0x65 should remain BSS-zero. */
    for (int chara = 0; chara < CHARA_SKILLS_CHARA_COUNT; chara++) {
        T_ASSERT_EQ_I(chara_equip_get_record_byte(0, chara, 0x5f), 0);
        T_ASSERT_EQ_I(chara_equip_get_record_byte(0, chara, 0x65), 0);
    }
    return 0;
}

/* ─── default skill-slot list ─────────────────────────────────────── */

int test_chara_skills_default_slot_list_is_0_to_4(void)
{
    setup_clean();
    chara_skills_init_at_stage_load();
    /* For chara 7 (count=1), pass 2 only overwrites slot[0] (= 0).
     * Slots [1..4] retain pass-1 values {1, 2, 3, 4}. */
    T_ASSERT_EQ_I(chara_skills_get_slot(7, 0), 0);
    T_ASSERT_EQ_I(chara_skills_get_slot(7, 1), 1);
    T_ASSERT_EQ_I(chara_skills_get_slot(7, 2), 2);
    T_ASSERT_EQ_I(chara_skills_get_slot(7, 3), 3);
    T_ASSERT_EQ_I(chara_skills_get_slot(7, 4), 4);
    return 0;
}

/* ─── threshold counting at NEW-GAME (level=0 for all) ────────────── */

int test_chara_skills_count_at_level_0_all_pass_first_threshold(void)
{
    /* All 8 chara rows have threshold[0] = 0; at level 0 every chara
     * passes exactly that one threshold. */
    setup_clean();
    chara_skills_init_at_stage_load();
    for (int chara = 0; chara < CHARA_SKILLS_CHARA_COUNT; chara++) {
        T_ASSERT_EQ_I(chara_skills_get_count(chara), 1);
    }
    return 0;
}

int test_chara_skills_count_at_level_9_chara0(void)
{
    /* Chara 0: thresholds [0, 9].  Level 9 passes both. */
    setup_clean();
    chara_equip_set_chara_level(0, 0, 9);
    chara_skills_init_at_stage_load();
    T_ASSERT_EQ_I(chara_skills_get_count(0), 2);
    return 0;
}

int test_chara_skills_count_at_level_8_chara0(void)
{
    /* Chara 0: thresholds [0, 9].  Level 8 passes only the 0 threshold. */
    setup_clean();
    chara_equip_set_chara_level(0, 0, 8);
    chara_skills_init_at_stage_load();
    T_ASSERT_EQ_I(chara_skills_get_count(0), 1);
    return 0;
}

int test_chara_skills_count_at_level_29_chara2_passes_all_5(void)
{
    /* Chara 2: thresholds [0, 4, 9, 24, 29].  Level 29 passes all 5. */
    setup_clean();
    chara_equip_set_chara_level(0, 2, 29);
    chara_skills_init_at_stage_load();
    T_ASSERT_EQ_I(chara_skills_get_count(2), 5);
    return 0;
}

int test_chara_skills_count_at_level_28_chara2_passes_4(void)
{
    /* Level 28 < 29, so chara 2 misses the 29 threshold. */
    setup_clean();
    chara_equip_set_chara_level(0, 2, 28);
    chara_skills_init_at_stage_load();
    T_ASSERT_EQ_I(chara_skills_get_count(2), 4);
    return 0;
}

int test_chara_skills_count_per_chara_isolation(void)
{
    /* Setting level on chara 0 doesn't affect chara 1's count. */
    setup_clean();
    chara_equip_set_chara_level(0, 0, 99);
    chara_skills_init_at_stage_load();
    T_ASSERT_EQ_I(chara_skills_get_count(0), 2);   /* chara 0 max = 2 */
    T_ASSERT_EQ_I(chara_skills_get_count(1), 1);   /* chara 1 still at level 0 */
    return 0;
}

int test_chara_skills_count_chara7_capped_at_1(void)
{
    /* Chara 7: count=1, threshold=[0].  No matter the level, never
     * exceeds 1 (the RDATA's count limit). */
    setup_clean();
    chara_equip_set_chara_level(0, 7, 99);
    chara_skills_init_at_stage_load();
    T_ASSERT_EQ_I(chara_skills_get_count(7), 1);
    return 0;
}

/* ─── tail scratch writes ─────────────────────────────────────────── */

int test_chara_skills_tail_writes(void)
{
    setup_clean();
    chara_skills_init_at_stage_load();
    T_ASSERT_EQ_I(chara_skills_get_dat_0438b874(), 0);
    T_ASSERT_EQ_I(chara_skills_get_dat_0438b878(), 2);
    T_ASSERT_EQ_I(chara_skills_get_dat_0438b87c(), 1);
    return 0;
}

/* ─── idempotency ─────────────────────────────────────────────────── */

int test_chara_skills_idempotent(void)
{
    setup_clean();
    chara_equip_set_chara_level(0, 1, 19);
    chara_skills_init_at_stage_load();
    int32_t c0_before = chara_skills_get_count(0);
    int32_t c1_before = chara_skills_get_count(1);
    chara_skills_init_at_stage_load();
    T_ASSERT_EQ_I(chara_skills_get_count(0), c0_before);
    T_ASSERT_EQ_I(chara_skills_get_count(1), c1_before);
    return 0;
}
