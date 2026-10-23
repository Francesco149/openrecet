/*
 * guild_fusion.c — see guild_fusion.h.
 */
#include "guild_fusion.h"

#include "save_bank.h"   /* SAVE_BANK_ITEM_TABLE_DWORD / _COUNT, SAVE_BANK_FIELD_ITEM_COUNT */

/* FUN_00469118: how many of item `id` the bag holds (0 for -1). */
int guild_count_item(const uint32_t *bank, int id)
{
    int n = 0;
    if (id == -1)
        return 0;
    const uint32_t *inv = bank + SAVE_BANK_ITEM_TABLE_DWORD;
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++)
        if (inv[i] != 0xFFFFFFFFu && id == (int32_t)inv[i] >> 6)
            n++;
    return n;
}

/* FUN_00469155: take `qty` of item `id` out of the bag, each time the slot
 * whose + (low nibble) equals `plus`, else the smallest above it, else the
 * largest below; `plus` drops by each taken item's + (floor 0).  Returns it. */
int guild_consume_item(uint32_t *bank, int id, int qty, int plus)
{
    uint32_t *inv = bank + SAVE_BANK_ITEM_TABLE_DWORD;
    if (id == -1 || qty == 0)
        return plus;
    for (int i = qty; i != 0; --i) {
        int lo_idx = -1, lo_val = -1, hi_idx = -1, hi_val = 99;
        int at = -1, take = 0;
        for (int k = 0; k < SAVE_BANK_ITEM_TABLE_COUNT; k++) {
            if (inv[k] == 0xFFFFFFFFu || (int32_t)inv[k] >> 6 != id)
                continue;
            const int q = (int)(inv[k] & 0xfu);
            if (q == plus) {
                at = k;
                take = plus;
                break;
            }
            if (q >= plus) {
                if (hi_val > q) { hi_val = q; hi_idx = k; }
            } else if (lo_val < q) {
                lo_val = q;
                lo_idx = k;
            }
        }
        if (at == -1) {
            at = hi_idx;
            take = hi_val;
            if (hi_idx == -1) {
                at = lo_idx;
                take = lo_val;
                if (lo_idx == -1)
                    continue;
            }
        }
        if (at >= 0) {
            plus -= take;
            if (plus < 0)
                plus = 0;
            for (int k = at; k < SAVE_BANK_ITEM_TABLE_COUNT - 1; k++)
                inv[k] = inv[k + 1];
            inv[SAVE_BANK_ITEM_TABLE_COUNT - 1] = 0xFFFFFFFFu;
            bank[SAVE_BANK_FIELD_ITEM_COUNT]--;
        }
    }
    return plus;
}
