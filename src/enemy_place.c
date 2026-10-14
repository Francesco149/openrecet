/*
 * enemy_place.c — see enemy_place.h.  Comments name the retail stack slots
 * (FUN_00431d54's frame) where the decompile is ambiguous.
 */
#include "enemy_place.h"

#include <stdio.h>
#include <string.h>

#include "audio_mci.h"        /* FUN_00451874 debug text rows */
#include "floor_layout.h"
#include "map_collision.h"
#include "rng.h"
#include "tables_enemylist.h"
#include "x87.h"

static int32_t rd32(const uint8_t *rec, uint32_t off) { int32_t v; memcpy(&v, rec + off, 4); return v; }

static const enemylist_section_t *section_for(int32_t dungeon, int32_t floor)
{
    if (dungeon < 0 || dungeon >= ENEMYLIST_DUNGEON_SLOTS) return NULL;
    for (int k = 0; k < ENEMYLIST_SECTIONS_PER_DUNGEON; k++) {
        const enemylist_section_t *s = &g_enemylist.sections[dungeon][k];
        if (s->floor_lo == -1) return NULL;
        if (floor >= s->floor_lo && floor <= s->floor_hi) return s;
    }
    return NULL;
}

void enemy_place_floor(const enemy_place_in_t *in)
{
    static const int32_t k_sizes[5] = { 2, 1, 1, 1, 3 };          /* DAT_005adbdc */
    static const float k40 = 40.0f, k2pi = 6.2831855f, k14 = 14.0f, k5 = 5.0f, k0p8 = 0.8f;
    static const float k0p5 = 0.5f, km1 = -1.0f, km3 = -3.0f, k10 = 10.0f, km10 = -10.0f;
    const uint8_t *rec = in->rec;

    const int32_t ngroups_total = g_floor_rooms_total - 2;           /* edi */
    if (ngroups_total <= 0) return;
    int32_t sizes[100] = { 0 };                                      /* [ebp-0x4bc] (stack: past the filled
                                                                        entries retail reads garbage) */
    for (int32_t i = 0; i < ngroups_total && i < 100; i++) sizes[i] = k_sizes[i % 5];
    if (in->is_boss_floor())
        for (int32_t i = 0; i < ngroups_total + 2 && i < 100; i++) sizes[i] = i % 3 + 4;
    for (int32_t i = 0; i < ngroups_total; i++) {
        const uint32_t j = (uint32_t)rng_next15() % (uint32_t)ngroups_total;
        const int32_t t = sizes[j];
        sizes[j] = sizes[i];
        sizes[i] = t;
    }

    const enemylist_section_t *sec = section_for(in->st.dungeon, in->st.floor);
    if (!sec) return;
    int32_t pair_id[64] = { 0 }, pair_var[64] = { 0 }, npairs = 0;   /* [ebp-0x32c]/[-0x328], [ebp-0x14] */
    for (int e = 0; e < ENEMYLIST_ENEMY_SLOTS_PER_SECTION; e++) {
        const enemylist_enemy_t *en = &sec->enemies[e];
        if (en->enemy_id == -1) break;
        for (int32_t c = 0; c < en->count; c++, npairs++)
            if (npairs < 64) { pair_id[npairs] = en->enemy_id; pair_var[npairs] = en->variant; }
    }

    /* the two specials, rolled only when every other condition holds */
    int32_t sp3 = 0, sp2 = 0;                                        /* [ebp-0x30], [ebp-0x34] */
    if (rd32(rec, 0x2dd64) != 2) {
        if (!in->is_boss_floor() && rec[0x2bc81] != 0 && rec[0x2bcc3] == 0 && rec[0x2bd07] == 0 &&
            rd32(rec, 0x2c3ec) >= 8 && (uint32_t)rng_next15() % 10u == 0)
            sp3 = 1;
        if (!in->is_boss_floor() && rd32(rec, 0x2c3ec) > 9 && rec[0x2bcf3] == 0 && in->st.dungeon < 3 &&
            rec[0x2bd3c] == 0 && (uint32_t)rng_next15() % 10u == 0)
            sp2 = 1;
    }

    for (int32_t y = 0; y < g_floor_h; y++)
        for (int32_t x = 0; x < g_floor_w; x++) {
            char b[16];
            snprintf(b, sizeof b, "%d", g_floor_rooms[y * FLOOR_GRID_STRIDE + x].type);
            audio_mci_record_command(x, y + 30, b);
        }

    int32_t order[64] = { 0 };                                       /* [ebp-0x234] */
    for (int32_t i = 0; i < npairs && i < 64; i++) order[i] = i;
    for (int32_t i = 0; i < npairs; i++) {
        const uint32_t j = (uint32_t)rng_next15() % (uint32_t)npairs;
        if (i < 64 && j < 64) { const int32_t t = order[j]; order[j] = order[i]; order[i] = t; }
    }

    int32_t cursor = 0, gidx = 0;                                    /* [ebp-0x48], [ebp-0x4] */
    mapcol_hit_t hit;                                                /* [ebp-0xa4] */
    memset(&hit, 0, sizeof hit);
    for (int32_t row = 0; row < g_floor_h; row++) {
        for (int32_t col = 0; col < g_floor_w; col++) {
            const int32_t ci = row * FLOOR_GRID_STRIDE + col;
            if (g_floor_rooms[ci].type == -1) continue;
            if (in->is_boss_floor()) {
                if (g_floor_marks[0][0] == col && g_floor_marks[0][1] == row) continue;
            } else if ((g_floor_marks[0][0] == col && g_floor_marks[0][1] == row) ||
                       (g_floor_marks[1][0] == col && g_floor_marks[1][1] == row)) {
                continue;   /* the start and exit rooms */
            }
            int32_t n = gidx < 100 ? sizes[gidx] : 0;                /* [ebp-0x1c] */
            gidx++;
            int32_t groups = n < 2 ? 2 : 1;                          /* [ebp-0x50] */
            if (sp3 && gidx == 3) groups = 1;
            if (sp2 && gidx == 2) groups = 1;
            for (; groups > 0; groups--) {
                const int32_t pi = npairs > 0 && cursor < 64 ? order[cursor] : 0;
                int32_t type = pi < 64 ? pair_id[pi] : 0;            /* edi */
                const int32_t var = pi < 64 ? pair_var[pi] : 0;      /* [ebp-0x3c] */
                if (++cursor == npairs) cursor = 0;
                if (sp3 && gidx == 3) n = 1;
                if (sp2 && gidx == 2) n = 5;
                if ((type == 35 || type == 36) && var == 1) n = 1;
                if (type == 16 && var == 2) n = 1;
                if (n == 0) continue;
                const float cx = spill32((long double)spill32((long double)-col) * k40);   /* [ebp-0x28] */
                const float cz = spill32((long double)spill32((long double)-row) * k40);   /* [ebp-0x20] */
                for (int32_t placed = 0; placed != n; placed++) {   /* ebx */
                    for (int tries = 0; tries < 100; tries++) {    /* esi */
                        const float ang = spill32((long double)rng_next_unit() * k2pi);
                        const float r = spill32((long double)rng_next_unit() * k14 + k5);
                        const double angd = ang;                     /* [ebp-0x5c] */
                        float sx = spill32((long double)spill32(x87_crt_sin(angd)) * r);
                        float sz = spill32((long double)spill32(x87_crt_cos(angd)) * r);
                        float frac = 0.0f;
                        hit.tri = 0;
                        hit.slot = 0;
                        if (mapcol_ray(cx, k0p5, cz, sx, 0.0f, sz, &frac, &hit) != 0) continue;
                        int adjust = 1;                  /* 0: type 30 goes straight to the probe */
                        if (sp3 && gidx == 3) type = 29;
                        if (sp2 && gidx == 2) {
                            if (placed == 0) { type = 30; adjust = 0; }
                            else type = 31;
                        }
                        if (adjust && (type == 35 || type == 36)) {
                            sx = spill32((long double)spill32(x87_crt_sin(angd)) * r * k0p8);
                            sz = spill32((long double)spill32(x87_crt_cos(angd)) * r * k0p8);
                        }
                        if (adjust && type == 16 && var == 2) {
                            if (g_floor_rooms[ci + FLOOR_GRID_STRIDE].type != -1) { sx = km3; sz = km10; }
                            else if (g_floor_rooms[ci - FLOOR_GRID_STRIDE].type != -1) { sx = km3; sz = k10; }
                            else if (g_floor_rooms[ci + 1].type == -1 && g_floor_rooms[ci - 1].type == -1) break;
                            else { sx = k10; sz = km3; }
                        }
                        const float gx = spill32((long double)sx + cx), gz = spill32((long double)sz + cz);
                        if (mapcol_ground(gx, k0p5, gz, &hit) != 1 || !(hit.height >= km1)) continue;
                        if (rec[0x2bd20] == 0) {
                            actor_t *a = actor_spawn_enemy(&in->st, gx, k0p5, gz, type);
                            if (a) {
                                act_seti(a, ACT_INUSE, 1);
                                if (type == 35 || type == 36) act_seti(a, ACT_PLACE_KIND, 1);
                                if (type == 16 && var == 2) {
                                    if (g_floor_rooms[ci + FLOOR_GRID_STRIDE].type != -1 ||
                                        g_floor_rooms[ci - FLOOR_GRID_STRIDE].type != -1)
                                        act_seti(a, ACT_PLACE_KIND, 2);
                                    else if (g_floor_rooms[ci + 1].type != -1 || g_floor_rooms[ci - 1].type != -1)
                                        act_seti(a, ACT_PLACE_KIND, 1);
                                }
                                act_seti(a, ACT_GROUP_VAR, var);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
}

void enemy_place_boss(const enemy_place_in_t *in)
{
    static const float km40 = -40.0f, km34 = -34.0f, km38 = -38.0f, k10 = 10.0f, k45 = 45.0f, k50 = 50.0f;
    static const float k15 = 15.0f;
    const int32_t d = in->st.dungeon, f = in->st.floor;
    if ((d == 0 && f == 4) || (d == 1 && f == 14) || (d == 2 && f == 29) ||
        (d == 3 && (f == 19 || f == 29 || f == 49)) ||
        (d == 4 && (f == 29 || f == 49 || f == 69 || f == 79 || f == 99)) ||
        (d == 5 && (f == 37 || f == 41 || f == 56 || f == 58)))
        return;
    if (!in->gate_query()) { enemy_place_floor(in); return; }
    const enemylist_section_t *sec = section_for(d, f);
    if (!sec || in->rec[0x2bcfe] != 0) return;
    const int32_t type = sec->enemies[0].enemy_id;
    if (type == -1) return;
    for (int32_t j = 0; j != sec->enemies[0].count; j++) {
        float x = km40, y = 0.0f, z = km34;
        const int32_t cnt = sec->enemies[0].count;
        if (cnt == 2) x = spill32((long double)spill32((long double)j) * k10 - k45);
        else if (cnt == 3) {
            x = spill32((long double)spill32((long double)j) * k10 - k50);
            if (j != 1) z = km38;
        }
        if (type == 70 || type == 71) z = km40;
        if (type == 23) y = k15;
        if (type == 54) y = k15;
        actor_t *a = actor_spawn_enemy(&in->st, x, y, z, type);
        if (a) {
            act_seti(a, ACT_INUSE, 1);
            act_seti(a, 0xa54, 8);
        }
    }
}
