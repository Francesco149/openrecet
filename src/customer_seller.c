/*
 * customer_seller.c — see customer_seller.h.  Transcribed from the disasm
 * 0x461792-0x461bf5 (the IDA view folds the three two-pass scans into
 * goto-loops; the asm is the spec).
 */
#include "customer_seller.h"
#include "rng.h"
#include "save_bank.h"
#include "tables_item.h"

/* save-bank byte offsets (rel. DAT_044e3798 + slot·0x2dfc8) */
#define SEL_F404_SELL_ACTIVE   0x2bc6c   /* scripted player-sell: bypasses every gate */
#define SEL_STORY_A            0x2bc4a   /* + kyaku: per-kyaku story bytes that buy one */
#define SEL_STORY_B            0x2bd16   /*   discarded rand for kyaku 2..9           */
#define SEL_STORY_C            0x2deae   /*   (C only outside game mode 2)            */

/* DAT_005c6c64[買 level] — closeness thresholds per item 買 (buy) level.  The 買
 * tag is one digit (tables_item.c); [6..9] run into the next .data table (the
 * story-event rows at 0x5c6c7c) and are mirrored as retail reads them. */
static const int32_t k_buy_thresh[10] = { 0, 0, 3, 10, 17, 22, 0x1b3, 2, 0x11, 3 };

#define ID_RELIC_5013 5013
#define ID_RELIC_5111 5111
#define ID_RELIC_5404 5404

static int item_id_at(int32_t slot)
{
    /* the kyaku≠18 no-candidate tail indexes by a stale handle: retail reads
     * past the table there.  Never reached (買(1) items pass any closeness). */
    if (slot < 0 || slot >= ITEM_MAX_RECORDS) return 0;
    return g_item.records[slot].item_id;
}

/* kyaku 18, rand&1==0: a 3000..3099 weapon whose ダ (dungeon) level band
 * matches the closeness band (0x4618d0-0x461992). */
static void pick_weapon(int32_t band, int32_t *b5a4)
{
    uint32_t pick = 0;                                   /* [esp+0x10] */
    for (int pass = 0; pass < 2; pass++) {
        uint32_t n = 0;
        for (int i = 0; i < g_item.count; i++) {
            const item_record_t *r = &g_item.records[i];
            if (r->valid != 1 || r->item_id < 3000 || r->item_id >= 3100) continue;
            int hit = (int8_t)r->stock_info[4] / 10 == band;
            if ((int8_t)r->stock_info[5] / 10 == band) hit = 1;
            if ((int8_t)r->stock_info[6] / 10 == band) hit = 1;
            if (!hit) continue;
            if (pass == 1 && n == pick) { *b5a4 = i; break; }
            n++;
        }
        if (pass == 0) {
            if (n == 0) return;
            pick = rng_next15() % n;
        }
    }
}

/* kyaku 18 relic scan: price > 1000, id in 5001-5050 / 5101-5150 / 5301-5350 /
 * 5401-5450 minus the three fixed relics, 市 (market) level == rank band
 * (0x461a8c-0x461b73). */
static void pick_relic(const uint32_t *bank32, int32_t *b5a4)
{
    uint32_t pick = 0;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t n = 0;
        for (int i = 0; i < g_item.count; i++) {
            const item_record_t *r = &g_item.records[i];
            if (r->valid != 1 || r->price <= 1000) continue;
            int32_t id = r->item_id;
            if (id == ID_RELIC_5111 || id == ID_RELIC_5013 || id == ID_RELIC_5404) continue;
            if (!((id >= 5001 && id < 5051) || (id >= 5101 && id < 5151) ||
                  (id >= 5301 && id < 5351) || (id >= 5401 && id < 5451))) continue;
            int32_t rank = (int32_t)bank32[SAVE_BANK_FIELD_SHOP_RANK];
            int32_t band = 2;
            if (rank >= 3) band = 3;
            if (rank >= 10) band = 4;
            if ((int8_t)r->stock_info[2] != band) continue;
            if (pass == 1 && n == pick) { *b5a4 = i; break; }
            n++;
        }
        if (pass == 0) {
            if (n == 0) return;
            pick = rng_next15() % n;
        }
    }
}

/* everyone but kyaku 18: price > 0 and DAT_005c6c64[買] ≤ level
 * (0x461b75-0x461bf1). */
static void pick_by_buy_level(int32_t level, int32_t *b5a4)
{
    uint32_t pick = 0;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t n = 0;
        for (int i = 0; i < g_item.count; i++) {
            const item_record_t *r = &g_item.records[i];
            if (r->valid != 1 || r->price <= 0) continue;
            int b = (int8_t)r->stock_info[3];
            if (b <= 0 || level < k_buy_thresh[b]) continue;
            if (pass == 1 && n == pick) { *b5a4 = i; break; }
            n++;
        }
        if (pass == 0) {
            if (n == 0) return;
            pick = rng_next15() % n;
        }
    }
}

int seller_select(const uint8_t *bank, int32_t owned_count, int32_t *entry,
                  seller_state_t *st)
{
    const uint32_t *bank32 = (const uint32_t *)bank;
    int f404 = bank[SEL_F404_SELL_ACTIVE] != 0;
    if (owned_count >= 15000 && !f404) return 0;
    if ((int32_t)bank32[SAVE_BANK_FIELD_GOLD] <= 1000 && !f404) return 0;
    if (st->b5ac >= 3 && !f404) return 0;

    int32_t k = entry[0];
    st->b56c = k;
    st->b570 = entry[1];
    if (k == -1) return 0;

    /* closeness (lo int16 of the per-customer dword) */
    int16_t close = *(const int16_t *)(bank + SAVE_BANK_FIELD_CLOSENESS * 4 + st->b570 * 4);
    int32_t level = close / 10 + 1;

    if (k >= 2 && k <= 9 &&
        (bank[SEL_STORY_A + k] != 0 || bank[SEL_STORY_B + k] != 0 || k == 2 ||
         (bank[SEL_STORY_C + k] != 0 && (int32_t)bank32[SAVE_BANK_FIELD_GAME_MODE] != 2)))
        (void)rng_next15();

    if (k != 18) {
        int32_t rank = (int32_t)bank32[SAVE_BANK_FIELD_SHOP_RANK];
        if (level >= rank) level = rank;
        pick_by_buy_level(level, &st->b5a4);
        st->b5a4 = item_id_at(st->b5a4) << 6;
    } else if ((rng_next15() & 1) == 0) {
        st->b5a4 = 1;
        int32_t band = 2;
        if (close >= 20) band = 4;
        else if (close >= 10) band = 3;
        pick_weapon(band, &st->b5a4);
        st->b5a4 = item_id_at(st->b5a4) << 6;
    } else {
        st->b5a4 = 1;
        uint32_t held = 0;                               /* bit0 5111, bit1 5013 */
        const int32_t *inv = (const int32_t *)bank32 + SAVE_BANK_ITEM_TABLE_DWORD;
        for (uint32_t c = bank32[SAVE_BANK_FIELD_ITEM_COUNT], j = 0; c != 0; c--, j++) {
            int32_t id = inv[j] >> 6;
            if (id == ID_RELIC_5111) held |= 1;
            if (id == ID_RELIC_5013) held |= 2;
        }
        int32_t wall = (int32_t)bank32[SAVE_BANK_FIELD_DECO_WALL];
        int32_t floor_ = (int32_t)bank32[SAVE_BANK_FIELD_DECO_FLOOR];
        if (wall == 10) held |= 1;
        int fixed = -1;                                  /* item id, or -1 = scan */
        if (floor_ == 12) held |= 2;                     /* (bit 1 is never read) */
        if (floor_ == 12 && wall == 10) {
            uint32_t r = rng_next15() % 6u;
            fixed = r == 0 ? ID_RELIC_5404 : r == 1 ? ID_RELIC_5013 : r == 2 ? ID_RELIC_5111 : -1;
        } else if (held & 1) {
            uint32_t r = rng_next15() & 3;
            fixed = r == 0 ? ID_RELIC_5013 : r == 1 ? ID_RELIC_5111 : -1;
        } else {
            fixed = (rng_next15() & 1) ? ID_RELIC_5111 : -1;
        }
        if (fixed >= 0) {
            st->b5a4 = fixed << 6;
        } else {
            pick_relic(bank32, &st->b5a4);
            st->b5a4 = item_id_at(st->b5a4) << 6;
        }
    }

    st->b5a8 = 0;
    st->b5ac += 1;
    if (!f404) entry[0] = -1;
    return 1;
}
