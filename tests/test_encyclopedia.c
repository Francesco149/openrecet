/*
 * test_encyclopedia.c — host tests for the pause Encyclopedia submenu (type 6):
 * the setup catalog build (FUN_0049f012) + the nav state machine (FUN_0049f365).
 * The render (FUN_0049f8b8) is Win32-only and verified pixel-by-pixel against
 * retail in Trace Studio v3.
 */
#include "t.h"
#include "encyclopedia.h"
#include "tables_item.h"
#include "save_work.h"
#include "sim.h"

#include <string.h>

/* Clear the encyclopedia + input state to a known base. */
static void enc_clear(void)
{
    memset(g_enc_slot, 0xff, sizeof g_enc_slot);
    for (int i = 0; i < ENC_CAT_MAX * ENC_CAT_CELLS; i++) g_enc_index[i] = i % ENC_CAT_CELLS;
    g_enc_detail = g_enc_col = g_enc_row = g_enc_anim = 0;
    g_enc_cat = 0; g_enc_cat_count = 0; g_enc_comp_num = g_enc_comp_den = 0;
    g_enc_scroll = 0; g_enc_rows_cur = g_enc_rows_alt = g_enc_rows_prev = 0;
    g_sim_buttons[0].pressed = 0;
    g_sim_buttons[0].held    = 0;
}

/* A tiny item DB: category 0 (Swords: header id0 + items id1,id2) and category 1
 * (Daggers: header id100 + item id101). */
static void enc_make_db(void)
{
    memset(&g_item, 0, sizeof g_item);
    struct { int id, cat, sub; } recs[] = {
        {  0, 0, 0 }, {  1, 0, 1 }, {  2, 0, 2 },   /* Swords */
        {100, 1, 0 }, {101, 1, 1 },                 /* Daggers */
    };
    g_item.count = 5;
    for (int i = 0; i < 5; i++) {
        g_item.records[i].valid    = 1;
        g_item.records[i].item_id  = recs[i].id;
        g_item.records[i].category = recs[i].cat;
        g_item.records[i].subindex = recs[i].sub;
        g_item.records[i].rank     = 0;
    }
}

/* Set a discovery flag for distinct-category store record `ci`, subindex `sub`. */
static void enc_discover(int slot, int ci, int sub)
{
    uint8_t *b = save_work_bank_at(slot);
    b[0x279d8 + ci * 0x48 + 8 + sub] = 1;
}

/* ── setup ── */

int test_encyclopedia_setup_all_discovered(void)
{
    enc_clear();
    enc_make_db();
    save_work_set_active_slot(0);
    memset(save_work_bank_at(0), 0, 0x2dfc8);
    /* discover both Swords items (cat 0: sub 0,1) + the Dagger (cat 1: sub 0). */
    enc_discover(0, 0, 0);
    enc_discover(0, 0, 1);
    enc_discover(0, 1, 0);

    int complete = encyclopedia_setup(0);

    T_ASSERT_EQ_I(g_enc_cat_count, 2);          /* Swords + Daggers */
    T_ASSERT_EQ_I(g_enc_comp_den, 3);           /* 2 swords + 1 dagger catalog */
    T_ASSERT_EQ_I(g_enc_comp_num, 3);           /* all 3 discovered */
    T_ASSERT_EQ_I(complete, 1);                 /* 100% → returns 1 */
    /* slot table packed with the displayed item ids (cat*100 + pos + 1). */
    T_ASSERT_EQ_I(g_enc_slot[0], 1);
    T_ASSERT_EQ_I(g_enc_slot[1], 2);
    T_ASSERT_EQ_I(g_enc_slot[ENC_CAT_CELLS], 101);
    return 0;
}

int test_encyclopedia_setup_partial(void)
{
    enc_clear();
    enc_make_db();
    save_work_set_active_slot(0);
    memset(save_work_bank_at(0), 0, 0x2dfc8);
    /* discover only the first Sword (cat 0 sub 0). */
    enc_discover(0, 0, 0);

    int complete = encyclopedia_setup(0);

    T_ASSERT_EQ_I(g_enc_cat_count, 2);          /* both categories still listed */
    T_ASSERT_EQ_I(g_enc_comp_den, 3);
    T_ASSERT_EQ_I(g_enc_comp_num, 1);           /* only 1 discovered */
    T_ASSERT_EQ_I(complete, 0);                 /* not 100% */
    T_ASSERT_EQ_I(g_enc_slot[0], 1);            /* the one discovered sword */
    /* the all-undiscovered Daggers keep one -2 placeholder at slot 0. */
    T_ASSERT_EQ_I(g_enc_slot[ENC_CAT_CELLS], -2);
    return 0;
}

/* ── nav ── */

/* A 1-category catalog with a single 3-wide row of items, cursor at (0,0). */
static void enc_one_cat_row(void)
{
    enc_clear();
    enc_make_db();
    g_enc_cat_count = 1;
    g_enc_cat = 0;
    g_enc_slot[0] = 1; g_enc_slot[1] = 2; g_enc_slot[2] = 1;   /* 3 cells filled */
    g_enc_index[0] = 0; g_enc_index[1] = 1; g_enc_index[2] = 2;
}

int test_encyclopedia_update_b_closes(void)
{
    enc_one_cat_row();
    g_sim_buttons[0].pressed = 0x20;            /* B */
    int r = encyclopedia_update();
    T_ASSERT_EQ_I(r, 1);                        /* close the submenu */
    return 0;
}

int test_encyclopedia_update_right_moves_column(void)
{
    enc_one_cat_row();
    g_sim_buttons[0].held = 0x01;               /* Right */
    int r = encyclopedia_update();
    T_ASSERT_EQ_I(r, 0);
    T_ASSERT_EQ_I(g_enc_col, 1);                /* col 0 → 1 (not at last populated col) */
    return 0;
}

int test_encyclopedia_update_rshoulder_pages(void)
{
    /* two categories so a page is possible. */
    enc_clear();
    enc_make_db();
    g_enc_cat_count = 2;
    g_enc_cat = 0;
    g_enc_slot[0] = 1;                          /* cat 0 has a cell */
    g_enc_slot[ENC_CAT_CELLS] = 101;           /* cat 1 has a cell */
    g_enc_index[0] = 0; g_enc_index[ENC_CAT_CELLS] = 0;
    g_sim_buttons[0].held = 0x80;               /* R-shoulder → page next */
    int r = encyclopedia_update();
    T_ASSERT_EQ_I(r, 0);
    T_ASSERT_EQ_I(g_enc_anim, 1);               /* category slide started (+1) */
    return 0;
}

int test_encyclopedia_anim_commits_at_ten(void)
{
    enc_clear();
    enc_make_db();
    g_enc_cat_count = 2;
    g_enc_cat = 0;
    g_enc_slot[0] = 1; g_enc_slot[ENC_CAT_CELLS] = 101;
    g_enc_anim = 9;                             /* mid positive slide */
    int r = encyclopedia_update();
    T_ASSERT_EQ_I(r, 0);
    T_ASSERT_EQ_I(g_enc_anim, 0);               /* committed + reset */
    /* positive anim commits to (catn-1+cat) % catn = (2-1+0)%2 = 1. */
    T_ASSERT_EQ_I(g_enc_cat, 1);
    return 0;
}

/* A FULL single-category grid: 12 items (4 rows × 3 cols), cursor at (0,0). */
static void enc_full_grid(void)
{
    enc_clear();
    enc_make_db();
    g_enc_cat_count = 1;
    g_enc_cat = 0;
    for (int i = 0; i < 12; i++) { g_enc_slot[i] = (int16_t)(i + 1); g_enc_index[i] = i; }
}

int test_encyclopedia_down_scrolls_full_grid(void)
{
    enc_full_grid();
    /* three Down presses: row 0→1→2→3, scroll 0→0→0→1 (window of 3). */
    for (int i = 0; i < 3; i++) {
        g_sim_buttons[0].held = 0x08;
        encyclopedia_update();
    }
    T_ASSERT_EQ_I(g_enc_row, 3);
    T_ASSERT_EQ_I(g_enc_scroll, 1);
    return 0;
}

int test_encyclopedia_update_detail_toggle(void)
{
    enc_one_cat_row();
    /* A (0x40) on a discovered cell opens the detail overlay. */
    g_sim_buttons[0].pressed = 0x40;
    encyclopedia_update();
    T_ASSERT_EQ_I(g_enc_detail, 1);
    /* A again closes it. */
    g_sim_buttons[0].pressed = 0x40;
    encyclopedia_update();
    T_ASSERT_EQ_I(g_enc_detail, 0);
    return 0;
}
