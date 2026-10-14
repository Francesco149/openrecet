/*
 * event_probe.c — see event_probe.h.  FUN_0045de68 (all_ida 61055-61130).
 */
#include "event_probe.h"

#include <stddef.h>

#include "save_bank.h"
#include "tables_event.h"

#define W_SCORE_TODAY   0xb0f8   /* dword 45304 */
#define W_LOOP          SAVE_BANK_FIELD_LOOP       /* 0xb0f9, dword 45305 */
#define W_DAY           SAVE_BANK_FIELD_CARD_DAY   /* 0xb0fb, dword 45307 */
#define W_PERIOD        SAVE_BANK_FIELD_CLOCK_TARGET /* 0xb0fc, dword 45308 */
#define B_FLAGS         0x2bc48  /* byte 179272: the event / story flag bytes */
#define B_EVENT_TODAY   0x2bca8  /* byte 179368 + cat */

static int flag_ok(const uint8_t *b, int32_t f)
{
    return f == -1 || b[B_FLAGS + f] != 0;
}

int event_probe(uint32_t *bank, int loc, int *grp, int *idx, int commit)
{
    int cat;
    switch (loc) {
    case 2: cat = 0; break;
    case 1: cat = 1; break;
    case 5: cat = 2; break;
    case 4: cat = 3; break;
    default: return 0;
    }
    if (bank == NULL || (int32_t)bank[SAVE_BANK_FIELD_GAME_MODE] == 2)
        return 0;
    uint8_t *b = (uint8_t *)bank;
    if (b[B_EVENT_TODAY + cat] != 0)
        return 0;
    for (int n = 0; n < EVENT_RECORDS_PER_CATEGORY; n++) {
        const event_record_t *r = &g_event.records[cat][n];
        if (r->id == -1)
            return 0;
        if ((int32_t)bank[W_LOOP] < r->loop_min || (int32_t)bank[SAVE_BANK_FIELD_GOLD] < r->decay_or_max
            || b[B_FLAGS + r->prereq[0]] != 0)
            continue;
        if (!flag_ok(b, r->prereq[1]) || !flag_ok(b, r->prereq[2]) || !flag_ok(b, r->prereq[3]))
            continue;
        const int32_t day = (int32_t)bank[W_DAY], per = (int32_t)bank[W_PERIOD];
        int hit = 0;
        for (int k = 0; k < EVENT_DAY_PAIRS && !hit; k++) {
            const int32_t s = r->day_pairs[k][0], e = r->day_pairs[k][1];
            hit = s != -1 && day >= s - 1 && (day <= e - 1 || e == EVENT_DAY_END_ALL)
                  && per >= r->time_first && per <= r->time_max;
        }
        if (!hit)
            continue;
        if (commit) {
            b[B_EVENT_TODAY + cat] = 1;
            b[B_FLAGS + r->prereq[0]] = 1;
            bank[W_SCORE_TODAY] += 100;
        }
        *grp = r->id;
        *idx = r->flag_on_trigger;
        return 1;
    }
    return 0;
}
