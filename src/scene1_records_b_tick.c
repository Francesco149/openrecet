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
 *   C8j-tick.4  Body 1 (2/3/4/0x22/0x54/0x67/0x6d/0x6e/0x6f/0x70) ← THIS CHIP
 *   C8j-tick.5..13  remaining clusters per the survey
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

int32_t g_scene1_records_b_tick_flag;        /* engine DAT_06a46f98 */
int32_t g_scene1_records_b_tick_anim_drive;  /* engine DAT_06a46f94 */

/* Engine DAT_005c2434/8/c — 256-entry per-NPC-motion-style table read by
 * C8j-tick.10 (Body 6 + Body 7) and presumably deeper bodies.  PHC #19;
 * default BSS-zero. */
scene1_b_motion_entry_t g_scene1_b_motion_table[256];

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
    default:
        /* Remaining tail types (0x58/0x66/0x75/0x83/0x84/0x87/0xa0..0xa6
         * and LAB_0043f39b death-effect spawn) are deferred to a future
         * sub-chip. */
        break;
    }
}
