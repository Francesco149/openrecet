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
