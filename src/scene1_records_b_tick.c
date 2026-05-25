/*
 * scene1_records_b_tick.c — see header for chip writeup.
 *
 * Engine FUN_0043ae20 @ 0x43ae20 outer-loop skeleton (lines 36449-36462,
 * 39499-39507 of docs/decompiled/all.c).  Decomp:
 *
 *   local_2c = 0;
 *   do {
 *     iVar13  = local_2c * 0x124;
 *     piVar14 = &DAT_069324b0 + local_2c * 0x49;
 *     if (*piVar14 == 0) goto LAB_0043fbbc;     // skip dead
 *     DAT_06a46f98 = 0;
 *     slot[POS_X] += slot[VEL_X];
 *     slot[POS_Y] += slot[VEL_Y];
 *     slot[POS_Z] += slot[VEL_Z];
 *     slot[AGE]   += 1;
 *     iVar15 = *piVar14;
 *     // ... 86-way per-type dispatch ... (sub-chip work)
 *     // LAB_004411e3: *piVar14 = 0          // kill path (sub-chip work)
 *  LAB_0043fbbc:
 *     local_2c++;
 *     if (local_2c == 0x200) return;
 *   } while (true);
 *
 * The engine treats slot[0] == 0 as the dead sentinel — matches the
 * C8j allocator ladder's commit pattern (writes TYPE = type at the
 * end of the allocator preamble).
 */

#include "scene1_records_b_tick.h"

#include <string.h>

#include "scene1_records.h"

int32_t g_scene1_records_b_tick_flag;  /* engine DAT_06a46f98 */

/* ─── per-tick hooks ─────────────────────────────────────────────────── */

static scene1_b_per_type_body_fn g_per_type_body;
static scene1_b_state_machine_fn g_state_machine_hook;

scene1_b_per_type_body_fn scene1_records_b_set_per_type_body(
    scene1_b_per_type_body_fn fn)
{
    scene1_b_per_type_body_fn prev = g_per_type_body;
    g_per_type_body = fn;
    return prev;
}

scene1_b_state_machine_fn scene1_records_b_set_state_machine_hook(
    scene1_b_state_machine_fn fn)
{
    scene1_b_state_machine_fn prev = g_state_machine_hook;
    g_state_machine_hook = fn;
    return prev;
}

/* The state-machine hook (engine FUN_0043865e, PHC #20) is installed
 * via scene1_records_b_set_state_machine_hook but not invoked from the
 * skeleton — sub-chip C8j-tick.9 will be the first per-type body that
 * fires it from inside its dispatch. */

/* ─── slot accessors ─────────────────────────────────────────────────── */

static inline int32_t *slot_base(int i)
{
    return &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
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

        /* Per-type dispatch — C8j-tick.2+ chips fill the body table.
         * Default (no hook installed) is a no-op; the slot just sees
         * the preamble's position drift this tick. */
        if (g_per_type_body) {
            g_per_type_body(i, type);
        }
    }
}
