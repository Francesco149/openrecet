/*
 * customer_haggle.c — see customer_haggle.h.
 *
 * Transcribed from the unpacked disassembly (the Ghidra decompile dropped the
 * x87 stack and is NOT faithful for the FP — it even rendered the rng-driven
 * floor/accept-ref as deterministic).  Every FP constant is decoded from
 * .rdata; every LCG draw is replicated in the engine's exact order.
 *
 * Precision: the engine is x87 (mingw32 i686 builds the port x87 too, so a real
 * port run matches retail); the values are small integers scaled by simple
 * ratios and every result is __ftol-truncated, so the host SSE build agrees at
 * the integer level for the tested ranges.  Frida pure-function-diff is the
 * authoritative cross-check (docs/findings/customer-service-haggle-RE.md §4).
 */

#include "customer_haggle.h"
#include "rng.h"
#include "x87.h"

/* x87 precision: see x87.h (retail spill points mirrored; P1-verified
 * 2026-10-13 under c11 AND gnu11 — tools/oracle/p1_haggle_offers.c, p1_pure.c). */
#define ftol x87_ftol
static const float  K0_0 = 0.0f, K0_1 = 0.1f, K0_2 = 0.2f, K0_35 = 0.35f, K0_45 = 0.45f, K0_5 = 0.5f,
                    K0_65 = 0.65f, K0_03 = 0.03f, K0_05 = 0.05f, K1_0 = 1.0f, K1_5 = 1.5f,
                    K1_005 = 1.005f, K1_05 = 1.05f, K2_0 = 2.0f, K5_0 = 5.0f, K65_0 = 65.0f,
                    K100_0 = 100.0f;
static const double D0_995 = 0.995, D0_95 = 0.95;                        /* m64 constants */
/* FUN_00471089: rand()&0x7fff → fild/fstp m32 → fld → fdiv m32 32768.0 (exact k/32768) */
static inline long double unit(void) { return (long double)rng_next_unit(); }

/* FUN_0045ecc0 — budget ceiling (pure integer; no FPU). */
int32_t haggle_budget_ceiling(int32_t market_price, int32_t budget_low, int32_t budget_high)
{
    int32_t v = market_price / 10;
    if (v > 10) v = 10;
    return (budget_high - budget_low) * v / 10 + budget_low;
}

/* FUN_00460672 — accept(1) / counter(2) / reject(0).  @0x460672:
 *   fild ref; fstp m32 [-4]; fld; fmul m32 1.005 → ftol   (accept upper)
 *   fild ref; fstp m64 [-c]; fld; fmul m64 0.995 → ftol   (accept lower)
 *   fld m32 [-4]; fmul m32 1.05 → ftol                    (counter upper)
 *   fld m64 [-c]; fmul m64 0.95 → ftol                    (counter lower) */
int32_t haggle_decide(int32_t player_ask, int32_t accept_ref)
{
    const float  rf = spill32((long double)accept_ref);
    const double rd = spill64((long double)accept_ref);
    int32_t iVar1 = ftol((long double)rf * K1_005);         /* accept upper (×1.005) */
    int32_t iVar2 = ftol((long double)rd * D0_995);         /* accept lower (×0.995) */
    int32_t iVar3 = ftol((long double)rf * K1_05);          /* counter upper (×1.05) */
    int32_t iVar4 = ftol((long double)rd * D0_95);          /* counter lower (×0.95) */
    if (accept_ref < 0x6e)                                 /* ref < 110 → accept band collapses */
        iVar2 = iVar1;
    if ((iVar1 < player_ask) || (player_ask < iVar2)) {    /* outside accept band */
        if ((iVar3 < player_ask) || (player_ask < iVar4)) /* outside counter band */
            return 0;                                      /* REJECT */
        return 2;                                          /* COUNTER */
    }
    return 1;                                              /* ACCEPT */
}

/* The shared round-0 prologue (@0x460188 / @0x4603f6): work = base, then the
 * price-trend tilt  work = ftol((u·a + b) · spill32(work))  — the (u·a+b) chain
 * stays in ST0, work is spilled to m32 first.  ONE draw iff trend != 0. */
static void haggle_round0_tilt(haggle_state_t *st, int32_t base, int32_t trend)
{
    st->work_price = base;                                  /* b57c = base */
    if (trend >= 1)
        st->work_price = ftol((unit() * K0_5 + K2_0) * spill32((long double)st->work_price));
    else if (trend <= -2)
        st->work_price = ftol((unit() * K0_1 + K0_35) * spill32((long double)st->work_price));
    else if (trend == -1)
        st->work_price = ftol((unit() * K0_1 + K0_45) * spill32((long double)st->work_price));
    /* trend == 0: no tilt, no rng draw. */
}

/* initial ± (rand % (2·random+1) − random): fild initial; fstp m32 [-4];
 * if random > 0: fild r; fstp m32; fld; fadd m32 [-4]; fstp m32 [-4].
 * ONE draw iff random > 0 (unsigned div). */
static float haggle_init_eff(const haggle_customer_t *c)
{
    float init_eff = spill32((long double)c->initial);
    if (c->random > 0) {
        int32_t spread = (int32_t)(rng_next15() % (uint32_t)(2 * c->random + 1)) - c->random;
        init_eff = spill32((long double)spill32((long double)spread) + init_eff);
    }
    return init_eff;
}

/* gullibility multiplier: g>2 → (rand % (g/2)) + g/2 (one draw), else g. */
static int32_t haggle_gullibility(int32_t g)
{
    if (g > 2) {
        int32_t h = (g - (g >> 31)) >> 1;          /* = g/2 (round toward zero) */
        return (int32_t)(rng_next15() % (uint32_t)h) + h;
    }
    return g;
}

/* FUN_00460161 — the customer raises its offer.  Spill points per the disasm
 * (0x460161..0x4603cd); see haggle_round0_tilt for the round-0 prologue. */
void haggle_offer_up(haggle_state_t *st, const haggle_customer_t *c,
                     int32_t base, int32_t player_ask, int32_t trend, int is_tutorial)
{
    if (st->round != 0) {
        /* rate·work/100: both operands spilled m32, quotient spilled m32 (@0x4602bc). */
        float rate = spill32((long double)((st->round == 2) ? c->rise1 : c->rise2));
        float work = spill32((long double)st->work_price);
        st->offer += ftol(spill32((long double)rate * work / K100_0));
        int32_t g_eff = haggle_gullibility(c->gullibility);
        /* step = spill32(spill32((ask−offer)·g) / 100); limit = spill32(spill32(base)·0.5) */
        float step  = spill32((long double)spill32((long double)((player_ask - st->offer) * g_eff)) / K100_0);
        float limit = spill32((long double)spill32((long double)base) * K0_5);
        if (step > limit) step = limit;            /* fcomp m32; jbe */
        if (step > K0_0)
            st->offer += ftol(step);
        if (is_tutorial)                           /* byte bank+0x450f406: work·1.5 (ST0) */
            st->offer = ftol((long double)spill32((long double)st->work_price) * K1_5);
        st->round += 1;
        return;
    }
    /* round 0. */
    haggle_round0_tilt(st, base, trend);
    st->floor = ftol((unit() + K2_0) * spill32((long double)st->work_price));
    float init_eff = haggle_init_eff(c);
    st->offer = ftol((long double)spill32((long double)st->work_price) * init_eff / K100_0);
    st->accept_ref = ftol((unit() * K0_1 + K1_0) * spill32((long double)st->work_price));
    st->round += 1;
}

/* FUN_004603cf — the customer lowers its offer (mirror of offer_up). */
void haggle_offer_down(haggle_state_t *st, const haggle_customer_t *c,
                       int32_t base, int32_t *io_player_ask, int32_t trend,
                       int is_tutorial, int32_t cust_index)
{
    (void)is_tutorial;   /* the down path has no f406 override */
    if (st->round != 0) {
        /* extra decrement: ftol(spill32((u·0.03 + 0.05) · spill32(base))) — the
         * product IS spilled here (@0x46059c fstp/fld m32), unlike the round-0 chains */
        st->offer -= ftol(spill32((unit() * K0_03 + K0_05) * spill32((long double)base)));
        float rate = spill32((long double)((st->round == 2) ? c->rise1 : c->rise2));
        float work = spill32((long double)st->work_price);
        st->offer -= ftol(spill32((long double)rate * work / K100_0));
        int32_t g_eff = haggle_gullibility(c->gullibility);
        float step = spill32((long double)spill32((long double)((*io_player_ask - st->offer) * g_eff)) / K100_0);
        if (step < K0_0)                            /* only move down (no clamp) */
            st->offer += ftol(step);
        st->round += 1;
        return;
    }
    /* round 0. */
    haggle_round0_tilt(st, base, trend);
    if (cust_index == 0x12) {                       /* special vendor customer ×5 (ST0 product) */
        st->work_price = ftol((long double)spill32((long double)st->work_price) * K5_0);
        *io_player_ask = ftol((long double)spill32((long double)*io_player_ask) * K5_0);
    }
    st->floor = ftol((unit() * K0_1 + K0_2) * spill32((long double)st->work_price));
    float init_eff = haggle_init_eff(c);
    /* factor = spill32(65 − (init_eff − 100)); offer = ftol(spill32(work)·factor/100) */
    float factor = spill32(K65_0 - ((long double)init_eff - K100_0));
    st->offer = ftol((long double)spill32((long double)st->work_price) * factor / K100_0);
    st->accept_ref = ftol((unit() * K0_1 + K0_65) * spill32((long double)st->work_price));
    st->round += 1;
}
