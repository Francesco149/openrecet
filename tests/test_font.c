/*
 * test_font.c — unit tests for src/font.c (init + age tick).
 *
 * Mirrors FUN_0047c228's init shape and FUN_0047c29d's per-frame age
 * bump. The atlas loader, GDI builder, slot allocator and draw_text
 * land in subsequent commits — their tests slot in here when they do.
 */

#include "t.h"

#include "../src/font.h"

int test_font_init_zeros_state(void)
{
    /* Pre-dirty every field so we know init clears them. */
    memset(&g_font, 0xab, sizeof g_font);

    font_init();

    /* Every texture pointer is null. */
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        T_ASSERT(g_font.textures[i] == NULL);
    }

    /* Every slot has slot_id == its index, everything else zero. */
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        T_ASSERT_EQ_U(g_font.slots[i].slot_id, (unsigned)i);
        T_ASSERT_EQ_U(g_font.slots[i].cp_byte0, 0);
        T_ASSERT_EQ_U(g_font.slots[i].cp_byte1, 0);
        T_ASSERT_EQ_U(g_font.slots[i].in_use, 0);
        T_ASSERT_EQ_U(g_font.slots[i].age, 0);
        T_ASSERT_EQ_U(g_font.slots[i].record_id, 0);
    }

    /* The two engine "default font height" globals at DAT_073b18bc /
     * DAT_073b18c0 — both set to 0x2a (42) by FUN_0047c228. */
    T_ASSERT_EQ_I(g_font.default_height,  0x2a);
    T_ASSERT_EQ_I(g_font.default_height2, 0x2a);

    return 0;
}

int test_font_init_is_idempotent(void)
{
    font_init();
    /* Allocate one slot, then re-init — slot should be wiped. */
    g_font.slots[5].in_use = 1;
    g_font.slots[5].cp_byte0 = 0x41; /* 'A' */
    g_font.slots[5].age = 42;
    g_font.textures[5] = (void *)0xdeadbeef;

    font_init();

    T_ASSERT_EQ_U(g_font.slots[5].in_use, 0);
    T_ASSERT_EQ_U(g_font.slots[5].cp_byte0, 0);
    T_ASSERT_EQ_U(g_font.slots[5].age, 0);
    T_ASSERT_EQ_U(g_font.slots[5].slot_id, 5);
    T_ASSERT(g_font.textures[5] == NULL);

    return 0;
}

int test_font_age_tick_advances_in_use_only(void)
{
    font_init();

    /* Mark three slots in_use (5, 17, 199), the rest free. */
    g_font.slots[5].in_use   = 1;
    g_font.slots[17].in_use  = 1;
    g_font.slots[199].in_use = 1;

    font_age_tick();

    T_ASSERT_EQ_U(g_font.slots[5].age,   1);
    T_ASSERT_EQ_U(g_font.slots[17].age,  1);
    T_ASSERT_EQ_U(g_font.slots[199].age, 1);

    /* Free slots stay at age 0. */
    T_ASSERT_EQ_U(g_font.slots[0].age,   0);
    T_ASSERT_EQ_U(g_font.slots[42].age,  0);
    T_ASSERT_EQ_U(g_font.slots[100].age, 0);

    /* A second tick bumps in-use ages but not free ones. */
    font_age_tick();
    T_ASSERT_EQ_U(g_font.slots[5].age,   2);
    T_ASSERT_EQ_U(g_font.slots[17].age,  2);
    T_ASSERT_EQ_U(g_font.slots[199].age, 2);
    T_ASSERT_EQ_U(g_font.slots[0].age,   0);

    return 0;
}

int test_font_age_tick_all_free_is_noop(void)
{
    font_init();
    /* All in_use=0 by init, so age tick should change nothing. */
    font_age_tick();
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        T_ASSERT_EQ_U(g_font.slots[i].age, 0);
    }
    return 0;
}

int test_font_age_tick_does_not_touch_other_fields(void)
{
    font_init();
    g_font.slots[10].in_use = 1;
    g_font.slots[10].cp_byte0 = 0x82;
    g_font.slots[10].cp_byte1 = 0xa0;
    g_font.slots[10].record_id = 0x1234;

    for (int i = 0; i < 50; i++) font_age_tick();

    T_ASSERT_EQ_U(g_font.slots[10].in_use,    1);
    T_ASSERT_EQ_U(g_font.slots[10].cp_byte0,  0x82);
    T_ASSERT_EQ_U(g_font.slots[10].cp_byte1,  0xa0);
    T_ASSERT_EQ_U(g_font.slots[10].record_id, 0x1234);
    T_ASSERT_EQ_U(g_font.slots[10].slot_id,   10);
    T_ASSERT_EQ_U(g_font.slots[10].age,       50);
    return 0;
}
