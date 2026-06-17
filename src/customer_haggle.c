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

/* __ftol — x87 truncate-toward-zero to int32 (= C cast for finite values). */
static inline int32_t ftol_f(float v)   { return (int32_t)v; }
static inline int32_t ftol_d(double v)  { return (int32_t)v; }

/* FUN_0045ecc0 — budget ceiling (pure integer; no FPU). */
int32_t haggle_budget_ceiling(int32_t market_price, int32_t budget_low, int32_t budget_high)
{
    int32_t v = market_price / 10;
    if (v > 10) v = 10;
    return (budget_high - budget_low) * v / 10 + budget_low;
}

/* FUN_00460672 — accept(1) / counter(2) / reject(0).  Mirrors the disasm's
 * mixed precision: the ×1.005 / ×1.05 bounds are FLOAT (DWORD operands), the
 * ×0.995 / ×0.95 bounds are DOUBLE (QWORD operands). */
int32_t haggle_decide(int32_t player_ask, int32_t accept_ref)
{
    int32_t iVar1 = ftol_f((float)accept_ref  * 1.005f);   /* accept upper (×1.005) */
    int32_t iVar2 = ftol_d((double)accept_ref * 0.995);    /* accept lower (×0.995) */
    int32_t iVar3 = ftol_f((float)accept_ref  * 1.05f);    /* counter upper (×1.05) */
    int32_t iVar4 = ftol_d((double)accept_ref * 0.95);     /* counter lower (×0.95) */
    if (accept_ref < 0x6e)                                 /* ref < 110 → accept band collapses */
        iVar2 = iVar1;
    if ((iVar1 < player_ask) || (player_ask < iVar2)) {    /* outside accept band */
        if ((iVar3 < player_ask) || (player_ask < iVar4)) /* outside counter band */
            return 0;                                      /* REJECT */
        return 2;                                          /* COUNTER */
    }
    return 1;                                              /* ACCEPT */
}

/* The shared round-0 prologue: seed work_price from base + the price-trend tilt.
 * Draws ONE rng_unit iff trend != 0 (the LCG-consumption gate is load-bearing). */
static void haggle_round0_tilt(haggle_state_t *st, int32_t base, int32_t trend)
{
    st->work_price = base;                                  /* b57c = base */
    if (trend >= 1) {
        float t = rng_next_unit() * 0.5f + 2.0f;            /* (u·0.5 + 2.0) */
        st->work_price = ftol_f(t * (float)st->work_price);
    } else if (trend <= -2) {
        float t = rng_next_unit() * 0.1f + 0.35f;
        st->work_price = ftol_f(t * (float)st->work_price);
    } else if (trend == -1) {
        float t = rng_next_unit() * 0.1f + 0.45f;
        st->work_price = ftol_f(t * (float)st->work_price);
    }
    /* trend == 0: no tilt, no rng draw. */
}

/* `initial ± (rng % (2·random+1) − random)` — draws ONE rng iff random > 0. */
static float haggle_init_eff(const haggle_customer_t *c)
{
    float init_eff = (float)c->initial;
    if (c->random > 0) {
        int32_t spread = (int32_t)(rng_next15() % (uint32_t)(2 * c->random + 1)) - c->random;
        init_eff = init_eff + (float)spread;
    }
    return init_eff;
}

/* gullibility multiplier: g>2 → (rng % (g/2)) + g/2 (draws one rng), else g. */
static int32_t haggle_gullibility(int32_t g)
{
    if (g > 2) {
        int32_t h = (g - (g >> 31)) >> 1;          /* = g/2 (round toward zero) */
        return (int32_t)(rng_next15() % (uint32_t)h) + h;
    }
    return g;
}

/* FUN_00460161 — the customer raises its offer. */
void haggle_offer_up(haggle_state_t *st, const haggle_customer_t *c,
                     int32_t base, int32_t player_ask, int32_t trend, int is_tutorial)
{
    if (st->round != 0) {
        /* round >= 1: raise by the per-round rate, then a gullibility step. */
        float rate = (float)((st->round == 2) ? c->rise1 : c->rise2);
        st->offer += ftol_f(rate * (float)st->work_price / 100.0f);
        int32_t g_eff = haggle_gullibility(c->gullibility);
        float step = (float)((player_ask - st->offer) * g_eff) / 100.0f;
        float limit = (float)base * 0.5f;
        if (step > limit) step = limit;            /* clamp to base·0.5 */
        if (step > 0.0f)
            st->offer += ftol_f(step);
        if (is_tutorial)
            st->offer = ftol_f((float)st->work_price * 1.5f);
        st->round += 1;
        return;
    }
    /* round 0. */
    haggle_round0_tilt(st, base, trend);
    st->floor = ftol_f((rng_next_unit() + 2.0f) * (float)st->work_price);
    float init_eff = haggle_init_eff(c);
    st->offer = ftol_f((float)st->work_price * init_eff / 100.0f);
    st->accept_ref = ftol_f((rng_next_unit() * 0.1f + 1.0f) * (float)st->work_price);
    st->round += 1;
}

/* FUN_004603cf — the customer lowers its offer (mirror of offer_up). */
void haggle_offer_down(haggle_state_t *st, const haggle_customer_t *c,
                       int32_t base, int32_t *io_player_ask, int32_t trend,
                       int is_tutorial, int32_t cust_index)
{
    (void)is_tutorial;   /* the down path has no f406 override */
    if (st->round != 0) {
        /* round >= 1: an extra random decrement, the per-round rate, then a
         * (negative-only) gullibility step toward the lower ask. */
        st->offer -= ftol_f((rng_next_unit() * 0.03f + 0.05f) * (float)base);
        float rate = (float)((st->round == 2) ? c->rise1 : c->rise2);
        st->offer -= ftol_f(rate * (float)st->work_price / 100.0f);
        int32_t g_eff = haggle_gullibility(c->gullibility);
        float step = (float)((*io_player_ask - st->offer) * g_eff) / 100.0f;
        if (step < 0.0f)                            /* only move down (no clamp) */
            st->offer += ftol_f(step);
        st->round += 1;
        return;
    }
    /* round 0. */
    haggle_round0_tilt(st, base, trend);
    if (cust_index == 0x12) {                       /* special vendor customer ×5 */
        st->work_price = ftol_f((float)st->work_price * 5.0f);
        *io_player_ask = ftol_f((float)*io_player_ask * 5.0f);
    }
    st->floor = ftol_f((rng_next_unit() * 0.1f + 0.2f) * (float)st->work_price);
    float init_eff = haggle_init_eff(c);
    /* offer = work_price · (65.0 − (init_eff − 100.0)) / 100  = ·(165 − init_eff)/100 */
    float factor = 65.0f - (init_eff - 100.0f);
    st->offer = ftol_f((float)st->work_price * factor / 100.0f);
    st->accept_ref = ftol_f((rng_next_unit() * 0.1f + 0.65f) * (float)st->work_price);
    st->round += 1;
}
