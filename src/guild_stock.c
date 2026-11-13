/*
 * guild_stock.c — FUN_00490e56 (all_ida 103660).  See guild_stock.h.
 * Was `npc_schedule.c` (a Ghidra-era reading: the kinds were never loaded and
 * the counts lived outside the work bank).
 */
#include "guild_stock.h"

#include <stdint.h>
#include <string.h>

#include "retail_image.h" /* the exe's .data1 / .rsrc bytes the init pass overruns into */
#include "rng.h"          /* thunk_FUN_005041f6 */
#include "save_work.h"    /* DAT_044e3798 + DAT_0438b1e0 · 0x2dfc8 */
#include "tables_gousei.h" /* DAT_09640650 */
#include "encyclopedia.h" /* DAT_09643698 / 09646578 / 09646650 (FUN_0049f012) */
#include "tables_item.h"  /* DAT_095d37d0: stock_info[0] = DAT_095d3810 */

#define W_EVENT_2DDE0 (0x2dde0 / 4)   /* work +0x2dde0, zeroed by mode != 1 */

static int8_t kind_of(int i) { return (int8_t)g_item.records[i].stock_info[0]; }

/* the CRT's _mbctype entry for byte c under code page 932 (_SBUP 0x10, _SBLOW
 * 0x20, lead 0x04, trail 0x08; the kana / punctuation bits are left out — no
 * overrun index lands on them) */
static uint8_t mbctype_932(int c)
{
    uint8_t f = 0;
    if (c >= 'A' && c <= 'Z') f |= 0x10;
    if (c >= 'a' && c <= 'z') f |= 0x20;
    if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) f |= 0x04;
    if ((c >= 0x40 && c <= 0x7e) || (c >= 0x80 && c <= 0xfc)) f |= 0x08;
    return f;
}

/* the "kind" byte of record i >= 600: whatever retail holds at DAT_095d3810 +
 * 0x2cc·i (asm 0x490ff9 reads it) — the item category tags / names
 * (DAT_0963c5f8 / 0963e5f8), the fusion table (DAT_09640650), a DirectMusic
 * segment pointer's low byte (DAT_09643038[16]: 8-aligned, 0x30 unless rhost
 * passes the compared run's; 0 — one rand fewer — on the rare boot where the
 * heap lands it so, or when DirectMusic fails, as under wine), the CRT's
 * _mbctype (DAT_0964e5e1), then the exe's own .data1 and .rsrc bytes.
 * the Encyclopedia catalog arrays (0x9643698..0x964c410) as the port holds them.
 * PORT-DEBT(simplified, FUN_00490e56): the other runtime globals it crosses
 * (0x9642dac..0x964d...) are taken at their fresh-boot value 0. */
static int s_dmusic_ptr_lo = 0x30;
void guild_stock_set_dmusic_ptr_lo(int lo) { s_dmusic_ptr_lo = lo & 0xff; }

static int8_t kind_past(int i)
{
    const uint32_t va = 0x95d3810u + 0x2ccu * (uint32_t)i;
    uint8_t b = 0;
    if (va >= 0x963c5f8u && va < 0x963c5f8u + 0x2000u) {
        const uint32_t o = va - 0x963c5f8u;
        if (o / ITEM_CATEGORY_NAME_LEN < ITEM_CATEGORY_COUNT)
            b = (uint8_t)g_item.categories[o / ITEM_CATEGORY_NAME_LEN].tag[o % ITEM_CATEGORY_NAME_LEN];
    } else if (va >= 0x963e5f8u && va < 0x963e5f8u + 0x2000u) {
        const uint32_t o = va - 0x963e5f8u;
        if (o / ITEM_CATEGORY_NAME_LEN < ITEM_CATEGORY_COUNT)
            b = (uint8_t)g_item.categories[o / ITEM_CATEGORY_NAME_LEN].singular[o % ITEM_CATEGORY_NAME_LEN];
    } else if (va >= 0x9640650u && va < 0x9640650u + (uint32_t)sizeof g_gousei.records) {
        b = ((const uint8_t *)g_gousei.records)[va - 0x9640650u];
    } else if (va == 0x9643078u) {
        b = (uint8_t)s_dmusic_ptr_lo;
    } else if (va >= 0x9643698u && va < 0x9643698u + (uint32_t)sizeof g_enc_slot) {
        /* the Encyclopedia catalog (FUN_0049f012): -1-filled once built — a
         * bag item's discovery rebuilds it (FUN_0049126b), e.g. the Survival
         * Hell start's item returns just before its FUN_00490e56(0) */
        b = ((const uint8_t *)g_enc_slot)[va - 0x9643698u];
    } else if (va >= 0x9646578u && va < 0x9646578u + (uint32_t)sizeof g_enc_cat_key) {
        b = ((const uint8_t *)g_enc_cat_key)[va - 0x9646578u];
    } else if (va >= 0x9646650u && va < 0x9646650u + (uint32_t)sizeof g_enc_index) {
        b = ((const uint8_t *)g_enc_index)[va - 0x9646650u];
    } else if (va >= 0x964e5e2u && va < 0x964e5e2u + 256u) {
        b = mbctype_932((int)(va - 0x964e5e2u));
    } else if (va >= 0x964f000u) {
        (void)retail_image_byte(va, &b);
    }
    return (int8_t)b;
}

void guild_stock_daily(int mode)
{
    uint8_t *w = save_work_bank_at(save_work_active_slot());
    if (w == NULL) return;
    int16_t *st = (int16_t *)(void *)(w + GUILD_STOCK_OFF);   /* [2i] count, [2i+1] timer */
    int reinit = 0;
    if (mode == 1) {
        for (int i = 0; i < GUILD_STOCK_ITEMS; i++)
            if (kind_of(i) == 4 && st[2 * i] >= 2) { reinit = 1; break; }
    } else {
        ((uint32_t *)(void *)w)[W_EVENT_2DDE0] = 0;
    }
    if (st[0] == 0 || reinit) {
        st[0] = 1;
        for (int i = 0; i < GUILD_STOCK_INIT_ITEMS; i++) {
            const int8_t k = i < GUILD_STOCK_ITEMS ? kind_of(i) : kind_past(i);
            if (k == 0) {
                st[2 * i] = 100;
            } else if (k == 1) {
                st[2 * i] = (int16_t)(rng_next15() % 3u + 3);
            } else if (k == 2) {
                (void)rng_next15();
                st[2 * i] = 0;
            } else if (k == 4) {
                st[2 * i] = 0;
            } else {
                (void)rng_next15();
                st[2 * i] = 1;
            }
        }
        return;
    }
    if (mode != 0) return;
    for (int i = 0; i < GUILD_STOCK_ITEMS; i++) {
        int16_t *s = &st[2 * i], *c = &st[2 * i + 1];
        int need;
        switch ((uint32_t)(int32_t)kind_of(i)) {
        case 0:
            *s = 100;
            continue;
        case 1:
            if (++*c >= 5) {
                *c = 0;
                if (*s < 10) ++*s;
            }
            continue;
        case 2:
            *s = (int16_t)(rng_next15() % 3u);
            if (++*c >= i % 8 + 15) { *c = 0; *s = 1; }
            continue;
        case 4: {
            const int16_t v = (int16_t)(uint16_t)(rng_next15() % 3u - 1u);
            *s = v < 0 ? 0 : v;
            if (++*c >= i % 8 + 20) { *c = 0; *s = 1; }
            continue;
        }
        case 5: need = 1; break;
        case 6: need = 2; break;
        default: need = 3; break;
        }
        if (*s == 0 && ++*c >= need) { *s = 1; *c = 0; }
    }
}
