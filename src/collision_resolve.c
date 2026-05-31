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

void collision_resolve_player(const collision_mesh *m, float pos[3], float vel[3],
                              int palette_mode)
{
    /* Integrate the planar velocity (FUN_00483170 L203-204). */
    pos[0] += vel[0];
    pos[2] += vel[2];

    /* Radial push (FUN_00483170 L84404-84445).  Ray count is 8, but **20** when
     * the stage is HOUSE-class (`*DAT_068dd2f0` == stage-palette mode 0) AND the
     * player is past pz=0.7 — the extra 12 rays sample the wall/counter at
     * stacked heights so the back of the room is sealed.  Confirmed against
     * retail: the resolver calls the raycast exactly 20×/frame at the HOUSE
     * counter row (runs/wall-retail call-trace).  `pos[2]` is read post-integrate
     * each call, so the count drops back to 8 once the player slides to pz≤0.7. */
    int nray = 8;
    if (palette_mode == 0 && pos[2] > 0.7f) nray = 20;

    for (int i = 0; i < nray; i++) {
        /* Ray height (L84413-84416): base rays 0..7 at py+0.7; the stacked rays
         * 8..19 climb in 0.08 steps from py+0.1, so the lower wall/counter faces
         * are sampled too. */
        float ry = pos[1] + 0.7f;
        if (i > 7) ry = (float)(i / 8 + 1) * 0.08f + pos[1] + 0.1f;

        float ang = (float)i * (2.0f * CR_PI / (float)nray) + CR_PI;
        float dx = sinf(ang);
        float dz = cosf(ang) * 1.05f;
        /* HOUSE-class (mode 0): forward-ish rays (cos·1.05 > 0.1) use the tighter
         * 1.03 scale instead of 1.05 (L84422-84425). */
        if (palette_mode == 0 && dz > 0.1f) dz = cosf(ang) * 1.03f;

        float rp[3] = { pos[0], ry, pos[2] };
        float rd[3] = { dx, 0.0f, dz };
        float frac, nrm[3];
        int type;
        if (!collision_raycast(m, rp, rd, &frac, nrm, &type)) continue;

        int push = 0;
        if (type == 1 || type == 2 || type == 7) push = 1;        /* wall types */
        else if (type == 0 && fabsf(nrm[1]) < 0.75f) push = 1;    /* vertical type-0 */
        if (push) {
            /* Push out by the PENETRATION depth (1 − frac), not the full ray
             * (FUN_00483170 asm 0x483bc3: `fld1; fsubs frac` → the Ghidra decomp
             * dropped this factor and showed a bare `* 1.0`).  Scaling by the
             * unpenetrated remainder is what makes the player settle *against*
             * the wall (Δpx exactly cancels the into-wall velocity) instead of
             * bouncing off by a full unit.  Verified 1:1 vs retail. */
            float s = 1.0f - frac;
            pos[0] -= s * dx;
            pos[2] -= s * dz;
        }
    }

    /* Ground snap (flat HOUSE floor): set Y to the floor under the player. */
    collision_hit h;
    if (collision_query_ground(m, pos[0], pos[1] + 1.0f, pos[2], &h))
        pos[1] = h.height;
}

void collision_resolve_player_floor(const collision_mesh *m,
                                    float pos[3], const float vel[3])
{
    /* FUN_004830f1: query the floor at (pos + delta); floor found → move OK,
     * else blocked.  The query point is lifted +1.0 above the player so the
     * downward-5u floor solve in collision_query_ground reaches a y≈0 floor
     * (mirrors the engine's py+dy probe with the flat-floor approximation). */
    const float probe_y = pos[1] + 1.0f;
    collision_hit h;

    float nx = pos[0] + vel[0];
    float nz = pos[2] + vel[2];

    if (collision_query_ground(m, nx, probe_y, nz, &h)) {
        /* Destination is over the floor — take the whole move. */
        pos[0] = nx;
        pos[2] = nz;
    } else {
        /* Off-floor: slide.  Try each axis alone so a wall blocks only the
         * into-wall component (retail "X blocked, Z free").  Test X against
         * the *current* Z, then Z against the (possibly updated) X. */
        if (collision_query_ground(m, nx, probe_y, pos[2], &h))
            pos[0] = nx;
        if (collision_query_ground(m, pos[0], probe_y, nz, &h))
            pos[2] = nz;
    }

    /* Ground snap (flat HOUSE floor). */
    if (collision_query_ground(m, pos[0], pos[1] + 1.0f, pos[2], &h))
        pos[1] = h.height;
}
