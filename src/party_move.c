/*
 * party_move.c — see party_move.h.  Line refs are retail VAs.
 */
#include "party_move.h"

#include <math.h>
#include <string.h>

#include "actor_table.h"         /* the actor table (enemy bodies) */
#include "audio.h"               /* FUN_00499519 */
#include "dungeon_flow.h"        /* FUN_0043647f */
#include "dungeon_objects.h"     /* the stage objects */
#include "party.h"
#include "tables_enemy.h"        /* DAT_005c23f0 (the body radii) */
#include "x87.h"

static const float k0 = 0.0f, k0p5 = 0.5f, k1 = 1.0f;
static const float k0p05 = 0.05000000074505806f;       /* 0x5198f8 */
static const float k0p1 = 0.10000000149011612f;        /* 0x5193a0 */
static const float k0p01 = 0.009999999776482582f;      /* 0x5193a4 */
static const float k0p001 = 0.0010000000474974513f;    /* 0x5198f4 */
static const float k1em4 = 9.999999747378752e-05f;     /* 0x519f5c */
static const float k0p3 = 0.30000001192092896f;        /* 0x5194ec */
static const float k0p6 = 0.6000000238418579f;         /* 0x51969c */
static const float k0p7 = 0.699999988079071f;          /* 0x519748 */
static const float k0p75 = 0.75f;                      /* 0x519b54 */
static const float k0p08 = 0.07999999821186066f;       /* 0x519894 */
static const float k1p03 = 1.0299999713897705f;        /* 0x519f60 */
static const float k1p05 = 1.0499999523162842f;        /* 0x5198ac */
static const float km0p4 = -0.4000000059604645f;       /* 0x519a7c */
static const float k0p005 = 0.004999999888241291f;     /* 0x5198c8 */
static const float k3 = 3.0f;                          /* 0x519438 */
static const float km70 = -70.0f;                      /* 0x519d0c */
static const float km10 = -10.0f, km30 = -30.0f, km100 = -100.0f;
static const float k_pi = 3.1415927410125732f;         /* 0x51943c */
static const float k_pi2 = 1.5707963705062866f;        /* 0x519434 */
static const float k_3pi2 = 4.71238899230957f;         /* 0x519afc */
static const float k_2pi = 6.2831854820251465f;        /* 0x519398 */

static long double crt_sqrt(double x) { return (long double)sqrt(x); }
static long double crt_atan2(double y, double x)
{
    unsigned short cw, cw53 = 0x027f;
    long double r;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    __asm__ volatile("fldcw %0" : : "m"(cw53));
    __asm__ volatile("fpatan" : "=t"(r) : "0"((long double)x), "u"((long double)y) : "st(1)");
    __asm__ volatile("fldcw %0" : : "m"(cw));
    return r;
}

/* the material class of a ground / ray hit's triangle (slot record +0x90) */
static int32_t hit_type(int32_t slot, int32_t tri)
{
    if (slot < 0 || slot >= MAPCOL_SLOTS || g_mapcol_slot[slot].tris == NULL ||
        tri < 0 || tri >= g_mapcol_slot[slot].cap)
        return 0;
    return g_mapcol_slot[slot].tris[tri].type;
}

#define PX (g_party.pos[0][0])
#define PY (g_party.pos[0][1])
#define PZ (g_party.pos[0][2])
#define VX (g_party.vel[0][0])
#define VY (g_party.vel[0][1])
#define VZ (g_party.vel[0][2])

int seg2_side(float a1, float a2, float a3, float a4, float a5, float a6)
{
    const int32_t v = x87_ftol(((long double)a2 - a4) * a5 + ((long double)a4 - a6) * a1 +
                               ((long double)a6 - a2) * a3);
    if (v > 0) return 1;
    return (v >= 0) - 1;
}

/* FUN_00433fa6: a = (x0, y0, x1, y1), b likewise — the bounding boxes
 * overlap, then each segment's ends lie on opposite sides of the other */
int seg2_cross(const float a[4], const float b[4])
{
    if ((a[2] <= a[0] ? a[2] : a[0]) > (b[2] >= b[0] ? b[2] : b[0])) return 0;
    if ((a[3] <= a[1] ? a[3] : a[1]) > (b[3] >= b[1] ? b[3] : b[1])) return 0;
    if ((b[2] <= b[0] ? b[2] : b[0]) > (a[2] >= a[0] ? a[2] : a[0])) return 0;
    if ((b[3] <= b[1] ? b[3] : b[1]) > (a[3] >= a[1] ? a[3] : a[1])) return 0;
    const int s1 = seg2_side(b[0], b[1], a[0], a[1], a[2], a[3]);
    if (seg2_side(b[2], b[3], a[0], a[1], a[2], a[3]) * s1 > 0) return 0;
    const int s3 = seg2_side(a[2], a[3], b[0], b[1], b[2], b[3]);
    return seg2_side(a[0], a[1], b[0], b[1], b[2], b[3]) * s3 <= 0;
}

int party_try_move(float x, float y, float z, float vx, float vy, float vz, mapcol_hit_t *hit)
{
    if (vx != k0 || vy != k0 || vz != k0) { hit->slot = 0; hit->tri = 0; }
    if (mapcol_ground(spill32((long double)x + vx), spill32((long double)y + vy),
                      spill32((long double)z + vz), hit) == 1)
        return 1;
    hit->slot = 0;
    hit->tri = 0;
    hit->height = km100;
    return 2;
}

int party_push(const float b[3], float radius, float height)
{
    const float dx = spill32((long double)b[0] - ((long double)PX + VX));
    float dz = spill32((long double)b[2] - ((long double)PZ + VZ));
    float len = spill32((long double)dz * dz + (long double)dx * dx);
    const float dy = spill32(fabsl((long double)spill32((long double)b[1] - PY)));
    if (len > k0) len = spill32(crt_sqrt(len));
    else { dz = k0p01; len = k0p01; }
    if (dy > height) return 0;
    float r = spill32((long double)spill32(x87_crt_cos(spill64((long double)dy * k_pi2 / height))) * radius);
    if (r < k0) r = 0.0f;
    float cx = spill32((long double)b[0] - PX);
    float cz = spill32((long double)b[2] - PZ);
    float c = spill32(crt_sqrt(spill64((long double)cz * cz + (long double)cx * cx)));
    if (c == k0) {
        cx = spill32((long double)b[0] + k0p001 - PX);
        cz = spill32((long double)b[2] - PZ);
        c = k0p001;
    }
    if (c < len) return 0;
    int push_out = c < r;
    if (!push_out) {
        if (len >= r) return 0;
        const int32_t st = PW(0x56db048u).i;
        if (st == 2 || st == 3 || st == 4) {
            if (VX != k0 || VZ != k0) push_out = 1;
            else return 0;
        }
    }
    if (push_out) {
        const float k = spill32((long double)r - c);
        VX = spill32(-((long double)k * cx / c) * k0p5);
        VZ = spill32(-((long double)k * cz / c) * k0p5);
        return 1;
    }
    /* slide around the body: aim at the tangent point len + 1e-4 out */
    const double ddx = dx, ddz = dz;
    const float a0 = spill32(crt_atan2(ddx, ddz));
    float ang = spill32((long double)a0 + k1em4);
    double X = ang;
    const float rr = spill32((long double)len + k1em4);
    float s = spill32(x87_crt_sin(X));
    float nx = spill32(((long double)b[0] - (long double)s * rr) - ((long double)PX + VX));
    float co = spill32(x87_crt_cos(X));
    float nz = spill32(((long double)b[2] - (long double)co * rr) - ((long double)PZ + VZ));
    float vl = spill32(crt_sqrt(spill32((long double)VZ * VZ + (long double)VX * VX)));
    float nl = spill32(crt_sqrt(spill64((long double)nz * nz + (long double)nx * nx)));
    const float av = spill32((long double)spill32(crt_atan2(VX, VZ)) + k_pi);
    const float an = spill32((long double)spill32(crt_atan2(nx, nz)) + k_pi);
    const float dd = spill32((long double)av - an);
    int other = 0;
    if (spill32(fabsl((long double)dd)) > k_pi2 && spill32(fabsl((long double)dd)) < k_3pi2) other = 1;
    if (other) {
        ang = spill32((long double)spill32(crt_atan2(ddx, ddz)) - k1em4);
        X = ang;
        s = spill32(x87_crt_sin(X));
        nx = spill32(((long double)b[0] - (long double)s * rr) - ((long double)PX + VX));
        co = spill32(x87_crt_cos(X));
        nz = spill32(((long double)b[2] - (long double)co * rr) - ((long double)PZ + VZ));
        vl = spill32(crt_sqrt(spill32((long double)VZ * VZ + (long double)VX * VX)));
        nl = spill32(crt_sqrt(spill64((long double)nz * nz + (long double)nx * nx)));
    } else if (vl < k0p01) {
        vl = k0p01;
    }
    VX = spill32((long double)nx / nl * vl * k0p5);
    VZ = spill32((long double)nz / nl * vl * k0p5);
    return 1;
}

/* the dobj kind table (.data 0x5c4c98) columns FUN_00483170 reads: [1] radius,
 * [2] height, [4] solid */
static const float   k_kind_r[22] = { 3.0f, 2.0f, 1.0f, 1.0f, 5.0f, 5.0f, 4.0f, 0.30000001192092896f,
                                      0.30000001192092896f, 0.800000011920929f, 0.800000011920929f,
                                      0.800000011920929f, 0.800000011920929f, 0.800000011920929f,
                                      0.800000011920929f, 0.800000011920929f, 0.800000011920929f,
                                      0.0f, 0.0f, 0.800000011920929f, 0.800000011920929f, 0.0f };
static const float   k_kind_h[22] = { 10.0f, 2.0f, 10.0f, 10.0f, 6.0f, 6.0f, 6.0f, 2.0f, 2.0f,
                                      10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f,
                                      10.0f, 10.0f, 10.0f, 10.0f, 10.0f };
static const int32_t k_kind_solid[22] = { 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0 };

void party_move(void)
{
    int hit = 0;
    /* enemy bodies (and the multi-part bodies of types 0x44 / 0x45 / 0x3e / 0x3f) */
    if (PW(0x56db118u).i == 0) {
        for (int i = 0; i < ACTOR_COUNT; i++) {
            actor_t *a = &g_actor[i];
            if (act_geti(a, ACT_INUSE) == 0 || act_geti(a, 0xaf4) > 0 ||
                act_getf(a, 0x450) < km70 || act_geti(a, 0xb04) != 0)
                continue;
            const int32_t type = act_geti(a, ACT_TYPE);
            const enemy_record_t *e = &g_enemy[type];
            const float *rf = e->runtime_floats;   /* +0x44 scale, +0x48 radius, +0x4c height */
            float body[3];
            memcpy(body, a->b + ACT_POS, 12);
            hit |= party_push(body, spill32((long double)rf[0] * rf[1]),
                              spill32((long double)rf[0] * rf[2] + (long double)rf[0] * rf[2]));
            if ((type == 0x44 || type == 0x45) && PW(0x56daeb0u).i == 0) {
                static const size_t parts_s[4] = { 0x6fc, 0x708, 0x714, 0x720 };
                for (int k = 0; k < 4; k++) {
                    memcpy(body, a->b + parts_s[k], 12);
                    hit |= party_push(body, spill32((long double)rf[0] * rf[1] * k0p3),
                                      spill32((long double)rf[0] * rf[2] * k0p6));
                }
                static const size_t parts_l[2] = { 0x744, 0x750 };
                for (int k = 0; k < 2; k++) {
                    memcpy(body, a->b + parts_l[k], 12);
                    hit |= party_push(body, spill32((long double)rf[0] * rf[1]),
                                      spill32((long double)rf[0] * rf[2]));
                }
            }
            if (type == 0x3e) {
                memcpy(body, a->b + 0x6fc, 12);
                hit |= party_push(body, spill32((long double)rf[0] * rf[1]),
                                  spill32((long double)rf[0] * rf[2] + (long double)rf[0] * rf[2]));
            }
            if (type == 0x3f) {
                memcpy(body, a->b + 0x708, 12);
                hit |= party_push(body, spill32((long double)rf[0] * rf[1]),
                                  spill32((long double)rf[0] * rf[2] + (long double)rf[0] * rf[2]));
            }
        }
    }
    /* the stage objects: kinds 0 / 1 slow the walk near them (db06c); solid
     * kinds push */
    for (int n = 0; n < DOBJ_COUNT; n++) {
        const dobj_t *o = &g_dobj[n];
        const int32_t kind = o->i[DOBJ_KIND];
        if (kind == -1 || o->i[DOBJ_STATE] == 1 || o->i[DOBJ_STATE] == 3) continue;
        const int ki = (kind >= 0 && kind < 22) ? kind : 0;
        if (kind == 0 || kind == 1) {
            const float ddx = spill32((long double)o->f[DOBJ_POS + 0] - PX);
            const float ddz = spill32((long double)o->f[DOBJ_POS + 2] - PZ);
            const float d = spill32(crt_sqrt(spill64((long double)ddz * ddz + (long double)ddx * ddx)));
            const float rr = spill32((long double)o->f[DOBJ_SCALE + 0] * k_kind_r[ki]);
            if ((long double)rr * k3 > d) {
                const float t = spill32((long double)rr + rr);
                PW(0x56db06cu).f = d < t ? 0.0f : spill32(((long double)d - t) / rr);
            }
        }
        if (k_kind_solid[ki] != 0)
            hit |= party_push(&o->f[DOBJ_POS], spill32((long double)o->f[DOBJ_SCALE + 0] * k_kind_r[ki]),
                              spill32((long double)o->f[DOBJ_SCALE + 1] * k_kind_h[ki]));
    }
    if (hit) {
        const float vz = VZ, vx = VX;
        const float s = spill32(crt_sqrt(spill32((long double)vx * vx + (long double)vz * vz)));
        if (s > k0p05) {
            VX = spill32((long double)VX * k0p05 / s);
            VZ = spill32((long double)VZ * k0p05 / s);
        }
    }

    /* the try-move: the ground under pos + vel; a slope edge (types 1 / 2)
     * redirects the velocity along the crossed edge */
    const float x0 = PX, y0 = PY, z0 = PZ;
    mapcol_hit_t *h0 = &g_party.hit[0];
    int moved = 0;
    int r = party_try_move(x0, y0, z0, VX, VY, VZ, h0);
    if (r != 0) {
        int32_t ty = hit_type(h0->slot, h0->tri);
        if (r != 1 || (ty != 1 && ty != 2) || g_party.ground_y[0] > h0->height) {
            moved = 1;
        } else {
            party_try_move(x0, y0, z0, VX, VY, VZ, h0);
            const float seg[4] = { x0, z0, spill32((long double)VX + x0), spill32((long double)VZ + z0) };
            const mapcol_tri_t *t = &g_mapcol_slot[h0->slot].tris[h0->tri];
            float e[4];
            int k = 1;
            for (; k != 4; k++) {
                e[0] = -t->vx[k - 1]; e[1] = t->vz[k - 1];
                e[2] = -t->vx[k % 3]; e[3] = t->vz[k % 3];
                if (seg2_cross(seg, e)) break;
            }
            if (k != 4) {
                const float vz = VZ, vx = VX;
                const float vl = spill32(spill32(crt_sqrt(spill32((long double)vx * vx + (long double)vz * vz))));
                float ex = spill32((long double)e[0] - e[2]);
                float ez = spill32((long double)e[1] - e[3]);
                const float el = spill32(crt_sqrt(spill64((long double)ez * ez + (long double)ex * ex)));
                ex = spill32((long double)ex * vl / el);
                ez = spill32((long double)ez * vl / el);
                const float a1 = spill32((long double)spill32(crt_atan2(VX, VZ)) + k_pi);
                const float a2 = spill32((long double)spill32(crt_atan2(ex, ex)) + k_pi);   /* retail: atan2(ex, ex) */
                const float d = a1 >= a2 ? spill32((long double)a1 - a2) : spill32((long double)a2 - a1);
                if (d < k_pi2 || d > k_3pi2) { VZ = ez; VX = ex; }
                else { VZ = spill32(-(long double)ez); VX = spill32(-(long double)ex); }
                r = party_try_move(x0, y0, z0, VX, VY, VZ, h0);
                if (r == 2) {
                    moved = 1;
                } else if (r != 0) {
                    ty = hit_type(h0->slot, h0->tri);
                    int bounce = ty == 1 || ty == 2 ||
                                 (ty == 0 && spill32(fabsl((long double)h0->n[1])) < k0p05);
                    if (!bounce) {
                        moved = 1;
                    } else {
                        VZ = spill32((long double)VZ * km0p4);
                        VX = spill32((long double)VX * km0p4);
                        if (party_try_move(x0, y0, z0, VX, VY, VZ, h0) == 1) {
                            ty = hit_type(h0->slot, h0->tri);
                            if (ty != 1 && ty != 2) {
                                if (ty != 0 || !(spill32(fabsl((long double)h0->n[1])) < k0p05))
                                    moved = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    int airborne = 0;
    if (moved == 1) {
        if (PY > g_party.ground_y[0]) airborne = 1;
        if (floor_event_active(18)) VX = spill32((long double)VX + k0p005);
        if (PW(0x56db108u).i <= 0) {
            if (PW(0x56db0fcu).i > 0) {
                PX = spill32((long double)VX * k0p5 + PX);
                PZ = spill32((long double)VZ * k0p5 + PZ);
            } else {
                PX = spill32((long double)VX + PX);
                PZ = spill32((long double)VZ + PZ);
            }
        }
    }

    /* the radial wall push: 8 rays at +0.7 (20, stacked from +0.18, on a
     * house stage past z 0.7) */
    int nr = 8;
    if (mapcol_maptype() == 0 && PZ > k0p7) nr = 20;
    const float nf = spill32((long double)nr);
    for (int i = 0; i < nr; i++) {
        const float q = spill32((long double)(i % nr));
        const float ang = spill32((long double)q * k_2pi / nf + k_pi);
        const float rx = PX, rz = PZ;
        float ry = spill32((long double)PY + k0p7);
        if (i >= 8) {
            const float t = spill32((long double)(i / 8 + 1));
            ry = spill32((long double)t * k0p08 + PY + k0p1);
        }
        const double X = ang;
        mapcol_hit_t rh;
        rh.slot = 0;
        rh.tri = 0;
        const float s = spill32(x87_crt_sin(X));
        float dz = spill32((long double)spill32(x87_crt_cos(X)) * k1p05);
        if (mapcol_maptype() == 0 && dz > k0p1)
            dz = spill32((long double)spill32(x87_crt_cos(X)) * k1p03);
        float frac = 0.0f;
        if (mapcol_ray(rx, ry, rz, s, 0.0f, dz, &frac, &rh) != 0) {
            const int32_t ty = hit_type(rh.slot, rh.tri);
            if (ty == 1 || ty == 2 || ty == 7) {
                const float k = spill32((long double)k1 - frac);
                PX = spill32((long double)PX - (long double)k * s);
                PZ = spill32((long double)PZ - (long double)k * dz);
            }
            if (ty == 0 && spill32(fabsl((long double)rh.n[1])) < k0p75) {
                const float k = spill32((long double)k1 - frac);
                PX = spill32((long double)PX - (long double)k * s);
                PZ = spill32((long double)PZ - (long double)k * dz);
            }
        }
    }

    /* the ground of actors 0 and 1; actor 0 falls by its vy and lands */
    for (int i = 0; i < 2; i++) {
        if (g_party.chr[i] == -1) continue;
        g_party.ground_y[i] = km10;
        if (mapcol_ground(g_party.pos[i][0], g_party.pos[i][1], g_party.pos[i][2], &g_party.hit[i]) == 1) {
            g_party.ground_y2[i] = g_party.hit[i].height;
            g_party.ground_y[i] = g_party.hit[i].height;
            if (i != 0) continue;
            const int32_t ty = hit_type(h0->slot, h0->tri);
            PY = spill32((long double)VY + PY);
            if (PY < h0->height) PY = h0->height;
            if (PY == h0->height &&
                (spill32(fabsl((long double)h0->n[1])) < k0p5 || ty == 1 || ty == 2 || ty == 7)) {
                VX = spill32((long double)h0->n[0] * k0p1 + VX);
                VZ = spill32((long double)VZ - (long double)h0->n[2] * k0p1);
            }
        } else {
            g_party.ground_y2[i] = km30;
            g_party.ground_y[i] = km30;
            if (i == 0) PY = spill32((long double)VY + PY);
            if (g_party.pos[i][1] < g_party.ground_y[i]) g_party.pos[i][1] = g_party.ground_y[i];
        }
    }
    if (PY <= g_party.ground_y[0] ||
        (!(VY > k0) && (long double)g_party.ground_y[0] + k0p5 > PY)) {
        PY = g_party.ground_y[0];
        if (airborne == 1 && PW(0x56daeb0u).i > 0) audio_play_se_by_id(0x168);
        if (VY < k0) VY = 0.0f;
    }
}
