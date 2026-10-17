/*
 * guild_stock.c — FUN_00490e56 (all_ida 103660).  See guild_stock.h.
 * Was `npc_schedule.c` (a Ghidra-era reading: the kinds were never loaded and
 * the counts lived outside the work bank).
 */
#include "guild_stock.h"

#include <stdint.h>
#include <string.h>

#include "rng.h"          /* thunk_FUN_005041f6 */
#include "save_work.h"    /* DAT_044e3798 + DAT_0438b1e0 · 0x2dfc8 */
#include "tables_item.h"  /* DAT_095d37d0: stock_info[0] = DAT_095d3810 */

#define W_EVENT_2DDE0 (0x2dde0 / 4)   /* work +0x2dde0, zeroed by mode != 1 */

static int8_t kind_of(int i) { return (int8_t)g_item.records[i].stock_info[0]; }

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
        for (int i = 0; i < GUILD_STOCK_ITEMS; i++) {
            const int8_t k = kind_of(i);
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
