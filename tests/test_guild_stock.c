/*
 * test_guild_stock.c — src/guild_stock.c (FUN_00490e56): the init pass by
 * restock kind, the daily pass, the Continue re-init gate.
 */
#include "t.h"
#include "guild_stock.h"
#include "rng.h"
#include "save_work.h"
#include "tables_item.h"

#include <string.h>

static int16_t *stock(void)
{
    return (int16_t *)(void *)(save_work_bank_at(save_work_active_slot()) + GUILD_STOCK_OFF);
}

/* g_item is shared with later tests: each test works on a copy's slot */
static item_state_t s_saved;

static void reset(void)
{
    s_saved = g_item;
    memset(&g_item, 0, sizeof g_item);
    save_work_set_active_slot(0);
    memset(stock(), 0, GUILD_STOCK_ITEMS * 4);
    rng_seed(1);
}

int test_guild_stock_init_by_kind(void)
{
    reset();
    for (int k = 0; k <= 5; k++) g_item.records[k].stock_info[0] = (uint8_t)k;
    rng_seed(7);
    const uint16_t r1 = rng_next15();
    rng_seed(7);
    guild_stock_daily(0);
    int16_t *s = stock();
    T_ASSERT_EQ_I(s[0], 100);
    T_ASSERT_EQ_I(s[2], r1 % 3 + 3);
    T_ASSERT_EQ_I(s[4], 0);
    T_ASSERT_EQ_I(s[6], 1);
    T_ASSERT_EQ_I(s[8], 0);
    T_ASSERT_EQ_I(s[10], 1);
    T_ASSERT_EQ_I(s[12], 100);   /* the rest: kind 0 */
    /* kinds 1, 2, 3, 5 draw; 4 and 0 do not */
    rng_seed(7);
    for (int i = 0; i < 4; i++) (void)rng_next15();
    const uint32_t want = g_rng_seed;
    rng_seed(7);
    memset(stock(), 0, GUILD_STOCK_ITEMS * 4);
    guild_stock_daily(0);
    T_ASSERT_EQ_U(g_rng_seed, want);
    g_item = s_saved;
    return 0;
}

int test_guild_stock_daily_kind4_and_kind1(void)
{
    reset();
    g_item.records[1].stock_info[0] = 4;
    g_item.records[2].stock_info[0] = 1;
    guild_stock_daily(0);                 /* init */
    int16_t *s = stock();
    s[4] = 3;                             /* kind 1: count 3 */
    for (int day = 0; day < 5; day++) guild_stock_daily(0);
    T_ASSERT_EQ_I(s[4], 4);               /* +1 after 5 days */
    T_ASSERT_EQ_I(s[5], 0);
    T_ASSERT(s[2] >= 0 && s[2] <= 1);     /* kind 4: max(rand % 3 - 1, 0) */
    T_ASSERT_EQ_I(s[3], 5);
    for (int day = 5; day < 1 % 8 + 20; day++) guild_stock_daily(0);
    T_ASSERT_EQ_I(s[2], 1);               /* 1 on day i % 8 + 20 */
    T_ASSERT_EQ_I(s[3], 0);
    g_item = s_saved;
    return 0;
}

int test_guild_stock_continue_reinit_gate(void)
{
    reset();
    g_item.records[3].stock_info[0] = 4;
    guild_stock_daily(0);
    int16_t *s = stock();
    s[6] = 1;
    s[0] = 77;
    guild_stock_daily(1);                 /* no kind-4 count >= 2: untouched */
    T_ASSERT_EQ_I(s[0], 77);
    s[6] = 2;
    guild_stock_daily(1);                 /* re-init */
    T_ASSERT_EQ_I(s[0], 100);
    T_ASSERT_EQ_I(s[6], 0);
    g_item = s_saved;
    return 0;
}
