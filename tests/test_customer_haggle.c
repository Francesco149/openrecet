/*
 * test_customer_haggle.c — the SELL price-haggle math (customer_haggle.c).
 *
 * Verifies the deterministic evaluators exactly (budget, accept/reject) and the
 * rng-driven offer setup by INVARIANTS + the LCG-consumption count (the draw
 * count/order is load-bearing for RNG parity vs retail).  Uses the real
 * tutorial customer (kyaku 13 "Woman": init 120, random 3, gullibility 20,
 * rise1/2 10, budget 3000-300000) and base price 1600 (the trace's BARGAIN!!
 * base).
 */
#include "t.h"
#include "../src/customer_haggle.h"
#include "../src/rng.h"

/* kyaku 13 — the forced tutorial customer. */
static const haggle_customer_t KYAKU13 = {
    .initial = 120, .random = 3, .gullibility = 20,
    .rise1 = 10, .rise2 = 10, .budget_low = 3000, .budget_high = 300000,
};

int test_haggle_budget_ceiling(void)
{
    /* v = min(price/10, 10); ceiling = low + (high-low)*v/10. */
    /* price 1600 → v capped 10 → full high. */
    T_ASSERT_EQ_I(haggle_budget_ceiling(1600, 3000, 300000), 300000);
    /* price 50 → v=5 → midway. (300000-3000)*5/10 + 3000 = 148500+3000. */
    T_ASSERT_EQ_I(haggle_budget_ceiling(50, 3000, 300000), 151500);
    /* price 0 → v=0 → low. */
    T_ASSERT_EQ_I(haggle_budget_ceiling(0, 3000, 300000), 3000);
    /* price exactly 100 → v=10 → full. */
    T_ASSERT_EQ_I(haggle_budget_ceiling(100, 3000, 300000), 300000);
    return 0;
}

int test_haggle_decide_bands(void)
{
    /* accept_ref 1600: accept ≈[1592,1607] (±0.5%), counter ≈[1520,1679] (±5%).
     * Test CLEARLY-inside / -outside values only — the exact ULP edges (1607 vs
     * 1608, 1679 vs 1680) are x87-vs-SSE sensitive (1.005f/1.05f round down) and
     * are Frida's job to confirm against retail. */
    T_ASSERT_EQ_I(haggle_decide(1600, 1600), 1);   /* center → accept */
    T_ASSERT_EQ_I(haggle_decide(1595, 1600), 1);   /* inside accept */
    T_ASSERT_EQ_I(haggle_decide(1640, 1600), 2);   /* outside accept, inside counter */
    T_ASSERT_EQ_I(haggle_decide(1560, 1600), 2);   /* below accept, inside counter */
    T_ASSERT_EQ_I(haggle_decide(1700, 1600), 0);   /* above counter → reject */
    T_ASSERT_EQ_I(haggle_decide(1400, 1600), 0);   /* below counter → reject */
    return 0;
}

int test_haggle_decide_small_ref_collapses_accept(void)
{
    /* accept_ref < 110 collapses the accept band to the single point ×1.005. */
    /* ref 100: iVar1 = ftol(100*1.005)=100, iVar2:=iVar1=100 → accept only at 100. */
    T_ASSERT_EQ_I(haggle_decide(100, 100), 1);     /* the point → accept */
    T_ASSERT_EQ_I(haggle_decide(101, 100), 2);     /* off the point, in [95,105] → counter */
    T_ASSERT_EQ_I(haggle_decide(99, 100), 2);
    T_ASSERT_EQ_I(haggle_decide(106, 100), 0);     /* outside ×1.05 → reject */
    return 0;
}

int test_haggle_offer_up_round0_invariants_and_draws(void)
{
    /* trend 0, random>0: draws floor(1) + spread(1) + accept_ref(1) = 3 LCG steps. */
    rng_seed(12345);
    unsigned long before = rng_call_count();
    haggle_state_t st = {0};
    haggle_offer_up(&st, &KYAKU13, /*base*/1600, /*ask*/1600, /*trend*/0, /*tutorial*/0);
    T_ASSERT_EQ_U(rng_call_count() - before, 3);
    T_ASSERT_EQ_I(st.round, 1);
    T_ASSERT_EQ_I(st.work_price, 1600);                 /* trend 0 → no tilt */
    /* offer = 1600 · (120 ± 3) / 100 ∈ [1872, 1968] */
    T_ASSERT(st.offer >= 1872 && st.offer <= 1968);
    /* floor = (u + 2.0)·1600, u ∈ [0,1) → [3200, 4800) */
    T_ASSERT(st.floor >= 3200 && st.floor < 4800);
    /* accept_ref = (u·0.1 + 1.0)·1600 → [1600, 1760) */
    T_ASSERT(st.accept_ref >= 1600 && st.accept_ref < 1760);
    return 0;
}

int test_haggle_offer_up_round0_trend_and_norandom_draws(void)
{
    /* trend != 0 adds one tilt draw; random == 0 drops the spread draw. */
    haggle_customer_t c = KYAKU13;
    c.random = 0;
    rng_seed(999);
    unsigned long before = rng_call_count();
    haggle_state_t st = {0};
    haggle_offer_up(&st, &c, 1600, 1600, /*trend*/3, 0);
    /* tilt(1) + floor(1) + [no spread] + accept_ref(1) = 3 */
    T_ASSERT_EQ_U(rng_call_count() - before, 3);
    /* trend>=1 tilt: work_price = (u·0.5 + 2.0)·1600 ∈ [3200, 4000) */
    T_ASSERT(st.work_price >= 3200 && st.work_price < 4000);
    /* offer = work_price · 120/100 (no spread) */
    T_ASSERT_EQ_I(st.offer, (int32_t)((float)st.work_price * 120.0f / 100.0f));
    return 0;
}

int test_haggle_offer_up_round0_no_draws_when_random0_trend0(void)
{
    haggle_customer_t c = KYAKU13;
    c.random = 0;
    rng_seed(1);
    unsigned long before = rng_call_count();
    haggle_state_t st = {0};
    haggle_offer_up(&st, &c, 1600, 1600, 0, 0);
    /* floor(1) + accept_ref(1) = 2 (no tilt, no spread). */
    T_ASSERT_EQ_U(rng_call_count() - before, 2);
    /* offer is exact: 1600·120/100 = 1920. */
    T_ASSERT_EQ_I(st.offer, 1920);
    return 0;
}

int test_haggle_offer_up_tutorial_override(void)
{
    /* round >= 1 with is_tutorial: the offer is forced to work_price·1.5 LAST,
     * overwriting the rise/gullibility result. */
    rng_seed(42);
    haggle_state_t st = { .round = 1, .offer = 1000, .work_price = 1600 };
    haggle_offer_up(&st, &KYAKU13, 1600, 2000, 0, /*tutorial*/1);
    T_ASSERT_EQ_I(st.offer, (int32_t)((float)1600 * 1.5f));   /* = 2400 */
    T_ASSERT_EQ_I(st.round, 2);
    return 0;
}

int test_haggle_offer_down_round0_invariants(void)
{
    /* down round 0: offer seeded LOW = work_price·(165 - init_eff)/100. */
    rng_seed(7);
    unsigned long before = rng_call_count();
    haggle_state_t st = {0};
    int32_t ask = 1600;
    haggle_offer_down(&st, &KYAKU13, 1600, &ask, 0, 0, /*cust_index*/5);
    /* trend 0 → no tilt; floor(1) + spread(1) + accept_ref(1) = 3 draws. */
    T_ASSERT_EQ_U(rng_call_count() - before, 3);
    T_ASSERT_EQ_I(st.work_price, 1600);
    /* offer = 1600·(165 - (120±3))/100 = 1600·(42..48)/100 ∈ [672, 768] */
    T_ASSERT(st.offer >= 672 && st.offer <= 768);
    /* accept_ref = (u·0.1 + 0.65)·1600 → [1040, 1200) */
    T_ASSERT(st.accept_ref >= 1040 && st.accept_ref < 1200);
    return 0;
}

int test_haggle_offer_down_special_vendor_x5(void)
{
    /* cust_index 0x12 pre-scales work_price AND the player ask by 5.0. */
    rng_seed(7);
    haggle_state_t st = {0};
    int32_t ask = 200;
    haggle_offer_down(&st, &KYAKU13, 1600, &ask, 0, 0, /*cust_index*/0x12);
    T_ASSERT_EQ_I(ask, 1000);                          /* 200·5 */
    T_ASSERT_EQ_I(st.work_price, 8000);                /* 1600·5 */
    return 0;
}
