/*
 * scene1_records.c — see scene1_records.h for the chip writeup.
 *
 * Engine sources:
 *   - FUN_0040f64b @ 0x40f64b   (sentinel-init preamble, 3 tables)
 *   - FUN_00459dfd L51-L81      (per-pass active counter scan)
 */

#include "scene1_records.h"

int32_t g_scene1_records_a[SCENE1_RECORDS_A_COUNT * SCENE1_RECORDS_A_STRIDE];
int32_t g_scene1_records_b[SCENE1_RECORDS_B_COUNT * SCENE1_RECORDS_B_STRIDE];
int32_t g_scene1_records_c[SCENE1_RECORDS_C_COUNT * SCENE1_RECORDS_C_STRIDE];

int g_scene1_records_a_count;
int g_scene1_records_b_count;
int g_scene1_records_c_count;

void scene1_records_reset(int reset_c)
{
    /* Table A — sentinel [0] = -1 on every slot. */
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE] = -1;
    }

    /* Table C — sentinel [0] = -1 on every slot.  Gated by reset_c
     * so the "soft reset" call sites (param_1==0) leave dungeon-side
     * particle state alive across the reset.  HOUSE entry passes 1. */
    if (reset_c) {
        for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
            g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE] = -1;
        }
    }

    /* Table B — [0]=0 sentinel, [2]=slot index for the engine's
     * slot-lookup-by-index path. */
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        int32_t *r = &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
        r[0] = 0;
        r[2] = i;
    }
}

void scene1_records_counter_scan(void)
{
    int count = 0;
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        if (g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE] != -1) {
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
        if (g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE] != -1) {
            count = i + 1;
        }
    }
    g_scene1_records_c_count = count;
}
