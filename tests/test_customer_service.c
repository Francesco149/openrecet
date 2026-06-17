/*
 * test_customer_service.c — the cc08==4 customer-service ENTRY (FUN_0045edaa,
 * tutorial forced-sale path).  Verifies the session-init STATE setup + the
 * load-bearing single RNG draw (the customer-count draw is consumed on every
 * path; its order/count must match retail for LCG parity).
 */
#include "t.h"
#include "../src/customer_service.h"
#include "../src/save_work.h"
#include "../src/save_bank.h"
#include "../src/tables_item.h"
#include "../src/rng.h"

/* DAT_0450f406 / f404 byte offsets within a slot bank (rel. save_work_dwords_at). */
#define F406_TUTORIAL_BYTE_OFF 0x2bc6e
#define F404_SELL_ACTIVE_BYTE_OFF 0x2bc6c

/* Clear the active slot bank + fill the 15×20 display grid with -1 (empty). */
static uint32_t *cs_test_bank_clean(void)
{
    save_work_set_active_slot(0);
    uint32_t *bank = save_work_dwords_at(0);
    uint8_t *bb = (uint8_t *)bank;
    bb[F406_TUTORIAL_BYTE_OFF] = 0;
    bb[F404_SELL_ACTIVE_BYTE_OFF] = 0;
    for (int i = 0; i < 15 * 20; i++)
        bank[SAVE_BANK_FIELD_DISPLAY_GRID + i] = 0xffffffffu;  /* empty cell */
    return bank;
}

/* The tutorial forced-sale path (f406 != 0) queues exactly one customer = kyaku
 * 13 ("Woman"), item slot 0, kind 0; seeds the eligible list {13, -2}; and
 * leaves the machine idle (b534=0, b5a8=-1). */
int test_cs_session_init_tutorial_queue(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F406_TUTORIAL_BYTE_OFF] = 1;   /* arm the tutorial sale */
    rng_seed(0x1234);

    customer_service_session_init();

    T_ASSERT_EQ_I(customer_service_queue_kyaku(0), 0xd);      /* kyaku 13 */
    T_ASSERT_EQ_I(customer_service_queue_item_slot(0), 0);
    T_ASSERT_EQ_I(customer_service_queue_kind(0), 0);
    T_ASSERT_EQ_I(customer_service_queue_count(), 1);
    T_ASSERT_EQ_I(customer_service_eligible(0), 0xd);
    T_ASSERT_EQ_I(customer_service_eligible(1), -2);
    T_ASSERT_EQ_I(customer_service_eligible(2), -1);          /* terminator+rest */
    T_ASSERT_EQ_I(customer_service_b534(), 0);               /* idle */
    return 0;
}

/* The customer-count RNG draw (all.c:57455) happens BEFORE the path branch, so
 * the entry consumes EXACTLY one LCG step on the tutorial path — load-bearing
 * for the shared RNG stream staying aligned with retail. */
int test_cs_session_init_draws_exactly_one_rng(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F406_TUTORIAL_BYTE_OFF] = 1;
    rng_seed(0x1234);

    unsigned long before = rng_call_count();
    customer_service_session_init();
    T_ASSERT_EQ_U(rng_call_count() - before, 1);
    return 0;
}

/* A second session_init (re-entry) must rebuild the same queue from scratch (the
 * entry zeroes its state) — a stale customer must not bleed in. */
int test_cs_session_init_reentry_rebuilds(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F406_TUTORIAL_BYTE_OFF] = 1;
    rng_seed(0x99);
    customer_service_session_init();
    customer_service_session_init();   /* re-enter */
    T_ASSERT_EQ_I(customer_service_queue_kyaku(0), 0xd);
    T_ASSERT_EQ_I(customer_service_eligible(0), 0xd);
    T_ASSERT_EQ_I(customer_service_eligible(1), -2);
    return 0;
}

/* The sell-active / scripted-sell path (f404 != 0) — the path the customer-service
 * TUTORIAL takes (RE doc §3.7): a 3-deep queue of placeholder customers (kyaku=1,
 * kind=0; item_slot left = entry index), count=3.  b56c will resolve to queue[0].
 * kyaku=1 in the master tick (matching the BIT-EXACT capture's b56c=1). */
int test_cs_session_init_sell_active_queue(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;   /* arm the sell */
    rng_seed(0x1234);

    customer_service_session_init();

    T_ASSERT_EQ_I(customer_service_queue_kyaku(0), 1);
    T_ASSERT_EQ_I(customer_service_queue_kyaku(1), 1);
    T_ASSERT_EQ_I(customer_service_queue_kyaku(2), 1);
    T_ASSERT_EQ_I(customer_service_queue_kind(0), 0);
    T_ASSERT_EQ_I(customer_service_queue_count(), 3);
    T_ASSERT_EQ_I(customer_service_queue_item_slot(0), 0);  /* left = entry idx */
    T_ASSERT_EQ_I(customer_service_queue_item_slot(1), 1);
    T_ASSERT_EQ_I(customer_service_b534(), 0);              /* idle */
    return 0;
}

/* Drive the master tick through the idle → greeting on the sell path.  The kind
 * selector (b524==0x3c) sets b5a8=2, b56c=queue[0].kyaku(=1), and the offered
 * handle b5a4=0xc0 (item id 3).  The greeting trigger (b524>0x77 & b52c>=0x20)
 * promotes b534 0→1 and computes base = item.price·count(=1) and
 * ask = ftol((float)item.price) [b5a8==2].  Mirrors the BIT-EXACT capture's
 * greeting frame: b56c=1, b5a8=2, base=ask=3000 (item id 3 → price 3000), b534=1. */
int test_cs_master_tick_sell_trajectory(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);

    /* item DB: id 3 → price 3000 (the f404 selector points b5a4=0xc0 at id 3).
     * Borrow + restore the shared g_item slot 0 so other tests are unaffected. */
    int32_t save_count = g_item.count;
    int32_t save_id    = g_item.records[0].item_id;
    int32_t save_price = g_item.records[0].price;
    g_item.count = 1;
    g_item.records[0].item_id = 3;
    g_item.records[0].price   = 3000;

    customer_service_session_init();
    customer_service_notify_loaded();          /* release the asset-load gate */

    for (int i = 0; i < 200; i++)
        customer_service_master_tick(0, 0);    /* no input → idle climbs to greeting */

    int b5a8 = customer_service_b5a8();
    int b56c = customer_service_b56c();
    int b534 = customer_service_b534();
    int base = customer_service_base_price();
    int ask  = customer_service_player_ask();

    g_item.count = save_count;                 /* restore shared state */
    g_item.records[0].item_id = save_id;
    g_item.records[0].price   = save_price;

    T_ASSERT_EQ_I(b5a8, 2);                    /* kind selector ran */
    T_ASSERT_EQ_I(b56c, 1);                    /* queue[0].kyaku */
    T_ASSERT_EQ_I(b534, 1);                    /* greeting */
    T_ASSERT_EQ_I(base, 3000);                 /* item.price · 1 */
    T_ASSERT_EQ_I(ask, 3000);                  /* ftol((float)item.price) */
    return 0;
}

/* The master tick is inert while the asset-load worker is running (b1cc==2, set by
 * session_init); the idle only advances once the load callback (notify_loaded)
 * fires — matching the engine's `if (DAT_0438b1cc == 2) return;` gate. */
int test_cs_master_tick_idle_gated_by_load(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);
    customer_service_session_init();           /* sets b1cc=2 (loading) */

    for (int i = 0; i < 200; i++)
        customer_service_master_tick(0, 0);
    T_ASSERT_EQ_I(customer_service_b534(), 0);  /* still idle — load not done */
    T_ASSERT_EQ_I(customer_service_b5a8(), -1); /* selector never ran */

    customer_service_notify_loaded();
    for (int i = 0; i < 200; i++)
        customer_service_master_tick(0, 0);
    T_ASSERT_EQ_I(customer_service_b534(), 1);  /* now reaches the greeting */
    T_ASSERT_EQ_I(customer_service_b5a8(), 2);
    return 0;
}
