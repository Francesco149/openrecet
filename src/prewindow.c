#include "prewindow.h"

#include <string.h>

#include "math3d.h"
#include "rng.h"

struct prewindow_state g_prewindow;

void prewindow_init(void)
{
    /* ─── named globals, in the engine's write order ─────────────────────── */
    g_prewindow.flag_b1c4 = 0;
    g_prewindow.flag_b8cc = 0;
    g_prewindow.camera[0] = 10.0f;       /* _DAT_0438cd64 = 0x41200000 */
    g_prewindow.flag_b1c0 = 1;
    g_prewindow.camera[1] = 61.0f;       /* _DAT_0438cd68 = 0x42740000 */
    g_prewindow.camera[2] = -203.0f;     /* _DAT_0438cd6c = 0xc34b0000 */

    /* ─── FUN_00404e44: 8544-entry object table init ─────────────────────── */
    for (int i = 0; i < PREWINDOW_OBJECT_COUNT; i++) {
        g_prewindow.objects[i].field0  = 0.0f;
        g_prewindow.objects[i].y       = 1.0f;
        g_prewindow.objects[i].field12 = 0.0f;
        /* pad08 + pad16_28 remain zero (BSS) — engine doesn't touch them. */
    }

    /* ─── FUN_00452569: 100-particle randomization ───────────────────────── *
     *
     * Loop structure: 10 outer × 10 inner = 100 particles. Per particle the
     * engine consumes 6 rand_unit values + writes (alive=1) once. The first
     * three writes are scaled to ±10 / -shifted along z; the next three are
     * π/10-scaled angles. *Then* every six floats are halved in place, so
     * the final stored values are at ±5 / -5 / π/20 scale.
     *
     * Engine order (preserved here — RNG consumption order matters for the
     * deterministic boot output):
     *   pos.x = (rand - 0.5) * 20
     *   pos.y = (rand - 0.5) * 20
     *   pos.z = (rand + 2.5) * -10
     *   rot.x = (rand - 0.5) * 0.31415927
     *   rot.y = (rand - 0.5) * 0.31415927
     *   alive = 1                           (no RNG)
     *   rot.z = (rand - 0.5) * 0.31415927
     *   <halve all six floats in place>
     *
     * The "halve in place" is significant for bit-exact replay: it stores
     * the unhalved value, re-reads it as a 32-bit float, multiplies by 0.5,
     * then stores again — so the final value is `round32(round32(x)*0.5)`
     * rather than `round32(x*0.5)`. We replicate that two-step in 32-bit. */
    const float kPiOverTen = 0.31415927f;

    int p = 0;
    for (int outer = 0; outer < PREWINDOW_PARTICLE_BLOCKS; outer++) {
        for (int inner = 0; inner < PREWINDOW_PARTICLES_PER_BLOCK; inner++) {
            float r;
            r = rng_next_unit(); g_prewindow.particle_pos[p][0] = (r - 0.5f) *  20.0f;
            r = rng_next_unit(); g_prewindow.particle_pos[p][1] = (r - 0.5f) *  20.0f;
            r = rng_next_unit(); g_prewindow.particle_pos[p][2] = (r + 2.5f) * -10.0f;
            r = rng_next_unit(); g_prewindow.particle_rot[p][0] = (r - 0.5f) * kPiOverTen;
            r = rng_next_unit(); g_prewindow.particle_rot[p][1] = (r - 0.5f) * kPiOverTen;
            r = rng_next_unit();
            g_prewindow.particle_alive[p] = 1;
            g_prewindow.particle_rot[p][2] = (r - 0.5f) * kPiOverTen;

            g_prewindow.particle_rot[p][0] = g_prewindow.particle_rot[p][0] * 0.5f;
            g_prewindow.particle_rot[p][1] = g_prewindow.particle_rot[p][1] * 0.5f;
            g_prewindow.particle_rot[p][2] = g_prewindow.particle_rot[p][2] * 0.5f;
            g_prewindow.particle_pos[p][0] = g_prewindow.particle_pos[p][0] * 0.5f;
            g_prewindow.particle_pos[p][1] = g_prewindow.particle_pos[p][1] * 0.5f;
            g_prewindow.particle_pos[p][2] = g_prewindow.particle_pos[p][2] * 0.5f;

            p++;
        }
    }

    /* ─── lookat + perspective + matmul (degenerate boot inputs) ─────────── *
     *
     * Eye is DAT_06a47110 — BSS-zero at this point in WinMain (see the
     * header comment). The lookat with eye=target=(0,0,0) is degenerate;
     * we faithfully reproduce the call.  The resulting matrix is unused
     * until a later "real" camera setup runs in-game. */
    const float eye[3]    = { 0.0f, 0.0f, 0.0f };
    const float target[3] = { 0.0f, 0.0f, 0.0f };
    const float up[3]     = { 0.0f, 1.0f, 0.0f };

    mat4_lookat_rh(g_prewindow.view, eye, target, up);
    mat4_perspective_fov_rh(g_prewindow.proj,
                            0.7853981f,   /* π/4   — 0x3f490fdb */
                            4.0f / 3.0f,  /*       — 0x3faaaaab */
                            10.0f,        /*       — 0x41200000 */
                            2000.0f);     /*       — 0x44fa0000 */
    mat4_mul(g_prewindow.view, g_prewindow.view, g_prewindow.proj);

    /* ─── tail writes ────────────────────────────────────────────────────── */
    g_prewindow.flag_bf84 = 0;
    g_prewindow.flag_bf88 = 0;
}
