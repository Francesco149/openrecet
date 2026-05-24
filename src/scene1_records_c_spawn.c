/*
 * scene1_records_c_spawn.c — see scene1_records_c_spawn.h for the chip
 * writeup.
 *
 * Ports:
 *   FUN_0044aef0 @ 0x44aef0 (96 B)  — scene1_records_c_spawn_pickup
 *   FUN_0044af50 @ 0x44af50 (419 B) — scene1_records_c_spawn_world_drop
 *   FUN_0044b0f3 @ 0x44b0f3 (60 B)  — _spawn_world_drop_default
 *   FUN_0044b12f @ 0x44b12f (61 B)  — _spawn_world_drop_typed
 *
 * Engine globals consumed:
 *   thunk_FUN_005041f6 → rng_next15()         (rng.h)
 *   FUN_00471089       → rng_next_unit()      (rng.h)
 *   FUN_00503a44 / 994 → sinf / cosf          (math.h)
 *
 * The four allocators write through the slot's true base (offset 0 =
 * pos.x) — the engine names DAT_06956cb0 as the slot base.  Our
 * g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE] anchors at that
 * same base.
 */

#include "scene1_records_c_spawn.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "rng.h"

#ifndef TWO_PI_F
#define TWO_PI_F 6.2831855f
#endif

/* ---- Slot accessors -------------------------------------------------- */

static inline void slot_set_i(int i, int off, int32_t v)
{
    g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + off] = v;
}

static inline int32_t slot_get_i(int i, int off)
{
    return g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + off];
}

static inline void slot_set_f(int i, int off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + off] = v;
}

static inline int slot_is_free(int i)
{
    return slot_get_i(i, SCENE1_RECORDS_C_OFF_TYPE) == -1;
}

/* ---- FUN_0044aef0: single-slot pickup spawn ------------------------- */

void scene1_records_c_spawn_pickup(int owner, float px, float py, float pz,
                                   int type)
{
    (void)owner;   /* engine param_1 unused — see header */

    for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
        if (!slot_is_free(i)) continue;

        slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_X, px);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_Y, py);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_Z, pz);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_X, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_Y, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_Z, 0.0f);
        slot_set_i(i, SCENE1_RECORDS_C_OFF_AGE, 0);
        slot_set_i(i, SCENE1_RECORDS_C_OFF_TYPE, type);   /* claim */
        slot_set_f(i, SCENE1_RECORDS_C_OFF_SCALE, 1.0f);
        slot_set_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2, 0);
        slot_set_i(i, SCENE1_RECORDS_C_OFF_STATE, 2);     /* pickup-bob */
        slot_set_i(i, SCENE1_RECORDS_C_OFF_EXTRA_AUX, 0);
        slot_set_i(i, SCENE1_RECORDS_C_OFF_AUX, 0);
        /* Engine quirk: slot[14] (PICKUP_E1) intentionally NOT
         * initialized — see header. */
        return;
    }
    /* Table full → silent noop, matching engine fallthrough. */
}

/* ---- FUN_0044af50: multi-slot world-drop spawn ---------------------- */

void scene1_records_c_spawn_world_drop(int owner, float px, float py,
                                       float pz, int type, int count,
                                       float mag, int e1, int extra_aux,
                                       int aux10, int type_override)
{
    if (count <= 0) return;

    /* Engine's `local_c` cap: 0x88 (136) for type<=6, else 200. */
    int scan_cap = (type > 6) ? SCENE1_RECORDS_C_COUNT : 0x88;
    if (scan_cap == 0) return;
    /* (engine has `if (local_c != 0)` — defensive; can't fire with
     * the constants above, but mirror the gate.) */

    int committed = 0;
    for (int i = 0; i < scan_cap; i++) {
        if (!slot_is_free(i)) continue;

        /* Position + zero velocity init (vel.x/y/z overwritten below).
         * Engine writes piVar4[-15..-13] = pos, [-12..-10] = vel zero,
         * then re-sets vel via the trig block.  We do the same in
         * order so any test that inspects mid-state sees engine layout. */
        slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_X, px);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_Y, py);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_Z, pz);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_X, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_Y, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_Z, 0.0f);

        /* age = rng_next15() & 7 — 0..7 stagger. */
        slot_set_i(i, SCENE1_RECORDS_C_OFF_AGE, (int)(rng_next15() & 7u));

        /* pickup_e1 = 0 (engine piVar4[-1]; overwritten in the
         * type>6 block below). */
        slot_set_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E1, 0);

        /* pickup_e2 = type_override or 0 (the initial write — gets
         * overwritten in the type>6 block when override<0 + 4-color
         * RNG ramp fires).  Engine piVar4[0]. */
        slot_set_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2,
                   (type_override < 0) ? 0 : type_override);

        slot_set_i(i, SCENE1_RECORDS_C_OFF_EXTRA_AUX, extra_aux);
        slot_set_i(i, SCENE1_RECORDS_C_OFF_AUX, aux10);

        if (type > 6) {
            slot_set_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E1, e1);

            if (type_override < 0) {
                slot_set_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2, 0);

                /* 4-color RNG ramp.  Window check: (type-7) ∉
                 * [0xc80, 0xce3] → ramp fires.  Inside the window
                 * pickup_e2 stays 0 (the write at the top of this
                 * block). */
                int window_lo = 0xc80;
                int window_hi = 0xce3;
                int wt = type - 7;
                if (wt < window_lo || wt > window_hi) {
                    unsigned uVar3 = rng_next15() % 100u;
                    int e2;
                    if (uVar3 < 0x32u)      e2 = 0;
                    else if (uVar3 < 0x46u) e2 = 1;
                    else if (uVar3 < 0x55u) e2 = 2;
                    else                    e2 = (uVar3 > 0x5eu) ? 4 : 3;
                    slot_set_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2, e2);
                }
            } else {
                slot_set_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2, type_override);
            }
        }

        /* Claim slot: type, scale, state. */
        slot_set_i(i, SCENE1_RECORDS_C_OFF_TYPE, type);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_SCALE, 1.0f);
        slot_set_i(i, SCENE1_RECORDS_C_OFF_STATE, 0);

        /* slot[18] = owner_ref.  Engine writes this BEFORE the per-type
         * branch (piVar4[3] = param_1 right at the top of the slot-init
         * block), but the value never gets overwritten elsewhere —
         * write order is irrelevant. */
        slot_set_i(i, SCENE1_RECORDS_C_OFF_OWNER, owner);

        /* Velocity: paired sin/cos at random angle + positive vy
         * bias.  Engine variable order:
         *   u1 = rng_next_unit();       fVar1 = (u1 + 0.2) * 0.5
         *   u2 = rng_next_unit();       angle = u2 * 2π
         *   vel.x = sin(angle) * fVar1 * mag
         *   u3 = rng_next_unit();       vel.y = (u3 + 0.2) * mag * 0.5
         *   vel.z = cos(angle) * fVar1 * mag    (reuses angle from sin) */
        float u1    = rng_next_unit();
        float fVar1 = (u1 + 0.2f) * 0.5f;
        float u2    = rng_next_unit();
        float angle = u2 * TWO_PI_F;
        float vx = sinf(angle) * fVar1 * mag;
        float u3 = rng_next_unit();
        float vy = (u3 + 0.2f) * mag * 0.5f;
        float vz = cosf(angle) * fVar1 * mag;
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_X, vx);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_Y, vy);
        slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_Z, vz);

        committed++;
        if (committed == count) return;
    }
    /* Table full / scan exhausted → silent return. */
}

/* ---- FUN_0044b0f3: 9-arg wrapper ------------------------------------ */

void scene1_records_c_spawn_world_drop_default(int owner, float px, float py,
                                               float pz, int type, int count,
                                               float mag, int e1, int extra_aux)
{
    scene1_records_c_spawn_world_drop(owner, px, py, pz, type, count, mag,
                                      e1, extra_aux, 0, -1);
}

/* ---- FUN_0044b12f: 10-arg wrapper ----------------------------------- */

void scene1_records_c_spawn_world_drop_typed(int owner, float px, float py,
                                             float pz, int type, int count,
                                             float mag, int e1, int extra_aux,
                                             int type_override)
{
    scene1_records_c_spawn_world_drop(owner, px, py, pz, type, count, mag,
                                      e1, extra_aux, 0, type_override);
}
