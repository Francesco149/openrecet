/*
 * collision_resolve.c — port of FUN_00433674 (raycast) + the player slice of
 * FUN_00483170 (radial push + ground snap).  World-space throughout.
 */
#include "collision_resolve.h"
#include "collision_query.h"

#include <math.h>

#define CR_PI 3.14159265358979323846f

/* Radial-push raycast exclusion (FUN_00433674 L222-225, first-object form):
 * the furniture-special / decorative types are skipped; walls are type 0 and
 * kept (told apart from floors by their near-horizontal normal). */
int collision_raycast_type_excluded(int type)
{
    switch (type) {
        case 5: case 6:                 /* slope/cliff */
        case 8: case 9:                 /* trees */
        case 10: case 11:               /* grass */
        case 12: case 13: case 14:      /* barrel/treasure/candle */
        case 15: case 16:               /* crystal/torch */
            return 1;
        default:
            return 0;                   /* 0,1,2,3 (and 7) participate */
    }
}

int collision_raycast(const collision_mesh *m,
                      const float pos[3], const float dir[3],
                      float *out_frac, float out_normal[3], int *out_type)
{
    float best = 1.0f;          /* only fractions in [0,1] count */
    const collision_tri *best_t = NULL;

    for (int oi = 0; oi < m->object_count; oi++) {
        const collision_object *o = &m->objects[oi];
        for (int ti = 0; ti < o->tri_count; ti++) {
            const collision_tri *t = &o->tris[ti];
            if (collision_raycast_type_excluded(t->type)) continue;

            const float nx = t->n[0], ny = t->n[1], nz = t->n[2], d = t->d;
            float sd = pos[0]*nx + pos[1]*ny + pos[2]*nz + d;   /* signed dist */
            if (sd < 0.0f) continue;                            /* behind face */
            float dn = dir[0]*nx + dir[1]*ny + dir[2]*nz;
            if (dn >= 0.0f) continue;                           /* not approaching */

            /* Ray-inside-triangle: (pos − v[k]) · (dir × edge[k]) ≥ 0 for all k
             * (FUN_00433674 L234-236). */
            int inside = 1;
            for (int k = 0; k < 3; k++) {
                const float *e = t->edge[k];
                float cx = dir[1]*e[2] - dir[2]*e[1];
                float cy = dir[2]*e[0] - dir[0]*e[2];
                float cz = dir[0]*e[1] - dir[1]*e[0];
                float dot = (pos[0]-t->v[k][0])*cx
                          + (pos[1]-t->v[k][1])*cy
                          + (pos[2]-t->v[k][2])*cz;
                if (dot < 0.0f) { inside = 0; break; }
            }
            if (!inside) continue;

            float frac = -(sd / dn);
            if (frac <= 1.0f && frac < best) { best = frac; best_t = t; }
        }
    }

    if (!best_t) return 0;
    if (out_frac) *out_frac = best;
    if (out_type) *out_type = best_t->type;
    if (out_normal) {
        /* Unit normal in the FUN_00433674 output form (cross(AB,BC), Y negated). */
        const float *ab = best_t->edge[0];
        const float *bc = best_t->edge[1];
        float x = ab[1]*bc[2] - bc[1]*ab[2];
        float y = bc[0]*ab[2] - bc[2]*ab[0];
        float z = bc[1]*ab[0] - ab[1]*bc[0];
        float len = sqrtf(x*x + y*y + z*z);
        if (len < 1e-20f) len = 1.0f;
        out_normal[0] = x/len; out_normal[1] = -(y/len); out_normal[2] = z/len;
    }
    return 1;
}

void collision_resolve_player(const collision_mesh *m, float pos[3], float vel[3])
{
    /* Integrate the planar velocity (FUN_00483170 L203-204). */
    pos[0] += vel[0];
    pos[2] += vel[2];

    /* Radial push: 8 horizontal rays around the player at head height
     * (FUN_00483170 L207-247).  *DAT_068dd2f0 != 0 in HOUSE so the count stays
     * 8 and the cos scale stays ×1.05. */
    const int nray = 8;
    for (int i = 0; i < nray; i++) {
        float ang = (float)i * (2.0f * CR_PI / (float)nray) + CR_PI;
        float dx = sinf(ang);
        float dz = cosf(ang) * 1.05f;
        float rp[3] = { pos[0], pos[1] + 0.7f, pos[2] };
        float rd[3] = { dx, 0.0f, dz };
        float frac, nrm[3];
        int type;
        if (!collision_raycast(m, rp, rd, &frac, nrm, &type)) continue;

        int push = 0;
        if (type == 1 || type == 2 || type == 7) push = 1;        /* wall types */
        else if (type == 0 && fabsf(nrm[1]) < 0.75f) push = 1;    /* vertical type-0 */
        if (push) { pos[0] -= dx; pos[2] -= dz; }
    }

    /* Ground snap (flat HOUSE floor): set Y to the floor under the player. */
    collision_hit h;
    if (collision_query_ground(m, pos[0], pos[1] + 1.0f, pos[2], &h))
        pos[1] = h.height;
}
