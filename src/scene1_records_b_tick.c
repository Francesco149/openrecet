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
 *   C8j-tick.2  0x1e/0x2f/0x88/0x9a + 0x89/0x9e   ← THIS CHIP
 *   C8j-tick.3  mid-cascade (0x9c/0x34/0x69/0x74/0x79/0x68) [TODO]
 *   C8j-tick.4..13  remaining clusters per the survey
 *
 * Slot dead sentinel: `slot[0] == 0`.
 */

#include "scene1_records_b_tick.h"

#include <math.h>
#include <string.h>

#include "rng.h"
#include "scene1_overlay.h"
#include "scene1_records.h"
#include "scene1_records_b_spawn.h"

int32_t g_scene1_records_b_tick_flag;  /* engine DAT_06a46f98 */

/* ─── hooks ──────────────────────────────────────────────────────────── */

static void dispatch_default(int slot_idx, int32_t type);

static scene1_b_per_type_body_fn g_per_type_body     = dispatch_default;
static scene1_b_state_machine_fn g_state_machine_hook;   /* default NULL */
static scene1_b_se_fn            g_se_hook;              /* default NULL */
static scene1_b_cull_query_fn    g_cull_query_hook;      /* default NULL = "visible" */

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

static inline void se_play(uint16_t id)
{
    if (g_se_hook) g_se_hook(id);
}

static inline void state_machine_call(int32_t *slot)
{
    if (g_state_machine_hook) g_state_machine_hook(slot);
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
     * then die). */
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
    default:
        /* C8j-tick.4..13 fill in additional cases here. */
        break;
    }
}
