/*
 * scene1_records.c — see scene1_records.h for the chip writeup.
 *
 * Engine sources:
 *   - FUN_0040f64b @ 0x40f64b   (sentinel-init preamble, 3 tables)
 *   - FUN_00459dfd L51-L81      (per-pass active counter scan)
 */

#include "scene1_records.h"

#include "scene1_overlay.h"
#include "scene1_per_frame_open.h"

#include <string.h>

int32_t g_scene1_records_a[SCENE1_RECORDS_A_COUNT * SCENE1_RECORDS_A_STRIDE];
int32_t g_scene1_records_b[SCENE1_RECORDS_B_COUNT * SCENE1_RECORDS_B_STRIDE];
int32_t g_scene1_records_c[SCENE1_RECORDS_C_COUNT * SCENE1_RECORDS_C_STRIDE];

int g_scene1_records_a_count;
int g_scene1_records_b_count;
int g_scene1_records_c_count;

void scene1_records_reset(int reset_c)
{
    /* Table A — TYPE field at slot offset 12 (= 0x30 bytes from slot
     * base).  Engine writes DAT_069b2fb0 = -1 on every slot via the
     * preamble loop; DAT_069b2fb0 == &DAT_069b2f80 + 12 dw. */
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE
                           + SCENE1_RECORDS_A_OFF_TYPE] = -1;
    }

    /* Table C — TYPE at offset 10 dw (NOT 12 like table A).  See
     * scene1_records.h SCENE1_RECORDS_C_OFF_TYPE for the layout note.
     * Gated by reset_c so the "soft reset" call sites (param_1==0)
     * leave dungeon-side particle state alive across the reset.  HOUSE
     * entry passes 1. */
    if (reset_c) {
        for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
            g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE
                               + SCENE1_RECORDS_C_OFF_TYPE] = -1;
        }
    }

    /* Table B — [0]=0 sentinel, [2]=slot index for the engine's
     * slot-lookup-by-index path.  Different layout from A/C; slot[0] IS
     * the slot base. */
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        int32_t *r = &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
        r[0] = 0;
        r[2] = i;
    }

    /* PFO.1 + PFO.2 prereq — engine FUN_0040f64b L9183 calls
     * FUN_00414902 right after touching the three record tables above.
     * FUN_00414902 has TWO sentinel-init loops:
     *
     *   1. Overlay slots (engine `DAT_0064e890..0072a890` stepping 0x37
     *      dw) — sets `*piVar1 = -1` on every slot's ACTIVE field.
     *      Ported as `scene1_overlay_reset()`.
     *
     *   2. Table A spawn-request queue (`DAT_00730c30..00733830`
     *      stepping 0xb dw) — sets `*piVar1 = -1` on every slot's
     *      SENTINEL field.  Ported as `scene1_pfo_table_a_init()`.
     *
     * Both halves fire on every HOUSE entry; without the overlay reset
     * the slot table stays BSS-zero (ACTIVE=0 ≠ -1) and any consumer
     * of the slot table (overlay dispatcher, particle integrator's
     * Table B tick) would fire on every slot instead of skipping
     * empty ones. */
    scene1_overlay_reset();
    scene1_pfo_table_a_init();
}

void scene1_records_counter_scan(void)
{
    int count = 0;
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        if (g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE
                               + SCENE1_RECORDS_A_OFF_TYPE] != -1) {
            count = i + 1;
        }
    }
    g_scene1_records_a_count = count;

    count = 0;
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        if (g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE] != 0) {
            count = i + 1;
        }
    }
    g_scene1_records_b_count = count;

    count = 0;
    for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
        if (g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE
                               + SCENE1_RECORDS_C_OFF_TYPE] != -1) {
            count = i + 1;
        }
    }
    g_scene1_records_c_count = count;
}

void scene1_records_inject_test_type92(float pos_x, float pos_y,
                                       float pos_z)
{
    int32_t *r = &g_scene1_records_a[0 * SCENE1_RECORDS_A_STRIDE];

    /* Clear the slot so any leftover scratch from a prior session is
     * gone.  Then write only the fields Pass F (and our own injection
     * gate) actually reads. */
    memset(r, 0, SCENE1_RECORDS_A_STRIDE * sizeof(int32_t));

    /* Position (pos.x/y/z stored as float).  Pass F reads via
     * piVar11[-0xc..-0xa] (= slot+0..2). */
    *(float *)&r[SCENE1_RECORDS_A_OFF_POS_X] = pos_x;
    *(float *)&r[SCENE1_RECORDS_A_OFF_POS_Y] = pos_y;
    *(float *)&r[SCENE1_RECORDS_A_OFF_POS_Z] = pos_z;

    /* Rotation triad (rot.x/y/z stored as float).  Pass F reads via
     * piVar11[-6,-5,-4] (= slot+6..8).  Start at zero — the engine's
     * type-0x92 integrator increments these by 0.0157 (~π/200) per
     * tick; for a static MVP particle they stay zero, which yields an
     * identity rotation. */
    *(float *)&r[SCENE1_RECORDS_A_OFF_ROT_X] = 0.0f;
    *(float *)&r[SCENE1_RECORDS_A_OFF_ROT_Y] = 0.0f;
    *(float *)&r[SCENE1_RECORDS_A_OFF_ROT_Z] = 0.0f;

    /* TYPE = 0x92.  Pass F gate: `*piVar11 == 0x92`. */
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x92;

    /* AGE = 0.  Pass F gate: `-1 < piVar11[1]`. */
    r[SCENE1_RECORDS_A_OFF_AGE] = 0;

    /* Scale and param2 control the world-matrix size via
     *     final_scale = piVar11[5] / 200.0 * piVar11[2] * 0.005
     * with piVar11[5] = PARAM2 (int) and piVar11[2] = SCALE (float bits).
     * Pick piVar11[5] = 200 and piVar11[2] = 1.0f → final_scale = 0.005,
     * matching the engine's typical spawn (uVar5 % 100 + 100 → 100..199,
     * spawn param_6 typically 1.0f). */
    *(float *)&r[SCENE1_RECORDS_A_OFF_SCALE] = 1.0f;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 200;

    /* PARAM1 (DAT_069b2fc0) — read by the integrator's type-0x92 handler
     * as a per-particle drift phase; Pass F does not touch it.  Leave
     * zero. */

    /* Force the counter so scene1_pass_f_render's table walk sees a
     * non-empty range.  Real engine path calls
     * scene1_records_counter_scan once per frame; we bypass that since
     * nothing else is populating today. */
    if (g_scene1_records_a_count < 1) {
        g_scene1_records_a_count = 1;
    }
}
