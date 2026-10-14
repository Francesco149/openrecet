/*
 * actor_table.c — see actor_table.h.
 */
#include "actor_table.h"

#include "rng.h"
#include "tables_enemy.h"   /* g_enemy[type].hp (DAT_005c241c) */
#include "x87.h"

actor_t g_actor[ACTOR_COUNT];
int32_t g_actor_alloc_n;

void actor_anim_set(actor_t *a, int32_t anim)
{
    if (act_geti(a, 0x14) != anim) {
        act_seti(a, 0x10, 0);
        act_seti(a, 0x0c, 0);
        act_seti(a, 0x14, anim);
        act_seti(a, 0x00, anim);
        act_setf(a, 0x08, 0.0f);
    }
}

int32_t actor_enemy_level(const actor_stage_t *st, int32_t type)
{
    static const float k1p7 = 1.7f, k50 = 50.0f;
    int32_t lv = 0;
    if (st->event_dungeon < 0) {
        const int32_t f = st->floor;
        switch (st->dungeon) {
        case 0: lv = f; break;
        case 1: lv = f / 2 + 2; break;
        case 2: lv = f / 3 + 6; break;
        case 3: lv = f / 4 + 16; break;
        case 4: lv = f / 5 + 25; break;
        case 5: {
            const float v = (float)(f < 30 ? f : f - 30);
            lv = x87_ftol((long double)v * k1p7 + k50);
            break;
        }
        default: break;
        }
    } else {
        const int32_t f = 5 * st->floor - 145;
        switch (st->event_dungeon) {
        case 1: lv = f / 2 + 2; break;
        case 2: lv = f / 3 + 6; break;
        case 3: lv = f / 4 + 16; break;
        case 4: lv = f / 5 + 25; break;
        default: break;
        }
    }
    if (type == 34) {
        const int32_t lo = lv + 10, hi = 2 * lv;
        lv += st->kills / 5;
        if (lo <= hi) { if (lv > hi) return hi; }
        else if (lv > lo) return lo;
    }
    return lv;
}

actor_t *actor_alloc(const actor_stage_t *st)
{
    actor_t *a = NULL;
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *c = &g_actor[i];
        if (act_geti(c, ACT_INUSE) != 0 || act_geti(c, ACT_BUSY) != 0) continue;
        int32_t cd = act_geti(c, ACT_COOLDOWN);
        if (cd > 0) act_seti(c, ACT_COOLDOWN, --cd);
        if (cd <= 0) { a = c; act_seti(a, ACT_SLOT, i); break; }
    }
    if (!a) return NULL;
    g_actor_alloc_n++;
    memset(a->b + 0xb5c, 0xff, 0x28);
    static const uint16_t zero[] = {
        0xb84, 0xb2c, 0x9f8, 0xb18, 0x9f4, 0x9fc, 0xac8, 0xacc, 0x420, 0x3fc, 0x400, 0x404, 0x408,
        0x40c, 0x410, 0xb90, 0xb94, 0xb98, 0xb9c, 0xba0, 0xb40, 0xb44, 0xb54, 0xb38, 0xb28, 0xa00,
        0x448, 0xb20, 0xb24, 0xab4, 0xa4c, 0x01c, 0x028, 0x020, 0x024, 0x440, 0x444, 0x44c, 0xadc,
        0xad4, 0xae8, 0xaa8, 0xaac, 0xab0, 0xaf4, 0xaf8, 0xa54, 0xa58, 0xa5c, 0xa60, 0xa64, 0xa68,
        0xa6c, 0xaf0, 0xb04, 0xb08, 0xb0c, 0x398, 0xb10, 0xb14, 0x02c, 0x030, 0x394, 0xa98, 0xaec,
        0xae4,
    };
    for (size_t k = 0; k < sizeof zero / sizeof zero[0]; k++) act_seti(a, zero[k], 0);
    act_seti(a, 0xb8c, 1);
    act_setf(a, 0x3e0, -1000.0f);
    act_setf(a, 0xac0, 1.0f);
    act_setf(a, 0xac4, 1.0f);
    memset(a->b + 0xa70, 0, 0x28);
    act_seti(a, 0xb30, -1);
    act_seti(a, 0xb34, -1);
    act_setf(a, 0xabc, 1.0f);
    act_setf(a, 0xab8, 1.0f);
    act_seti(a, 0xad8, -1);
    act_seti(a, 0x3dc, 255);
    act_seti(a, 0xb1c, -1);
    memset(a->b + 0x034, 0, 0x40);    /* two 8-float blocks +0x34 / +0x54 */
    memset(a->b + 0x074, 0, 0x320);   /* two 100-dword blocks +0x74 / +0x204 */
    act_seti(a, 0x014, -1);
    act_seti(a, 0xa04, -1);
    act_seti(a, ACT_LEVEL, actor_enemy_level(st, 0));
    act_seti(a, 0x018, 1);
    actor_anim_set(a, 0);
    return a;
}

actor_t *actor_spawn_enemy(const actor_stage_t *st, float x, float y, float z, int32_t type)
{
    static const float k0p2 = 0.2f, k1 = 1.0f, k0p1 = 0.1f, kpi = 3.1415927f;
    actor_t *a = actor_alloc(st);
    if (!a) return NULL;
    (void)rng_next_unit();                  /* FUN_00471089, result dropped (fstp st0) */
    act_seti(a, ACT_INUSE, 1);
    act_seti(a, 0xa4c, 0);
    act_seti(a, 0xafc, 0);
    act_seti(a, ACT_PHASE_RND, rng_next15() & 0x3f);
    act_setf(a, ACT_POS + 0, x);
    act_setf(a, ACT_POS + 4, y);
    act_seti(a, ACT_TYPE, type);
    act_setf(a, ACT_POS + 8, z);
    act_setf(a, ACT_HOME + 0, x);
    act_setf(a, ACT_HOME + 4, y);
    act_setf(a, ACT_HOME + 8, z);
    const int32_t lv = actor_enemy_level(st, type);
    act_seti(a, ACT_LEVEL, lv);
    const float scale = spill32((long double)spill32((long double)lv) * k0p2 + k1);
    float hp = spill32((long double)spill32((long double)g_enemy[type].hp) * scale);
    act_setf(a, ACT_HP_MAX, hp);
    if (st->weak_enemies) act_setf(a, ACT_HP_MAX, spill32((long double)hp * k0p1));
    hp = act_getf(a, ACT_HP_MAX);
    act_seti(a, 0x438, 0);
    act_seti(a, 0x43c, 0);
    act_seti(a, 0xa54, 0);
    act_setf(a, ACT_HP, hp);
    act_setf(a, ACT_HP2, hp);
    act_setf(a, ACT_YAW, kpi);
    return a;
}
