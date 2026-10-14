/*
 * actor_ai.c — see actor_ai.h.  Line refs are retail VAs.
 */
#include "actor_ai.h"

#include <math.h>
#include <string.h>

#include "actor_spawn.h"         /* DAT_007c8c5c */
#include "audio.h"               /* FUN_00499519 */
#include "dungeon_flow.h"        /* FUN_0043647f */
#include "floor_layout.h"        /* g_floor_rooms (the visited flags), DAT_0438c148 */
#include "map_collision.h"       /* FUN_00432e50, FUN_00433674 */
#include "party.h"
#include "rng.h"                 /* FUN_00471089, j__rand */
#include "scene1_chr_sprite.h"   /* FUN_00482a71 */
#include "scene1_particles_tick.h" /* g_scene1_camera_yaw (DAT_073de39c) */
#include "scene1_render.h"       /* the view / projection matrices (DAT_073de29c / 2dc) */
#include "scene1_spawn.h"        /* FUN_00447f4f */
#include "stage_gate.h"          /* FUN_0043195d */
#include "stage_palette.h"
#include "tables_enemy.h"        /* DAT_005c23f0 */
#include "x87.h"

int32_t g_popup[32][7];
float   g_view_proj[16], g_view_mat[16];
static float s_screen[6];        /* DAT_095d37b0..c4 */

static const float k0 = 0.0f, k0p5 = 0.5f, k1 = 1.0f, k2 = 2.0f, k5 = 5.0f;
static const float k0p02 = 0.019999999552965164f;      /* 0x5198dc */
static const float k0p03 = 0.029999999329447746f;      /* 0x519900 */
static const float k0p05 = 0.05000000074505806f;       /* 0x5198f8 */
static const float k0p1 = 0.10000000149011612f;        /* 0x5193a0 */
static const float k0p15 = 0.15000000596046448f;       /* 0x5198cc */
static const float k0p3 = 0.30000001192092896f;        /* 0x5194ec */
static const float k0p6 = 0.6000000238418579f;         /* 0x51969c */
static const float k0p8 = 0.800000011920929f;          /* 0x519470 */
static const float k0p01 = 0.009999999776482582f;      /* 0x5193a4 */
static const float k0p012 = 0.012000000104308128f;     /* 0x519b70 */
static const float k0p014 = 0.014000000432133675f;     /* 0x519b6c */
static const float k0p08b = 0.07999998331069946f;      /* 0x519b8c */
static const float k0p82 = 0.8199999928474426f;        /* 0x519b88 */
static const float k0p92 = 0.9200000166893005f;        /* 0x5198a4 */
static const float k0p95 = 0.949999988079071f;         /* 0x5198b4 */
static const float k0p98 = 0.9800000190734863f;        /* 0x5198ec */
static const float k1p3 = 1.2999999523162842f;         /* 0x519b90 */
static const float k12 = 12.0f, k20 = 20.0f, k24 = 24.0f, k40 = 40.0f, km40 = -40.0f;
static const float k300 = 300.0f;
static const float k_pi2 = 1.5707963705062866f;        /* 0x519434 */
static const float k_pi4 = 0.7853981852531433f;        /* 0x519394 */
static const float k_pi8 = 0.39269909262657166f;       /* 0x519b78 */
static const float k_2pi = 6.2831854820251465f;        /* 0x519398 */
static const float k4 = 4.0f, k8 = 8.0f;

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

/* raw actor fields */
#define AI(a, off)  (*(int32_t *)(void *)((a)->b + (off)))
#define AF(a, off)  (*(float *)(void *)((a)->b + (off)))
#define PX (g_party.pos[0][0])
#define PY (g_party.pos[0][1])
#define PZ (g_party.pos[0][2])

static const enemy_record_t *enemy_of(const actor_t *a)
{
    int32_t t = AI(a, ACT_TYPE);
    if (t < 0 || t >= ENEMY_COUNT) t = 0;
    return &g_enemy[t];
}

static int32_t hit_type(int32_t slot, int32_t tri)
{
    if (slot < 0 || slot >= MAPCOL_SLOTS || g_mapcol_slot[slot].tris == NULL ||
        tri < 0 || tri >= g_mapcol_slot[slot].cap)
        return 0;
    return g_mapcol_slot[slot].tris[tri].type;
}

void popup_add(float x, float y, float z, int32_t value, int32_t kind)
{
    for (int k = 0; k < 32; k++) {
        int32_t *p = g_popup[k];
        if (p[3] != 0) continue;
        memcpy(&p[0], &x, 4);
        memcpy(&p[1], &y, 4);
        memcpy(&p[2], &z, 4);
        p[3] = (kind == 1 || kind == 3) ? 90 : 60;
        p[6] = kind;
        p[4] = p[3];
        p[5] = value;
        return;
    }
}

/* FUN_0042df55: the stage's static spawn list DAT_0052c068 (100 × 40 bytes per
 * stage) — stages 0..19 carry none (entries -2 / -1). */
void actor_stage_spawns(void)
{
    if (g_actor_spawn_done == 0 && g_stage_b4dc >= 20) {
        /* PORT-DEBT(stub, FUN_0042df55): stages >= 20 index past the dungeon
         * rows into the next .data block (non-empty lists there) */
    }
    ++g_actor_spawn_done;
}

void actor_view_plane(void)
{
    static const float k320 = 320.0f, k240 = 240.0f, km240 = -240.0f;
#ifdef _WIN32
    memcpy(g_view_proj, scene1_render_proj_matrix(), sizeof g_view_proj);
    memcpy(g_view_mat, scene1_render_view_matrix(), sizeof g_view_mat);
#endif
    s_screen[4] = 1.0f;
    s_screen[5] = 1.0f;
    s_screen[2] = k320;
    s_screen[3] = k240;
    if (g_view_proj[11] == 0.0f) return;   /* host builds: no render matrices */
    s_screen[0] = spill32((long double)g_view_proj[0] * k320 / g_view_proj[11]);
    s_screen[1] = spill32((long double)g_view_proj[5] * km240 / g_view_proj[11]);
}

float actor_view_depth(float x, float y, float z)
{
    /* PORT-DEBT(render-coupled, FUN_00441c3e): the view matrix is the render's;
     * the dungeon camera is not ported, so the depths follow the house pose */
    return (float)(((long double)g_view_mat[2] * x + (long double)g_view_mat[6] * y) +
                   (long double)g_view_mat[10] * z + g_view_mat[14]);
}

/* FUN_0042c9b3: the slimes — wander (state 0: a random axis-or-player heading
 * re-rolled when blocked, a 40-tick creep, a rest of rand & 0x3f), then a
 * lunge (1) or, for type 4, a double hop (2) */
void actor_ai_slime(actor_t *a)
{
    int i = 0;
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        /* PORT-DEBT(stub, FUN_00445a8c): the slime's hit reaction (kind 11) */
    }
    actor_anim_set(a, 0);
    const int32_t st = AI(a, 0xa54);
    const int32_t type = AI(a, ACT_TYPE);
    if (st == 2) {
        const int32_t t = ++AI(a, 0xa58);
        if (t == 2) AF(a, 0x400) = k0p3;
        if (t == 17) AF(a, 0x400) = k0p3;
        if (t == 5) {
            /* PORT-DEBT(stub, FUN_00445a8c): the hop's hit record (kind 31) */
            audio_play_se_by_id(330);
        }
        if (AI(a, 0xa58) == 20) {
            /* PORT-DEBT(stub, FUN_00445a8c): kind 31 */
            audio_play_se_by_id(330);
        }
        if (AI(a, 0xa58) == 30) { AI(a, 0xa58) = 0; AI(a, 0xa54) = 0; }
    } else if (st == 1) {
        const int32_t t = ++AI(a, 0xa58);
        if (t >= 1) {
            const int32_t u = t - 1;
            if (u == 1) AF(a, 0x400) = k0p5;
            if (u >= 1 && u < 30) {
                const double h = AF(a, 0x420);
                AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p03 + AF(a, 0x3fc));
                AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * k0p03 + AF(a, 0x404));
            }
            if (u == (type == 3 ? 60 : 90)) { AI(a, 0xa58) = 0; AI(a, 0xa54) = 0; }
        }
    } else if (st == 0) {
        actor_anim_set(a, 0);
        if (++AI(a, 0xa58) == 1) {
            (void)rng_next_unit();
            float dx = spill32((long double)PX - AF(a, 0x3f0));
            float dz = spill32((long double)PZ - AF(a, 0x3f8));
            int32_t r = (int32_t)(rng_next15() & 0x3f);
            AI(a, 0xa60) = r;
            if (type == 3) AI(a, 0xa60) = r / 2;
            const float adx = spill32(fabsl((long double)dx));
            const float adz = spill32(fabsl((long double)dz));
            if (adx > adz) dz = 0.0f; else dx = 0.0f;
            for (; i != 20; i++) {
                const float q = spill32((long double)(rng_next15() & 3));
                AF(a, 0x420) = spill32((long double)q * k_pi2);
                if (rng_next15() & 1)
                    AF(a, 0x420) = spill32(crt_atan2(dx, dz));
                const double h = AF(a, 0x420);
                const float sx = spill32((long double)spill32(x87_crt_sin(h)) * k5);
                const float sz = spill32((long double)spill32(x87_crt_cos(h)) * k5);
                float frac = 0.0f;
                mapcol_hit_t rh;
                rh.slot = 0;
                rh.tri = 0;
                if (mapcol_ray(AF(a, 0x3f0), AF(a, 0x3f4), AF(a, 0x3f8), sx, 0.0f, sz, &frac, &rh) == 0)
                    break;
            }
            i = 0;
        }
        int32_t wait = 30;
        if (type == 1) wait = 15;
        if (type == 3) wait = 0;
        const int32_t t = AI(a, 0xa58);
        if (t >= wait) {
            const int32_t u = t - wait;
            if (AF(a, 0x3f4) == AF(a, 0x3e0)) {
                if (u == 2) AF(a, 0x400) = k0p3;
                if (u == 18) AF(a, 0x400) = k0p3;
                if (u == 34) AF(a, 0x400) = k0p3;
            }
            if (u >= 1 && u < 40) {
                float sp = k0p01;
                if (type == 1) sp = k0p012;
                if (type == 3) sp = k0p014;
                const double h = AF(a, 0x420);
                AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * sp + AF(a, 0x3fc));
                AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * sp + AF(a, 0x404));
            }
            if (u >= AI(a, 0xa60) + 30) {
                const int on_ground = AF(a, 0x3f4) == AF(a, 0x3e0);
                AI(a, 0xa58) = 0;
                if (on_ground) AI(a, 0xa54) = (type == 4) + 1;
            }
        }
    }
    if (type == 5) AI(a, 0xb54) = 1;
}

/* FUN_0042f28c */
void actor_ai_alive(actor_t *a)
{
    if (AI(a, 0xb40) == 1) {
        actor_anim_set(a, 0);
        /* PORT-DEBT(stub, FUN_0042c8a5): the knocked-back pose */
    } else {
        const int32_t sub = AI(a, 0xae4), type = AI(a, ACT_TYPE);
        if (sub == 1 && (type == 13 || type == 14)) {
            /* PORT-DEBT(stub, FUN_00428718) */
        } else if (sub == 2 && (type == 13 || type == 14)) {
            /* PORT-DEBT(stub, FUN_004284e1) */
        } else if (AI(a, 0x440) > 0 || AI(a, 0xb18) > 0) {
            if (AI(a, 0xa00) == 1) AF(a, 0x400) = spill32((long double)AF(a, 0x400) * 0.75f);
        } else if (type >= 0 && type <= 5) {
            actor_ai_slime(a);
        } else {
            /* PORT-DEBT(stub, FUN_0042f28c): the other types' AI (FUN_0042ce17,
             * FUN_00428f0a, FUN_00428ae4, FUN_0042a33e, FUN_004262b7,
             * FUN_0042a74c, … FUN_00428213 for the rest) */
        }
    }
    if (AI(a, 0xa00) != 0) {
        if (AI(a, 0x440) > 0 || AI(a, 0xb18) > 0)
            AF(a, 0x400) = spill32((long double)AF(a, 0x400) - k0p05);
    } else {
        const float vy = spill32((long double)AF(a, 0x400) - k0p05);
        AF(a, 0x400) = vy;
        const int32_t t = AI(a, ACT_TYPE);
        if (t != 61 && t != 64 && vy < -0.699999988079071f) AF(a, 0x400) = -0.699999988079071f;
    }
}

/* FUN_004304b3 */
void actor_physics(actor_t *a)
{
    const enemy_record_t *e = enemy_of(a);
    const float *rf = e->runtime_floats;   /* +0x44 scale, +0x48 radius, +0x4c height, … +0x5c wall reach */
    if (AI(a, 0xa98) == 0) {
        mapcol_hit_t *gh = (mapcol_hit_t *)(void *)(a->b + 0xa08);
        if (mapcol_ground(spill32((long double)AF(a, 0x3fc) + AF(a, 0x3f0)), AF(a, 0x3f4),
                          spill32((long double)AF(a, 0x3f8) + AF(a, 0x404)), gh) != 0) {
            AF(a, 0x3e0) = gh->height;
            const int32_t ty = hit_type(gh->slot, gh->tri);
            if (ty == 1 || (ty == 0 && spill32(fabsl((long double)gh->n[1])) < k0p05)) {
                AF(a, 0x3fc) = 0.0f;
                AF(a, 0x404) = 0.0f;
            }
            const float dx = spill32(((long double)AF(a, 0x3fc) + AF(a, 0x3f0)) - PX);
            const float dz = spill32(((long double)AF(a, 0x3f8) + AF(a, 0x404)) - PZ);
            float d = spill32((long double)dz * dz + (long double)dx * dx);
            if (d > k0) d = spill32(crt_sqrt(d));
            float dy = spill32(fabsl((long double)spill32((long double)AF(a, 0x3f4) - PY)));
            if (AI(a, 0xb04) == 0) {
                const float h = spill32((long double)rf[2] * rf[0] + (long double)rf[2] * rf[0]);
                if (dy < h) {
                    float r = spill32(x87_crt_cos(spill32((long double)dy * k_pi2 / h)));
                    r = spill32((long double)r * rf[1] * rf[0]);
                    if (r < k0) r = 0.0f;
                    if (d < r) {
                        AF(a, 0x3fc) = spill32((long double)AF(a, 0x3fc) * k0p3);
                        AF(a, 0x404) = spill32((long double)AF(a, 0x404) * k0p3);
                    }
                }
            }
        }
        float mx = 0.0f, mz = 0.0f;
        if (AI(a, 0xb14) <= 0) {
            mx = AF(a, 0x3fc);
            mz = AF(a, 0x404);
            if (floor_event_active(10) || floor_event_active(12)) {
                mx = spill32((long double)AF(a, 0x3fc) * k1p3);
                mz = spill32((long double)AF(a, 0x404) * k1p3);
            }
            long double vy = AF(a, 0x400);
            if (AI(a, 0x444) != 0) vy = vy * k0p1;
            AF(a, 0x3f4) = spill32(vy + AF(a, 0x3f4));
        }
        AF(a, 0x3f0) = spill32(((long double)mx + AF(a, 0x408)) + AF(a, 0x3f0));
        AF(a, 0x3f8) = spill32(((long double)AF(a, 0x410) + AF(a, 0x3f8)) + mz);
        AF(a, 0x408) = spill32((long double)AF(a, 0x408) * k0p95);
        AF(a, 0x410) = spill32((long double)AF(a, 0x410) * k0p95);
        if (AF(a, 0x3f4) < AF(a, 0x3e0)) {
            AF(a, 0x3f4) = AF(a, 0x3e0);
            AF(a, 0x400) = 0.0f;
        }
    }
    if (AI(a, 0xb98) == 1 && !(AF(a, 0x42c) > k0)) {
        /* a dead body slides to a stop */
        float sx = spill32((long double)AF(a, 0x3fc) * k0p08b);
        float sz = spill32((long double)AF(a, 0x404) * k0p08b);
        if (sx == k0 && sz == k0) {
            AF(a, 0x3fc) = 0.0f;
            AF(a, 0x404) = 0.0f;
        } else {
            const float l = spill32(crt_sqrt(spill64((long double)sz * sz + (long double)sx * sx)));
            if (l > k0p03) {
                sx = spill32((long double)sx * k0p03 / l);
                sz = spill32((long double)sz * k0p03 / l);
                AF(a, 0x3fc) = spill32((long double)AF(a, 0x3fc) - sx);
                AF(a, 0x404) = spill32((long double)AF(a, 0x404) - sz);
            } else {
                AF(a, 0x3fc) = spill32((long double)AF(a, 0x3fc) * k0p92);
                AF(a, 0x404) = spill32((long double)AF(a, 0x404) * k0p92);
            }
        }
        AF(a, 0x400) = spill32((long double)AF(a, 0x400) - k0p03);
    } else if (AI(a, 0x9f4) == 0 || AI(a, 0x440) > 0) {
        AF(a, 0x3fc) = spill32((long double)AF(a, 0x3fc) * k0p82);
        AF(a, 0x404) = spill32((long double)AF(a, 0x404) * k0p82);
    }
    AF(a, 0x400) = spill32((long double)AF(a, 0x400) * k0p98);
    /* the facing: 4 ways (8 for types 35 / 36, and 15 in its sub 1) */
    const float ang = spill32((long double)g_scene1_camera_yaw + AF(a, 0x420));
    const int32_t q = x87_ftol((((long double)ang + k_pi4) / k_2pi) * k4 + k4) & 3;
    if (q == 0) AI(a, 0x18) = 0;
    if (q == 1) AI(a, 0x18) = 2;
    if (q == 2) AI(a, 0x18) = 4;
    if (q == 3) AI(a, 0x18) = 6;
    const int32_t type = AI(a, ACT_TYPE);
    if (type == 35 || type == 36 || (type == 15 && AI(a, 0xae4) == 1))
        AI(a, 0x18) = x87_ftol((((long double)ang + k_pi8) / k_2pi) * k8 + k8) & 7;
    /* the wall push: 4 rays (reach rf[6]·scale·2) at +0.6, and at the ground
     * + 0.8 (at most 5 below) when flying */
    if (AI(a, 0x9fc) == 0) {
        const int passes = AI(a, 0xa00) != 0 ? 2 : 1;
        for (int p = 0; p < passes; p++) {
            float ry = spill32((long double)AF(a, 0x3f4) + k0p6);
            if (p == 1) {
                ry = spill32((long double)AF(a, 0x3e0) + k0p8);
                const float lo = spill32((long double)AF(a, 0x3f4) - k5);
                if (ry < lo) ry = lo;
            }
            for (int k = 0; k < 4; k++) {
                const float fk = spill32((long double)k);
                const float an = spill32((long double)fk * k_2pi / k4);
                const float rx = AF(a, 0x3f0), rz = AF(a, 0x3f8);
                const enemy_record_t *ee = enemy_of(a);
                const float reach = spill32((long double)ee->runtime_floats[6] * ee->runtime_floats[0] +
                                            (long double)ee->runtime_floats[6] * ee->runtime_floats[0]);
                const double ad = an;
                const float sx = spill32((long double)spill32(x87_crt_sin(ad)) * reach);
                const float sz = spill32((long double)spill32(x87_crt_cos(ad)) * reach);
                float frac = 0.0f;
                mapcol_hit_t rh;
                rh.slot = 0;
                rh.tri = 0;
                if (mapcol_ray(rx, ry, rz, sx, 0.0f, sz, &frac, &rh) != 0) {
                    const int32_t ty = hit_type(rh.slot, rh.tri);
                    if (ty == 0 || ty == 1 || ty == 2 || ty == 7) {
                        const float kk = spill32((long double)k1 - frac);
                        AF(a, 0x3f0) = spill32((long double)AF(a, 0x3f0) - (long double)kk * sx);
                        AF(a, 0x3f8) = spill32((long double)AF(a, 0x3f8) - (long double)kk * sz);
                    }
                }
            }
        }
    }
    /* the leash: at most 300 from home */
    if (AI(a, 0x9f8) == 0) {
        const float hx = AF(a, 0x414), hz = AF(a, 0x41c);
        const float dx = spill32((long double)hx - AF(a, 0x3f0));
        const float dz = spill32((long double)hz - AF(a, 0x3f8));
        float d = spill32((long double)dz * dz + (long double)dx * dx);
        if (d > k0) d = spill32(crt_sqrt(d));
        if (d > k300) {
            AF(a, 0x3f0) = spill32((long double)hx - (long double)dx * k300 / d);
            AF(a, 0x3f8) = spill32((long double)hz - (long double)dz * k300 / d);
        }
    }
    ++AI(a, 0x398);
}

/* FUN_00430c6d */
void actor_pass(void)
{
    actor_stage_spawns();                    /* FUN_0042df55 */
    actor_view_plane();                      /* FUN_0049065b */
    /* the respawn countdown (+0xa98 > 0) ticks only beyond 40 of the player */
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *a = &g_actor[i];
        if (AI(a, 0xad0) <= 0) continue;
        const float dx = spill32((long double)PX - AF(a, 0x3f0));
        const float dy = spill32((long double)PY - AF(a, 0x3f4));
        const float dz = spill32((long double)PZ - AF(a, 0x3f8));
        float d = spill32(((long double)dz * dz + (long double)dy * dy) + (long double)dx * dx);
        if (d > k0) d = spill32(crt_sqrt(d));
        if (d > k40) --AI(a, 0xad0);
        if (AI(a, 0xad0) == 0) {
            const float hp = AF(a, 0x434);
            AF(a, 0x42c) = hp;
            AF(a, 0x430) = hp;
            AI(a, 0xaf4) = 0;
            AI(a, 0x428) = 1;
            AI(a, 0xb8c) = 1;
            AI(a, 0xa54) = 0; AI(a, 0xa58) = 0; AI(a, 0xa5c) = 0; AI(a, 0xa60) = 0;
            AI(a, 0xa64) = 0; AI(a, 0xa68) = 0;
            AI(a, 0xaf0) = 0;
            AI(a, 0x440) = 0;
            AI(a, 0xb94) = 0;
            AI(a, 0x44c) = 0;
            AI(a, 0xb18) = 0;
            AI(a, 0x1c) = 0;
            AI(a, 0xb10) = 0;
            AI(a, 0xb14) = 0;
        }
    }
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *a = &g_actor[i];
        /* the HP bar eases to the HP (2 % of max per tick) */
        if (AF(a, 0x430) > AF(a, 0x42c)) {
            const float v = spill32((long double)AF(a, 0x430) - (long double)AF(a, 0x434) * k0p02);
            AF(a, 0x430) = v;
            if (v < AF(a, 0x42c)) AF(a, 0x430) = AF(a, 0x42c);
            ++AI(a, 0x438);
        } else if (AF(a, 0x430) < AF(a, 0x42c)) {
            const float v = spill32((long double)AF(a, 0x434) * k0p02 + AF(a, 0x430));
            AF(a, 0x430) = v;
            if (v > AF(a, 0x42c)) AF(a, 0x430) = AF(a, 0x42c);
        } else {
            AI(a, 0x438) = 0;
        }
        if (AI(a, ACT_INUSE) == 0) continue;
        const enemy_record_t *e = enemy_of(a);
        float dx = spill32((long double)PX - AF(a, 0x3f0));
        const float dy = spill32((long double)PY - AF(a, 0x3f4));
        const float dz0 = spill32((long double)PZ - AF(a, 0x3f8));
        const float dz2 = spill32((long double)dz0 * dz0);
        dx = spill32((long double)dx * dx);
        float d3 = spill32(((long double)dy * dy + dx) + dz2);
        if (d3 > k0) d3 = spill32(crt_sqrt(d3));
        float d2 = spill32((long double)dx + dz2);
        if (d2 > k0) d2 = spill32(crt_sqrt(d2));
        (void)d3;
        if (AI(a, 0xb18) > 0) --AI(a, 0xb18);
        if (AI(a, 0xafc) > 0) { --AI(a, 0xafc); continue; }
        /* the sleep / wake state +0xaf4 by the player's distance */
        if (g_floor_c148 == 1 || stage_gate_floor_is_checkpoint() || e->flags == 1 ||
            AI(a, 0xacc) == 1 || AI(a, ACT_TYPE) == 0x22 || AI(a, ACT_INUSE) == 2) {
            AI(a, 0xaf4) = 0;
        } else {
            if (AI(a, 0xaf4) == 1 && d2 < k12) AI(a, 0xaf4) = 0;
            if (AI(a, 0xaf4) == 2 && !(d2 > k24)) AI(a, 0xaf4) = 0;
            if (AI(a, 0xaf4) == 0 && d2 > k24) AI(a, 0xaf4) = 2;
        }
        /* the room's visited flag: the actor's cell seen on the auto-map */
        if (AI(a, 0x43c) == 0) {
            const int32_t cx = x87_ftol(((long double)AF(a, 0x3f0) - k20) / km40);
            const int32_t cz = x87_ftol(((long double)AF(a, 0x3f8) - k20) / km40);
            if (cx >= 0 && cz >= 0 && cz < 15 && cx < 15 &&
                g_floor_rooms[cz * FLOOR_GRID_STRIDE + cx].pad != 0)
                AI(a, 0x43c) = 1;
        }
        if (AI(a, 0xb40) == 0 && AI(a, 0xaf4) > 0) continue;
        {
            const float f = spill32((long double)AF(a, 0xaf8) + k0p05);
            AF(a, 0xaf8) = f;
            if (f > k1) AF(a, 0xaf8) = 1.0f;
        }
        if (AI(a, 0xb14) > 0) --AI(a, 0xb14);
        if (AI(a, 0xb38) > 0) --AI(a, 0xb38);
        if (AI(a, 0xb10) > 0) {
            AI(a, 0x20) = 1;
            --AI(a, 0xb10);
            if (AI(a, 0xb10) % 20 == 0) {
                const float z = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + AF(a, 0x3f8));
                const float y = spill32((long double)AF(a, 0x3f4) + k2);
                const float x = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + AF(a, 0x3f0));
                scene1_spawn(0, x, y, z, 59, k0p15, 1);
            }
            if (AI(a, 0xb10) % 60 == 0 && AF(a, 0x42c) > k0) {
                float p = spill32((long double)AF(a, 0x434) * k0p1);
                if (p < k1) p = 1.0f;
                if (p > k20) p = 20.0f;
                (void)p;
                /* PORT-DEBT(stub, FUN_0042e791): the poison tick (ftol(p), 4) */
            }
        } else {
            AI(a, 0x20) = 0;
        }
        const int32_t type = AI(a, ACT_TYPE);
        if (type != 63 && type != 62 && type != 66 && type != 65) {
            if (e->flags == 0) {
                chr_anim_tick((int32_t *)(void *)a->b, e->unknown_24, e->unknown_28);
            } else {
                long double f = AF(a, 0x2c);
                if (AI(a, 0x30) == 0) {
                    const float v = spill32(f - k0p1);
                    AF(a, 0x2c) = v;
                    if (v < k0) AF(a, 0x2c) = 0.0f;
                } else {
                    const float v = spill32(f + k0p1);
                    AF(a, 0x2c) = v;
                    if (v > k1) AF(a, 0x2c) = 1.0f;
                }
                for (int k = 0; k < 8; k++)
                    AF(a, 0x54 + 4 * k) = spill32((long double)AF(a, 0x54 + 4 * k) + k1);
            }
            if (AI(a, 0x28) > 0) {
                const int32_t v = AI(a, 0x28) + 1;
                AI(a, 0x28) = v;
                if (v > 40) AI(a, 0x28) = 0;
            }
            if (AI(a, 0xba0) > 0) --AI(a, 0xba0);
            if (AI(a, 0x1c) > 0) --AI(a, 0x1c);
            if (AI(a, 0x44c) > 0) --AI(a, 0x44c);
            if (AI(a, 0x440) > 0) --AI(a, 0x440);
            if (AI(a, 0xb94) > 0 && --AI(a, 0xb94) == 0) {
                AI(a, 0x1c) = 20;
                const int32_t heal = AI(a, 0xb90);
                if (heal >= 0) {
                    const float h = spill32((long double)spill32((long double)heal) + AF(a, 0x42c));
                    AF(a, 0x42c) = h;
                    if (h > AF(a, 0x434)) AF(a, 0x42c) = AF(a, 0x434);
                    popup_add(AF(a, 0x3f0), spill32((long double)AF(a, 0x3f4) + k0p5), AF(a, 0x3f8), heal, 1);
                }
            }
            if (AI(a, 0xba0) == 0 && AI(a, ACT_INUSE) == 1 && AF(a, 0x42c) == k0) {
                /* PORT-DEBT(stub, FUN_0042e1a6): the death (EXP, drops, the kill count) */
            }
            if (AI(a, ACT_INUSE) == 2) {
                /* PORT-DEBT(stub, FUN_0042f7ad): the dying actor */
            } else if (AI(a, ACT_INUSE) == 1) {
                actor_ai_alive(a);           /* FUN_0042f28c */
            }
            actor_physics(a);                /* FUN_004304b3 */
            continue;
        }
        /* PORT-DEBT(stub, FUN_00430c6d): the golem parts (types 62 / 63 / 65 / 66) */
    }
    /* the head points and the view depths */
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *a = &g_actor[i];
        AF(a, 0x450) = 0.0f;
        if (AI(a, ACT_INUSE) == 0) continue;
        const int32_t t = AI(a, ACT_TYPE);
        if (t < 0) continue;
        const enemy_record_t *e = &g_enemy[t];
        AF(a, 0x3e4) = AF(a, 0x3f0);
        AF(a, 0x3e8) = spill32((long double)e->runtime_floats[2] * e->runtime_floats[0] + AF(a, 0x3f4));
        AF(a, 0x3ec) = AF(a, 0x3f8);
        AF(a, 0x450) = actor_view_depth(AF(a, 0x3f0), AF(a, 0x3f4), AF(a, 0x3f8));
    }
    /* the falls: below the stage floor (or in water on a -1000 stage), a
     * non-boss dies */
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *a = &g_actor[i];
        if (AI(a, ACT_INUSE) != 1) continue;
        const uint8_t *st = (const uint8_t *)g_stage_palette;
        int32_t fall_y = 0;
        if (st) memcpy(&fall_y, st + 0x1b1c, 4);
        int water = fall_y == -1000 && AI(a, 0xa24) == 1;
        if (AI(a, 0xa98) != 0) continue;
        const float fy = spill32((long double)fall_y);
        if (!(AF(a, 0x3f4) < fy) && !water) continue;
        if (enemy_of(a)->flags != 0) continue;
        audio_play_se_by_id(319);
        AF(a, 0x42c) = 0.0f;
        AI(a, 0xadc) = 0;
        AI(a, 0xaf0) = 0;
        AI(a, ACT_INUSE) = 2;
    }
}
