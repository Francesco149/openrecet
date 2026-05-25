/*
 * scene1_records_b_tick.c — see header for chip writeup.
 *
 * Engine FUN_0043ae20 @ 0x43ae20 outer-loop + per-type cascade.  Decomp
 * shape:
 *
 *   local_2c = 0;
 *   do {
 *     piVar14 = &DAT_069324b0 + local_2c * 0x49;
 *     if (*piVar14 == 0) goto LAB_0043fbbc;     // skip dead
 *     DAT_06a46f98 = 0;
 *     slot[POS_*] += slot[VEL_*];
 *     slot[AGE]   += 1;
 *     iVar15 = *piVar14;
 *     // ... 86-way per-type dispatch ... (sub-chip work)
 *  LAB_0043fbbc:
 *     local_2c++;
 *     if (local_2c == 0x200) return;
 *   } while (true);
 *
 * Per-type cluster ladder:
 *
 *   C8j-tick.1  skeleton + dispatch table
 *   C8j-tick.2  0x1e/0x2f/0x88/0x9a + 0x89/0x9e
 *   C8j-tick.3  mid-cascade (0x9c/0x34/0x69/0x74/0x79/0x68)
 *   C8j-tick.4  Body 1 (2/3/4/0x22/0x54/0x67/0x6d/0x6e/0x6f/0x70)
 *   C8j-tick.5..13  remaining clusters per the survey
 *   C8j-tick.14  type 0x58 / 0x66 shared anchor-rotor body
 *   C8j-tick.15a..j  trivial / shared / paired tail bodies
 *   C8j-tick.15k  type 0x75 ground-cull walker  ← THIS CHIP
 *
 * Slot dead sentinel: `slot[0] == 0`.
 */

#include "scene1_records_b_tick.h"

#include <math.h>
#include <string.h>

#include "math3d.h"
#include "rng.h"
#include "scene1_overlay.h"
#include "scene1_particles_tick.h"
#include "scene1_per_frame_open.h"
#include "scene1_records.h"
#include "scene1_records_b_spawn.h"
#include "scene1_spawn.h"
#include "sim.h"                       /* g_sim_frame_count (engine DAT_0438b8cc) */

int32_t g_scene1_records_b_tick_flag;        /* engine DAT_06a46f98 */
int32_t g_scene1_records_b_tick_anim_drive;  /* engine DAT_06a46f94 */

/* Engine DAT_005c2434/8/c — 256-entry per-NPC-motion-style table read by
 * C8j-tick.10 (Body 6 + Body 7) and presumably deeper bodies.  PHC #19;
 * default BSS-zero. */
scene1_b_motion_entry_t g_scene1_b_motion_table[256];

/* Engine DAT_438c218 / DAT_438c3a8 — wall-id lifetime / freshness banks.
 * See scene1_records_b_tick.h.  Sized 256; production exercises these
 * only when a wall populator sets up a record table. */
int32_t g_scene1_b_wall_lifetime[256];
int32_t g_scene1_b_wall_freshness[256];

/* ─── hooks ──────────────────────────────────────────────────────────── */

static void dispatch_default(int slot_idx, int32_t type);

static scene1_b_per_type_body_fn g_per_type_body     = dispatch_default;
static scene1_b_state_machine_fn g_state_machine_hook;   /* default NULL */
static scene1_b_se_fn            g_se_hook;              /* default NULL */
static scene1_b_cull_query_fn    g_cull_query_hook;      /* default NULL = "visible" */
static scene1_b_aux_1arg_fn      g_aux_485979_hook;      /* default NULL */
static scene1_b_aux_2arg_fn      g_aux_482a51_hook;      /* default NULL */
static scene1_b_notify_queue_fn  g_notify_queue_hook;    /* default NULL */
static scene1_b_tick_ground_query_fn  g_ground_query_hook;    /* default NULL = "no hit" */
static scene1_b_aux_4532bc_fn    g_aux_4532bc_hook;      /* default NULL */
static scene1_b_overlay_spawn_fn g_overlay_spawn_hook;   /* default NULL = call real spawn */
static scene1_b_aux_4319d6_fn    g_aux_4319d6_hook;      /* default NULL = returns 0 */
static scene1_b_sw_record_at_fn  g_sw_record_at_hook;    /* default NULL = no records */
static scene1_b_aux_43ab6e_fn    g_aux_43ab6e_hook;      /* default NULL = returns -1 */
static scene1_b_wall_raycast_fn  g_wall_raycast_hook;    /* default NULL = no hit */
static scene1_b_wall_flag_at_fn  g_wall_flag_at_hook;    /* default NULL = flag 0 */
static scene1_b_wall_destroy_fn  g_wall_destroy_hook;    /* default NULL */
static scene1_b_aux_44b255_fn    g_aux_44b255_hook;      /* default NULL */

scene1_b_per_type_body_fn scene1_records_b_set_per_type_body(
    scene1_b_per_type_body_fn fn)
{
    scene1_b_per_type_body_fn prev = g_per_type_body;
    g_per_type_body = fn ? fn : dispatch_default;
    return prev;
}

scene1_b_state_machine_fn scene1_records_b_set_state_machine_hook(
    scene1_b_state_machine_fn fn)
{
    scene1_b_state_machine_fn prev = g_state_machine_hook;
    g_state_machine_hook = fn;
    return prev;
}

scene1_b_se_fn scene1_records_b_set_se_hook(scene1_b_se_fn fn)
{
    scene1_b_se_fn prev = g_se_hook;
    g_se_hook = fn;
    return prev;
}

scene1_b_cull_query_fn scene1_records_b_set_cull_query_hook(
    scene1_b_cull_query_fn fn)
{
    scene1_b_cull_query_fn prev = g_cull_query_hook;
    g_cull_query_hook = fn;
    return prev;
}

scene1_b_aux_1arg_fn scene1_records_b_set_aux_485979_hook(
    scene1_b_aux_1arg_fn fn)
{
    scene1_b_aux_1arg_fn prev = g_aux_485979_hook;
    g_aux_485979_hook = fn;
    return prev;
}

scene1_b_aux_2arg_fn scene1_records_b_set_aux_482a51_hook(
    scene1_b_aux_2arg_fn fn)
{
    scene1_b_aux_2arg_fn prev = g_aux_482a51_hook;
    g_aux_482a51_hook = fn;
    return prev;
}

scene1_b_notify_queue_fn scene1_records_b_set_notify_queue_hook(
    scene1_b_notify_queue_fn fn)
{
    scene1_b_notify_queue_fn prev = g_notify_queue_hook;
    g_notify_queue_hook = fn;
    return prev;
}

scene1_b_tick_ground_query_fn scene1_records_b_set_ground_query_hook(
    scene1_b_tick_ground_query_fn fn)
{
    scene1_b_tick_ground_query_fn prev = g_ground_query_hook;
    g_ground_query_hook = fn;
    return prev;
}

scene1_b_aux_4532bc_fn scene1_records_b_set_aux_4532bc_hook(
    scene1_b_aux_4532bc_fn fn)
{
    scene1_b_aux_4532bc_fn prev = g_aux_4532bc_hook;
    g_aux_4532bc_hook = fn;
    return prev;
}

scene1_b_overlay_spawn_fn scene1_records_b_set_overlay_spawn_hook(
    scene1_b_overlay_spawn_fn fn)
{
    scene1_b_overlay_spawn_fn prev = g_overlay_spawn_hook;
    g_overlay_spawn_hook = fn;
    return prev;
}

scene1_b_aux_4319d6_fn scene1_records_b_set_aux_4319d6_hook(
    scene1_b_aux_4319d6_fn fn)
{
    scene1_b_aux_4319d6_fn prev = g_aux_4319d6_hook;
    g_aux_4319d6_hook = fn;
    return prev;
}

scene1_b_sw_record_at_fn scene1_records_b_set_sw_record_at_hook(
    scene1_b_sw_record_at_fn fn)
{
    scene1_b_sw_record_at_fn prev = g_sw_record_at_hook;
    g_sw_record_at_hook = fn;
    return prev;
}

scene1_b_aux_43ab6e_fn scene1_records_b_set_aux_43ab6e_hook(
    scene1_b_aux_43ab6e_fn fn)
{
    scene1_b_aux_43ab6e_fn prev = g_aux_43ab6e_hook;
    g_aux_43ab6e_hook = fn;
    return prev;
}

scene1_b_wall_raycast_fn scene1_records_b_set_wall_raycast_hook(
    scene1_b_wall_raycast_fn fn)
{
    scene1_b_wall_raycast_fn prev = g_wall_raycast_hook;
    g_wall_raycast_hook = fn;
    return prev;
}

scene1_b_wall_flag_at_fn scene1_records_b_set_wall_flag_at_hook(
    scene1_b_wall_flag_at_fn fn)
{
    scene1_b_wall_flag_at_fn prev = g_wall_flag_at_hook;
    g_wall_flag_at_hook = fn;
    return prev;
}

scene1_b_wall_destroy_fn scene1_records_b_set_wall_destroy_hook(
    scene1_b_wall_destroy_fn fn)
{
    scene1_b_wall_destroy_fn prev = g_wall_destroy_hook;
    g_wall_destroy_hook = fn;
    return prev;
}

scene1_b_aux_44b255_fn scene1_records_b_set_aux_44b255_hook(
    scene1_b_aux_44b255_fn fn)
{
    scene1_b_aux_44b255_fn prev = g_aux_44b255_hook;
    g_aux_44b255_hook = fn;
    return prev;
}

static inline int wall_raycast_call(float ox, float oy, float oz,
                                    float dx, float dy, float dz,
                                    scene1_b_wall_ray_result_t *out)
{
    out->t = 0.0f;
    out->wall_x = 0;
    out->wall_z = 0;
    out->wall_id = 0;
    if (g_wall_raycast_hook) {
        return g_wall_raycast_hook(ox, oy, oz, dx, dy, dz, out);
    }
    return 0;
}

static inline int wall_flag_at_call(int32_t wall_x, int32_t wall_z)
{
    return g_wall_flag_at_hook ? g_wall_flag_at_hook(wall_x, wall_z) : 0;
}

static inline void wall_destroy_call(int32_t wall_id)
{
    if (g_wall_destroy_hook) g_wall_destroy_hook(wall_id);
}

static inline void aux_44b255_call(void)
{
    if (g_aux_44b255_hook) g_aux_44b255_hook();
}

static inline int32_t aux_43ab6e_call(int32_t *slot,
                                      float a, float b, float c, float d,
                                      int32_t old_idx)
{
    if (g_aux_43ab6e_hook) {
        return g_aux_43ab6e_hook(slot, a, b, c, d, old_idx);
    }
    return -1;
}

static inline int32_t *sw_record_at(int idx)
{
    return g_sw_record_at_hook ? g_sw_record_at_hook(idx) : NULL;
}

static inline int aux_4319d6_call(void)
{
    return g_aux_4319d6_hook ? g_aux_4319d6_hook() : 0;
}

/* Wrapper used by all per-type bodies in this TU.  Routes to the
 * installed hook (test-only) or the real scene1_overlay_spawn
 * (production).  Same 10-arg shape as scene1_overlay_spawn. */
static inline void overlay_spawn(const void *template_owner,
                                 float pos_x, float pos_y, float pos_z,
                                 int   template_id,
                                 float scale_base,
                                 int   override_dur,
                                 int   override_rot_y,
                                 int   shape_mode,
                                 int   mode)
{
    if (g_overlay_spawn_hook) {
        g_overlay_spawn_hook(template_owner, pos_x, pos_y, pos_z,
                             template_id, scale_base, override_dur,
                             override_rot_y, shape_mode, mode);
    } else {
        scene1_overlay_spawn(template_owner, pos_x, pos_y, pos_z,
                             template_id, scale_base, override_dur,
                             override_rot_y, shape_mode, mode);
    }
}

static inline void se_play(uint16_t id)
{
    if (g_se_hook) g_se_hook(id);
}

static inline void aux_485979_call(int32_t arg1)
{
    if (g_aux_485979_hook) g_aux_485979_hook(arg1);
}

static inline void aux_482a51_call(int32_t arg1, int32_t arg2)
{
    if (g_aux_482a51_hook) g_aux_482a51_hook(arg1, arg2);
}

static inline void notify_queue_call(int32_t a, int32_t b, int32_t c, float d)
{
    if (g_notify_queue_hook) g_notify_queue_hook(a, b, c, d);
}

static inline int ground_query(float x, float y, float z, float *out_y)
{
    if (g_ground_query_hook) return g_ground_query_hook(x, y, z, out_y);
    *out_y = 0.0f;
    return 0;
}

static inline void aux_4532bc_call(int32_t arg1)
{
    if (g_aux_4532bc_hook) g_aux_4532bc_hook(arg1);
}

static inline void state_machine_call(int32_t *slot)
{
    if (g_state_machine_hook) g_state_machine_hook(slot);
}

/* C8j-tick.4 helper — returns 1 when a hook is installed (engine's "state
 * machine ran, continue iter loop") and 0 when no hook is installed
 * ("state machine reported no progress; break").  The void hook signature
 * doesn't expose engine's int return value, so this is the closest we
 * can model: HOOK installed → loop runs all 5 iters; NULL → loop runs 0
 * iters.  Tests of the type-4 anim-drive special case install a hook
 * that writes g_scene1_records_b_tick_anim_drive to exercise the branch. */
static inline int state_machine_call_ret(int32_t *slot)
{
    if (g_state_machine_hook) {
        g_state_machine_hook(slot);
        return 1;
    }
    return 0;
}

/* Default cull-query returns -1 ("visible") so the state machine fires
 * every loop iteration without an explicit hook installed. */
static inline int cull_query(float x, float y)
{
    return g_cull_query_hook ? g_cull_query_hook(x, y) : -1;
}

/* ─── slot accessors ─────────────────────────────────────────────────── */

static inline int32_t *slot_base(int i)
{
    return &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
}

static inline int32_t slot_get_i(int i, int off)
{
    return slot_base(i)[off];
}

static inline void slot_set_i(int i, int off, int32_t v)
{
    slot_base(i)[off] = v;
}

static inline float slot_get_f(int i, int off)
{
    int32_t v = slot_base(i)[off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static inline void slot_set_f(int i, int off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    slot_base(i)[off] = v;
}

/* Byte-pun helpers for owner-blob reads — engine treats OWNER_B as an
 * int and accesses `*(float *)(owner + byte_off)`.  Mirrors the helper
 * pattern in scene1_records_b_spawn.c. */
static inline float owner_read_f(const void *owner, int byte_off)
{
    int32_t v;
    memcpy(&v, (const char *)owner + byte_off, sizeof v);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static inline int32_t owner_read_i(const void *owner, int byte_off)
{
    int32_t v;
    memcpy(&v, (const char *)owner + byte_off, sizeof v);
    return v;
}

/* ─── public ─────────────────────────────────────────────────────────── */

void scene1_records_b_tick_kill_slot(int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= SCENE1_RECORDS_B_COUNT) return;
    slot_base(slot_idx)[SCENE1_RECORDS_B_OFF_TYPE] = 0;
}

void scene1_records_b_tick(void)
{
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        int32_t *slot = slot_base(i);
        int32_t  type = slot[SCENE1_RECORDS_B_OFF_TYPE];

        /* Engine L36454 — `if (*piVar14 == 0) goto LAB_0043fbbc;` */
        if (type == 0) continue;

        /* Engine L36455 — `DAT_06a46f98 = 0;` */
        g_scene1_records_b_tick_flag = 0;

        /* Engine L36456-62 — preamble pos += vel + age++. */
        float pos_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pos_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pos_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vel_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vel_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vel_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, pos_x + vel_x);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, pos_y + vel_y);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pos_z + vel_z);
        slot[SCENE1_RECORDS_B_OFF_AGE] += 1;

        g_per_type_body(i, type);
    }
}

/* ─── helpers for C8j-tick.2 per-type bodies ─────────────────────────── */

/* OWNER pointer convention for the C8j-tick.2 types: every body reads
 * the NPC owner via slot[OWNER_B] (engine field +0x14 = slot dw 5).
 * If the slot was committed by the entity allocator (OWNER_A != NULL,
 * OWNER_B == NULL) the owner ptr is NULL — engine would deref NULL +
 * 0x420 etc. and crash; in our port we early-return to keep behavior
 * sane.  Allocator paths in C8j ladder always populate OWNER_B for
 * NPC-class slot commits (and the types here are all NPC-class). */
static const void *slot_owner(int i)
{
    int32_t p = slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_B);
    /* Win32 user-mode pointers fit in the low 2 GB (or 3 GB with
     * LARGEADDRESSAWARE), so the cast-via-uint32 is lossless on 32-bit
     * native.  The uint32 step matters for 64-bit host tests where
     * sign-extending a small static-blob address (~0x55..) would land
     * in invalid kernel-VA territory; zero-extending preserves the
     * low-32-bit blob placement (see tests' mmap MAP_32BIT). */
    return (const void *)(uintptr_t)(uint32_t)p;
}

/* Sibling for the C8j-tick.3 entity-allocator types (0x68 / 0x74 / 0x79 /
 * 0x69), which read the entity owner via slot[OWNER_A].  Same zero-extend
 * rationale as slot_owner(). */
static void *slot_owner_a(int i)
{
    int32_t p = slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_A);
    return (void *)(uintptr_t)(uint32_t)p;
}

static inline void owner_write_f(void *owner, int byte_off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    memcpy((char *)owner + byte_off, &v, sizeof v);
}

static inline void owner_write_i(void *owner, int byte_off, int32_t v)
{
    memcpy((char *)owner + byte_off, &v, sizeof v);
}

/* Engine LAB_0043b205 tail (drag + iter loop + AGE-kill).  Receives the
 * just-computed cos(angle) which the predecessor leaves in ST(0); the
 * tail stores it into VEL_Z and continues.
 *
 * Per-type table (from raw asm 0x43b208..0x43b251):
 *
 *   type 0x1e (default): drag = -0.5, age_lo = 0x4b, age_hi = 0x5a,
 *                         iter_count = 30, age_kill = 0x69
 *   type 0x2f:           drag =  1.0, age_lo = 5,    age_hi = 0xb4,
 *                         iter_count = 30, age_kill = 0xcd
 *                         (also: state machine NOT called inside loop)
 *   type 0x88:           drag =  1.0, age_lo = 0x4b, age_hi = 0x87,
 *                         iter_count = 40, age_kill = 0x96
 *   type 0x9a:           drag =  0.5, age_lo = 0x4b, age_hi = 0x14f,
 *                         iter_count = 40, age_kill = 0x15e
 *
 * The iter loop's net pos effect is zero when state_machine is a no-op
 * (pos += vel * iter_count, then pos -= vel * iter_count); side effects
 * fire iter_count times. */
static void lab_b205_tail(int i, int32_t type, float vel_z)
{
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vel_z);

    float drag;
    int   age_lo, age_hi, iter_count, age_kill;
    int   call_state_machine_in_loop = 1;
    switch (type) {
    case 0x2f:
        drag = 1.0f;  age_lo = 5;    age_hi = 0xb4; age_kill = 0xcd;
        iter_count = 30;
        call_state_machine_in_loop = 0;  /* engine: `if (*piVar14 != 0x2f)` */
        break;
    case 0x88:
        drag = 1.0f;  age_lo = 0x4b; age_hi = 0x87; age_kill = 0x96;
        iter_count = 40;
        break;
    case 0x9a:
        drag = 0.5f;  age_lo = 0x4b; age_hi = 0x14f; age_kill = 0x15e;
        iter_count = 40;
        break;
    default: /* 0x1e */
        drag = -0.5f; age_lo = 0x4b; age_hi = 0x5a; age_kill = 0x69;
        iter_count = 30;
        break;
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);

    int part_idx = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    int age      = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (part_idx == 0 && age_lo <= age && age < age_hi) {
        for (int n = 0; n < iter_count; n++) {
            if (call_state_machine_in_loop) {
                state_machine_call(slot_base(i));
            }
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
            float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
            float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px + vx);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py + vy);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz + vz);
        }
        /* Anchor back using the FINAL vel (state machine may have
         * mutated it; in our no-op hook setup it has not). */
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - (float)iter_count * vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - (float)iter_count * vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - (float)iter_count * vz);
    }

    if (age == age_kill) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine LAB_0043aead body (types 0x2f / 0x88 / 0x9a — joint-table
 * anchored).  Reaches LAB_0043b205 tail via vel_z fall-through. */
static void body_anchor_joint(int i, int32_t type)
{
    const void *owner = slot_owner(i);
    if (!owner) return;

    /* Engine L36468-73 — initial pose = owner.pose + (0, 3, 0). */
    float opx = owner_read_f(owner, 0x3f0);
    float opy = owner_read_f(owner, 0x3f4);
    float opz = owner_read_f(owner, 0x3f8);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, opx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, opy + 3.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, opz);

    /* Engine L36474-82 — joint-table lookup overwrites pose.  Each
     * joint slot is 12 bytes (x, y, z floats) at owner+0x6fc onward.
     * `aux_b0 = slot[AUX_B0]` (dw 44) selects the slot. */
    int aux_b0    = slot_get_i(i, SCENE1_RECORDS_B_OFF_AUX_B0);
    int joint_off = 0x6fc + aux_b0 * 12;
    float jpx = owner_read_f(owner, joint_off + 0);
    float jpy = owner_read_f(owner, joint_off + 4) - 1.0f;
    float jpz = owner_read_f(owner, joint_off + 8);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, jpx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, jpy);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, jpz);

    /* Engine L36483 — gate on per-joint enable byte at +0xa88. */
    if (owner_read_i(owner, 0xa88 + aux_b0 * 4) != 0) return;

    /* Engine L36485-98 — angle bias by joint index. */
    float bias = (aux_b0 == 1) ? 1.5707964f : -1.5707964f;
    float ang  = bias + owner_read_f(owner, 0x420);

    float velx = sinf(ang) * 2.5f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, velx);
    int32_t vy_bits = (int32_t)0xbe99999a;  /* -0.3f */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy_bits);
    float velz = cosf(ang) * 2.5f;

    lab_b205_tail(i, type, velz);
}

/* Engine L36574-L36645 — type 0x1e else branch (gate + sub-dispatch on
 * owner+0x424 NPC motion-style ID).  Reaches LAB_0043b205 tail via
 * vel_z fall-through. */
static void body_0x1e(int i)
{
    const void *owner = slot_owner(i);
    if (!owner) return;

    /* Engine L36574 — gate. */
    if (owner_read_i(owner, 0x428) != 1) return;

    /* Engine L36577-81 — SE on AGE == 4 and AGE == 0x4b.  Raw asm:
     *   age == 4    → se_play(0x176)
     *   age == 0x4b → se_play(0x2a4) */
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 4)    se_play(0x176);
    if (age == 0x4b) se_play(0x2a4);

    /* Engine L36583-41 — sub-dispatch on owner+0x424 (motion-style ID). */
    int  motion = owner_read_i(owner, 0x424);
    float ang_base = owner_read_f(owner, 0x420);
    float ang;
    float velz;

    if (motion == 0x4b || motion == 0x4c) {
        /* Engine joint-slot-1 pose; angle ± 0.3 per sub-id. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 3.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x708));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x70c));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x710));
        ang = ang_base + ((motion == 0x4b) ? 0.3f : -0.3f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(ang));
        slot_set_i(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0);
        velz = cosf(ang);
    } else if (motion == 0x48) {
        /* Same joint-slot-1 pose; angle + 0.3. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 3.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x708));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x70c));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x710));
        ang = ang_base + 0.3f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(ang));
        slot_set_i(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0);
        velz = cosf(ang);
    } else {
        /* ALT_POS billboard variant: pos.x = sin(ang)*1.5 + ALT_POS_X;
         * pos.y = ALT_POS_Y; pos.z = cos(ang)*1.5 + ALT_POS_Z.  Vel
         * from raw sin/cos(angle) — no jitter offset. */
        ang = ang_base;
        float alt_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
        float alt_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
        float alt_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sinf(ang) * 1.5f + alt_x);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, alt_y);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cosf(ang) * 1.5f + alt_z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(ang));
        slot_set_i(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0);
        velz = cosf(ang);
    }

    lab_b205_tail(i, 0x1e, velz);
}

/* Engine LAB_0043b325 body (types 0x89 / 0x9e — compass-direction
 * billboard around owner pose). */
static void body_compass(int i, int32_t type)
{
    const void *owner = slot_owner(i);
    if (!owner) return;

    /* Engine L36651 — angle = (owner+0x18 int as float) * 2π / 8. */
    float ang = (float)owner_read_i(owner, 0x18) * 6.2831855f / 8.0f;

    /* Engine L36652-55 — radius = (type==0x9e) ? slot[LIFE_MULT]*5 : 2.0. */
    float radius = (type == 0x9e)
                       ? slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) * 5.0f
                       : 2.0f;

    /* Engine L36658-67 — pose. */
    float sa = sinf(ang);
    float ca = cosf(ang);
    float opx = owner_read_f(owner, 0x3f0);
    float opy = owner_read_f(owner, 0x3f4);
    float opz = owner_read_f(owner, 0x3f8);
    float lm  = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * radius + opx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, lm + lm + opy);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * radius + opz);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca);

    /* Engine L36675-700 — type 0x89 sparkle (age>10).  Two
     * scene1_overlay_spawn calls per tick.
     *
     * Raw asm (0x43b40e..0x43b556) confirms:
     *   - K1 = 0.02 (age * K1 → local_c, clamped to 1.0)
     *   - 2π scale on first random angle
     *   - sin/cos of random angle multiplied by 3.0 for spawn 1 xz
     *   - rng vertical scatter (rng-0.5)*6
     *   - spawn 1 template=0xf, scale=local_c*0.8
     *   - spawn 2 reuses the OUTER `ang` (not a fresh random) for xz
     *     offset of 0.5
     *   - spawn 2 template=0x41, scale=local_c*5.0 */
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (type == 0x89 && age > 10) {
        float local_c = (float)age * 0.02f;
        if (local_c > 1.0f) local_c = 1.0f;

        float rand_ang = rng_next_unit() * 6.2831855f;
        float r_sa = sinf(rand_ang);
        float r_ca = cosf(rand_ang);
        float spawn_y = (rng_next_unit() - 0.5f) * 6.0f
                        + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        overlay_spawn(NULL,
                             r_sa * 3.0f + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X),
                             spawn_y,
                             r_ca * 3.0f + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z),
                             0xf, local_c * 0.8f, -1, 0, 0, 0);

        /* Spawn 2 uses the OUTER (owner-compass) angle, not a fresh
         * random — asm reuses QWORD [ebp-0x10] from L36656. */
        overlay_spawn(NULL,
                             sa * 0.5f + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X),
                             slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y),
                             ca * 0.5f + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z),
                             0x41, local_c * 5.0f, -1, 0, 0, 0);
    }

    /* Engine L36702-04 — SE on AGE == 0x50. */
    if (age == 0x50) se_play(0x2c1);

    /* Engine L36705 — drag = 2.5. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 2.5f);

    /* Engine L36706-25 — iter loop (20 iters, age in [0x50, 0xa0)).
     * Ordering DIFFERS from LAB_0043b205: here `pos += vel` runs BEFORE
     * the state-machine call (raw asm 0x43b593..0x43b5b6).  Anchor
     * back by 20*vel after the loop. */
    if (age >= 0x50 && age < 0xa0) {
        for (int n = 0; n < 20; n++) {
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
            float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
            float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px + vx);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py + vy);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz + vz);
            state_machine_call(slot_base(i));
        }
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - 20.0f * vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - 20.0f * vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - 20.0f * vz);
    }

    /* Engine L36726-31 — kill if owner+0x428 != 1, kill if AGE == 0xaf. */
    if (owner_read_i(owner, 0x428) != 1) {
        scene1_records_b_tick_kill_slot(i);
    }
    if (age == 0xaf) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ─── C8j-tick.3 — mid-cascade bodies (L408-L649) ────────────────────── */

/* Engine L412-L477 / asm 0x43b798..0x43ba2f — NPC shoulder-arc bend.
 *
 * Per-tick math:
 *   ang   = slot[ROT_X]
 *   scale = slot[LIFE_MULT]
 *   local_c = clamp(10.0 - AGE*0.3, scale, ∞)             ; AGE < 0xbe
 *           = (AGE-0xbe)*0.6 + scale                      ; AGE >= 0xbe
 *   slot[ROT_SCR] = -π/2                                  ; AGE < 0x1a
 *                 = min((AGE-0x1a)*π/40 - π/2, 0.0)       ; AGE >= 0x1a
 *   pos.x = owner[0x3f0] - sin(ang) * scale * 1.5
 *   pos.y = local_c * scale + owner[0x3f4]
 *   pos.z = owner[0x3f8] - cos(ang) * scale * 1.5
 *
 * Side effects (gated):
 *   AGE in [0x21, 0xa3):
 *     scene1_overlay_spawn(owner_B, sin*scale*5, 2*scale, cos*scale*5,
 *                          0x6a, scale, -1, 0, 6, 0)
 *     scene1_overlay_spawn(owner_B, ... same offsets, 0x6e, ...)
 *     if AGE % 3 == 0:
 *       scene1_overlay_spawn(owner_B, ... same offsets, 0x6f, ...)
 *   AGE == 1:
 *     se_play(0x2c2)
 *     scene1_record_b_spawn_npc(owner_B, 0x9e, 1)
 *   AGE == 0xc8:
 *     kill slot
 *
 * Asm-verified shape_mode = 6 across all three spawns (0x43b95e/9a3/9ee
 * `push 0x6` — Ghidra dropped this arg in the decompile).
 */
static void body_0x9c(int i)
{
    const void *owner = slot_owner(i);
    if (!owner) return;

    int   age   = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    float scale = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);

    float local_c = 10.0f - (float)age * 0.3f;
    if (local_c < scale) local_c = scale;
    if (age >= 0xbe) local_c = (float)(age - 0xbe) * 0.6f + scale;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, -1.5707964f);
    if (age >= 0x1a) {
        float v = (float)(age - 0x1a) * 0.07853982f - 1.5707964f;
        if (v > 0.0f) v = 0.0f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, v);
    }

    float ang = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    float sa  = sinf(ang);
    float ca  = cosf(ang);
    float opx = owner_read_f(owner, 0x3f0);
    float opy = owner_read_f(owner, 0x3f4);
    float opz = owner_read_f(owner, 0x3f8);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, opx - sa * scale * 1.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, local_c * scale + opy);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, opz - ca * scale * 1.5f);

    if (age >= 0x21 && age < 0xa3) {
        float ox = sa * scale * 5.0f;
        float oy = 2.0f * scale;
        float oz = ca * scale * 5.0f;
        overlay_spawn(owner, ox, oy, oz, 0x6a, scale, -1, 0, 6, 0);
        overlay_spawn(owner, ox, oy, oz, 0x6e, scale, -1, 0, 6, 0);
        if (age % 3 == 0) {
            overlay_spawn(owner, ox, oy, oz, 0x6f, scale,
                                 -1, 0, 6, 0);
        }
    }

    if (age == 1) {
        se_play(0x2c2);
        scene1_record_b_spawn_npc(owner, 0x9e, 1);
    }

    if (age == 0xc8) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine L479-L518 / asm verified — NPC joint-target lerp.
 *
 *   joint_idx = slot[AUX_SENT1]                ; preamble default -1
 *   pos.x = owner[ (joint_idx + 0x96) * 12 ]   ; with default: 0x6fc
 *   pos.y = owner[ (joint_idx + 0x97) * 12 ]   ; with default: 0x708
 *   pos.z = owner[ (joint_idx + 0x97) * 12 + 4 ] ; with default: 0x70c
 *   vel   = (slot[ALT_POS] - pos) / 15         ; lerp toward ALT_POS
 *   slot[DRAG] = -0.5
 *
 *   if (slot[PART_IDX] == 0 && 0x5a <= AGE < 0x78):
 *     for n in [0..20):
 *       state_machine(slot)
 *       pos += vel
 *     pos -= 20 * vel  ; net-zero anchor-back
 *
 *   if AGE == 0x96: kill
 *
 * NB: slot[AUX_SENT1] is the field set to -1 in the C8j allocator
 * preamble (engine AUX_SENT1 = "particle slot sentinel" in other
 * contexts).  0x34 reuses this field as a joint index.  With the
 * preamble default the joint pose reads bytes 0x6fc/0x708/0x70c —
 * structurally the engine's joint-slot-0 x followed by joint-slot-1 x/y,
 * which is unusual but matches the decompile verbatim.
 */
static void body_0x34(int i)
{
    const void *owner = slot_owner(i);
    if (!owner) return;

    int joint_idx = slot_get_i(i, SCENE1_RECORDS_B_OFF_AUX_SENT1);

    float px = owner_read_f(owner, (joint_idx + 0x96) * 12);
    float py = owner_read_f(owner, (joint_idx + 0x97) * 12);
    float pz = owner_read_f(owner, (joint_idx + 0x97) * 12 + 4);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz);

    float ax = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
    float ay = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
    float az = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, (ax - px) / 15.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, (ay - py) / 15.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, (az - pz) / 15.0f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, -0.5f);

    int part_idx = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    int age      = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (part_idx == 0 && age >= 0x5a && age < 0x78) {
        for (int n = 0; n < 20; n++) {
            state_machine_call(slot_base(i));
            float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
            float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
            float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx + vx);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy + vy);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz + vz);
        }
        float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx - 20.0f * vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy - 20.0f * vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz - 20.0f * vz);
    }

    if (age == 0x96) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine L525-L568 / asm 0x43bba1..0x43bcea — three-phase NPC spawn cycle.
 *
 *   slot[DRAG]  = 1.0
 *   age_off     = 0
 *   if (slot[FLAG_A] == 1):
 *     slot[DRAG]  = 0.2
 *     age_off     = 0x14
 *
 *   if AGE == age_off + 10:   se_play(0x2a4)
 *   if AGE == 1:              spawn type 5, scale 1.0, dur 100
 *   if AGE == age_off + 0x28: se_play(0x2a9) + spawn type 0xe, scale 0.2, dur -1
 *   if AGE <  age_off + 0x14: pos -= vel (anchor back)
 *   if AGE == age_off + 0x1e: spawn type 0, scale 0.8, dur -1
 *   if age_off <= AGE < age_off + 0x3c: state_machine(slot)
 *   if AGE == age_off + 0x4b: kill
 *
 * All overlay spawns use OWNER_A as template_owner, position = slot[ALT_POS],
 * shape_mode = 0, mode = 0 (FUN_004147d5 wrapper appends mode=0).
 */
static void body_0x68(int i)
{
    void *owner_a = slot_owner_a(i);
    if (!owner_a) return;

    int age_off = 0;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.0f);
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_FLAG_A) == 1) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.2f);
        age_off = 0x14;
    }

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age == age_off + 10) se_play(0x2a4);

    if (age == 1) {
        float x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
        float y = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
        float z = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
        overlay_spawn(owner_a, x, y, z, 5, 1.0f, 100, 0, 0, 0);
    }

    if (age == age_off + 0x28) {
        se_play(0x2a9);
        float x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
        float y = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
        float z = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
        overlay_spawn(owner_a, x, y, z, 0xe, 0.2f, -1, 0, 0, 0);
    }

    if (age < age_off + 0x14) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - vz);
    }

    if (age == age_off + 0x1e) {
        float x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
        float y = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
        float z = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
        overlay_spawn(owner_a, x, y, z, 0, 0.8f, -1, 0, 0, 0);
    }

    if (age >= age_off && age < age_off + 0x3c) {
        state_machine_call(slot_base(i));
    }

    if (age == age_off + 0x4b) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine L519-L644 (shared body for 0x74/0x79) / asm 0x43bcef..0x43bf81.
 *
 *   slot[DRAG] = (type == 0x74) ? 0.0 : 0.7  ; 0x79 = 0.7
 *   pos -= vel                                ; cancel preamble
 *
 *   if AGE == 1:
 *     spawn type 0x11, scale 1.0, dur -1, override_rot_y = slot[ROT_X bits],
 *       shape_mode = 0
 *     if type == 0x74:
 *       spawn type 0x12, scale 1.0, dur -1, override_rot_y = 0
 *
 *   slot[AUX_C8] = 1                          ; engine `mov [esi+0xc4], edi=1`
 *
 *   if AGE % 4 == 0:
 *     owner_a+0x904 = vel.x * 0.2
 *     owner_a+0x908 = vel.y * 0.2
 *     owner_a+0x90c = vel.z * 0.2
 *     ang = (float)owner_a[+0x948 int] * 2π / 8.0
 *     u1, u2, u3 = three rng_next_unit draws
 *     local_28 (x) = (u1 - 0.5) * 3.0 + pos.x - sin(ang) * 5.0
 *     local_24 (y) = pos.y + 2 * (u2 - 0.5)        ; ENGINE QUIRK: one draw, used twice
 *     local_8  (z) = (u3 - 0.5) * 3.0 + pos.z - cos(ang) * 5.0
 *     scene1_overlay_spawn(owner_a, local_28, local_24, local_8, 0x2c, 1.8,
 *                          -1, 0, 0, 0)
 *     owner_a+0x904/0x908/0x90c = 0.0           ; clear anim drive
 *
 *   if 0 <= AGE < 0x28:
 *     for n in [0..20):
 *       if cull_query(pos.x, pos.y) < 0:
 *         state_machine(slot)
 *       pos += vel
 *     pos -= 20 * vel                          ; net-zero anchor-back
 *
 *   if owner_a+0xcf8 != 0: kill
 *   if AGE == 0x37:        kill
 */
static void body_0x74_or_0x79(int i, int32_t type)
{
    void *owner_a = slot_owner_a(i);
    if (!owner_a) return;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, (type == 0x74) ? 0.0f : 0.7f);

    /* Anchor back: pos -= vel (cancel preamble's += vel). */
    {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - vz);
    }

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age == 1) {
        float px      = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py      = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz      = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        int   rot_x_bits = slot_get_i(i, SCENE1_RECORDS_B_OFF_ROT_X);
        /* Asm 0x43bd30 loads slot[+0x90] as the override_rot_y arg; field
         * is slot[ROT_X] (dw 36 / byte 0x90).  Engine treats float bits
         * as int per scene1_overlay_spawn's override_rot_y contract. */
        overlay_spawn(owner_a, px, py, pz, 0x11, 1.0f, -1,
                             rot_x_bits, 0, 0);
        if (type == 0x74) {
            overlay_spawn(owner_a, px, py, pz, 0x12, 1.0f, -1,
                                 0, 0, 0);
        }
    }

    /* slot[AUX_C8] = 1 unconditionally — asm `mov [esi+0xc4], edi=1`
     * runs after the AGE==1 spawn branch, BEFORE the kill check. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);

    if (age % 4 == 0) {
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        owner_write_f(owner_a, 0x904, vx * 0.2f);
        owner_write_f(owner_a, 0x908, vy * 0.2f);
        owner_write_f(owner_a, 0x90c, vz * 0.2f);

        float ang = (float)owner_read_i(owner_a, 0x948) * 6.2831855f / 8.0f;
        float sa  = sinf(ang);

        float u1 = rng_next_unit();
        float spawn_x = (u1 - 0.5f) * 3.0f
                        + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X)
                        - sa * 5.0f;

        /* Engine quirk: one rng draw, used twice in `(u - 0.5) + (u - 0.5)`
         * (= `fadd st(0), st`).  Equivalent to 2*(u-0.5). */
        float u2 = rng_next_unit();
        float spawn_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y)
                        + 2.0f * (u2 - 0.5f);

        float ca = cosf(ang);
        float u3 = rng_next_unit();
        float spawn_z = (u3 - 0.5f) * 3.0f
                        + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z)
                        - ca * 5.0f;

        overlay_spawn(owner_a, spawn_x, spawn_y, spawn_z, 0x2c,
                             1.8f, -1, 0, 0, 0);

        owner_write_f(owner_a, 0x904, 0.0f);
        owner_write_f(owner_a, 0x908, 0.0f);
        owner_write_f(owner_a, 0x90c, 0.0f);
    }

    if (age >= 0 && age < 0x28) {
        for (int n = 0; n < 20; n++) {
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            if (cull_query(px, py) < 0) {
                state_machine_call(slot_base(i));
            }
            float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
            float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
            float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx + vx);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy + vy);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz + vz);
        }
        float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx - 20.0f * vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy - 20.0f * vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz - 20.0f * vz);
    }

    if (owner_read_i(owner_a, 0xcf8) != 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }
    if (age == 0x37) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine L646-L650 / asm 0x43bb40..0x43bb81 — entity self-spawn-then-die.
 *
 *   if AGE == slot[PART_IDX] * 4 + 0x14:
 *     owner_a+0xea0 = slot[PART_IDX]
 *     scene1_record_b_spawn_entity(owner_a, 0x68, -1)
 *     kill self
 *
 * Asm-verified flag = -1 (push 0xffffffff at 0x43bb6e) — Ghidra showed
 * the engine FUN_0044376a as a 1-arg call; it's actually 3 args, the
 * survey's "FUN_0044376a is the entity allocator" matches our C8j ladder
 * port of `scene1_record_b_spawn_entity(owner, type, flag)`.
 */
static void body_0x69(int i)
{
    void *owner_a = slot_owner_a(i);
    if (!owner_a) return;

    int age      = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    int part_idx = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);

    if (age == part_idx * 4 + 0x14) {
        owner_write_i(owner_a, 0xea0, part_idx);
        scene1_record_b_spawn_entity(owner_a, 0x68, -1);
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ─── C8j-tick.4 — Body 1 (L689-L812) ────────────────────────────────── */

/* Per-type DRAG (engine FUN_0043ae20 L36497..L36514 / asm 0x43c117..0x43c165).
 * Values confirmed via tools/analyze/pe.py read of .rdata constants
 * (0x519314 / 0x519c24 / 0x519a20 / 0x5198d0 / 0x5198e0). */
static float body1_drag_for(int32_t type)
{
    switch (type) {
    case 2:
    case 0x54: return 2.0f;
    case 0x67: return 5.5f;
    case 0x22: return 3.5f;
    case 0x6d:
    case 0x6e:
    case 0x6f:
    case 0x70: return 2.5f;
    case 3:
    case 4:
    default:   return 1.5f;
    }
}

/* Engine L36500-L36605 / asm 0x43c117..0x43c491 — Body 1 (kill-on-ground +
 * bounce-particle pose helper).  Handles types {2, 3, 4, 0x22, 0x54, 0x67,
 * 0x6d, 0x6e, 0x6f, 0x70}.
 *
 *   slot[DRAG]   = per-type-table-driven value (see body1_drag_for)
 *
 *   if slot[FLAG_B] < 0:
 *     per_scale = (float)(int)slot[PART_IDX] * -0.4
 *     sa = sinf(ROT_X)
 *     ca = cosf(ROT_X)
 *     pos.{x,y,z} = owner_a[+0x20..+0x28] + (sa, 1.0, ca)
 *     ALT_POS.{x,y,z} = owner_a[+0x20..+0x28] + (per_scale*sa, 1.0, per_scale*ca)
 *     if type in {0x6d, 0x6e, 0x6f, 0x70}: pos.y += 1.0
 *   else (FLAG_B >= 0):
 *     joint_base = owner_a + slot[FLAG_B]*0x44 + 0x9e0
 *     pos.{x,y,z} = joint_base.{x,y,z} + (sin(ROT_X), 1.0, cos(ROT_X))
 *     ALT_POS = direct-copy joint_base.{x,y,z} + (0, 1.0, 0)
 *       (engine calls sinf/cosf for ALT_POS writes but DISCARDS the
 *        result via `fstp st(0)` at 0x43c214/0x43c256 — pure-FPU-state
 *        noop; we elide the calls since float math is observable-equiv)
 *
 *   if type == 0x67:
 *     ang_h = (float)(int)slot[AGE] * 0.5
 *     scene1_overlay_spawn(owner_a,
 *       sin(ang_h)*4 + pos.x, pos.y, cos(ang_h)*4 + pos.z,
 *       0xd, 1.0f, -1, 0, 0, 0)
 *
 *   if slot[PART_IDX] == 0 && 5 < slot[AGE] < 10:
 *     for n in [0..5):
 *       g_scene1_records_b_tick_anim_drive = 0
 *       ret = state_machine(slot)             ; 0/1 return contract
 *       if ret == 0: break
 *       if ret == 1 && type == 4 && anim_drive > 0:
 *         anim_drive /= 10            ; floor 1
 *         owner_a+0xe30 = anim_drive
 *         owner_a+0xe38 = 0x1e
 *
 *   kill if owner_a+0xcf8 != 0
 *   kill if AGE == 0x14
 */
static void body_kill_bounce(int i, int32_t type)
{
    void *owner_a = slot_owner_a(i);
    if (!owner_a) return;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, body1_drag_for(type));

    int32_t flag_b = slot_get_i(i, SCENE1_RECORDS_B_OFF_FLAG_B);
    float   rotx   = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    float   sa     = sinf(rotx);
    float   ca     = cosf(rotx);

    if (flag_b < 0) {
        /* Simple branch: pose anchored at owner+0x20. */
        int   part_idx  = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
        float per_scale = (float)part_idx * -0.4f;

        float ox = owner_read_f(owner_a, 0x20);
        float oy = owner_read_f(owner_a, 0x24);
        float oz = owner_read_f(owner_a, 0x28);

        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa + ox);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca + oz);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, per_scale * sa + ox);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, oy + 1.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, per_scale * ca + oz);

        if (type == 0x6d || type == 0x6e || type == 0x6f || type == 0x70) {
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py + 1.0f);
        }
    } else {
        /* Joint branch: pose at owner + FLAG_B*0x44 + 0x9e0. */
        int joint_base = flag_b * 0x44 + 0x9e0;
        float jx = owner_read_f(owner_a, joint_base + 0);
        float jy = owner_read_f(owner_a, joint_base + 4);
        float jz = owner_read_f(owner_a, joint_base + 8);

        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa + jx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, jy + 1.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca + jz);
        /* ALT_POS direct-copy from joint base (+1 on y).  Engine calls
         * sinf/cosf and discards — preserved as a comment, no actual
         * RNG-affecting side effect. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, jx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, jy + 1.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, jz);
    }

    /* Type 0x67 spawn — circling-around-pos overlay every tick. */
    if (type == 0x67) {
        int   age      = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
        float ang_h    = (float)age * 0.5f;
        float sa_spawn = sinf(ang_h);
        float ca_spawn = cosf(ang_h);
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        overlay_spawn(owner_a,
                             sa_spawn * 4.0f + px,
                             py,
                             ca_spawn * 4.0f + pz,
                             0xd, 1.0f, -1, 0, 0, 0);
    }

    /* PART_IDX==0 + AGE in [6, 10) → up to 5-iter state_machine loop with
     * the type-4 anim-drive special case (engine asm 0x43c406..0x43c473). */
    int part_idx = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    int age      = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (part_idx == 0 && age > 5 && age < 10) {
        for (int n = 0; n < 5; n++) {
            g_scene1_records_b_tick_anim_drive = 0;
            int ret = state_machine_call_ret(slot_base(i));
            if (ret == 0) break;
            if (ret == 1 && type == 4 && g_scene1_records_b_tick_anim_drive > 0) {
                int v = g_scene1_records_b_tick_anim_drive / 10;
                if (v < 1) v = 1;
                g_scene1_records_b_tick_anim_drive = v;
                owner_write_i(owner_a, 0xe30, v);
                owner_write_i(owner_a, 0xe38, 0x1e);
            }
        }
    }

    if (owner_read_i(owner_a, 0xcf8) != 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }
    if (age == 0x14) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ─── C8j-tick.5 — Body 2 (L812-L1050) ───────────────────────────────── */

/* Shared 5-iter early-break state-machine loop used by 0x5b-group AND
 * 0x71/0x72/0x7d bodies.  Engine pattern (asm 0x43c711-0x43c722 and
 * 0x43caed-0x43cafe): zero `edi`, then loop calling FUN_0043865e; break
 * when it returns 0; cap at 5 iterations.  With our NULL state-machine
 * hook the loop runs 0 iters (state_machine_call_ret returns 0).  When
 * a hook is installed, the loop runs all 5 iters. */
static void body2_state_machine_5iter_loop(int slot_idx)
{
    for (int n = 0; n < 5; n++) {
        int ret = state_machine_call_ret(slot_base(slot_idx));
        if (ret == 0) break;
    }
}

/* Engine L36631-L36670 / asm 0x43c500..0x43c5ab — type 0x85 body.
 *
 *   slot[DRAG] = 0.5
 *   sa = sinf(ROT_X); ca = cosf(ROT_X)
 *   pos.x   = sa + owner_a[+0x20]
 *   pos.y   = owner_a[+0x24] + 1.0
 *   pos.z   = ca + owner_a[+0x28]
 *   ALT_POS = (owner_a[+0x20], owner_a[+0x24]+1.0, owner_a[+0x28])
 *     (engine still calls sinf/cosf and discards — fstp st(0) — pure
 *     FPU-state noop, elided)
 *
 *   ret = state_machine(slot)
 *   if (ret != 0):
 *     owner_a[+0xe90] = 7
 *     owner_a[+0xe94] = 0
 *     kill slot          ; engine: jmp 0x4411e3 (LAB_004411e3)
 *   else:
 *     if (owner_a[+0xcf8] != 0): kill slot
 *     if (AGE == 0x24):          kill slot
 */
static void body_0x85(int i)
{
    void *owner_a = slot_owner_a(i);
    if (!owner_a) return;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.5f);

    float rotx = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    float sa   = sinf(rotx);
    float ca   = cosf(rotx);
    float ox   = owner_read_f(owner_a, 0x20);
    float oy   = owner_read_f(owner_a, 0x24);
    float oz   = owner_read_f(owner_a, 0x28);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca + oz);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, oz);

    int ret = state_machine_call_ret(slot_base(i));
    if (ret != 0) {
        owner_write_i(owner_a, 0xe90, 7);
        owner_write_i(owner_a, 0xe94, 0);
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    if (owner_read_i(owner_a, 0xcf8) != 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 0x24) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine L36739-L36805 / asm 0x43c73d..0x43c8fe — types 0x8a / 0x8b
 * (shared body).
 *
 *   slot[DRAG] = 0.5
 *   sa = sinf(ROT_X); ca = cosf(ROT_X)
 *   pos.x = sa * 0.5 + owner_a[+0x20]
 *   pos.y = owner_a[+0x24] + 1.0
 *   pos.z = ca * 0.5 + owner_a[+0x28]
 *
 *   ret = state_machine(slot)
 *   if (ret != 0):
 *     owner_a[+0xe7c] = 0
 *     owner_a[+0xe80] = 0
 *     owner_a[+0xe84] = 0
 *     vel_scale = 1.4         ; 0x8a default
 *     if type == 0x8a:
 *       aux_485979(0)
 *       SE(0x13f)
 *       owner_a[+0xcf8] = 0x2d
 *       owner_a[+0xe90] = 1
 *       aux_482a51(owner_a+0x930, 2)
 *       notify_queue(8, 4, 4, 0.5)
 *     else:                    ; 0x8b
 *       owner_a[+0xcf8] = 0xf
 *       owner_a[+0xe90] = 1
 *       vel_scale = 0.8
 *     Common tail (executes for BOTH 0x8a and 0x8b after the cascade):
 *       sa2 = sinf(ROT_X) * vel_scale
 *       ca2 = cosf(ROT_X) * vel_scale
 *       *piVar14 = 0           ; immediate kill (TYPE=0)
 *       owner_a[+0x904] = sa2 * -0.1
 *       owner_a[+0x908] = 0.3
 *       owner_a[+0x90c] = ca2 * -0.1
 *
 *   Kill checks (executed unconditionally after the branch):
 *     if (owner_a[+0xe90] != 0): kill slot
 *     if (owner_a[+0xe7c] == 0): kill slot
 *     if (owner_a[+0xcf8] != 0): kill slot
 *     if (AGE == 20000):         kill slot
 *
 * Note: the immediate `*piVar14 = 0` write inside the state-machine
 * branch is what makes the slot effectively kill-on-state-machine; the
 * three "if owner+X != 0 → kill" checks at the tail are no-ops in that
 * case (slot already TYPE=0).  When state_machine returned 0, only the
 * unconditional tail checks fire.
 */
static void body_0x8a_or_0x8b(int i, int32_t type)
{
    void *owner_a = slot_owner_a(i);
    if (!owner_a) return;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.5f);

    float rotx = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    float sa   = sinf(rotx);
    float ca   = cosf(rotx);
    float ox   = owner_read_f(owner_a, 0x20);
    float oy   = owner_read_f(owner_a, 0x24);
    float oz   = owner_read_f(owner_a, 0x28);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * 0.5f + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * 0.5f + oz);

    int ret = state_machine_call_ret(slot_base(i));
    if (ret != 0) {
        owner_write_i(owner_a, 0xe7c, 0);
        owner_write_i(owner_a, 0xe80, 0);
        owner_write_i(owner_a, 0xe84, 0);

        float vel_scale;
        if (type == 0x8a) {
            aux_485979_call(0);
            se_play(0x13f);
            owner_write_i(owner_a, 0xcf8, 0x2d);
            owner_write_i(owner_a, 0xe90, 1);
            int32_t owner_int = slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_A);
            aux_482a51_call(owner_int + 0x930, 2);
            notify_queue_call(8, 4, 4, 0.5f);
            vel_scale = 1.4f;
        } else {
            owner_write_i(owner_a, 0xcf8, 0xf);
            owner_write_i(owner_a, 0xe90, 1);
            vel_scale = 0.8f;
        }

        /* Re-read ROT_X — the engine recomputes sin/cos here.  Field is
         * unchanged across the branch but the engine does fresh loads. */
        float rotx2 = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
        float sa2   = sinf(rotx2) * vel_scale;
        float ca2   = cosf(rotx2) * vel_scale;

        scene1_records_b_tick_kill_slot(i);
        owner_write_f(owner_a, 0x904, sa2 * -0.1f);
        owner_write_f(owner_a, 0x908, 0.3f);
        owner_write_f(owner_a, 0x90c, ca2 * -0.1f);
    }

    /* Unconditional kill checks (asm 0x43c8cd..0x43c8fe). */
    if (owner_read_i(owner_a, 0xe90) != 0) {
        scene1_records_b_tick_kill_slot(i);
    }
    if (owner_read_i(owner_a, 0xe7c) == 0) {
        scene1_records_b_tick_kill_slot(i);
    }
    if (owner_read_i(owner_a, 0xcf8) != 0) {
        scene1_records_b_tick_kill_slot(i);
    }
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 20000) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine L36805-L36870 / asm 0x43c5e9..0x43c738 — types 0x5b / 0x5c /
 * 0x5e / 0x86 / 0x87 (shared body).
 *
 *   slot[DRAG] = 1.5             ; default
 *   y_offset   = 1.0             ; default (used in AGE==2 spawn arg)
 *   if type == 0x87: slot[DRAG] = 2.5
 *   if type == 0x5b: y_offset   = 0
 *
 *   pose at owner+0x20 + (sin(ROT_X), 1, cos(ROT_X))   ; full radius
 *   ALT_POS direct-copy from owner+0x20 + (0, 1, 0)
 *     (engine calls sinf/cosf again, discards via fstp st(0))
 *
 *   if AGE == 2:
 *     scene1_spawn(0, POS_X, POS_Y + y_offset, POS_Z, 4, 1.8, 1)
 *
 *   if 1 < AGE < 6:
 *     body2_state_machine_5iter_loop
 *
 *   if (owner_a[+0xcf8] != 0): kill
 *   if (AGE == 0x14):           kill
 */
static void body_0x5b_group(int i, int32_t type)
{
    void *owner_a = slot_owner_a(i);
    if (!owner_a) return;

    float drag     = 1.5f;
    float y_offset = 1.0f;
    if (type == 0x87) drag = 2.5f;
    if (type == 0x5b) y_offset = 0.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);

    float rotx = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    float sa   = sinf(rotx);
    float ca   = cosf(rotx);
    float ox   = owner_read_f(owner_a, 0x20);
    float oy   = owner_read_f(owner_a, 0x24);
    float oz   = owner_read_f(owner_a, 0x28);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca + oz);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, oz);

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age == 2) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        scene1_spawn(0, px, py + y_offset, pz, 4, 1.8f, 1);
    }

    if (age >= 2 && age < 6) {
        body2_state_machine_5iter_loop(i);
    }

    if (owner_read_i(owner_a, 0xcf8) != 0) {
        scene1_records_b_tick_kill_slot(i);
    }
    if (age == 0x14) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine L36870-L36970 / asm 0x43c903..0x43cb46 — types 0x71 / 0x72 /
 * 0x7d (shared body, chr-walker dynamic-scale pose).
 *
 *   slot[DRAG] = 0.5             ; default
 *   age_f      = (float)AGE
 *   scale      = min(2.0, age_f * 0.2 + 0.5)
 *
 *   if type == 0x71 && AGE < 0x14:
 *     scale = sinf(age_f * π/2 / 20.0) * 2.5 + 0.5
 *   if type == 0x7d:
 *     scale = 2.5
 *     slot[DRAG] = 1.5
 *   if type == 0x72:
 *     scale *= 0.9
 *     slot[DRAG] = 0.4
 *
 *   pose at owner+0x20 + scale*(sin(ROT_X), _, cos(ROT_X))
 *   POS_Y = owner+0x24 + 1.0
 *
 *   compass dispatch via owner[+0x948] int:
 *     == 0:  POS_X -= 0.4
 *     == 4:  POS_X += 0.4
 *     else:  POS_Z += 0.4
 *
 *   if type == 0x7d && AGE == 1:
 *     scene1_pfo_table_a_alloc_passthrough(owner_a_int, POS_X, 0,
 *                                          POS_Z, 6, 1.0, -1, 0, 0)
 *
 *   kill_age = 0x14                ; default 0x71/0x7d
 *   if type == 0x72:
 *     kill_age = 20000
 *     slot[DRAG] = 1.0
 *     if AGE % 5 == 4: SEQ_ID = seq_counter_next()
 *
 *   if 3 < AGE < kill_age:
 *     body2_state_machine_5iter_loop
 *
 *   if (owner_a[+0xcf8] != 0): kill
 *
 *   if type == 0x72:
 *     if (owner_a[+0xe90] != 2): kill
 *     if AGE % 5 != 4: skip remaining age-kill check  (engine jmp 0x43fbbc)
 *     kill if AGE == 20000
 *   else:
 *     kill if AGE == 0x1e
 *
 * Note on AGE == 0x1e: the survey doc says 0x1e (= 30) but the kill_age
 * default for the loop window is 0x14 (= 20).  These are different
 * gates — 0x14 caps the state-machine loop window, 0x1e is the slot
 * lifespan kill.  Engine sets both unambiguously (asm 0x43cab0 push
 * 0x14 → edi as loop cap; 0x43cb3f cmp [esi+0x98], 0x1e → kill).
 */
static void body_0x71_72_7d(int i, int32_t type)
{
    void *owner_a = slot_owner_a(i);
    if (!owner_a) return;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.5f);

    int   age   = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    float age_f = (float)age;
    float scale = age_f * 0.2f + 0.5f;
    if (scale > 2.0f) scale = 2.0f;

    /* Engine 0x43c947-0x43c98d: for type 0x71, unconditionally override
     * `scale = 3.0`, then if AGE < 0x14 override again to the sin ramp.
     * The base min(2.0, AGE*0.2+0.5) path is dead code for 0x71. */
    if (type == 0x71) {
        scale = 3.0f;
        if (age < 0x14) {
            scale = sinf(age_f * 1.5707964f / 20.0f) * 2.5f + 0.5f;
        }
    }
    if (type == 0x7d) {
        scale = 2.5f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.5f);
    }
    if (type == 0x72) {
        scale *= 0.9f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.4f);
    }

    float rotx = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    float sa   = sinf(rotx);
    float ca   = cosf(rotx);
    float ox   = owner_read_f(owner_a, 0x20);
    float oy   = owner_read_f(owner_a, 0x24);
    float oz   = owner_read_f(owner_a, 0x28);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, scale * sa + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, scale * ca + oz);

    /* Compass dispatch via owner+0x948 int. */
    int32_t compass = owner_read_i(owner_a, 0x948);
    if (compass == 0) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - 0.4f);
    } else if (compass == 4) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px + 0.4f);
    } else {
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz + 0.4f);
    }

    if (type == 0x7d && age == 1) {
        int32_t owner_int = slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_A);
        float   px        = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float   pz        = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        scene1_pfo_table_a_alloc_passthrough(owner_int, px, 0.0f, pz,
                                             /*template_id=*/6,
                                             /*scale_base=*/1.0f,
                                             /*override_dur=*/-1,
                                             /*override_rot_y_bits=*/0,
                                             /*param_8=*/0);
    }

    int kill_age = 0x14;
    if (type == 0x72) {
        kill_age = 20000;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.0f);
        if (age % 5 == 4) {
            int32_t seq = (int32_t)g_scene1_record_b_seq_counter;
            g_scene1_record_b_seq_counter++;
            slot_set_i(i, SCENE1_RECORDS_B_OFF_SEQ_ID, seq);
        }
    }

    if (age > 3 && age < kill_age) {
        body2_state_machine_5iter_loop(i);
    }

    if (owner_read_i(owner_a, 0xcf8) != 0) {
        scene1_records_b_tick_kill_slot(i);
    }

    if (type == 0x72) {
        if (owner_read_i(owner_a, 0xe90) != 2) {
            scene1_records_b_tick_kill_slot(i);
        }
        /* Engine `if (AGE % 5 != 4) goto LAB_0043fbbc` — i.e. skip the
         * AGE==20000 kill check entirely on non-cadence frames.  The
         * SEQ_ID write above and the previous kill checks have already
         * fired, so a return here matches the engine's outer-loop
         * advance. */
        if (age % 5 != 4) return;
        if (age == 20000) {
            scene1_records_b_tick_kill_slot(i);
        }
    } else {
        if (age == 0x1e) {
            scene1_records_b_tick_kill_slot(i);
        }
    }
}

/* ─── C8j-tick.6 — Body 3 (L1050-L1187, asm 0x43cb4a..0x43cdef) ──────── */

/* Engine 0x43cb4f..0x43cba9 — type 0x1f body.
 *
 *   slot[LIFE_MULT] += 0.03
 *   if (LIFE_MULT > 1.5): LIFE_MULT = 1.5
 *   slot[DRAG] = LIFE_MULT * 0.1 - 0.5
 *   state_machine(slot)
 *   if (AGE == 0x78): kill
 *
 * Notable: NO owner read.  Pure slot state machine — LIFE_MULT ramps
 * up at 0.03/tick, asymptotes at 1.5; DRAG follows.  At AGE=120 the
 * slot dies (no spawn-on-death side effects).
 */
static void body_0x1f(int i)
{
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) + 0.03f;
    if (life_mult > 1.5f) life_mult = 1.5f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 0.1f - 0.5f);

    state_machine_call(slot_base(i));

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 0x78) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43cca5..0x43cde0 — types 0x5a / 0x98 shared body.
 *
 *   slot[LIFE_MULT] += 0.03
 *   if (LIFE_MULT > 2.0): LIFE_MULT = 2.0      ; different clamp from 0x1f
 *   slot[DRAG] = LIFE_MULT * 0.1 + 0.5         ; different sign from 0x1f
 *
 *   if ((PART_IDX + 3) * 10 < AGE && AGE < 0x78):
 *     ; horizontal drift toward player (PHC #8 sibling)
 *     VEL_X += (player.x - POS_X) * 0.003
 *     VEL_Z += (player.z - POS_Z) * 0.003
 *     VEL_X *= 0.95
 *     VEL_Z *= 0.95
 *
 *   if (AGE == 0x78):
 *     scene1_overlay_spawn(NULL, POS_X, POS_Y,     POS_Z, 7,   1.5, 0x1e, 0, 0, 0)
 *     scene1_overlay_spawn(NULL, POS_X, POS_Y-1.0, POS_Z, 0xb, 1.0, -1,    0, 0, 0)
 *     se_play(0x2ac)
 *     POS_Y += 1.0
 *     state_machine(slot)
 *     kill
 *
 *   if (owner_b+0x428 != 1): kill              ; engine LAB_0043cded
 *
 * Notable: reads engine player position (DAT_056da1d8 / DAT_056da1e0 =
 * g_scene1_player_pos[0/2]).  Reads OWNER_B (not OWNER_A like Body 1/2).
 */
static void body_0x5a_or_0x98(int i)
{
    const void *owner = slot_owner(i);
    if (!owner) return;

    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) + 0.03f;
    if (life_mult > 2.0f) life_mult = 2.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 0.1f + 0.5f);

    int part_idx = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    int age      = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    int age_lo   = (part_idx + 3) * 10;

    if (age_lo < age && age < 0x78) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        vx = (g_scene1_player_pos[0] - px) * 0.003f + vx;
        vz = (g_scene1_player_pos[2] - pz) * 0.003f + vz;
        vx *= 0.95f;
        vz *= 0.95f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz);
    }

    if (age == 0x78) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        overlay_spawn(NULL, px, py,        pz, 7,   1.5f, 0x1e, 0, 0, 0);
        overlay_spawn(NULL, px, py - 1.0f, pz, 0xb, 1.0f, -1,   0, 0, 0);
        se_play(0x2ac);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py + 1.0f);
        state_machine_call(slot_base(i));
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* Engine LAB_0043cded: kill on owner_b+0x428 != 1. */
    if (owner_read_i(owner, 0x428) != 1) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43cbc6..0x43cbef — type 0x6c body.
 *
 *   slot[DRAG] = LIFE_MULT * 0.1 - 0.5     ; same as 0x1f
 *   state_machine(slot)                    ; ARGLESS in asm (push esi)
 *   if (AGE == 0xc8): kill                 ; 0xc8 = 200
 *
 * No LIFE_MULT update (unlike 0x1f).
 */
static void body_0x6c(int i)
{
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 0.1f - 0.5f);

    state_machine_call(slot_base(i));

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 0xc8) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43cbf9..0x43cc60 — type 0x6b body.
 *
 *   if (AGE == 0x2d):
 *     se_play(0x2ac)
 *     scene1_overlay_spawn(NULL, POS_X, POS_Y+1.5, POS_Z, 7, 1.5, 0x78, 0, 0, 0)
 *   if (AGE >= 0x2d):
 *     state_machine(slot)
 *   if (AGE == 0x9b): kill
 *
 * No DRAG / LIFE_MULT writes.
 */
static void body_0x6b(int i)
{
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age == 0x2d) {
        se_play(0x2ac);
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        overlay_spawn(NULL, px, py + 1.5f, pz, 7, 1.5f, 0x78, 0, 0, 0);
    }
    if (age >= 0x2d) {
        state_machine_call(slot_base(i));
    }
    if (age == 0x9b) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43cc6b..0x43cc99 — type 0x28 body.
 *
 *   slot[DRAG]  = LIFE_MULT * 0.1     ; no offset
 *   slot[VEL_Y] -= 0.003
 *   state_machine(slot)
 *   if (AGE == 0x12c): kill            ; 0x12c = 300
 */
static void body_0x28(int i)
{
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 0.1f);

    float vel_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.003f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vel_y);

    state_machine_call(slot_base(i));

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 0x12c) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ─── C8j-tick.7 — scattered post-Body-3 bodies (asm 0x43cdef..0x43d0b6) ─ */

/* Engine 0x43cdef..0x43ce15 — type 0x38 body.
 *
 *   slot[DRAG] = 2.0
 *   state_machine(slot)
 *   if (AGE == 0x12c): kill          ; 0x12c = 300
 */
static void body_0x38(int i)
{
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 2.0f);
    state_machine_call(slot_base(i));
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 0x12c) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43ce15..0x43cf5c — type 0x29 body.  Mt. Everest piece in
 * this cascade — state machine 5-iter inner loop + .rdata-driven
 * lifecycle gates.
 *
 *   slot[DRAG] = LIFE_MULT * 4.0
 *   if (AGE == 1):
 *     notify_queue(0xa, 0x10, 0x10, 1.0)
 *   if (AGE > 10 && (float)AGE < SCALE_Y * 90.0):
 *     if (AGE % 3 == 1):
 *       iter_count = min(5, AGE/8 + 1)
 *       for n in [0, iter_count):
 *         POS_Y += (float)n * 3.0
 *         state_machine(slot)
 *         POS_Y -= (float)n * 3.0
 *     if (AGE % 10 == 0):
 *       SEQ_ID = seq_counter_next()
 *   spawn_age = (int)(SCALE_Y * 136.0 - 32.0)        ; __ftol truncation
 *   if (AGE < spawn_age):
 *     scene1_spawn(0, POS_X, POS_Y, POS_Z, 0x4e, LIFE_MULT*0.5, 1)
 *   if (SCALE_Y * 136.0 <= (float)AGE):
 *     kill
 *
 * SCALE_Y is slot byte 0xbc (dw 47) — entity-allocator scale field,
 * default 1.0 per C8j allocator preamble.  With SCALE_Y=1.0 the window
 * is AGE in (10, 90); spawn_age = 104; kill at AGE >= 136.
 */
static void body_0x29(int i)
{
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 4.0f);

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 1) {
        notify_queue_call(0xa, 0x10, 0x10, 1.0f);
    }

    float scale_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_SCALE_Y);

    if (age > 10 && (float)age < scale_y * 90.0f) {
        if (age % 3 == 1) {
            int iter_count = age / 8 + 1;
            if (iter_count > 5) iter_count = 5;
            for (int n = 0; n < iter_count; n++) {
                float offset = (float)n * 3.0f;
                float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py + offset);
                state_machine_call(slot_base(i));
                py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - offset);
            }
        }
        if (age % 10 == 0) {
            int32_t seq = (int32_t)g_scene1_record_b_seq_counter;
            g_scene1_record_b_seq_counter++;
            slot_set_i(i, SCENE1_RECORDS_B_OFF_SEQ_ID, seq);
        }
    }

    int spawn_age = (int)(scale_y * 136.0f - 32.0f);
    if (age < spawn_age) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        scene1_spawn(0, px, py, pz, 0x4e, life_mult * 0.5f, 1);
    }

    if (scale_y * 136.0f <= (float)age) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43d0b8..0x43d0e2 — type 0x8c body.
 *
 *   slot[DRAG]   = 1.0
 *   slot[ROT_X] += 0.15
 *   state_machine(slot)
 *   if (PART_IDX == 100): kill         ; engine `goto LAB_0043fc81` (kill)
 *   if (AGE > 0x4af): kill              ; 0x4af = 1199
 */
static void body_0x8c(int i)
{
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.0f);
    float rot_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X) + 0.15f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
    state_machine_call(slot_base(i));

    int part_idx = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    if (part_idx == 100) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age > 0x4af) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43cf67..0x43d0b6 — type 0x2c body.  Physics-bounce billboard:
 * gravity + damping on VEL_Y, two-axis rotation matrix accumulation,
 * one-shot ground-bounce trigger with notify_queue + SE on first hit,
 * FLAG-counter shutdown at 0x1e.
 *
 *   VEL_Y = (VEL_Y - 0.02) * 0.95          ; gravity (-0.02) + damping (0.95)
 *   ROT_SCR += 0.05; ROT_Z += 0.03
 *   mat4_rotation_x(MATRIX0, ROT_SCR)      ; engine stores X-rot here
 *   mat4_rotation_y(scratch,  ROT_Z)
 *   mat4_mul(MATRIX0, scratch, MATRIX0)    ; MATRIX0 = scratch * MATRIX0
 *
 *   if (VEL_Y < 0):
 *     if (ground_query(POS) == 1):
 *       threshold = LIFE_MULT * 0.5 + slot[AUX_9]
 *       if (POS_Y <= threshold):
 *         POS_Y = threshold
 *         VEL_Y *= -0.8                    ; bounce
 *         if (FLAG == 0):
 *           FLAG = 1
 *           notify_queue(4, 4, 4, 1.0)
 *           SE(0x168)
 *
 *   if (FLAG > 0): FLAG++
 *   if (FLAG == 0x1e): kill                ; 0x1e = 30 ticks post-first-hit
 *   if (FLAG == 0):
 *     DRAG = LIFE_MULT * 1.2
 *     state_machine(slot)                  ; consumes MATRIX0
 *
 *   mat4_identity(MATRIX0)                 ; wipe for next user
 *   if (AGE >= 0x12c): kill                ; 0x12c = 300
 */
static void body_0x2c(int i)
{
    int32_t *slot = slot_base(i);

    /* L37544 / asm 0x43cf67-0x43cf89: VEL_Y = (VEL_Y - 0.02) * 0.95. */
    float vel_y = (slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.02f) * 0.95f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vel_y);

    /* L37545-46 / asm 0x43cf8c-0x43cf9e: ROT_SCR += 0.05; ROT_Z += 0.03. */
    float rot_scr = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR) + 0.05f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, rot_scr);
    float rot_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z) + 0.03f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z, rot_z);

    /* L37547-49 / asm 0x43cfa0-0x43cfc6: MATRIX0 = rot_y(ROT_Z) *
     * rot_x(ROT_SCR).  Engine asm pushes (MATRIX0, scratch, MATRIX0) to
     * mat4_multiply; our `mat4_mul(out, a, b)` computes out = a * b. */
    float *mat0 = (float *)(slot + SCENE1_RECORDS_B_OFF_MATRIX0);
    float scratch[16];
    mat4_rotation_x(mat0, rot_scr);
    mat4_rotation_y(scratch, rot_z);
    mat4_mul(mat0, scratch, mat0);

    /* L37550-63 / asm 0x43cfcb-0x43d05d: bounce gate.  Three conjoined
     * conditions all must hold. */
    if (vel_y < 0.0f) {
        float gy = 0.0f;
        float pos_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pos_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pos_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        int hit = ground_query(pos_x, pos_y, pos_z, &gy);
        if (hit == 1) {
            float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
            float aux9      = slot_get_f(i, SCENE1_RECORDS_B_OFF_AUX_9);
            float threshold = life_mult * 0.5f + aux9;
            if (pos_y <= threshold) {
                slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, threshold);
                vel_y *= -0.8f;
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vel_y);
                if (slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX) == 0) {
                    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
                    notify_queue_call(4, 4, 4, 1.0f);
                    se_play(0x168);
                }
            }
        }
    }

    /* L37564-67 / asm 0x43d064-0x43d075: FLAG > 0 → FLAG++. */
    int flag = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    if (flag > 0) {
        flag = flag + 1;
        slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, flag);
    }

    /* L37568 / asm 0x43d077-0x43d07c: FLAG == 0x1e → kill + skip remainder. */
    if (flag == 0x1e) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* L37569-72 / asm 0x43d082-0x43d09e: FLAG == 0 → DRAG = LIFE_MULT *
     * 1.2; state_machine(slot).  State machine consumes MATRIX0. */
    if (flag == 0) {
        float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 1.2f);
        state_machine_call(slot);
    }

    /* L37573 / asm 0x43d09f-0x43d0a1: mat4_identity(MATRIX0) — engine
     * wipes the scratch matrix for the next consumer.  Without this, the
     * pre-multiply work would leak into whoever next reads slot[MATRIX0]
     * (likely a different per-type body next tick, or the render path). */
    mat4_identity(mat0);

    /* L37574-76 / asm 0x43d0a5-0x43d0b0: AGE >= 0x12c (300) → kill. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) >= 0x12c) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43d100..0x43d3f4 — type 0x23 body.  Ground-bouncer with
 * one-shot impact-spawn cascade (7 particles + 30-iter state-machine
 * loop) and a delayed FLAG-counter graceful shutdown.
 *
 *   slot[DRAG]   = LIFE_MULT * 3.0
 *   state_machine(slot)
 *   slot[ROT_SCR] += 0.04
 *   slot[ROT_X]   += 0.04
 *
 *   if (FLAG == 0):
 *     ; engine fires two sin/cos calls on (π - camera_yaw) and discards
 *     ; both results (asm 0x43d152-0x43d196 + 0x43d196 fstp st0 ×2).
 *     ; pure dead code — no FPU side effects relevant in our port.
 *     if (AGE & 1):
 *       scene1_spawn(0, POS_X, POS_Y, POS_Z, 0x53, LIFE_MULT*0.1, 1)
 *     gy = 0; hit = ground_query(POS)
 *     if (hit == 1 && POS_Y < gy + 1.0):
 *       POS_Y = gy; FLAG = 1; VEL_Y = 0
 *       aux_4532bc(0x20)
 *       notify_queue(0x28, 0x10, 0x10, 1.0)
 *       scene1_spawn(0, POS, 0x0f, LIFE_MULT,      1)
 *       scene1_spawn(0, POS, 0x36, LIFE_MULT*0.8, 0x40)
 *       scene1_spawn(0, POS, 0x2a, LIFE_MULT*0.4,  1)
 *       scene1_spawn(0, POS, 0x52, LIFE_MULT*0.7,  1)
 *       scene1_spawn(0, POS_X, gy,       POS_Z, 0x51, LIFE_MULT, 2)
 *       scene1_spawn(0, POS_X, gy + 2.0, POS_Z, 0x51, LIFE_MULT, 2)
 *       scene1_spawn(0, POS_X, gy + 2.0, POS_Z, 0x51, LIFE_MULT, 2) ; dup (engine)
 *       DRAG = LIFE_MULT * 8.0
 *       for n in [0, 30): if !state_machine(slot): break
 *   else (FLAG != 0):
 *     FLAG = FLAG + 1
 *     if (FLAG > 20):  POS_Y -= 0.1
 *     if (FLAG == 10): kill            ; only reachable when FLAG was 9
 *
 *   if (AGE == 200): kill
 *
 * The duplicate 0x51 spawn at (POS_X, gy+2, POS_Z) matches Ghidra L37631
 * and asm 0x43d362..0x43d392 verbatim — appears intentional (or a long-
 * standing engine copy-paste) and we preserve it.
 */
static void body_0x23(int i)
{
    int32_t *slot = slot_base(i);

    /* asm 0x43d103-0x43d116: DRAG = LIFE_MULT * 3.0; state_machine. */
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 3.0f);
    state_machine_call(slot);

    /* asm 0x43d11b-0x43d142: ROT_SCR += 0.04; ROT_X += 0.04. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR,
               slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR) + 0.04f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,
               slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X) + 0.04f);

    int flag = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    int age  = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (flag == 0) {
        /* asm 0x43d152-0x43d196: vestigial sin/cos discard (omitted). */

        /* asm 0x43d18d-0x43d1c7: AGE&1 → particle spawn 0x53. */
        if (age & 1) {
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            scene1_spawn(0, px, py, pz, 0x53, life_mult * 0.1f, 1);
        }

        /* asm 0x43d1ca-0x43d209: ground_query + bounce gate.  Ghidra
         * dropped the (z, &out_buf) trailing args at L37608 — full asm
         * shows the 4-arg signature `(POS_X, POS_Y, POS_Z, &local_140)`
         * with the ground Y read back from local_138 (= base + 0xc). */
        float gy = 0.0f;
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        int hit = ground_query(px, py, pz, &gy);
        if (hit == 1 && py < gy + 1.0f) {
            /* asm 0x43d20f-0x43d228: snap POS_Y to ground, FLAG=1, VEL_Y=0. */
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, gy);
            slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
            py = gy;

            /* asm 0x43d228: FUN_004532bc(0x20). */
            aux_4532bc_call(0x20);

            /* asm 0x43d22d-0x43d23d: notify_queue(0x28, 0x10, 0x10, 1.0). */
            notify_queue_call(0x28, 0x10, 0x10, 1.0f);

            /* asm 0x43d23d-0x43d265: scene1_spawn(0, POS, 0x0f, LIFE_MULT, 1). */
            scene1_spawn(0, px, py, pz, 0x0f, life_mult, 1);
            /* asm 0x43d26a-0x43d297: scene1_spawn(0, POS, 0x36, LIFE_MULT*0.8, 0x40). */
            scene1_spawn(0, px, py, pz, 0x36, life_mult * 0.8f, 0x40);
            /* asm 0x43d29c-0x43d2c8: scene1_spawn(0, POS, 0x2a, LIFE_MULT*0.4, 1). */
            scene1_spawn(0, px, py, pz, 0x2a, life_mult * 0.4f, 1);
            /* asm 0x43d2cd-0x43d2f9: scene1_spawn(0, POS, 0x52, LIFE_MULT*0.7, 1). */
            scene1_spawn(0, px, py, pz, 0x52, life_mult * 0.7f, 1);
            /* asm 0x43d2fe-0x43d328: 0x51 stack at gy. */
            scene1_spawn(0, px, gy, pz, 0x51, life_mult, 2);
            /* asm 0x43d333-0x43d35d: 0x51 stack at gy + 2.0. */
            scene1_spawn(0, px, gy + 2.0f, pz, 0x51, life_mult, 2);
            /* asm 0x43d362-0x43d392: 0x51 stack at gy + 2.0 (DUP — engine
             * preserves this duplicate verbatim, see Ghidra L37631). */
            scene1_spawn(0, px, gy + 2.0f, pz, 0x51, life_mult, 2);

            /* asm 0x43d397-0x43d3bd: DRAG = LIFE_MULT * 8.0; 30-iter
             * state_machine early-break loop. */
            slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 8.0f);
            for (int counter = 0; counter < 30; counter++) {
                if (!state_machine_call_ret(slot)) break;
            }
        }
    } else {
        /* asm 0x43d3bf-0x43d3e0: FLAG++ then POS_Y dec then maybe-kill. */
        int new_flag = flag + 1;
        slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, new_flag);
        if (new_flag > 0x14) {
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
                       slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y) - 0.1f);
        }
        if (new_flag == 10) {
            scene1_records_b_tick_kill_slot(i);
        }
    }

    /* asm 0x43d3e2-0x43d3f0: AGE == 200 → kill (re-check current age, not
     * the cached `age` from earlier — engine reads [esi+0x98] fresh). */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 200) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43d3f6..0x43d5f8 — type 0x3a body.  Alternate ground-bouncer:
 * no DRAG/state_machine preamble; cleanup-spawn cascade gates on either
 * ground-hit OR the (always-run) DRAG=0.5 state-machine "made progress".
 *
 *   if (FLAG == 0):
 *     ; vestigial sin/cos discard at 0x43d40d-0x43d44f (omitted).
 *     bvar17 = false
 *     if (AGE & 1):
 *       scene1_spawn(0, POS, 0x53, LIFE_MULT*0.1, 1)
 *     gy = 0; hit = ground_query(POS)
 *     if (hit == 1 && POS_Y < gy + 1.0):
 *       POS_Y = gy; FLAG = 1; VEL_Y = 0
 *       DRAG = 3.0
 *       state_machine(slot)
 *       bvar17 = true
 *     DRAG = 0.5
 *     if (state_machine(slot) returned non-zero):
 *       bvar17 = true
 *     if (bvar17):
 *       aux_4532bc(0x20)
 *       notify_queue(0x28, 0x10, 0x10, 1.0)
 *       scene1_spawn(0, POS, 0x52, LIFE_MULT*0.7, 1)
 *       scene1_spawn(0, POS_X, POS_Y,       POS_Z, 0x51, LIFE_MULT, 2)
 *       scene1_spawn(0, POS_X, POS_Y + 2.0, POS_Z, 0x51, LIFE_MULT, 2)
 *       scene1_spawn(0, POS_X, POS_Y + 2.0, POS_Z, 0x51, LIFE_MULT, 2) ; dup
 *       kill
 *
 *   if (AGE == 0x78): kill              ; 0x78 = 120
 *
 * Note vs 0x23: this body has NO FLAG != 0 branch — when FLAG != 0 we
 * skip straight to the AGE-kill check.  The 0x51 stack uses POS_Y (which
 * equals gy after the ground snap) instead of gy directly — same end
 * value when ground-hit, but observable when a future caller mutates
 * POS_Y between the snap and the spawn.
 */
static void body_0x3a(int i)
{
    int32_t *slot = slot_base(i);

    int flag = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);

    if (flag == 0) {
        /* asm 0x43d40d-0x43d44f: vestigial sin/cos discard (omitted). */

        int bvar17 = 0;
        int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
        float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);

        /* asm 0x43d448-0x43d482: AGE&1 → spawn 0x53. */
        if (age & 1) {
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            scene1_spawn(0, px, py, pz, 0x53, life_mult * 0.1f, 1);
        }

        /* asm 0x43d485-0x43d4ec: ground_query + bounce + DRAG=3.0 state_machine. */
        float gy = 0.0f;
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        int hit = ground_query(px, py, pz, &gy);
        if (hit == 1 && py < gy + 1.0f) {
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, gy);
            slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 3.0f);
            state_machine_call(slot);
            bvar17 = 1;
        }

        /* asm 0x43d4ed-0x43d506: DRAG = 0.5; state_machine; bvar17 |= ret!=0. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.5f);
        if (state_machine_call_ret(slot)) {
            bvar17 = 1;
        }

        if (bvar17) {
            /* asm 0x43d50f-0x43d521: trigger + notify. */
            aux_4532bc_call(0x20);
            notify_queue_call(0x28, 0x10, 0x10, 1.0f);

            /* asm 0x43d526-0x43d5e8: 4 scene1_spawn calls.  Reload POS_Y
             * since the ground-hit branch may have snapped it. */
            px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            scene1_spawn(0, px, py, pz, 0x52, life_mult * 0.7f, 1);
            scene1_spawn(0, px, py,         pz, 0x51, life_mult, 2);
            scene1_spawn(0, px, py + 2.0f,  pz, 0x51, life_mult, 2);
            scene1_spawn(0, px, py + 2.0f,  pz, 0x51, life_mult, 2);

            /* asm 0x43d5eb: kill. */
            scene1_records_b_tick_kill_slot(i);
        }
    }

    /* asm 0x43d5ed-0x43d5f4: AGE == 0x78 → kill. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x78) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.9 — scattered post-Body-3 to Body 5 cluster ═══════════════
 * Engine asm 0x43d5f8..0x43dc02.  Six per-type bodies covering the gap
 * between C8j-tick.8 (0x3a) and Body 6 (10/0xb/0x14/0x13/0x99 at 0x43dc03).
 *
 *   0x3c (0x43d5f8..0x43d669) — small physics: DRAG=0.1; state_machine on
 *     AGE in (7, 100); on AGE==1 fire scene1_spawn(0x54) with arg7 =
 *     rng_next15(); kill on AGE==0x78.
 *   0x3b (0x43d66a..0x43d7b6) — NPC sister-spawn with owner.pos swap:
 *     DRAG=0.1; temporarily writes slot.POS into owner.pos[3f0..3f8],
 *     fires scene1_record_b_spawn_npc(owner_b, 0x3c, 1), restores
 *     owner.pos.  Then for AGE in (30, 120) drifts VEL_{X,Z} toward
 *     (player_pos[0] + ALT_POS_X) / (player_pos[2] + ALT_POS_Z) via
 *     `vel += (target - pos) * 0.005`, then damps `vel *= 0.95`.
 *     Caps |VEL_xz| to 0.6 via Pythagoras + sqrt + scale.  Kill on
 *     AGE==0x100.
 *   Body 5 — 0x21/0x25/0x31/0x32 (0x43d7b7..0x43d845) — shared state
 *     machine body: LIFE_MULT += 0.002; DRAG = LIFE_MULT*0.1; ROT_Z +=
 *     0.03; type==0x21 → state_machine if AGE<0x48, kill AGE==0x50;
 *     others → state_machine if AGE<0xf8, kill AGE==0x100.
 *   0x2b (0x43d846..0x43d9ec) — ground-bounce with overlay cascade:
 *     DRAG = LIFE_MULT*0.2; state_machine if AGE<0x48; FLAG==1 short-
 *     circuit to AGE==0x50 kill check.  FLAG!=1 path: ROT_SCR += 0.05,
 *     ROT_Z += 0.03, MATRIX0 = rot_y(ROT_Z) × rot_x(ROT_SCR),
 *     VEL_Y -= 0.02, ground_query(POS).  On hit + VEL_Y<0 + POS_Y <
 *     LIFE_MULT*0.5 + ground_y: POS_Y = threshold, VEL_{X,Y,Z}=0, FLAG=1,
 *     AGE=0x28, fire 2 scene1_overlay_spawn calls (type 7 scale 1.5 dur
 *     0x20 at POS; type 0xb scale 1.0 dur -1 at POS-(0,1,0)), and if
 *     (slot_idx & 1) play SE(0x2c0).  Kill AGE==0x50.
 *   0x26/0x2a (0x43d9ed..0x43dac1) — shared body: LIFE_MULT += 0.002;
 *     DRAG = LIFE_MULT*0.2; ROT_Z += 0.03; state_machine if AGE<0x98;
 *     ground_query.  On hit + VEL_Y<0 + POS_Y < LIFE_MULT*0.5 + ground_y:
 *     POS_Y = threshold, VEL_Y = -VEL_Y (true bounce vs 0x2b's snap-to-
 *     zero).  Kill AGE==0xa0.
 *   0x27 (0x43dac7..0x43dc02) — three-phase state machine.  FLAG==2:
 *     LIFE_MULT -= 0.1; kill if LIFE_MULT < 0 (fade-out phase).  FLAG==1:
 *     LIFE_MULT += 0.3 (clamp 10.0); DRAG = LIFE_MULT*0.5; state_machine
 *     (grow phase).  FLAG==0 (default): ROT_Z += 0.03; VEL_Y -= 0.01;
 *     LIFE_MULT += 0.1; ground_query.  On hit + POS_Y < ground_y + 0.3:
 *     POS_Y = ground_y + 0.3, VEL_{X,Y,Z}=0, FLAG=1, LIFE_MULT += 0.5
 *     (transition phase 0 → 1).
 *
 * Engine quirk: 0x2b's per-tick SE gate uses `[ebp-0x28] & 1` which is
 * the function-local loop iterator (= slot index `i`), so half of all
 * 0x2b slots play SE(0x2c0) on every bounce — even/odd by slot index.
 * No new globals or hooks; all use existing scene1_spawn, scene1_overlay_spawn,
 * scene1_record_b_spawn_npc, ground_query, notify_queue, aux_4532bc, se_play. */

/* Engine 0x43d5f8..0x43d669 — type 0x3c body.
 *
 *   slot[DRAG] = 0.1f
 *   if 8 <= AGE < 100: state_machine(slot)
 *   if AGE == 1:
 *     u = rng_next15()                       ; engine call to 0x471084
 *     scene1_spawn(0, POS_X, POS_Y+1.0, POS_Z, 0x54, 0.1f, u)
 *   if AGE == 0x78: kill                     ; 120
 *
 * Ghidra dropped the trailing scale/param7 args at L37708 — asm
 * (0x43d629..0x43d65c) shows 7 pushes: edi=0, POS_X, POS_Y+1.0, POS_Z,
 * 0x54, 0.1f, eax(=rng_next15()).
 */
static void body_0x3c(int i)
{
    int32_t *slot = slot_base(i);

    /* asm 0x43d5fd-0x43d603: DRAG = 0.1f. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.1f);

    /* asm 0x43d609-0x43d61f: state_machine if 8 <= AGE < 100. */
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age >= 8 && age < 100) {
        state_machine_call(slot);
    }

    /* asm 0x43d620-0x43d65c: AGE == 1 → spawn 0x54 with rng_next15() as
     * param7.  Ghidra drops scale + param7 at L37708; asm preserves them. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 1) {
        uint16_t u = rng_next15();
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        scene1_spawn(0, px, py + 1.0f, pz, 0x54, 0.1f, (int)u);
    }

    /* asm 0x43d65f-0x43d668: AGE == 0x78 → kill. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x78) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43d66a..0x43d7b6 — type 0x3b body.
 *
 *   slot[DRAG] = 0.1f
 *   save = (owner.pos.x, owner.pos.y, owner.pos.z)         ; ebp-{4,20,24}
 *   owner.pos = slot.POS                                   ; temp graft
 *   scene1_record_b_spawn_npc(owner_b, 0x3c, 1)
 *   owner.pos = save                                       ; restore
 *
 *   ; per-tick drift gate (NPC owner is now the freshly spawned 0x3c slot)
 *   if 30 < AGE < 120:
 *     vel.x += (player.x + ALT_POS_X - POS_X) * 0.005
 *     vel.z += (player.z + ALT_POS_Z - POS_Z) * 0.005
 *     vel.x *= 0.95
 *     vel.z *= 0.95
 *
 *   ; clamp |vel.xz| to 0.6
 *   speed_sq = vel.x*vel.x + vel.z*vel.z
 *   if speed_sq > 0:
 *     speed = sqrtf(speed_sq)
 *     if speed > 0.6:
 *       vel.x = vel.x * 0.6 / speed
 *       vel.z = vel.z * 0.6 / speed
 *
 *   if AGE == 0x100: kill
 *
 * Owner pose lives at owner+0x3f0/0x3f4/0x3f8 (= pose triplet, same as
 * the NPC owner pose convention).  OWNER_B field stores owner pointer
 * (npc-allocator side).
 */
static void body_0x3b(int i)
{
    int32_t *slot = slot_base(i);

    /* asm 0x43d673-0x43d67d: DRAG = 0.1f. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.1f);

    /* asm 0x43d683-0x43d6e0: owner.pos save → graft slot.POS → spawn NPC
     * sister → restore owner.pos. */
    int32_t owner_ptr_int = slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_B);
    if (owner_ptr_int) {
        uint8_t *owner = (uint8_t *)(uintptr_t)owner_ptr_int;
        float save_x, save_y, save_z;
        memcpy(&save_x, owner + 0x3f0, sizeof save_x);
        memcpy(&save_y, owner + 0x3f4, sizeof save_y);
        memcpy(&save_z, owner + 0x3f8, sizeof save_z);

        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        memcpy(owner + 0x3f0, &px, sizeof px);
        memcpy(owner + 0x3f4, &py, sizeof py);
        memcpy(owner + 0x3f8, &pz, sizeof pz);

        scene1_record_b_spawn_npc(owner, 0x3c, 1);

        /* Owner pointer may have moved (asm reloads [esi+0x14] post-call). */
        int32_t reloaded_int = slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_B);
        if (reloaded_int) {
            uint8_t *owner2 = (uint8_t *)(uintptr_t)reloaded_int;
            memcpy(owner2 + 0x3f0, &save_x, sizeof save_x);
            memcpy(owner2 + 0x3f4, &save_y, sizeof save_y);
            memcpy(owner2 + 0x3f8, &save_z, sizeof save_z);
        }
    }

    /* asm 0x43d6e1-0x43d738: drift body, gated on AGE in (30, 120). */
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age > 0x1e && age < 0x78) {
        float pos_x  = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pos_z  = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float alt_x  = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
        float alt_z  = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
        float vel_x  = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vel_z  = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

        vel_x = ((g_scene1_player_pos[0] + alt_x - pos_x) * 0.005f + vel_x)
                * 0.95f;
        vel_z = ((g_scene1_player_pos[2] + alt_z - pos_z) * 0.005f + vel_z)
                * 0.95f;

        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vel_x);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vel_z);
    }

    /* asm 0x43d739-0x43d7a7: |vel.xz| cap to 0.6. */
    float vel_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
    float vel_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
    float speed_sq = vel_x * vel_x + vel_z * vel_z;
    if (speed_sq > 0.0f) {
        float speed = sqrtf(speed_sq);
        if (speed > 0.6f) {
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vel_x * 0.6f / speed);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vel_z * 0.6f / speed);
        }
    }

    /* asm 0x43d7a8-0x43d7b6: AGE == 0x100 → kill. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x100) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43d7b7..0x43d845 — Body 5 (types 0x21/0x25/0x31/0x32).
 *
 *   LIFE_MULT += 0.002
 *   slot[DRAG] = LIFE_MULT * 0.1
 *   ROT_Z += 0.03
 *   if type == 0x21:
 *     if AGE < 0x48: state_machine(slot)
 *     if AGE == 0x50: kill
 *   else (0x25/0x31/0x32):
 *     if AGE < 0xf8: state_machine(slot)
 *     if AGE == 0x100: kill
 */
static void body_021_to_032(int i, int32_t type)
{
    int32_t *slot = slot_base(i);

    /* asm 0x43d7cd-0x43d805: LIFE_MULT += 0.002; DRAG = LIFE_MULT*0.1;
     * ROT_Z += 0.03.  Engine ROT_Z is at slot+0x94 = dw 37. */
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) + 0.002f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,      life_mult * 0.1f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z) + 0.03f);

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (type == 0x21) {
        /* asm 0x43d80b-0x43d82c: 0x21 short-life. */
        if (age < 0x48) state_machine_call(slot);
        if (age == 0x50) scene1_records_b_tick_kill_slot(i);
    } else {
        /* asm 0x43d824-0x43d843: 0x25/0x31/0x32 long-life. */
        if (age < 0xf8) state_machine_call(slot);
        if (age == 0x100) scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43d846..0x43d9ec — type 0x2b body.
 *
 *   slot[DRAG] = LIFE_MULT * 0.2
 *   if AGE < 0x48: state_machine(slot)
 *   if FLAG == 1: goto END (which checks AGE==0x50 → kill)
 *
 *   ; FLAG != 1 path
 *   ROT_SCR += 0.05
 *   ROT_Z   += 0.03
 *   mat4_rotation_x(MATRIX0, ROT_SCR)
 *   mat4_rotation_y(scratch, ROT_Z)
 *   mat4_mul(MATRIX0, scratch, MATRIX0)
 *   VEL_Y -= 0.02
 *
 *   hit = ground_query(POS_X, POS_Y, POS_Z, &gy)
 *   if hit == 1 && VEL_Y < 0:
 *     threshold = LIFE_MULT * 0.5 + gy
 *     if POS_Y < threshold:
 *       POS_Y = threshold
 *       VEL_X = VEL_Y = VEL_Z = 0
 *       FLAG = 1
 *       AGE = 0x28
 *       scene1_overlay_spawn(NULL, POS_X, POS_Y, POS_Z, 7, 1.5f, 0x20,
 *                            0, 0, 0)
 *       scene1_overlay_spawn(NULL, POS_X, POS_Y-1.0, POS_Z, 0xb, 1.0f,
 *                            -1, 0, 0, 0)
 *       if (slot_idx & 1): SE(0x2c0)            ; engine [ebp-0x28] = iter
 *
 *   if AGE == 0x50: kill
 *
 * Ghidra-dropped arg in scene1_overlay_spawn call 1: arg8 (override_rot_y)
 * — asm at 0x43d96f shows fldz + fstp [esp] writing 0.0f as the 8th arg
 * (since override_rot_y is `int` in the API, 0.0f bit pattern = int 0).
 */
static void body_0x2b(int i)
{
    int32_t *slot = slot_base(i);

    /* asm 0x43d84f-0x43d860: DRAG = LIFE_MULT * 0.2. */
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, life_mult * 0.2f);

    /* asm 0x43d861-0x43d870: state_machine if AGE < 0x48. */
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age < 0x48) state_machine_call(slot);

    /* asm 0x43d871-0x43d878: FLAG == 1 → skip the rest, fall through to
     * AGE==0x50 kill. */
    int flag = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    if (flag != 1) {
        /* asm 0x43d87e-0x43d8a7: ROT_SCR += 0.05; ROT_Z += 0.03. */
        float rot_scr = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR) + 0.05f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, rot_scr);
        float rot_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z) + 0.03f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z, rot_z);

        /* asm 0x43d8a9-0x43d8d3: MATRIX0 = rot_y(ROT_Z) × rot_x(ROT_SCR).
         * Engine pushes (MATRIX0, scratch, MATRIX0) to mat4_multiply; our
         * `mat4_mul(out, a, b)` computes out = a × b — so the call is
         * `mat4_mul(MATRIX0, scratch, MATRIX0)` = MATRIX0 = scratch × MATRIX0
         * after we filled scratch with rot_y(ROT_Z) and MATRIX0 with
         * rot_x(ROT_SCR). */
        float *mat0 = (float *)(slot + SCENE1_RECORDS_B_OFF_MATRIX0);
        float scratch[16];
        mat4_rotation_x(mat0, rot_scr);
        mat4_rotation_y(scratch, rot_z);
        mat4_mul(mat0, scratch, mat0);

        /* asm 0x43d8d4-0x43d8e2: VEL_Y -= 0.02. */
        float vel_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.02f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vel_y);

        /* asm 0x43d8e5-0x43d903: ground_query.  Engine passes a 4-float
         * out buffer at [esi+0x18] (= slot offset dw 6 — scratch space);
         * we collapse to a single `float *out_y`. */
        float gy = 0.0f;
        float pos_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pos_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pos_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        int hit = ground_query(pos_x, pos_y, pos_z, &gy);

        /* asm 0x43d904-0x43d937: hit AND VEL_Y < 0 AND POS_Y < threshold. */
        if (hit == 1 && vel_y < 0.0f) {
            float threshold = life_mult * 0.5f + gy;
            if (pos_y < threshold) {
                /* asm 0x43d93d-0x43d96c: snap + zero vel + FLAG=1 + AGE=0x28. */
                slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, threshold);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);
                slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
                slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE,      0x28);

                /* asm 0x43d96f-0x43d998: overlay spawn 1.  9 args (Ghidra
                 * dropped override_rot_y at L37801). */
                overlay_spawn(NULL, pos_x, threshold, pos_z,
                                     /*template=*/7, /*scale=*/1.5f,
                                     /*override_dur=*/0x20,
                                     /*override_rot_y=*/0,
                                     /*shape_mode=*/0, /*mode=*/0);

                /* asm 0x43d99b-0x43d9cd: overlay spawn 2. */
                overlay_spawn(NULL, pos_x, threshold - 1.0f, pos_z,
                                     /*template=*/0xb, /*scale=*/1.0f,
                                     /*override_dur=*/-1,
                                     /*override_rot_y=*/0,
                                     /*shape_mode=*/0, /*mode=*/0);

                /* asm 0x43d9d0-0x43d9e0: (slot_idx & 1) → SE(0x2c0).
                 * Engine `test [ebp-0x28], 0x1` reads the function-local
                 * loop iterator (= slot index). */
                if (i & 1) se_play(0x2c0);
            }
        }
    }

    /* asm 0x43d9e1-0x43d9ec: AGE == 0x50 → kill. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x50) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43d9ed..0x43dac1 — type 0x26/0x2a shared body.
 *
 *   LIFE_MULT += 0.002
 *   slot[DRAG] = LIFE_MULT * 0.2
 *   ROT_Z += 0.03
 *   if AGE < 0x98: state_machine(slot)
 *   hit = ground_query(POS_X, POS_Y, POS_Z, &gy)
 *   if hit == 1 && VEL_Y < 0:
 *     threshold = LIFE_MULT * 0.5 + gy
 *     if POS_Y < threshold:
 *       POS_Y = threshold
 *       VEL_Y = -VEL_Y                        ; true bounce, not zero-snap
 *   if AGE == 0xa0: kill
 *
 * Distinct from 0x2b: bounces (VEL_Y inverted) instead of snap-to-zero;
 * no overlay spawn cascade; no FLAG check; longer life (0xa0 vs 0x50).
 */
static void body_0x26_or_0x2a(int i)
{
    int32_t *slot = slot_base(i);

    /* asm 0x43d9fd-0x43da32: LIFE_MULT += 0.002; DRAG = LIFE_MULT*0.2;
     * ROT_Z += 0.03. */
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) + 0.002f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,      life_mult * 0.2f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z) + 0.03f);

    /* asm 0x43da34-0x43da46: state_machine if AGE < 0x98. */
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age < 0x98) state_machine_call(slot);

    /* asm 0x43da47-0x43daa9: ground_query + bounce. */
    float gy = 0.0f;
    float pos_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float pos_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    float pos_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
    int hit = ground_query(pos_x, pos_y, pos_z, &gy);
    if (hit == 1) {
        float vel_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        if (vel_y < 0.0f) {
            float threshold = life_mult * 0.5f + gy;
            if (pos_y < threshold) {
                slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, threshold);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, -vel_y);
            }
        }
    }

    /* asm 0x43daac-0x43dab9: AGE == 0xa0 → kill. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0xa0) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43dac7..0x43dc02 — type 0x27 body.  Three-phase state machine
 * gated on slot[FLAG]:
 *
 *   FLAG == 2 (fade-out):
 *     LIFE_MULT -= 0.1
 *     if LIFE_MULT < 0: kill
 *
 *   FLAG == 1 (grow):
 *     LIFE_MULT += 0.3 (clamp 10.0)
 *     slot[DRAG] = LIFE_MULT * 0.5
 *     state_machine(slot)
 *
 *   FLAG == 0 (default — pre-bounce drift):
 *     ROT_Z      += 0.03
 *     VEL_Y      -= 0.01
 *     LIFE_MULT  += 0.1
 *     hit = ground_query(POS_X, POS_Y, POS_Z, &gy)
 *     if hit == 1:
 *       threshold = gy + 0.3
 *       if POS_Y < threshold:
 *         POS_Y     = threshold
 *         FLAG      = 1                       ; phase 0 → 1
 *         VEL_X = VEL_Y = VEL_Z = 0
 *         LIFE_MULT += 0.5                    ; one-shot splash boost
 *
 * No AGE-based kill — the body relies on FLAG==2's LIFE_MULT-fade-to-zero.
 */
static void body_0x27(int i)
{
    int32_t *slot = slot_base(i);
    int flag = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);

    if (flag == 2) {
        /* asm 0x43dadb-0x43db09: fade-out phase. */
        float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 0.1f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
        if (life_mult < 0.0f) {
            scene1_records_b_tick_kill_slot(i);
        }
    } else if (flag == 1) {
        /* asm 0x43db12-0x43db5d: grow phase.  Clamp at 10.0. */
        float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) + 0.3f;
        if (life_mult > 10.0f) life_mult = 10.0f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,      life_mult * 0.5f);
        state_machine_call(slot);
    } else {
        /* asm 0x43db62-0x43dc02: default (FLAG==0) — pre-bounce drift. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
                   slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z) + 0.03f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y,
                   slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.01f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT,
                   slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) + 0.1f);

        float gy = 0.0f;
        float pos_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pos_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pos_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        int hit = ground_query(pos_x, pos_y, pos_z, &gy);
        if (hit == 1) {
            float threshold = gy + 0.3f;
            if (pos_y < threshold) {
                slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,    threshold);
                slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X,    0.0f);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y,    0.0f);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z,    0.0f);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT,
                           slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) + 0.5f);
            }
        }
    }
}

/* ═══ C8j-tick.10 — Body 6 + Body 7 (motion-table-driven anchor) ═══════
 * Engine asm 0x43dc03..0x43dd79.  Two small bodies that share the new
 * per-NPC-motion-style physical-constants table at DAT_005c2434/8/c
 * (modeled as g_scene1_b_motion_table[256]; PHC #19).  Both anchor the
 * slot's POS to owner+0x3f0/3f4/3f8 every tick with a motion-style-
 * scaled Y offset; pose is rewritten unconditionally each tick.
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x5193a0 = 0.1f    0x5198e0 = 1.5f    0x5194ec = 0.3f
 *   0x51935c = 0.5f    0x519c20 = -0.8f
 */

/* Engine 0x43dc03..0x43dcdb — Body 6 (types 0x10/0xb/0x14/0x13/0x99).
 *
 *   if owner+0x428 != 1: kill slot
 *   motion_idx = owner+0x424
 *   if motion_idx in {0xd, 0xe}: DRAG = -0.8
 *   else: DRAG = ((entry.drag_base + 0.1 - 1.5) * owner+0xabc *
 *                 entry.drag_mul) - 0.3
 *   POS_X = owner+0x3f0
 *   POS_Y = owner+0xabc * entry.pos_y_mul * entry.drag_mul * 0.5
 *           + owner+0x3f4
 *   POS_Z = owner+0x3f8
 *   ret = state_machine(slot)
 *   if (ret != 0 && type == 0x13):
 *     owner+0xb90 = anim_drive (DAT_06a46f94)
 *     owner+0xb94 = 0x1e
 */
static void body_motion_anchor_keep(int i, int32_t type)
{
    void *owner = slot_owner_a(i);
    if (!owner) {
        /* Engine reads owner+0x428 via this pointer; NULL owner would crash
         * retail.  In our port we keep the slot alive (no kill) since the
         * smoke flag's owner is a static blob and always non-NULL. */
        return;
    }

    /* asm 0x43dc37-0x43dc41: gate on owner+0x428 == 1, else kill. */
    if (owner_read_i(owner, 0x428) != 1) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* asm 0x43dc27-0x43dc32: motion_idx = owner+0x424. */
    int32_t motion_idx = owner_read_i(owner, 0x424);
    /* Clamp to valid table range; engine reads unchecked (would OOB for
     * motion_idx >= 256 or < 0).  In production motion IDs are byte-sized
     * NPC enum values so OOB is unreachable; defensive clamp here keeps
     * host tests from corrupting adjacent memory if a test stages an
     * out-of-band ID. */
    if (motion_idx < 0 || motion_idx >= 256) motion_idx = 0;
    const scene1_b_motion_entry_t *entry = &g_scene1_b_motion_table[motion_idx];

    float drag;
    if (motion_idx == 0xd || motion_idx == 0xe) {
        /* asm 0x43dc46-0x43dc4e: fixed DRAG = -0.8 for these motion IDs. */
        drag = -0.8f;
    } else {
        /* asm 0x43dc50-0x43dc6e: formula. */
        float owner_abc = owner_read_f(owner, 0xabc);
        drag = ((entry->drag_base + 0.1f) - 1.5f) * owner_abc * entry->drag_mul
               - 0.3f;
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);

    /* asm 0x43dc7c-0x43dcb3: pose write from owner anchor. */
    float opx = owner_read_f(owner, 0x3f0);
    float opy = owner_read_f(owner, 0x3f4);
    float opz = owner_read_f(owner, 0x3f8);
    float owner_abc = owner_read_f(owner, 0xabc);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, opx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_abc * entry->pos_y_mul * entry->drag_mul * 0.5f + opy);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, opz);

    /* asm 0x43dcb3-0x43dcd1: state_machine + type-0x13 anim-drive write. */
    int prog = state_machine_call_ret(slot_base(i));
    if (prog != 0 && type == 0x13) {
        owner_write_i(owner, 0xb90, g_scene1_records_b_tick_anim_drive);
        owner_write_i(owner, 0xb94, 0x1e);
    }
}

/* Engine 0x43dcdb..0x43dd79 — Body 7 (types 0x11/0xc).
 *
 *   if owner+0x428 != 1: kill slot
 *   if type == 0x11: DRAG = 0.0
 *   else (0xc):      DRAG = ((entry.drag_base + 0.1 - 1.5) *
 *                            owner+0xabc * entry.drag_mul) - 0.3
 *   Same pose write as Body 6.
 *   state_machine(slot)
 *   if AGE != 7: skip kill; else kill.
 *
 * Difference from Body 6:
 *   - DRAG for type 0x11 is hard 0 (vs Body 6's motion-ID branch).
 *   - state_machine return value is NOT checked.
 *   - Always kills on AGE == 7 (Body 6 has no AGE-kill).
 */
static void body_motion_anchor_kill_7(int i, int32_t type)
{
    void *owner = slot_owner_a(i);
    if (!owner) return;

    /* asm 0x43dcfc-0x43dd02: gate on owner+0x428 == 1, else kill. */
    if (owner_read_i(owner, 0x428) != 1) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* asm 0x43dcee-0x43dcf7: motion_idx = owner+0x424. */
    int32_t motion_idx = owner_read_i(owner, 0x424);
    if (motion_idx < 0 || motion_idx >= 256) motion_idx = 0;
    const scene1_b_motion_entry_t *entry = &g_scene1_b_motion_table[motion_idx];

    float drag;
    if (type == 0x11) {
        /* asm 0x43dd04-0x43dd0b: type 0x11 → DRAG = 0. */
        drag = 0.0f;
    } else {
        /* asm 0x43dd0d-0x43dd2b: type 0xc → motion formula. */
        float owner_abc = owner_read_f(owner, 0xabc);
        drag = ((entry->drag_base + 0.1f) - 1.5f) * owner_abc * entry->drag_mul
               - 0.3f;
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);

    /* asm 0x43dd31-0x43dd68: pose write. */
    float opx = owner_read_f(owner, 0x3f0);
    float opy = owner_read_f(owner, 0x3f4);
    float opz = owner_read_f(owner, 0x3f8);
    float owner_abc = owner_read_f(owner, 0xabc);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, opx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_abc * entry->pos_y_mul * entry->drag_mul * 0.5f + opy);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, opz);

    /* asm 0x43dd68: state_machine; return ignored. */
    state_machine_call(slot_base(i));

    /* asm 0x43dd6d-0x43dd77: kill on AGE == 7 only.  Engine inverts:
     *   if (AGE != 7) goto LAB_0043dd79; *piVar14 = 0;
     * Either branch falls through into the next-type cascade. */
    (void)type;
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 7) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.11 — Body 7a (asm 0x43dd79..0x43e22b) ═══════════════════
 * Four scattered per-type bodies — first major dispatch cluster into the
 * unported FUN_0043865e state machine (PHC #20).  All use slot[OWNER_A].
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x519314 = 2.0    0x5198d0 = 2.5    0x5198e0 = 1.5    0x51939c = 4.0
 *   0x519438 = 3.0    0x51935c = 0.5    0x519a20 = 3.5    0x519c18 = 1.15
 *   0x519c1c = 8.5    0x5196b0 = 6.5
 */

/* Engine 0x43dd96..0x43deb1 — type 0x46 body (overlay cascade).
 *
 *   if AGE == 1:
 *     scene1_overlay_spawn(NULL, POS_X, POS_Y, POS_Z, 0x44, 2.5, -1, 0)
 *   if AGE == 0x28:
 *     scene1_overlay_spawn(NULL, POS_X, POS_Y+4.0, POS_Z, 0x42, 2.0, -1, 0)
 *     scene1_overlay_spawn(NULL, POS_X, POS_Y,     POS_Z, 0x43, 1.5, -1, 0)
 *     scene1_overlay_spawn(NULL, POS_X, POS_Y,     POS_Z, 0x45, 1.5, -1, 0)
 *     se_play(0x2a3)
 *   DRAG = 3.0
 *   if AGE in [0x28, 0x30): state_machine
 *   if AGE == 0x3c: kill
 */
static void body_0x46(int i)
{
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);

    if (age == 1) {
        overlay_spawn(NULL, px, py, pz, 0x44, 2.5f, -1, 0, 0, 0);
    }
    if (age == 0x28) {
        overlay_spawn(NULL, px, py + 4.0f, pz, 0x42, 2.0f, -1, 0, 0, 0);
        overlay_spawn(NULL, px, py,        pz, 0x43, 1.5f, -1, 0, 0, 0);
        overlay_spawn(NULL, px, py,        pz, 0x45, 1.5f, -1, 0, 0, 0);
        se_play(0x2a3);
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 3.0f);

    if (age >= 0x28 && age < 0x30) {
        state_machine_call(slot_base(i));
    }
    if (age == 0x3c) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43debd..0x43df16 — type 0x97 body (overlay emit, kill-on-old).
 *
 *   DRAG = 1.0; state_machine.
 *   if AGE % 2 == 1:
 *     scene1_overlay_spawn(OWNER_A, POS_X, 0.0, POS_Z, 0x56, 1.0, -1, 0)
 *   if AGE >= 800 (0x320): kill
 *
 * Note: scene1_overlay_spawn's owner arg is `slot[+0x10]` = SCENE1_
 * RECORDS_B_OFF_OWNER_A (entity allocator owner pointer).  Engine
 * passes the dword value verbatim; we surface as void* (engine treats
 * NULL same as non-NULL for the overlay system).
 */
static void body_0x97(int i)
{
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.0f);
    state_machine_call(slot_base(i));

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if ((age & 1) == 1) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        void *owner_a = slot_owner_a(i);
        overlay_spawn(owner_a, px, 0.0f, pz,
                             0x56, 1.0f, -1, 0, 0, 0);
    }
    if (age >= 0x320) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43df1b..0x43e108 — types 0xe / 0x12 body (motion-id sub-
 * dispatch).  Reads owner+0x424 to select sub-behavior.
 *
 * Eight sub-branches by motion_idx ∈ {0x31, 0xf, 0x25/0x26/0x27/0x28,
 * 0x3d/0x3e/0x3f/0x40/0x41/0x42, 0x46/0x47, 0x44/0x45, 0x43, 0x18/0x3b/
 * 0x3c, else}.  Each sets DRAG (per-sub formula) then calls SM then
 * tests AGE for a per-sub kill threshold.
 */
static void body_0xe_or_0x12(int i)
{
    void *owner = slot_owner_a(i);
    if (!owner) return;

    int32_t motion = owner_read_i(owner, 0x424);
    int     age    = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    int32_t *slot  = slot_base(i);

    switch (motion) {
    case 0x31: {
        /* asm 0x43df37-0x43df51: DRAG = 3.0; SM; kill AGE >= 8. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 3.0f);
        state_machine_call(slot);
        if (age >= 8) scene1_records_b_tick_kill_slot(i);
        return;
    }
    case 0xf: {
        /* asm 0x43df5b-0x43df70: DRAG = 1.0; SM; kill AGE >= 0 (always). */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.0f);
        state_machine_call(slot);
        if (age >= 0) scene1_records_b_tick_kill_slot(i);
        return;
    }
    case 0x25: case 0x26: case 0x27: case 0x28: {
        /* asm 0x43e12d-0x43e158: DRAG = 4.0; up-to-20 SM iter loop with
         * SM-return==0 early break; kill AGE >= 0 (always). */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 4.0f);
        for (int n = 0; n < 20; n++) {
            if (!state_machine_call_ret(slot)) break;
        }
        if (age >= 0) scene1_records_b_tick_kill_slot(i);
        return;
    }
    case 0x3d: case 0x3e: case 0x3f:
    case 0x40: case 0x41: case 0x42: {
        /* asm 0x43e111-0x43e12b: DRAG = 3.5; SM; kill AGE >= 5. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 3.5f);
        state_machine_call(slot);
        if (age >= 5) scene1_records_b_tick_kill_slot(i);
        return;
    }
    case 0x46: case 0x47: {
        /* asm 0x43e10d-0x43e108: DRAG = 1.0; SM; kill AGE >= 1 (always). */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.0f);
        state_machine_call(slot);
        if (age >= 1) scene1_records_b_tick_kill_slot(i);
        return;
    }
    case 0x44: case 0x45: {
        /* asm 0x43e0e0-0x43e108: DRAG = motion_table[motion].drag_mul *
         * 1.15; SM; kill AGE >= 1 (always). */
        int32_t mi = motion;
        if (mi < 0 || mi >= 256) mi = 0;
        float drag = g_scene1_b_motion_table[mi].drag_mul * 1.15f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);
        state_machine_call(slot);
        if (age >= 1) scene1_records_b_tick_kill_slot(i);
        return;
    }
    case 0x43: {
        /* asm 0x43dffc-0x43e08e: DRAG = 3.5; pose around owner; SM;
         * kill AGE == 0x3c. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 3.5f);
        float bend = owner_read_f(owner, 0x420);
        float opx  = owner_read_f(owner, 0x3f0);
        float opy  = owner_read_f(owner, 0x3f4);
        float opz  = owner_read_f(owner, 0x3f8);
        float s    = sinf(bend);
        float c    = cosf(bend);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, s + s + opx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, opy + 2.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, c + c + opz);
        state_machine_call(slot);
        if (age == 0x3c) scene1_records_b_tick_kill_slot(i);
        return;
    }
    case 0x18: case 0x3b: case 0x3c: {
        /* asm 0x43e0aa-0x43e0db: DRAG = owner+0xa58 < 100 ? 6.5 : 8.5;
         * SM; kill AGE == 0xf. */
        int32_t owner_a58 = owner_read_i(owner, 0xa58);
        float drag = (owner_a58 < 100) ? 6.5f : 8.5f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);
        state_machine_call(slot);
        if (age == 0xf) scene1_records_b_tick_kill_slot(i);
        return;
    }
    default: {
        /* asm 0x43e0a2-0x43e0db (default else): DRAG = 2.0; SM; kill
         * AGE == 0xf. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 2.0f);
        state_machine_call(slot);
        if (age == 0xf) scene1_records_b_tick_kill_slot(i);
        return;
    }
    }
}

/* Engine 0x43e15d..0x43e226 — types 0xd / 0x15 body (pose around owner).
 *
 *   DRAG = 0.5 (type 0xd; or type 0x15 with motion != 0x19)
 *   DRAG = 0.0 (type 0x15 with motion == 0x19)
 *   bend = owner+0x420
 *   POS_X = 2*sin(bend) + owner+0x3f0
 *   POS_Y = owner+0x3f4 + 2.0
 *   POS_Z = 2*cos(bend) + owner+0x3f8
 *   SM gate:
 *     type 0xd : AGE in [5, 9)
 *     type 0x15: AGE in [0, 0xf)
 *   Kill AGE == 0x28.
 */
static void body_0xd_or_0x15(int i, int32_t type)
{
    void *owner = slot_owner_a(i);
    if (!owner) return;

    float drag = 0.5f;
    if (type == 0x15 && owner_read_i(owner, 0x424) == 0x19) {
        drag = 0.0f;
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);

    float bend = owner_read_f(owner, 0x420);
    float opx  = owner_read_f(owner, 0x3f0);
    float opy  = owner_read_f(owner, 0x3f4);
    float opz  = owner_read_f(owner, 0x3f8);
    float s    = sinf(bend);
    float c    = cosf(bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, s + s + opx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, opy + 2.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, c + c + opz);

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    int age_lo, age_hi;
    if (type == 0x15) {
        age_lo = 0;    age_hi = 0xf;
    } else {
        age_lo = 5;    age_hi = 9;
    }
    if (age >= age_lo && age < age_hi) {
        state_machine_call(slot_base(i));
    }
    if (age == 0x28) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.12 — Body 7b head (0xf-motion, 0x9b, 0x24) ═══════════════
 * Engine asm 0x43e22b..0x43e5d0.  Three per-type bodies that follow Body
 * 7a in the outer dispatch cascade.
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x5198e0 = 1.5   0x51935c = 0.5   0x519364 = 1.0   0x5194f0 = 10.0
 *   0x5194ec = 0.3   0x5194e4 = 15.0  0x51969c = 0.6   0x519a18 = -π/2
 *   0x519c28 = π/40  0x519434 = π/2   0x519320 = 0.0   0x519a20 = 3.5
 */

/* Engine 0x43e22b..0x43e2ed — type 0xf body (wide-followup walker arm).
 *
 *   Gate: owner+0x424 in {0x18, 0x3b, 0x3c}; else no-op.
 *   DRAG = 1.5
 *   POS_X = 0.5 * sin(owner+0x420) + owner+0x3f0
 *   POS_Y = owner+0x3f4 + 1.0
 *   POS_Z = 0.5 * cos(owner+0x420) + owner+0x3f8
 *   state_machine
 *   kill on AGE >= 1 (slot dies on first tick after pose write).
 */
static void body_0xf_motion_walker(int i)
{
    void *owner = slot_owner_a(i);
    if (!owner) return;

    int32_t motion = owner_read_i(owner, 0x424);
    if (motion != 0x18 && motion != 0x3b && motion != 0x3c) return;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.5f);

    float bend = owner_read_f(owner, 0x420);
    float opx  = owner_read_f(owner, 0x3f0);
    float opy  = owner_read_f(owner, 0x3f4);
    float opz  = owner_read_f(owner, 0x3f8);
    float s    = sinf(bend);
    float c    = cosf(bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, s * 0.5f + opx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, opy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, c * 0.5f + opz);

    state_machine_call(slot_base(i));

    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) >= 1) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43e2ed..0x43e5ac — type 0x9b body (NPC big animation cluster).
 *
 *   local_c = max(15.0 - AGE*0.3, LIFE_MULT*2)
 *   if AGE >= 365: local_c = (AGE-365)*0.6 + LIFE_MULT*2
 *
 *   ROT_SCR = -π/2
 *   if AGE >= 36: ROT_SCR = clamp_max((AGE-36)*π/40 - π/2, 0.0)
 *
 *   pose:
 *     POS_X = owner+0x20 - 1.5*sin(ROT_X)*LIFE_MULT
 *     POS_Y = owner+0x24 + local_c
 *     POS_Z = owner+0x28 - 1.5*cos(ROT_X)*LIFE_MULT
 *
 *   spawn pre-compute:
 *     l28 = 3.5*sin(ROT_X)*LIFE_MULT
 *     l2c = 2*LIFE_MULT
 *     l18 = 3.5*cos(ROT_X)*LIFE_MULT
 *
 *   AGE in [123, 365):
 *     overlay_spawn(OWNER_A, l28, l2c, l18, 0x6a, LIFE_MULT, -1, 0, 0, 1)
 *     overlay_spawn(OWNER_A, l28, l2c, l18, 0x6e, LIFE_MULT, -1, 0, 0, 1)
 *     if AGE % 3 == 0:
 *       overlay_spawn(OWNER_A, l28, l2c, l18, 0x6f, LIFE_MULT, -1, 0, 0, 1)
 *
 *   if AGE == 200: scene1_record_b_spawn_npc(OWNER_A, 0x9d, -1)
 *   if AGE == 130: se_play(0x2c2)
 *   if AGE == 390: kill
 *   if owner+0xcf8 != 0: kill
 */
static void body_0x9b(int i)
{
    void *owner = slot_owner_a(i);
    if (!owner) return;

    int   age       = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    float life_mult = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
    float rot_x     = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);

    /* asm 0x43e303-0x43e35b: local_c sliding scale. */
    float local_c   = 15.0f - (float)age * 0.3f;
    float lm_double = life_mult + life_mult;
    if (local_c < lm_double) local_c = lm_double;
    if (age >= 0x16d) {
        local_c = (float)(age - 0x16d) * 0.6f + lm_double;
    }

    /* asm 0x43e35e-0x43e3ac: ROT_SCR. */
    float rot_scr = -1.5707964f;
    if (age >= 0x24) {
        float val = (float)(age - 0x24) * 0.07853982f - 1.5707964f;
        if (val > 0.0f) val = 0.0f;
        rot_scr = val;
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, rot_scr);

    /* asm 0x43e3b2-0x43e427: pose. */
    float s_rot = sinf(rot_x);
    float c_rot = cosf(rot_x);
    float opx = owner_read_f(owner, 0x20);
    float opy = owner_read_f(owner, 0x24);
    float opz = owner_read_f(owner, 0x28);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, opx - s_rot * life_mult * 1.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, opy + local_c);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, opz - c_rot * life_mult * 1.5f);

    /* asm 0x43e42a-0x43e495: spawn precompute. */
    float l28 = s_rot * life_mult * 3.5f;
    float l2c = lm_double;
    float l18 = c_rot * life_mult * 3.5f;

    /* asm 0x43e498-0x43e552: overlay spawn cascade for AGE in [123, 365). */
    if (age >= 0x7b && age < 0x16d) {
        overlay_spawn(owner, l28, l2c, l18, 0x6a, life_mult, -1, 0, 0, 1);
        overlay_spawn(owner, l28, l2c, l18, 0x6e, life_mult, -1, 0, 0, 1);
        if ((age % 3) == 0) {
            overlay_spawn(owner, l28, l2c, l18, 0x6f, life_mult, -1, 0, 0, 1);
        }
    }

    /* asm 0x43e555-0x43e570: AGE == 200 → entity spawn 0x9d.
     * Engine calls FUN_0044376a (entity allocator), NOT FUN_00445a8c (NPC). */
    if (age == 0xc8) {
        scene1_record_b_spawn_entity(owner, 0x9d, -1);
    }
    /* asm 0x43e573-0x43e589: AGE == 130 → SE. */
    if (age == 0x82) {
        se_play(0x2c2);
    }
    /* asm 0x43e58a-0x43e596: AGE == 390 → kill. */
    if (age == 0x186) {
        scene1_records_b_tick_kill_slot(i);
    }
    /* asm 0x43e598-0x43e5a7: owner+0xcf8 != 0 → kill. */
    if (owner_read_i(owner, 0xcf8) != 0) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* Engine 0x43e5ac..0x43e5d0 — type 0x24 body.
 *
 *   DRAG = 10.0
 *   state_machine
 *   kill on AGE == 10  (LAB_440dbd: jne 0x440dc1 — i.e. AGE != 10 skip kill).
 */
static void body_0x24(int i)
{
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 10.0f);
    state_machine_call(slot_base(i));
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 10) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.13 — type 0x53 (drift-damping body) ═════════════════════
 * Engine asm 0x43e5d0..0x43e755.  Largest single-type body in the
 * integrator at 0x185 bytes.  Two-phase LIFE_MULT life curve, velocity
 * damping, and SM-driven mid-life.
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x5198c8 = 0.005   0x5198f4 = 0.001   0x519940 = 0.015
 *   0x5198c4 = 0.04    0x5198dc = 0.02    0x5196ac = 1.9
 *   0x5198a4 = 0.92    0x519364 = 1.0     0x519320 = 0.0
 */

/* Engine 0x43e5d0..0x43e755 — type 0x53 body.
 *
 *   kill_age = (FLAG_A in {0, 3} && aux_4319d6() == 1) ? 120 : 600
 *   LIFE_MULT = 0.005
 *   if AGE >= 45:
 *     LIFE_MULT = clamp_max(0.005 + (AGE-45)*0.001, 0.015) *
 *                 (1.0 + 0.02 * sin((AGE-45)*0.04))
 *   if AGE >= kill_age-45:
 *     LIFE_MULT = clamp_min(0.015 - (AGE-(kill_age-45))*0.001, 0.0)
 *   if AGE < 30:  VEL_X *= 0.92; VEL_Z *= 0.92
 *   if AGE <= 45: VEL_X = 0;     VEL_Z = 0
 *   if AGE > 45 && AGE < kill_age-45:
 *     DRAG = LIFE_MULT * 1.9 / 0.015
 *     state_machine
 *   kill on AGE == kill_age (LAB_43f0f8 → jl skip; else jmp 0x43f73a kill).
 */
static void body_0x53(int i)
{
    int   age   = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    int32_t fa  = slot_get_i(i, SCENE1_RECORDS_B_OFF_FLAG_A);

    /* asm 0x43e5d9-0x43e5f6: kill_age dispatch.
     * Engine: ebx = 0x258 (600).  If FLAG_A in {0, 3} AND aux_4319d6
     * returns 1, ebx = 0x78 (120). */
    int kill_age = 0x258;
    if (fa == 0 || fa == 3) {
        if (aux_4319d6_call() == 1) {
            kill_age = 0x78;
        }
    }

    /* asm 0x43e5f7-0x43e699: LIFE_MULT life curve.  edi = 45. */
    float life_mult = 0.005f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
    if (age >= 45) {
        /* Ramp 0.005 → 0.015 over AGE 45..55, then sin-modulated. */
        float lm_ramp = 0.005f + (float)(age - 45) * 0.001f;
        if (lm_ramp > 0.015f) lm_ramp = 0.015f;
        /* asm 0x43e655-0x43e699: sin modulation around peak. */
        float ang = (float)(age - 45) * 0.04f;
        float mod = sinf(ang) * 0.02f + 1.0f;
        life_mult = lm_ramp * mod;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
    }

    /* asm 0x43e69f-0x43e6e8: late-life ramp down (AGE >= kill_age-45). */
    int ramp_down_threshold = kill_age - 45;
    if (age >= ramp_down_threshold) {
        float lm_down = 0.015f
                        - (float)(age - ramp_down_threshold) * 0.001f;
        if (lm_down < 0.0f) lm_down = 0.0f;
        life_mult = lm_down;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life_mult);
    }

    /* asm 0x43e6ea-0x43e708: AGE < 30 → vel damping. */
    if (age < 0x1e) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X,
                   slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X) * 0.92f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z,
                   slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z) * 0.92f);
    }
    /* asm 0x43e70b-0x43e71d: AGE > 45 → zero vel.x/z.  Engine cmp is
     * `cmp AGE, edi (=45); jle skip` — i.e. skip when AGE <= 45; zero
     * when AGE > 45.  Combined with the AGE<30 damping above: AGE<30
     * damps, AGE in [30, 45] no-op, AGE > 45 zeros.  (At AGE>45 the
     * vel is wiped before the SM phase calls into the body.) */
    if (age > 45) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);
    }

    /* asm 0x43e71d-0x43e749: AGE in (45, kill_age-45) → DRAG + SM. */
    if (age > 45 && age < ramp_down_threshold) {
        float drag = life_mult * 1.9f / 0.015f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);
        state_machine_call(slot_base(i));
    }

    /* asm 0x43e74a-0x43e750 → LAB_43f0f8 → jl skip; jmp 0x43f73a (kill).
     * Equivalent: if AGE >= kill_age then kill.  Engine uses `cmp [esi
     * +0x98], ebx` (sets flags); shared LAB_43f0f8 does `jl 0x440dc1`
     * (skip kill if AGE < ebx); else falls through to LAB_43f73a which
     * does `mov [esi], 0` (kill).  Engine semantics: AGE >= kill_age
     * triggers kill — collapsed to AGE == kill_age in normal operation
     * since the body fires once per tick and AGE increments by 1. */
    if (age >= kill_age) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.14 — type 0x58 / 0x66 shared body (anchor rotor) ════════
 *
 * Engine asm 0x440bb3..0x440db4.  Decomp L38341-L38405 (FUN_0043ae20
 * cascade).  Shared body for types 0x58 and 0x66 — NPC-anchored rotor
 * with optional radial shift for 0x66 + compass dispatch via owner+0x948.
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x519b90 = 1.3      (slot[DRAG] init + POS_Y owner-anchor offset)
 *   0x5195c8 = 0.4      (AGE-clamped radial multiplier)
 *   0x519364 = 1.0      (added after AGE multiply; ALT_POS_Y offset)
 *   0x51939c = 4.0      (radius clamp ceiling)
 *   0x519434 = π/2      (0x66 sub-branch yaw bias)
 *   0x519750 = 1.6      (slot[DRAG] override for 0x66)
 *   0x519748 = 0.7      (compass +X offset for owner+0x948 in {0, 4})
 *   0x5194ec = 0.3      (compass +Z offset for owner+0x948 in {2, 6})
 *
 * Per-tick math:
 *   slot[DRAG] = 1.3
 *   r = clamp_max(AGE * 0.4 + 1.0, 4.0)
 *   pos.x = sin(ROT_X) * (r + ROT_Z) + owner_a.pose.x
 *   pos.y = owner_a.pose.y + 1.3
 *   pos.z = cos(ROT_X) * (r + ROT_Z) + owner_a.pose.z
 *
 *   if type == 0x66:
 *     pos.x += sin(ROT_X + π/2) * ROT_SCR
 *     pos.z += cos(ROT_X + π/2) * ROT_SCR
 *     slot[DRAG] = 1.6
 *
 *   compass = owner_a[+0x948]:
 *     0 or 4 → pos.x += 0.7
 *     2 or 6 → pos.z += 0.3
 *
 *   ALT_POS = (owner_a.pose.x, owner_a.pose.y + 1.0, owner_a.pose.z)
 *     (engine calls sinf/cosf(ROT_X) again here but DISCARDS the result
 *      via `fstp st(0)` at 0x440d4d/0x440d7e — pure-FPU-state noop, elided.)
 *
 *   if 6 <= AGE < 10:
 *     for n in [0..5):
 *       ret = state_machine(slot)         ; 0/1 return contract
 *       if ret == 0: break
 *
 *   kill if owner_a[+0xcf8] != 0
 *   kill if AGE == 0xe                     (asm 0x440db6: cmp AGE, 0xe;
 *                                            shared LAB_00440dbd cleanup.)
 */
static void body_0x58_or_0x66(int i, int32_t type)
{
    void *owner = slot_owner_a(i);
    if (!owner) return;

    /* asm 0x440bb3-0x440bb9: slot[DRAG] = 1.3 */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.3f);

    /* asm 0x440bbf-0x440bf1: r = clamp_max(AGE * 0.4 + 1.0, 4.0).  Engine
     * uses `fild AGE` then mul/add/fcomp — see ds:0x5195c8/364/39c. */
    int   age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    float r   = (float)age * 0.4f + 1.0f;
    if (r > 4.0f) r = 4.0f;

    /* asm 0x440bf1-0x440c22: POS_X = sin(ROT_X) * (r + ROT_Z) + owner+0x20. */
    float rot_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    float rot_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z);
    float ox    = owner_read_f(owner, 0x20);
    float oy    = owner_read_f(owner, 0x24);
    float oz    = owner_read_f(owner, 0x28);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
               sinf(rot_x) * (r + rot_z) + ox);

    /* asm 0x440c25-0x440c31: POS_Y = owner+0x24 + 1.3. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.3f);

    /* asm 0x440c34-0x440c65: POS_Z = cos(ROT_X) * (r + ROT_Z) + owner+0x28. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
               cosf(rot_x) * (r + rot_z) + oz);

    /* asm 0x440c68-0x440cd2: 0x66 sub-branch (radial shift by ROT_SCR
     * along ROT_X + π/2; override DRAG to 1.6). */
    if (type == 0x66) {
        float rot_scr = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR);
        float ang     = rot_x + 1.5707964f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                   sinf(ang) * rot_scr
                   + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                   cosf(ang) * rot_scr
                   + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.6f);
    }

    /* asm 0x440cd5-0x440d34: compass dispatch via owner+0x948 int. */
    int32_t compass = owner_read_i(owner, 0x948);
    if (compass == 0 || compass == 4) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                   slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X) + 0.7f);
    }
    if (compass == 2 || compass == 6) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                   slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z) + 0.3f);
    }

    /* asm 0x440d34-0x440d83: ALT_POS = owner pose + (0, 1.0, 0).  The
     * sin/cos(ROT_X) calls at 0x440d45/0x440d73 have their results
     * dropped via `fstp st(0)` — pure-FPU-state noop, elided. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, oz);

    /* asm 0x440d86-0x440da7: 5-iter SM loop if 6 <= AGE < 10. */
    if (age >= 6 && age < 10) {
        for (int n = 0; n < 5; n++) {
            if (state_machine_call_ret(slot_base(i)) == 0) break;
        }
    }

    /* asm 0x440da9-0x440db4: kill if owner+0xcf8 != 0. */
    if (owner_read_i(owner, 0xcf8) != 0) {
        scene1_records_b_tick_kill_slot(i);
    }

    /* asm 0x440db6-0x440dbf (shared LAB_00440dbd cleanup with this body's
     * bVar17 = (AGE == 0xe)): kill if AGE == 0xe. */
    if (age == 0xe) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15a — three trivial tail bodies ═════════════════════════
 *
 * Decomp L39122 / L39129 / L38702 (FUN_0043ae20 tail cascade).  All
 * three bodies are driven by the existing SM hook with no new hooks:
 *   - type 0x33  (asm 0x43fd99..0x43fdcd): ROT_Z+=0.05 spin + DRAG from
 *                LIFE_MULT*3 + SM + kill AGE==0x100
 *   - type 0x60  (asm 0x43fdd0..0x43fe14): pose-snap to owner_a + DRAG=8
 *                + SM + kill AGE==5
 *   - type 0x65  (asm 0x43f34f..0x43f3a6): late-AGE vertical drift damper
 *                with VEL_Y floor and kill AGE==0x78
 *
 * The LAB_00440dc1 default-tail body is now ported as body_lab_00440dc1
 * (C8j-tick.16) and wired as the dispatch `default:` arm.  All named
 * tail types {0x75/0x83/0x84/0xa0..0xa6 etc} are landed, so the default
 * arm is now safe to wire (no risk of mis-routing).  See the body's
 * docstring near body_lab_00440dc1 for the asm trace and gate semantics.
 *
 * Constants verified via `tools/analyze/pe.py bytes <va> 4`:
 *   0x5198f8 = 0.05       (type 0x33 ROT_Z step; also type 0x65 VEL_Y step)
 *   0x519438 = 3.0        (type 0x33 DRAG multiplier)
 *   0x51935c = 0.5        (type 0x60 POS_Y bias)
 *   0x519378 = 8.0        (type 0x60 DRAG)
 *   0x5198b0 = 0.99       (type 0x65 VEL_Y damp)
 *   0x519c2c = -0.5       (type 0x65 VEL_Y floor)
 */

static void body_0x33(int i)
{
    /* asm 0x43fd9e-0x43fdcd: ROT_Z += 0.05 (fadd ds:0x5198f8); DRAG =
     * LIFE_MULT * 3 (fld [esi+0x108]; fmul ds:0x519438); SM();
     * jmp 0x440298 → kill on AGE == 0x100 shared tail. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z) + 0.05f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,
               slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT) * 3.0f);
    state_machine_call(slot_base(i));
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x100) {
        scene1_records_b_tick_kill_slot(i);
    }
}

static void body_0x60(int i)
{
    /* asm 0x43fdd5-0x43fe14: read owner via [esi+0x10] (= OWNER_A slot
     * field), copy pose +0x20/+0x24/+0x28 into POS_X..Z with +0.5 on Y
     * (fadd ds:0x51935c); DRAG = 8.0 (fld ds:0x519378); SM();
     * jmp 0x4402a2 → bVar17 = (AGE == 5); shared kill at 0x440dbd. */
    void *owner = slot_owner_a(i);
    if (!owner) return;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x20));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x24) + 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x28));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 8.0f);
    state_machine_call(slot_base(i));
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 5) {
        scene1_records_b_tick_kill_slot(i);
    }
}

static void body_0x65(int i)
{
    /* asm 0x43f354-0x43f3a6: AGE > 0x1e → VEL_Y = (VEL_Y - 0.05) * 0.99
     * (fsub ds:0x5198f8; fmul ds:0x5198b0); if VEL_Y < -0.5 (fcomp
     * ds:0x519c2c; jae skip): VEL_Y = -0.5 (fld ds:0x519c2c); call
     * 0x43865e; jne 0x4411e3 (SM ret != 0 → kill).  Shared LAB_0043f39b
     * tail: jmp 0x440dbd kill on AGE == 0x78. */
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age > 0x1e) {
        float vy = (slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.05f) * 0.99f;
        if (vy < -0.5f) vy = -0.5f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy);
        if (state_machine_call_ret(slot_base(i)) != 0) {
            scene1_records_b_tick_kill_slot(i);
            return;
        }
    }
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x78) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15b — types 0x5f / 0x3e (shared) + 0x82 ═════════════════
 *
 * Decomp L39139 / L39287 / L39155 / L39300.  All three bodies are
 * owner_a-anchored, driven by the existing SM hook.  Types 0x5f and 0x3e
 * literally share their tail in the engine: 0x3e's body jumps to
 * 0x43fe91 (= mid-0x5f) to reuse SHAPE_GUARD write + SM + owner+0xe90
 * gate + kill on AGE == 0x19.
 *
 *   - type 0x5f  (asm 0x43fe15..0x43febd): POS = owner+(2*sin(yaw),
 *                  1.2, 2*cos(yaw)); SHAPE_GUARD=1.5.  Yaw read from
 *                  owner+0xea4 (engine NPC yaw field).
 *   - type 0x3e  (asm 0x43ff7c..0x440000): POS = owner+(3*sin(yaw),
 *                  1.5, 3*cos(yaw)); SHAPE_GUARD=2.0.  Same yaw source.
 *                  Tail JMPs into 0x5f at 0x43fe91 to reuse SM + kill.
 *   - shared post (asm 0x43fe8b..0x43febd): SHAPE_GUARD; SM(); kill if
 *                  owner+0xe90 not in [4..7]; kill on AGE == 0x19.
 *   - type 0x82  (asm 0x43fec2..0x43ff7b): AGE == 1 → ALT_POS = owner+
 *                  (0, 1.5, 0) (anchor snapshot).  Every tick: POS =
 *                  owner+(0, 1.5, 0), SHAPE_GUARD = 2.0.  AGE == 20 → run
 *                  20-iter inner loop calling SM, after each iter
 *                  POS_X += (ALT_POS_X - POS_X) * 0.05, POS_Z += dz too.
 *                  Kill on AGE == 0x23.  No owner+0xe90 gate.
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x519924 = 1.2        (0x5f POS_Y bias)
 *   0x5198e0 = 1.5        (0x5f SHAPE_GUARD; 0x82 ALT_POS_Y/POS_Y bias;
 *                          0x3e POS_Y bias)
 *   0x519314 = 2.0        (0x82 + 0x3e SHAPE_GUARD)
 *   0x519438 = 3.0        (0x3e scale — same .rdata word as 0x33's
 *                          DRAG=LIFE_MULT*3 multiplier)
 *   0x5198f8 = 0.05       (0x82 inner-loop lerp factor — same .rdata
 *                          word as 0x33's ROT_Z step and 0x65's VEL_Y
 *                          step)
 */

/* Shared post-body for types 0x5f and 0x3e.  Stores SHAPE_GUARD, runs
 * SM, then applies the owner-anim-state range gate (kill if owner+0xe90
 * not in [4..7]) and the AGE == 0x19 kill check from the shared
 * LAB_004402a2 tail. */
static void shared_5f_3e_tail(int i, const void *owner, float shape_guard)
{
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, shape_guard);
    state_machine_call(slot_base(i));

    /* asm 0x43fea3-0x43feb5: read owner+0xe90 → kill slot if value < 4
     * or > 7 (i.e. anim state is not in [4..7]). */
    int32_t anim_state = owner_read_i(owner, 0xe90);
    if (anim_state < 4 || anim_state > 7) {
        scene1_records_b_tick_kill_slot(i);
    }

    /* asm 0x43feb6-0x43febd shared LAB_004402a2 tail:
     *   bVar17 = (AGE == 0x19); if bVar17: *piVar14 = 0. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x19) {
        scene1_records_b_tick_kill_slot(i);
    }
}

static void body_0x5f_or_0x3e(int i, int32_t type)
{
    void *owner = slot_owner_a(i);
    if (!owner) return;

    /* asm 0x43fe1e-0x43fe88 (0x5f) / 0x43ff85-0x43fff7 (0x3e):
     * yaw = owner+0xea4 (NPC bend angle); POS = owner.pose +
     * scale*(sin(yaw), 0, cos(yaw)) with per-type +Y bias.
     *
     * 0x5f uses `fadd st(0),st` to double (scale = 2.0); 0x3e uses
     * `fmul ds:0x519438` (scale = 3.0). */
    float yaw = owner_read_f(owner, 0xea4);
    float ox  = owner_read_f(owner, 0x20);
    float oy  = owner_read_f(owner, 0x24);
    float oz  = owner_read_f(owner, 0x28);

    float scale, y_bias, shape_guard;
    if (type == 0x5f) {
        scale = 2.0f; y_bias = 1.2f; shape_guard = 1.5f;
    } else {
        /* type 0x3e */
        scale = 3.0f; y_bias = 1.5f; shape_guard = 2.0f;
    }

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sinf(yaw) * scale + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + y_bias);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cosf(yaw) * scale + oz);

    shared_5f_3e_tail(i, owner, shape_guard);
}

static void body_0x82(int i)
{
    /* asm 0x43fec2-0x43ff7b.  Owner-anchored snap-to-pose with one-shot
     * ALT_POS recording at AGE==1 and a 20-iter SM-driven lerp at
     * AGE==20.  No owner+0xe90 kill gate.  Kill on AGE == 0x23. */
    void *owner = slot_owner_a(i);
    if (!owner) return;

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    /* asm 0x43fecd-0x43fef4: AGE == 1 → ALT_POS = owner+(0, 1.5, 0).
     * Records the initial anchor pose to lerp back toward at AGE==20. */
    if (age == 1) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, owner_read_f(owner, 0x20));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y,
                   owner_read_f(owner, 0x24) + 1.5f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, owner_read_f(owner, 0x28));
    }

    /* asm 0x43fef7-0x43ff29: SHAPE_GUARD = 2.0; POS = owner+(0, 1.5, 0). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 2.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x20));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x24) + 1.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x28));

    /* asm 0x43ff2a-0x43ff6e: AGE == 0x14 → 20-iter SM-driven X/Z lerp
     * toward ALT_POS at 0.05/iter.  dx/dz are loaded ONCE before the
     * loop from the current (ALT - POS) deltas — they don't update as
     * POS marches, so net displacement = iter_count * delta * 0.05 =
     * 20 * delta * 0.05 = delta.  POS ends at ALT_POS_X / ALT_POS_Z
     * after the loop (geometric reading of the asm). */
    if (age == 0x14) {
        float dx = (slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X)
                    - slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X)) * 0.05f;
        float dz = (slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z)
                    - slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z)) * 0.05f;
        for (int n = 0; n < 0x14; n++) {
            state_machine_call(slot_base(i));
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                       slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X) + dx);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                       slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z) + dz);
        }
    }

    /* asm 0x43ff70-0x43ff7b shared LAB_004402a2 tail: kill on AGE == 0x23. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x23) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15c — types 0x7b / 0xa1 / 0xa4 shared ground-bounce body ═══
 *
 * Engine asm 0x44074d..0x4408a1 / decomp L39400-L39446 (cite L2548-2594 of
 * docs/decompiled/by-address/43ae20.c).  All three types share one body;
 * entry points at 0x44074d (0x7b: sets ebx = 0xa4 then falls through) and
 * 0x440752 (0xa1/0xa4 entry).  Two-phase body driven by `bounce_count` at
 * slot[PART_IDX] (dw 39 = +0x9c, engine `&DAT_0693254c`):
 *
 *   Phase 0  (bounce_count == 0): in-flight + ground impact
 *     VEL_Y -= 0.01           ; gravity-like accel
 *     if VEL_Y < 0:
 *       ground_y = 0
 *       if type != 0xa4:
 *         if ground_query(POS_X, POS_Y, POS_Z): ground_y = hit
 *       if POS_Y <= ground_y + 0.3:
 *         POS_Y = ground_y + 1.0
 *         VEL = (0, 0, 0)
 *         notify_queue(10, 4, 4, 1.0)
 *         se(0x148)
 *         se(0x2a5)                       ; raw-asm verified, Ghidra dropped arg
 *         scale = 1.0 / 1.5 (0xa1) / 4.0 (0xa4)
 *         scene1_pfo_table_a_alloc_passthrough(0, POS_X, 0, POS_Z, 5,
 *                                              scale, -1, 0, 0)
 *         bounce_count++
 *     g_scene1_records_b_tick_flag = 1   ; DAT_06a46f98
 *
 *   Phase 1  (bounce_count != 0): post-bounce settle
 *     DRAG = 2.0 (0x7b) / 4.0 (0xa1) / 8.0 (0xa4)
 *     state_machine(slot)
 *
 *   Shared LAB_004402a2 tail: kill if AGE == 0x82, else fall through to
 *   default-tail (LAB_00440dc1, still deferred to C8j-tick.15z+).
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x5193a4 = 0.01     (VEL_Y per-tick decrement)
 *   0x519320 = 0.0      (VEL_Y < 0 sign check)
 *   0x5194ec = 0.3      (ground-impact y-threshold offset)
 *   0x519364 = 1.0      (POS_Y above ground after impact)
 *   0x5198e0 = 1.5      (0xa1 spawn scale)
 *   0x51939c = 4.0      (0xa4 spawn scale; 0xa1 settle DRAG)
 *   0x519378 = 8.0      (0xa4 settle DRAG)
 *   0x519314 = 2.0      (0x7b default settle DRAG)
 *
 * Asm corrections vs decomp (raw-asm reads in 0x44074d..0x4408a1):
 *   - Ghidra showed both FUN_00499519 calls as argless (L2565/L2566); raw
 *     asm at 0x4407f9 / 0x440803 has `push 0x148` and `push 0x2a5` — both
 *     pass specific SE IDs.
 *   - Ghidra showed FUN_0044b219(10, 4, 4) at L2564 (3 args); raw asm at
 *     0x4407e9..0x4407f4 pushes 4 args including a `fld1` (1.0) float —
 *     matches our notify_queue_call(a, b, c, d) 4-arg hook signature.
 *   - Ghidra showed FUN_0041331d 7-arg call at L2574 (`0xffffffff` last);
 *     raw asm at 0x440831..0x44085f shows the full 9-arg PFO.6 form: push
 *     0 (param_8) + fldz/store (override_rot_y_bits float bits, 0) +
 *     0xffffffff (override_dur=-1) + scale + 5 (template_id) + pos_z + 0
 *     (pos_y float) + pos_x + 0 (template_owner).
 *   - The engine's ground_query uses `&slot[+0x18]` as a 4-float scratch
 *     buffer and reads back `slot[+0x24]` (= AUX_9) for the ground_y.
 *     Our hook is collapsed to `int hook(x, y, z, *out_y)` and writes
 *     into the out_y param directly — observably equivalent here since
 *     the body's only ground_y consumer is the immediate POS_Y check.
 *
 * Dormant in HOUSE under default smoke flags — types 0x7b/0xa1/0xa4 are
 * not in any landed C8j allocator's type set, but `--force-b-npc 0x7b`
 * / `0xa1` / `0xa4` exercises the falling + ground impact + PFO Table A
 * spawn chain end-to-end.  No new hooks needed (ground_query / notify_
 * queue / SE / table_a_alloc_passthrough all wired in earlier chips).
 */
static void body_0x7b_a1_a4(int i, int32_t type)
{
    int32_t bounce_count = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);

    if (bounce_count == 0) {
        /* Phase 0 — falling.  VEL_Y -= 0.01 each tick; once VEL_Y goes
         * negative, the ground-impact check arms. */
        float vel_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.01f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vel_y);

        if (vel_y < 0.0f) {
            /* asm 0x440783-0x4407b5: type==0xa4 skips ground_query entirely
             * (always uses ground_y=0); other types query terrain. */
            float ground_y = 0.0f;
            if (type != 0xa4) {
                float px  = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
                float py  = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
                float pz  = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
                float hit = 0.0f;
                if (ground_query(px, py, pz, &hit) == 1) {
                    ground_y = hit;
                }
            }

            float pos_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            if (pos_y <= ground_y + 0.3f) {
                /* asm 0x4407cd-0x440862: impact — snap up by 1.0, zero
                 * velocity, notify + 2× SE + Table A spawn, bump bounce
                 * counter. */
                slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, ground_y + 1.0f);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

                notify_queue_call(10, 4, 4, 1.0f);
                se_play(0x148);
                se_play(0x2a5);

                float scale = 1.0f;
                if (type == 0xa1) scale = 1.5f;
                if (type == 0xa4) scale = 4.0f;

                float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
                float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
                scene1_pfo_table_a_alloc_passthrough(
                    /*template_owner=*/0,
                    /*pos_x=*/px,
                    /*pos_y=*/0.0f,
                    /*pos_z=*/pz,
                    /*template_id=*/5,
                    /*scale_base=*/scale,
                    /*override_dur=*/-1,
                    /*override_rot_y_bits=*/0,
                    /*param_8=*/0);

                slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX,
                           bounce_count + 1);
            }
        }

        /* asm 0x440868-0x44086f: latch per-tick flag regardless of
         * whether impact fired. */
        g_scene1_records_b_tick_flag = 1;
    } else {
        /* Phase 1 — post-bounce settle.  DRAG override + SM. */
        float drag = 2.0f;
        if (type == 0xa1) drag = 4.0f;
        if (type == 0xa4) drag = 8.0f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, drag);
        state_machine_call(slot_base(i));
    }

    /* Shared LAB_004402a2 tail at 0x4408a1 — kill on AGE == 0x82, else
     * fall through to LAB_00440dc1 default-tail wall-bounce body via
     * the engine's `jne 0x440dc1` at LAB_004402a2.  body_lab_00440dc1
     * (C8j-tick.16) gates internally on slot[AUX_C8] != 0 + per-tick
     * flag != 0; this body sets the per-tick flag at line 4013 so the
     * wall-bounce body's flag-gate is open here.  AUX_C8 must be set
     * by another body (e.g. body_0x74_or_0x79) before this body fires
     * for the wall-bounce to actually do work; otherwise body_lab_00440dc1
     * returns at its AUX_C8 gate. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x82) {
        scene1_records_b_tick_kill_slot(i);
    }
    scene1_records_b_run_lab_00440dc1(i);
}

/* ═══ C8j-tick.15d — type 0x84 single-body ground-bounce + self-kill ════
 *
 * Engine asm 0x43fc96..0x43fd97 / decomp L2713-2741.  Single-type body —
 * structurally similar to 0x7b/0xa1/0xa4 (gravity + ground impact) but
 * with three notable differences:
 *
 *   1. Impact threshold uses 0.2 (.rdata 0x5198d8), NOT 0.3 (0x5194ec).
 *   2. POS_Y on impact snaps to ground_y + 0.2 (the threshold itself),
 *      NOT ground_y + 1.0.  VEL_Y is set to -0.01 (.rdata 0x519c08);
 *      VEL_X / VEL_Z zero as usual.
 *   3. Impact KILLS the slot (`mov [esi], edi` with edi=0 at asm 0x43fd74)
 *      — no bounce_count bump.  Body never reaches its phase-1 branch in
 *      practice; that branch (POS_Y -= VEL_Y to undo preamble's gravity
 *      add) is preserved for engine fidelity only.
 *
 *   No notify_queue call; single SE (id 0x2b0); Table A spawn args differ
 *   (template_id=1, scale=0.3 constant, pos_y is the impact pos_y rather
 *   than 0.0).  AGE-300 kill via shared LAB_0043f6c8 / LAB_004402a2 tail.
 *
 *   state_machine ALWAYS runs at end of phase 0 even if impact fires (the
 *   engine has no jump skipping it after the kill).  Its return value
 *   gates: ret == 1 → second kill (idempotent on already-dead slot);
 *   ret != 1 → fall through to AGE-300 kill.
 *
 *   Phase 1 (bounce_count != 0): POS_Y -= VEL_Y, then AGE-300 kill.
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x519998 = -0.15    (DRAG, written unconditionally at body entry)
 *   0x5193a4 =  0.01    (per-tick VEL_Y decrement; shared w/ 0x7b)
 *   0x519320 =  0.0     (VEL_Y < 0 sign threshold; shared)
 *   0x5198d8 =  0.2     (impact threshold offset; UNIQUE to 0x84)
 *   0x519c08 = -0.01    (post-impact VEL_Y, downward latch)
 *   0x5194ec =  0.3     (Table A spawn scale — same .rdata word as 0x7b's
 *                        impact threshold, reused here for scale)
 *
 * Engine's ground_query fills its `&slot[+0x18]` scratch buffer and the
 * body reads back `slot[+0x24]` (= AUX_9 = dw 9) for the threshold.  Our
 * hook is collapsed to `out_y` only, so we mirror the engine's slot
 * mutation by writing slot[AUX_9] = out_y after a hit — keeps the slot's
 * post-tick AUX_9 observable-state engine-faithful.
 *
 * Dormant in HOUSE under default smoke flags — `--force-b-npc 0x84`
 * exercises the falling + ground-impact + Table A spawn + kill chain.
 */
static void body_0x84(int i)
{
    /* asm 0x43fca1-0x43fca7: DRAG = -0.15 unconditionally (both phases). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, -0.15f);

    int32_t bounce_count = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);

    if (bounce_count == 0) {
        float vel_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.01f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vel_y);

        if (vel_y < 0.0f) {
            float px  = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py  = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz  = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float hit = 0.0f;
            if (ground_query(px, py, pz, &hit) == 1) {
                /* Mirror engine: slot[AUX_9] is what the engine reads back
                 * for the threshold (it was written via the 4-float
                 * scratch buffer at &slot[+0x18]).  Our hook collapses to
                 * `out_y` only; we restore the engine's observable side-
                 * effect by writing AUX_9 explicitly. */
                slot_set_f(i, SCENE1_RECORDS_B_OFF_AUX_9, hit);
                float threshold = hit + 0.2f;
                float pos_y     = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
                if (pos_y <= threshold) {
                    /* Impact — snap to threshold (NOT +1.0 like 0x7b),
                     * set downward-latch VEL_Y, zero VEL_X/Z, SE + Table
                     * A spawn + kill self. */
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, threshold);
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, -0.01f);
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);
                    se_play(0x2b0);

                    float npx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
                    float npy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
                    float npz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
                    scene1_pfo_table_a_alloc_passthrough(
                        /*template_owner=*/0,
                        /*pos_x=*/npx,
                        /*pos_y=*/npy,
                        /*pos_z=*/npz,
                        /*template_id=*/1,
                        /*scale_base=*/0.3f,
                        /*override_dur=*/-1,
                        /*override_rot_y_bits=*/0,
                        /*param_8=*/0);

                    /* asm 0x43fd74: mov [esi], edi (edi=0) — kill slot.
                     * State machine still runs after this; the engine has
                     * no jump skipping it. */
                    scene1_records_b_tick_kill_slot(i);
                }
            }
        }

        /* asm 0x43fd76-0x43fd86: state_machine ALWAYS runs at phase-0
         * tail (whether or not impact fired).  ret==1 → kill (idempotent
         * on already-dead impact slot); ret!=1 → fall through to AGE-300
         * shared tail. */
        if (state_machine_call_ret(slot_base(i)) == 1) {
            scene1_records_b_tick_kill_slot(i);
            return;
        }
    } else {
        /* asm 0x43fd8b-0x43fd94: POS_Y -= VEL_Y (cancel preamble gravity).
         * Unreachable in normal flow since impact kills the slot before
         * bounce_count is ever non-zero; ported for engine fidelity. */
        float pos_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float vel_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, pos_y - vel_y);
    }

    /* LAB_0043f6c8 / shared LAB_004402a2 tail — kill on AGE == 300, else
     * fall through to default-tail (LAB_00440dc1, deferred). */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 300) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15e — types 0x73 / 0x78 / 0x7a shared trail-cull body ════
 *
 * Engine asm 0x4402ad..0x4405ee / decomp L2967-L2992 + L3056-L3109.  Three
 * types share a single body via LAB_004402ad (head) + LAB_004402c0 (the 0x73
 * direct entry skips one cmp) and fall through the entity-bounce shared
 * cascade tail (with no entity-bounce side-effect matches).
 *
 *   Phase 0  (AGE < 0): preamble-cancel + owner-flag exit gate
 *     POS_X -= VEL_X ; POS_Y -= VEL_Y ; POS_Z -= VEL_Z   (cancel preamble)
 *     if owner_a+0xcf8 != 0 → KILL (LAB_0043fbb9)
 *     else                → continue iter loop, no kill  (LAB_0043fbbc)
 *     [body terminates here; nothing further runs]
 *
 *   Phase 1  (AGE == 1): dual trail emit
 *     scene1_overlay_spawn(owner_a, POS-VEL, tid=0x10, 1.0, -1, 0, 0, 0)
 *     scene1_overlay_spawn(owner_a, ALT_POS, tid=0x13, 0.7, -1, 0, 0, 0)
 *
 *   Phase 2  (AGE >= 0): shared cull + state-machine + AGE-120 tail
 *     slot[DRAG] = 0.0
 *     (entity-bounce type-specific writes — none match 0x73/0x78/0x7a)
 *     if cull_query(POS_X, POS_Y) >= 0 → KILL + LAB_00440741 (AGE-78 check)
 *     else:
 *       DAT_06a46f94 = 0
 *       ret = state_machine(slot)
 *       ret == 0 → LAB_00440741 (AGE-78 check, no kill if != 0x78)
 *       ret != 0 → KILL (LAB_0043fbb9)
 *     LAB_00440741: bVar17 = AGE==0x78; LAB_004402a2 → LAB_00440dc1
 *     (default-tail body — deferred to a future chip; in the BSS-zero
 *     default state it's a no-op so we inline the kill check.)
 *
 *   The engine cascade between LAB_004402ad's tail (DRAG=0 at 0x44036a)
 *   and the cull check (0x44047a) scans the entity-bounce type list
 *   {0x4d/0x4e/0x4f/0x50/0xa5/0xa6/99/0x52/0x4d/0x56/0x96} — none match
 *   our 3 types, so the body skips through unchanged.
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x519748 = 0.7f      (second overlay_spawn scale)
 *
 * Asm corrections vs decomp (raw-asm reads in 0x4402c0..0x4405ee):
 *   - Both FUN_004147d5 (= scene1_overlay_spawn, 9-arg wrapper that
 *     appends mode=0) calls have 9 explicit args; Ghidra dropped the
 *     8th (override_rot_y_bits = 0.0f / 0).  Raw asm pushes 0xffffffff
 *     for override_dur and zeroes for the trailing slots; the engine
 *     stack cleanup is `add esp, 0x24` (= 9 dwords).
 *   - Ghidra shows `FUN_00490820(POS_X, POS_Y)` at L2945 / L3056 — only
 *     2 args.  Raw asm at 0x440495 pushes 4 args (POS_X, POS_Y, POS_Z,
 *     0.0f) before the call.  Our cull_query hook is 2-arg (POS_X/Y);
 *     the dropped Z + 0.0f args feed FUN_00490820's depth calc but the
 *     default-visible behavior (return < 0) is preserved.
 *   - DAT_06a46f94 is cleared at 0x44058d (`and ds:0x6a46f94, 0x0`) just
 *     before the state_machine call.  Mirrors the same DAT_06a46f94 alias
 *     wired in C8j-tick.4 (`g_scene1_records_b_tick_anim_drive`).
 *
 * AGE<0 owner_a NULL-guard: engine asm at 0x4402e5 dereferences slot[+0x10]
 * (= OWNER_A) without a NULL check.  Real allocator commits always set
 * OWNER_A for entity-class types, but a smoke / test slot with OWNER_A=0
 * would crash there.  Our port mirrors engine fidelity — the caller must
 * populate OWNER_A before exercising the AGE<0 branch.
 *
 * Dormant in HOUSE under default smoke flags — types 0x73/0x78/0x7a not in
 * any landed C8j allocator's type set; `--force-b-entity 0x73/0x78/0x7a`
 * exercises the trail-emit + cull + state-machine chain.  No new hooks
 * needed.
 */
static void body_0x73_or_0x78_or_0x7a(int i)
{
    int32_t age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age < 0) {
        /* asm 0x4402ca-0x4402e2: POS -= VEL — cancel the preamble's
         * gravity-free integration.  These slots haven't "started" yet
         * (AGE goes negative when an allocator initializes with a
         * spawn-delay countdown). */
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - vz);

        /* asm 0x4402e5-0x4402ef: `cmp [eax+0xcf8], 0x0 ; jmp 0x43fbb7`.
         * LAB_0043fbb7: if ZF (== 0) → advance iter (no kill); else → kill. */
        const void *owner_a = slot_owner_a(i);
        if (owner_a && owner_read_i(owner_a, 0xcf8) != 0) {
            scene1_records_b_tick_kill_slot(i);
        }
        return;
    }

    /* AGE == 1: dual trail emit.  Both overlay_spawn calls pass owner_a
     * (engine `[esi+0x10]` = slot dw 4 = OWNER_A).  asm at 0x440329 and
     * 0x44035f confirms the 1st arg is OWNER_A; Ghidra renders this as
     * `(&DAT_069324c0)[uVar6 * 0x49]` at L2981 / L2987. */
    if (age == 1) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

        const void *owner_a = slot_owner_a(i);

        /* asm 0x4402f9-0x440331: template 0x10 trail at POS-VEL. */
        overlay_spawn(owner_a, px - vx, py - vy, pz - vz,
                      /*template_id=*/0x10,
                      /*scale_base=*/1.0f,
                      /*override_dur=*/-1,
                      /*override_rot_y_bits=*/0,
                      /*shape_mode=*/0,
                      /*mode=*/0);

        /* asm 0x440334-0x440367: template 0x13 trail at ALT_POS, scale 0.7. */
        float ax = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
        float ay = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
        float az = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
        overlay_spawn(owner_a, ax, ay, az,
                      /*template_id=*/0x13,
                      /*scale_base=*/0.7f,
                      /*override_dur=*/-1,
                      /*override_rot_y_bits=*/0,
                      /*shape_mode=*/0,
                      /*mode=*/0);
    }

    /* asm 0x44036a-0x44036f: DRAG = 0 unconditionally. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);

    /* asm 0x44047a-0x44049f: cull_query.  Engine pushes 4 args (POS_X,
     * POS_Y, POS_Z, 0.0f); our 2-arg hook receives POS_X/POS_Y only — the
     * default returns -1 ("visible") so cull behavior is preserved in
     * production.  CULL (ret >= 0): kill + AGE-78 tail. */
    float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    if (cull_query(px, py) >= 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* asm 0x44058d-0x4405a3: clear DAT_06a46f94, state_machine(slot).
     * ret == 1 → 0x52-specific code (doesn't fire for our 3 types) →
     *           fall through to LAB_0043fbb9 KILL
     * ret == 0 → LAB_00440741 (AGE==0x78 check)
     * ret >= 2 → LAB_0043fbb9 KILL */
    g_scene1_records_b_tick_anim_drive = 0;
    int sm_ret = state_machine_call_ret(slot_base(i));
    if (sm_ret != 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* LAB_00440741 / LAB_004402a2 — kill on AGE == 0x78, else fall through
     * to LAB_00440dc1 default-tail (deferred). */
    if (age == 0x78) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15f — type 0x76 / 0xa3 shared crawl+pose-snap body ═══════
 *
 * Engine asm 0x440aae..0x440bae / decomp L2738-L2772.  Two types share
 * one body via the dispatch's paired `je 0x440aae`.  Type 0xa3 adds an
 * entity-pose-snap side-effect (calls scene1_record_b_spawn_npc(0x97, 1)
 * after temporarily grafting slot.POS_XZ into owner_b's +0x3f0/+0x3f8
 * pose triplet) when AGE matches a slot-index-derived value.
 *
 *   Phase A  (AGE < 0): pure preamble cancel + advance
 *     POS_X -= VEL_X ; POS_Y -= VEL_Y ; POS_Z -= VEL_Z   (cancel preamble)
 *     return                                              (no kill)
 *
 *   Phase B  (AGE >= 0): drift damp + SM gate + 0xa3 pose-snap
 *     LIFE_MULT += 0.01
 *     VEL_X     *= 0.99   (note: VEL_Y is NOT damped)
 *     VEL_Z     *= 0.99
 *     if PART_IDX == 0:
 *       ret = state_machine(slot)
 *       if ret != 0: KILL + return            (LAB_004411e3)
 *     # fall through (PART_IDX != 0, OR PART_IDX == 0 + ret == 0):
 *     if type == 0xa3 AND PART_IDX == 0 AND AGE == (slot_idx % 0xf + 0x3c):
 *       save owner_b.pose = (owner_b+0x3f0..0x3f8)
 *       owner_b.pose = (slot.POS_X, 0, slot.POS_Z)        # graft
 *       scene1_record_b_spawn_npc(owner_b, 0x97, 1)
 *       owner_b.pose = saved                              # restore
 *     if AGE == 0x5a: KILL                                 (LAB_00440dbd kill-eq)
 *     # fall through to LAB_00440dbd default-tail (deferred)
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x5193a4 = 0.01    (LIFE_MULT increment, shared with 0x84 VEL_Y dec)
 *   0x5198b0 = 0.99    (horizontal vel damp, shared with 0x65)
 *
 * Asm corrections vs decomp:
 *   - Ghidra renders `FUN_00445a8c(iVar13)` (single arg).  Raw asm at
 *     0x440b44-0x440b7f pushes 3 args: push 0x1 ; push 0x97 ; push eax
 *     (= owner_b).  cdecl → scene1_record_b_spawn_npc(owner_b, 0x97, 1).
 *
 * AGE<0 / no-owner branch differences vs 0x73/0x78/0x7a (C8j-tick.15e):
 *   - 0x73/0x78/0x7a: AGE<0 has an owner_a+0xcf8 nonzero-kill gate.
 *   - 0x76/0xa3:      AGE<0 always advances iter without kill.  Slot
 *                     stays alive until allocator-set AGE counts up.
 *
 * `local_2c` in decomp = the outer-loop slot iterator.  Engine asm at
 * 0x440b2d loads `mov eax, [ebp-0x28]` — `[ebp-0x28]` is the function's
 * slot iter variable.  Our port uses the C loop iter `i` directly.
 *
 * Owner pose-snap temp-graft mirrors body_0x3b's pattern (C8j-tick.7):
 * the NPC allocator's preamble at scene1_records_b_spawn.c L42027-42029
 * reads owner+0x3f0/+0x3f4/+0x3f8 as the spawn POS init, so the engine
 * temporarily writes slot.POS_XZ + 0 into those fields, calls the
 * allocator, then restores the originals.  The reload pattern after the
 * call (`mov eax, [esi+0x14]` re-reads OWNER_B in case spawn_npc moved
 * it; in our port the static blob layout is fixed but we preserve the
 * reload for fidelity).
 *
 * Dormant in HOUSE under default smoke flags — types 0x76/0xa3 not in any
 * landed C8j allocator's type set; `--force-b-npc 0x76/0xa3` exercises
 * the crawl + LIFE_MULT ramp + pose-snap chain.  No new hooks needed.
 */
static void body_0x76_or_0xa3(int i, int32_t type)
{
    int32_t age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age < 0) {
        /* asm 0x440ab6-0x440ad1: POS -= VEL (cancel preamble), then
         * `jmp 0x43fbbc` — advance iter WITHOUT kill.  No owner gate. */
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - vz);
        return;
    }

    /* asm 0x440ad6-0x440aff: LIFE_MULT += 0.01, VEL_X *= 0.99, VEL_Z *= 0.99
     * (VEL_Y is deliberately not damped). */
    float life = slot_get_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, life + 0.01f);
    float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
    float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx * 0.99f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz * 0.99f);

    /* asm 0x440b02-0x440b13: SM gate.  Only fires when PART_IDX == 0.  If
     * SM ret != 0 → LAB_004411e3 → KILL + advance iter. */
    int32_t part_idx = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    if (part_idx == 0) {
        if (state_machine_call_ret(slot_base(i)) != 0) {
            scene1_records_b_tick_kill_slot(i);
            return;
        }
    }

    /* asm 0x440b19-0x440ba1: 0xa3-only entity pose-snap.  Gated on
     * type==0xa3 AND PART_IDX==0 AND AGE == (slot_idx % 0xf + 0x3c). */
    if (type == 0xa3 && part_idx == 0) {
        int target_age = (int)((unsigned)i % 0xf) + 0x3c;
        if (age == target_age) {
            int32_t owner_b_int =
                slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_B);
            if (owner_b_int) {
                uint8_t *owner =
                    (uint8_t *)(uintptr_t)(uint32_t)owner_b_int;

                float save_x, save_y, save_z;
                memcpy(&save_x, owner + 0x3f0, sizeof save_x);
                memcpy(&save_y, owner + 0x3f4, sizeof save_y);
                memcpy(&save_z, owner + 0x3f8, sizeof save_z);

                float px   = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
                float pz   = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
                float zero = 0.0f;
                memcpy(owner + 0x3f0, &px,   sizeof px);
                memcpy(owner + 0x3f4, &zero, sizeof zero);
                memcpy(owner + 0x3f8, &pz,   sizeof pz);

                scene1_record_b_spawn_npc(owner, 0x97, 1);

                /* Reload owner_b — asm at 0x440b84 re-reads [esi+0x14]
                 * after the call.  Fidelity-only in our port (OWNER_B
                 * doesn't move). */
                int32_t reloaded =
                    slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_B);
                if (reloaded) {
                    uint8_t *owner2 =
                        (uint8_t *)(uintptr_t)(uint32_t)reloaded;
                    memcpy(owner2 + 0x3f0, &save_x, sizeof save_x);
                    memcpy(owner2 + 0x3f4, &save_y, sizeof save_y);
                    memcpy(owner2 + 0x3f8, &save_z, sizeof save_z);
                }
            }
        }
    }

    /* asm 0x440ba7-0x440bae: kill on AGE == 0x5a, else fall through to
     * LAB_00440dbd default-tail (deferred). */
    if (age == 0x5a) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15f — type 0x77 / 0xa2 shared owner-follow body ══════════
 *
 * Engine asm 0x4408b0..0x440aa9 / decomp L2773-L2820.  Two types share
 * one body via the dispatch's paired `je 0x4408b0`.  Each tick:
 *
 *   1. Type-0x77-only kill: if owner_a+0xcf8 != 0 → KILL.
 *      (LAB_0043fbb9 — `and [esi], 0x0` — slot.ACTIVE = 0)
 *      [type 0xa2 skips this check]
 *   2. ROT_X += 0.2  (unconditional, both types)
 *   3. If AGE > threshold (0x14 for 0x77, 0x3c for 0xa2):
 *        target = owner_a.pose@+0x20/+0x28 (for 0x77),
 *               OR owner_b.pose@+0x3f0/+0x3f8 (for 0xa2)
 *        (dx, dz) = target - slot.POS_XZ
 *        if (dx, dz) != (0, 0):
 *          len = sqrtf(dx*dx + dz*dz)
 *          if len < 1.5: KILL slot (engine `and [esi], 0x0`)
 *          if len > 1.0: (dx, dz) /= len
 *        factor   = (AGE - 0x14) * 0.002 + 0.001
 *        VEL_X   += factor * dx
 *        VEL_Z   += factor * dz
 *        if (VEL_X, VEL_Z) != (0, 0):
 *          speed = sqrtf(VEL_X^2 + VEL_Z^2)
 *          if speed > 0.3:
 *            VEL_X *= 0.3 / speed
 *            VEL_Z *= 0.3 / speed
 *        VEL_X *= 0.98  ; VEL_Z *= 0.98
 *   4. Per-type SM dispatch:
 *      type == 0xa2: SM iff AGE > 0x3c (ret IGNORED)
 *      type == 0x77: SM always; if ret != 0 → advance iter (skip step 5)
 *   5. Kill on AGE == 4000 (LAB_004402a2 / LAB_00440dbd kill-eq)
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x5198d8 = 0.2     (ROT_X increment; shared w/ 0x84 ground threshold)
 *   0x5198d4 = 0.002   (factor slope on AGE-0x14)
 *   0x5198f4 = 0.001   (factor offset)
 *   0x5198e0 = 1.5     (kill-if-len-below threshold)
 *   0x519364 = 1.0     (normalize-if-len-above threshold)
 *   0x5194ec = 0.3     (horizontal speed cap)
 *   0x5198ec = 0.98    (horizontal drag)
 *   0x519320 = 0.0     (sign-compare zero)
 *
 * Asm corrections vs decomp:
 *   - Decomp shows `if (iVar13 != 0) goto LAB_0043fbbc;` for the 0x77 SM
 *     branch.  This is "advance iter, NO kill" (LAB_0043fbbc = `inc
 *     [ebp-0x28]`); not the same as the type-0x77-specific kill at
 *     LAB_0043fbb9 (`and [esi], 0x0`).  Raw asm at 0x440a99 `jne 0x43fbbc`
 *     confirms: 0x77 with SM ret != 0 keeps the slot ALIVE for one more
 *     tick (skipping the AGE==4000 kill check).
 *   - Decomp `(*piVar14 == 0xa2 && (iVar13 < (int)(&...)[uVar6 * 0x49]))`
 *     SM gate is "AGE > threshold (= 0x3c for 0xa2)"; matches asm
 *     0x440a7f `cmp [esi+0x98], ebx ; jle 0x440a9f` where ebx == 0x3c.
 *   - Length < 1.5 KILL: engine inlines `and [esi], 0x0` at 0x44096f; rest
 *     of body still executes (VEL writes + SM + AGE==4000 check) on the
 *     now-dead slot — observable side-effects are confined to slot
 *     memory, and next tick the dead-slot skip at scene1_records_b_tick
 *     top elides further work.
 *
 * Owner field convention:
 *   - type 0x77: owner_a at slot+0x10 (= OWNER_A, dw 4) with pose at
 *     owner+0x20/+0x24/+0x28 (NPC-style entity pose).
 *   - type 0xa2: owner_b at slot+0x14 (= OWNER_B, dw 5) with pose at
 *     owner+0x3f0/+0x3f4/+0x3f8 (NPC-allocator-owner-style pose).
 *
 * Engine assumes owner != NULL; deref would crash if it were.  Our port
 * adds a NULL guard that skips the motion block but still advances the
 * SM + AGE==4000 tail.
 *
 * Dormant in HOUSE under default smoke flags — types 0x77/0xa2 not in any
 * landed C8j allocator's type set; `--force-b-npc 0x77/0xa2` exercises
 * the owner-follow chain.  No new hooks needed.
 */
static void body_0x77_or_0xa2(int i, int32_t type)
{
    /* asm 0x4408b8-0x4408c2: type 0x77 specific kill. */
    if (type == 0x77) {
        const void *owner_a = slot_owner_a(i);
        if (owner_a && owner_read_i(owner_a, 0xcf8) != 0) {
            scene1_records_b_tick_kill_slot(i);
            return;
        }
    }

    /* asm 0x4408c8-0x4408da: ROT_X += 0.2 (unconditional). */
    float rot_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x + 0.2f);

    /* asm 0x4408dc-0x4408eb: per-type AGE threshold for motion block. */
    int     age_threshold = (type == 0xa2) ? 0x3c : 0x14;
    int32_t age           = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    int motion_ran = 0;
    if (age > age_threshold) {
        /* asm 0x4408f1-0x44091c: target = owner.pose - slot.POS_XZ. */
        float dx = 0.0f, dz = 0.0f;
        int   have_target = 0;
        if (type == 0x77) {
            const void *owner_a = slot_owner_a(i);
            if (owner_a) {
                float ox = owner_read_f(owner_a, 0x20);
                float oz = owner_read_f(owner_a, 0x28);
                dx = ox - slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
                dz = oz - slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
                have_target = 1;
            }
        } else {
            int32_t owner_b_int =
                slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_B);
            if (owner_b_int) {
                const void *owner_b =
                    (const void *)(uintptr_t)(uint32_t)owner_b_int;
                float ox = owner_read_f(owner_b, 0x3f0);
                float oz = owner_read_f(owner_b, 0x3f8);
                dx = ox - slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
                dz = oz - slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
                have_target = 1;
            }
        }

        if (have_target) {
            motion_ran = 1;

            /* asm 0x440922-0x440990: kill-if-len<1.5 / normalize-if-len>1.0
             * (gated on (dx, dz) != (0, 0)). */
            if (dx != 0.0f || dz != 0.0f) {
                float len = sqrtf(dx * dx + dz * dz);
                if (len < 1.5f) {
                    scene1_records_b_tick_kill_slot(i);
                }
                if (len > 1.0f) {
                    dx /= len;
                    dz /= len;
                }
            }

            /* asm 0x440992-0x4409e9: VEL += factor * dxz. */
            float factor = (float)(age - 0x14) * 0.002f + 0.001f;
            float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
            float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
            vx += factor * dx;
            vz += factor * dz;
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz);

            /* asm 0x4409ec-0x440a60: clamp horizontal speed to 0.3. */
            if (vx != 0.0f || vz != 0.0f) {
                float speed = sqrtf(vx * vx + vz * vz);
                if (speed > 0.3f) {
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X,
                               vx * 0.3f / speed);
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z,
                               vz * 0.3f / speed);
                }
            }

            /* asm 0x440a63-0x440a78: 0.98 horizontal drag. */
            vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
            vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx * 0.98f);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz * 0.98f);
        }
    }
    (void)motion_ran;

    /* asm 0x440a7b-0x440a9f: per-type SM dispatch. */
    if (type == 0xa2) {
        /* asm 0x440a7f-0x440a8e: 0xa2 calls SM iff AGE > 0x3c; return
         * value ignored. */
        if (age > age_threshold) {
            state_machine_call(slot_base(i));
        }
    } else {
        /* asm 0x440a90-0x440a99: 0x77 calls SM unconditionally.  If
         * ret != 0 → advance iter (LAB_0043fbbc; skip AGE==4000 check). */
        if (state_machine_call_ret(slot_base(i)) != 0) {
            return;
        }
    }

    /* asm 0x440a9f-0x440aa9: LAB_004402a2 / LAB_00440dbd — kill on AGE
     * == 4000, else fall through to LAB_00440dbd default-tail (deferred). */
    if (age == 4000) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15g — types 0x2e / 0x36 player-homing damped drift ═══════
 *
 * Decomp L39190+ / asm 0x440005..0x440295 (FUN_0043ae20 tail cascade).
 * Two-type shared body reached when the upstream cascade has rejected
 * 0x82 / 0x3e / 0x5f / 0x60 / 0x33 / 0x82-and-0x3e family — the next
 * else-arm checks `(iVar15 != 0x2e) && (iVar15 != 0x36) goto LAB_00440dc1`.
 *
 * Unconditional preamble (asm 0x440013..0x44002e):
 *   ROT_Z += 0.05        (slot[+0x94] = OFF_ROT_Z; constant 0x5198f8)
 *   DRAG   = 0           (slot[+0xa8] = OFF_DRAG; fldz)
 *
 * Per-type motion block:
 *   - 0x2e (asm 0x44003e..0x440101): when AGE in (0x1e, 0xc8):
 *       step  = AGE - 0x1e (int → float)
 *       rate  = min(step * 5e-05, 0.005)        (lerp speed grows w/ age)
 *       drag  = max(1.0 - step * 0.01, 0.98)    (drag grows w/ age, floor)
 *       VEL_{X,Y,Z} += rate * ((player_pos[i] + ALT[i]) - POS[i])
 *       VEL_{X,Y,Z} *= drag
 *   - 0x36 (asm 0x44010d..0x440176): when AGE in (0x1e, 0x78):
 *       VEL_{X,Y,Z} += 0.005 * ((player_pos[i] + ALT[i]) - POS[i])
 *       VEL_{X,Y,Z} *= 0.95
 *
 * Shared speed-cap (asm 0x440179..0x440213):
 *   if |VEL|² > 0 and |VEL| > cap → VEL *= cap / |VEL|
 *   cap = 0.25 (0x2e), 0.75 (0x36)  (constants 0x519344 / 0x519b54)
 *
 * SM call (asm 0x440210..0x44021c): state_machine(slot); if ret != 0 →
 * kill slot via LAB_004411e3 (fall through still executes 0x36 spawns
 * since the AND DWORD PTR [esi],0 happens BEFORE the 0x36 spawn check).
 *
 * Wait — actually the AND DWORD PTR [esi],0 at 0x44021b zeroes TYPE, so
 * the subsequent `cmp DWORD PTR [esi],0x36; jne 0x440298` will fail and
 * the spawn cluster is skipped.  Re-reading asm: SM ret!=0 → kill →
 * skip 0x36 spawns → kill check at 0x440298 (kills if AGE==0x100, here
 * vacuous since slot is already dead).
 *
 * Type 0x36 spawn cluster (asm 0x44021e..0x440295):
 *   scene1_spawn(0, POS_X, POS_Y, POS_Z, 0x70, 0.2, 1)
 *   scene1_spawn(0, POS_X+VEL_X*0.5, POS_Y+VEL_Y*0.5, POS_Z+VEL_Z*0.5,
 *                0x70, 0.2, 1)
 *
 * Shared LAB_00440298 tail (asm 0x440298..0x4402a8): kill if AGE == 0x100.
 *
 * Asm corrections vs Ghidra:
 *   - Both scene1_spawn calls show 5/6 args in decomp (`0x70, 0x3e4ccccd`
 *     and `0x70` alone).  Raw asm pushes 7 args for both: scale=0.2
 *     (0x5198d8) and param7=1 are present.  Total `add esp, 0x1c` = 28 B
 *     = 7 dwords pop confirms 7-arg form for both calls.  Standard
 *     Ghidra "trailing args dropped on x87 ABI" pattern (PHC #3 family).
 *
 * Constants verified via `tools/analyze/pe.py bytes <va> 4`:
 *   0x5198f8 = 0.05         (ROT_Z spin step)
 *   0x519bf4 = 5e-05        (0x2e rate-per-tick coefficient)
 *   0x5198c8 = 0.005        (0x2e rate ceiling AND 0x36 constant rate)
 *   0x5193a4 = 0.01         (0x2e drag step)
 *   0x519364 = 1.0          (0x2e drag base)
 *   0x5198ec = 0.98         (0x2e drag floor)
 *   0x5198b4 = 0.95         (0x36 constant drag)
 *   0x519320 = 0.0          (|VEL|² > 0 sentinel)
 *   0x519344 = 0.25         (0x2e speed cap)
 *   0x519b54 = 0.75         (0x36 speed cap)
 *   0x5198d8 = 0.2          (0x36 spawn scale)
 *   0x51935c = 0.5          (0x36 second-spawn POS+VEL*0.5 multiplier)
 *
 * Player position read from `g_scene1_player_pos[3]` (DAT_056da1d8/dc/e0).
 */

static void body_0x2e_or_0x36(int i, int32_t type)
{
    int32_t *slot = slot_base(i);

    /* asm 0x440013-0x44002e: ROT_Z += 0.05; DRAG = 0. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z) + 0.05f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);

    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    /* asm 0x44002f-0x440176: per-type motion block.  Both branches read
     * (player_pos + ALT) as the homing target and write into VEL_{X,Y,Z}. */
    if (type == 0x2e) {
        if (age > 0x1e && age < 0xc8) {
            /* asm 0x440052-0x4400a8: rate and drag derived from step. */
            float step = (float)(age - 0x1e);
            float rate = step * 5e-05f;
            if (rate > 0.005f) rate = 0.005f;
            float drag = 1.0f - step * 0.01f;
            if (drag < 0.98f) drag = 0.98f;

            /* asm 0x4400aa-0x440100: VEL = (VEL + rate*delta) * drag. */
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float ax = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
            float ay = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
            float az = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
            float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
            float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
            float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

            vx = ((g_scene1_player_pos[0] + ax - px) * rate + vx) * drag;
            vy = ((g_scene1_player_pos[1] + ay - py) * rate + vy) * drag;
            vz = ((g_scene1_player_pos[2] + az - pz) * rate + vz) * drag;

            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz);
        }
    } else {
        /* type == 0x36 — asm 0x440103-0x440176. */
        if (age > 0x1e && age < 0x78) {
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float ax = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
            float ay = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
            float az = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
            float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
            float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
            float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

            vx = ((g_scene1_player_pos[0] + ax - px) * 0.005f + vx) * 0.95f;
            vy = ((g_scene1_player_pos[1] + ay - py) * 0.005f + vy) * 0.95f;
            vz = ((g_scene1_player_pos[2] + az - pz) * 0.005f + vz) * 0.95f;

            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz);
        }
    }

    /* asm 0x440179-0x440213: speed-cap (per-type cap). */
    float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
    float speed_sq = vx * vx + vy * vy + vz * vz;
    if (speed_sq > 0.0f) {
        float speed = sqrtf(speed_sq);
        float cap = (type == 0x36) ? 0.75f : 0.25f;
        if (speed > cap) {
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx * cap / speed);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy * cap / speed);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz * cap / speed);
        }
    }

    /* asm 0x440210-0x44021c: SM call.  Nonzero return zeroes slot[TYPE]
     * (asm `and [esi], 0x0`) — which both kills the slot AND prevents
     * the subsequent type==0x36 spawn cluster from firing. */
    int sm_kills = (state_machine_call_ret(slot) != 0);
    if (sm_kills) {
        scene1_records_b_tick_kill_slot(i);
    }

    /* asm 0x44021e-0x440295: type 0x36 spawn cluster (skipped when slot
     * is now dead — TYPE was just zeroed by the SM-kill path). */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_TYPE) == 0x36) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx2 = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy2 = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz2 = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

        scene1_spawn(0, px, py, pz, 0x70, 0.2f, 1);
        scene1_spawn(0,
                     px + vx2 * 0.5f,
                     py + vy2 * 0.5f,
                     pz + vz2 * 0.5f,
                     0x70, 0.2f, 1);
    }

    /* asm 0x440298-0x4402a8: shared LAB_00440298 tail — kill on AGE == 0x100. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x100) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15h — types 0xa0 + 0x7e cull-tail variants ═══════════════
 *
 * Two more LAB_004402ad / LAB_004402c0 cousins extending the 15e trail-cull
 * body family.
 *
 * **Type 0xa0** — engine asm 0x43fb88..0x43fc91 / decomp L2932-L2965.
 * Owns its own body (NOT routed through LAB_004402ad).  Same overall shape
 * as 0x73/0x78/0x7a (15e) but with two key swaps:
 *   - AGE<0 owner check reads `[esi+0x14] + 0x440` = OWNER_B + 0x440 (vs
 *     OWNER_A + 0xcf8 for the 0x73 family).  Field +0x440 is an NPC-blob
 *     flag; nonzero → kill, zero → continue iter loop with POS rolled
 *     back.
 *   - AGE==1 dual overlay_spawn passes `edi=0` (NULL owner) as 1st arg
 *     for both calls — the 0x73 family pushes `[esi+0x10]` = OWNER_A.
 *   Template IDs (0x10 + 0x13) and scales (1.0 + 0.7) are identical to
 *   the 0x73 family head.
 *
 *   Phase 0  (AGE < 0):
 *     POS -= VEL                                      (cancel preamble)
 *     if OWNER_B+0x440 != 0 → KILL                    (LAB_0043fbb9)
 *     else                  → continue iter loop      (LAB_0043fbbc)
 *
 *   Phase 1  (AGE == 1):
 *     scene1_overlay_spawn(NULL, POS-VEL, tid=0x10, 1.0, -1, 0, 0, 0)
 *     scene1_overlay_spawn(NULL, ALT_POS, tid=0x13, 0.7, -1, 0, 0, 0)
 *
 *   Phase 2  (AGE >= 0):
 *     DRAG = 0
 *     if cull_query(POS_X, POS_Y) >= 0   → KILL → AGE-78 kill check
 *     else SM:
 *       ret != 0 → KILL + advance (skip AGE-78 path)
 *       ret == 0 → AGE-78 kill check
 *
 * **Type 0x7e** — engine asm 0x4402ad..0x44058d.  Enters LAB_004402ad
 * (jne 0x44036a at 0x4402ba skips the 0x73-family head since 0x7e is not
 * in {0x73, 0x78, 0x7a}) and falls through to the shared DRAG=0 + per-type
 * cascade + cull + SM tail.  None of the per-type cascade checks
 * ({0x4d/0x4e/0x4f/0x50/0xa5/0xa6/99/0x52/0x4d/0x56/0x96}) match 0x7e, so
 * the per-type effects are all skipped — body collapses to:
 *
 *     DRAG = 0
 *     if cull_query(POS_X, POS_Y) >= 0   → KILL → AGE-78 kill check
 *     else SM:
 *       ret != 0 → KILL + advance
 *       ret == 0 → AGE-78 kill check
 *
 * Identical to 0xa0's Phase 2 (modulo source asm address).
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x519748 = 0.7f          (0xa0 second overlay_spawn scale; same as 0x73 family)
 *
 * Asm corrections vs decomp (raw asm 0x43fb88..0x43fc91 for 0xa0):
 *   - L2933 `FUN_004147d5` calls are 9-arg wrappers around
 *     scene1_overlay_spawn appending mode=0 — Ghidra renders them with
 *     dropped 8th arg (override_rot_y_bits = 0); raw asm at 0x43fbdf /
 *     0x43fc1d pushes 0xffffffff for override_dur and zeroes for the
 *     trailing slots (`add esp, 0x24` = 9 dwords).
 *   - L2945 `FUN_00490820(POS_X, POS_Y)` is 4-arg in raw asm at 0x43fc6a
 *     (POS_X, POS_Y, POS_Z, 0.0f); our cull_query hook is 2-arg — the
 *     dropped Z + 0 feed the engine's depth calc, but the default-visible
 *     (return < 0) behavior is preserved (same handling as 15e's body).
 *   - L2948 `LAB_0043fc81` = `mov [esi], edi; jmp 0x43fbbc` — kill +
 *     skip the LAB_0043fada wall-bounce-tail path (advance directly).
 *
 * AGE<0 OWNER_B NULL-guard: engine asm at 0x43fbad dereferences slot+0x14
 * (= OWNER_B) without a NULL check.  Real allocator commits set OWNER_B
 * for NPC-class types (0xa0 is in FUN_00445a8c's "Mega-B" cluster per
 * C8j.11), but a smoke / test slot with OWNER_B=0 would crash.  Our port
 * mirrors engine fidelity (early-return when null) per the same NULL-guard
 * convention as the 0x73 family body.
 *
 * Dormant in HOUSE under default smoke flags — neither type is in the
 * landed allocator's set; `--force-b-npc 0xa0` and `--force-b-entity 0x7e`
 * exercise each body end-to-end.  No new hooks; reuses cull_query
 * (PHC #14 stand-in), state_machine_call_ret (PHC #20), overlay_spawn
 * (PHC #11), scene1_records_b_tick_kill_slot.
 */
static void body_0xa0(int i)
{
    int32_t age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age < 0) {
        /* asm 0x43fb92-0x43fbaa: POS -= VEL — cancel preamble. */
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - vz);

        /* asm 0x43fbad-0x43fbb9: read OWNER_B+0x440; kill iff nonzero. */
        const void *owner_b = slot_owner(i);
        if (owner_b && owner_read_i(owner_b, 0x440) != 0) {
            scene1_records_b_tick_kill_slot(i);
        }
        return;
    }

    /* asm 0x43fbd1-0x43fc43: AGE == 1 dual NULL-owner overlay_spawn. */
    if (age == 1) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

        /* asm 0x43fbd6-0x43fc0d: template 0x10 trail at POS-VEL, scale 1.0,
         * owner=NULL (engine pushes `edi` which was zeroed at 0x43fbd6). */
        overlay_spawn(NULL, px - vx, py - vy, pz - vz,
                      /*template_id=*/0x10,
                      /*scale_base=*/1.0f,
                      /*override_dur=*/-1,
                      /*override_rot_y_bits=*/0,
                      /*shape_mode=*/0,
                      /*mode=*/0);

        /* asm 0x43fc10-0x43fc43: template 0x13 trail at ALT_POS, scale 0.7,
         * owner=NULL. */
        float ax = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
        float ay = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
        float az = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
        overlay_spawn(NULL, ax, ay, az,
                      /*template_id=*/0x13,
                      /*scale_base=*/0.7f,
                      /*override_dur=*/-1,
                      /*override_rot_y_bits=*/0,
                      /*shape_mode=*/0,
                      /*mode=*/0);
    }

    /* asm 0x43fc47-0x43fc4a: DRAG = 0. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);

    /* asm 0x43fc4a-0x43fc6f: cull_query(POS_X, POS_Y, POS_Z, 0.0). */
    float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    if (cull_query(px, py) >= 0) {
        /* asm 0x43fc88: kill + AGE==0x78 check (LAB_0043fada). */
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* asm 0x43fc76-0x43fc83: SM call; nonzero → kill+advance (skip
     * AGE-78 path), zero → AGE-78 kill check (LAB_0043fada). */
    int sm_ret = state_machine_call_ret(slot_base(i));
    if (sm_ret != 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* LAB_0043fada / LAB_00440dc1 — kill on AGE == 0x78; else fall through
     * to wall-bounce tail (deferred). */
    if (age == 0x78) {
        scene1_records_b_tick_kill_slot(i);
    }
}

static void body_0x7e(int i)
{
    /* asm 0x44036a-0x44036f: DRAG = 0 (entry via LAB_004402ad jne 0x44036a
     * — skips the 0x73-family head since 0x7e isn't in {0x73,0x78,0x7a}). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);

    /* asm 0x440372-0x440474: per-type cascade dispatches to {0x4d-0x50/
     * 0xa5/0xa6/0x63/0x52/0x4d/0x56/0x96} — none of which match 0x7e,
     * so all per-type effects are skipped.  Falls into the cull_query at
     * 0x44047a. */

    /* asm 0x44047a-0x44049f: cull_query(POS_X, POS_Y, POS_Z, 0.0); CULL
     * (ret >= 0) → kill + AGE==0x78 check (LAB_00440741). */
    float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    if (cull_query(px, py) >= 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* asm 0x44058d-0x4405a3: clear DAT_06a46f94, SM call.
     * ret == 1: 0x52-special path doesn't fire (we're 0x7e) →
     *           fall through to LAB_0043fbb9 KILL (advance)
     * ret == 0: LAB_00440741 (AGE-78 check)
     * ret >= 2: LAB_0043fbb9 KILL (advance) */
    g_scene1_records_b_tick_anim_drive = 0;
    int sm_ret = state_machine_call_ret(slot_base(i));
    if (sm_ret != 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* LAB_00440741 / LAB_004402a2 — kill on AGE == 0x78, else fall through
     * to LAB_00440dc1 wall-bounce tail (deferred). */
    int age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
    if (age == 0x78) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15i — entity-bounce shared body ═══════════════════════════
 *
 * Engine asm 0x44036a..0x440748 / decomp L39346-L39440.  Shared LAB_004402ad
 * tail body covering 12 types: {0x4d, 0x4e, 0x4f, 0x50, 0xa5, 0xa6, 99,
 * 0x51, 0x52, 0x56, 0x96, 0x62}.  Types 0x73/0x78/0x7a, 0xa0, 0x7e also
 * traverse this body but have their own entry points (with private head
 * code) and live in body_0x73_or_0x78_or_0x7a / body_0xa0 / body_0x7e.
 *
 * Body structure (entry at asm 0x44036a):
 *
 *   1. DRAG = 0 always
 *   2. Per-type head:
 *      - {0x4d/0x4e/0x4f/0x50/0xa5/0xa6}: AGE==1 → overlay_spawn(owner_a,
 *        POS, template=9, scale=0.4); DRAG=0.5
 *      - 99: overlay_spawn(owner_a, POS+VEL*12, template=0x24, scale=1.0);
 *        DRAG=1.2
 *      - 0x52: DRAG=0.5
 *      - 0x4d: VEL_Y -= 0.01
 *      - {0x56/0x96}: full ground-bounce body (jumps over cull/general SM)
 *
 *   3. Cull gate (skipped by 0x56/0x96 — they jumped to the bounce body):
 *      `iVar13 = FUN_00490820(POS_X, POS_Y, POS_Z, 0.0f)` (4-arg in raw asm;
 *      our 2-arg hook drops Z+0, default-visible behavior preserved).
 *      iVar13 >= 0 → KILL + LAB_00440741.
 *
 *   4. Visible (cull < 0) per-type sub-dispatch:
 *      - 0x51: 3-iter SM-driven displaced-POS loop (cos*(iter-1), sin*(iter-
 *        1)) using slot.ROT_X (engine byte +0x90) as angle.  Restore POS
 *        after loop.  AGE==0x3c → KILL.  Else LAB_00440741.
 *      - 99: DRAG=1.0; SM(); LAB_00440741.
 *      - general: anim_drive=0; ret = SM().
 *        - ret == 1: if type==0x52 AND anim_drive > 0 → damage write to
 *          owner_a+0xe30 (= anim_drive/10, min 1) + owner_a+0xe38 = 0x1e.
 *          KILL (always, regardless of damage-write).
 *        - ret == 0: LAB_00440741.
 *        - ret >= 2: KILL.
 *
 *   5. 0x56/0x96 ground-bounce body (asm 0x4405f3-0x440705):
 *      - DRAG = -0.15; VEL_Y -= 0.01
 *      - ROT_SCR += 0.05; ROT_Z += 0.03
 *      - matrix update: MATRIX0 = RotY(ROT_Z) * RotX(ROT_SCR)
 *      - If VEL_Y < 0 AND ground_query() == 1 AND POS_Y <= ground+0.3:
 *        - POS_Y = ground+0.3
 *        - VEL_Y *= -0.5; VEL_X *= 0.7; VEL_Z *= 0.7
 *        - bounce_count++
 *        - if bounce_count == 1: SE (0x158 for 0x56, 0x168 for 0x96)
 *        - if bounce_count >= 2: KILL (return)
 *      - g_scene1_records_b_tick_flag = 1
 *      - if bounce_count == 0: SM ret=1→KILL, ret=2→bounce_count=1, ret=0
 *        fall-through; if bounce_count != 0: bounce_count++
 *      - LAB_00440741 (kill on AGE==0x78)
 *
 *   6. LAB_00440741 / LAB_004402a2: kill on AGE == 0x78, else fall through
 *      to LAB_00440dc1 default-tail (deferred).
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x5195c8 = 0.4         ({0x4d-0x50/0xa5/0xa6} overlay scale)
 *   0x51935c = 0.5         (cluster + 0x52 DRAG)
 *   0x519560 = 12.0        (99 POS+VEL*12 offset)
 *   0x519924 = 1.2         (99 DRAG; 0x51 DRAG)
 *   0x5193a4 = 0.01        (VEL_Y subtract for 0x4d, 0x56, 0x96)
 *   0x519364 = 1.0         (99 DRAG override; 0x51 iter-1.0; ground+1.0
 *                           override for non-0x56/0x96 — unused here)
 *   0x519998 = -0.15       (0x56/0x96 DRAG)
 *   0x5198f8 = 0.05        (0x56/0x96 ROT_SCR step)
 *   0x519900 = 0.03        (0x56/0x96 ROT_Z step)
 *   0x519320 = 0.0         (VEL_Y < 0 compare)
 *   0x5194ec = 0.3         (ground+0.3 bounce threshold)
 *   0x519c2c = -0.5        (VEL_Y *= -0.5 bounce)
 *   0x519748 = 0.7         (VEL_X/Z bounce damp)
 *
 * Asm corrections vs Ghidra decomp:
 *   - cull_query (FUN_00490820) raw asm at 0x44047a..0x440495 pushes 4 args
 *     (POS_X, POS_Y, POS_Z, 0.0f); Ghidra L39437 shows 2.  Our 2-arg hook
 *     preserves default-visible behavior (existing 0x73/0x78/0x7a/0x7e
 *     bodies use the same elision).
 *   - 0x51 3-iter loop: Ghidra L39397 renders the exit condition as a
 *     float compare against 4.2039e-45.  Raw asm at 0x440544-0x44054b
 *     uses INT increment + `cmp [ebp-0x8], 0x3 ; jne` — local_c is an
 *     int.  Iteration count is 3 (values 0/1/2), not 4.
 *   - 0x56/0x96 SE call at 0x4406eb/0x4406f2: Ghidra L39429 renders
 *     `FUN_00499519()` argless; raw asm pushes 0x158 (for 0x56) or 0x168
 *     (for 0x96).
 *   - 0x51 angle source: Ghidra L39391 shows `slot[0x40]` (= byte 0x40 =
 *     dw 16, undocumented); raw asm at 0x4404e6 / 0x44050f loads from
 *     `[esi+0x90]` = byte 0x90 = dw 36 = SCENE1_RECORDS_B_OFF_ROT_X.
 *
 * Dormant in HOUSE under default smoke flags — table B BSS-zero, no
 * allocator wires these types in production today.  `--force-b-{npc,
 * entity} <type>` exercises end-to-end (cull-default-visible → SM →
 * AGE==0x78 kill).
 */
static void body_entity_bounce_inner(int i, int32_t type);
static void body_entity_bounce(int i, int32_t type)
{
    /* Engine-faithful tail: body_entity_bounce sets the per-tick flag
     * via the inline 0x56/0x96 ground-bounce path (asm 0x440714) and
     * the engine then falls through to LAB_00440dc1 default-tail wall-
     * bounce body via the shared LAB_004402a2 / LAB_00440741 epilogue.
     * Wrap the inner body so every early return funnels through the
     * post-body fall-through.  body_lab_00440dc1 gates internally on
     * slot[AUX_C8] + per-tick flag, so this is a no-op for slots that
     * don't have AUX_C8 latched from an earlier body. */
    body_entity_bounce_inner(i, type);
    scene1_records_b_run_lab_00440dc1(i);
}

static void body_entity_bounce_inner(int i, int32_t type)
{
    /* 1. asm 0x44036a-0x44036f: DRAG = 0 unconditionally. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);

    int32_t age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    /* 2a. asm 0x440372-0x4403df: cluster {0x4d-0x50/0xa5/0xa6} — AGE==1
     * overlay (template=9, scale=0.4), then DRAG = 0.5. */
    if (type == 0x4d || type == 0x4e || type == 0x4f || type == 0x50 ||
        type == 0xa5 || type == 0xa6) {
        if (age == 1) {
            const void *owner_a = slot_owner_a(i);
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            overlay_spawn(owner_a, px, py, pz,
                          /*template_id=*/9,
                          /*scale_base=*/0.4f,
                          /*override_dur=*/-1,
                          /*override_rot_y_bits=*/0,
                          /*shape_mode=*/0,
                          /*mode=*/0);
        }
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.5f);
    }

    /* 2b. asm 0x4403e1-0x440441: type 99 — overlay (template=0x24,
     * scale=1.0) at POS + VEL*12 (POS.y unscaled); DRAG = 1.2. */
    if (type == 99) {
        const void *owner_a = slot_owner_a(i);
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        overlay_spawn(owner_a, px + vx * 12.0f, py, pz + vz * 12.0f,
                      /*template_id=*/0x24,
                      /*scale_base=*/1.0f,
                      /*override_dur=*/-1,
                      /*override_rot_y_bits=*/0,
                      /*shape_mode=*/0,
                      /*mode=*/0);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.2f);
    }

    /* 2c. asm 0x440442-0x440451: type 0x52 — DRAG = 0.5. */
    if (type == 0x52) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.5f);
    }

    /* 2d. asm 0x440453-0x440461: type 0x4d — VEL_Y -= 0.01. */
    if (type == 0x4d) {
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy - 0.01f);
    }

    /* 2e. asm 0x440464-0x440474: types 0x56/0x96 take the inline ground-
     * bounce body and BYPASS the cull check + general SM cascade. */
    if (type == 0x56 || type == 0x96) {
        /* asm 0x4405f3-0x440605: DRAG = -0.15. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, -0.15f);

        /* asm 0x44060b-0x44061b: VEL_Y -= 0.01. */
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.01f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy);

        /* asm 0x44061e-0x440630: ROT_SCR += 0.05; ROT_Z += 0.03. */
        float rot_scr =
            slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR) + 0.05f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, rot_scr);
        float rot_z =
            slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_Z) + 0.03f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z, rot_z);

        /* asm 0x440632-0x44065c: MATRIX0 = RotY(ROT_Z) * RotX(ROT_SCR).
         * Engine pushes (slot[MATRIX0], scratch, slot[MATRIX0]) — our
         * `mat4_mul(out, a, b)` writes out = a * b, so the call matches
         * scratch * slot[MATRIX0] = RotY(ROT_Z) * RotX(ROT_SCR). */
        int32_t *slot = slot_base(i);
        float *mat0 = (float *)(slot + SCENE1_RECORDS_B_OFF_MATRIX0);
        float scratch[16];
        mat4_rotation_x(mat0, rot_scr);
        mat4_rotation_y(scratch, rot_z);
        mat4_mul(mat0, scratch, mat0);

        /* asm 0x44065d-0x4406ab: VEL_Y < 0 + ground hit + POS_Y <=
         * ground+0.3 gate. */
        int bounced = 0;
        if (vy < 0.0f) {
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float gy = 0.0f;
            if (ground_query(px, py, pz, &gy) == 1) {
                float threshold = gy + 0.3f;
                if (py <= threshold) {
                    /* asm 0x4406ae-0x4406d8: snap POS_Y, damp velocity,
                     * bounce_count++. */
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, threshold);
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy * -0.5f);
                    float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
                    float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx * 0.7f);
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz * 0.7f);
                    int bc =
                        slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX) + 1;
                    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, bc);

                    /* asm 0x4406de-0x4406fc: bounce_count == 1 → SE
                     * (0x158 for 0x56, 0x168 for 0x96). */
                    if (bc == 1) {
                        se_play(type == 0x56 ? 0x158 : 0x168);
                    }

                    /* asm 0x4406fd-0x440706: bounce_count >= 2 → KILL
                     * (skip SM + LAB_00440741). */
                    if (bc >= 2) {
                        scene1_records_b_tick_kill_slot(i);
                        return;
                    }
                    bounced = 1;
                }
            }
        }
        (void)bounced;

        /* asm 0x440714: g_scene1_records_b_tick_flag = 1 always. */
        g_scene1_records_b_tick_flag = 1;

        /* asm 0x44071a-0x44073b: bounce_count branches. */
        int bc = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
        if (bc == 0) {
            int ret = state_machine_call_ret(slot_base(i));
            if (ret == 1) {
                scene1_records_b_tick_kill_slot(i);
                return;
            }
            if (ret == 2) {
                slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
            }
            /* ret == 0 (or unhandled): fall through to LAB_00440741. */
        } else {
            slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, bc + 1);
        }

        /* LAB_00440741: kill on AGE == 0x78 (else default-tail deferred). */
        if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x78) {
            scene1_records_b_tick_kill_slot(i);
        }
        return;
    }

    /* 3. asm 0x44047a-0x44049f: cull_query gate (skipped for 0x56/0x96).
     * Engine pushes 4 args (POS_X, POS_Y, POS_Z, 0.0f); our 2-arg hook
     * receives POS_X/POS_Y only — default returns -1 ("visible") so
     * production behavior unchanged.  ret >= 0 → KILL + LAB_00440741. */
    float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    if (cull_query(px, py) >= 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* 4a. asm 0x4404a5-0x440569: type 0x51 — 3-iter SM-driven loop with
     * displaced POS (cos*(iter-1), sin*(iter-1)) using slot.ROT_X as
     * angle.  DRAG = 1.2 prelude.  AGE==0x3c → KILL; else LAB_00440741. */
    if (type == 0x51) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.2f);

        float orig_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float orig_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float orig_z = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float angle  = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);

        for (int n = 0; n < 3; n++) {
            float off = (float)n - 1.0f;
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                       cosf(angle) * off + orig_x);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, orig_y);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                       sinf(angle) * off + orig_z);
            int ret = state_machine_call_ret(slot_base(i));
            /* asm 0x44053d-0x440542: SM ret != 0 + TYPE == 0 → break.
             * (SM may kill the slot via internal LAB_004411e3.) */
            if (ret != 0 && slot_get_i(i, SCENE1_RECORDS_B_OFF_TYPE) == 0) {
                break;
            }
        }

        /* asm 0x44054d-0x44055e: restore POS unconditionally after loop. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, orig_x);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, orig_y);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, orig_z);

        /* asm 0x44055f-0x44056f: AGE == 0x3c → KILL + LAB_00440741. */
        int cur_age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);
        if (cur_age == 0x3c) {
            scene1_records_b_tick_kill_slot(i);
        }
        if (cur_age == 0x78) {
            scene1_records_b_tick_kill_slot(i);
        }
        return;
    }

    /* 4b. asm 0x440579-0x440588: type 99 — DRAG = 1.0; SM(); LAB_00440741. */
    if (type == 99) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.0f);
        state_machine_call(slot_base(i));
        if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x78) {
            scene1_records_b_tick_kill_slot(i);
        }
        return;
    }

    /* 4c. asm 0x44058d-0x4405ee: general SM cascade for cluster +
     * {0x52, 0x62}.  anim_drive clear; SM(); branches on ret:
     *   ret == 1: 0x52-specific damage write to owner_a, then KILL
     *   ret == 0: LAB_00440741 (AGE==0x78 kill)
     *   ret >= 2: KILL */
    g_scene1_records_b_tick_anim_drive = 0;
    int ret = state_machine_call_ret(slot_base(i));
    if (ret == 1) {
        if (type == 0x52 && g_scene1_records_b_tick_anim_drive > 0) {
            int dmg = g_scene1_records_b_tick_anim_drive / 10;
            if (dmg < 1) dmg = 1;
            g_scene1_records_b_tick_anim_drive = dmg;
            void *owner_a = slot_owner_a(i);
            if (owner_a) {
                owner_write_i(owner_a, 0xe30, dmg);
                owner_write_i(owner_a, 0xe38, 0x1e);
            }
        }
        scene1_records_b_tick_kill_slot(i);
        return;
    }
    if (ret != 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }
    /* ret == 0: LAB_00440741. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x78) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ─── C8j-tick.15j — body_0x83 (shop-walker velocity nudge) ──────────────
 *
 * Engine FUN_0043ae20's type-0x83 body at decomp L38821-L38924 / asm
 * 0x43f3dc..0x43f6d2 (758 B).  Long-lived shop-walker AI driver: emits
 * particles 0x1f/0x20 + iterates 128 shop-walker records and nudges
 * each gated record's velocity toward this slot's POS by 0.03 per tick.
 * First body in the ladder that writes directly to the engine's shop-
 * walker record table (DAT_0076bd98) — earlier 0x83-related writes
 * landed in C8j-tick.5 (Body 2) flowed through FUN_0043865e state machine
 * (PHC #20), not direct table writes.
 *
 * Phases (per asm trace 0x43f3dc..0x43f6d2):
 *   1. AGE == 0x3c: SE 0x2bb (engine arg push verified vs Ghidra-argless).
 *   2. AGE == 0x1e: SE 0x2a5.
 *   3. AGE == 0x3c: VEL = (sin(ROT_X)*0.15, -0.02, cos(ROT_X)*0.15).
 *   4. AGE <= 0xb4: VEL *= 0.98; else: VEL = 0.
 *   5. SEQ_ID capture at AGE in {0x78, 0xa0, 0xc8, 0xf0} — FUN_0044375e
 *      monotonic counter (already in g_scene1_record_b_seq_counter).
 *   6. DRAG = 1.5 (unconditional, .rdata 0x5198e0).
 *   7. AGE in (0x50, 0x10e): per-AGE-bucket modulo emit particle 0x1f
 *      with scale = rng_next_unit() + 1.0  → [1, 2).  Divisor table:
 *        AGE >= 0xa0: divisor = 1   (every tick)
 *        AGE >= 0x8c: divisor = 2
 *        AGE >= 0x78: divisor = 3
 *        AGE >= 0x64: divisor = 4
 *        AGE >= 0x51: divisor = 5
 *      Emits at slot.POS with owner=OWNER_A, mode=0, shape=0, rot_y=0.
 *   8. AGE % 2 == 0: emit particle 0x20 at slot.POS, scale = 1.0.
 *   9. AGE < 0x46 AND owner_a+0xcf8 != 0: KILL.
 *  10. AGE in (0x50, 0x11d): iterate 128 shop-walker records at engine
 *      DAT_0076bd98..DAT_007c8f94 (stride 0xba4).  Per-record gates:
 *      rec[+0x1b3] <= 0 (engine `jg` skip-when->0) AND rec[+0] == 1
 *      (TYPE) AND rec[+0x1b7] == 0.  When all pass:
 *        delta  = slot.POS - rec.POS  (rec.POS at rec[-0xe..-0xc])
 *        lensq  = (dz² + dy²) + dx²  (engine fmul cascade order)
 *        if lensq > 0:
 *          len = sqrt(lensq)
 *          rec.VEL[-0xb..-0x9] += (delta / len) * 0.03  (.rdata 0x519900)
 *      Default `sw_record_at_hook` returns NULL → loop is a no-op
 *      (matches BSS-zero retail until a real table populator ports).
 *  11. (Inside phase 10's gate, AGE >= 0x3c always true since AGE > 0x50)
 *      g_scene1_records_b_tick_anim_drive = 0; SM(); if SM ret != 0 AND
 *      drive > 0: drive = max(1, drive/2); write owner_a+0xe2c = drive,
 *      owner_a+0xe34 = 0x1e.  Note +0xe2c/+0xe34 differ from body_kill_
 *      bounce's +0xe30/+0xe38 (C8j-tick.15c) and body_entity_bounce's
 *      0x52-cluster +0xe30/+0xe38 (C8j-tick.15i) — likely a sibling
 *      field pair tied to a different damage class.
 *  12. AGE == 300 (0x12c): KILL.  Else falls through (LAB_00440dc1
 *      wall-bounce post-tail, NOT ported here).
 *
 * .rdata constants verified via tools/analyze/pe.py (2026-05-25):
 *   0x5198cc = 0.15        sin/cos velocity init magnitude
 *   0x519c10 = -0.02       initial VEL_Y at AGE==0x3c
 *   0x5198ec = 0.98        drag multiplier
 *   0x5198e0 = 1.5         DRAG (unconditional set)
 *   0x519364 = 1.0         scale offset (rng + 1.0)
 *   0x519900 = 0.03        shop-walker velocity nudge magnitude
 *   0x519320 = 0.0         lensq > 0 compare
 *
 * Asm corrections vs Ghidra decomp:
 *   - SE 0x2bb / 0x2a5 push verified vs Ghidra-argless FUN_00499519.
 *   - cos/sin call at L38830 / L38834: argless decomp; asm 0x43f418 /
 *     0x43f444 shows the angle reload from [ebp-0x4] populated immediately
 *     before each call.  Both reload slot.ROT_X (+0x90).
 *   - FUN_00471089 call at 0x43f515 has 3 phantom stack args (push edx,
 *     push ecx+fstp 0.0, push -1) that the rng helper doesn't read.
 *     Our rng_next_unit() is argless, matching the engine's actual
 *     consumption pattern.
 *   - Damage write to owner_a+0xe2c / +0xe34 (NOT +0xe30 / +0xe38) — verified
 *     asm 0x43f6b8 / 0x43f6be. */
static void body_0x83(int i)
{
    int32_t age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    /* Phase 1-2: AGE-gated SE plays. */
    if (age == 0x3c) se_play(0x2bb);
    if (age == 0x1e) se_play(0x2a5);

    /* Phase 3: AGE == 0x3c — angle-derived velocity init. */
    if (age == 0x3c) {
        float ang = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(ang) * 0.15f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, -0.02f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(ang) * 0.15f);
    }

    /* Phase 4: AGE <= 0xb4 → drag 0.98; else zero. */
    if (age <= 0xb4) {
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx * 0.98f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy * 0.98f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz * 0.98f);
    } else {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);
    }

    /* Phase 5: SEQ_ID capture at AGE in {0x78, 0xa0, 0xc8, 0xf0}. */
    for (int32_t age_check = 0x78; age_check != 0x118; age_check += 0x28) {
        if (age == age_check) {
            int32_t seq = (int32_t)g_scene1_record_b_seq_counter;
            g_scene1_record_b_seq_counter++;
            slot_set_i(i, SCENE1_RECORDS_B_OFF_SEQ_ID, seq);
        }
    }

    /* Phase 6: DRAG = 1.5 (unconditional). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 1.5f);

    /* Phase 7: AGE in (0x50, 0x10e) — modulo-N particle 0x1f emit. */
    if (age > 0x50 && age < 0x10e) {
        int divisor = 1;
        if (age < 0xa0) divisor = 2;
        if (age < 0x8c) divisor = 3;
        if (age < 0x78) divisor = 4;
        if (age < 0x64) divisor = 5;
        if (age % divisor == 0) {
            const void *owner_a = slot_owner_a(i);
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float scale = rng_next_unit() + 1.0f;
            overlay_spawn(owner_a, px, py, pz, 0x1f, scale, -1, 0, 0, 0);
        }
    }

    /* Phase 8: AGE % 2 == 0 — particle 0x20 emit. */
    if (age % 2 == 0) {
        const void *owner_a = slot_owner_a(i);
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        overlay_spawn(owner_a, px, py, pz, 0x20, 1.0f, -1, 0, 0, 0);
    }

    /* Phase 9: AGE < 0x46 AND owner_a+0xcf8 != 0 → KILL. */
    if (age < 0x46) {
        void *owner_a = slot_owner_a(i);
        if (owner_a && owner_read_i(owner_a, 0xcf8) != 0) {
            scene1_records_b_tick_kill_slot(i);
            return;
        }
    }

    /* Phase 10+11: AGE in (0x50, 0x11d) — shop-walker record nudge + SM. */
    if (age > 0x50 && age < 0x11d) {
        float spx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float spy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float spz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);

        for (int r = 0; r < 128; r++) {
            int32_t *rec = sw_record_at(r);
            if (!rec) continue;

            /* Gate: rec[+0x1b3] <= 0 (engine `jg 0x43f668` skips when > 0). */
            if (rec[0x1b3] > 0) continue;
            /* Gate: rec[0] == 1 (TYPE). */
            if (rec[0] != 1) continue;
            /* Gate: rec[+0x1b7] == 0. */
            if (rec[0x1b7] != 0) continue;

            float rpx, rpy, rpz;
            memcpy(&rpx, &rec[-0xe], sizeof rpx);
            memcpy(&rpy, &rec[-0xd], sizeof rpy);
            memcpy(&rpz, &rec[-0xc], sizeof rpz);

            float dx = spx - rpx;
            float dy = spy - rpy;
            float dz = spz - rpz;

            /* Engine fmul cascade: (dz² + dy²) + dx². */
            float lensq = (dz * dz + dy * dy) + dx * dx;
            /* Engine fcomp 0.0 + `jbe` — skip when lensq <= 0. */
            if (!(lensq > 0.0f)) continue;
            float len = sqrtf(lensq);

            float rvx, rvy, rvz;
            memcpy(&rvx, &rec[-0xb], sizeof rvx);
            memcpy(&rvy, &rec[-0xa], sizeof rvy);
            memcpy(&rvz, &rec[-0x9], sizeof rvz);

            rvx += (dx / len) * 0.03f;
            rvy += (dy / len) * 0.03f;
            rvz += (dz / len) * 0.03f;

            memcpy(&rec[-0xb], &rvx, sizeof rvx);
            memcpy(&rec[-0xa], &rvy, sizeof rvy);
            memcpy(&rec[-0x9], &rvz, sizeof rvz);
        }

        /* Phase 11: AGE >= 0x3c (always true here) — SM + damage write. */
        g_scene1_records_b_tick_anim_drive = 0;
        int ret = state_machine_call_ret(slot_base(i));
        if (ret != 0 && g_scene1_records_b_tick_anim_drive > 0) {
            int v = g_scene1_records_b_tick_anim_drive / 2;
            if (v < 1) v = 1;
            g_scene1_records_b_tick_anim_drive = v;
            void *owner_a = slot_owner_a(i);
            if (owner_a) {
                owner_write_i(owner_a, 0xe2c, v);
                owner_write_i(owner_a, 0xe34, 0x1e);
            }
        }
    }

    /* Phase 12: AGE == 300 → KILL. */
    if (age == 300) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15k — type 0x75 ground-cull walker body ═══════════════════
 *
 * Engine asm 0x43e767..0x43ea72 / decomp L38406-L38484.  Single-type body
 * (no other type shares this path).  Two phases driven by AGE.
 *
 * Phase 1 (AGE < 200) — "anchor rotor":
 *   LIFE_MULT = 0.5
 *   if AGE > 100: AGE--                     (decay back toward 100)
 *
 *   live_count    = number of slots with TYPE==0x75 AND AGE<200 (incl self)
 *   preceding_cnt = number of such slots strictly before self in scan order
 *
 *   if AGE >= 100 AND live_count > 6:        (phase 1→2 boost)
 *     AGE = 200
 *     angle = owner_a+0xea4                  (yaw stash)
 *     VEL_X = sinf(angle) * 0.24
 *     VEL_Y = 0.1
 *     VEL_Z = cosf(angle) * 0.24
 *     → fall through to LAB_0043ed87 tail (AGE-400 kill — won't fire here)
 *
 *   else:
 *     target = (preceding_cnt * 2π / live_count) + g_sim_frame_count * 0.04
 *     ROT_X = angle_step_toward(ROT_X, target, 0.08)
 *     ROT_SCR = 0; ROT_Z = 0
 *
 *     mat = T(0,0,0)
 *     mat = RotY(ROT_SCR) × mat              (no-op since ROT_SCR=0)
 *     mat = RotZ(ROT_Z)   × mat              (no-op since ROT_Z=0)
 *     mat = RotX(ROT_X)   × mat              (effective: mat = RotX)
 *     scale = min(AGE * 0.1, 3.0)
 *     mat = T(0, 0, scale) × mat             (final mat = T(0,0,scale)*RotX)
 *
 *     POS_X = mat[12] + owner_a.pos_x
 *     POS_Y = mat[13] + owner_a.pos_y + 2.1
 *     POS_Z = mat[14] + owner_a.pos_z
 *     → jmp 0x43ea5e (SM tail, skip phase 2 DRAG/ground)
 *
 * Phase 2 (AGE >= 200) — "ground-bounce settle":
 *   VEL_Y -= 0.01                            (gravity)
 *   if VEL_Y < 0:
 *     hit = ground_query(POS_X, POS_Y, POS_Z, &ground_y)
 *     if hit == 1 AND POS_Y <= ground_y + 0.3:
 *       POS_Y = ground_y + 1.0
 *       VEL_Y *= -0.5                        (bounce)
 *       bounce_count++   (slot[PART_IDX])
 *       if bounce_count == 3 → KILL          (LAB_004411e3)
 *   DRAG_VAR = 0.3
 *
 * Shared tail (both phases, label LAB_0043ea5e):
 *   sm_ret = state_machine(slot)
 *   if sm_ret != 0 → KILL
 *   else → fall to LAB_0043ed87 → AGE==400 kill check
 *
 * Notable engine quirks:
 *   - FUN_00482ae7 (angle_step_toward) takes 3 args (current, target, max_step)
 *     but Ghidra decomp drops the 3rd (max_step constant 0.08 from .rdata
 *     0x519894).  Ported inline below — pure math, no hook needed.
 *   - Matrix chain is sequenced with RotY/RotZ even though both inputs are
 *     forced to 0 in this body.  Ported faithfully for engine fidelity (and
 *     so the structure mirrors the cousin 0x6a body at +37 bytes).
 *   - bounce_count is stored in slot[PART_IDX] (the engine reuses this slot
 *     field as a bounce counter when the slot has no real "part index" use). */

/* Engine FUN_00482ae7 @ 0x482ae7 (348 B, pure math, no globals or hooks). */
static float angle_step_toward(float current, float target, float max_step)
{
    const float TAU = 6.2831855f;
    const float PI  = 3.1415927f;

    while (current < -PI) current += TAU;
    while (PI < current) current -= TAU;
    while (target  < -PI) target  += TAU;
    while (PI < target)  target  -= TAU;

    float delta = current - target;
    float abs_delta = delta < 0.0f ? -delta : delta;

    float result_pre_norm;
    if (abs_delta < max_step) {
        result_pre_norm = target;
    } else if (TAU - max_step < abs_delta) {
        result_pre_norm = target;
    } else if (target <= current) {
        /* delta = current - target ∈ [0, TAU); engine `if (fVar1 < PI)` → step back. */
        if (delta < PI) {
            result_pre_norm = current - max_step;
        } else {
            result_pre_norm = current + max_step;
        }
    } else if (PI <= (target - current)) {
        result_pre_norm = current - max_step;
    } else {
        result_pre_norm = current + max_step;
    }

    /* Renormalize result_pre_norm to [-PI, PI]. */
    float result = result_pre_norm;
    if (PI < result) result -= TAU;
    while (result < -PI) result += TAU;
    while (PI < result)  result -= TAU;
    return result;
}

static void body_0x75(int i)
{
    int32_t age = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age < 200) {
        /* Phase 1 — anchor rotor. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.5f);

        if (age > 100) {
            age = age - 1;
            slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE, age);
        }

        /* Live-cousin scan: count all slots with TYPE==0x75 AND AGE<200
         * (incl self), and snapshot the count taken so far when we visit
         * self.  Engine treats self as always-counted (we got here because
         * self.TYPE==0x75 + self.AGE<200). */
        int32_t total_count    = 0;
        int32_t preceding_cnt  = 0;
        for (int j = 0; j < SCENE1_RECORDS_B_COUNT; j++) {
            if (j == i) {
                preceding_cnt = total_count;
                total_count += 1;
                continue;
            }
            int32_t jt = slot_get_i(j, SCENE1_RECORDS_B_OFF_TYPE);
            if (jt != 0x75) continue;
            if (slot_get_i(j, SCENE1_RECORDS_B_OFF_AGE) >= 200) continue;
            total_count += 1;
        }

        if (age >= 100 && total_count > 6) {
            /* Phase 1→2 boost: latch velocity from owner yaw, jump AGE to 200. */
            const void *owner_a = slot_owner_a(i);
            float yaw = 0.0f;
            if (owner_a) {
                yaw = owner_read_f(owner_a, 0xea4);
            }
            slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE, 200);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(yaw) * 0.24f);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.1f);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(yaw) * 0.24f);
            /* Engine `jmp LAB_0043ed87` — skip phase 2 DRAG/ground entirely
             * this tick, fall straight to AGE==400 kill check (won't fire). */
            if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 400) {
                scene1_records_b_tick_kill_slot(i);
            }
            return;
        }

        /* Phase 1 angle-step + matrix-driven pose around owner_a. */
        float rot_x  = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
        float target = ((float)preceding_cnt * 6.2831855f) / (float)total_count
                     + (float)(int32_t)g_sim_frame_count * 0.04f;
        rot_x = angle_step_toward(rot_x, target, 0.08f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,   0.0f);

        float mat[16], scratch[16];
        mat4_translation(mat, 0.0f, 0.0f, 0.0f);
        mat4_rotation_y(scratch, 0.0f);     /* ROT_SCR = 0 */
        mat4_mul(mat, scratch, mat);
        mat4_rotation_z(scratch, 0.0f);     /* ROT_Z = 0 */
        mat4_mul(mat, scratch, mat);
        mat4_rotation_x(scratch, rot_x);
        mat4_mul(mat, scratch, mat);

        float scale = (float)slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) * 0.1f;
        if (scale > 3.0f) scale = 3.0f;
        mat4_translation(scratch, 0.0f, 0.0f, scale);
        mat4_mul(mat, scratch, mat);

        const void *owner_a = slot_owner_a(i);
        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        if (owner_a) {
            ox = owner_read_f(owner_a, 0x20);
            oy = owner_read_f(owner_a, 0x24);
            oz = owner_read_f(owner_a, 0x28);
        }
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, mat[12] + ox);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, mat[13] + oy + 2.1f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, mat[14] + oz);

        /* Engine `jmp 0x43ea5e` — skip phase 2 DRAG/ground, fall to SM. */
    } else {
        /* Phase 2 — ground-bounce settle. */
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.01f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy);

        if (vy < 0.0f) {
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            float gy = 0.0f;
            if (ground_query(px, py, pz, &gy) == 1) {
                /* Mirror engine: ground_y is reloaded from slot[AUX_9] (the
                 * scratch buffer the engine passes to ground_query) for the
                 * threshold compare.  Our hook returns out_y directly; write
                 * AUX_9 to preserve the engine's observable side-effect. */
                slot_set_f(i, SCENE1_RECORDS_B_OFF_AUX_9, gy);
                if (py <= gy + 0.3f) {
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, gy + 1.0f);
                    float new_vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) * -0.5f;
                    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, new_vy);
                    int32_t bc = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX) + 1;
                    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, bc);
                    if (bc == 3) {
                        scene1_records_b_tick_kill_slot(i);
                        return;
                    }
                }
            }
        }

        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.3f);
    }

    /* Shared tail (LAB_0043ea5e): state_machine call, ret!=0 kills, else
     * fall to LAB_0043ed87 AGE==400 kill check. */
    if (state_machine_call_ret(slot_base(i)) != 0) {
        scene1_records_b_tick_kill_slot(i);
        return;
    }
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 400) {
        scene1_records_b_tick_kill_slot(i);
    }
}

/* ═══ C8j-tick.15l — type 0x7c sister-hunting projectile body ═══════════
 *
 * Engine asm 0x43f700..0x43fae6 / decomp L39519-L39647.  Single-type
 * 2-phase body.  Looks like a thrown-with-trail projectile that searches
 * for a nearby "sister" NPC, latches a cooldown countdown, ground-bounces
 * on impact, then emits a fireworks-style 3-overlay burst on ground hit
 * before settling into a 25-tick DRAG=2.0 outro.
 *
 * Phase 2 (AGE > 200) — post-impact settle:
 *   AGE = AGE + 1
 *   DRAG = 2.0
 *   state_machine(slot)
 *   if AGE > 230 → KILL                    (LAB_0043f73a)
 *   → LAB_00440dc1 (default-tail wall-bounce body, deferred)
 *
 * Phase 1 (AGE <= 200) — flight + sister-hunt + ground bounce:
 *   hit_flag = 0
 *   if AGE < 0: cancel preamble pos += vel  (POS -= VEL)
 *
 *   if AGE > 5:
 *     sm_ret = state_machine(slot)
 *     if sm_ret != 0:
 *       emit overlay_spawn(NULL, POS, 0x3a, 2.25, -1, 0, 0, 0)
 *       hit_flag = 1
 *     else:
 *       POS_Y -= 0.5                       (engine probes ground at lower-Y)
 *       sm_ret2 = state_machine(slot)
 *       POS_Y += 0.5                       (restore — always, even when ret2==0)
 *       if sm_ret2 != 0:
 *         emit overlay_spawn(NULL, POS (with restored POS_Y), 0x3a, 2.25, ...)
 *         hit_flag = 1
 *
 *   if AGE == 1: scene1_spawn(0, POS, 0x70, 0.5, 1)   (one-shot birth particle)
 *
 *   - PART_IDX is reused as a sister-cooldown timer (engine quirk):
 *   if PART_IDX > 0: PART_IDX++
 *   if AGE > 0x14 AND PART_IDX < 0x28:
 *     AUX_SENT2 = aux_43ab6e(slot, 0.6, 0.03, 0.05, 0.96, AUX_SENT2)
 *   if AUX_SENT2 != -1 AND PART_IDX == 0:
 *     PART_IDX = 1                          (start cooldown)
 *
 *   - Ground impact (always queried, no VEL_Y < 0 gate).  Threshold uses
 *     +1.0 not +0.3 (vs body_0x75) — soft landing.
 *   if ground_query(POS, &gy) == 1:
 *     write slot[AUX_9] = gy                (mirror engine scratch-buffer)
 *   if POS_Y <= gy + 1.0:
 *     POS_Y = gy + 1.0
 *     VEL = (0, 0, 0)
 *     hit_flag = 1
 *
 *   if hit_flag:
 *     notify_queue(10, 4, 4, 1.0)           (post-impact "thunk" queue)
 *     se_play(0x148)
 *     emit overlay_spawn(NULL, POS, 0x2e, 0.8, -1, 0, 0, 0)
 *     emit overlay_spawn(NULL, POS, 0x44, 0.8, -1, 0, 0, 0)
 *     emit overlay_spawn(NULL, POS, 0x32, 0.8, -1, 0, 0, 0)
 *     AGE = 200                              (jump straight to Phase 2 outro
 *                                             next tick — kill window opens
 *                                             at AGE > 230)
 *
 *   - Velocity-trail spawn (iters = max(1, (int)|VEL|/0.1)):
 *   speed_sq = VEL.x² + VEL.y² + VEL.z²
 *   if speed_sq > 0:
 *     iters = max(1, (int)(sqrtf(speed_sq) / 0.1))
 *     for i in [0, iters):
 *       t = i / (float)iters
 *       trail_pos = POS - t * VEL
 *       emit overlay_spawn(NULL, trail_pos, 0x3a, 0.25, -1, 0, 0, 0)
 *
 *   if AGE == 0x82 (130): KILL              (mid-flight kill window)
 *   → LAB_00440dc1 (default-tail body, deferred)
 *
 * Constants verified via tools/analyze/pe.py (all read from .rdata):
 *   0x519314 = 2.0    (Phase 2 DRAG)
 *   0x51935c = 0.5    (POS_Y nudge magnitude)
 *   0x519c0c = 2.25   (SM-emit overlay scale)
 *   0x519364 = 1.0    (ground threshold + snap)
 *   0x519470 = 0.8    (post-impact 3-emit overlay scale)
 *   0x5193a0 = 0.1    (trail iters divisor — speed/0.1 = speed*10)
 *   0x519344 = 0.25   (trail overlay scale)
 *   0x51969c = 0.6, 0x519900 = 0.03, 0x5198f8 = 0.05, 0x519b04 = 0.96
 *                     (sister-search arg2..5)
 *
 * Asm corrections vs Ghidra decomp (raw asm 0x43f700..0x43fae6):
 *   - scene1_spawn at AGE==1 is 7-arg (decomp shows 6; missing param_7=1).
 *   - All 5 overlay_spawn calls in this body are 9-arg with the 8th arg
 *     (override_rot_y_bits) = 0 and shape_mode/mode = 0; Ghidra dropped
 *     the trailing args in places.
 *   - notify_queue is 4-arg (a=10, b=4, c=4, d=1.0); decomp shows 3-arg
 *     in places — fld1 at 0x43f923 confirms the 4th float arg.
 *   - SE 0x148 call is single-arg; the bare `FUN_00499519()` in decomp
 *     L2658 hides the `push 0x148`.
 *   - FUN_0043ab6e is 6-arg `(slot, f1, f2, f3, f4, int)` returning int.
 *     Decomp shows `uVar9 = FUN_0043ab6e()` argless; raw asm at 0x43f88c
 *     shows 5 push instructions before the call + the asm 0x43f863..0x43f88b
 *     load sequence pushes 0.6/0.03/0.05/0.96 as the 4 floats and
 *     `[esi+0x114]` (slot[AUX_SENT2]) as the int.
 *   - PART_IDX (dw 39, byte +0x9c) is repurposed as a sister-cooldown
 *     timer — not a "particle index" in this body.  Engine pattern: when
 *     a sister is found (AUX_SENT2 != -1) and the timer is dormant
 *     (PART_IDX == 0), seed it to 1; thereafter increment every tick.
 *     The search window (AGE > 0x14 AND PART_IDX < 0x28) means the
 *     cooldown gives ~40 ticks of sister-tracking before re-searching.
 *
 * Engine quirks / cross-body parity:
 *   - The ground threshold is +1.0 (matches body_0x75 Phase 2 snap), but
 *     this body lacks the body_0x75 +0.3 "impact gate" — every tick that
 *     POS_Y is at-or-below ground+1.0 latches the impact (cascading
 *     notify+SE+3-emit then AGE→200).  In practice the body kills itself
 *     at AGE==130 well before the impact window stays open multiple ticks.
 *   - LAB_00440dc1 (the default-tail wall-bounce body, asm 0x440dc1..
 *     0x4411e3, 1058 B) is unported.  Both phases of body_0x7c end with
 *     a `jmp LAB_00440dc1` in the engine — our port simply returns,
 *     skipping any wall-bounce post-processing.  Observable effect:
 *     production HOUSE-state slots that touch a wall while AGE in
 *     [-N, 200] won't bounce off it.  Not a regression today since type
 *     0x7c isn't allocated by HOUSE-state callers.  PHC #25 tracks.
 */
static void body_0x7c(int i)
{
    int32_t age_initial = slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE);

    if (age_initial > 0xc8) {
        /* Phase 2 — DRAG=2.0 settle, kill at AGE>230. */
        slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE, age_initial + 1);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 2.0f);
        state_machine_call(slot_base(i));
        if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) > 0xe6) {
            scene1_records_b_tick_kill_slot(i);
        }
        /* engine `jmp LAB_00440dc1` — default-tail body unported. */
        return;
    }

    /* Phase 1 (AGE <= 200).  Track hit_flag locally; engine reuses
     * [ebp-0x8] for both the SM-emit-happened sentinel AND the ground-
     * impact sentinel — semantically the same "fire post-impact block". */
    int hit_flag = 0;

    /* AGE < 0: cancel preamble pos += vel.  Engine 0x43f74a-0x43f762. */
    if (age_initial < 0) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py - vy);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - vz);
    }

    /* AGE > 5: SM-driven optional 0x3a spawn.  Engine 0x43f765-0x43f7fd.
     * Re-reads AGE from slot (cached age_initial unchanged at this point
     * but engine pattern is to re-read).  Two SM call sites — second one
     * at POS_Y-0.5 to probe a lower hit. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) > 5) {
        int sm_ret = state_machine_call_ret(slot_base(i));
        int should_emit = 0;
        float emit_y = 0.0f;
        if (sm_ret != 0) {
            should_emit = 1;
            emit_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        } else {
            float py_orig = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py_orig - 0.5f);
            int sm_ret2 = state_machine_call_ret(slot_base(i));
            /* Engine restores POS_Y unconditionally (asm 0x43f7b1-0x43f7c3
             * runs before the je-skip check). */
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py_orig);
            if (sm_ret2 != 0) {
                should_emit = 1;
                emit_y = py_orig;
            }
        }
        if (should_emit) {
            float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            overlay_spawn(NULL, px, emit_y, pz,
                          /*tid=*/0x3a, /*scale=*/2.25f,
                          /*dur=*/-1, /*rot_y=*/0,
                          /*shape_mode=*/0, /*mode=*/0);
            hit_flag = 1;
        }
    }

    /* AGE == 1: one-shot 0x70 birth spawn (asm 0x43f804-0x43f837). */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 1) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        scene1_spawn(0, px, py, pz, 0x70, 0.5f, 1);
    }

    /* PART_IDX-as-cooldown timer (asm 0x43f83a-0x43f845). */
    int32_t timer = slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX);
    if (timer > 0) {
        slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, timer + 1);
    }

    /* Sister-search (asm 0x43f84b-0x43f894). */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) > 0x14 &&
        slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX) < 0x28) {
        int32_t old = slot_get_i(i, SCENE1_RECORDS_B_OFF_AUX_SENT2);
        int32_t found = aux_43ab6e_call(slot_base(i),
                                        0.6f, 0.03f, 0.05f, 0.96f,
                                        old);
        slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_SENT2, found);
    }

    /* Sister-found-and-cooldown-dormant: seed cooldown (asm 0x43f89a-0x43f8b6). */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AUX_SENT2) != -1 &&
        slot_get_i(i, SCENE1_RECORDS_B_OFF_PART_IDX) == 0) {
        slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    }

    /* Ground query — unconditional (no VEL_Y < 0 gate, unlike body_0x75).
     * Engine 0x43f8b6-0x43f917.  out_y from hook used both as the impact
     * threshold (+1.0) and to write slot[AUX_9] mirroring engine's
     * `lea eax,[esi+0x18]` 4-float scratch buffer with ground_y at
     * buffer[3] = slot[AUX_9]. */
    {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float gy = 0.0f;
        if (ground_query(px, py, pz, &gy) == 1) {
            slot_set_f(i, SCENE1_RECORDS_B_OFF_AUX_9, gy);
        } else {
            gy = 0.0f;
        }
        float threshold = gy + 1.0f;
        if (py <= threshold) {
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, threshold);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);
            hit_flag = 1;
        }
    }

    /* Post-impact burst (asm 0x43f923-0x43f9da). */
    if (hit_flag) {
        notify_queue_call(10, 4, 4, 1.0f);
        se_play(0x148);
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        overlay_spawn(NULL, px, py, pz, 0x2e, 0.8f, -1, 0, 0, 0);
        overlay_spawn(NULL, px, py, pz, 0x44, 0.8f, -1, 0, 0, 0);
        overlay_spawn(NULL, px, py, pz, 0x32, 0.8f, -1, 0, 0, 0);
        slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE, 0xc8);
    }

    /* Velocity-driven trail spawn (asm 0x43f9e4-0x43facd).  Engine
     * re-reads VEL from slot — so the post-impact VEL=0 zeros speed_sq
     * and the trail loop short-circuits. */
    {
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        float speed_sq = vx * vx + vy * vy + vz * vz;
        if (speed_sq > 0.0f) {
            float speed = sqrtf(speed_sq);
            /* Engine `fdiv 0x5193a0` (= speed / 0.1) then `__ftol`. */
            int iters = (int)(speed / 0.1f);
            if (iters < 1) iters = 1;
            float trail_px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
            float trail_py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
            float trail_pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
            for (int k = 0; k < iters; k++) {
                float t = (float)k / (float)iters;
                overlay_spawn(NULL,
                              trail_px - t * vx,
                              trail_py - t * vy,
                              trail_pz - t * vz,
                              0x3a, 0.25f, -1, 0, 0, 0);
            }
        }
    }

    /* Tail kill check (asm 0x43fad0-0x43fae2): AGE == 130 → KILL. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) == 0x82) {
        scene1_records_b_tick_kill_slot(i);
    }
    /* engine `jmp LAB_00440dc1` (direct, asm 0x43fae2) — wall-bounce tail.
     * body_0x7c does NOT set the per-tick flag itself; this call is a
     * no-op in production unless an upstream helper set the flag earlier
     * in the same tick.  Wired for engine fidelity. */
    scene1_records_b_run_lab_00440dc1(i);
}

/* ─── C8j-tick.16 — LAB_00440dc1 default-tail wall-bounce body ──────────
 *
 * Engine asm 0x440dc1..0x4412b1 (~1264 B).  The last unported body in the
 * FUN_0043ae20 cascade.  Runs as a shared wall-bounce post-processing
 * tail after most per-type bodies via fall-through `jmp LAB_00440dc1`,
 * AND as the dispatch `default:` arm for types absent from the cascade.
 *
 * Three-gate prologue: TYPE != 0 (slot alive), slot[AUX_C8] != 0 (per-slot
 * "wall bounce enabled" flag), and `g_scene1_records_b_tick_flag != 0`
 * (DAT_06a46f98 — per-tick "side effect happened" flag set by an earlier
 * helper such as a ground-bounce body).  Both gates are BSS-zero in
 * default tests, so the body is unreachable in production HOUSE state.
 *
 * Two paths after the prologue:
 *   - Path A (OWNER_A != 0): wall raycast from "back-stepped" origin
 *     (pos - 0.2*vel) with vel as direction.  Type 0x58 uses a different
 *     origin computation involving (age-6).
 *   - Path B (OWNER_A == 0): wall raycast from slot.POS with slot.VEL.
 *     Simpler TYPE response (only 0xa0 and 0x1f have special branches).
 *
 * On hit, the body reads the wall record flag (via engine table at
 * DAT_007ca434 with stride 0x98 × 0x2f8020; modeled as a hook) and one
 * of three outcomes:
 *   - Flag != 0 AND != 1: ignore (skip to next slot).
 *   - Flag is 0 or 1, wall_id > 0: decrement g_wall_lifetime[wall_id];
 *     when it hits zero, call wall_destroy (FUN_0042353c).  Otherwise
 *     play SE 0x169 and spawn a Table-A particle at the back-step pos.
 *     Kill the slot.
 *   - Flag is 0 or 1, wall_id == 0: per-TYPE bounce response (see below).
 *
 * Per-TYPE bounce responses (Path A only):
 *   - 0x2/0x54/0x3/0x4/0x22/0x67/0x6d/0x6e/0x6f/0x70 → LAB_0044117a:
 *     scene1_spawn(owner_a, hit_pos, 0x29, 0.2, 1) + same for 0x2a +
 *     SE 0x167 + FUN_0044b255 + KILL.
 *   - 0x72 → inline same scene1_spawn pair + SE 0x167; NO kill,
 *     NO FUN_0044b255 — slot continues for next tick.
 *   - 0x5b/0x5c/0x5f/0x85/0x86/0x87 → SE 0x29e + default-particle path.
 *   - 0x4d/0x4e/0x4f/0x50/0xa5/0xa6 → SE 0x2b0 + default-particle path.
 *   - 0x78 → overlay_spawn(owner_a, slot.pos, 0x14, scale 0.8) + 0x44b255 + KILL.
 *   - 0x7a → overlay_spawn(owner_a, slot.pos, 0x14, scale 1.0) + 0x44b255 + KILL.
 *   - all other types → default-particle path:
 *       scene1_pfo_table_a_alloc_passthrough(owner_a, hit_pos, 1, 0.3,
 *                                            -1, 0.0, 0) + 0x44b255 + KILL.
 *
 * Path B specials:
 *   - 0xa0 → scene1_overlay_spawn(NULL, slot.pos, 0x14, 0.8, -1, 0.0, 0).
 *   - 0x1f → slot[AGE] = 0x70 (reset AGE to 0x70).
 *   - other types → no action (just falls through to next slot).
 *
 * Constants verified via tools/analyze/pe.py:
 *   0x5198d8 = 0.2     (back-step ray origin scale; also overlay_spawn arg)
 *   0x5194ec = 0.3     (default particle scale; also 0x58 mid-point scale)
 *   0x519470 = 0.8     (0x78 overlay scale)
 *   ds:0x519364 = 1.0  (loaded via fld1 for 0x7a)
 *
 * Engine FUN_00433674 wall raycast (PHC #13): 8 args — 6 floats + 2 out
 * pointers (out_t float + out_buffer with wall_x/wall_z/wall_id at byte
 * offsets +0x10/+0x14/+0x18).  Hook returns 1 on hit, 0 on miss.
 *
 * Engine FUN_0042353c wall destroy: 1 arg (wall_id - 1).  Engine plays
 * SE 0x13e and writes per-wall globals.
 *
 * Engine FUN_0044b255 at 0x44b255 is `ret` (no-op leftover); hook exists
 * for test observability only.
 */
void scene1_records_b_run_lab_00440dc1(int i);
static void body_lab_00440dc1(int i) { scene1_records_b_run_lab_00440dc1(i); }
void scene1_records_b_run_lab_00440dc1(int i)
{
    int32_t type = slot_get_i(i, SCENE1_RECORDS_B_OFF_TYPE);
    if (type == 0) return;                   /* 0x440dc1: slot dead */
    /* 0x440dcb: slot[+0xc4] == AUX_C8 == OFF 49.  BSS-zero default
     * → body skipped unless an earlier helper enabled it. */
    if (slot_get_i(i, SCENE1_RECORDS_B_OFF_AUX_C8) == 0) return;
    if (g_scene1_records_b_tick_flag == 0) return;  /* 0x440dd8 */

    /* OWNER_A is stored as an int (engine `mov DWORD PTR [esi+0x10]`);
     * scene1_spawn / scene1_pfo_table_a_alloc_passthrough both consume
     * it as int.  overlay_spawn wants `const void *` so we cast at the
     * call site below. */
    int32_t owner_a = slot_get_i(i, SCENE1_RECORDS_B_OFF_OWNER_A);

    if (owner_a == 0) {
        /* ── Path B: 0x4411ea — OWNER_A == 0, simpler raycast/response.
         *
         * Engine raycasts from current slot.POS with current slot.VEL
         * (no back-step), reads the same wall record flag, then for
         * TYPE 0xa0 spawns a different overlay; TYPE 0x1f resets AGE
         * to 0x70.  No other TYPEs take any action in this path. */
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

        scene1_b_wall_ray_result_t ray;
        if (!wall_raycast_call(px, py, pz, vx, vy, vz, &ray)) return;

        int flag = wall_flag_at_call(ray.wall_x, ray.wall_z);
        if (flag != 0 && flag != 1) return;     /* 0x44125b */

        if (type == 0xa0) {
            /* 0x441269-0x44129b: scene1_overlay_spawn(NULL, slot.pos,
             * 0x14, 0.8, -1, 0.0, shape_mode=0, mode=0). */
            overlay_spawn(NULL,
                          slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X),
                          slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y),
                          slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z),
                          0x14, 0.8f, -1, 0, 0, 0);
        }
        if (type == 0x1f) {
            slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE, 0x70);
        }
        return;
    }

    /* ── Path A: 0x440def — OWNER_A != 0.
     *
     * Ray origin = pos - 0.2 * vel (back-step); ray dir = vel. */
    float ox = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X)
             - slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X) * 0.2f;
    float oy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y)
             - slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y) * 0.2f;
    float oz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z)
             - slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z) * 0.2f;
    float dx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
    float dy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
    float dz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);

    if (type == 0x58) {
        /* 0x440e33-0x440e87: type-0x58 substitutes (age-6) * vel * 0.3
         * + (pos - vel) for the ray origin. */
        float f = (float)(slot_get_i(i, SCENE1_RECORDS_B_OFF_AGE) - 6);
        float vxc = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
        float vyc = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
        float vzc = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
        ox = (slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X) - vxc)
           + f * vxc * 0.3f;
        oy = (slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y) - vyc)
           + f * vyc * 0.3f;
        oz = (slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z) - vzc)
           + f * vzc * 0.3f;
    }

    scene1_b_wall_ray_result_t ray;
    if (!wall_raycast_call(ox, oy, oz, dx, dy, dz, &ray)) return;

    /* Wall record flag gate: flag must be 0 or 1 to bounce; otherwise skip. */
    int flag = wall_flag_at_call(ray.wall_x, ray.wall_z);
    if (flag != 1 && flag != 0) return;     /* 0x440efb-0x440f01 */

    if (ray.wall_id > 0) {
        /* 0x440f03-0x440f7d: lifetime tracking + destroy-on-zero.
         *
         * Engine: if g_wall_lifetime[wall_id] < 0x64 → set
         * g_wall_freshness[wall_id] = 0x1e + decrement lifetime.  Then
         * if lifetime hit 0 → wall_destroy(wall_id - 1) + KILL.  Else
         * SE 0x169 + Table-A particle at back-step pos + KILL. */
        int wid = ray.wall_id;
        if (wid > 0 && wid < (int)(sizeof g_scene1_b_wall_lifetime
                                  / sizeof g_scene1_b_wall_lifetime[0])) {
            if (g_scene1_b_wall_lifetime[wid] < 0x64) {
                g_scene1_b_wall_freshness[wid] = 0x1e;
                g_scene1_b_wall_lifetime[wid]--;
            }
            if (g_scene1_b_wall_lifetime[wid] == 0) {
                wall_destroy_call(wid - 1);
                scene1_records_b_tick_kill_slot(i);
                return;
            }
        }
        se_play(0x169);
        scene1_pfo_table_a_alloc_passthrough(
            owner_a, ox, oy, oz, 1, 0.3f, -1, 0.0f, 0);
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* wall_id == 0: per-TYPE bounce particle response.
     * hit_pos = back_step_origin + out_t * direction. */
    float hit_x = ox + ray.t * dx;
    float hit_y = oy + ray.t * dy;
    float hit_z = oz + ray.t * dz;

    /* TYPEs in {0x2, 0x54, 0x3, 0x4, 0x22, 0x67, 0x6d, 0x6e, 0x6f, 0x70}
     * → LAB_0044117a: scene1_spawn pair (0x29 + 0x2a, scale 0.2) +
     * SE 0x167 + FUN_0044b255 + KILL. */
    if (type == 0x2 || type == 0x54 || type == 0x3 || type == 0x4
        || type == 0x22 || type == 0x67 || type == 0x6d
        || type == 0x6e || type == 0x6f || type == 0x70) {
        scene1_spawn(owner_a, hit_x, hit_y, hit_z, 0x29, 0.2f, 1);
        scene1_spawn(owner_a, hit_x, hit_y, hit_z, 0x2a, 0.2f, 1);
        se_play(0x167);
        aux_44b255_call();
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* TYPE 0x72 → same scene1_spawn pair + SE 0x167; NO kill, NO
     * aux_44b255 (asm 0x44106b: jmp 0x43fbbc — straight to next slot). */
    if (type == 0x72) {
        scene1_spawn(owner_a, hit_x, hit_y, hit_z, 0x29, 0.2f, 1);
        scene1_spawn(owner_a, hit_x, hit_y, hit_z, 0x2a, 0.2f, 1);
        se_play(0x167);
        return;
    }

    /* TYPE 0x5b/0x5c/0x5f/0x85/0x86/0x87 → SE 0x29e + default-particle. */
    int default_se = 0;
    if (type == 0x5b || type == 0x5c || type == 0x5f
        || type == 0x85 || type == 0x86 || type == 0x87) {
        default_se = 0x29e;
    }
    /* TYPE 0x4d/0x4e/0x4f/0x50/0xa5/0xa6 → SE 0x2b0 + default-particle. */
    if (type == 0x4d || type == 0x4e || type == 0x4f
        || type == 0x50 || type == 0xa5 || type == 0xa6) {
        default_se = 0x2b0;
    }

    /* TYPE 0x78 / 0x7a → overlay_spawn at slot.pos (not hit_pos).
     * 0x78: scale 0.8; 0x7a: scale 1.0.  Then 0x44b255 + KILL. */
    if (type == 0x78 || type == 0x7a) {
        float scale = (type == 0x78) ? 0.8f : 1.0f;
        overlay_spawn((const void *)(intptr_t)owner_a,
                      slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X),
                      slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y),
                      slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z),
                      0x14, scale, -1, 0, 0, 0);
        aux_44b255_call();
        scene1_records_b_tick_kill_slot(i);
        return;
    }

    /* Default-particle path (LAB_0044112d).  Fires the SE if one was
     * latched by an SE-prefix branch (0x29e or 0x2b0), then spawns a
     * Table-A particle at hit_pos with template 1, scale 0.3, dur -1. */
    if (default_se) se_play((uint16_t)default_se);
    scene1_pfo_table_a_alloc_passthrough(
        owner_a, hit_x, hit_y, hit_z, 1, 0.3f, -1, 0.0f, 0);
    aux_44b255_call();
    scene1_records_b_tick_kill_slot(i);
}

/* ─── default dispatch ───────────────────────────────────────────────── */

static void dispatch_default(int slot_idx, int32_t type)
{
    /* C8j-tick.2 — anchor cascade.  Per the engine's nested if/goto
     * cascade at L36464-L36732, types 0x2f/0x88/0x9a share a body and
     * 0x1e takes the else branch; 0x89/0x9e are dispatched separately
     * at LAB_0043b325.
     *
     * C8j-tick.3 — mid-cascade.  Types 0x9c (NPC shoulder-arc), 0x34
     * (NPC joint-target lerp), 0x68 (three-phase NPC spawn cycle), 0x74
     * + 0x79 (shared entity ground-cull walker), 0x69 (entity self-spawn
     * then die).
     *
     * C8j-tick.4 — Body 1 (L689-L812).  Types {2, 3, 4, 0x22, 0x54, 0x67,
     * 0x6d, 0x6e, 0x6f, 0x70} — kill-on-ground + bounce particles with
     * owner_a anchored pose + per-type DRAG + state-machine loop. */
    switch (type) {
    case 0x2f:
    case 0x88:
    case 0x9a:
        body_anchor_joint(slot_idx, type);
        break;
    case 0x1e:
        body_0x1e(slot_idx);
        break;
    case 0x89:
    case 0x9e:
        body_compass(slot_idx, type);
        break;
    case 0x9c:
        body_0x9c(slot_idx);
        break;
    case 0x34:
        body_0x34(slot_idx);
        break;
    case 0x68:
        body_0x68(slot_idx);
        break;
    case 0x74:
    case 0x79:
        body_0x74_or_0x79(slot_idx, type);
        break;
    case 0x69:
        body_0x69(slot_idx);
        break;
    case 2:
    case 3:
    case 4:
    case 0x22:
    case 0x54:
    case 0x67:
    case 0x6d:
    case 0x6e:
    case 0x6f:
    case 0x70:
        body_kill_bounce(slot_idx, type);
        break;
    case 0x85:
        body_0x85(slot_idx);
        break;
    case 0x8a:
    case 0x8b:
        body_0x8a_or_0x8b(slot_idx, type);
        break;
    case 0x5b:
    case 0x5c:
    case 0x5e:
    case 0x86:
    case 0x87:
        body_0x5b_group(slot_idx, type);
        break;
    case 0x71:
    case 0x72:
    case 0x7d:
        body_0x71_72_7d(slot_idx, type);
        break;
    case 0x1f:
        body_0x1f(slot_idx);
        break;
    case 0x5a:
    case 0x98:
        body_0x5a_or_0x98(slot_idx);
        break;
    case 0x6c:
        body_0x6c(slot_idx);
        break;
    case 0x6b:
        body_0x6b(slot_idx);
        break;
    case 0x28:
        body_0x28(slot_idx);
        break;
    case 0x38:
        body_0x38(slot_idx);
        break;
    case 0x29:
        body_0x29(slot_idx);
        break;
    case 0x2c:
        body_0x2c(slot_idx);
        break;
    case 0x8c:
        body_0x8c(slot_idx);
        break;
    case 0x23:
        body_0x23(slot_idx);
        break;
    case 0x3a:
        body_0x3a(slot_idx);
        break;
    case 0x3c:
        body_0x3c(slot_idx);
        break;
    case 0x3b:
        body_0x3b(slot_idx);
        break;
    case 0x21:
    case 0x25:
    case 0x31:
    case 0x32:
        body_021_to_032(slot_idx, type);
        break;
    case 0x2b:
        body_0x2b(slot_idx);
        break;
    case 0x26:
    case 0x2a:
        body_0x26_or_0x2a(slot_idx);
        break;
    case 0x27:
        body_0x27(slot_idx);
        break;
    /* C8j-tick.10 — Body 6 + Body 7 (motion-table anchor). */
    case 0x10:
    case 0xb:
    case 0x14:
    case 0x13:
    case 0x99:
        body_motion_anchor_keep(slot_idx, type);
        break;
    case 0x11:
    case 0xc:
        body_motion_anchor_kill_7(slot_idx, type);
        break;
    /* C8j-tick.11 — Body 7a (first FUN_0043865e dispatch cluster). */
    case 0x46:
        body_0x46(slot_idx);
        break;
    case 0x97:
        body_0x97(slot_idx);
        break;
    case 0xe:
    case 0x12:
        body_0xe_or_0x12(slot_idx);
        break;
    case 0xd:
    case 0x15:
        body_0xd_or_0x15(slot_idx, type);
        break;
    /* C8j-tick.12 — Body 7b head (motion-gated walker + 0x9b big body + 0x24). */
    case 0xf:
        body_0xf_motion_walker(slot_idx);
        break;
    case 0x9b:
        body_0x9b(slot_idx);
        break;
    case 0x24:
        body_0x24(slot_idx);
        break;
    /* C8j-tick.13 — type 0x53 (drift-damping body). */
    case 0x53:
        body_0x53(slot_idx);
        break;
    /* C8j-tick.14 — type 0x58 / 0x66 shared anchor-rotor body. */
    case 0x58:
    case 0x66:
        body_0x58_or_0x66(slot_idx, type);
        break;
    /* C8j-tick.15a — trivial tail bodies (0x33 / 0x60 / 0x65). */
    case 0x33:
        body_0x33(slot_idx);
        break;
    case 0x60:
        body_0x60(slot_idx);
        break;
    case 0x65:
        body_0x65(slot_idx);
        break;
    /* C8j-tick.15b — owner-anchored 5f/3e shared + 0x82 lerp. */
    case 0x5f:
    case 0x3e:
        body_0x5f_or_0x3e(slot_idx, type);
        break;
    case 0x82:
        body_0x82(slot_idx);
        break;
    /* C8j-tick.15c — three-type ground-bounce shared body. */
    case 0x7b:
    case 0xa1:
    case 0xa4:
        body_0x7b_a1_a4(slot_idx, type);
        break;
    /* C8j-tick.15d — single-type 0x84 ground-bounce + Table A spawn + kill. */
    case 0x84:
        body_0x84(slot_idx);
        break;
    /* C8j-tick.15e — three-type trail-emit + cull + state-machine body. */
    case 0x73:
    case 0x78:
    case 0x7a:
        body_0x73_or_0x78_or_0x7a(slot_idx);
        break;
    /* C8j-tick.15f — paired owner-driven tail bodies. */
    case 0x76:
    case 0xa3:
        body_0x76_or_0xa3(slot_idx, type);
        break;
    case 0x77:
    case 0xa2:
        body_0x77_or_0xa2(slot_idx, type);
        break;
    /* C8j-tick.15g — player-homing damped drift body. */
    case 0x2e:
    case 0x36:
        body_0x2e_or_0x36(slot_idx, type);
        break;
    /* C8j-tick.15h — types 0xa0 + 0x7e cull-tail variants. */
    case 0xa0:
        body_0xa0(slot_idx);
        break;
    case 0x7e:
        body_0x7e(slot_idx);
        break;
    case 0x4d:
    case 0x4e:
    case 0x4f:
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x56:
    case 0x62:
    case 99:
    case 0x96:
    case 0xa5:
    case 0xa6:
        body_entity_bounce(slot_idx, type);
        break;
    case 0x83:
        body_0x83(slot_idx);
        break;
    /* C8j-tick.15k — type 0x75 ground-cull walker. */
    case 0x75:
        body_0x75(slot_idx);
        break;
    /* C8j-tick.15l — type 0x7c sister-hunting projectile. */
    case 0x7c:
        body_0x7c(slot_idx);
        break;
    default:
        /* LAB_00440dc1 default-tail wall-bounce body (C8j-tick.16).
         * Heavily gated (TYPE != 0 + slot[AUX_C8] != 0 + per-tick flag);
         * BSS-zero defaults make it unreachable in HOUSE production.  See
         * body_lab_00440dc1 above for the full asm trace. */
        body_lab_00440dc1(slot_idx);
        break;
    }
}
