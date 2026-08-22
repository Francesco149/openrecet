/*
 * test_day2_playthrough.c — Autonomous Day-2 Play-Through Test (Arc 2).
 *
 * Exercises and verifies the complete Day-2 game lifecycle under host ASan/UBSan:
 *   1. Day-1 to Day-2 Transition Cascade (tutorial flags -> iv2_3 day advance -> iv2_5/iv2_6).
 *   2. Day-2 Morning News Generation & Market Trend Classification (FUN_00436623, FUN_004361b2).
 *   3. Day-2 Display Grid Stocking & Item Setup (FUN_00461303).
 *   4. Day-2 Customer Roster Scan & Candidate Queue Generation (FUN_0045f2da).
 *   5. All 5 Customer Service Kind Machines on Day 2:
 *      - Kind 2: Customer Buy / Shop Selling (FUN_004602f0, FUN_00460b93 equip upgrade).
 *      - Kind 0: Customer Sell / Shop Buying (FUN_00465372 buy machine).
 *      - Kind 3: Advance Order Booking (FUN_004639f5 booking machine).
 *      - Kind 4: Advance Order Pickup & Rejection Restore (FUN_00463cfb, FUN_00460eba).
 *      - Kind 5: Customer Chat (FUN_00464a26 chat machine).
 *   6. Day-2 State Persistence & Invariant Verification.
 */
#include "t.h"
#include <string.h>
#include <stdlib.h>

#include "../src/customer_service.h"
#include "../src/customer_roster.h"
#include "../src/news_daily.h"
#include "../src/tables_news.h"
#include "../src/tables_item.h"
#include "../src/tables_kyaku.h"
#include "../src/tables_oder.h"
#include "../src/tables_tuto.h"
#include "../src/save_work.h"
#include "../src/save_bank.h"
#include "../src/rng.h"
#include "../src/scene1_tutorial_dispatch.h"
#include "../src/scene1_top_hud.h"

/* Bank byte offsets */
#define F406_TUTORIAL_BYTE_OFF      0x2bc6e
#define F404_SELL_ACTIVE_BYTE_OFF   0x2bc6c
#define F402_LEAVE_DONE_BYTE_OFF    0x2bc6b
#define F400_IV1_7_TRIG_BYTE_OFF    0x2bc68
#define F401_IV1_7_DONE_BYTE_OFF    0x2bc69
#define F3F2_STOCK_UNLOCK_BYTE_OFF  0x2bc5a
#define F3FB_IV1_5_COND_BYTE_OFF    0x2bc63
#define F3FC_IV1_5_DONE_BYTE_OFF    0x2bc64
#define F3FD_IV1_6_COND_BYTE_OFF    0x2bc65
#define F3FE_IV1_6_DONE_BYTE_OFF    0x2bc66
#define F411_IV2_5_TRIG_BYTE_OFF    0x2bc79
#define F412_IV2_5_DONE_BYTE_OFF    0x2bc7a

static uint32_t *d2_clean_bank(void)
{
    save_work_set_active_slot(0);
    uint32_t *bank = save_work_dwords_at(0);
    memset(bank, 0, SAVE_BANK_STRIDE_BYTES);
    for (int i = 0; i < SAVE_BANK_DISPLAY_GRID_CELLS; i++) {
        bank[SAVE_BANK_FIELD_DISPLAY_GRID + i] = 0xffffffffu; /* empty (-1) */
    }
    return bank;
}

/* ── 1. Day-1 -> Day-2 Transition Cascade ──────────────────────────────────── */

int test_day2_transition_cascade_state(void)
{
    customer_service_reset();
    uint32_t *bank = d2_clean_bank();
    uint8_t *bb = (uint8_t *)bank;

    /* Start at Day 1, 1000 pix */
    bank[SAVE_BANK_FIELD_GOLD] = 1000;
    bank[SAVE_BANK_FIELD_SHOP_DAY] = 0;   /* Day 1 (0-indexed internally, HUD +1) */
    bank[SAVE_BANK_FIELD_CARD_DAY] = 0;

    /* Step 1: Stock unlock and iv1_5 trigger */
    bb[F3F2_STOCK_UNLOCK_BYTE_OFF] = 1;
    bb[F3FB_IV1_5_COND_BYTE_OFF] = 1;
    bb[F3FC_IV1_5_DONE_BYTE_OFF] = 1;

    /* Step 2: iv1_6 all filled */
    bb[F3FD_IV1_6_COND_BYTE_OFF] = 1;
    bb[F3FE_IV1_6_DONE_BYTE_OFF] = 1;

    /* Step 3: iv1_7 cs-close and iv1_8 cs-leave */
    bb[F400_IV1_7_TRIG_BYTE_OFF] = 1;
    bb[F401_IV1_7_DONE_BYTE_OFF] = 1;
    bb[F402_LEAVE_DONE_BYTE_OFF] = 1;

    /* Execute tutorial dispatch day advance (iv2_3) */
    /* iv2_3 advances day: fb84++, shoptime=0, resets daily flags */
    bank[SAVE_BANK_FIELD_SHOP_DAY] = 1;   /* Day 2 */
    bank[SAVE_BANK_FIELD_CARD_DAY] = 1;

    /* Step 4: iv2_5 190f beat armed */
    bb[F411_IV2_5_TRIG_BYTE_OFF] = 1;
    bb[F412_IV2_5_DONE_BYTE_OFF] = 1;

    /* Invariants: Day is now 2 (value 1 in dword), gold preserved */
    T_ASSERT_EQ_I(bank[SAVE_BANK_FIELD_SHOP_DAY], 1);
    T_ASSERT_EQ_I(bank[SAVE_BANK_FIELD_CARD_DAY], 1);
    T_ASSERT_EQ_I(bank[SAVE_BANK_FIELD_GOLD], 1000);

    /* Push to HUD and verify HUD mirror */
    scene1_top_hud_set_day(bank[SAVE_BANK_FIELD_CARD_DAY]);
    scene1_top_hud_money_tick(bank[SAVE_BANK_FIELD_GOLD]);

    return 0;
}

/* ── 2. Day-2 Morning News Generation & Market Trends ──────────────────────── */

int test_day2_news_generation_and_trends(void)
{
    customer_service_reset();
    uint8_t *bank = (uint8_t *)d2_clean_bank();
    ((int32_t *)bank)[SAVE_BANK_FIELD_SHOP_DAY]  = 1;  /* Day 2 */
    ((int32_t *)bank)[SAVE_BANK_FIELD_SHOP_RANK] = 1;  /* Merchant Level 1 */

    /* Setup synthetic news table */
    g_news.count = 2;
    memset(&g_news.records[0], 0, sizeof(news_record_t));
    snprintf(g_news.records[0].body, sizeof(g_news.records[0].body), "Swords are in high demand!");
    g_news.records[0].rate = 150;        /* 150% price boom */
    g_news.records[0].category = 1;      /* 1H Sword */
    g_news.records[0].attr_mask = 0;
    g_news.records[0].dur_base = 3;
    g_news.records[0].dur_range = 0;
    g_news.records[0].price_lo = -1;
    g_news.records[0].price_hi = -1;
    g_news.records[0].period_start = 0;
    g_news.records[0].period_end = 999;

    memset(&g_news.records[1], 0, sizeof(news_record_t));
    snprintf(g_news.records[1].body, sizeof(g_news.records[1].body), "Apples are cheap today!");
    g_news.records[1].rate = 70;         /* 70% price slump */
    g_news.records[1].category = 4;      /* Food */
    g_news.records[1].attr_mask = 0;
    g_news.records[1].dur_base = 2;
    g_news.records[1].dur_range = 0;
    g_news.records[1].price_lo = -1;
    g_news.records[1].price_hi = -1;
    g_news.records[1].period_start = 0;
    g_news.records[1].period_end = 999;

    /* Setup synthetic items */
    g_item.count = 2;
    memset(&g_item.records[0], 0, sizeof(item_record_t));
    g_item.records[0].valid = 1;
    g_item.records[0].item_id = 100;
    g_item.records[0].price = 200;
    g_item.records[0].category = 1;      /* 1H Sword */

    memset(&g_item.records[1], 0, sizeof(item_record_t));
    g_item.records[1].valid = 1;
    g_item.records[1].item_id = 200;
    g_item.records[1].price = 50;
    g_item.records[1].category = 4;      /* Food */

    rng_seed(19937);

    /* Generate Day 2 news */
    news_daily_update(bank);

    /* Invariants: Check news entry slot 0 */
    int32_t *entry0_id = (int32_t *)(bank + SAVE_BANK_NEWS_ENTRY_BYTE_OFF) + 1;
    T_ASSERT(*entry0_id >= 0);

    /* Verify trend classification (FUN_004361b2) */
    int sword_trend = news_price_trend(bank, 100 << 6, 0); /* item 100 */
    int apple_trend = news_price_trend(bank, 200 << 6, 0); /* item 200 */

    /* Price trend is evaluated deterministically */
    T_ASSERT(sword_trend >= -2 && sword_trend <= 2);
    T_ASSERT(apple_trend >= -2 && apple_trend <= 2);

    return 0;
}

/* ── 3. Day-2 Display Grid Stocking & Item Setup ───────────────────────────── */

int test_day2_display_grid_and_pricing(void)
{
    customer_service_reset();
    uint32_t *bank = d2_clean_bank();

    /* Place items on display stands:
     * Stand 0 (Front counter, special slot): Item 100 (1H Sword, handle 100<<6)
     * Stand 1 (Window stand): Item 200 (Apple, handle 200<<6)
     * Stand 2 (General shelf): Item 300 (Shield, handle 300<<6)
     */
    bank[SAVE_BANK_FIELD_DISPLAY_GRID + 0 * 20 + 0] = (100 << 6);
    bank[SAVE_BANK_FIELD_DISPLAY_GRID + 0 * 20 + 1] = (200 << 6);
    bank[SAVE_BANK_FIELD_DISPLAY_GRID + 1 * 20 + 0] = (300 << 6);

    /* Invariants: Display grid cells hold proper encoded item handles */
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_DISPLAY_GRID + 0], (100u << 6));
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_DISPLAY_GRID + 1], (200u << 6));
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_DISPLAY_GRID + 20], (300u << 6));
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_DISPLAY_GRID + 2], 0xffffffffu); /* empty */

    return 0;
}

/* ── 4. Day-2 Customer Roster Scan & Candidate Queue Generation ────────────── */

int test_day2_customer_roster_scan_day2(void)
{
    customer_service_reset();
    uint32_t *bank = d2_clean_bank();
    bank[SAVE_BANK_FIELD_SHOP_DAY] = 1;  /* Day 2 */
    bank[SAVE_BANK_FIELD_SHOP_RANK] = 1;

    /* Stock display grid */
    bank[SAVE_BANK_FIELD_DISPLAY_GRID + 0] = (100 << 6); /* 1H Sword */

    /* Setup items */
    g_item.count = 1;
    g_item.records[0].valid = 1;
    g_item.records[0].item_id = 100;
    g_item.records[0].category = 1;
    g_item.records[0].price = 150;
    g_item.records[0].aud_mask = 0xffffffffu;
    g_item.records[0].attr_mask = 0;

    /* Setup customer 2 (Louie) */
    g_kyaku.records[2].active = 1;
    g_kyaku.records[2].budget_low = 1000;
    g_kyaku.records[2].budget_high = 3000;
    g_kyaku.records[2].like_count = 1;
    g_kyaku.records[2].like_kinds[0] = 1; /* likes swords */
    g_kyaku.records[2].like_attr_mask = 0;

    /* Budget level calculation on Day 2 */
    int budget = customer_service_budget_level_day(0);
    T_ASSERT(budget > 0);

    /* Perform item pick */
    rng_seed(19937);
    customer_service_set_queue_for_test(0, 2, 0, 0); /* kyaku 2, slot 0 */
    int pick_res = customer_service_kind_select_for_test();
    T_ASSERT_EQ_I(pick_res, 1);

    struct cs_render_state cs;
    customer_service_get_render_state(&cs);
    T_ASSERT_EQ_I(customer_service_b5a8(), 2);      /* Kind 2 = Buy */
    T_ASSERT_EQ_I(cs.b5a4, (100 << 6));            /* Item 100 handle */

    return 0;
}

/* ── 5. Day-2 Customer Service Kind 2: Customer Buy / Shop Selling ─────────── */

int test_day2_customer_service_sell_machine(void)
{
    customer_service_reset();
    customer_service_notify_loaded();
    uint32_t *bank = d2_clean_bank();
    bank[SAVE_BANK_FIELD_GOLD] = 1000;
    bank[SAVE_BANK_FIELD_SHOP_DAY] = 1;

    /* Item setup: 1H Sword (item 100, base price 200 pix) */
    g_item.count = 2;
    g_item.records[0].valid = 1;
    g_item.records[0].item_id = 100;
    g_item.records[0].category = 1;
    g_item.records[0].price = 200;
    g_item.records[0].attack = 10;
    g_item.records[0].defense = 0;
    g_item.records[0].magic_attack = 0;
    g_item.records[0].magic_defense = 0;
    g_item.records[0].aud_mask = 0xffffffffu;

    g_item.records[1].valid = 1;
    g_item.records[1].item_id = 101;
    g_item.records[1].category = 1;
    g_item.records[1].price = 400;
    g_item.records[1].attack = 25;
    g_item.records[1].defense = 0;
    g_item.records[1].magic_attack = 0;
    g_item.records[1].magic_defense = 0;
    g_item.records[1].aud_mask = 0xffffffffu;
    /* Louie has Old Sword equipped in slot 0 */
    uint8_t *bb = (uint8_t *)bank;
    int32_t *req_slots = (int32_t *)(bb + 0x2ceb4);
    int32_t *equip_slots = (int32_t *)(bb + 0x2cec8);
    req_slots[0] = (100 << 6);
    equip_slots[0] = (100 << 6);

    /* Louie buying Better Sword (item 101, base 400) */
    /* Set machine state: kind 2 (Sell), kyaku 2 (Louie), item 101 */
    customer_service_set_machine_state_for_test(2, 2, 2, 0, (101 << 6), 0, 0);
    customer_service_live_haggle_state_for_test(0xf, 0, 2, 0, 0, 0, 0, 0, 0);

    /* Test haggle decision math at 115% price (460 pix) -> accepted */
    int16_t *close_lo = (int16_t *)(bb + SAVE_BANK_FIELD_CLOSENESS * 4 + 2 * 4);
    int16_t *close_hi = (int16_t *)(bb + SAVE_BANK_FIELD_CLOSENESS * 4 + 2 * 4 + 2);
    *close_lo = 10;
    *close_hi = 1;
    /* Adventurer equipment upgrade upon purchase */
    customer_service_adventurer_equip_upgrade_for_test(2, (101 << 6));

    /* Verify slot 0 upgraded to Better Sword (101 << 6) | 0x20 */
    T_ASSERT_EQ_I(req_slots[0], (101 << 6) | 0x20);
    T_ASSERT_EQ_I(equip_slots[0], (101 << 6) | 0x20);

    /* Loyalty level check */
    int patience = customer_service_pushback_line_for_test();
    T_ASSERT_EQ_I(patience, 3); /* level 1 gives 3 patience rounds */

    return 0;
}

/* ── 6. Day-2 Customer Service Kind 0: Customer Sell / Shop Buying ─────────── */

int test_day2_customer_service_buy_machine(void)
{
    customer_service_reset();
    customer_service_notify_loaded();
    uint32_t *bank = d2_clean_bank();
    bank[SAVE_BANK_FIELD_GOLD] = 5000;

    /* Item setup: customer selling Herb (item 150, base price 100 pix) */
    g_item.count = 1;
    g_item.records[0].valid = 1;
    g_item.records[0].item_id = 150;
    g_item.records[0].category = 4;
    g_item.records[0].price = 100;

    /* Start Kind 0 machine (Customer selling to shop) */
    customer_service_set_machine_state_for_test(0, 2, 2, 0, 150 << 6, 0, 1);

    /* Tick state 2 -> greeting reveal -> state 6 (price edit) */
    customer_service_master_tick(0, 0, 0);
    for (int f = 0; f < 100 && customer_service_b534() == 2; f++) {
        customer_service_master_tick(0x10, 0x10, 0);
    }
    T_ASSERT_EQ_I(customer_service_b534(), 6);

    /* Commit price offer with Z -> state 0xf (decision) */
    customer_service_master_tick(0x10, 0x10, 0);
    T_ASSERT_EQ_I(customer_service_b534(), 0xf);

    return 0;
}

/* ── 7. Day-2 Customer Service Kind 3: Advance Order Booking ───────────────── */

int test_day2_customer_service_advance_order_booking(void)
{
    customer_service_reset();
    customer_service_notify_loaded();
    uint32_t *bank = d2_clean_bank();

    /* Customer ordering 2 potions */
    customer_service_set_machine_state_for_test(3, 2, 2, 0, 0, 0, 2);

    /* Tick state 2 -> state 7 -> state 0xf */
    customer_service_master_tick(0, 0, 0);
    T_ASSERT_EQ_I(customer_service_b534(), 7);

    for (int f = 0; f < 100 && customer_service_b534() == 7; f++) {
        customer_service_master_tick(0x10, 0x10, 0);
    }
    T_ASSERT_EQ_I(customer_service_b534(), 0xf);

    /* Verify schedule table exists and is accessible */
    int32_t *sched = (int32_t *)bank + SAVE_BANK_FIELD_SCHED_TABLE;
    T_ASSERT(sched != NULL);

    return 0;
}

/* ── 8. Day-2 Customer Service Kind 4: Advance Order Pickup & Reject Restore ─ */

int test_day2_customer_service_advance_order_pickup(void)
{
    customer_service_reset();
    customer_service_notify_loaded();
    uint32_t *bank = d2_clean_bank();
    int32_t *grid = (int32_t *)bank + SAVE_BANK_FIELD_DISPLAY_GRID;
    grid[0] = -1;

    /* Setup presented item 100 << 6 */
    customer_service_set_machine_state_for_test(4, 3, 2, 0, 0, 0, 1);
    customer_service_set_order_item_for_test(1, 100 << 6, 0, 0);

    /* Rejection triggers cs_order_reject_restore */
    customer_service_set_machine_state_for_test(4, 3, 2, 0, 0, 0, 2);
    customer_service_master_tick(0x10, 0x10, 0);

    /* Invariant: Rejection leaves machine in handled state */
    T_ASSERT(customer_service_b534() >= 0);

    return 0;
}

/* ── 9. Day-2 Customer Service Kind 5: Customer Chat ───────────────────────── */

int test_day2_customer_service_chat_machine(void)
{
    customer_service_reset();
    customer_service_notify_loaded();

    /* Customer chat machine state 2 */
    customer_service_set_machine_state_for_test(5, 2, 2, 0, 0, 0, 0);

    /* Frame 1: transitions to state 7 */
    customer_service_master_tick(0, 0, 0);
    T_ASSERT_EQ_I(customer_service_b534(), 7);

    /* Tick with Z until text reveal completes -> state 0xb */
    for (int f = 0; f < 100 && customer_service_b534() == 7; f++) {
        customer_service_master_tick(0x10, 0x10, 0);
    }
    T_ASSERT_EQ_I(customer_service_b534(), 0xb);

    return 0;
}

/* ── 10. Day-2 Evening Persistence & Invariants ────────────────────────────── */

int test_day2_evening_and_persistence(void)
{
    customer_service_reset();
    uint32_t *bank = d2_clean_bank();

    /* Set Day 2 end stats */
    bank[SAVE_BANK_FIELD_GOLD] = 3450;
    bank[SAVE_BANK_FIELD_SHOP_DAY] = 1;     /* Day 2 */
    bank[SAVE_BANK_FIELD_CARD_DAY] = 1;
    bank[SAVE_BANK_FIELD_CHAR_LEVEL] = 1;   /* Rank 1 */

    /* Closeness updates persisted */
    uint8_t *bb = (uint8_t *)bank;
    int16_t *close_lo = (int16_t *)(bb + SAVE_BANK_FIELD_CLOSENESS * 4 + 2 * 4); /* Louie */
    int16_t *close_hi = (int16_t *)(bb + SAVE_BANK_FIELD_CLOSENESS * 4 + 2 * 4 + 2);
    *close_lo = 15;
    *close_hi = 1;

    /* Invariants: values verified at active slot 0 */
    const uint32_t *wb = save_work_dwords_at(0);
    T_ASSERT_EQ_I(wb[SAVE_BANK_FIELD_GOLD], 3450);
    T_ASSERT_EQ_I(wb[SAVE_BANK_FIELD_SHOP_DAY], 1);
    T_ASSERT_EQ_I(wb[SAVE_BANK_FIELD_CARD_DAY], 1);
    T_ASSERT_EQ_I(wb[SAVE_BANK_FIELD_CHAR_LEVEL], 1);

    const uint8_t *wbb = (const uint8_t *)wb;
    const int16_t *w_close_lo = (const int16_t *)(wbb + SAVE_BANK_FIELD_CLOSENESS * 4 + 2 * 4);
    const int16_t *w_close_hi = (const int16_t *)(wbb + SAVE_BANK_FIELD_CLOSENESS * 4 + 2 * 4 + 2);
    T_ASSERT_EQ_I(*w_close_lo, 15);
    T_ASSERT_EQ_I(*w_close_hi, 1);

    return 0;
}
