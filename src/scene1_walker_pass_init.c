/*
 * scene1_walker_pass_init.c — PII.3a port of FUN_00457714 phase 2.
 * See scene1_walker_pass_init.h for the chip writeup + asm refs.
 */

#include "scene1_walker_pass_init.h"

#include <stddef.h>
#include <string.h>

#include "math3d.h"

/* ─── per-mesh field arrays (BSS-zero by default) ─────────────────── */

int32_t g_scene1_walker_phase2_mesh_type[SCENE1_WALKER_PHASE2_MAX];
float   g_scene1_walker_phase2_rot_y    [SCENE1_WALKER_PHASE2_MAX];
float   g_scene1_walker_phase2_pos_y    [SCENE1_WALKER_PHASE2_MAX];
float   g_scene1_walker_phase2_pos_x    [SCENE1_WALKER_PHASE2_MAX];
float   g_scene1_walker_phase2_pos_z    [SCENE1_WALKER_PHASE2_MAX];

int32_t g_scene1_walker_phase2_count = 0;

/* ─── flag-byte hook ──────────────────────────────────────────────── */

static scene1_walker_phase2_flag_fn s_flag_hook = NULL;

void scene1_walker_phase2_set_flag_hook(scene1_walker_phase2_flag_fn fn)
{
    s_flag_hook = fn;
}

scene1_walker_phase2_flag_fn scene1_walker_phase2_get_flag_hook(void)
{
    return s_flag_hook;
}

/* ─── reset ───────────────────────────────────────────────────────── */

void scene1_walker_phase2_reset(void)
{
    memset(g_scene1_walker_phase2_mesh_type, 0,
           sizeof(g_scene1_walker_phase2_mesh_type));
    memset(g_scene1_walker_phase2_rot_y, 0,
           sizeof(g_scene1_walker_phase2_rot_y));
    memset(g_scene1_walker_phase2_pos_y, 0,
           sizeof(g_scene1_walker_phase2_pos_y));
    memset(g_scene1_walker_phase2_pos_x, 0,
           sizeof(g_scene1_walker_phase2_pos_x));
    memset(g_scene1_walker_phase2_pos_z, 0,
           sizeof(g_scene1_walker_phase2_pos_z));
    g_scene1_walker_phase2_count = 0;
    s_flag_hook = NULL;
}

/* ─── matrix builder ──────────────────────────────────────────────── */

/* Engine .rdata constants (verified via tools/analyze/pe.py). */
#define K_TWO         2.0f               /* 0x519314 = 0x40000000 */
#define K_ZERO        0.0f               /* 0x519320 */
#define K_HALF_PI     1.5707964f         /* 0x519434 = 0x3fc90fdb */
#define K_PI          3.1415927f         /* 0x51943c = 0x40490fdb */
#define K_FIVE        5.0f               /* 0x51953c */
#define K_POINT_TWO   0.2f               /* 0x5198d8 = 0x3e4ccccd */
#define K_NEG_PT_TWO (-0.2f)             /* 0x519a8c = 0xbe4ccccd */

/* Append `T(2,0,0) × RotY(π) × world` to world, in place.
 *
 * Engine pattern (asm 0x457ebd..0x457f0a — repeated at 0x457f1c..0x457f65
 * and 0x457f78..0x457fc7).  Each occurrence: MatrixTranslation(t2,2,0,0);
 * Multiply(world, t2, world); MatrixRotationY(ry, π); Multiply(world, ry, world).
 *
 * Result on row-vector (D3D row-major) world matrix:
 *     world' = RotY(π) × T(2,0,0) × world
 * (composed right-to-left in our row-vector convention, so applied
 *  left-to-right to a vertex: first T(2,0,0), then RotY(π), then
 *  whatever was already there). */
static void append_flip_chain(float world[16])
{
    float t2[16];
    float ry_pi[16];
    mat4_translation(t2, K_TWO, K_ZERO, K_ZERO);
    mat4_mul(world, t2, world);
    mat4_rotation_y(ry_pi, K_PI);
    mat4_mul(world, ry_pi, world);
}

int scene1_walker_phase2_compute(float *out_matrices)
{
    if (!out_matrices) return 0;

    int count = g_scene1_walker_phase2_count;
    if (count < 0) count = 0;
    if (count > SCENE1_WALKER_PHASE2_MAX) count = SCENE1_WALKER_PHASE2_MAX;
    if (count == 0) return 0;

    for (int i = 0; i < count; i++) {
        float *world = out_matrices + i * 16;

        /* Stage 1: T(pos_x, pos_y, pos_z) → world.
         * Asm 0x457e48..0x457e64 (push pz [esi+0xf0], py [esi+0xa0],
         * px [esi+0x50], out=edi; call MatrixTranslation). */
        mat4_translation(world,
                         g_scene1_walker_phase2_pos_x[i],
                         g_scene1_walker_phase2_pos_y[i],
                         g_scene1_walker_phase2_pos_z[i]);

        /* Stage 2-3: world = RotY(rot_y) × world.
         * Asm 0x457e69..0x457e8a (RotationY(local, [esi]); Multiply
         * (edi, local, edi)). */
        {
            float ry[16];
            mat4_rotation_y(ry, g_scene1_walker_phase2_rot_y[i]);
            mat4_mul(world, ry, world);
        }

        /* Optional flip chain (mesh_type==4 + flag gates).
         * Asm 0x457e8f..0x457fc7. */
        if (g_scene1_walker_phase2_mesh_type[i] == 4) {
            int32_t flag = s_flag_hook ? s_flag_hook(i) : 0;
            if (flag != 0 && (flag & (int32_t)0xffffffc0) == (int32_t)0x000514c0) {
                float rot_y = g_scene1_walker_phase2_rot_y[i];
                if (rot_y == K_ZERO) {
                    /* rot==0 branch (asm 0x457ebd..0x457f0a, then
                     * jmp 0x457fc5 — skips the > 5.0 check). */
                    append_flip_chain(world);
                } else if (rot_y == K_HALF_PI) {
                    /* rot==π/2 branch (asm 0x457f1c..0x457f65 +
                     * fall-through to > 5.0 check). */
                    append_flip_chain(world);
                    if (g_scene1_walker_phase2_pos_y[i] > K_FIVE) {
                        /* > 5.0 branch (asm 0x457f78..0x457fc7) —
                         * applied AGAIN after the π/2 branch's flip. */
                        append_flip_chain(world);
                    }
                } else {
                    /* Direct fall-through to > 5.0 check (rot != 0,
                     * rot != π/2). */
                    if (g_scene1_walker_phase2_pos_y[i] > K_FIVE) {
                        append_flip_chain(world);
                    }
                }
            }
        }

        /* Stage 4: world = S(-0.2, 0.2, 0.2) × world.
         * Asm 0x457fcc..0x457fff (MatrixScaling(local, -0.2, 0.2, 0.2);
         * Multiply(edi, local, edi)).  Final per-mesh transform. */
        {
            float s[16];
            mat4_scaling(s, K_NEG_PT_TWO, K_POINT_TWO, K_POINT_TWO);
            mat4_mul(world, s, world);
        }
    }

    return count;
}
