/*
 * test_title_continue_picker.c — the title "Continue / load" slot picker
 * (src/title_continue_picker.{c,h} = FUN_0049b537 + the FUN_0049a59e
 * DAT_09643524==1 body).
 */
#include "t.h"

#include <string.h>

#include "save_bank.h"
#include "save_work.h"
#include "scene_title.h"
#include "title_continue_picker.h"

/* Mark SAVE bank `b` occupied (non-zero dword 2) with `gold`. */
static void seed_occupied(int b, uint32_t gold)
{
    uint32_t *bank = save_bank_dwords_at(b);
    bank[SAVE_BANK_FIELD_OCCUPIED] = 1;
    bank[SAVE_BANK_FIELD_GOLD]     = gold;
    /* Non-empty item table so the count recompute has something. */
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++) {
        bank[SAVE_BANK_ITEM_TABLE_DWORD + i] = (i < 4) ? (uint32_t)(200 + i)
                                                       : 0xFFFFFFFFu;
    }
}

int test_picker_open_identity_and_cursor(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 17);
    title_continue_picker_t *p = &g_title_continue_picker;
    T_ASSERT_EQ_I(p->slot_count, 100);
    T_ASSERT_EQ_I(p->slot_index[0], 0);
    T_ASSERT_EQ_I(p->slot_index[42], 42);
    T_ASSERT_EQ_I(p->slot_index[99], 99);
    T_ASSERT_EQ_I(p->cursor, 17);
    T_ASSERT_EQ_I(p->scroll, 15);     /* 17 - 2 */
    T_ASSERT_EQ_I(p->overwrite_mode, 0);
    return 0;
}

int test_picker_open_clamps_scroll(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 1);
    T_ASSERT_EQ_I(g_title_continue_picker.scroll, 0);   /* 1-2 clamped */
    return 0;
}

int test_picker_open_overwrite_mode(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_NEW_HAS_SAVE, 30);
    title_continue_picker_t *p = &g_title_continue_picker;
    T_ASSERT_EQ_I(p->overwrite_mode, 1);
    T_ASSERT_EQ_I(p->cursor, 0);
    T_ASSERT_EQ_I(p->scroll, 0);
    T_ASSERT_EQ_I(p->prompt_pending, 1);
    return 0;
}

int test_picker_cursor_down_up(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 10);
    /* Park the cursor at the TOP of the visible window so ±1 moves stay
     * in-window (no scroll animation to freeze the next step). */
    g_title_continue_picker.scroll = 10;
    int load = -1;
    /* DOWN: +1 → 11, still within the 3-row window. */
    title_continue_picker_step(0, SCENE_TITLE_INPUT_DOWN, &load);
    T_ASSERT_EQ_I(g_title_continue_picker.cursor, 11);
    T_ASSERT_EQ_I(g_title_continue_picker.hscroll_anim, 0);
    /* UP: -1 → 10. */
    title_continue_picker_step(0, SCENE_TITLE_INPUT_UP, &load);
    T_ASSERT_EQ_I(g_title_continue_picker.cursor, 10);
    return 0;
}

int test_picker_cursor_right_arms_column_scroll(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 10);
    /* A ±3 column jump can't fit in a 3-row window, so it always also
     * arms the between-column scroll animation (engine behavior). */
    title_continue_picker_step(0, SCENE_TITLE_INPUT_RIGHT, NULL);
    T_ASSERT_EQ_I(g_title_continue_picker.cursor, 13);
    T_ASSERT_EQ_I(g_title_continue_picker.vscroll_anim, 1);
    return 0;
}

int test_picker_cursor_left_arms_column_scroll(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 40);
    /* cursor=40, scroll=38. LEFT needs cursor>0 && scroll>0. */
    title_continue_picker_step(0, SCENE_TITLE_INPUT_LEFT, NULL);
    T_ASSERT_EQ_I(g_title_continue_picker.cursor, 37);
    T_ASSERT_EQ_I(g_title_continue_picker.vscroll_anim, -1);
    return 0;
}

int test_picker_cursor_clamps_at_ends(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 0);
    /* UP at top: no move. */
    title_continue_picker_step(0, SCENE_TITLE_INPUT_UP, NULL);
    T_ASSERT_EQ_I(g_title_continue_picker.cursor, 0);
    /* Jump near the end and DOWN past it: clamps at count-1. */
    g_title_continue_picker.cursor = 99;
    title_continue_picker_step(0, SCENE_TITLE_INPUT_DOWN, NULL);
    T_ASSERT_EQ_I(g_title_continue_picker.cursor, 99);
    return 0;
}

int test_picker_scroll_anim_commits_after_5(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 2);
    /* cursor=2, scroll=0, window=3 visible. DOWN to 3 pushes past the
     * window → hscroll_anim armed (+1). */
    title_continue_picker_step(0, SCENE_TITLE_INPUT_DOWN, NULL);
    T_ASSERT_EQ_I(g_title_continue_picker.cursor, 3);
    T_ASSERT_EQ_I(g_title_continue_picker.hscroll_anim, 1);
    /* Anim runs 1→2→3→4→5 then commits scroll++ and resets. Input is
     * blocked while animating (cursor frozen). */
    for (int i = 0; i < 4; i++) {
        title_continue_picker_step(0, SCENE_TITLE_INPUT_DOWN, NULL);
    }
    T_ASSERT_EQ_I(g_title_continue_picker.hscroll_anim, 0);
    T_ASSERT_EQ_I(g_title_continue_picker.scroll, 1);
    T_ASSERT_EQ_I(g_title_continue_picker.cursor, 3);  /* frozen during anim */
    return 0;
}

int test_picker_confirm_occupied_loads(void)
{
    save_bank_arena_clear();
    save_work_clear();
    seed_occupied(8, 7777);
    save_header_set_last_slot(0);

    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 8);
    int load = -1;
    title_picker_result_t r =
        title_continue_picker_step(SCENE_TITLE_INPUT_A, 0, &load);

    T_ASSERT_EQ_I(r, TITLE_PICKER_LOAD);
    T_ASSERT_EQ_I(load, 8);
    /* Bank loaded into the active working slot (0). */
    T_ASSERT_EQ_U(save_work_dwords_at(0)[SAVE_BANK_FIELD_GOLD], 7777u);
    T_ASSERT_EQ_I(save_work_item_count(0), 4);
    /* Last-used slot remembered. */
    T_ASSERT_EQ_I(save_header_get_last_slot(), 8);
    return 0;
}

int test_picker_confirm_empty_slot_rejected(void)
{
    save_bank_arena_clear();
    save_work_clear();
    /* slot 5 left empty (dword 2 == 0). */
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 5);
    int load = -1;
    title_picker_result_t r =
        title_continue_picker_step(SCENE_TITLE_INPUT_A, 0, &load);
    T_ASSERT_EQ_I(r, TITLE_PICKER_NONE);
    T_ASSERT_EQ_I(load, -1);
    return 0;
}

int test_picker_cancel(void)
{
    title_continue_picker_open(SCENE_TITLE_MENU_CONTINUE_ANY, 5);
    title_picker_result_t r =
        title_continue_picker_step(SCENE_TITLE_INPUT_B, 0, NULL);
    T_ASSERT_EQ_I(r, TITLE_PICKER_CANCEL);
    return 0;
}
