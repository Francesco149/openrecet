/*
 * item_use.c — see item_use.h.  Float code mirrors the retail disassembly
 * (x87.h): every fstp m32 is spill32, __ftol truncates the unrounded ST0.
 */
#include "item_use.h"

#include <string.h>

#include "chara_equip.h"   /* FUN_0048093f, DAT_0438b7d8 */
#include "dungeon_flow.h"  /* FUN_0043647f */
#include "party.h"         /* HP DAT_056db0bc, SP DAT_056db0c0 */
#include "rng.h"           /* FUN_00471084 */
#include "save_work.h"
#include "tables_item.h"   /* FUN_004681f6 */
#include "x87.h"

/* the active chara's record: work +0x2ceb0 + 0x6c·DAT_0438b7d8 */
static uint8_t *chara(void)
{
    uint8_t *w = save_work_bank_at(save_work_active_slot());
    return w ? w + 0x2ceb0 + 0x6c * chara_equip_get_current_chara() : NULL;
}
static int16_t i16(const uint8_t *c, int off)
{
    int16_t v = 0;
    if (c) memcpy(&v, c + off, 2);
    return v;
}

/* the stat sums of the item's base (quality bits cleared) */
static void stats(int32_t item, int32_t s[4])
{
    s[0] = s[1] = s[2] = s[3] = 0;
    chara_equip_item_stats((uint32_t)item & 0xfffffff0u, s);   /* FUN_0048093f */
}

int32_t item_hp_recovery(int32_t item)
{
    int32_t s[4];
    stats(item, s);
    const int32_t q = item & 0xf;
    if (q >= 1 && s[0] >= 1) {
        float b = spill32((long double)(s[0] * q / 5));
        if (b < 1.0f) b = 1.0f;
        s[0] = x87_ftol((long double)spill32((long double)s[0]) + b);
    }
    return s[0];
}

int32_t item_sp_recovery(int32_t item)
{
    int32_t s[4];
    stats(item, s);
    const int32_t q = item & 0xf;
    if (q >= 1 && s[1] >= 1) {
        int32_t b = s[1] * q / 5;
        if (b < 1) b = 1;
        s[1] += b;
    }
    return s[1];
}

int32_t item_use_food(int32_t item, int32_t *hp, int32_t *sp)
{
    const uint8_t *c = chara();
    const int32_t hp0 = x87_ftol(PW(0x56db0bcu).f), sp0 = x87_ftol(PW(0x56db0c0u).f);
    int32_t ret = 0;
    int32_t h = item_hp_recovery(item);
    if (h != 0) {
        if (h >= 40) h += (int32_t)(rng_next15() % 10u);
        else if (h >= 20) h += rng_next15() & 7;
        else h += rng_next15() & 3;
        if (floor_event_active(13)) h *= 2;
        if (floor_event_active(14)) h /= 2;
        ret = 1;
    }
    if (PW(0x56db0bcu).f > 0.0f)
        PW(0x56db0bcu).f = spill32((long double)spill32((long double)h) + PW(0x56db0bcu).f);
    if (PW(0x56db0bcu).f < 1.0f) PW(0x56db0bcu).f = 1.0f;
    const float hmax = spill32((long double)(i16(c, 0x3c) + i16(c, 0x3e)));
    if (PW(0x56db0bcu).f > hmax) PW(0x56db0bcu).f = hmax;
    *hp = x87_ftol((long double)PW(0x56db0bcu).f - spill32((long double)hp0));
    int32_t s = item_sp_recovery(item);
    if (s != 0) {
        if (s >= 20 && s < 40) s += (int32_t)(rng_next15() % 3u);
        else if (s >= 40) s += rng_next15() & 3;
        else s += rng_next15() & 1;
        if (floor_event_active(13)) s *= 2;
        if (floor_event_active(14)) s /= 2;
        ret |= 2;
    }
    PW(0x56db0c0u).f = spill32((long double)spill32((long double)s) + PW(0x56db0c0u).f);
    if (PW(0x56db0c0u).f < 0.0f) PW(0x56db0c0u).f = 0.0f;
    const float smax = spill32((long double)(i16(c, 0x40) + i16(c, 0x42)));
    if (PW(0x56db0c0u).f > smax) PW(0x56db0c0u).f = smax;
    *sp = x87_ftol((long double)PW(0x56db0c0u).f - spill32((long double)sp0));
    return ret;
}

int32_t item_use_medicine(int32_t item)
{
    uint8_t *c = chara();
    if (c == NULL) return 0;
    const int32_t before = i16(c, 0x3c) + i16(c, 0x3e);
    const int slot = tables_item_find_slot_by_id(&g_item, item >> 6);   /* FUN_004681f6 */
    const int32_t id = slot >= 0 ? g_item.records[slot].item_id : 0;
    if (id > 4107) {
        switch (id) {
        case 4108: PW(0x56db010u).i = 3600; return 0;
        case 4109: PW(0x56db014u).i = 3600; return 30;
        case 4110: PW(0x56db018u).i = 3600; return 30;
        case 4111: PW(0x56db01cu).i = 3600; return 30;
        default:   return 0;
        }
    }
    int32_t up = 10;
    switch (id) {
    case 4101: up = (int32_t)(rng_next15() & 1) + 2; break;
    case 4102: up = (int32_t)(rng_next15() & 1) + 3; break;
    case 4103: up = (int32_t)(rng_next15() & 1) + 4; break;
    case 4104: up = (int32_t)(rng_next15() % 3u) + 5; break;
    case 4105: up = (int32_t)(rng_next15() % 3u) + 6; break;
    case 4106: up = (int32_t)(rng_next15() % 3u) + 7; break;
    case 4107: up = (int32_t)(rng_next15() & 3) + 9; break;
    default:   break;
    }
    int16_t bonus = (int16_t)(i16(c, 0x3e) + up);
    if (bonus > 200) bonus = 200;
    memcpy(c + 0x3e, &bonus, 2);
    if (PW(0x56db0bcu).f > 0.0f)
        PW(0x56db0bcu).f = spill32((long double)spill32((long double)up) + PW(0x56db0bcu).f);
    const int32_t now = i16(c, 0x3e) + i16(c, 0x3c);
    const float hmax = spill32((long double)now);
    if (PW(0x56db0bcu).f > hmax) PW(0x56db0bcu).f = hmax;
    return now - before;
}

int item_category_is(int32_t item, const char *name)
{
    const int slot = tables_item_find_slot_by_id(&g_item, item >> 6);
    if (slot < 0) return 0;
    const int c = g_item.records[slot].category;
    if (c < 0 || c >= ITEM_CATEGORY_COUNT) return 0;
    return memcmp(g_item.categories[c].singular, name, 4) == 0;   /* FUN_00479f4d(.., 4) */
}
