/*
 * model_anchor.c — the model enemies' anchor points, a render-side state write.
 *
 * Retail draws every in-use enemy whose type has a model (enemy record +0x20
 * == 1) in FUN_00459dfd's two actor passes (FUN_004552d0: alpha +0x3dc 255;
 * FUN_00456f56: the rest, unless +0xaf4 > 0).  FUN_00456d48 builds the
 * model's root matrices (scale·rotY, translation), runs the animation on the
 * frame tree (FUN_00403d79 → FUN_0040150e per animated frame, the actor's two
 * anim slots' per-track times +0x74 / +0x204 cross-faded by +0x2c) and
 * FUN_00404870, which updates the frames (FUN_00403e33) and writes, for each of
 * the model's named points (model.txt, up to 10), the frame's combined
 * translation to +0x6fc + 12·i, its matrix to +0x774 + 64·i and a ground query
 * (FUN_00432e50) to +0x454 + 68·i.  The AIs read those points (the crab's claw
 * hits, the golem parts).  The frame trees: `xfile_parse_hier` (the DX8 SDK
 * SkinnedMesh loader FUN_00402551, files xfile2/…).
 */
#include "model_anchor.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor_table.h"     /* g_actor, ACT_* */
#include "map_collision.h"   /* FUN_00432e50 */
#include "math3d.h"          /* the D3DX matrix helpers */
#include "tables_enemy.h"    /* DAT_005c23f0 */
#include "tables_model.h"    /* DAT_073ae258: the anchor names */
#include "x87.h"
#include "xfile.h"

#define MODEL_FILES 19

/* FUN_00473c15: the model per index (DAT_073da2f0 + 0x2f0·n) */
static const char *const k_model_file[MODEL_FILES] = {
    "xfile2/g_lat/g_lat_06.x",          "xfile2/g_lat/g_lat_07.x",
    "xfile2/g_lat/g_lat_08.x",          "xfile2/golem_g/golem_g01.x",
    "xfile2/golem_g/golem_g02.x",       "xfile2/golem_g/golem_g03.x",
    "xfile2/d_golem_g/golem_g01.x",     "xfile2/d_golem_g/golem_g02.x",
    "xfile2/d_golem_g/golem_g03.x",     "xfile2/boss_omu/omu_boss_01.x",
    "xfile2/kani/kani01.x",             "xfile2/d_kani/kani01.x",
    "xfile2/kurage/kurage_01.x",        "xfile2/d_kurage/kurage_01.x",
    "xfile2/maou/maou_02.X",            "xfile2/crystal_gorem/cyg_body.X",
    "xfile2/crystal_gorem/cyg_core.X",  "xfile2/crystal_gorem/cyg_r.X",
    "xfile2/crystal_gorem/cyg_l.X",
};

static xfile_hier *s_hier[MODEL_FILES];
static int s_tried[MODEL_FILES];
static model_anchor_reader_fn s_reader;

void model_anchor_set_reader(model_anchor_reader_fn fn) { s_reader = fn; }

/* The trees are loaded once: a draw rewrites every animated frame's matRot
 * from the actor's times and the root from the actor, so retail's reload at
 * each stage load (FUN_00473c15) leaves the same state. */
static xfile_hier *model_hier(int n)
{
    if (n < 0 || n >= MODEL_FILES) return NULL;
    if (!s_tried[n] && s_reader) {
        s_tried[n] = 1;
        void *buf = NULL;
        const size_t len = s_reader(k_model_file[n], &buf);
        if (len) s_hier[n] = xfile_parse_hier((const char *)buf, len);
        free(buf);
    }
    return s_hier[n];
}

/* ── x87 helpers (CW 0x027F, as the game runs) ─────────────────────────── */
static long double fsin_(long double x)
{
    long double r;
    __asm__ volatile("fsin" : "=t"(r) : "0"(x));
    return r;
}
static long double fsqrt_(long double x)
{
    long double r;
    __asm__ volatile("fsqrt" : "=t"(r) : "0"(x));
    return r;
}
static long double fpatan_(long double y, long double x)
{
    long double r;
    __asm__ volatile("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

/* FUN_00404b81 (D3DXVec3Lerp, inline): (b − a)·t + a per lane */
static void vec3_lerp_(float out[3], const float a[3], const float b[3], float t)
{
    for (int i = 0; i < 3; i++) {
        const float v = spill32(((long double)b[i] - a[i]) * t + a[i]);
        out[i] = v;
    }
}

/* D3DXQuaternionSlerp c_ 0x4a4ba5 (slot 35: the SSE2 table keeps it) */
static void quat_slerp_(float out[4], const float q1[4], const float q2[4], float t)
{
    static const float k_eps = 1.1920928955078125e-07f;   /* 0x51c6ec */
    long double s0 = 1.0L - t;
    long double dot = (long double)q1[0] * q2[0];
    dot = dot + (long double)q2[2] * q1[2];
    dot = dot + (long double)q2[1] * q1[1];
    dot = dot + (long double)q2[3] * q1[3];
    const float sign = dot < 0.0L ? -1.0f : 1.0f;
    const long double c = (long double)sign * dot;
    const float cf = spill32(c);
    long double s1;
    if ((1.0L - c) > (long double)k_eps) {
        const long double sn = fsqrt_(1.0L - c * c);
        const float th = spill32(fpatan_(sn, cf));
        const float inv = spill32(1.0L / sn);
        s0 = fsin_((long double)th * s0) * inv;
        s1 = fsin_((long double)th * t) * inv;
    } else {
        s1 = t;
    }
    s1 = s1 * sign;
    const float x = spill32(s0 * q1[0] + s1 * q2[0]);
    const float y = spill32(s1 * q2[1] + s0 * q1[1]);
    const float z = spill32(s1 * q2[2] + s0 * q1[2]);
    const float w = spill32(s1 * q2[3] + s0 * q1[3]);
    out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}

/* D3DXMatrixRotationQuaternion c_ 0x4a3849 (slot 38) */
static void mat4_rotation_quat_(float m[16], const float q[4])
{
    static const float k2 = 2.0f;
    const float xs = spill32((long double)q[0] * k2);
    const float ys = spill32((long double)q[1] * k2);
    const long double zs = (long double)k2 * q[2];
    const long double wx = (long double)xs * q[3];
    const long double wy = (long double)ys * q[3];
    const long double wz = zs * q[3];
    const float xx = spill32((long double)xs * q[0]);
    const float xy = spill32((long double)ys * q[0]);
    const float xz = spill32(zs * q[0]);
    const float yy = spill32((long double)ys * q[1]);
    const float yz = spill32(zs * q[1]);
    const long double zz = zs * q[2];
    m[0] = spill32((1.0L - yy) - zz);
    m[1] = spill32((long double)xy + wz);
    m[2] = spill32((long double)xz - wy);
    m[3] = 0.0f;
    m[4] = spill32((long double)xy - wz);
    const long double oxx = 1.0L - xx;
    const float oxxf = spill32(oxx);
    m[5] = spill32(oxx - zz);
    m[6] = spill32((long double)yz + wx);
    m[7] = 0.0f;
    m[8] = spill32((long double)xz + wy);
    m[9] = spill32((long double)yz - wx);
    m[10] = spill32((long double)oxxf - yy);
    m[11] = 0.0f;
    m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
}

/* FUN_0040150e's key search: the first key after t (else key 0), the one
 * before it, and the fraction between them */
static float key_frac(uint32_t t_lo, uint32_t t_hi, float t)
{
    const float k1 = spill32((long double)t_lo), k2 = spill32((long double)t_hi);
    const float d = spill32((long double)k2 - k1);
    if (d == 0.0f) return 0.0f;
    return spill32(((long double)t - k1) / d);
}

#define KEY_SEARCH(keys, n, tf, i2, i3) do { \
    (i2) = (i3) = 0; \
    for (uint32_t k_ = 0; k_ < (n); k_++) { \
        if (spill32((long double)(keys)[k_].t) > (tf)) { (i3) = k_; (i2) = k_ ? k_ - 1 : 0; break; } \
    } \
} while (0)

/* FUN_0040150e: one animated frame at the two slot times, cross-faded */
static void sframe_set_time(xfile_sframe *f, float t0, float t1, float blend)
{
    xfile_sframe *to = f->to_animate;
    if (to == NULL) return;                  /* retail writes through NULL */
    const float tt[2] = { t0, t1 };
    if (f->matk) {                           /* the nearer matrix key at slot 0's time */
        const float tf = (float)fmod((double)t0, (double)f->matk[f->nmat - 1].t);
        uint32_t i2, i3;
        KEY_SEARCH(f->matk, f->nmat, tf, i2, i3);
        const float fr = key_frac(f->matk[i2].t, f->matk[i3].t, tf);
        memcpy(to->rot, f->matk[(double)fr > 0.5 ? i3 : i2].m, 64);
        return;
    }
    float res[16], m[16];
    mat4_identity(res);
    int any = 0;
    if (f->scale) {
        float v[2][3];
        for (int s = 0; s < 2; s++) {
            const float tf = (float)fmod((double)tt[s], (double)f->scale[f->nscale - 1].t);
            uint32_t i2, i3;
            KEY_SEARCH(f->scale, f->nscale, tf, i2, i3);
            vec3_lerp_(v[s], f->scale[i2].v, f->scale[i3].v, key_frac(f->scale[i2].t, f->scale[i3].t, tf));
        }
        vec3_lerp_(v[0], v[0], v[1], blend);
        mat4_scaling(m, v[0][0], v[0][1], v[0][2]);
        mat4_mul(res, res, m);
        any = 1;
    }
    if (f->rotk) {
        float q[2][4];
        for (int s = 0; s < 2; s++) {
            const float tf = (float)fmod((double)tt[s], (double)f->rotk[f->nrot - 1].t);
            uint32_t i2, i3;
            KEY_SEARCH(f->rotk, f->nrot, tf, i2, i3);
            quat_slerp_(q[s], f->rotk[i2].q, f->rotk[i3].q, key_frac(f->rotk[i2].t, f->rotk[i3].t, tf));
            q[s][3] = -q[s][3];
        }
        float qq[4];
        quat_slerp_(qq, q[0], q[1], blend);
        mat4_rotation_quat_(m, qq);
        mat4_mul(res, res, m);
        any = 1;
    }
    if (f->pos) {
        float v[2][3];
        for (int s = 0; s < 2; s++) {
            const float tf = (float)fmod((double)tt[s], (double)f->pos[f->npos - 1].t);
            uint32_t i2, i3;
            KEY_SEARCH(f->pos, f->npos, tf, i2, i3);
            vec3_lerp_(v[s], f->pos[i2].v, f->pos[i3].v, key_frac(f->pos[i2].t, f->pos[i3].t, tf));
        }
        vec3_lerp_(v[0], v[0], v[1], blend);
        mat4_translation(m, v[0][0], v[0][1], v[0][2]);
        mat4_mul(res, res, m);
    } else {
        mat4_translation(m, to->rot_orig[12], to->rot_orig[13], to->rot_orig[14]);
        mat4_mul(res, res, m);
        if (!any) return;
    }
    memcpy(to->rot, res, 64);
}

/* FUN_00403d79: per animated frame k (the list order, capped at 99) the
 * slots' track times (+0x74[k] / +0x204[k], base 0) ·4.8 */
static void hier_set_times(xfile_hier *h, const int32_t *tr0, const int32_t *tr1, float blend)
{
    static const float k4p8 = 4.8000001907348633f;   /* 0x519340 */
    int k = 0;
    for (xfile_sframe *f = h->anim_head; f; f = f->anim_next) {
        const float a = spill32((long double)spill32((long double)(uint32_t)tr0[k]) * k4p8);
        const float b = spill32((long double)spill32((long double)(uint32_t)tr1[k]) * k4p8);
        sframe_set_time(f, a, b, blend);
        if (++k > 99) k = 99;
    }
}

/* FUN_00403e33: combined = matRot · parent, then · matTrans; the children */
static void sframe_update(xfile_sframe *f, const float cur[16])
{
    memcpy(f->combined, cur, 64);
    mat4_mul(f->combined, f->rot, f->combined);
    mat4_mul(f->combined, f->combined, f->trans);
    for (xfile_sframe *c = f->child; c; c = c->sibling)
        sframe_update(c, f->combined);
}

/* FUN_00456d48's pose: the model's root Scale(-s, s, s) then RotY(π - yaw),
 * T(pos); the frames at the actor's two slots' track times (+0x74 / +0x204,
 * cross-faded by +0x2c); FUN_00404870's frame update */
xfile_hier *model_anchor_pose(actor_t *a, int *model_out)
{
    static const float k0p04 = 0.03999999910593033f;  /* 0x5198c4 */
    static const float k_pi = 3.1415927410125732f;    /* 0x51943c */
    const int32_t type = act_geti(a, ACT_TYPE);
    if (type < 0 || type >= ENEMY_COUNT) return NULL;
    const enemy_record_t *e = &g_enemy[type];
    const int model = e->unknown_24;
    if (model_out) *model_out = model;
    xfile_hier *h = model_hier(model);
    if (h == NULL) return NULL;
    const float s = spill32((long double)e->runtime_floats[0] * k0p04);
    float tr[16], sc[16], ry[16], rot[16];
    mat4_translation(tr, act_getf(a, 0x3f0), act_getf(a, 0x3f4), act_getf(a, 0x3f8));
    mat4_scaling(sc, -s, s, s);
    mat4_rotation_y(ry, spill32((long double)k_pi - act_getf(a, 0x420)));
    mat4_mul(rot, ry, sc);
    hier_set_times(h, (const int32_t *)(const void *)(a->b + 0x74),
                   (const int32_t *)(const void *)(a->b + 0x204), act_getf(a, 0x2c));
    /* FUN_00404870 */
    memcpy(h->root->rot, rot, 64);
    memcpy(h->root->trans, tr, 64);
    float id[16];
    mat4_identity(id);
    sframe_update(h->root, id);
    return h;
}

const char *model_anchor_file(int model)
{
    return (model >= 0 && model < MODEL_FILES) ? k_model_file[model] : NULL;
}

/* FUN_00456d48's state half + FUN_00404870 */
static void model_actor_draw(actor_t *a)
{
    int model = -1;
    xfile_hier *h = model_anchor_pose(a, &model);
    if (h == NULL) return;
    if (model < 0 || model >= MODEL_DEF_COUNT) return;
    const tables_model_t *md = &g_models[model];
    for (int i = 0; i < 10; i++) {
        if (md->used[i] == 0) continue;
        char name[MODEL_DEF_NAME_MAX + 1];
        memcpy(name, md->point[i], MODEL_DEF_NAME_MAX);
        name[MODEL_DEF_NAME_MAX] = '\0';
        const xfile_sframe *f = xfile_find_frame(h->root, name);
        if (f == NULL) continue;
        const float x = f->combined[12], y = f->combined[13], z = f->combined[14];
        act_setf(a, 0x6fc + 12 * i, x);
        act_setf(a, 0x700 + 12 * i, y);
        act_setf(a, 0x704 + 12 * i, z);
        memcpy(a->b + 0x774 + 64 * i, f->combined, 64);
        mapcol_ground(x, y, z, (mapcol_hit_t *)(void *)(a->b + 0x454 + 68 * i));
    }
}

xfile_hier *model_hier_load(const char *path)
{
    if (s_reader == NULL || path == NULL) return NULL;
    void *buf = NULL;
    const size_t len = s_reader(path, &buf);
    xfile_hier *h = len ? xfile_parse_hier((const char *)buf, len) : NULL;
    free(buf);
    return h;
}

void model_hier_pose(xfile_hier *h, const int32_t *tr0, const int32_t *tr1, float blend,
                     const float rot[16], const float trans[16])
{
    if (h == NULL || h->root == NULL) return;
    hier_set_times(h, tr0, tr1, blend);
    memcpy(h->root->rot, rot, 64);
    memcpy(h->root->trans, trans, 64);
    float id[16];
    mat4_identity(id);
    sframe_update(h->root, id);
}

/* FUN_00459dfd's actor passes: FUN_004552d0 (opaque, +0x3dc 255), then
 * FUN_00456f56 (the rest, unless +0xaf4 > 0) */
void model_anchor_draw_pass(void)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < ACTOR_COUNT; i++) {
            actor_t *a = &g_actor[i];
            if (act_geti(a, ACT_INUSE) == 0) continue;
            const int32_t type = act_geti(a, ACT_TYPE);
            if (type < 0 || type >= ENEMY_COUNT || g_enemy[type].flags != 1) continue;
            const int opaque = act_geti(a, 0x3dc) == 255;
            if (pass == 0 ? !opaque : (opaque || act_geti(a, 0xaf4) > 0)) continue;
            model_actor_draw(a);
        }
    }
}
