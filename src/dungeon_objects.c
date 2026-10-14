/*
 * dungeon_objects.c — see dungeon_objects.h.
 */
#include "dungeon_objects.h"

#include <string.h>

#include "map_collision.h"   /* mapcol_ground (FUN_00432e50) */
#include "save_work.h"       /* the active work record */
#include "stage_palette.h"   /* g_stage_b4dc (DAT_0438b4dc) */

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
