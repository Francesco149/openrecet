/*
 * table_c.c — see table_c.h.  Line refs are retail VAs.
 */
#include "table_c.h"

#include <math.h>
#include <string.h>

#include "dungeon_party.h"       /* FUN_00484dd1 */
#include "map_collision.h"       /* FUN_00432e50, FUN_00433674 */
#include "party.h"
#include "scene1_records.h"
#include "scene1_spawn.h"        /* FUN_00447f4f */
#include "x87.h"

#define CI(r, k) (*(int32_t *)(void *)&(r)[k])
#define CF(r, k) (*(float *)(void *)&(r)[k])

static int32_t *rec_at(int k) { return &g_scene1_records_c[k * SCENE1_RECORDS_C_STRIDE]; }
static int is_orb(int32_t kind) { return kind == 2 || kind == 0 || kind == 1 || kind == 3; }

void table_c_bound_update(void)
{
    g_table_c_bound = 0;
    for (int k = 0; k < SCENE1_RECORDS_C_COUNT; k++)
        if (CI(rec_at(k), 10) != -1) g_table_c_bound = k + 1;
}

void table_c_tick(void)
{
    static const float k0 = 0.0f, k0p05 = 0.05000000074505806f, k0p97 = 0.9700000286102295f;
    static const float k0p6 = 0.6000000238418579f, k1p5 = 1.5f, k1 = 1.0f;
    static const float k0p1 = 0.10000000149011612f, k0p2 = 0.20000000298023224f, k0p5 = 0.5f;
    static const float km0p3 = -0.30000001192092896f, km0p7 = -0.699999988079071f, km1 = -1.0f;
    static const float k0p15 = 0.15000000596046448f, k_2pi = 6.2831854820251465f, k3 = 3.0f;
    const int n = g_table_c_bound;
    /* more than 6 orbs: the oldest past 1200 ticks goes */
    int orbs = 0;
    for (int k = 0; k < n; k++) if (is_orb(CI(rec_at(k), 10))) orbs++;
    if (orbs > 6) {
        int pick = -1;
        int32_t oldest = 1200;
        for (int k = 0; k < n; k++) {
            const int32_t *r = rec_at(k);
            if (is_orb(CI(r, 10)) && oldest < CI(r, 11)) { oldest = CI(r, 11); pick = k; }
        }
        if (pick >= 0) CI(rec_at(pick), 10) = -1;
    }
    for (int k = 0; k < n; k++) {
        int32_t *r = rec_at(k);
        if (CI(r, 10) == -1) continue;
        if (CI(r, 36) == 1) {
            --CI(r, 11);
        } else if (CI(r, 16) == 2) {
            /* to the bag: a sparkle at 10, the float up 21..79, in at 120 */
            if (CI(r, 11) == 10) scene1_spawn(0, CF(r, 0), CF(r, 1), CF(r, 2), 45, k0p15, 1);
            if (CI(r, 11) > 20 && CI(r, 11) < 80) CF(r, 1) = spill32((long double)CF(r, 1) + k0p05);
            if (CI(r, 11) == 120) {
                party_get_item(CI(r, 10), CI(r, 14), CI(r, 15));   /* FUN_00484dd1 */
                CI(r, 10) = -1;
            }
        } else if (CI(r, 16) == 0) {
            const float sc = CF(r, 12);
            CF(r, 0) = spill32((long double)sc * CF(r, 3) + CF(r, 0));
            CF(r, 1) = spill32((long double)sc * CF(r, 4) + CF(r, 1));
            CF(r, 2) = spill32((long double)sc * CF(r, 5) + CF(r, 2));
            CF(r, 3) = spill32((long double)CF(r, 3) * k0p97);
            CF(r, 4) = spill32((long double)CF(r, 4) * k0p97);
            CF(r, 5) = spill32((long double)CF(r, 5) * k0p97);
            for (int32_t j = 0; j != 3; j++) {
                const float ang = spill32((long double)spill32((long double)j) * k_2pi / k3);
                const float ox = CF(r, 0), oy = spill32((long double)CF(r, 1) + k0p6), oz = CF(r, 2);
                const double ad = ang;
                const float sx = spill32((long double)spill32(x87_crt_sin(ad)) * k1p5);
                const float sz = spill32((long double)spill32(x87_crt_cos(ad)) * k1p5);
                float frac = 0.0f;
                mapcol_hit_t h;
                h.tri = 0;
                h.slot = 0;
                if (mapcol_ray(ox, oy, oz, sx, 0.0f, sz, &frac, &h) != 0) {
                    const float t = spill32((long double)k1 - frac);
                    CF(r, 0) = spill32((long double)CF(r, 0) - (long double)t * sx);
                    CF(r, 2) = spill32((long double)CF(r, 2) - (long double)t * sz);
                    CF(r, 3) = spill32((long double)h.n[0] * k0p1);
                    CF(r, 5) = spill32((long double)h.n[2] * k0p1);
                }
            }
            if (CI(r, 11) < 1200) {
                CF(r, 4) = spill32((long double)CF(r, 4) - k0p05);
                if (CI(r, 11) > 60 && is_orb(CI(r, 10))) {
                    /* an orb homes on the player */
                    const float dx = spill32((long double)g_party.pos[0][0] - CF(r, 0));
                    float dz = spill32((long double)g_party.pos[0][2] - CF(r, 2));
                    CF(r, 3) = spill32((long double)dx * k0p2 + CF(r, 3));
                    dz = spill32((long double)dz * k0p2 + CF(r, 5));
                    CF(r, 5) = dz;
                    const float vx = CF(r, 3);
                    float l = spill32((long double)dz * dz + (long double)vx * vx);
                    if (l > k0) {
                        l = spill32((long double)sqrt(spill64(l)));
                        if (l > k1) {
                            CF(r, 3) = spill32((long double)CF(r, 3) * k0p5 / l);
                            CF(r, 5) = spill32((long double)CF(r, 5) * k0p5 / l);
                        }
                    }
                }
                mapcol_hit_t *g = (mapcol_hit_t *)(void *)&r[19];
                if (mapcol_ground(CF(r, 0), CF(r, 1), CF(r, 2), g) != 0) {
                    const float gy = g->height;
                    CF(r, 9) = gy;
                    if (CF(r, 1) < gy) {
                        CF(r, 1) = gy;
                        if (CF(r, 4) < k0) {
                            if (CF(r, 4) > km0p3) {
                                CF(r, 4) = 0.0f;
                                if (!is_orb(CI(r, 10))) {
                                    CF(r, 3) = 0.0f;
                                    CF(r, 5) = 0.0f;
                                    CI(r, 16) = 1;
                                }
                            } else {
                                CF(r, 4) = spill32((long double)CF(r, 4) * km0p7);
                            }
                        }
                    }
                }
            }
        }
        ++CI(r, 11);
        if (is_orb(CI(r, 10))) {
            if (CI(r, 11) == 240) CI(r, 10) = -1;
            if (CF(r, 1) < km1) CI(r, 10) = -1;
        }
    }
}
