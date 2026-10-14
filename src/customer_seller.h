/*
 * customer_seller.h — FUN_00461792, the kind-1 customer-queue entry: a customer
 * SELLS the player an item (the buy side of the shop counter, b5a8 = 0).
 *
 * Run by the queue advance (FUN_00462403 b524==0x3c) for a kind-1 entry, for
 * kind 0 never.  Gates (unless the scripted-sell flag f404 is set): ≥15000 items
 * held, ≤1000 gold, or ≥3 sellers today → no seller.  Otherwise binds the
 * customer (b56c/b570) and draws the offered item:
 *   • kyaku 2..9 with a story flag: one discarded rand first;
 *   • kyaku 18 (the adventurer seller): rand&1 → a 3000..3099 weapon whose
 *     dungeon level band (ダ bytes /10) matches the closeness band, or a
 *     5xxx relic (fixed 5404/5013/5111 by deco + inventory, else a market-
 *     level-banded scan);
 *   • everyone else: an item whose 買 (buy) level threshold fits
 *     min(closeness/10+1, shop rank).
 * Every scan is the engine's two-pass count → rand%n → pick shape.
 *
 * Pure C over the save bank + g_item + the LCG (rhost-shadowable:
 * tools/rhost/rh_hooks.c `seller_select`).
 */
#ifndef OPENRECET_CUSTOMER_SELLER_H
#define OPENRECET_CUSTOMER_SELLER_H

#include <stdint.h>

/* The engine globals FUN_00461792 reads/writes (DAT_0730b5xx). */
typedef struct {
    int32_t b56c;   /* DAT_0730b56c — customer kyaku (out) */
    int32_t b570;   /* DAT_0730b570 — closeness / candidate index (out) */
    int32_t b5a4;   /* DAT_0730b5a4 — offered item handle (id<<6).  IN too: the
                     * kyaku≠18 scan leaves it unset when nothing qualifies, and
                     * the tail then reads it as an item SLOT (retail quirk). */
    int32_t b5a8;   /* DAT_0730b5a8 — transaction kind (out: 0 = customer sells) */
    int32_t b5ac;   /* DAT_0730b5ac — sellers served today (in/out) */
} seller_state_t;

/* FUN_00461792.  `bank` = active working-bank base (byte pointer);
 * `owned_count` = FUN_00491b16 (items held); `entry` = the queue entry
 * DAT_0730aca0[6·DAT_0730b1a8[b318]] (its kyaku is set -1 once served unless
 * f404).  Returns 1 if a seller was set up, 0 if refused. */
int seller_select(const uint8_t *bank, int32_t owned_count, int32_t *entry,
                  seller_state_t *st);

#endif /* OPENRECET_CUSTOMER_SELLER_H */
