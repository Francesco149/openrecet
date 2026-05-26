/*
 * scene1_records_c_tick.c — see scene1_records_c_tick.h for the chip
 * writeup.
 *
 * Port of FUN_0044284b @ 0x44284b (1083 B).  Engine layout (decomp
 * lines 40229-40393):
 *
 *   Phase 1: overflow eviction (DEAD CODE).  Counts type-{0,1,2,3}
 *   slots; if count > 6, scans for the OLDEST type-{0,1,2,3} slot with
 *   age > 0x4b0 (= 1200) and clears its TYPE.  Since type-{0,1,2,3}
 *   slots are kill-gated at age==0xf0 (= 240) at the end of phase 2,
 *   the age > 1200 condition is never satisfied → eviction never
 *   fires.  Ported faithfully; deviating could mask a future Frida-
 *   verified engine value change.
 *
 *   Phase 2: per-slot integrate.  Dispatches on slot[16] (state):
 *     state==1: just decrement age (death-staging slots).
 *     state==2: pickup-bob animation.  Sparkle spawn at age==10,
 *               pos.y lift over frames 0x14..0x4f, commit_pickup +
 *               kill at age==0x78.
 *     state==0: world-drop physics.  3-azimuth wall raycast,
 *               velocity drag (×0.97), gravity, player attraction
 *               (for type ∈ {0,1,2,3} after age > 0x3c), ground
 *               clamp + bounce/stop.
 *
 * Translation notes:
 *
 *   - The 3-azimuth loop in phase 2 (engine L40316-40333) uses
 *     `local_8` as an FPU-register loop counter with bit-reinterpret
 *     increments — Ghidra renders it as `local_8 = (float)((int)local_8 + 1)`
 *     and `local_8 != 4.2039e-45` (= float-bit pattern for int 3).
 *     Port flattens to a plain `for (int k = 0; k < 3; k++)`.
 *
 *   - The ground-query at L40354 has `(&DAT_0693250c)[uVar6 * 0x49]`
 *     in the Ghidra decomp — looks like table B addressing, but uVar6
 *     was never set in the visible body so this is almost certainly a
 *     Ghidra register-naming + address-pun artifact for the slot's own
 *     pos.x / pos.z.  Port uses slot pos.x / pos.z.  Logged as PHC #12
 *     for retail Frida confirmation.
 *
 *   - The raycast at L40327-40330 uses `local_74` and `local_6c` which
 *     are uninitialized in the visible body — Ghidra dropped output
 *     args from FUN_00433674.  Port models the raycast as writing
 *     (out_t, out_n_x, out_n_z); local_74 ≡ out_n_x, local_6c ≡ out_n_z.
 *     Logged as PHC #13.
 *
 *   - The pickup-commit call at L40304 has `FUN_00484dd1(piVar4[-1],
 *     piVar4[3])` (2 args) but engine FUN_00484dd1 signature is
 *     (type, extra1, extra2) per L85327 (the only other call site).
 *     Ghidra dropped the 3rd arg.  Port calls with all 3 fields
 *     derived from the slot — extra2 = slot[PICKUP_E2].  Logged as
 *     PHC #14.
 */

#include "scene1_records_c_tick.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "scene1_particles_tick.h"   /* g_scene1_player_pos */
#include "scene1_records.h"
#include "scene1_spawn.h"
#include "call_trace.h"

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

/* ---- Slot-field helpers ---------------------------------------------- */

static inline int32_t *c_slot(int i)
{
    return &g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE];
}

static inline float c_slot_f(int i, int off)
{
    int32_t v = g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + off];
    float f;
    __builtin_memcpy(&f, &v, sizeof f);
    return f;
}

static inline void c_slot_set_f(int i, int off, float f)
{
    int32_t v;
    __builtin_memcpy(&v, &f, sizeof v);
    g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + off] = v;
}

static inline int32_t c_slot_i(int i, int off)
{
    return g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + off];
}

static inline void c_slot_set_i(int i, int off, int32_t v)
{
    g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + off] = v;
}

static int is_world_drop_type(int t)
{
    return t == 0 || t == 1 || t == 2 || t == 3;
}

/* ---- Hooks ---------------------------------------------------------- */

static int default_ground_query(float x, float z, float *out_y)
{
    (void)x; (void)z; (void)out_y;
    return 0;   /* HOUSE-dormant: no ground → world-drop physics skipped */
}

static int default_raycast(float ox, float oy, float oz,
                           float dx, float dy, float dz,
                           float *out_t, float *out_n_x, float *out_n_z)
{
    (void)ox; (void)oy; (void)oz;
    (void)dx; (void)dy; (void)dz;
    (void)out_t; (void)out_n_x; (void)out_n_z;
    return 0;   /* No collision */
}

static void default_commit_pickup(int type, int extra1, int extra2)
{
    (void)type; (void)extra1; (void)extra2;
}

static scene1_c_ground_query_fn  g_ground_query  = default_ground_query;
static scene1_c_raycast_fn       g_raycast       = default_raycast;
static scene1_c_commit_pickup_fn g_commit_pickup = default_commit_pickup;

scene1_c_ground_query_fn scene1_records_c_set_ground_query(scene1_c_ground_query_fn fn)
{
    scene1_c_ground_query_fn prev = g_ground_query;
    g_ground_query = fn ? fn : default_ground_query;
    return prev;
}

scene1_c_raycast_fn scene1_records_c_set_raycast(scene1_c_raycast_fn fn)
{
    scene1_c_raycast_fn prev = g_raycast;
    g_raycast = fn ? fn : default_raycast;
    return prev;
}

scene1_c_commit_pickup_fn scene1_records_c_set_commit_pickup(scene1_c_commit_pickup_fn fn)
{
    scene1_c_commit_pickup_fn prev = g_commit_pickup;
    g_commit_pickup = fn ? fn : default_commit_pickup;
    return prev;
}

/* ---- Phase 1: overflow eviction ------------------------------------- */

static void phase1_evict(void)
{
    int count = 0;
    for (int i = 0; i < g_scene1_records_c_count; i++) {
        int t = c_slot_i(i, SCENE1_RECORDS_C_OFF_TYPE);
        if (is_world_drop_type(t)) count++;
    }
    if (count <= 6) return;

    int evict_idx = -1;
    /* Engine starts max_age at 0x4b0 = 1200 — kill threshold for
     * type-{0,1,2,3} slots is 0xf0 = 240, so this condition is never
     * met and the branch is dead code. */
    int max_age = 0x4b0;
    for (int i = 0; i < g_scene1_records_c_count; i++) {
        int t   = c_slot_i(i, SCENE1_RECORDS_C_OFF_TYPE);
        int age = c_slot_i(i, SCENE1_RECORDS_C_OFF_AGE);
        if (is_world_drop_type(t) && age > max_age) {
            evict_idx = i;
            max_age = age;
        }
    }
    if (evict_idx >= 0) {
        c_slot_set_i(evict_idx, SCENE1_RECORDS_C_OFF_TYPE, -1);
    }
}

/* ---- Phase 2 substates ---------------------------------------------- */

static void substate_pickup_bob(int i)
{
    /* state == 2: pickup-bob animation.  Lifts the drop on a fixed
     * frame range, sparkles at age==10, commits + kills at age==0x78. */
    int age  = c_slot_i(i, SCENE1_RECORDS_C_OFF_AGE);
    int type = c_slot_i(i, SCENE1_RECORDS_C_OFF_TYPE);

    if (age == 10) {
        scene1_spawn(0,
                     c_slot_f(i, SCENE1_RECORDS_C_OFF_POS_X),
                     c_slot_f(i, SCENE1_RECORDS_C_OFF_POS_Y),
                     c_slot_f(i, SCENE1_RECORDS_C_OFF_POS_Z),
                     0x2d, 0.15f, 1);
    }
    if (age > 0x14 && age < 0x50) {
        float py = c_slot_f(i, SCENE1_RECORDS_C_OFF_POS_Y);
        c_slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_Y, py + 0.05f);
    }
    if (age == 0x78) {
        g_commit_pickup(type,
                        c_slot_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E1),
                        c_slot_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2));
        c_slot_set_i(i, SCENE1_RECORDS_C_OFF_TYPE, -1);
    }
}

static void substate_world_drop(int i)
{
    /* state == 0: world-drop physics.
     *
     * Engine writes the integrate-by-velocity step using `scale` (slot[12],
     * initialized to 1.0f) as a per-slot dt.  Then damps velocity ×0.97.
     * Then runs a 3-azimuth wall raycast at the slot's pos+y0.6 offset.
     * Then (age < 1200 — basically always) applies gravity + (for world-
     * drop types, after age > 60) player attraction, capped at speed 0.5,
     * then ground-clamp + bounce/stop. */

    float scale = c_slot_f(i, SCENE1_RECORDS_C_OFF_SCALE);
    float px = c_slot_f(i, SCENE1_RECORDS_C_OFF_POS_X);
    float py = c_slot_f(i, SCENE1_RECORDS_C_OFF_POS_Y);
    float pz = c_slot_f(i, SCENE1_RECORDS_C_OFF_POS_Z);
    float vx = c_slot_f(i, SCENE1_RECORDS_C_OFF_VEL_X);
    float vy = c_slot_f(i, SCENE1_RECORDS_C_OFF_VEL_Y);
    float vz = c_slot_f(i, SCENE1_RECORDS_C_OFF_VEL_Z);

    px += scale * vx;
    py += scale * vy;
    pz += scale * vz;
    vx *= 0.97f;
    vy *= 0.97f;
    vz *= 0.97f;

    /* 3-azimuth horizontal wall raycast at pos+y0.6.  See translation
     * notes — the engine's `local_8` FPU-counter pattern unrolls to a
     * plain 3-iter int loop. */
    for (int k = 0; k < 3; k++) {
        float angle = (float)k * 6.2831855f / 3.0f;
        float dx = sinf(angle) * 1.5f;
        float dz = cosf(angle) * 1.5f;
        float out_t = 0.0f, out_n_x = 0.0f, out_n_z = 0.0f;
        if (g_raycast(px, py + 0.6f, pz, dx, 0.0f, dz,
                      &out_t, &out_n_x, &out_n_z)) {
            px -= (1.0f - out_t) * dx;
            pz -= (1.0f - out_t) * dz;
            vx = out_n_x * 0.1f;
            vz = out_n_z * 0.1f;
        }
    }

    int age  = c_slot_i(i, SCENE1_RECORDS_C_OFF_AGE);
    int type = c_slot_i(i, SCENE1_RECORDS_C_OFF_TYPE);

    if (age < 0x4b0) {
        vy -= 0.05f;   /* gravity */

        if (age > 0x3c && is_world_drop_type(type)) {
            /* Attract toward player (xz only). */
            float dx = g_scene1_player_pos[0] - px;
            float dz = g_scene1_player_pos[2] - pz;
            vx += dx * 0.2f;
            vz += dz * 0.2f;
            /* Cap horizontal speed at 0.5 (engine: speed² check then
             * sqrt + divide-and-halve). */
            float speed_sq = vx * vx + vz * vz;
            if (speed_sq > 0.0f) {
                float speed = sqrtf(speed_sq);
                if (speed > 1.0f) {
                    vx = (vx * 0.5f) / speed;
                    vz = (vz * 0.5f) / speed;
                }
            }
        }

        /* Ground clamp.  Hook writes out_y; engine then backs up the
         * ground height into slot[9] (pos.y backup) and tests pos.y
         * against ground. */
        float ground_y = 0.0f;
        if (g_ground_query(px, pz, &ground_y)) {
            c_slot_set_f(i, SCENE1_RECORDS_C_OFF_GROUND_Y, ground_y);
            c_slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_Y_BAK, ground_y);
            if (py < ground_y) {
                py = ground_y;
                if (vy < 0.0f) {
                    if (vy <= -0.3f) {
                        vy = vy * -0.7f;   /* bounce */
                    } else {
                        vy = 0.0f;
                        /* Non-world-drop types come to rest: zero
                         * horizontal vel + advance state to 1 (decrement-
                         * only, slated for kill). */
                        if (!is_world_drop_type(type)) {
                            vx = 0.0f;
                            vz = 0.0f;
                            c_slot_set_i(i, SCENE1_RECORDS_C_OFF_STATE, 1);
                        }
                    }
                }
            }
        }
    }

    c_slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_X, px);
    c_slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_Y, py);
    c_slot_set_f(i, SCENE1_RECORDS_C_OFF_POS_Z, pz);
    c_slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_X, vx);
    c_slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_Y, vy);
    c_slot_set_f(i, SCENE1_RECORDS_C_OFF_VEL_Z, vz);
}

/* ---- Phase 2: per-slot integrate ------------------------------------ */

static void phase2_integrate(void)
{
    for (int i = 0; i < g_scene1_records_c_count; i++) {
        int type = c_slot_i(i, SCENE1_RECORDS_C_OFF_TYPE);
        if (type == -1) continue;

        int aux   = c_slot_i(i, SCENE1_RECORDS_C_OFF_AUX);
        int state = c_slot_i(i, SCENE1_RECORDS_C_OFF_STATE);

        if (aux == 1) {
            int age = c_slot_i(i, SCENE1_RECORDS_C_OFF_AGE);
            c_slot_set_i(i, SCENE1_RECORDS_C_OFF_AGE, age - 1);
        } else if (state == 2) {
            substate_pickup_bob(i);
        } else if (state == 0) {
            substate_world_drop(i);
        }

        /* Trailing per-slot work.  Engine reads/updates `type` AFTER
         * the substate body — the pickup-bob substate may have killed
         * the slot (set type = -1) in which case the trailing age++ +
         * kill checks would re-test a dead slot.  We mirror that: re-
         * read type rather than reusing the cached value. */
        type = c_slot_i(i, SCENE1_RECORDS_C_OFF_TYPE);

        /* age++ regardless of substate (even the aux==1 path bumped by
         * -1 above — the engine first decrements then increments,
         * yielding a net zero per tick for aux==1.  Quirk preserved). */
        int age = c_slot_i(i, SCENE1_RECORDS_C_OFF_AGE);
        c_slot_set_i(i, SCENE1_RECORDS_C_OFF_AGE, age + 1);

        if (is_world_drop_type(type)) {
            int new_age = c_slot_i(i, SCENE1_RECORDS_C_OFF_AGE);
            if (new_age == 0xf0) {
                c_slot_set_i(i, SCENE1_RECORDS_C_OFF_TYPE, -1);
            }
            float py = c_slot_f(i, SCENE1_RECORDS_C_OFF_POS_Y);
            if (py < -1.0f) {
                c_slot_set_i(i, SCENE1_RECORDS_C_OFF_TYPE, -1);
            }
        }
    }
}

/* ---- Public entry --------------------------------------------------- */

void scene1_records_c_tick(void)
{
    /* E.2 probe — FUN_0044284b @ 0x44284b (table C tick). */
    CALL_TRACE_ENTER(0x44284bu);

    phase1_evict();
    phase2_integrate();
}
