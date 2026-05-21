/*
 * prewindow.h — FUN_00451790 (WinMain step 2, "very early init").
 *
 * Sequence of writes / calls reproduced from
 * docs/decompiled/by-address/451790.c:
 *
 *   _DAT_0438b1c4 = 0;
 *   DAT_0438b8cc  = 0;
 *   _DAT_0438cd64 = 10.0f;      // camera.x
 *   DAT_0438b1c0  = 1;
 *   _DAT_0438cd68 = 61.0f;      // camera.y
 *   _DAT_0438cd6c = -203.0f;    // camera.z
 *   FUN_00404e44();             // 8544-entry object table init
 *   FUN_00452569();             // 100-particle randomization
 *   D3DXMatrixLookAtRH(view, eye=&DAT_06a47110, target=(0,0,0), up=(0,1,0));
 *   D3DXMatrixPerspectiveFovRH(proj, π/4, 4/3, 10, 2000);
 *   D3DXMatrixMultiply(view, view, proj);   // out aliases m1
 *   _DAT_0438bf84 = 0;
 *   _DAT_0438bf88 = 0;
 *
 * RNG state at entry must be 1 (the .data initial value of DAT_006023a0)
 * for the particle table to match what the engine produces at boot. The
 * engine's RNG-from-time reseed (FUN_005045eb → FUN_00471050) happens
 * *after* this — see rng_seed_from_now in rng.h.
 *
 * The eye position is DAT_06a47110, which is in the BSS portion of .data
 * (raw size 0xdbe00 doesn't cover that VA) → reads as (0,0,0) at boot.
 * That makes the LookAtRH call mathematically degenerate (zaxis tries to
 * normalise (0,0,0)) — we reproduce the call as-is; nothing reads the
 * resulting matrix until a real camera setup runs in-game.
 */

#ifndef OPENRECET_PREWINDOW_H
#define OPENRECET_PREWINDOW_H

#include <stdint.h>

#define PREWINDOW_OBJECT_COUNT       8544
#define PREWINDOW_PARTICLE_BLOCKS    10
#define PREWINDOW_PARTICLES_PER_BLOCK 10
#define PREWINDOW_PARTICLE_COUNT    (PREWINDOW_PARTICLE_BLOCKS * \
                                     PREWINDOW_PARTICLES_PER_BLOCK)

/*
 * Per-entry "object" struct. Inferred from FUN_00404e44's stride (32
 * bytes) + three touched offsets per entry (-4, 0, +8 of the iteration
 * pointer; the iteration pointer is offset +4 within the entry, so the
 * three writes land at struct offsets +0, +4, +12). The other 5 dwords
 * are BSS-zero and untouched at init.
 *
 * What these entries represent is still TBD — we've only seen the
 * initializer, not a consumer. Exposed as a flat array so future ports
 * can identify the consumer and rename the field at that point.
 */
struct prewindow_object {
    float field0;          /* set to 0.0 at init (also BSS-zero)       */
    float y;               /* set to 1.0 at init  — the meaningful one */
    float pad08;           /* BSS-zero (untouched by init)             */
    float field12;         /* set to 0.0 at init (also BSS-zero)       */
    float pad16_28[4];     /* BSS-zero (untouched by init)             */
};

struct prewindow_state {
    /* The six named engine globals set on entry. The bit pattern matches
     * the unpacked binary's literal writes in FUN_00451790. */
    int   flag_b1c4;       /* DAT_0438b1c4 → 0 */
    int   flag_b8cc;       /* DAT_0438b8cc → 0 */
    int   flag_b1c0;       /* DAT_0438b1c0 → 1 */
    float camera[3];       /* DAT_0438cd64..6c → (10, 61, -203) */
    int   flag_bf84;       /* DAT_0438bf84 → 0  (set at function tail) */
    int   flag_bf88;       /* DAT_0438bf88 → 0  (set at function tail) */

    /* 8544 object entries (FUN_00404e44 init). */
    struct prewindow_object objects[PREWINDOW_OBJECT_COUNT];

    /* 100 randomized particles (FUN_00452569 init).
     * pos.x/y at base DAT_06a475fc / +4; pos.z at +8.  Engine multiplies
     * the (rand-0.5)*scale result by 0.5 after storing, so the final
     * field values are half of the named scale (see prewindow.c).        */
    float particle_pos[PREWINDOW_PARTICLE_COUNT][3]; /* DAT_06a475fc grid */
    float particle_rot[PREWINDOW_PARTICLE_COUNT][3]; /* DAT_06a47130 grid */
    int   particle_alive[PREWINDOW_PARTICLE_COUNT];  /* DAT_06a49728 grid */

    /* Boot-time view + projection matrices.
     * Row-major (D3DXMATRIX-style) float[16] each. */
    float view[16];        /* DAT_06a470d0 — overwritten with view*proj   */
    float proj[16];        /* DAT_06a498f8                                */
};

extern struct prewindow_state g_prewindow;

/*
 * Reads `g_rng_seed` and advances it by 600 LCG steps (100 particles ×
 * 6 random calls each). Re-invocation overwrites all state. Tests that
 * want the engine's deterministic boot particles should `rng_seed(1)`
 * first to mirror the .data initial value.
 */
void prewindow_init(void);

#endif /* OPENRECET_PREWINDOW_H */
