/*
 * customer_haggle.h — the in-shop SELL price-haggle math (pure, host-testable).
 *
 * Ported 1:1 from the retail haggle evaluators, transcribed from the unpacked
 * disassembly (Ghidra dropped the x87 FPU stack, so docs/decompiled is NOT a
 * faithful source here — the by-address decompile shows bare __ftol()/
 * FUN_00471089() calls).  Every FP constant is decoded from the PE .rdata and
 * every rng draw is replicated in ORDER (the LCG-consumption count/order is
 * load-bearing for RNG parity vs retail — see [[feedback_verify_1to1_before_done]]).
 *
 * Engine functions ported here (all part of the cc08==4 customer-service mode):
 *   - FUN_0045ecc0 (0x45ecc0) — the customer's budget ceiling for an item.
 *   - FUN_00460672 (0x460672) — the accept / counter / reject decision.
 *   - FUN_00460161 (0x460161) — the customer's offer when haggling UP.
 *   - FUN_004603cf (0x4603cf) — the customer's offer when haggling DOWN.
 *
 * The offer functions MUTATE the per-customer haggle working state (the
 * DAT_0730bXXX block subset below) and READ the kyaku tuning fields + the
 * shared price scalars; they are modelled as pure functions over an explicit
 * state struct + customer struct so the FP + rng order can be unit-tested.
 * The engine-facing wrapper (src/customer_service.c, TBD) binds these to the
 * real DAT_0730bXXX / DAT_005c6bXX globals + the kyaku record.
 *
 * RNG: the engine draws via FUN_005041f6 (the MSVC LCG, = the port's
 * rng_next15()) and FUN_00471089 (= rng_next15()/32768 as a float, = the port's
 * rng_next_unit()).  These functions call those port primitives directly so a
 * real run stays bit-identical to retail's LCG stream; host tests seed the LCG
 * (rng_seed) to make the draws deterministic.
 *
 * Full RE + per-field disasm: docs/findings/customer-service-haggle-RE.md §4.
 */

#ifndef OPENRECET_CUSTOMER_HAGGLE_H
#define OPENRECET_CUSTOMER_HAGGLE_H

#include <stdint.h>

/* The kyaku tuning fields the haggle math reads (record-relative offsets noted;
 * record base &DAT_06a5ea90 + idx*0x2c670).  Mirrors the subset of
 * kyaku_record_t the evaluators consume. */
typedef struct {
    int32_t initial;      /* 初回 +0x51c8 — first-offer % of work price */
    int32_t random;       /* ランダム +0x51cc — ± spread on `initial` */
    int32_t gullibility;  /* 騙 +0x51bc — closes the gap to the player ask per round */
    int32_t rise1;        /* 上昇１ +0x51c0 — round-2 raise rate % */
    int32_t rise2;        /* 上昇２ +0x51c4 — other-round raise rate % */
    int32_t budget_low;   /* 予算 low +0x51d0 */
    int32_t budget_high;  /* 予算 high +0x51d4 */
} haggle_customer_t;

/* The mutable per-haggle working state (DAT_0730bXXX subset). */
typedef struct {
    int32_t round;        /* DAT_0730b584 — haggle round (0 = first offer) */
    int32_t offer;        /* DAT_0730b574 — customer's current offered price */
    int32_t work_price;   /* DAT_0730b57c — working price (seeded = base price) */
    int32_t floor;        /* DAT_0730b580 — haggle floor / give-up bound */
    int32_t accept_ref;   /* DAT_0730b588 — the accept-test reference price */
} haggle_state_t;

/* FUN_0045ecc0 — the customer's hard budget ceiling for an item.
 *   v = min(market_price / 10, 10)
 *   ceiling = budget_low + (budget_high - budget_low) * v / 10
 * `market_price` is the item's current market price (the int16 at save-bank
 * dword 0xb484 + slot; the caller reads it).  Pure integer (no FPU). */
int32_t haggle_budget_ceiling(int32_t market_price, int32_t budget_low, int32_t budget_high);

/* FUN_00460672 — accept / counter / reject for the player's named price.
 * `player_ask` = DAT_005c6bb8, `accept_ref` = DAT_0730b588.  Returns:
 *   1 = ACCEPT  (ask within ±0.5% of accept_ref — exact point if accept_ref<110)
 *   2 = COUNTER (ask within ±5% but outside the accept band)
 *   0 = REJECT  (outside ±5%)
 * Mixed float/double precision exactly mirrors the disasm. */
int32_t haggle_decide(int32_t player_ask, int32_t accept_ref);

/* FUN_00460161 — the customer raises its offer (haggle UP).  Mutates `st`.
 *   base = DAT_005c6bc0 (base/reference price), player_ask = DAT_005c6bb8,
 *   trend = FUN_004361b2 (price-trend level; live via cs_news_price_trend),
 *   is_tutorial = DAT_0450f406[slot] != 0 (forces offer = work_price*1.5).
 * Draws the LCG in the engine's exact order (round 0: [tilt if trend!=0],
 * floor, [spread if random>0], accept_ref; round>=1: [gullibility if >2]). */
void haggle_offer_up(haggle_state_t *st, const haggle_customer_t *c,
                     int32_t base, int32_t player_ask, int32_t trend, int is_tutorial);

/* FUN_004603cf — the customer lowers its offer (haggle DOWN).  Mutates `st`.
 * Mirror of haggle_offer_up; round 0 seeds the offer at the low end
 * (work_price*(165 - init_eff)/100) and accept_ref at ~0.65*work_price.
 * `cust_index` is DAT_0730b56c (the special vendor customer 0x12 pre-scales
 * work_price and *io_player_ask by 5.0; pass the real index + the ask ptr). */
void haggle_offer_down(haggle_state_t *st, const haggle_customer_t *c,
                       int32_t base, int32_t *io_player_ask, int32_t trend,
                       int is_tutorial, int32_t cust_index);

#endif /* OPENRECET_CUSTOMER_HAGGLE_H */
