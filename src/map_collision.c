/*
 * map_collision.c — see map_collision.h.  Line refs are retail VAs (the
 * disassembly; the IDA decompile drops the x87 compare operands).
 */
#include "map_collision.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "floor_layout.h"    /* g_floor_rooms (DAT_073e03ac), g_floor_c148 */
#include "math3d.h"          /* the D3DX8 SSE mirrors */
#include "stage_palette.h"   /* g_stage_palette (DAT_068dd2f0) */
#include "x87.h"

_Static_assert(sizeof(mapcol_tri_t) == 0x98, "mapcol_tri_t is FUN_00432ac6's 0x98-byte record");
_Static_assert(offsetof(mapcol_tri_t, vx) == 0x18 && offsetof(mapcol_tri_t, ex) == 0x48 &&
               offsetof(mapcol_tri_t, bmin) == 0x78 && offsetof(mapcol_tri_t, type) == 0x90,
               "mapcol_tri_t field offsets");
_Static_assert(sizeof(mapcol_objs_t) == 0x198, "DAT_0438bfb0..0x438c147");
_Static_assert(offsetof(mapcol_movers_t, slot) == 0x190 && offsetof(mapcol_movers_t, x) == 0x320 &&
               offsetof(mapcol_movers_t, z) == 0x4b0, "DAT_0438c150/c2e0/c470/c600");
_Static_assert(offsetof(mapcol_hit_t, flag) == 0x1c && offsetof(mapcol_hit_t, plane) == 0x34 &&
               sizeof(mapcol_hit_t) == 0x44, "FUN_00432e50 output layout");

mapcol_slot_t   g_mapcol_slot[MAPCOL_SLOTS];
mapcol_objs_t   g_mapcol_objs;
mapcol_movers_t g_mapcol_movers;
int32_t         g_mapcol_wall_pass;

/* retail .rdata constants (m32 operands) */
static const float k20 = 20.0f, km20 = -20.0f, k40 = 40.0f, km40 = -40.0f;
static const float k0p2 = 0.2f, k0p5 = 0.5f, k1 = 1.0f, k1p5 = 1.5f, k3 = 3.0f, k5 = 5.0f, k10 = 10.0f;
static const float k0p001 = 0.001f, km10000 = -10000.0f, k1000 = 1000.0f;

int32_t mapcol_maptype(void)
{
    return g_stage_palette ? g_stage_palette->mode : 0;
}

/* ───── FUN_00471d45 work areas ─────────────────────────────────────────── */

typedef struct { int32_t v[3]; int32_t type; uint8_t mat[4]; } mc_face_t;   /* DAT_04348754, 0x14 */
static float     s_vpool[MAPCOL_MAX_VERTS][3];   /* DAT_0432a754 (x, y, z) */
static mc_face_t s_faces[MAPCOL_MAX_TRIS];
/* Copy A (DAT_00946434 + slot·0x2f8020): only the x/z vertex floats the slot AABB
 * reads.  It persists across builds like retail's: FUN_0043289b's AABB loop runs
 * over [0, all faces), and the records past the kept (non type-4) ones are
 * whatever an earlier build of the slot left there (zero before the first). */
static float   (*s_copya[MAPCOL_SLOTS])[6];
static int32_t   s_copya_cap[MAPCOL_SLOTS];

void mapcol_clear_all(void)
{
    for (int i = 0; i < MAPCOL_SLOTS; i++) g_mapcol_slot[i].count = 0;
    for (int i = 0; i < MAPCOL_MAX_TRIS; i++) s_faces[i].type = 0;
}

/* MSVC text-mode fgets over a memory image: CRLF → LF, 0x1a ends the file,
 * at most n-1 chars, the newline kept. */
typedef struct { const unsigned char *p, *end; } mc_stream_t;
static char *mc_fgets(char *buf, int n, mc_stream_t *s)
{
    int i = 0;
    while (i < n - 1 && s->p < s->end) {
        unsigned char c = *s->p;
        if (c == 0x1a) { s->end = s->p; break; }
        s->p++;
        if (c == '\r' && s->p < s->end && *s->p == '\n') { s->p++; c = '\n'; }
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    buf[i] = 0;
    return buf;
}

/* CRT atoi 0x503d03 */
static int32_t mc_atoi(const char *p)
{
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    int neg = 0;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10u + (uint32_t)(*p++ - '0');
    return neg ? -(int32_t)v : (int32_t)v;
}

/* CRT atof 0x503c2b, then `fstp m32` */
static float mc_atof(const char *p) { return spill32(strtod(p, NULL)); }

/* scan to `c` and step past it, staying inside the 512-byte line buffer
 * (retail's scan is unbounded; the corpus always has the delimiter) */
static const char *mc_past(const char *p, const char *end, char c)
{
    while (p < end && *p != c) p++;
    return p < end ? p + 1 : p;
}

/* FUN_00471d45's material keyword chain (state 18).  `gake` stores edx, which
 * still holds the 1 loaded at the top of the line loop (0x471dd6). */
static int32_t mc_classify(const char *q)
{
    static const struct { const char *kw; int32_t type; } chain[] = {
        { "mizu", 5 }, { "gake", 1 }, { "toumei", 7 }, { "kabe", 7 }, { "yuki_", 6 },
        { "dame", 2 }, { "Plane", 2 }, { "hit", 3 }, { "hikari", 4 }, { "nohit", 4 },
        { "crystal", 15 }, { "taimatu", 16 }, { "takara", 13 }, { "taru", 12 },
        { "shokudai", 14 }, { "tree01", 8 }, { "tree02", 9 }, { "kusa01", 10 }, { "kusa02", 11 },
    };
    for (size_t i = 0; i < sizeof chain / sizeof chain[0]; i++)
        if (!strncmp(q, chain[i].kw, strlen(chain[i].kw))) return chain[i].type;
    return 0;
}

/* FUN_00432ac6: one triangle record from vertex-pool indices a (A), b (B), c (C). */
static void mc_tri_build(mapcol_tri_t *t, int32_t a, int32_t b, int32_t c, int large_pad)
{
    /* retail indexes the pool unchecked; out-of-range faces read vertex 0 here */
    if ((uint32_t)a >= MAPCOL_MAX_VERTS) a = 0;
    if ((uint32_t)b >= MAPCOL_MAX_VERTS) b = 0;
    if ((uint32_t)c >= MAPCOL_MAX_VERTS) c = 0;
    const float ax = spill32(-(long double)s_vpool[a][0]), ay = s_vpool[a][1], az = s_vpool[a][2];
    const float cx = spill32(-(long double)s_vpool[c][0]), cy = s_vpool[c][1], cz = s_vpool[c][2];
    const float bx = spill32(-(long double)s_vpool[b][0]), by = s_vpool[b][1], bz = s_vpool[b][2];
    t->vx[0] = ax; t->vy[0] = ay; t->vz[0] = az;
    t->vx[2] = cx; t->vy[2] = cy; t->vz[2] = cz;
    t->vx[1] = bx; t->vy[1] = by; t->vz[1] = bz;

    /* 0x432b74: min/max, A then B then C (strict compares) */
    t->bmin[0] = ax; if (ax > bx) t->bmin[0] = bx; if (t->bmin[0] > cx) t->bmin[0] = cx;
    t->bmax[0] = ax; if (ax < bx) t->bmax[0] = bx; if (t->bmax[0] < cx) t->bmax[0] = cx;
    t->bmin[1] = ay; if (ay > by) t->bmin[1] = by; if (t->bmin[1] > cy) t->bmin[1] = cy;
    t->bmax[1] = ay; if (ay < by) t->bmax[1] = by; if (t->bmax[1] < cy) t->bmax[1] = cy;
    t->bmin[2] = az; if (az > bz) t->bmin[2] = bz; if (t->bmin[2] > cz) t->bmin[2] = cz;
    t->bmax[2] = az; if (az < bz) t->bmax[2] = bz; if (t->bmax[2] < cz) t->bmax[2] = cz;
    if (large_pad) {   /* maptype of stage DAT_0438b4dc >= 1 */
        t->bmin[0] = spill32((long double)t->bmin[0] - k1);
        t->bmax[0] = spill32((long double)t->bmax[0] + k1);
        t->bmin[1] = spill32((long double)t->bmin[1] - k5);
        t->bmax[1] = spill32((long double)t->bmax[1] + k10);
        t->bmin[2] = spill32((long double)t->bmin[2] - k1);
        t->bmax[2] = spill32((long double)t->bmax[2] + k1);
    } else {
        t->bmin[0] = spill32((long double)t->bmin[0] - k0p5);
        t->bmax[0] = spill32((long double)t->bmax[0] + k0p5);
        t->bmin[1] = spill32((long double)t->bmin[1] - k0p5);
        t->bmax[1] = spill32((long double)t->bmax[1] + k3);
        t->bmin[2] = spill32((long double)t->bmin[2] - k0p5);
        t->bmax[2] = spill32((long double)t->bmax[2] + k0p5);
    }

    /* 0x432d61: edges spill to m32 before the cross products */
    const float ex0 = spill32((long double)bx - ax), ey0 = spill32((long double)by - ay), ez0 = spill32((long double)bz - az);
    const float ex1 = spill32((long double)cx - bx), ey1 = spill32((long double)cy - by), ez1 = spill32((long double)cz - bz);
    t->ex[0] = ex0; t->ey[0] = ey0; t->ez[0] = ez0;
    t->ex[1] = ex1; t->ey[1] = ey1; t->ez[1] = ez1;
    t->ex[2] = spill32((long double)ax - cx);
    t->ey[2] = spill32((long double)ay - cy);
    t->ez[2] = spill32((long double)az - cz);

    const float nx = spill32((long double)ey1 * ez0 - (long double)ez1 * ey0);
    const float ny = spill32((long double)ez1 * ex0 - (long double)ex1 * ez0);
    const float nz = spill32((long double)ex1 * ey0 - (long double)ey1 * ex0);
    t->plane[0] = nx; t->plane[1] = ny; t->plane[2] = nz;
    t->plane[3] = spill32(-(((long double)nz * az + (long double)ny * ay) + (long double)nx * ax));
    t->nlen2 = spill32(((long double)nz * nz + (long double)ny * ny) + (long double)nx * nx);
}

static void mc_reserve(int slot, int32_t ntri, int32_t nfaces)
{
    mapcol_slot_t *s = &g_mapcol_slot[slot];
    if (ntri > s->cap) {
        s->tris = (mapcol_tri_t *)realloc(s->tris, (size_t)ntri * sizeof *s->tris);
        s->cap = ntri;
    }
    int32_t need = nfaces > 0 ? nfaces : 1;   /* record 0 seeds the AABB */
    if (need > s_copya_cap[slot]) {
        s_copya[slot] = (float (*)[6])realloc(s_copya[slot], (size_t)need * sizeof *s_copya[slot]);
        memset(s_copya[slot] + s_copya_cap[slot], 0, (size_t)(need - s_copya_cap[slot]) * sizeof *s_copya[slot]);
        s_copya_cap[slot] = need;
    }
}

/* FUN_0043289b(0, nfaces, slot) */
static void mc_build_slot(int slot, int32_t nfaces, int large_pad)
{
    mapcol_slot_t *s = &g_mapcol_slot[slot];
    int32_t kept = 0;
    for (int32_t i = 0; i < nfaces; i++) kept += s_faces[i].type != 4;
    mc_reserve(slot, kept, nfaces);
    s->count = 0;
    for (int32_t i = 0; i < nfaces; i++) {
        const mc_face_t *f = &s_faces[i];
        if (f->type == 4) continue;
        mapcol_tri_t *t = &s->tris[s->count];
        mc_tri_build(t, f->v[0], f->v[1], f->v[2], large_pad);
        t->type = f->type;
        t->_pad94 = 0;
        float *a = s_copya[slot][s->count];
        a[0] = t->vx[0]; a[1] = t->vx[1]; a[2] = t->vx[2];
        a[3] = t->vz[0]; a[4] = t->vz[1]; a[5] = t->vz[2];
        s->count++;
    }
    s->count2 = kept;
    float (*a)[6] = s_copya[slot];
    s->aabb[0] = s->aabb[3] = a[0][0];
    s->aabb[2] = s->aabb[5] = a[0][3];
    for (int32_t i = 0; i < nfaces; i++) {
        for (int k = 0; k < 3; k++) if (s->aabb[0] > a[i][k]) s->aabb[0] = a[i][k];
        for (int k = 0; k < 3; k++) if (s->aabb[3] < a[i][k]) s->aabb[3] = a[i][k];
        for (int k = 3; k < 6; k++) if (s->aabb[2] > a[i][k]) s->aabb[2] = a[i][k];
        for (int k = 3; k < 6; k++) if (s->aabb[5] < a[i][k]) s->aabb[5] = a[i][k];
    }
}

int mapcol_load_x(int slot, const void *text, size_t len, int large_pad)
{
    if (slot < 0 || slot >= MAPCOL_SLOTS) return 0;
    for (int i = 0; i < MAPCOL_MAX_TRIS; i++) s_faces[i].type = 0;

    int st = -1;                                       /* [ebp-0x8] */
    int32_t nverts = 0, vread = 0, vtotal = 0;         /* -0x38, -0x20, -0x30 */
    int32_t nfaces = 0, fcnt = 0, ftotal = 0, vbase = 0; /* -0x18, -0xc, -0x24, -0x28 */
    int32_t fbase = 0, mlst = 0, mfaces = 0, mcount = 0, mmax = 0; /* -0x1c, -0x14, -0x34, -0x4, -0x10 */
    float top[16] = { 0 }, child[16] = { 0 }, cur[16];  /* -0x84, -0xc4, -0x1dc */
    int32_t types[50] = { 0 };                          /* -0x19c (0xc8 bytes to the next local) */
    mat4_identity(cur);

    mc_stream_t s = { (const unsigned char *)text, (const unsigned char *)text + len };
    char line[513];                   /* [512] stays NUL: a scan that runs off the line reads "" */
    const char *end = line + 512;
    line[512] = 0;
    while (mc_fgets(line, 512, &s)) {
        if (line[0] == '\r' || line[0] == '\n') continue;
        const char *p = line;
        while (*p == ' ') p++;
        if (st == -1) { if (!strncmp(p, "Frame ", 6)) st = 0; continue; }
        if (st == 0) { st = 1; continue; }
        if (st >= 1 && st <= 4) {             /* the root FrameTransformMatrix rows */
            for (int k = 0; k < 4; k++) {
                top[(st - 1) * 4 + k] = mc_atof(p);
                if (k != 3) p = mc_past(p, end, ',');
            }
            st++;
            continue;
        }
        if (st == 5) {
            if (!strncmp(p, "Frame ", 6)) { st = 6; mat4_identity(cur); }
            continue;
        }
        if (st == 6) { st = 7; continue; }
        if (st >= 7 && st <= 10) {            /* a child frame's rows: cur = child × cur */
            for (int k = 0; k < 4; k++) {
                child[(st - 7) * 4 + k] = mc_atof(p);
                if (k != 3) p = mc_past(p, end, ',');
            }
            if (++st == 11) mat4_mul(cur, child, cur);
            continue;
        }
        switch (st) {
        case 11:
            if (!strncmp(p, "Mesh ", 5)) st = 12;
            if (!strncmp(p, "Frame ", 6)) st = 6;
            break;
        case 12:                              /* vertex count; cur = cur × root, per Mesh */
            nverts = mc_atoi(p);
            vread = 0;
            st = 13;
            mat4_mul(cur, cur, top);
            break;
        case 13: {                            /* "x;y;z;," → D3DXVec3Transform × 0.2f */
            float v[3], o[4];
            for (int k = 0; k < 3; k++) { v[k] = mc_atof(p); p = mc_past(p, end, ';'); }
            vec3_transform(o, v, cur);
            if (vtotal < MAPCOL_MAX_VERTS) {
                s_vpool[vtotal][0] = spill32((long double)o[0] * k0p2);
                s_vpool[vtotal][1] = spill32((long double)o[1] * k0p2);
                s_vpool[vtotal][2] = spill32((long double)o[2] * k0p2);
            }
            vtotal++;
            if (++vread == nverts) st = 14;
            break;
        }
        case 14:
            nfaces = mc_atoi(p);
            if (nfaces > 0) { fcnt = 0; st = 15; g_mapcol_slot[slot].count2 = nfaces; }
            break;
        case 15:                              /* "3;a,b,c;," */
            p = mc_past(p, end, ';');
            for (int k = 0; k < 3; k++) {
                int32_t v = mc_atoi(p) + vbase;
                if (ftotal < MAPCOL_MAX_TRIS) s_faces[ftotal].v[k] = v;
                if (k != 2) p = mc_past(p, end, ',');
            }
            fcnt++;
            ftotal++;
            if (fcnt >= nfaces) { fcnt = 0; vbase += vread; st = 16; }
            break;
        case 16:
            st = 17;
            mlst = 0;
            break;
        case 17: {                            /* MeshMaterialList: count; nindices; indices */
            int32_t fi = fbase + fcnt, done = 0;
            do {
                int32_t val = mc_atoi(p);
                while (p < end && *p != ';' && *p != ',' && *p != '\r' && *p != '\n') p++;
                if (p >= end) break;          /* retail would read stale buffer bytes */
                const char c = p[1];
                p++;
                if (c == '\r' || c == '\n' || c == ' ') done = 1;
                if (mlst == 0) {
                    mlst = 1;
                } else if (mlst == 1) {
                    mlst = 2;
                    mfaces = val;
                    mcount = 0;
                    mmax = 0;
                    for (int32_t j = 0; j < nfaces; j++)
                        if (fbase + j < MAPCOL_MAX_TRIS) s_faces[fbase + j].type = 0;
                } else {
                    if (mmax < val) mmax = val;
                    fcnt++;
                    if (fi < MAPCOL_MAX_TRIS) s_faces[fi].type = val;
                    fi++;
                    if (fcnt >= mfaces) done = 2;
                }
            } while (done == 0);
            if (done == 2) st = 18;
            break;
        }
        case 18: {                            /* "{MaterialName}" per material index */
            const char c = *p;
            if (c == 0 || c == '\r' || c == '\n') break;
            const char *q = p;
            if (c != '{') { do q++; while (q < end && *q != '{'); }
            q = q < end ? q + 1 : q;
            if (mcount < 50) types[mcount] = mc_classify(q);
            mcount++;
            if (mmax != 0) { mmax--; break; }
            for (int32_t j = 0; j < nfaces; j++) {
                mc_face_t *f = &s_faces[fbase + j];
                if (fbase + j < MAPCOL_MAX_TRIS) f->type = (f->type >= 0 && f->type < 50) ? types[f->type] : 0;
            }
            fbase += nfaces;
            fcnt = 0;
            st = 5;
            break;
        }
        default:                              /* 0x4727c2 (no state reaches it) */
            mc_atoi(p);
            if (++fcnt >= nfaces) { fcnt = 0; st = 5; }
            break;
        }
    }
    mc_build_slot(slot, ftotal < MAPCOL_MAX_TRIS ? ftotal : MAPCOL_MAX_TRIS, large_pad);
    return 1;
}

/* ───── the queries ─────────────────────────────────────────────────────── */

/* The probe's floor cell: (v − 20) / −40 truncated, clamped to 0..14. */
static int mc_cell(float x, float z)
{
    int cx = x87_ftol(((long double)x - k20) / km40);
    int cz = x87_ftol(((long double)z - k20) / km40);
    if (cx < 0) cx = 0;
    if (cx >= 14) cx = 14;
    if (cz < 0) cz = 0;
    if (cz >= 14) cz = 14;
    return cz * FLOOR_GRID_STRIDE + cx;
}

static float mc_wrap(float v)
{
    while (v > k20) v = spill32((long double)v - k40);
    while (v < km20) v = spill32((long double)v + k40);
    return v;
}

/* the winning triangle's normal: cross of its edges AB, BC; unit, y negated */
static void mc_normal(const mapcol_tri_t *t, mapcol_hit_t *out)
{
    const float n0 = spill32((long double)t->ey[0] * t->ez[1] - (long double)t->ey[1] * t->ez[0]);
    const float n1 = spill32((long double)t->ex[1] * t->ez[0] - (long double)t->ez[1] * t->ex[0]);
    const float n2 = spill32((long double)t->ey[1] * t->ex[0] - (long double)t->ey[0] * t->ex[1]);
    out->n[0] = n0; out->n[1] = n1; out->n[2] = n2;
    const float len2 = spill32(((long double)n2 * n2 + (long double)n1 * n1) + (long double)n0 * n0);
    const float len = spill32(sqrt((double)len2));   /* CRT sqrt 0x5031e4 */
    out->n[0] = spill32((long double)out->n[0] / len);
    out->n[1] = spill32(-((long double)out->n[1] / len));
    out->n[2] = spill32((long double)out->n[2] / len);
}

/* RotY(−rot) about the object origin: T(p)·R·T(−p), as the D3DX calls build it */
static void mc_unrotate(float rot, float *lx, float *ly, float *lz)
{
    float m[16], r[16], t[16];
    mat4_translation(m, -*lx, -*ly, -*lz);
    mat4_rotation_y(r, -rot);
    mat4_mul(m, r, m);
    mat4_translation(t, *lx, *ly, *lz);
    mat4_mul(m, t, m);
    *lx = spill32((long double)m[12] + *lx);
    *ly = spill32((long double)m[13] + *ly);
    *lz = spill32((long double)m[14] + *lz);
}

static int mc_excluded(int32_t type)   /* 15, 8..11, 16, 12..14 */
{
    return type == 15 || (type >= 8 && type <= 14) || type == 16;
}

static int mc_edges_xz(const mapcol_tri_t *t, float lx, float lz)
{
    for (int j = 0; j < 3; j++)
        if (((long double)lz - t->vz[j]) * t->ex[j] - ((long double)lx - t->vx[j]) * t->ez[j] < 0) return 0;
    return 1;
}

int mapcol_ground(float a1, float a2, float a3, mapcol_hit_t *out)
{
    float off_x = 0.0f, off_z = 0.0f;                  /* [ebp-0x24], [ebp-0x20] */
    const floor_room_t *cell = &g_floor_rooms[mc_cell(a1, a3)];
    int32_t obj = cell->type, slot = cell->type;       /* [ebp-0x1c], [ebp-0x10] */
    if (mapcol_maptype() >= 1 && g_floor_c148 == 0) {
        a1 = mc_wrap(a1);
        a3 = mc_wrap(a3);
        float t;
        switch (cell->pat) {
        case 1: t = a1; a1 = a3; a3 = -t; break;
        case 2: a3 = -a3; a1 = -a1; break;
        case 3: t = a1; a1 = -a3; a3 = t; break;
        default: break;
        }
    }

    for (int pass = 0;; ) {                            /* [ebp-0xc] */
        float best = km10000;                          /* [ebp-0x28] */
        int32_t best_slot = 0, best_tri = 0;           /* [ebp-0x34], [ebp-0x14] */
        out->flag = 0;
        const int32_t maptype = mapcol_maptype();
        int32_t nobj = 1;
        if (maptype == 0) nobj = g_mapcol_objs.n1 + g_mapcol_objs.n2;
        else if (g_mapcol_movers.on[0] > 0) nobj = g_mapcol_movers.on[0] + 1;

        for (int32_t i = 0; i < nobj; i++) {
            const int32_t mt = mapcol_maptype();
            if (mt == 0) {
                if (i >= g_mapcol_objs.n1) { obj = i - g_mapcol_objs.n1 + 5; slot = g_mapcol_objs.slot[obj]; }
                else { obj = i; slot = g_mapcol_objs.slot[i]; }
            } else if (i > 0) {
                slot = obj = g_mapcol_movers.slot[i];
            } else if (g_floor_c148 == 1) {
                slot = g_floor_rooms[FLOOR_GRID_STRIDE + 1].type;   /* DAT_073e04ac */
                obj = 0;
            }   /* else: obj/slot as left (the cell's, or the last pass's last object) */
            float yoff = 0.0f;                         /* [ebp-0x38] */
            float lx = spill32(((long double)a1 - g_mapcol_objs.x[obj]) + off_x);
            float ly = spill32((long double)a2 - g_mapcol_objs.y[obj]);
            float lz = spill32(((long double)a3 - g_mapcol_objs.z[obj]) + off_z);
            if (mt > 0) {
                if (i == 0) {
                    if (g_floor_c148 == 1) {
                        lx = spill32((long double)lx + k40);
                        lz = spill32((long double)lz + k40);
                    }
                } else {
                    if (g_mapcol_movers.on[i] == 0) continue;
                    if (g_mapcol_movers.on[0] > 10 &&
                        ((long double)g_mapcol_movers.x[i] - k3 > a1 || (long double)g_mapcol_movers.x[i] + k3 < a1 ||
                         (long double)g_mapcol_movers.z[i] - k3 > a3 || (long double)g_mapcol_movers.z[i] + k3 < a3))
                        continue;
                    lx = spill32((long double)a1 - g_mapcol_movers.x[i]);
                    ly = spill32((long double)a2 - g_mapcol_movers.y[i]);
                    lz = spill32((long double)a3 - g_mapcol_movers.z[i]);
                    yoff = g_mapcol_movers.y[i];
                }
            }
            mc_unrotate(g_mapcol_objs.rot[obj], &lx, &ly, &lz);
            ly = spill32((long double)ly + k1p5);

            const mapcol_slot_t *s = &g_mapcol_slot[slot];
            if (s->aabb[0] > lx || s->aabb[3] < lx || s->aabb[2] > lz || s->aabb[5] < lz) continue;
            for (int32_t k = 0; k < s->count; k++) {
                const mapcol_tri_t *t = &s->tris[k];
                if (t->bmin[0] > lx || t->bmax[0] < lx || t->bmin[1] > ly || t->bmax[1] < ly ||
                    t->bmin[2] > lz || t->bmax[2] < lz)
                    continue;
                if (g_mapcol_wall_pass == 0 && t->type == 7) continue;
                if (mc_excluded(t->type)) continue;
                if (t->type == 5 || t->type == 6) {
                    const float sd = spill32(((((long double)ly + k1p5) * t->plane[1] + (long double)lz * t->plane[2]) +
                                              (long double)lx * t->plane[0]) + t->plane[3]);
                    if (!(sd > 0.0f) || !mc_edges_xz(t, lx, lz)) continue;
                    if (t->plane[1] != 0.0f)
                        out->slope_h = spill32(-((((long double)lz * t->plane[2] + (long double)lx * t->plane[0]) +
                                                  t->plane[3]) / t->plane[1]));
                    if (t->type == 5) { if ((long double)ly - k1p5 < out->slope_h) out->flag = 1; }
                    else out->flag = 2;
                    continue;
                }
                const float sd = spill32((((long double)lz * t->plane[2] + (long double)ly * t->plane[1]) +
                                          (long double)lx * t->plane[0]) + t->plane[3]);
                if (!(sd > 0.0f) || !mc_edges_xz(t, lx, lz)) continue;
                if (t->plane[1] == 0.0f) continue;
                const float h = spill32((long double)yoff - (((long double)lz * t->plane[2] + (long double)lx * t->plane[0]) +
                                                              t->plane[3]) / t->plane[1]);
                if (best < h) { best = h; best_slot = slot; best_tri = k; }
            }
        }

        /* 0x43352a: pass 0 needs ground within 5 below; passes 1 and 2 are discarded;
         * pass 3 (= pass 0's offsets) and pass 4 take any ground */
        int ok = 0;
        if (pass == 0) {
            if ((long double)a2 - k5 < best) ok = 1;
            else { pass = 1; off_x = k0p001; off_z = 0.0f; }
        } else {
            pass++;
            if (pass == 2) { off_x = k0p001; off_z = k0p001; }
            else if (pass == 3) { off_x = 0.0f; off_z = 0.0f; }
            else if (best != km10000) ok = 1;
            else if (pass != 4) return 0;
            else { off_x = k0p001; off_z = k0p001; }
        }
        if (ok) {
            out->slot = best_slot;
            out->height = best;
            out->tri = best_tri;
            const mapcol_tri_t *t = &g_mapcol_slot[best_slot].tris[best_tri];
            mc_normal(t, out);
            for (int k = 0; k < 4; k++) out->plane[k] = t->plane[k];
            return 1;
        }
    }
}

static float mc_abs(float v) { return v >= 0.0f ? v : -v; }   /* fcomp 0; jae keep */

int mapcol_ray(float px, float py, float pz, float dx, float dy, float dz, float *frac, mapcol_hit_t *out)
{
    const floor_room_t *cell = &g_floor_rooms[mc_cell(px, pz)];
    const int32_t maptype = mapcol_maptype();
    int32_t obj = cell->type, slot = cell->type;       /* ebx, [ebp-0x1c] */
    if (maptype >= 1 && g_floor_c148 == 0) {
        float q = spill32((long double)(x87_ftol(px) / 40));
        px = spill32((long double)px - (long double)q * k40);
        q = spill32((long double)(x87_ftol(pz) / 40));
        pz = spill32((long double)pz - (long double)q * k40);
        px = mc_wrap(px);
        pz = mc_wrap(pz);
        float t;
        switch (cell->pat) {
        case 1: t = px; px = pz; pz = -t; t = dx; dx = dz; dz = -t; break;
        case 2: pz = -pz; dz = -dz; px = -px; dx = -dx; break;
        case 3: t = px; px = -pz; pz = t; t = dx; dx = -dz; dz = t; break;
        default: break;
        }
    }

    float best = k1000;                                /* [ebp-0x8] */
    int32_t best_slot = 0, best_tri = 0, best_obj = 0; /* -0x40, -0x30, -0xc */
    int32_t nobj = 1, house_extra = 0;                 /* -0x24, -0x3c */
    if (maptype == 0) nobj = g_mapcol_objs.n1 + g_mapcol_objs.n2;
    else if (g_mapcol_movers.on[0] > 0) nobj = g_mapcol_movers.on[0] + 1;
    const float sdx = dx, sdy = dy, sdz = dz;          /* -0x48, -0x34, -0x50 */
    if (nobj == 0) return 0;

    for (int32_t i = 0; i < nobj; i++) {
        const int32_t mt = mapcol_maptype();
        if (mt == 0) {
            if (i >= 1) house_extra = 1;
            if (i >= g_mapcol_objs.n1) { obj = i - g_mapcol_objs.n1 + 5; slot = g_mapcol_objs.slot[obj]; }
            else { obj = i; slot = g_mapcol_objs.slot[i]; }
        } else if (i > 0) {
            slot = obj = g_mapcol_movers.slot[i];
            if (g_mapcol_movers.on[0] > 10 &&
                ((long double)g_mapcol_movers.x[i] - k3 > px || (long double)g_mapcol_movers.x[i] + k3 < px ||
                 (long double)g_mapcol_movers.z[i] - k3 > pz || (long double)g_mapcol_movers.z[i] + k3 < pz))
                continue;
        } else if (g_floor_c148 == 1) {
            slot = g_floor_rooms[FLOOR_GRID_STRIDE + 1].type;
            obj = 0;
        }
        float lx = spill32((long double)px - g_mapcol_objs.x[obj]);
        float ly = spill32((long double)py - g_mapcol_objs.y[obj]);
        float lz = spill32((long double)pz - g_mapcol_objs.z[obj]);
        if (mt > 0) {
            if (i == 0) {
                if (g_floor_c148 == 1) {
                    lx = spill32((long double)lx + k40);
                    lz = spill32((long double)lz + k40);
                }
            } else {
                if (g_mapcol_movers.on[i] == 0) continue;
                lx = spill32((long double)px - g_mapcol_movers.x[i]);
                ly = spill32((long double)py - g_mapcol_movers.y[i]);
                lz = spill32((long double)pz - g_mapcol_movers.z[i]);
            }
        }
        float tdx = sdx, tdy = sdy, tdz = sdz;
        if (g_mapcol_objs.rot[obj] != 0.0f) {         /* p·R and d·R through the D3DX calls */
            float m[16], r[16], t[16];
            mat4_translation(m, 0.0f, 0.0f, 0.0f);
            mat4_rotation_y(r, -g_mapcol_objs.rot[obj]);
            mat4_mul(m, r, m);
            mat4_translation(t, lx, ly, lz);
            mat4_mul(m, t, m);
            lx = m[12]; ly = m[13]; lz = m[14];
            mat4_translation(m, 0.0f, 0.0f, 0.0f);
            mat4_rotation_y(r, -g_mapcol_objs.rot[obj]);
            mat4_mul(m, r, m);
            mat4_translation(t, sdx, sdy, sdz);
            mat4_mul(m, t, m);
            tdx = m[12]; tdy = m[13]; tdz = m[14];
        }
        const float ax = mc_abs(tdx), ay = mc_abs(tdy), az = mc_abs(tdz);
        const float xhi = spill32((long double)ax + lx), xlo = spill32((long double)lx - ax);
        const float yhi = spill32((long double)ay + ly), ylo = spill32((long double)ly - ay);
        const float zhi = spill32((long double)az + lz), zlo = spill32((long double)lz - az);

        const mapcol_slot_t *s = &g_mapcol_slot[slot];
        if (s->aabb[0] > xhi || s->aabb[3] < xlo || s->aabb[2] > zhi || s->aabb[5] < zlo) continue;
        for (int32_t k = 0; k < s->count; k++) {
            const mapcol_tri_t *t = &s->tris[k];
            if (g_mapcol_wall_pass == 0 && t->type == 7) continue;
            if (t->bmin[0] > xhi || t->bmax[0] < xlo || t->bmin[2] > zhi || t->bmax[2] < zlo ||
                t->bmin[1] > yhi || t->bmax[1] < ylo)
                continue;
            if (!house_extra && (t->type == 5 || t->type == 6 || mc_excluded(t->type))) continue;
            const float sd = spill32((((long double)lx * t->plane[0] + (long double)lz * t->plane[2]) +
                                      (long double)ly * t->plane[1]) + t->plane[3]);
            if (sd < 0.0f) continue;
            if (((long double)tdx * t->plane[0] + (long double)tdy * t->plane[1]) + (long double)tdz * t->plane[2] >= 0)
                continue;
            int j = 0;
            for (; j < 3; j++) {
                const long double e =
                    ((long double)tdy * t->ez[j] - (long double)tdz * t->ey[j]) * ((long double)lx - t->vx[j]) +
                    ((long double)tdx * t->ey[j] - (long double)tdy * t->ex[j]) * ((long double)lz - t->vz[j]) +
                    ((long double)tdz * t->ex[j] - (long double)tdx * t->ez[j]) * ((long double)ly - t->vy[j]);
                if (e < 0) break;
            }
            if (j != 3) continue;
            const float fr = spill32(-((long double)sd / ((((long double)tdy * t->plane[1]) + (long double)tdz * t->plane[2]) +
                                                          (long double)tdx * t->plane[0])));
            if (fr > k1) continue;
            if (!(best > fr)) continue;
            best = fr; best_slot = slot; best_tri = k; best_obj = i;
        }
    }
    if (best == k1000) return 0;
    *frac = best;
    out->tri = best_tri;
    out->slot = best_slot;
    out->obj = best_obj;
    const mapcol_tri_t *t = &g_mapcol_slot[best_slot].tris[best_tri];
    mc_normal(t, out);
    for (int k = 0; k < 4; k++) out->plane[k] = t->plane[0];
    return 1;
}
