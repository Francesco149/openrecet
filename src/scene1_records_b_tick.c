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

int32_t g_scene1_records_b_tick_flag;  /* engine DAT_06a46f98 */

/* ─── hooks ──────────────────────────────────────────────────────────── */

static void dispatch_default(int slot_idx, int32_t type);

static scene1_b_per_type_body_fn g_per_type_body     = dispatch_default;
static scene1_b_state_machine_fn g_state_machine_hook;   /* default NULL */
static scene1_b_se_fn            g_se_hook;              /* default NULL */

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

static inline void se_play(uint16_t id)
{
    if (g_se_hook) g_se_hook(id);
}

static inline void state_machine_call(int32_t *slot)
{
    if (g_state_machine_hook) g_state_machine_hook(slot);
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

/* ─── default dispatch ───────────────────────────────────────────────── */

static void dispatch_default(int slot_idx, int32_t type)
{
    /* C8j-tick.2 — anchor cascade.  Per the engine's nested if/goto
     * cascade at L36464-L36732, types 0x2f/0x88/0x9a share a body and
     * 0x1e takes the else branch; 0x89/0x9e are dispatched separately
     * at LAB_0043b325. */
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
    default:
        /* C8j-tick.3..13 fill in additional cases here. */
        break;
    }
}
