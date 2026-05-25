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

#include "rng.h"
#include "scene1_overlay.h"
#include "scene1_particles_tick.h"
#include "scene1_per_frame_open.h"
#include "scene1_records.h"
#include "scene1_records_b_spawn.h"
#include "scene1_spawn.h"

int32_t g_scene1_records_b_tick_flag;        /* engine DAT_06a46f98 */
int32_t g_scene1_records_b_tick_anim_drive;  /* engine DAT_06a46f94 */

/* ─── hooks ──────────────────────────────────────────────────────────── */

static void dispatch_default(int slot_idx, int32_t type);

static scene1_b_per_type_body_fn g_per_type_body     = dispatch_default;
static scene1_b_state_machine_fn g_state_machine_hook;   /* default NULL */
static scene1_b_se_fn            g_se_hook;              /* default NULL */
static scene1_b_cull_query_fn    g_cull_query_hook;      /* default NULL = "visible" */
static scene1_b_aux_1arg_fn      g_aux_485979_hook;      /* default NULL */
static scene1_b_aux_2arg_fn      g_aux_482a51_hook;      /* default NULL */
static scene1_b_notify_queue_fn  g_notify_queue_hook;    /* default NULL */

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
        scene1_overlay_spawn(NULL,
                             r_sa * 3.0f + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X),
                             spawn_y,
                             r_ca * 3.0f + slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z),
                             0xf, local_c * 0.8f, -1, 0, 0, 0);

        /* Spawn 2 uses the OUTER (owner-compass) angle, not a fresh
         * random — asm reuses QWORD [ebp-0x10] from L36656. */
        scene1_overlay_spawn(NULL,
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
        scene1_overlay_spawn(owner, ox, oy, oz, 0x6a, scale, -1, 0, 6, 0);
        scene1_overlay_spawn(owner, ox, oy, oz, 0x6e, scale, -1, 0, 6, 0);
        if (age % 3 == 0) {
            scene1_overlay_spawn(owner, ox, oy, oz, 0x6f, scale,
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
        scene1_overlay_spawn(owner_a, x, y, z, 5, 1.0f, 100, 0, 0, 0);
    }

    if (age == age_off + 0x28) {
        se_play(0x2a9);
        float x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X);
        float y = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y);
        float z = slot_get_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z);
        scene1_overlay_spawn(owner_a, x, y, z, 0xe, 0.2f, -1, 0, 0, 0);
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
        scene1_overlay_spawn(owner_a, x, y, z, 0, 0.8f, -1, 0, 0, 0);
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
        scene1_overlay_spawn(owner_a, px, py, pz, 0x11, 1.0f, -1,
                             rot_x_bits, 0, 0);
        if (type == 0x74) {
            scene1_overlay_spawn(owner_a, px, py, pz, 0x12, 1.0f, -1,
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

        scene1_overlay_spawn(owner_a, spawn_x, spawn_y, spawn_z, 0x2c,
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
        scene1_overlay_spawn(owner_a,
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
        scene1_overlay_spawn(NULL, px, py,        pz, 7,   1.5f, 0x1e, 0, 0, 0);
        scene1_overlay_spawn(NULL, px, py - 1.0f, pz, 0xb, 1.0f, -1,   0, 0, 0);
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
        scene1_overlay_spawn(NULL, px, py + 1.5f, pz, 7, 1.5f, 0x78, 0, 0, 0);
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
    default:
        /* C8j-tick.7..13 fill in additional cases here. */
        break;
    }
}
