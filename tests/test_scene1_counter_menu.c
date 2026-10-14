/*
 * test_scene1_counter_menu.c — the HOUSE counter menu (scene1_counter_menu.c):
 * FUN_004850fe list build, the b74c slide ramp, case-0 input, case-10 close.
 * Expected values = rhost retail (buy-40pct-accept: level 2 → [Open Store,
 * Cancel]; b74c 1..12 from the open; b754 1..15 → entry at 15).
 */

#include "t.h"

#include <string.h>

#include "scene1_counter_menu.h"
#include "save_bank.h"

static uint32_t s_bank[SAVE_BANK_STRIDE_BYTES / 4];

static const uint8_t *bank_with(int level, int day)
{
    memset(s_bank, 0, sizeof s_bank);
    s_bank[SAVE_BANK_FIELD_MERCHANT_LEVEL] = (uint32_t)level;
    s_bank[SAVE_BANK_FIELD_CARD_DAY] = (uint32_t)day;
    return (const uint8_t *)s_bank;
}

int test_counter_menu_build_by_level(void)
{
    counter_menu_reset();
    counter_menu_build(bank_with(2, 5));
    T_ASSERT_EQ_I(counter_menu_count(), 2);
    T_ASSERT_EQ_I(counter_menu_entry(0), COUNTER_MENU_OPEN_STORE);
    T_ASSERT_EQ_I(counter_menu_entry(1), COUNTER_MENU_CANCEL);

    counter_menu_build(bank_with(13, 20));      /* every decor entry */
    T_ASSERT_EQ_I(counter_menu_count(), 7);
    static const int want[7] = { 0, 2, 1, 3, 5, 4, 6 };
    for (int i = 0; i < 7; i++)
        T_ASSERT_EQ_I(counter_menu_entry(i), want[i]);
    T_ASSERT(strcmp(counter_menu_label(0), "Open Store") == 0);
    T_ASSERT(strcmp(counter_menu_label(6), "Cancel") == 0);
    return 0;
}

int test_counter_menu_slide_ramp(void)
{
    counter_menu_reset();
    for (int f = 1; f <= 14; f++) {
        counter_menu_slide_tick(0);
        T_ASSERT_EQ_I(counter_menu_slide(), f < 12 ? f : 12);
    }
    counter_menu_slide_tick(4);                 /* cc08 4: slides back out */
    T_ASSERT_EQ_I(counter_menu_slide(), 11);
    counter_menu_slide_tick(10);
    T_ASSERT_EQ_I(counter_menu_slide(), 12);
    return 0;
}

int test_counter_menu_input_and_close(void)
{
    counter_menu_reset();
    counter_menu_build(bank_with(2, 5));
    counter_menu_open();
    T_ASSERT_EQ_I(counter_menu_input(0, 0x8), COUNTER_MENU_IN_NONE);   /* down */
    T_ASSERT_EQ_I(counter_menu_cursor(), 1);
    T_ASSERT_EQ_I(counter_menu_input(0, 0x8), COUNTER_MENU_IN_NONE);   /* wraps */
    T_ASSERT_EQ_I(counter_menu_cursor(), 0);
    T_ASSERT_EQ_I(counter_menu_input(0, 0x4), COUNTER_MENU_IN_NONE);   /* up wraps */
    T_ASSERT_EQ_I(counter_menu_cursor(), 1);
    T_ASSERT_EQ_I(counter_menu_input(0x20, 0), COUNTER_MENU_IN_CANCEL);
    T_ASSERT_EQ_I(counter_menu_input(0x10, 0), COUNTER_MENU_IN_CONFIRM);

    counter_menu_open();                        /* cursor back on Open Store */
    for (int f = 1; f < 15; f++)
        T_ASSERT_EQ_I(counter_menu_close_tick(), -1);
    T_ASSERT_EQ_I(counter_menu_close_tick(), COUNTER_MENU_OPEN_STORE);
    T_ASSERT_EQ_I(counter_menu_timer(), 15);
    return 0;
}
