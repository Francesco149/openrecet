/*
 * actor_ai.c — see actor_ai.h.  Line refs are retail VAs.
 */
#include "actor_ai.h"

#include <stdio.h>

#include <math.h>
#include <string.h>

#include "actor_spawn.h"         /* DAT_007c8c5c */
#include "audio.h"               /* FUN_00499519 */
#include "dungeon_flow.h"        /* FUN_0043647f */
#include "floor_layout.h"        /* g_floor_rooms (the visited flags), DAT_0438c148 */
#include "map_collision.h"
#include "math3d.h"                /* vec3_transform (D3DXVec3Transform) */       /* FUN_00432e50, FUN_00433674 */
#include "dungeon_party.h"        /* FUN_00482ae7 */
#include "scene1_records.h"
#include "party.h"
#include "rng.h"                 /* FUN_00471089, j__rand */
#include "scene1_camera.h"       /* FUN_0044b219 */
#include "scene1_chr_sprite.h"   /* FUN_00482a71 */
#include "scene1_combat_sm.h"    /* DAT_0438b4c8 */
#include "scene1_per_frame_open.h" /* FUN_0041331d */
#include "scene1_records_b_spawn.h" /* FUN_00445a8c */
#include "scene1_particles_tick.h" /* g_scene1_camera_yaw (DAT_073de39c) */
#include "save_work.h"
#include "scene1_overlay.h"      /* FUN_00414345 */
#include "scene1_render.h"       /* the view / projection matrices (DAT_073de29c / 2dc) */
#include "audio_mci.h"          /* FUN_00451874 — the debug text rows */
#include "scene1_spawn.h"        /* FUN_00447f4f */
#include "sim.h"                 /* FUN_004532bc */
#include "stage_gate.h"          /* FUN_0043195d */
#include "stage_post_load.h"     /* DAT_0438b4d0 */
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

/* FUN_00490c78 → FUN_00490d29 (+ FUN_00490cc6): the screen x of a world
 * point through the last frame's view DAT_073de29c and projection
 * DAT_073de2dc — 320 − (proj·(1,1,1)).x · 640 / 2 · v.x / v.z */
float actor_screen_x(float x, float y, float z)
{
    static const float k640 = 640.0f, k2 = 2.0f, k320 = 320.0f;
#ifdef _WIN32
    const float *view = scene1_render_view_matrix();
    const float *proj = scene1_render_proj_matrix();
#else
    static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const float *view = ident, *proj = ident;
#endif
    static const float one[3] = { 1.0f, 1.0f, 1.0f };
    float n[4], v[4];
    vec3_transform(n, one, proj);                      /* FUN_004a2227 */
    const float fx = spill32((long double)n[0] * k640 / k2);
    const float p[3] = { x, y, z };
    vec3_transform(v, p, view);                        /* FUN_004a22c5: w 1 (affine) */
    const float vx = spill32((long double)v[0] / v[3]);
    const float vz = spill32((long double)v[2] / v[3]);
    return spill32((long double)k320 - (long double)fx * vx / vz);
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
        scene1_record_b_spawn_npc(a, 11, 1);     /* FUN_00445a8c: the contact record */
    }
    actor_anim_set(a, 0);
    const int32_t st = AI(a, 0xa54);
    const int32_t type = AI(a, ACT_TYPE);
    if (st == 2) {
        const int32_t t = ++AI(a, 0xa58);
        if (t == 2) AF(a, 0x400) = k0p3;
        if (t == 17) AF(a, 0x400) = k0p3;
        if (t == 5) {
            scene1_record_b_spawn_npc(a, 31, 1);  /* FUN_00445a8c: the shot */
            audio_play_se_by_id(330);
        }
        if (AI(a, 0xa58) == 20) {
            scene1_record_b_spawn_npc(a, 31, 1);
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

/* the stage inputs of FUN_0041f903 (DAT_0438b4c8 / b4cc / b4d0 / cbe4, the
 * weak-enemies byte) */
static actor_stage_t stage_now(void)
{
    const uint8_t *w = save_work_bank_at(save_work_active_slot());
    const actor_stage_t st = { g_scene1_combat_stage_id, stage_gate_get_next(),
                               stage_post_load_get_mode_b4d0(), g_ghost_count,
                               w ? w[0x2bc57] : 0 };
    return st;
}

/* FUN_00423789: the walk-and-line-up step of several enemy types.  Tick 1 of a
 * run: a60 = the rest (rand & 0x3f) and a heading — a random axis or toward the
 * player's longer axis (away from the player inside 3), re-rolled while a
 * 2-unit ray from 1 up is blocked (20 tries), then 1-in-4 a random axis when
 * the player is 2+ away.  From tick `wait`: the player in line (< 1.5 off one
 * axis, 2..reach along the other) after `min_runs` runs → face the player
 * and return 1.  The walk at `speed` past tick `start`; the run ends after
 * a60 + `rest` more ticks (+0xa5c counts the runs). */
static int actor_ai_line_up(actor_t *a, float reach, int32_t wait, float speed,
                            int32_t start, int32_t rest, int32_t min_runs)
{
    static const float k1p5 = 1.5f, k3 = 3.0f;
    float dx = spill32((long double)PX - AF(a, 0x3f0));
    float dz = spill32((long double)PZ - AF(a, 0x3f8));
    const float dist = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
    int ret = 0;
    if (++AI(a, 0xa58) == 1) {
        AI(a, 0xa60) = (int32_t)(rng_next15() & 0x3f);
        if (dist < k3) { dx = -dx; dz = -dz; }
        const float adx = spill32(fabsl((long double)dx));
        const float adz = spill32(fabsl((long double)dz));
        if (adx > adz) dz = 0.0f; else dx = 0.0f;
        for (int i = 0; i != 20; i++) {
            const float q = spill32((long double)(rng_next15() & 3));
            AF(a, 0x420) = spill32((long double)q * k_pi2);
            if (rng_next15() & 1)
                AF(a, 0x420) = spill32(crt_atan2(dx, dz));
            const double h = AF(a, 0x420);
            const float sx = spill32(2.0L * spill32(x87_crt_sin(h)));
            const float sz = spill32(2.0L * spill32(x87_crt_cos(h)));
            float frac = 0.0f;
            mapcol_hit_t rh;
            rh.slot = 0;
            rh.tri = 0;
            if (mapcol_ray(AF(a, 0x3f0), spill32((long double)AF(a, 0x3f4) + k1), AF(a, 0x3f8),
                           sx, 0.0f, sz, &frac, &rh) == 0)
                break;
        }
        if (dist >= k2 && (rng_next15() & 3) == 0) {
            const float q = spill32((long double)(rng_next15() & 3));
            AF(a, 0x420) = spill32((long double)q * k_pi2);
        }
    }
    if (AI(a, 0xa58) >= wait) {
        float adx = spill32(fabsl((long double)dx)), adz;
        if (adx < k1p5 && (adz = spill32(fabsl((long double)dz))) > k2 && adz < reach) {
            dx = 0.0f;
            ret = 1;
        } else if ((adz = spill32(fabsl((long double)dz))) < k1p5 &&
                   (adx = spill32(fabsl((long double)dx))) > k2 && adx < reach) {
            dz = 0.0f;
            ret = 1;
        }
        if (AI(a, 0xa5c) < min_runs) ret = 0;
        if (ret) {
            const float h = spill32(crt_atan2(dx, dz));
            AI(a, 0xa5c) = 0;
            AF(a, 0x420) = h;
        }
    }
    const int32_t t = AI(a, 0xa58);
    if (t >= start) {
        const int32_t u = t - start;
        if (u >= 1) {
            const double h = AF(a, 0x420);
            AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * speed + AF(a, 0x3fc));
            AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * speed + AF(a, 0x404));
        }
        if (u >= AI(a, 0xa60) + rest) {
            AI(a, 0xa58) = 0;
            ++AI(a, 0xa5c);
        }
    }
    return ret;
}

/* a group member of `a`: in use, the same type and home x / z */
static int same_group(const actor_t *a, const actor_t *e)
{
    return AI(e, ACT_INUSE) != 0 && AI(a, ACT_TYPE) == AI(e, ACT_TYPE) &&
           AF(a, 0x414) == AF(e, 0x414) && AF(a, 0x41c) == AF(e, 0x41c);
}

/* FUN_004262b7: type 16 (variant != 3).  Variant 2 is a line of four: the
 * leader (+0xa60 = its axis) spawns three followers 2 / 4 / 6 along it, turns
 * them all toward the player's side every tick on a 120-tick clock (the shot
 * 86 at 56); one of them hit → the whole line turns variant 1.  Else: walk and
 * line up (FUN_00423789) then the attack — variant 0 the charge (record 21,
 * SE 683, the dash with the trail overlay 91), others the shot 86. */
static void actor_ai_t16(actor_t *a)
{
    static const float k_pi = 3.1415927410125732f;     /* 0x51943c */
    static const float k_3pi2 = 4.71238899230957f;     /* 0x519afc */
    static const float k0p2 = 0.20000000298023224f;    /* 0x5198d8 */
    static const float k0p025 = 0.02500000037252903f;  /* 0x5193f4 */
    static const float k1p5 = 1.5f, k18 = 18.0f;
    const int32_t sub = AI(a, 0xae4);
    if (sub == 2) {
        if (AI(a, 0xb8c) != 0 && AI(a, 0xa60) != 0) {
            AI(a, 0xb8c) = 0;
            const actor_stage_t st = stage_now();
            for (int32_t k = 1, off = 2; k != 4; k++, off += 2) {
                float x = AF(a, 0x3f0), z = AF(a, 0x3f8);
                const float fk = spill32((long double)k);
                if (AI(a, 0xa60) == 1) z = spill32(2.0L * fk + z);
                else x = spill32(2.0L * fk + x);
                actor_t *e = actor_spawn_enemy(&st, x, AF(a, 0x3f4), z, AI(a, ACT_TYPE));
                if (e) {
                    AI(e, 0xa64) = off;
                    AF(e, 0x414) = AF(a, 0x414);
                    AI(e, 0xae4) = 2;
                    AF(e, 0x418) = AF(a, 0x418);
                    AI(e, ACT_INUSE) = 1;
                    AF(e, 0x41c) = AF(a, 0x41c);
                    AF(e, 0x420) = AI(a, 0xa60) == 1 ? k_pi2 : k_pi;
                    AI(e, 0xb2c) = 1;
                }
            }
            AF(a, 0x420) = AI(a, 0xa60) == 1 ? k_pi2 : k_pi;
        }
        const int32_t ph = AI(a, 0xa58) % 120;
        if (AI(a, 0xa60) != 0) {
            const float dx = spill32((long double)PX - AF(a, 0x3f0));
            ++AI(a, 0xa58);
            const float dz = spill32((long double)PZ - AF(a, 0x3f8));
            if (AI(a, 0xa60) == 1) AF(a, 0x420) = dx > 0.0f ? k_pi2 : k_3pi2;
            else AF(a, 0x420) = dz > 0.0f ? 0.0f : k_pi;
        }
        actor_anim_set(a, ph >= 30 ? 3 : 0);
        if (ph == 56 && AI(a, 0xb14) <= 0) {
            scene1_record_b_spawn_npc(a, 86, 1);     /* FUN_00445a8c */
            audio_play_se_by_id(691);
        }
        if (AI(a, 0xa60) != 0)
            for (int k = 0; k < ACTOR_COUNT; k++)
                if (same_group(a, &g_actor[k])) {
                    AI(&g_actor[k], 0xa58) = AI(a, 0xa58);
                    AF(&g_actor[k], 0x420) = AF(a, 0x420);
                }
        int hit = 0;
        for (int k = 0; k < ACTOR_COUNT; k++)
            if (same_group(a, &g_actor[k]) && (AI(&g_actor[k], 0x440) > 0 || AI(&g_actor[k], 0xb18) > 0))
                hit = 1;
        if (hit)
            for (int k = 0; k < ACTOR_COUNT; k++)
                if (same_group(a, &g_actor[k])) {
                    AI(&g_actor[k], 0xae4) = 1;
                    AI(&g_actor[k], 0xa58) = 0;
                }
        return;
    }
    const int32_t st = AI(a, 0xa54);
    if (st == 0) {
        actor_anim_set(a, 0);
        const float reach = sub == 1 ? k18 : k8;
        if (actor_ai_line_up(a, reach, 20, k0p025, 5, 30, 0) && AF(a, 0x3f4) == AF(a, 0x3e0)) {
            AI(a, 0xa5c) = 0;
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 1;
            actor_anim_set(a, 3);
        }
    } else if (st == 1) {
        const int32_t t = ++AI(a, 0xa58);
        if (sub != 0) {
            if (t == 26 && AI(a, 0xb14) <= 0) {
                scene1_record_b_spawn_npc(a, 86, 1);
                audio_play_se_by_id(691);
            }
            if (AI(a, 0xa58) >= 1 && AI(a, 0xa58) - 1 == 90) {
                AI(a, 0xa58) = 0;
                AI(a, 0xa54) = 0;
            }
        } else {
            if (t == 20) {
                scene1_record_b_spawn_npc(a, 21, 1);
                audio_play_se_by_id(683);
            }
            const int32_t t2 = AI(a, 0xa58);
            if (t2 >= 1) {
                const int32_t u = t2 - 1;
                if (u >= 15 && u < 35) {
                    const double h = AF(a, 0x420);
                    AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p05 + AF(a, 0x3fc));
                    AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * k0p05 + AF(a, 0x404));
                    if (u % 2 == 0) {
                        const float sx = spill32(2.0L * spill32(x87_crt_sin(h)));
                        const float x = spill32(((long double)rng_next_unit() - k0p5) * k0p2 + sx + AF(a, 0x3f0));
                        const float sz = spill32(2.0L * spill32(x87_crt_cos(h)));
                        const float z = spill32(((long double)rng_next_unit() - k0p5) * k0p2 + sz + AF(a, 0x3f8));
                        scene1_overlay_spawn(NULL, x, spill32((long double)AF(a, 0x3f4) + k1p5), z,
                                             91, k0p5, -1, 0, 0, 0);   /* FUN_004147d5 */
                    }
                }
                if (u == 90) {
                    AI(a, 0xa58) = 0;
                    AI(a, 0xa54) = 0;
                }
            }
        }
    }
}

/* FUN_0042f7ad's bomb arm (types 37..40; trap 9 spawns them lit): the fuse
 * (+0xaf0) starts at 240 / 480 / 360 / 240 (red / blue / green / yellow) and
 * blows at 540; a bomb rolling faster than 0.01 blows early on another live
 * actor within 1.5 or a wall (not type 2) within its reach (+0x5c · scale · 3)
 * ahead: SE 328, the burst 12, the shake, the blast record 18.  (The heading it
 * also normalises into ±π is never used.) */
static void actor_bomb_fuse(actor_t *a, int32_t t)
{
    static const float k0p2 = 0.20000000298023224f, k1p5 = 1.5f, k2f = 2.0f, k3f = 3.0f;
    const int32_t type = AI(a, ACT_TYPE);
    if (type == 37 && t < 240) AI(a, 0xaf0) = 240;
    if (type == 39 && AI(a, 0xaf0) < 360) AI(a, 0xaf0) = 360;
    if (type == 38 && AI(a, 0xaf0) < 480) AI(a, 0xaf0) = 480;
    if (type == 40 && AI(a, 0xaf0) < 240) AI(a, 0xaf0) = 240;
    int hit = 0;
    const float vz = AF(a, 0x404), vx = AF(a, 0x3fc);
    const float s2 = spill32((long double)vx * vx + (long double)vz * vz);
    if (s2 > 0.0f) {
        const float sp = spill32(crt_sqrt(s2));
        if (sp > k0p01) {
            for (int i = 0; i < ACTOR_COUNT; i++) {
                const actor_t *o = &g_actor[i];
                if (AI(o, ACT_INUSE) == 0 || o == a || AI(o, ACT_TYPE) < 0) continue;
                const float dx = spill32((long double)AF(a, 0x3f0) - AF(o, 0x3f0));
                const float dz = spill32((long double)AF(a, 0x3f8) - AF(o, 0x3f8));
                const float d = spill32(crt_sqrt((double)((long double)dz * dz + (long double)dx * dx)));
                if (d < k1p5) hit = 1;
            }
            const float yb = spill32((long double)AF(a, 0x3f4) + k0p2);
            const float *rf = enemy_of(a)->runtime_floats;
            const float r = spill32((long double)rf[6] * rf[0] * k3f);
            float rx = AF(a, 0x3fc), rz = AF(a, 0x404);
            if (sp < r) {
                rx = spill32((long double)r * AF(a, 0x3fc) / sp);
                rz = spill32((long double)r * AF(a, 0x404) / sp);
            }
            float frac = 0.0f;
            mapcol_hit_t h;
            h.tri = 0;
            h.slot = 0;
            if (mapcol_ray(AF(a, 0x3f0), yb, AF(a, 0x3f8), rx, 0.0f, rz, &frac, &h) != 0 &&
                hit_type(h.slot, h.tri) != 2 && frac < k1)
                hit = 1;
        }
    }
    if (AI(a, 0xaf0) != 540 && !hit) return;
    AI(a, ACT_INUSE) = 0;
    audio_play_se_by_id(328);
    scene1_pfo_table_a_alloc_passthrough(0, AF(a, 0x3f0), AF(a, 0x3f4), AF(a, 0x3f8),
                                         12, k2f, -1, 0, 0);   /* FUN_0041331d */
    camera_shake(32, 4, 4, k1);
    scene1_record_b_spawn_npc(a, 18, 3);         /* FUN_00445a8c */
}

/* FUN_0042f685: every 8th tick of a boss's death (from 61) a shake, two SEs and
 * a burst at a random point within 2.5 / 0..5 above. */
static void actor_boss_burst(actor_t *a)
{
    if (AI(a, 0xaf0) % 8 != 1) return;
    camera_shake(8, 4, 4, k1);
    audio_play_se_by_id(328);
    audio_play_se_by_id(319);
    const float x = spill32(((long double)rng_next_unit() - k0p5) * k5 + AF(a, 0x3f0));
    const float y = spill32((long double)rng_next_unit() * k5 + AF(a, 0x3f4));
    const float z = spill32(((long double)rng_next_unit() - k0p5) * k5 + AF(a, 0x3f8));
    scene1_pfo_table_a_alloc_passthrough(0, x, y, z, 3, 1.0f, -1, 0, 0);   /* FUN_0041331d */
}

/* FUN_0042f746: when no other actor of `a`'s type is alive (+0x428 == 1), every
 * live actor starts dying (+0x428 = 2, +0xaf0 = 0, anim 2). */
static void actor_last_of_kind(actor_t *a)
{
    for (int i = 0; i < ACTOR_COUNT; i++) {
        const actor_t *o = &g_actor[i];
        if (o != a && AI(o, ACT_INUSE) == 1 && AI(o, ACT_TYPE) == AI(a, ACT_TYPE)) return;
    }
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *o = &g_actor[i];
        if (AI(o, ACT_INUSE) != 1) continue;
        AI(o, ACT_INUSE) = 2;
        AI(o, 0xaf0) = 0;
        actor_anim_set(o, 2);
    }
}

static void actor_anim_blend(actor_t *a, int32_t anim);   /* FUN_0041f759 */

/* FUN_0042f7ad: a dying actor (+0x428 == 2) — the regular enemies' arm: at
 * tick 2 the slot frees (a 1-in-3 respawn timer of 600 off the checkpoint
 * floors), the shake, SE 337 and the burst overlay 3; a reviving one
 * (+0xadc) stands up at 60 / 90 */
void actor_dying(actor_t *a)
{
    static const float k0p025 = 0.02500000037252903f;
    const int32_t type = AI(a, ACT_TYPE);
    const enemy_record_t *e = enemy_of(a);
    AI(a, 0xb58) = 20;
    const int32_t t = ++AI(a, 0xaf0);
    AF(a, 0x400) = spill32((long double)AF(a, 0x400) - (AI(a, 0xb98) == 1 ? k0p025 : k0p05));
    switch (type) {
    case 25: {
        /* Charme beaten: the slow-mo (becc 120) and DAT_056db0f4 1200 at 1.  In
         * dungeon 5 she fades (+0x3dc - 16 from 60) and frees; on floor 15 she
         * stays for the scene — event stage 5 holds her 3 left / 1.5 back of the
         * start facing +x, stage 6 shrinks her away (+0xac0 - 0.05, +0xac4 + 0.2,
         * rising b8a4 · 0.25) until +0xac0 drops below 0 */
        static const float k3 = 3.0f, k1p5 = 1.5f, k0p2 = 0.20000000298023224f, k0p25 = 0.25f;
        if (t == 1) {
            g_dat_0438becc = 120;
            PW(0x56db0f4u).i = 1200;
        }
        if (g_scene1_combat_stage_id == 5) {
            if (t >= 60 && (AI(a, 0x3dc) -= 16) < 0) AI(a, ACT_INUSE) = 0;
            return;
        }
        if (g_event_stage == 5) {
            AF(a, 0x3f0) = spill32((long double)g_scene1_stage_player_default_pos[0] - k3);
            AF(a, 0x3f4) = g_scene1_stage_player_default_pos[1];
            AF(a, 0x3f8) = spill32((long double)g_scene1_stage_player_default_pos[2] - k1p5);
            actor_anim_set(a, 0);
            AI(a, 0x18) = 2;
            AF(a, 0x420) = k_pi2;
        }
        if (g_event_stage != 6) return;
        const float sxz = spill32((long double)AF(a, 0xac0) - k0p05);
        AF(a, 0xac0) = sxz;
        AF(a, 0xac4) = spill32((long double)AF(a, 0xac4) + k0p2);
        AF(a, 0x3f4) = spill32((long double)spill32((long double)g_event_tick) * k0p25);
        if (sxz < 0.0f) AI(a, ACT_INUSE) = 0;
        return;
    }
    case 23: case 49: case 54: case 55:
        /* the crown slime / the black knight / the boss ahriman / the pumpkin
         * ghost: a 120-tick burst; the last of its kind takes the floor down */
        if (t == 1) {
            g_dat_0438becc = 60;
            int n = 0;
            for (int i = 0; i < ACTOR_COUNT; i++) {
                const actor_t *o = &g_actor[i];
                if (o != a && AI(o, ACT_INUSE) == 1 && AI(o, ACT_TYPE) == type) n++;
            }
            PW(0x56db0f4u).i = n != 0 ? 60 : 1200;
            camera_shake(8, 4, 4, k1);
            audio_play_se_by_id(328);
            audio_play_se_by_id(319);
            scene1_pfo_table_a_alloc_passthrough(0, AF(a, 0x3f0),
                spill32((long double)e->runtime_floats[2] * e->runtime_floats[0] + AF(a, 0x3f4)),
                AF(a, 0x3f8), 3, 1.0f, -1, 0, 0);   /* FUN_0041331d */
            AI(a, 0x3dc) = 0;
        }
        if (AI(a, 0xaf0) == 60) actor_last_of_kind(a);   /* FUN_0042f746 */
        if (AI(a, 0xaf0) > 60) actor_boss_burst(a);      /* FUN_0042f685 */
        if (AI(a, 0xaf0) == 120) AI(a, ACT_INUSE) = 0;
        return;
    case 37: case 38: case 39: case 40:
        actor_bomb_fuse(a, t);
        return;
    case 24: case 59: case 60: {
        /* the mushroom king: the cross-fade to anim 18; at 1 the slow-mo (becc
         * 60) and DAT_056db0f4 1200, shakes at 20 / 38; at 60 the cinematic
         * camera (FUN_0044269c), the body to the arena centre facing -π, the
         * records cleared (FUN_0040f64b(0)) and every other live actor dying;
         * the wobble at 200, shakes at 280 / 284; from 300 it fades (+0x3dc - 2)
         * shedding spores (69) to 347; the slot frees at 500 */
        static const float k6 = 6.0f, k0p4 = 0.4000000059604645f;
        actor_anim_blend(a, 18);                     /* FUN_0041f759 */
        if (t == 1) {
            g_dat_0438becc = 60;
            PW(0x56db0f4u).i = 1200;
        }
        if (t == 20 || t == 38) {
            camera_shake(8, 4, 4, k1);
            audio_play_se_by_id(360);
        }
        if (AI(a, 0xaf0) == 60) {
            g_dat_06a46fa0 = 1;                      /* FUN_0044269c */
            AF(a, 0x3f0) = -40.0f;
            AF(a, 0x3f4) = 0.0f;
            AF(a, 0x3f8) = -40.0f;
            AF(a, 0x420) = -3.1415927410125732f;
            scene1_records_reset(0);                 /* FUN_0040f64b(0) */
            for (int i = 0; i < ACTOR_COUNT; i++) {
                actor_t *o = &g_actor[i];
                if (AI(o, ACT_INUSE) == 1) {
                    AI(o, ACT_INUSE) = 2;
                    AI(o, 0xaf0) = 0;
                }
            }
        }
        if (AI(a, 0xaf0) == 200) sim_screen_wobble(80);
        if (AI(a, 0xaf0) == 280 || AI(a, 0xaf0) == 284) {
            camera_shake(24, 4, 4, k1);
            audio_play_se_by_id(360);
            sim_screen_wobble(32);
        }
        const int32_t u = AI(a, 0xaf0);
        if (u == 500) AI(a, ACT_INUSE) = 0;
        if (u >= 300) {
            AI(a, 0x3dc) -= 2;
            if (AI(a, 0x3dc) < 0) AI(a, 0x3dc) = 0;
            if (u < 348) {
                const float x = spill32(((long double)rng_next_unit() - k0p5) * k5 + AF(a, 0x3f0));
                const float z = spill32((long double)rng_next_unit() * k6 + AF(a, 0x3f8));
                scene1_spawn(0, x, k0p5, z, 69, k0p4, 3);   /* FUN_00447f4f */
            }
        }
        return;
    }
    case 27: case 28:
    case 41: case 61: case 64: case 67:
    case 68: case 69: case 70: case 71: case 73: case 74:
        /* PORT-DEBT(stub, FUN_0042f7ad): the other bosses' / special enemies'
         * death arms (the boss cut-ins, FUN_0041f759, FUN_0042439e) */
        return;
    default:
        break;
    }
    if (AI(a, 0xadc) != 0) {
        if (t == 1) { actor_anim_set(a, 4); audio_play_se_by_id(335); }
        if (AI(a, 0xaf0) == 60) { actor_anim_set(a, 5); audio_play_se_by_id(334); }
        if (AI(a, 0xaf0) == 90) { --AI(a, 0xadc); AI(a, ACT_INUSE) = 1; }
        return;
    }
    if (t != 2) return;
    AI(a, ACT_INUSE) = 0;
    if (AI(a, 0xb40) == 0 && AI(a, 0xb2c) == 0 && rng_next15() % 3u == 0 &&
        !stage_gate_floor_is_checkpoint())
        AI(a, 0xad0) = 600;
    camera_shake(8, 4, 4, k1);
    audio_play_se_by_id(337);
    scene1_pfo_table_a_alloc_passthrough(0, AF(a, 0x3f0),
                                         spill32((long double)e->runtime_floats[2] * e->runtime_floats[0] + AF(a, 0x3f4)),
                                         AF(a, 0x3f8), 3, 0.699999988079071f, -1, 0, 0);
}

/* FUN_0042c8a5: a monster-house slime (+0xb40 == 1) circles the chest point
 * (+0xb48..b50) at a radius that shrinks from 10 over 280 ticks and back,
 * slot +0xb44 of 8 around the ring, turning 0.015 a tick */
static void actor_ai_ring(actor_t *a)
{
    static const float k10 = 10.0f, k300 = 300.0f, k0p015 = 0.014999999664723873f;
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        scene1_record_b_spawn_npc(a, 11, 1);     /* FUN_00445a8c */
    }
    const float r = spill32((long double)k10 - (long double)spill32((long double)AI(a, 0xa58)) * k10 / k300);
    const float ang = spill32((long double)spill32((long double)AI(a, 0xb44)) * k_2pi / 8.0f +
                              (long double)spill32((long double)AI(a, 0xa5c)) * k0p015);
    const double ad = ang;
    AF(a, 0x3f0) = spill32((long double)spill32(x87_crt_sin(ad)) * r + AF(a, 0xb48));
    AF(a, 0x3f4) = AF(a, 0xb4c);
    AF(a, 0x3f8) = spill32((long double)spill32(x87_crt_cos(ad)) * r + AF(a, 0xb50));
    ++AI(a, 0xa5c);
    if (AI(a, 0xa60) == 0) {
        if (++AI(a, 0xa58) == 280) AI(a, 0xa60) = 1;
    } else if (--AI(a, 0xa58) == 0) {
        AI(a, 0xa60) = 0;
    }
}

/* FUN_0041f789: turn the facing (+0x420, wrapped into ±π first) toward the
 * player by at most `rate`; within `rate` (or 2π - rate) it snaps */
static void actor_turn_to_player(actor_t *a, float rate)
{
    static const float k_pi = 3.1415927410125732f, k_mpi = -3.1415927410125732f;
    const float dx = spill32((long double)PX - AF(a, 0x3f0));
    const float dz = spill32((long double)PZ - AF(a, 0x3f8));
    const float to = spill32(spill32(crt_atan2(dx, dz)));
    while (AF(a, 0x420) < k_mpi) AF(a, 0x420) = spill32((long double)AF(a, 0x420) + k_2pi);
    while (AF(a, 0x420) > k_pi) AF(a, 0x420) = spill32((long double)AF(a, 0x420) - k_2pi);
    const float f = AF(a, 0x420);
    const float d = spill32((long double)f - to);
    const float ad = spill32(fabsl((long double)d));
    long double r;
    if (ad < rate || (long double)k_2pi - rate < ad) r = to;
    else if (f < to) r = ((long double)to - f < k_pi) ? (long double)rate + f : (long double)f - rate;
    else r = (d >= k_pi) ? (long double)rate + f : (long double)f - rate;
    AF(a, 0x420) = spill32(r);
    if (AF(a, 0x420) > k_pi) AF(a, 0x420) = spill32((long double)AF(a, 0x420) - k_2pi);
    else if (AF(a, 0x420) < k_mpi) AF(a, 0x420) = spill32((long double)AF(a, 0x420) + k_2pi);
}

/* FUN_0042a74c: the fliers (types 17..21, 56..58).  A fresh one (+0xb8c)
 * spawns its contact record 19 and faces the player; a group leader (variant
 * 1) brings a second of its type 18 off the player — the first of three
 * directions whose ray from the player is clear.  State 0 drifts toward the
 * player (turning by the type's rate, pushed along the facing); type 19 backs
 * off (state 1: pushed away for 20 ticks) every 120..134 ticks, type 21 pauses
 * and sheds sparkles (overlay 15) on a 400-tick cycle. */
static void actor_ai_flier(actor_t *a)
{
    static const float k18 = 18.0f, km18 = -18.0f, k0p7 = 0.699999988079071f;
    static const float k0p015 = 0.014999999664723873f, k0p023 = 0.023000000044703484f;
    static const float k0p027 = 0.027000000700354576f, k0p04 = 0.03999999910593033f;
    static const float k0p025 = 0.02500000037252903f, k0p06 = 0.05999999865889549f;
    AI(a, 0xb04) = 1;
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        scene1_record_b_spawn_npc(a, 19, 1);     /* FUN_00445a8c */
        actor_turn_to_player(a, 1.0f);
        if (AI(a, 0xae4) == 1) {
            const float dx = spill32((long double)PX - AF(a, 0x3f0));
            const float dz = spill32((long double)PZ - AF(a, 0x3f8));
            const float adx = spill32(fabsl((long double)dx));
            const float adz = spill32(fabsl((long double)dz));
            float ox[3], oz[3];
            if (adx > adz) {
                ox[0] = dx > 0.0f ? k18 : km18; ox[1] = 0.0f; ox[2] = 0.0f;
                oz[0] = 0.0f; oz[1] = k18; oz[2] = km18;
            } else {
                oz[0] = dz > 0.0f ? k18 : km18; oz[1] = 0.0f; oz[2] = 0.0f;
                ox[0] = 0.0f; ox[1] = k18; ox[2] = km18;
            }
            for (int i = 0; i != 3; i++) {
                float frac = 0.0f;
                mapcol_hit_t h;
                h.tri = 0;
                h.slot = 0;
                if (mapcol_ray(PX, k0p7, PZ, ox[i], k0p5, oz[i], &frac, &h) != 0) continue;
                const actor_stage_t st = stage_now();
                actor_t *n = actor_spawn_enemy(&st, spill32((long double)PX + ox[i]), 0.0f,
                                               spill32((long double)PZ + oz[i]), AI(a, ACT_TYPE));
                if (n) {   /* retail does not check */
                    AI(n, 0xae4) = 2;
                    AI(n, 0xb2c) = 1;
                }
                break;
            }
        }
    }
    const int32_t type = AI(a, ACT_TYPE);
    if (type == 19 || type == 20) AI(a, 0xb54) = 2;
    else if (type >= 56 && type <= 58) AI(a, 0xb54) = 1;
    float speed = k0p015, turn = k0p03;
    switch (type) {
    case 17: speed = k0p015; turn = k0p03; break;
    case 18: speed = k0p023; turn = k0p03; break;
    case 19: speed = k0p02;  turn = k0p04; break;
    case 20: speed = k0p027; turn = k0p05; break;
    case 21: speed = k0p023; turn = k0p05; break;
    case 56: case 57: case 58: speed = k0p025; turn = k0p06; break;
    default: break;
    }
    const int32_t st = AI(a, 0xa54);
    if (st == 1) {
        actor_turn_to_player(a, turn);
        if (AI(a, 0xa58) < 20) {
            const double h = AF(a, 0x420);
            AF(a, 0x3fc) = spill32((long double)AF(a, 0x3fc) - (long double)spill32(x87_crt_sin(h)) * speed);
            AF(a, 0x404) = spill32((long double)AF(a, 0x404) - (long double)spill32(x87_crt_cos(h)) * speed);
        }
        if (++AI(a, 0xa58) == 1) AI(a, 0xa60) = (int32_t)(rng_next15() % 15u);
        if (AI(a, 0xa58) > AI(a, 0xa60) + 30) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st != 0) return;
    actor_anim_set(a, 0);
    ++AI(a, 0xa58);
    if (type == 19) {
        if (AI(a, 0xa58) == 1) AI(a, 0xa60) = (int32_t)(rng_next15() % 15u);
        if (AI(a, 0xa58) > AI(a, 0xa60) + 120) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 1;
        }
    }
    if (type == 21) {
        const int32_t t = AI(a, 0xa58) % 400;
        speed = k0p023;
        AI(a, 0xb54) = 0;
        if (t >= 30 && t < 60) speed = 0.0f;
        if (t >= 60 && t < 180) {
            speed = k0p023;
            ++AI(a, 0xa5c);
            AI(a, 0xb54) = 1;
        }
        if (t >= 30 && t < 180 && AI(a, 0xa5c) % 4 == 0) {
            const float z = AF(a, 0x3f8);
            const float y = spill32(((long double)rng_next_unit() + AF(a, 0x3f4)) + k0p5);
            scene1_overlay_spawn(NULL, AF(a, 0x3f0), y, z, 15, k0p5, -1, 0, 0, 0);   /* FUN_004147d5 */
        }
    }
    actor_turn_to_player(a, turn);
    const double h = AF(a, 0x420);
    AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * speed + AF(a, 0x3fc));
    AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * speed + AF(a, 0x404));
}

/* FUN_00428ae4: types 13 / 14 on foot — state 0 (+0xa54): pick a heading to a
 * random point 8 from the player, walk it 40 ticks at 0.02 (on the ground every
 * 9th tick a 0.6 hop, the heading re-rolled: a right angle or the dominant axis
 * toward a point 3 from the player, up to 20 tries for a clear 5-ray); six walks
 * → state 1 (anim 3): a 0.04 lunge on ticks 2..40, back to state 0 at 91. */
static void actor_ai_t13(actor_t *a)
{
    static const float k0p04 = 0.03999999910593033f, k3 = 3.0f;
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        scene1_record_b_spawn_npc(a, 10, 1);     /* FUN_00445a8c */
    }
    if (AI(a, 0xae4) == 1) return;
    const int32_t st = AI(a, 0xa54);
    if (st == 1) {
        const int32_t t = ++AI(a, 0xa58);
        if (t < 1) return;
        if (t - 1 >= 1 && t - 1 < 40) {
            const double h = AF(a, 0x420);
            AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p04 + AF(a, 0x3fc));
            AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * k0p04 + AF(a, 0x404));
        }
        if (t - 1 == 90) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st != 0) return;
    actor_anim_set(a, 0);
    if (++AI(a, 0xa58) == 1) {
        const double r = spill32((long double)rng_next_unit() * k_2pi);
        const float tx = spill32((long double)spill32(x87_crt_sin(r)) * k8 + PX);
        const float tz = spill32((long double)spill32(x87_crt_cos(r)) * k8 + PZ);
        const float dx = spill32((long double)tx - AF(a, 0x3f0));
        const float dz = spill32((long double)tz - AF(a, 0x3f8));
        AF(a, 0x420) = spill32(crt_atan2(dx, dz));
    }
    const int32_t t = AI(a, 0xa58);
    if (t < 0) return;
    if (t >= 40) {
        AI(a, 0xa58) = 0;
        if (++AI(a, 0xa5c) == 6) {
            AI(a, 0xa5c) = 0;
            AI(a, 0xa54) = 1;
            actor_anim_set(a, 3);
        }
        return;
    }
    {
        const double h = AF(a, 0x420);
        AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p02 + AF(a, 0x3fc));
        AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * k0p02 + AF(a, 0x404));
    }
    if (AF(a, 0x3f4) != AF(a, 0x3e0) || ++AI(a, 0xa60) <= 8) return;
    AI(a, 0xa60) = 0;
    AF(a, 0x400) = k0p6;
    const double r = spill32((long double)rng_next_unit() * k_2pi);
    const float tx = spill32((long double)spill32(x87_crt_sin(r)) * k3 + PX);
    const float tz = spill32((long double)spill32(x87_crt_cos(r)) * k3 + PZ);
    float dx = spill32((long double)tx - AF(a, 0x3f0));
    float dz = spill32((long double)tz - AF(a, 0x3f8));
    const float adx = dx < 0.0f ? -dx : dx, adz = dz < 0.0f ? -dz : dz;
    if (adx > adz) dz = 0.0f;
    else dx = 0.0f;
    for (int i = 0; i != 20; i++) {
        const float q = (float)(int32_t)(rng_next15() & 3u);
        AF(a, 0x420) = spill32((long double)q * k_pi2);
        if (rng_next15() & 1u)
            AF(a, 0x420) = spill32(crt_atan2(dx, dz));
        const double h = AF(a, 0x420);
        const float sx = spill32((long double)spill32(x87_crt_sin(h)) * k5);
        const float sz = spill32((long double)spill32(x87_crt_cos(h)) * k5);
        float frac = 0.0f;
        mapcol_hit_t hit;
        hit.tri = 0;
        hit.slot = 0;
        if (mapcol_ray(AF(a, 0x3f0), spill32((long double)AF(a, 0x3f4) + k1), AF(a, 0x3f8),
                       sx, 0.0f, sz, &frac, &hit) == 0)
            break;
    }
}

/* FUN_00429962: type 22 (the rabbit) — state 0: a heading to a point 2 from the
 * player (up to 20 tries for a clear 3-ray), a 30 + rand&63 tick pause after 30
 * ticks of 0.02 walk, then on the ground state 1 (anim 3): the hop (0.6 at tick
 * 2, 0.04 forward ticks 2..30, record 12 at 5, back at 41).  The player's swing
 * (DAT_056db048 2, DAT_056db04c < 20) within 4 → state 2: a 0.04 backstep for 30
 * ticks, with a 0.6 jump when the 3-ray behind is clear (+0xb08 for 6 ticks). */
static void actor_ai_t22(actor_t *a)
{
    static const float k0p04 = 0.03999999910593033f, k3 = 3.0f, km3 = -3.0f;
    float dx = spill32((long double)PX - AF(a, 0x3f0));
    float dz = spill32((long double)PZ - AF(a, 0x3f8));
    const float d2 = spill32((long double)dz * dz + (long double)dx * dx);
    float dist;
    if (d2 == 0.0f) {
        dz = k0p01;
        dist = k0p01;
    } else {
        dist = spill32(crt_sqrt(d2));
    }
    const int32_t st = AI(a, 0xa54);
    if (st == 2) {
        const int32_t t = ++AI(a, 0xa58);
        if (t < 1) return;
        const int32_t e = t - 1;
        if (e < 30) {
            const double h = AF(a, 0x420);
            AF(a, 0x3fc) = spill32((long double)AF(a, 0x3fc) - (long double)spill32(x87_crt_sin(h)) * k0p04);
            AF(a, 0x404) = spill32((long double)AF(a, 0x404) - (long double)spill32(x87_crt_cos(h)) * k0p04);
        }
        if (e == 0) {
            const double h = AF(a, 0x420);
            const float sx = spill32((long double)spill32(x87_crt_sin(h)) * km3);
            const float sz = spill32((long double)spill32(x87_crt_cos(h)) * km3);
            float frac = 0.0f;
            mapcol_hit_t hit;
            hit.tri = 0;
            hit.slot = 0;
            if (mapcol_ray(AF(a, 0x3f0), spill32((long double)AF(a, 0x3f4) + k1), AF(a, 0x3f8),
                           sx, 0.0f, sz, &frac, &hit) != 0)
                return;
            AI(a, 0xb08) = 1;
            AF(a, 0x400) = k0p6;
        }
        if (e >= 6) AI(a, 0xb08) = 0;
        if (e == 60) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st == 1) {
        if (++AI(a, 0xa58) == 5)
            scene1_record_b_spawn_npc(a, 12, 1);  /* FUN_00445a8c */
        const int32_t t = AI(a, 0xa58);
        if (t < 1) return;
        const int32_t e = t - 1;
        if (e == 1) AF(a, 0x400) = k0p6;
        if (e >= 1 && e < 30) {
            const double h = AF(a, 0x420);
            AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p04 + AF(a, 0x3fc));
            AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * k0p04 + AF(a, 0x404));
        }
        if (e == 40) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st != 0) return;
    actor_anim_set(a, 0);
    if (++AI(a, 0xa58) == 1) {
        for (int i = 0; ; ) {
            const double r = spill32((long double)rng_next_unit() * k_2pi);
            const float s = spill32(x87_crt_sin(r));
            const float tx = spill32((long double)s + s + PX);
            const float c = spill32(x87_crt_cos(r));
            const float tz = spill32((long double)c + c + PZ);
            const float ddx = spill32((long double)tx - AF(a, 0x3f0));
            const float ddz = spill32((long double)tz - AF(a, 0x3f8));
            const float h = spill32(crt_atan2(ddx, ddz));
            AF(a, 0x420) = h;
            const float sx = spill32((long double)spill32(x87_crt_sin(h)) * k3);
            const float sz = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * k3);
            float frac = 0.0f;
            mapcol_hit_t hit;
            hit.tri = 0;
            hit.slot = 0;
            if (mapcol_ray(AF(a, 0x3f0), AF(a, 0x3f4), AF(a, 0x3f8), sx, 0.0f, sz, &frac, &hit) == 0)
                break;
            if (++i == 20) break;
        }
        AI(a, 0xa60) = (int32_t)(rng_next15() & 0x3fu);
    }
    if (PW(0x56db048u).i == 2 && PW(0x56db04cu).i < 20 && dist < k4) {
        AI(a, 0xa58) = 0;
        AI(a, 0xa54) = 2;
        AF(a, 0x420) = spill32(crt_atan2(dx, dz));
        return;
    }
    const int32_t t = AI(a, 0xa58);
    if (t < 30) return;
    const int32_t e = t - 30;
    if (e >= 1 && e < 60) {
        const double h = AF(a, 0x420);
        AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p02 + AF(a, 0x3fc));
        AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * k0p02 + AF(a, 0x404));
    }
    if (e < AI(a, 0xa60) + 30) return;
    AI(a, 0xa58) = 0;
    if (AF(a, 0x3f4) == AF(a, 0x3e0)) {
        AI(a, 0xa54) = 1;
        actor_anim_set(a, 3);
    }
}

/* FUN_00428f0a: types 10..12 (the bees) fly (+0xa00 = 1).  State 0: bob toward
 * 2.5 + 2 sin(t·0.05) (vy eased, clamped ±0.1) and circle a drifting centre
 * (+0xa9c / +0xaa4, 5% a tick toward the player — or home when +0xa64 % 4 >= 2)
 * at radius 5 on a sin-wobbled sector (+0xa50 & 7 of 7), 288-tick cycles; at
 * tick 130 + rand&127 of the cycle state 1 (anim 3; the player phase) or stay.
 * State 1: turn in, record 17 at 20, the -0.18 dive at 0.07 on ticks 15..54, back
 * at 70.  State 2: home again at 15, back at 70.  The head's player distance
 * (its sqrt discarded) is dead. */
static void actor_ai_bee(actor_t *a)
{
    static const float k2p5 = 2.5f, k0p0285 = 0.028499998152256012f;
    static const float k0p07 = 0.07000000029802322f, km0p18 = -0.18000000715255737f;
    static const float km0p1 = -0.10000000149011612f, k0p04 = 0.03999999910593033f;
    static const float k7 = 7.0f, k0p628 = 0.6283185482025146f, k0p033 = 0.032999999821186066f;
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        AI(a, 0xb04) = 1;
    }
    AI(a, 0xa00) = 1;
    const int32_t st = AI(a, 0xa54);
    if (st == 2) {
        const int32_t t = ++AI(a, 0xa58);
        if (t == 15) {
            AF(a, 0x3f0) = AF(a, 0x414);
            AF(a, 0x3f4) = AF(a, 0x418);
            AF(a, 0x3f8) = AF(a, 0x41c);
        }
        if (t == 70) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st == 1) {
        if (++AI(a, 0xa58) == 20)
            scene1_record_b_spawn_npc(a, 17, 1);  /* FUN_00445a8c */
        if (AI(a, 0xa58) < 20)
            actor_turn_to_player(a, k0p1);        /* FUN_0041f789 */
        const int32_t t = AI(a, 0xa58);
        if (t >= 15 && t < 55) {
            AF(a, 0x400) = km0p18;
            const double h = AF(a, 0x420);
            AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p07 + AF(a, 0x3fc));
            AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * k0p07 + AF(a, 0x404));
        }
        if (AI(a, 0xa58) == 70) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st != 0) return;
    const int32_t cyc = AI(a, 0xa5c) % 288;
    const float ph = spill32((long double)spill32((float)AI(a, 0x398)) * k0p05);
    const float s0 = spill32(x87_crt_sin(ph));
    const float hy = spill32((long double)s0 + s0 + k2p5);
    if ((long double)hy - k0p5 > AF(a, 0x3f4))
        AF(a, 0x400) = spill32((long double)AF(a, 0x400) * k0p95 + k0p0285);
    else if ((long double)hy + k0p5 < AF(a, 0x3f4))
        AF(a, 0x400) = spill32(((long double)AF(a, 0x400) - k0p03) * k0p95);
    if (AF(a, 0x400) > k0p1) AF(a, 0x400) = k0p1;
    if (AF(a, 0x400) < km0p1) AF(a, 0x400) = km0p1;
    ++AI(a, 0xa5c);
    if (cyc == 1) {
        AI(a, 0xa60) = (int32_t)(rng_next15() & 0x7fu);
        if (AI(a, 0xa64) % 4 < 2) {
            AF(a, 0xa9c) = PX;
            AF(a, 0xaa4) = PZ;
        } else {
            AF(a, 0xa9c) = AF(a, 0x414);
            AF(a, 0xaa4) = AF(a, 0x41c);
        }
    }
    const int32_t w = AI(a, 0xa60);
    if (cyc == w + 130) {
        AI(a, 0xa54) = AI(a, 0xa64) % 4 < 2 ? 1 : 0;
        actor_anim_set(a, 3);
        const int32_t r = (int32_t)(rng_next15() & 7u);
        ++AI(a, 0xa64);
        AI(a, 0xa50) = r;
        return;
    }
    float tx, tz;
    if (AI(a, 0xa64) % 4 < 2) {
        tx = PX;
        tz = PZ;
    } else {
        tx = AF(a, 0x414);
        tz = AF(a, 0x41c);
    }
    const float wp = spill32((long double)spill32((float)(AI(a, 0xa5c) + 2 * w)) * k0p04);
    const float s = spill32(x87_crt_sin(wp));
    const float q = spill32((float)(AI(a, 0xa50) & 7));
    const float ang = spill32((long double)q * k_2pi / k7 + (long double)s * k0p628);
    AF(a, 0xa9c) = spill32(((long double)tx - AF(a, 0xa9c)) * k0p05 + AF(a, 0xa9c));
    AF(a, 0xaa4) = spill32(((long double)tz - AF(a, 0xaa4)) * k0p05 + AF(a, 0xaa4));
    const float ax = spill32((long double)ang - g_scene1_camera_yaw);
    const float px2 = spill32((long double)spill32(x87_crt_sin(ax)) * k5 + AF(a, 0xa9c));
    const float az = spill32((long double)ang - g_scene1_camera_yaw);
    const float pz2 = spill32((long double)spill32(x87_crt_cos(az)) * k5 + AF(a, 0xaa4));
    const float ddx = spill32((long double)px2 - AF(a, 0x3f0));
    const float ddz = spill32((long double)pz2 - AF(a, 0x3f8));
    const float d2 = spill32((long double)ddz * ddz + (long double)ddx * ddx);
    if (d2 > 0.0f) {
        const float dist = spill32(crt_sqrt(d2));
        actor_turn_to_player(a, k0p1);            /* FUN_0041f789 */
        if ((double)dist > 1.0) {
            AF(a, 0x3fc) = spill32((long double)ddx * k0p033 / dist + AF(a, 0x3fc));
            AF(a, 0x404) = spill32((long double)ddz * k0p033 / dist + AF(a, 0x404));
        }
    }
    actor_anim_set(a, 0);
}

/* FUN_0042aef7: type 23 (the crown slime boss).  Every blue slime (type 2) older
 * than 600 ticks slides to it at 0.2 and, within 3 and 2 high, is absorbed (power
 * +0xab8 += 4, max 5).  Every 300 ticks the power above 1 sheds 1.34 (a ring of
 * 10 particles).  +0xabc eases 0.1 a tick toward 0.15 + power·0.15.  While fewer
 * blue slimes live than the dungeon's cap (1; 3 / 4 / 5 in dungeons 2 / 3 / 4),
 * one spawns 10 ticks into the 600-tick cycle (+0xa64) at 2..10 from (-40, -40),
 * more than 10 from the player.  State 0 walks (hops 0.3 at 32 / 48 / 64) to a
 * point 8 from the player; then on the ground state 2: the leap (vy 1, homing at
 * 0.025 for 20 ticks, the landing record 14 + shake + wobble 16 at 31).  State 8
 * (set elsewhere): the shake at 30, back at 240. */
static void actor_ai_t23(actor_t *a)
{
    static const float k3 = 3.0f, k0p2 = 0.20000000298023224f, k1p34 = 1.340000033378601f;
    static const float k0p45 = 0.44999998807907104f, k0p75 = 0.75f, k0p025 = 0.02500000037252903f;
    static const float k0p015 = 0.014999999664723873f, k2p5 = 2.5f, k10 = 10.0f;
    int32_t count = 0;
    const float dx0 = spill32((long double)PX - AF(a, 0x3f0));
    const float dz0 = spill32((long double)PZ - AF(a, 0x3f8));
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        AF(a, 0xab8) = k5;
    }
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *b = &g_actor[i];
        if (AI(b, ACT_INUSE) == 0 || AI(b, ACT_TYPE) != 2) continue;
        if (AI(b, 0x398) >= 600) {
            const float ddx = spill32((long double)AF(a, 0x3f0) - AF(b, 0x3f0));
            const float ddy = spill32((long double)AF(a, 0x3f4) - AF(b, 0x3f4));
            const float ddz = spill32((long double)AF(a, 0x3f8) - AF(b, 0x3f8));
            const float d = spill32(crt_sqrt((double)((long double)ddz * ddz + (long double)ddx * ddx)));
            if (d >= k3) {
                AF(b, 0x3f0) = spill32((long double)ddx * k0p2 / d + AF(b, 0x3f0));
                AF(b, 0x3f8) = spill32((long double)ddz * k0p2 / d + AF(b, 0x3f8));
            } else if ((ddy < 0.0f ? -ddy : ddy) < k2 && AI(b, ACT_INUSE) != 2) {
                AI(b, ACT_INUSE) = 0;
                camera_shake(8, 4, 4, k1);           /* FUN_0044b219 */
                audio_play_se_by_id(0x13f);
                scene1_spawn(0, AF(b, 0x3f0), spill32((long double)AF(b, 0x3f4) + k0p5),
                             AF(b, 0x3f8), 0x93, k0p2, AI(a, ACT_SLOT));   /* FUN_00447f4f */
                AI(a, 0xb90) = -1;
                AI(a, 0xb94) = 30;
                AI(a, 0xa5c) = 0;
                audio_play_se_by_id(0x151);
                AF(b, ACT_HP) = 0.0f;
                const float p = spill32((long double)AF(a, 0xab8) + k4);
                AI(b, 0xadc) = 0;
                AF(a, 0xab8) = p;
                if (p >= k5) AF(a, 0xab8) = k5;
            }
        }
        count++;
    }
    if (AI(a, 0xa54) == 8) {
        if (++AI(a, 0xa58) == 30) {
            camera_shake(30, 4, 4, k1);
            audio_play_se_by_id(0x16d);
            sim_screen_wobble(32);                   /* FUN_004532bc */
        }
        if (AI(a, 0xa58) == 240) {
            AI(a, 0xa54) = 0;
            AI(a, 0xa58) = 0;
        }
        return;
    }
    if (++AI(a, 0xa5c) >= 300) {
        AI(a, 0xa5c) = 0;
        if (AF(a, 0xab8) > k1) {
            audio_play_se_by_id(0x16e);
            const float p = spill32((long double)AF(a, 0xab8) - k1p34);
            AF(a, 0xab8) = p;
            if (p < k1) AF(a, 0xab8) = k1;
            for (int i = 0; i != 10; i++) {
                const double r = spill32((long double)spill32((float)i) * k_pi4);
                const float s = spill32(x87_crt_sin(r));
                const float px = spill32((long double)s + s + AF(a, 0x3f0));
                const float c = spill32(x87_crt_cos(r));
                const float pz = spill32((long double)c + c + AF(a, 0x3f8));
                scene1_spawn(0, px, spill32((long double)AF(a, 0x3f4) + k2p5), pz, 14, k0p45, 4);
            }
        }
    }
    const float sc = spill32((long double)AF(a, 0xab8) / k5 * k0p75 + k0p15);
    if ((long double)sc - k0p1 >= AF(a, 0xabc))
        AF(a, 0xabc) = spill32((long double)AF(a, 0xabc) + k0p1);
    else if ((long double)sc + k0p1 <= AF(a, 0xabc))
        AF(a, 0xabc) = spill32((long double)AF(a, 0xabc) - k0p1);
    if (AI(a, 0xa64) >= 10) AI(a, 0xa64)++;
    int32_t cap = 1;
    if (g_scene1_combat_stage_id == 4) cap = 5;
    if (g_scene1_combat_stage_id == 3) cap = 4;
    if (g_scene1_combat_stage_id == 2) cap = 3;
    if (count < cap) {
        if (AI(a, 0xa64) < 10) AI(a, 0xa64)++;
        if (AI(a, 0xa64) == 10) {
            float tx = 0.0f, tz = 0.0f;
            for (int i = 0; i != 30; i++) {
                const float r = spill32((long double)rng_next_unit() * k_2pi);
                const float rad = spill32((long double)rng_next_unit() * k8 + k2);
                const double X = r;
                tx = spill32((long double)spill32(x87_crt_sin(X)) * rad - k40);
                tz = spill32((long double)spill32(x87_crt_cos(X)) * rad - k40);
                const float ddx = spill32((long double)PX - tx);
                const float ddz = spill32((long double)PZ - tz);
                if (ddx == 0.0f && ddz == 0.0f) continue;
                const float d = spill32(crt_sqrt((double)((long double)ddz * ddz + (long double)ddx * ddx)));
                if (d > k10) break;
            }
            const actor_stage_t st = stage_now();
            actor_t *n = actor_spawn_enemy(&st, tx, k10, tz, 2);   /* FUN_0041f903 */
            if (n) AI(n, 0xb2c) = 1;                 /* retail does not check */
        }
    }
    if (AI(a, 0xa64) == 600) AI(a, 0xa64) = 0;
    AI(a, 0xb20) = 1;
    const int32_t st = AI(a, 0xa54);
    if (st == 2) {
        const int32_t t = ++AI(a, 0xa58);
        if (t == 1) AF(a, 0x400) = k1;
        AF(a, 0x3fc) = spill32((long double)AF(a, 0x3fc) * k0p95);
        AF(a, 0x404) = spill32((long double)AF(a, 0x404) * k0p95);
        if (t < 20) {
            const float d2 = spill32((long double)dz0 * dz0 + (long double)dx0 * dx0);
            if (d2 > 0.0f) {
                AF(a, 0x3fc) = spill32((long double)dx0 * k0p025);
                AF(a, 0x404) = spill32((long double)dz0 * k0p025);
            }
        }
        if (t == 31) {
            scene1_record_b_spawn_npc(a, 14, 1);  /* FUN_00445a8c */
            camera_shake(30, 4, 4, k1);
            audio_play_se_by_id(0x16d);
            sim_screen_wobble(16);
        }
        if (AI(a, 0xa58) > 60) {
            AI(a, 0xa54) = 0;
            AI(a, 0xa58) = 0;
        }
        return;
    }
    if (st != 0) return;
    actor_anim_set(a, 0);
    if (++AI(a, 0xa58) == 1) {
        const double r = spill32((long double)rng_next_unit() * k_2pi);
        const float tx = spill32((long double)spill32(x87_crt_sin(r)) * k8 + PX);
        const float tz = spill32((long double)spill32(x87_crt_cos(r)) * k8 + PZ);
        const float ddx = spill32((long double)tx - AF(a, 0x3f0));
        const float ddz = spill32((long double)tz - AF(a, 0x3f8));
        AF(a, 0x420) = spill32(crt_atan2(ddx, ddz));
        AI(a, 0xa60) = (int32_t)(rng_next15() & 0x7fu) + 0x3f;
    }
    const int32_t t = AI(a, 0xa58);
    if (t < 30) return;
    const int32_t e = t - 30;
    if (AF(a, 0x3f4) == AF(a, 0x3e0) && (e == 2 || e == 18 || e == 34))
        AF(a, 0x400) = k0p3;
    if (e >= 1 && e < 40) {
        const double h = AF(a, 0x420);
        AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p015 + AF(a, 0x3fc));
        AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(h)) * k0p015 + AF(a, 0x404));
    }
    if (e < AI(a, 0xa60) + 30) return;
    AI(a, 0xa58) = 0;
    if (AF(a, 0x3f4) == AF(a, 0x3e0)) AI(a, 0xa54) = 2;
}

/* FUN_00428718: a type 13 / 14 mushroom underground (+0xae4 1): the anim
 * holds, +0xa58 counts; from 40 it sinks 0.5 a tick (off the ground snap,
 * +0xa98 / +0xb08), dust 90..119, rises from 130, surfaces at 140; at 60 it
 * picks a spot 4..8 + the body reach away (up to 100 tries for a clear ray);
 * below -500 for 300 ticks (or spawned below -100) the slot frees. */
static void actor_ai_burrow(actor_t *a)
{
    static const float km100 = -100.0f, km500 = -500.0f;
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        scene1_record_b_spawn_npc(a, 10, 1);     /* FUN_00445a8c */
        AF(a, 0xaa8) = AF(a, 0x414);
        AI(a, 0xa98) = 1;
        AF(a, 0xaac) = AF(a, 0x418);
        AF(a, 0xab0) = AF(a, 0x41c);
        if (AF(a, 0x418) < km100) goto gone;
    }
    if (AI(a, 0x440) <= 0 && AI(a, 0xb18) <= 0) actor_anim_set(a, 0);   /* FUN_00482a51 */
    const int32_t t = ++AI(a, 0xa58);
    AF(a, 0xac0) = 1.0f;
    AF(a, 0xabc) = 1.0f;
    if (t == 40) {
        AI(a, 0xb08) = 1;
        AI(a, 0xa98) = 1;
    }
    if (t > 40) {
        const float d = (float)(t - 40);
        AF(a, 0xaac) = spill32((long double)AF(a, 0x3e0) - (long double)d * k0p5);
    }
    if (t > 130) {
        const float d = (float)(t - 130);
        AF(a, 0xaac) = spill32((long double)d * k0p5 + ((long double)AF(a, 0x3e0) - k5));
    }
    if (t > 90 && t < 120)
        scene1_spawn(0, AF(a, 0xaa8), AF(a, 0x3e0), AF(a, 0xab0), 14, k0p15, 1);   /* FUN_00447f4f */
    {
        mapcol_hit_t *gh = (mapcol_hit_t *)(void *)(a->b + 0xa08);
        if (mapcol_ground(spill32((long double)AF(a, 0x3fc) + AF(a, 0x3f0)),
                          spill32((long double)AF(a, 0x3f4) + k8),
                          spill32((long double)AF(a, 0x404) + AF(a, 0x3f8)), gh) != 0)
            AF(a, 0x3e0) = gh->height;
    }
    if (AF(a, 0x3e0) < km500 && ++AI(a, 0xa64) > 300) goto gone;
    AI(a, 0x9fc) = 1;
    if (AI(a, 0xa58) == 140) {
        AI(a, 0xa58) = 0;
        AI(a, 0xb08) = 0;
        AF(a, 0xaac) = AF(a, 0x3e0);
        AI(a, 0xa98) = 1;
    }
    {
        const float lo = spill32((long double)AF(a, 0x3e0) - k4);
        if (AF(a, 0xaac) < lo) {
            AF(a, 0xaac) = lo;
            AF(a, 0xac0) = 0.0f;
            AF(a, 0xabc) = 0.0f;
        }
    }
    if (AI(a, 0xa58) == 60) {
        const enemy_record_t *e = enemy_of(a);
        for (int i = 0; i != 100; i++) {
            const float r = spill32(((long double)rng_next_unit() + k1) * k4);          /* [ebp+8] */
            const float h = spill32((long double)rng_next_unit() * k_2pi);             /* [ebp-0x18] */
            const long double m = (long double)e->runtime_floats[6] * e->runtime_floats[0];
            float reach = spill32(m + m);                                              /* fadd st, st */
            reach = spill32((long double)reach + r);
            const float sx = spill32((long double)spill32(x87_crt_sin(h)) * reach);
            const float sz = spill32((long double)spill32(x87_crt_cos(h)) * reach);
            float frac = 0.0f;
            mapcol_hit_t hit;
            hit.tri = 0;
            hit.slot = 0;
            if (mapcol_ray(AF(a, 0x414), AF(a, 0x418), AF(a, 0x41c), sx, 0.0f, sz, &frac, &hit) == 0) {
                AF(a, 0xaa8) = spill32((long double)spill32(x87_crt_sin(h)) * r + AF(a, 0x414));
                AF(a, 0xaac) = AF(a, 0x418);
                AF(a, 0xab0) = spill32((long double)spill32(x87_crt_cos(h)) * r + AF(a, 0x41c));
                break;
            }
        }
    }
    {
        char b[256];
        snprintf(b, sizeof b, "Y:%f", (double)AF(a, 0x3f4));
        audio_mci_record_command(30, 7, b);     /* FUN_00451874 (debug row) */
    }
    AF(a, 0x3f0) = AF(a, 0xaa8);
    AF(a, 0x3f4) = AF(a, 0xaac);
    AF(a, 0x3f8) = AF(a, 0xab0);
    return;
gone:
    AI(a, ACT_INUSE) = 0;
    AI(a, 0xad0) = 0;
}

/* FUN_004284e1: a type 13 / 14 mushroom spawned underground (+0xae4 2, the
 * monster house / a split): it starts at tick 90 (130 unless +0xa5c), rises
 * 0.5 a tick from 4 below the ground after 130, dust 90..119, lands at 140. */
static void actor_ai_rise(actor_t *a)
{
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        scene1_record_b_spawn_npc(a, 10, 1);     /* FUN_00445a8c */
        AI(a, 0xa58) = 90;
        AF(a, 0xaa8) = AF(a, 0x414);
        AI(a, 0xa5c) = 1;
        AI(a, 0xa98) = 1;
        AF(a, 0xaac) = AF(a, 0x418);
        AI(a, 0xb08) = 1;
        AI(a, 0xb20) = 1;
        AF(a, 0xab0) = AF(a, 0x41c);
    }
    if (AI(a, 0x440) <= 0 && AI(a, 0xb18) <= 0) actor_anim_set(a, 0);   /* FUN_00482a51 */
    if (AI(a, 0xa5c) == 0 && AI(a, 0xa58) < 130) AI(a, 0xa58) = 130;
    ++AI(a, 0xa58);
    AF(a, 0xac0) = 1.0f;
    AF(a, 0xabc) = 1.0f;
    {
        mapcol_hit_t *gh = (mapcol_hit_t *)(void *)(a->b + 0xa08);
        if (mapcol_ground(spill32((long double)AF(a, 0x3fc) + AF(a, 0x3f0)),
                          spill32((long double)AF(a, 0x3f4) + k8),
                          spill32((long double)AF(a, 0x404) + AF(a, 0x3f8)), gh) != 0)
            AF(a, 0x3e0) = gh->height;
    }
    const int32_t t = AI(a, 0xa58);
    const float g = AF(a, 0x3e0);                /* [ebp+8] */
    if (t <= 130) {
        AF(a, 0xaac) = spill32((long double)g - k4);
    } else {
        const float d = (float)(t - 130);
        AF(a, 0xaac) = spill32((long double)d * k0p5 + ((long double)g - k4));
    }
    if (t > 90 && t < 120)
        scene1_spawn(0, AF(a, 0xaa8), g, AF(a, 0xab0), 14, k0p15, 1);   /* FUN_00447f4f */
    AI(a, 0x9fc) = 1;
    if (AI(a, 0xa58) == 140) {
        AI(a, 0xb08) = 0;
        AI(a, 0xa98) = 1;
        AF(a, 0xaac) = AF(a, 0x3e0);
    }
    if (AF(a, 0xaac) > AF(a, 0x3e0)) AF(a, 0xaac) = AF(a, 0x3e0);
    {
        const float lo = spill32((long double)AF(a, 0x3e0) - k4);
        if (AF(a, 0xaac) < lo) {
            AF(a, 0xaac) = lo;
            AF(a, 0xac0) = 0.0f;
            AF(a, 0xabc) = 0.0f;
        }
    }
    AF(a, 0x3f0) = AF(a, 0xaa8);
    AF(a, 0x3f4) = AF(a, 0xaac);
    AF(a, 0x3f8) = AF(a, 0xab0);
}

/* FUN_0041f759: a multi-part model's anim cross-fade — the new anim goes to the
 * other slot (+0x30 toggles), the blend +0x2c restarts at that slot's far end
 * (1.0 for slot 0, 0.0 for slot 1) and the slot's frame timer +0x54 at 0 */
static void actor_anim_blend(actor_t *a, int32_t anim)
{
    int32_t s = AI(a, 0x30);
    if (AI(a, 0x34 + 4 * s) == anim) return;
    s ^= 1;
    AI(a, 0x30) = s;
    AF(a, 0x2c) = s == 0 ? 1.0f : 0.0f;
    AI(a, 0x34 + 4 * s) = anim;
    AF(a, 0x54 + 4 * s) = 0.0f;
}

/* FUN_0042b6b7: types 24 / 59 / 60, the mushroom king (dungeon 1, floor 10).
 * Under 4 mushrooms on the floor it plants one (13 / 14, rising: +0xae4 2) every
 * 180 ticks somewhere 2..12 from the arena centre (-40, -40).  State (+0xa54):
 *   0 wander to a point 7..12 from the centre (anim 1; record 15 after 60
 *     ticks); with DAT_056db108 > 100 straight to the player (state 1); once
 *     near (< 8) or after 240 ticks and past 120, pick: the nearest grown
 *     mushroom (+0x398 >= 600, more than 4 away) half the time → 2, 1 in 20 the
 *     spin (3), else 3 in 4 the approach (1, a point 1.5 short of the player);
 *   1 the approach (0.02 / 0.022 / 0.025 by type, re-aimed 3 / 3.5 short of the
 *     player for the first 60 ticks), then a swing set chosen by rand: anim 2
 *     (one hit, record 14 at 25), anim 3 (two, 25 / 56), or for type 60 anim 17
 *     (three lunges 1 / 1 / 2 forward, the last with the shock ring 67, a shake
 *     and the wobble); dust 148 at 22 / 51 / 95;
 *   2 walk to the mushroom (1.4 in front of the mouth), eat it at 38 (+0xa74:
 *     13 → 2, 14 → 1), then 4 (14: the power-up, anim 16) or 5 (13: heal 1/6 of
 *     max HP with ring 67, a shake, a popup) — else back to 0 at 120;
 *   3 the spin: 13 records along a sine sweep, 37 or 38 (+0xa70);
 *   8 the stun: anim 13 to 200 (the 68 burst at 45), back to 0 at 240. */
static void actor_ai_t24(actor_t *a)
{
    static const float k1p5 = 1.5f, k3 = 3.0f, k3p5 = 3.5f, k6 = 6.0f, k7 = 7.0f, k10 = 10.0f,
                       k13 = 13.0f, k40 = 40.0f, k1000 = 1000.0f;
    static const float k1p4 = 1.399999976158142f;        /* 0x519b68 */
    static const float k0p022 = 0.02199999988079071f;    /* 0x519b60 */
    static const float k0p025 = 0.02500000037252903f;    /* 0x5193f4 */
    static const float k_pi20 = 0.15707963705062866f;    /* 0x519a9c */
    static const float k_3pi100 = 0.09424778074026108f;  /* 0x519b64 */
    static const float k_pi50 = 0.06283185631036758f;    /* 0x519ac4 */
    static const float k_5pi2 = 7.853981971740723f;      /* 0x519a34 */
    static const float k_3pi10 = 0.9424778819084167f;    /* 0x519a28 */

    const float dx = spill32((long double)PX - AF(a, 0x3f0));      /* [ebp-8] */
    float dz = spill32((long double)PZ - AF(a, 0x3f8));            /* [ebp+8] */
    if (spill32((long double)dz * dz + (long double)dx * dx) == 0.0f)
        dz = k0p01;                                                /* the sqrt's result is dropped */
    const float ang = spill32(crt_atan2(dx, dz));                  /* [ebp-4] */
    const int32_t type = AI(a, ACT_TYPE);
    AI(a, 0xb28) = 0;
    AI(a, 0xb20) = 1;
    AF(a, 0xab8) = type == 60 ? k3 : type == 59 ? k2 : k1p5;
    int n = 0;
    for (int i = 0; i < ACTOR_COUNT; i++) {
        const actor_t *m = &g_actor[i];
        if (AI(m, ACT_INUSE) == 0) continue;
        if (AI(m, ACT_TYPE) == 13) n++;
        if (AI(m, ACT_TYPE) == 14) n++;
    }
    if (AI(a, 0xa54) != 8) {
        if (++AI(a, 0xa64) == 10 && n < 4) {
            const float h = spill32((long double)rng_next_unit() * k_2pi);
            const float r = spill32((long double)rng_next_unit() * k10 + k2);
            const float x = spill32((long double)spill32(x87_crt_sin(h)) * r - k40);
            const float z = spill32((long double)spill32(x87_crt_cos(h)) * r - k40);
            uint32_t q;
            if (type == 60) q = rng_next15() & 3u;
            else if (type == 59) q = rng_next15() % 3u;
            else q = rng_next15() & 1u;
            const actor_stage_t st = stage_now();
            actor_t *m = actor_spawn_enemy(&st, x, 0.0f, z, q != 0 ? 13 : 14);   /* FUN_0041f903 */
            if (m) {   /* retail does not check */
                AI(m, 0xae4) = 2;
                AI(m, 0xb2c) = 1;
            }
        }
        if (AI(a, 0xa64) == 180) AI(a, 0xa64) = 0;
    }
    const int32_t st = AI(a, 0xa54);
    if (st == 8) {
        actor_anim_blend(a, ++AI(a, 0xa58) >= 200 ? 0 : 13);
        if (AI(a, 0xa58) == 45) {
            audio_play_se_by_id(672);
            sim_screen_wobble(32);                                 /* FUN_004532bc */
            scene1_spawn(0, AF(a, 0x3f0), spill32((long double)AF(a, 0x3f4) + k8),
                         spill32((long double)AF(a, 0x3f8) - k4), 68, k0p15, 1);
        }
        if (AI(a, 0xa58) == 240) {
            AI(a, 0xa54) = 0;
            AI(a, 0xa58) = 0;
        }
        return;
    }
    if (st == 5) {
        if (++AI(a, 0xa58) == 1) actor_anim_blend(a, 15);
        if (AI(a, 0xa58) == 11) {
            scene1_spawn(0, AF(a, 0x3f0), AF(a, 0x3f4), AF(a, 0x3f8), 67, k0p5, 1);
            camera_shake(30, 4, 4, k1);                            /* FUN_0044b219 */
            audio_play_se_by_id(365);
            sim_screen_wobble(32);
        }
        if (AI(a, 0xa58) == 40) {
            const float v = spill32((long double)AF(a, ACT_HP_MAX) / k6);
            AI(a, 0xa58) = 0;
            AI(a, 0xa5c) = 0;
            AI(a, 0xa54) = 0;
            const float hp = spill32((long double)v + AF(a, ACT_HP));
            AF(a, ACT_HP) = hp;
            if (hp > AF(a, ACT_HP_MAX)) AF(a, ACT_HP) = AF(a, ACT_HP_MAX);
            popup_add(AF(a, 0x3f0), spill32((long double)AF(a, 0x3f4) + k0p5), AF(a, 0x3f8),
                      (int32_t)v, 1);                              /* FUN_0044b1c0, __ftol */
        }
        return;
    }
    if (st == 4) {
        const int32_t t = ++AI(a, 0xa58);
        AI(a, 0xb28) = 1;
        AF(a, 0xab8) = 1.0f;
        if (t == 1) actor_anim_blend(a, 16);
        if (AI(a, 0xa58) == 20) audio_play_se_by_id(360);
        if (AI(a, 0xa58) == 120) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa5c) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st == 3) {
        AI(a, 0xa70) = 4;
        if (AI(a, 0xa58) == 0) actor_anim_blend(a, 13);
        if (++AI(a, 0xa58) < 15) AF(a, 0x420) = angle_ease(AF(a, 0x420), ang, k_pi20);   /* FUN_00482ae7 */
        for (int k = 0, i = 40; i != 144; i += 8, k++) {
            if (AI(a, 0xa58) != i) continue;
            const float f0 = AF(a, 0x420);
            const double w = (double)((long double)(float)k * k_5pi2 / k13);
            AF(a, 0x420) = spill32((long double)spill32(x87_crt_sin(w)) * k_3pi10 + AF(a, 0x420));
            if (AI(a, 0xa70) == 1) {
                scene1_record_b_spawn_npc(a, 37, 1);               /* FUN_00445a8c */
            } else if (AI(a, 0xa70) == 4) {
                scene1_record_b_spawn_npc(a, 38, 1);
                if ((k & 3) == 0) audio_play_se_by_id(330);
            }
            AF(a, 0x420) = f0;
        }
        if (AI(a, 0xa58) == 209) {
            AI(a, 0xa70) = 0;
            AI(a, 0xa58) = 0;
            AI(a, 0xa5c) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st == 2) {
        actor_t *m = &g_actor[AI(a, 0xa50)];
        if (AI(a, 0xa58) == 0) {
            if (++AI(a, 0xa5c) < 180) {
                const float s = spill32(x87_crt_sin(AF(a, 0x420)));
                const float ex = spill32((long double)AF(m, 0x3f0) - ((long double)s * k1p4 + AF(a, 0x3f0)));
                const float c = spill32(x87_crt_cos(AF(a, 0x420)));
                float ez = spill32((long double)AF(m, 0x3f8) - ((long double)c * k1p4 + AF(a, 0x3f8)));
                float d = spill32((long double)ez * ez + (long double)ex * ex);
                if (d > 0.0f) {
                    d = spill32(crt_sqrt(d));
                } else {
                    d = k0p01;
                    ez = k0p01;
                }
                const float h = angle_ease(AF(a, 0x420), spill32(crt_atan2(ex, ez)), k_3pi100);
                AF(a, 0x420) = h;
                AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p02 + AF(a, 0x3fc));
                AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * k0p02 + AF(a, 0x404));
                if (d > k1p4) return;
                ++AI(a, 0xa58);
                actor_anim_blend(a, 14);
                return;
            }
        } else {
            const int32_t t = ++AI(a, 0xa58);
            if (t == 48) AI(a, 0xa70) = AI(a, 0xa74);
            if (t == 38) {
                AI(a, 0xa74) = 0;
                if (AI(m, ACT_INUSE) == 1) {                        /* eaten */
                    AI(m, ACT_INUSE) = 0;
                    audio_play_se_by_id(337);
                    AI(a, 0xa74) = (AI(m, ACT_TYPE) == 13) + 1;
                }
            }
            if (AI(a, 0xa58) != 120) return;
        }
        if (AI(a, 0xa70) != 0) {
            AI(a, 0xad8) = -1;
            AI(a, 0xa54) = (AI(a, 0xa74) != 1) + 4;
        } else {
            AI(a, 0xa54) = 0;
            AI(a, 0xa5c) = 0;
        }
        AI(a, 0xa58) = 0;
        AI(a, 0xa5c) = 0;
        return;
    }
    if (st == 1) {
        if (AI(a, 0xa58) == 0) {
            const float ex = spill32((long double)AF(a, 0xa9c) - AF(a, 0x3f0));   /* [ebp-0x10] */
            ++AI(a, 0xa5c);
            const float ez = spill32((long double)AF(a, 0xaa4) - AF(a, 0x3f8));   /* [ebp-0xc] */
            float d = spill32((long double)ez * ez + (long double)ex * ex);
            if (d != 0.0f) d = spill32(crt_sqrt(d));
            const float speed = type == 60 ? k0p025 : type == 59 ? k0p022 : k0p02;
            if (AI(a, 0xa5c) < 60 && d > k5) {
                const float r = PW(0x56db108u).i > 0 ? k3 : k3p5;
                AF(a, 0xa9c) = spill32((long double)PX - (long double)spill32(x87_crt_sin(ang)) * r);
                AF(a, 0xaa4) = spill32((long double)PZ - (long double)spill32(x87_crt_cos(ang)) * r);
            }
            const float h = angle_ease(AF(a, 0x420), spill32(crt_atan2(ex, ez)), k_pi50);
            AF(a, 0x420) = h;
            AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * speed + AF(a, 0x3fc));
            AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * speed + AF(a, 0x404));
            if (d >= k4 && AI(a, 0xa5c) <= 120) return;            /* jb: arrive below 4 */
            ++AI(a, 0xa58);
            AF(a, 0x3fc) = 0.0f;
            AF(a, 0x404) = 0.0f;
            if (type == 60) {
                if (rng_next15() & 1u) {
                    AI(a, 0xa5c) = 2;
                    actor_anim_blend(a, 17);
                    return;
                }
            } else if (rng_next15() & 1u) {
                AI(a, 0xa5c) = 0;
                actor_anim_blend(a, 2);
                return;
            }
            AI(a, 0xa5c) = 1;
            actor_anim_blend(a, 3);
            return;
        }
        if (AI(a, 0xa58) < 20) AF(a, 0x420) = angle_ease(AF(a, 0x420), ang, k_pi20);
        const int32_t t = ++AI(a, 0xa58);
        if (t == 22) scene1_spawn(0, AF(a, 0x3f0), AF(a, 0x3f4), AF(a, 0x3f8), 148, k1, 1);
        if (AI(a, 0xa58) == 51) scene1_spawn(0, AF(a, 0x3f0), AF(a, 0x3f4), AF(a, 0x3f8), 148, k1, 1);
        if (AI(a, 0xa58) == 95) scene1_spawn(0, AF(a, 0x3f0), AF(a, 0x3f4), AF(a, 0x3f8), 148, k1, 1);
        int end;
        const int32_t sub = AI(a, 0xa5c);
        if (sub == 0) {
            if (AI(a, 0xa58) == 20) audio_play_se_by_id(343);
            if (AI(a, 0xa58) == 25) scene1_record_b_spawn_npc(a, 14, 1);
            end = AI(a, 0xa58) == (type == 59 ? 60 : 75);
        } else if (sub == 1) {
            if (AI(a, 0xa58) == 20) audio_play_se_by_id(343);
            if (AI(a, 0xa58) == 51) audio_play_se_by_id(343);
            if (AI(a, 0xa58) == 25) scene1_record_b_spawn_npc(a, 14, 1);
            if (AI(a, 0xa58) == 56) scene1_record_b_spawn_npc(a, 14, 1);
            end = AI(a, 0xa58) == (type == 59 ? 99 : 114);
        } else {
            /* the lunges: the records go out from a point stepped forward, the
             * body stays (x / y / z restored) */
            if (AI(a, 0xa58) == 20) audio_play_se_by_id(343);
            if (AI(a, 0xa58) == 51) audio_play_se_by_id(343);
            if (AI(a, 0xa58) == 95) audio_play_se_by_id(343);
            const float sx = AF(a, 0x3f0), sy = AF(a, 0x3f4), sz = AF(a, 0x3f8);
            if (AI(a, 0xa58) == 25 || AI(a, 0xa58) == 56) {
                AF(a, 0x3f0) = spill32((long double)spill32(x87_crt_sin(AF(a, 0x420))) + AF(a, 0x3f0));
                AF(a, 0x3f8) = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) + AF(a, 0x3f8));
                scene1_record_b_spawn_npc(a, 14, 1);
            }
            if (AI(a, 0xa58) == 100) {
                const long double s2 = spill32(x87_crt_sin(AF(a, 0x420)));
                AF(a, 0x3f0) = spill32((s2 + s2) + AF(a, 0x3f0));
                const long double c2 = spill32(x87_crt_cos(AF(a, 0x420)));
                AF(a, 0x3f8) = spill32((c2 + c2) + AF(a, 0x3f8));
                scene1_record_b_spawn_npc(a, 14, 1);
                audio_play_se_by_id(365);
                const float h = AF(a, 0x420);                       /* [ebp-0x10] */
                const float s = spill32(x87_crt_sin(AF(a, 0x420)));
                const float c = spill32(x87_crt_cos(h));
                scene1_spawn(0, spill32((long double)s * k8 + AF(a, 0x3f0)), AF(a, 0x3f4),
                             spill32((long double)c * k8 + AF(a, 0x3f8)), 67, k1, 1);
                camera_shake(10, 4, 4, k1);
                sim_screen_wobble(30);
            }
            AF(a, 0x3f0) = sx;
            AF(a, 0x3f4) = sy;
            AF(a, 0x3f8) = sz;
            end = AI(a, 0xa58) == 141;
        }
        if (end) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa5c) = 0;
            AI(a, 0xa54) = 0;
        }
        return;
    }
    if (st != 0) return;
    actor_anim_blend(a, 1);
    if (++AI(a, 0xa60) > 60) scene1_record_b_spawn_npc(a, 15, 1);
    if (AI(a, 0xa58) == 0) {
        const float h = spill32((long double)rng_next_unit() * k_2pi);
        const float r = spill32((long double)rng_next_unit() * k5 + k7);
        AF(a, 0xa9c) = spill32((long double)spill32(x87_crt_sin(h)) * r - k40);
        AF(a, 0xaa4) = spill32((long double)spill32(x87_crt_cos(h)) * r - k40);
    }
    ++AI(a, 0xa58);
    if (PW(0x56db108u).i > 100) {
        AI(a, 0xa58) = 0;
        AI(a, 0xa5c) = 0;
        AI(a, 0xa54) = 1;
        AF(a, 0xa9c) = spill32((long double)PX - (long double)spill32(x87_crt_sin(ang)) * k3);
        AF(a, 0xaa4) = spill32((long double)PZ - (long double)spill32(x87_crt_cos(ang)) * k3);
        return;
    }
    const float ex = spill32((long double)AF(a, 0xa9c) - AF(a, 0x3f0));
    float ez = spill32((long double)AF(a, 0xaa4) - AF(a, 0x3f8));
    float d = spill32((long double)ez * ez + (long double)ex * ex);
    if (d > 0.0f) {
        d = spill32(crt_sqrt(d));
    } else {
        d = k0p01;
        ez = k0p01;
    }
    const float h = angle_ease(AF(a, 0x420), spill32(crt_atan2(ex, ez)), k_pi50);
    AF(a, 0x420) = h;
    AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p02 + AF(a, 0x3fc));
    AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * k0p02 + AF(a, 0x404));
    if (!(d >= k8) || AI(a, 0xa58) > 240) AI(a, 0xa5c) |= 1;     /* jb */
    if (AI(a, 0xa58) > 120) AI(a, 0xa5c) |= 2;
    if (AI(a, 0xa5c) != 3) return;
    float best = k1000;
    AI(a, 0xa50) = -1;
    AI(a, 0xa58) = 0;
    for (int i = 0; i < ACTOR_COUNT; i++) {
        const actor_t *m = &g_actor[i];
        if (AI(m, ACT_INUSE) == 0 || AI(m, 0x398) < 600) continue;
        if (AI(m, ACT_TYPE) != 13 && AI(m, ACT_TYPE) != 14) continue;
        const float mx = spill32((long double)AF(m, 0x3f0) - AF(a, 0x3f0));
        const float mz = spill32((long double)AF(m, 0x3f8) - AF(a, 0x3f8));
        float md = spill32((long double)mz * mz + (long double)mx * mx);
        if (md > 0.0f) md = spill32(crt_sqrt(md));
        if (md > k4 && md < best) {
            AI(a, 0xa50) = i;
            best = md;
        }
    }
    if (AI(a, 0xa50) != -1 && (rng_next15() & 1u) == 0) {
        AI(a, 0xa5c) = 0;
        AI(a, 0xa54) = 2;
        return;
    }
    if (rng_next15() % 20u == 0) {
        AI(a, 0xa54) = 3;
        return;
    }
    if ((rng_next15() & 3u) == 0) return;
    AI(a, 0xa54) = 1;
    AI(a, 0xa58) = 0;
    AI(a, 0xa5c) = 0;
    AI(a, 0xa60) = 0;
    AF(a, 0xa9c) = spill32((long double)PX - (long double)spill32(x87_crt_sin(ang)) * k1p5);
    AF(a, 0xaa4) = spill32((long double)PZ - (long double)spill32(x87_crt_cos(ang)) * k1p5);
}

/* FUN_0042ce17: types 6..9 (the bats, +0xae4 != 3) fly (+0xa00 = 1), bobbing
 * 0.5 ± 0.3 over the ground on a sine of +0xa58 (the climb eases at 0.95,
 * clamped to ±0.1).  State 0 walks the line-up step (FUN_00423789: reach 18,
 * wait 20, 0.025, from 5, rest 30); in line → state 1: anim 3, the bite
 * (record 31 + SE 330 at 26 unless stunned, +0xb14), anim 0 from 60, back to
 * 0 after 91. */
static void actor_ai_t6(actor_t *a)
{
    static const float k0p04 = 0.03999999910593033f, k0p019 = 0.01899999938905239f;
    static const float km0p1 = -0.10000000149011612f, k0p025 = 0.02500000037252903f, k18 = 18.0f;
    const float ph = spill32((long double)spill32((float)AI(a, 0xa58)) * k0p04);
    ++AI(a, 0xa5c);
    AI(a, 0xa00) = 1;
    const float h = spill32((long double)spill32(x87_crt_sin(ph)) * k0p3 + k0p5);
    if ((long double)h + AF(a, 0x3e0) > AF(a, 0x3f4))
        AF(a, 0x400) = spill32((long double)AF(a, 0x400) * k0p95 + k0p019);
    else if ((long double)h + k1 + AF(a, 0x3e0) < AF(a, 0x3f4))
        AF(a, 0x400) = spill32(((long double)AF(a, 0x400) - k0p02) * k0p95);
    if (AF(a, 0x400) > k0p1) AF(a, 0x400) = k0p1;
    if (AF(a, 0x400) < km0p1) AF(a, 0x400) = km0p1;
    if (AI(a, 0xa54) == 0) {
        actor_anim_set(a, 0);
        if (actor_ai_line_up(a, k18, 20, k0p025, 5, 30, 0)) {
            AI(a, 0xa5c) = 0;
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 1;
            actor_anim_set(a, 3);
        }
    } else if (AI(a, 0xa54) == 1) {
        const int32_t t = ++AI(a, 0xa58);
        if (t >= 60) actor_anim_set(a, 0);
        else if (t >= 0) actor_anim_set(a, 3);
        if (AI(a, 0xa58) == 26 && AI(a, 0xb14) <= 0) {
            scene1_record_b_spawn_npc(a, 31, 1);    /* FUN_00445a8c */
            audio_play_se_by_id(330);
        }
        if (AI(a, 0xa58) >= 1 && AI(a, 0xa58) - 1 == 90) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
    }
}

/* FUN_00424509: types 6..9 with +0xae4 == 3 — a fresh one (+0xb8c) spawns
 * record 0x99 and flies; it holds 1 up and drifts along its facing at
 * 0.3..0.4 a tick; from 40 (once +0x9fc is clear) it turns normal, at 100 it
 * is gone (+0x428 = 0). */
static void actor_ai_t6_drift(actor_t *a)
{
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        scene1_record_b_spawn_npc(a, 0x99, 1);
        AI(a, 0xa00) = 1;
        AI(a, 0xb04) = 1;
        AI(a, 0xb08) = 1;
        AI(a, 0xac8) = 1;
        AI(a, 0xacc) = 1;
    }
    AF(a, 0x3f4) = k1;
    const float r = spill32((long double)rng_next_unit() * k0p1 + k0p3);
    AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(AF(a, 0x420))) * r);
    AF(a, 0x400) = 0.0f;
    AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * r);
    actor_anim_set(a, 0);
    const int32_t t = ++AI(a, 0xa58);
    if (t >= 40 && AI(a, 0x9fc) == 0) {
        AI(a, 0xb04) = 0;
        AI(a, 0xb08) = 0;
        AI(a, 0xae4) = 0;
    }
    if (t >= 100) {
        AI(a, 0x428) = 0;
        AI(a, 0xad0) = 0;
    }
}

/* FUN_00427f59: types 44..48 — the line-up step (FUN_00423789, reach =
 * lunge·260) with per-type tunings, then the lunge: record 13 at 20, the push
 * along the facing for ticks 16..35, back to 0 at the type's end.  +0xb9c
 * (set every tick) clears once the lunge passes the type's gate.  State 4
 * holds 20 ticks (anim 1). */
static void actor_ai_t44(actor_t *a)
{
    static const float k0p015 = 0.014999999664723873f, k0p025 = 0.02500000037252903f;
    static const float k0p035 = 0.03500000014901161f, k0p04 = 0.03999999910593033f;
    static const float k260 = 260.0f;
    float speed = k0p025, lunge = k0p05;
    int32_t end = 90, wait = 20, start = 5, rest = 30, min_runs = 0, gate = 20;
    if (AI(a, 0xb8c) != 0) {
        AI(a, 0xb8c) = 0;
        AI(a, 0xb9c) = 1;
    }
    AI(a, 0xb9c) = 1;
    switch (AI(a, ACT_TYPE)) {
    case 44: speed = k0p015; end = 60; lunge = k0p03; gate = 5; start = 20; wait = 40; rest = 40; min_runs = 2; break;
    case 45: speed = k0p02; end = 80; lunge = k0p03; gate = 10; rest = 20; start = 30; wait = 30; min_runs = 1; break;
    case 46: speed = k0p035; end = 90; lunge = k0p04; min_runs = 1; break;
    case 47: speed = k0p02; end = 70; lunge = k0p04; start = 0; rest = 15; break;
    case 48: end = 50; break;
    default: break;
    }
    const int32_t st = AI(a, 0xa54);
    if (st == 1 && AI(a, 0xa58) >= gate) AI(a, 0xb9c) = 0;
    if (st == 0) {
        actor_anim_set(a, 0);
        const float reach = spill32((long double)lunge * k260);
        if (actor_ai_line_up(a, reach, wait, speed, start, rest, min_runs) &&
            AF(a, 0x3f4) == AF(a, 0x3e0)) {
            AI(a, 0xa5c) = 0;
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 1;
            actor_anim_set(a, 3);
        }
    } else if (st == 1) {
        if (++AI(a, 0xa58) == 20) scene1_record_b_spawn_npc(a, 13, 1);   /* FUN_00445a8c */
        const int32_t t = AI(a, 0xa58);
        if (t >= 1) {
            const int32_t u = t - 1;
            if (u >= 15 && u < 35) {
                AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(AF(a, 0x420))) * lunge + AF(a, 0x3fc));
                AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * lunge + AF(a, 0x404));
            }
            if (u == end) {
                AI(a, 0xa58) = 0;
                AI(a, 0xa54) = 0;
            }
        }
    } else if (st == 4) {
        if (++AI(a, 0xa58) == 20) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
            actor_anim_set(a, 1);
        }
    }
}

/* FUN_004976ae: Charme's voice — 0 one of eight attack lines (the 24-pick bag
 * FUN_0049a2a9), 1 one of two (FUN_00499fda) */
static void charme_voice(int32_t kind)
{
    static const char *const k_attack[8] = {
        "bin/se/03sy/action/03sy_01a.bin", "bin/se/03sy/action/03sy_01b.bin",
        "bin/se/03sy/action/03sy_02a.bin", "bin/se/03sy/action/03sy_02b.bin",
        "bin/se/03sy/action/03sy_03a.bin", "bin/se/03sy/action/03sy_03b.bin",
        "bin/se/03sy/action/03sy_04a.bin", "bin/se/03sy/action/03sy_04b.bin",
    };
    if (kind == 0) {
        const uint32_t v = (uint32_t)voice_bag8();
        if (v <= 7) audio_play_se_file(k_attack[v]);   /* FUN_0049933c */
    } else if (kind == 1) {
        const int32_t v = voice_bag2();
        if (v == 0) audio_play_se_file("bin/se/03sy/action/03sy_06a.bin");
        else if (v == 1) audio_play_se_file("bin/se/03sy/action/03sy_06b.bin");
    }
}

static int charme_ray(const actor_t *a, float sx, float sz)
{
    float frac = 0.0f;
    mapcol_hit_t rh;
    rh.slot = 0;
    rh.tri = 0;
    return mapcol_ray(AF(a, 0x3f0), spill32((long double)AF(a, 0x3f4) + k1), AF(a, 0x3f8),
                      sx, 0.0f, sz, &frac, &rh);
}

/* FUN_0041fa6e: type 25, Charme (dungeon 1, floor 14).  Hit 3+ times
 * (+0xa60 > 2) she flees: a hop to a point 10+ from the player inside the
 * arena (±10 around (-40, -40), 30 tries), steered 20 ticks at 0.08, back to
 * 0 once landed.  Else by state (+0xa54):
 *   8 the entrance (event stages 0 / 1: the hop at 240, the slide −z to 268;
 *     stage 3 → 0; in dungeon 5 back to 0 at 240);
 *   0 the stalk: a plan per run (+0xae4: 0..3, two more chances at 2), every
 *     30 ticks a heading near a point 4 from the player (20 rays of 4), the
 *     walk at 0.047 from tick 6, a run of rand&63 + 30; from 10 a plan is
 *     taken when the player lines up (1.5 off an axis: plan 0 → 3 the dash
 *     within 8, plan 1 → 2 within 18 near the centre with a clear 12-ray,
 *     plan 2 → 1 within 4; under 3 it becomes plan 2);
 *   1 the slash: anim 3, record 0x10 + her line at 7, done at 28;
 *   2 the spin: anim 5, record 0x53 at 12, done at 51;
 *   3 the dash: anim 6 then 4 from 45, the cry at 1, ticks 45..64 the push at
 *     0.08 with the afterimages (5, 0x73), record 0x15 at 50, done at 110.
 * State 0 walks (anim 1) while faster than 0.05. */
static void actor_ai_t25(actor_t *a)
{
    static const float k0p08 = 0.07999999821186066f, k3 = 3.0f, k1p5 = 1.5f, k65536 = 65536.0f;
    static const float k10 = 10.0f, k18 = 18.0f, km0p4 = -0.4000000059604645f, k0p047 = 0.04699999839067459f;
    const float px = PX, pz = PZ;
    float dx = spill32((long double)px - AF(a, 0x3f0));
    float dz = spill32((long double)pz - AF(a, 0x3f8));
    if (AI(a, 0xb08) > 0) AI(a, 0xb08)--;
    ++AI(a, 0xa58);
    ++AI(a, 0xa64);
    if (AI(a, 0xa60) > 2) {
        AI(a, 0xb08) = 1;
        actor_anim_set(a, 6);
        if (AI(a, 0xa58) == 1) {
            AF(a, 0x400) = k1;
            for (int i = 0; i != 30; i++) {
                AF(a, 0xaa8) = spill32(((long double)rng_next_unit() - k0p5) * k20 - k40);
                const float tz = spill32(((long double)rng_next_unit() - k0p5) * k20 - k40);
                AF(a, 0xab0) = tz;
                const float ddx = spill32((long double)PX - AF(a, 0xaa8));
                const float ddz = spill32((long double)PZ - tz);
                if (ddx == 0.0f && ddz == 0.0f) continue;
                const float d = spill32(crt_sqrt(spill64((long double)ddz * ddz + (long double)ddx * ddx)));
                if (d > k10) break;
            }
        }
        AF(a, 0x3fc) = spill32((long double)AF(a, 0x3fc) * k0p95);
        AF(a, 0x404) = spill32((long double)AF(a, 0x404) * k0p95);
        if (AI(a, 0xa58) < 20) {
            const float d2 = spill32((long double)dz * dz + (long double)dx * dx);
            if (d2 > 0.0f) {
                AF(a, 0x3fc) = spill32(((long double)AF(a, 0xaa8) - AF(a, 0x3f0)) * k0p08);
                AF(a, 0x404) = spill32(((long double)AF(a, 0xab0) - AF(a, 0x3f8)) * k0p08);
            }
        }
        if (AI(a, 0xa58) > 20 && AF(a, 0x3f4) == AF(a, 0x3e0)) {
            AI(a, 0xa54) = 0;
            AI(a, 0xa58) = 0;
            AI(a, 0xa60) = 0;
        }
        return;
    }
    const int32_t t = AI(a, 0xa58);
    switch (AI(a, 0xa54)) {
    case 8:
        if (g_scene1_combat_stage_id == 5) {
            if (t >= 240) {
                AI(a, 0xa58) = 0;
                AI(a, 0xa54) = 0;
            }
            break;
        }
        if (g_event_stage < 2) {
            if (t == 240) AF(a, 0x400) = k0p8;
            if (t >= 240 && t < 268) AF(a, 0x404) = km0p4;
        }
        if (g_event_stage == 3) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        break;
    case 0: {
        if (t == 1) {
            AI(a, 0xae4) = (int32_t)(rng_next15() & 3u);
            if (rng_next15() & 1u) AI(a, 0xae4) = 2;
            if ((rng_next15() & 3u) == 0) AI(a, 0xae4) = 2;
        }
        if (AI(a, 0xa58) % 30 == 1) {
            const float r = spill32((long double)rng_next_unit() * k_2pi);
            AI(a, 0xa5c) = (int32_t)(rng_next15() & 0x3fu);
            const double X = r;
            const float tx = spill32((long double)spill32(x87_crt_sin(X)) * k4 + PX);
            const float tz = spill32((long double)spill32(x87_crt_cos(X)) * k4 + PZ);
            float ddx = spill32((long double)tx - AF(a, 0x3f0));
            float ddz = spill32((long double)tz - AF(a, 0x3f8));
            const float adx = spill32(fabsl((long double)ddx));
            const float adz = spill32(fabsl((long double)ddz));
            if (adx > adz) ddz = spill32((long double)ddz * k0p5);
            else ddx = spill32((long double)ddx * k0p5);
            for (int i = 0; i != 20; i++) {
                const float q = spill32((long double)(rng_next15() & 3u));
                AF(a, 0x420) = spill32((long double)q * k_pi2);
                if (rng_next15() % 3u != 0)
                    AF(a, 0x420) = spill32(crt_atan2(ddx, ddz));
                const float sx = spill32((long double)spill32(x87_crt_sin(AF(a, 0x420))) * k4);
                const float sz = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * k4);
                if (charme_ray(a, sx, sz) == 0) break;
            }
        }
        if (AI(a, 0xa58) >= 10) {
            float reach = k8, off = k1p5;
            int go = 0;
            const int32_t plan = AI(a, 0xae4);
            if (plan == 1) reach = k18;
            else if (plan == 2) { reach = k4; off = 0.0f; }
            if (plan == 1) {
                const float a1 = spill32(fabsl((long double)spill32((long double)km40 - PX)));
                const float a2 = spill32(fabsl((long double)spill32((long double)km40 - PZ)));
                if (a1 < k10 && a2 < k10) {
                    const float sx = spill32((long double)spill32(x87_crt_sin(AF(a, 0x420))) * k12);
                    const float sz = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * k12);
                    if (charme_ray(a, sx, sz) == 0) go = 1;
                }
            } else {
                float ax = spill32(fabsl((long double)dx)), az;
                if (ax < k1p5 && (az = spill32(fabsl((long double)dz))) > off &&
                    spill32(fabsl((long double)dz)) < reach) {
                    if (spill32(fabsl((long double)dz)) < k3) AI(a, 0xae4) = 2;
                    dx = 0.0f;
                    go = 1;
                } else if ((az = spill32(fabsl((long double)dz))) < k1p5 &&
                           (ax = spill32(fabsl((long double)dx))) > off &&
                           spill32(fabsl((long double)dx)) < reach) {
                    if (spill32(fabsl((long double)dx)) < k3) AI(a, 0xae4) = 2;
                    dz = 0.0f;
                    go = 1;
                }
            }
            if (AI(a, 0xae4) != 3 && go) {
                AF(a, 0x420) = spill32(crt_atan2(dx, dz));
                if (AF(a, 0x3f4) == AF(a, 0x3e0)) {
                    AI(a, 0xa58) = 0;
                    const int32_t p2 = AI(a, 0xae4);
                    AI(a, 0xa54) = p2 == 0 ? 3 : (p2 == 1) + 1;
                }
            }
        }
        const int32_t u = AI(a, 0xa58);
        if (u >= 5) {
            const int32_t e = u - 5;
            if (AI(a, 0xae4) == 3) {
                AF(a, 0x420) = spill32(crt_atan2(dx, dz));
            } else if (e >= 1) {
                AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(AF(a, 0x420))) * k0p047 + AF(a, 0x3fc));
                AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * k0p047 + AF(a, 0x404));
            }
            if (e >= AI(a, 0xa5c) + 30) AI(a, 0xa58) = 0;
        }
        break;
    }
    case 1:
        actor_anim_set(a, t >= 3 ? 3 : 0);
        if (AI(a, 0xa58) == 7) {
            charme_voice(0);                         /* FUN_004976ae */
            audio_play_se_by_id(0x157);
            scene1_record_b_spawn_npc(a, 0x10, 1);   /* FUN_00445a8c */
        }
        if (AI(a, 0xa58) >= 28) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        break;
    case 2:
        actor_anim_set(a, 5);
        if (AI(a, 0xa58) == 12) {
            scene1_record_b_spawn_npc(a, 0x53, 1);
            charme_voice(0);
            audio_play_se_by_id(0x157);
        }
        if (AI(a, 0xa58) >= 1 && AI(a, 0xa58) - 1 == 50) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        break;
    case 3: {
        actor_anim_set(a, t < 45 ? 6 : 4);
        if (AI(a, 0xa58) == 1) charme_voice(1);
        if (AI(a, 0xa58) == 50) {
            audio_play_se_by_id(0x15d);
            scene1_record_b_spawn_npc(a, 0x15, 1);
        }
        const int32_t v = AI(a, 0xa58);
        if (v >= 1 && v >= 45 && v < 65) {
            const float h = AF(a, 0x420);
            AI(a, 0xb08) = 1;
            AF(a, 0x3fc) = spill32((long double)spill32(x87_crt_sin(h)) * k0p08 + AF(a, 0x3fc));
            AF(a, 0x404) = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * k0p08 + AF(a, 0x404));
            const float sx = spill32((long double)spill32(x87_crt_sin(AF(a, 0x420))) * k3 + AF(a, 0x3f0));
            const float sy = spill32((long double)AF(a, 0x3f4) + k1p5);
            const float sz = spill32((long double)spill32(x87_crt_cos(AF(a, 0x420))) * k3 + AF(a, 0x3f8));
            if (AI(a, 0xa58) & 1) scene1_spawn(0, sx, sy, sz, 5, k0p5, 1);   /* FUN_00447f4f */
            scene1_spawn(0, sx, sy, sz, 0x73, k0p3, x87_ftol((long double)AF(a, 0x420) * k65536));
        }
        if (AI(a, 0xa58) == 65) AI(a, 0xb08) = 0;
        if (AI(a, 0xa58) == 110) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa54) = 0;
        }
        break;
    }
    default:
        break;
    }
    if (AI(a, 0xa54) == 0) {
        const float vz = AF(a, 0x404), vx = AF(a, 0x3fc);
        const float s2 = spill32((long double)vz * vz + (long double)vx * vx);
        const float d = spill32(crt_sqrt(s2));
        actor_anim_set(a, d > k0p05 ? 1 : 0);
    }
}

/* FUN_0042f28c */
void actor_ai_alive(actor_t *a)
{
    if (AI(a, 0xb40) == 1) {
        actor_anim_set(a, 0);
        actor_ai_ring(a);                        /* FUN_0042c8a5 */
    } else {
        const int32_t sub = AI(a, 0xae4), type = AI(a, ACT_TYPE);
        if (sub == 1 && (type == 13 || type == 14)) {
            actor_ai_burrow(a);                  /* FUN_00428718 */
        } else if (sub == 2 && (type == 13 || type == 14)) {
            actor_ai_rise(a);                    /* FUN_004284e1 */
        } else if (AI(a, 0x440) > 0 || AI(a, 0xb18) > 0) {
            if (AI(a, 0xa00) == 1) AF(a, 0x400) = spill32((long double)AF(a, 0x400) * 0.75f);
        } else if (type >= 0 && type <= 5) {
            actor_ai_slime(a);
        } else if (type >= 6 && type <= 9) {
            if (sub == 3) actor_ai_t6_drift(a);  /* FUN_00424509 */
            else actor_ai_t6(a);                 /* FUN_0042ce17 */
        } else if (type >= 10 && type <= 12) {
            actor_ai_bee(a);                     /* FUN_00428f0a */
        } else if (type == 13 || type == 14) {
            actor_ai_t13(a);                     /* FUN_00428ae4 */
        } else if (type == 22) {
            actor_ai_t22(a);                     /* FUN_00429962 */
        } else if (type >= 44 && type <= 48) {
            actor_ai_t44(a);                     /* FUN_00427f59 */
        } else if (type == 23) {
            actor_ai_t23(a);                     /* FUN_0042aef7 */
        } else if (type == 24 || type == 59 || type == 60) {
            actor_ai_t24(a);                     /* FUN_0042b6b7 */
        } else if (type == 25) {
            actor_ai_t25(a);                     /* FUN_0041fa6e */
        } else if (type == 16 && sub != 3) {
            actor_ai_t16(a);
        } else if ((type >= 17 && type <= 21) || (type >= 56 && type <= 58)) {
            actor_ai_flier(a);                   /* FUN_0042a74c */
        } else {
            /* PORT-DEBT(stub, FUN_0042f28c): the other types' AI (FUN_0042ce17,
             * FUN_00428f0a, FUN_00428ae4, FUN_0042a33e, … FUN_00428213 for
             * the rest) */
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

/* FUN_00430c00: the actors during an event (the dialogue arm FUN_004427d3):
 * the multi-part kinds (enemy flags != 0) only advance their two anim timers
 * (+0x54 / +0x58) by 1, the rest tick their sprite anim unless knocked back
 * (+0xb18); then FUN_0042ea35 */
void actor_event_anim_pump(void)
{
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *a = &g_actor[i];
        if (AI(a, ACT_INUSE) == 0) continue;
        const enemy_record_t *e = enemy_of(a);
        if (e->flags != 0) {
            AF(a, 0x54) = spill32((long double)AF(a, 0x54) + k1);
            AF(a, 0x58) = spill32((long double)AF(a, 0x58) + k1);
        } else if (AI(a, 0xb18) == 0) {
            chr_anim_tick((int32_t *)(void *)a->b, e->unknown_24, 1.0f);   /* FUN_00482a71 */
        }
    }
    /* PORT-DEBT(stub, FUN_0042ea35): the multi-part bone timers (no rand) */
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
                actor_dying(a);              /* FUN_0042f7ad */
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
