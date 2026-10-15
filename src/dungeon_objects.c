/*
 * dungeon_objects.c — see dungeon_objects.h.
 */
#include "dungeon_objects.h"

#include <string.h>

#include "audio.h"           /* FUN_00499519 */
#include "dungeon_flow.h"    /* DAT_0438bebc */
#include "map_collision.h"   /* mapcol_ground (FUN_00432e50) */
#include "rng.h"             /* FUN_00471089 */
#include "save_work.h"       /* the active work record */
#include "scene1_spawn.h"    /* FUN_00447f4f */
#include "stage_palette.h"   /* g_stage_b4dc (DAT_0438b4dc) */
#include "x87.h"

_Static_assert(sizeof(dobj_t) == 0x2a0, "DAT_0695ef78 record stride");
_Static_assert(sizeof(mapcol_hit_t) == 17 * 4, "the FUN_00432e50 output fits dwords 10..26");

dobj_t g_dobj[DOBJ_COUNT];

/* .data 0x5c4c98: the object kinds, 9 dwords each; FUN_0044ad81 copies [0].
 * (Kinds 0..3 overlap the tail of the enemy table's last entry.) */
static const int32_t k_dobj_attr[22] = {
    -1, -1, -1, -1, 1, 1, 1, 3, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

void dobj_reset(void)
{
    for (int n = 0; n < DOBJ_COUNT; n++) {
        dobj_t *r = &g_dobj[n];
        r->i[DOBJ_KIND]  = -1;
        r->i[DOBJ_D27]   = 0;
        r->i[DOBJ_STATE] = 0;
        for (int k = 0; k < 100; k++) r->i[DOBJ_RING + k] = r->i[DOBJ_PARAM];
    }
}

int dobj_spawn(const float pos[3], const float scale[3], float yaw, int32_t kind)
{
    int n = 0;
    dobj_t *r = g_dobj;
    while (r->i[DOBJ_KIND] != -1) {
        ++n;
        if (++r == g_dobj + DOBJ_COUNT)
            return 0;   /* retail: MessageBoxA(…, "error") */
    }
    r->i[DOBJ_KIND] = kind;
    r->i[DOBJ_ATTR] = (kind >= 0 && kind < 22) ? k_dobj_attr[kind] : -1;
    r->f[DOBJ_POS + 0] = pos[0];
    r->f[DOBJ_POS + 1] = pos[1];
    r->f[DOBJ_POS + 2] = pos[2];
    r->f[DOBJ_D36] = 0.0f;
    r->f[DOBJ_D31] = 0.0f;
    r->f[DOBJ_HIT + 3] = 0.0f;
    mapcol_ground(r->f[DOBJ_POS + 0], r->f[DOBJ_POS + 1], r->f[DOBJ_POS + 2],
                  (mapcol_hit_t *)&r->i[DOBJ_HIT]);
    r->f[DOBJ_POS + 1] = r->f[DOBJ_HIT + 3];
    r->f[DOBJ_HOME + 0] = r->f[DOBJ_POS + 0];
    r->f[DOBJ_HOME + 1] = r->f[DOBJ_POS + 1];
    r->f[DOBJ_HOME + 2] = r->f[DOBJ_POS + 2];
    r->f[DOBJ_SCALE + 0] = scale[0];
    r->f[DOBJ_SCALE + 1] = scale[1];
    r->f[DOBJ_SCALE + 2] = scale[2];
    r->f[DOBJ_YAW] = yaw;
    r->f[DOBJ_D29] = 0.0f;
    r->f[DOBJ_D30] = 0.0f;
    r->f[DOBJ_STATE] = 0.0f;
    r->f[DOBJ_PARAM] = 0.0f;
    for (int k = 0; k < 100; k++) r->i[DOBJ_RING + k] = r->i[DOBJ_PARAM];
    memset(&r->i[DOBJ_LINKS], 0xff, 10 * 4);
    if (r->i[DOBJ_KIND] == 4) {
        /* work byte +0x2bb80 + DAT_0438b4dc (per stage) set → state 2, param 1496 */
        const uint8_t *rec = save_work_bank_at(save_work_active_slot());
        if (rec != NULL && rec[0x2bb80 + g_stage_b4dc] != 0) {
            r->i[DOBJ_STATE] = 2;
            r->i[DOBJ_PARAM] = 1496;
            for (int k = 0; k < 100; k++) r->i[DOBJ_RING + k] = r->i[DOBJ_PARAM];
        } else {
            r->f[DOBJ_STATE] = 0.0f;
        }
    }
    return n;
}

/* FUN_0043a5d9: the stage objects' tick — the doors / gates slide (9 / 10 /
 * 15 / 18 / 19), the spinners (16 / 17 / 20 / 21), the levers (22 / 30), the
 * fades (6 / 7 / 14), the collapsing floor (12 / 13), an open chest (4 / 5 /
 * 8: the glow, the sparkles from 110, gone at 120); the hit shake (+0x24) */
void dobj_tick(void)
{
    static const float k0p1 = 0.10000000149011612f, k0p01 = 0.009999999776482582f;
    static const float k0p0025 = 0.0024999999441206455f, k0p5 = 0.5f, k1 = 1.0f;
    static const float k13 = 13.0f, km13 = -13.0f, km10 = -10.0f, k192 = 192.0f, k225 = 225.0f;
    static const float k325 = 325.0f, k0p4 = 0.4000000059604645f, k0p05 = 0.05000000074505806f;
    static const float k1p5 = 1.5f, k5em5 = 4.999999873689376e-05f, k0p02 = 0.019999999552965164f;
    static const float km1p1327 = -1.1327186822891235f, k0p15 = 0.15000000596046448f;
    static const float k1p885 = 1.8849557638168335f, k0p325 = 0.32499998807907104f;
    for (int i = 0; i < DOBJ_COUNT; i++) {
        dobj_t *o = &g_dobj[i];
        const int32_t kind = o->i[DOBJ_KIND];
        if (kind == -1) continue;
        const int32_t st = o->i[DOBJ_STATE];
        if (st == 3) continue;
        float v;
        switch (kind) {
        case 10: case 19:
            if (st == 1) {
                v = spill32((long double)o->f[0] - k0p1);
                o->f[0] = v;
                if (v < km13) o->f[0] = km13;
            }
            break;
        case 9: case 18:
            if (st == 1) {
                v = spill32((long double)o->f[0] + k0p1);
                o->f[0] = v;
                if (v > k13) o->f[0] = k13;
            }
            break;
        case 21:
            if (st == 0) o->f[28] = spill32((long double)o->f[28] + k0p01);
            else if (st == 1) o->i[155] += 17;
            break;
        case 16: case 20:
            o->f[28] = spill32((long double)o->f[28] + k0p0025);
            break;
        case 17:
            o->f[28] = spill32((long double)o->f[28] - k0p0025);
            break;
        case 15:
            if (st == 1) {
                v = spill32((long double)o->f[0] - k0p1);
                o->f[0] = v;
                if (v < km10) o->f[0] = km10;
            }
            break;
        case 12:
            if (st != 1) break;
            v = spill32((long double)o->f[31] + k1);
            o->f[31] = v;
            if (!(v < k192)) {
                float k = 1.0f;
                if (!(v < k225)) {
                    k = spill32((long double)k1 - ((long double)v - k225) * k0p01);
                    if (k < 0.0f) k = 0.0f;
                }
                o->f[0] = spill32((long double)k * k0p4 + o->f[0]);
                o->f[30] = spill32((long double)k * k0p05 + o->f[30]);
            }
            if (!(o->f[31] < k225)) {
                float d = spill32(((long double)o->f[31] - k225) * k0p05);
                if (d > k1p5) d = k1p5;
                o->f[1] = spill32((long double)o->f[1] - d);
            }
            if (!(o->f[31] < k325)) o->i[DOBJ_KIND] = -1;
            break;
        case 13:
            if (st != 1) break;
            v = spill32((long double)o->f[31] + k5em5);
            o->f[31] = v;
            if (g_dat_0438bebc > 0) { if (v > k0p02) o->f[31] = k0p02; }
            else if (v > k0p01) o->f[31] = k0p01;
            o->f[30] = spill32((long double)o->f[30] - o->f[31]);
            break;
        case 11:
            if (st == 1) {
                v = spill32((long double)o->f[29] - k0p01);
                o->f[29] = v;
                if (v < km1p1327) o->f[29] = km1p1327;
            }
            break;
        case 22: case 30:
            if (st == 0) {
                v = spill32((long double)o->f[28] - k0p1);
                o->f[28] = v;
                if (v < k0p15 && !(v < k0p05)) audio_play_se_by_id(336);
                if (o->f[28] < 0.0f) o->f[28] = 0.0f;
            } else if (st == 1) {
                v = spill32((long double)o->f[28] + k0p1);
                o->f[28] = v;
                if (v < k0p15 && !(v < k0p05)) audio_play_se_by_id(347);
                if (o->f[28] > k1p885) o->f[28] = k1p885;
            }
            break;
        case 7: case 14: case 6:
            if (st == 1) {
                o->i[155] += 17;
                if (kind == 7 && o->i[155] == 17) audio_play_se_by_id(318);
                const int32_t cap = kind == 6 ? 1360 : 1020;
                if (o->i[155] > cap) o->i[155] = cap;
                for (int k = 0; k < 100; k++) o->i[DOBJ_RING + k] = o->i[155];
            }
            break;
        case 4: case 5: case 8:
            if (st == 2) {
                int32_t n = ++o->i[155];
                if (n > 88) n = 88;
                for (int k = 0; k < 100; k++) o->i[DOBJ_RING + k] = 17 * n;
                if (o->i[155] > 110) {
                    const float z = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + o->f[2]);
                    const float y = spill32((long double)o->f[1] + k0p5);
                    const float x = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + o->f[0]);
                    scene1_spawn(0, x, y, z, 14, k0p325, 1);
                }
                if (o->i[155] == 120) o->i[DOBJ_KIND] = -1;
            }
            break;
        default:
            if (st == 1) o->i[DOBJ_STATE] = 0;
            break;
        }
        if (o->i[9] != 0) {
            --o->i[9];
            o->f[6] = spill32(((long double)rng_next_unit() - k0p5) * k0p5);
            o->f[8] = spill32(((long double)rng_next_unit() - k0p5) * k0p5);
        } else {
            o->f[6] = 0.0f;
            o->f[8] = 0.0f;
        }
        ++o->i[27];
    }
}
