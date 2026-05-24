/*
 * scene1_per_frame_open.c — see scene1_per_frame_open.h.
 *
 * Engine sources:
 *   PFO.1: FUN_00414902 @ 0x414902 — Table A sentinel-init.
 *   PFO.2: FUN_00412a89 @ 0x412a89 L17-L42 — parent template table
 *          first init loop (per-entry default fill: -1 sentinels,
 *          100-quartet RGBA, 1.0 scale_mul, 0 xyz).
 *
 * Other halves of FUN_00414929 land in PFO.3..PFO.7 per the chip
 * ladder in docs/findings/scene1-per-frame-open.md.
 */

#include "scene1_per_frame_open.h"

int32_t g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_COUNT *
                             SCENE1_PFO_TABLE_A_STRIDE];

int32_t g_scene1_pfo_parent_table[SCENE1_PFO_PARENT_TABLE_COUNT *
                                  SCENE1_PFO_PARENT_TABLE_STRIDE];

void scene1_pfo_table_a_init(void)
{
    /* Engine FUN_00414902 L12548-L12552:
     *   puVar1 = &DAT_00730c30;
     *   do { *puVar1 = -1; puVar1 += 0xb; }
     *   while (puVar1 != &DAT_00733830);
     *
     * Each iteration writes the sentinel field (slot dw 4) to -1.
     * Other fields are NOT zeroed by the engine; our BSS-zero storage
     * leaves them at 0 which matches engine first-call behavior. */
    for (int i = 0; i < SCENE1_PFO_TABLE_A_COUNT; i++) {
        g_scene1_pfo_table_a[i * SCENE1_PFO_TABLE_A_STRIDE +
                             SCENE1_PFO_TABLE_A_OFF_SENTINEL] = -1;
    }
}

void scene1_pfo_parent_table_init(void)
{
    /* Engine FUN_00412a89 L18-L42 walks `puVar5 = &DAT_00744580` (=
     * entry+40 dw) stepping +0x5f per iter, and per entry runs a
     * 7-iter inner loop with strided pointer arithmetic:
     *
     *   puVar5+-15+k : sub_rec[k].sentinel  = -1
     *   puVar5+-8+k  : sub_rec[k].age_match = 0
     *   puVar5+(4k-1, 4k, 4k+1, 4k+2) : sub_rec[k].rgba = (100, 100, 100, 100)
     *   puVar5+0x1b+k : sub_rec[k].scale_mul = 1.0f
     *   puVar5+0x22+3k .. +0x24+3k : sub_rec[k].xyz = (0, 0, 0)
     *
     * Re-anchored to entry start (entry+0..94): sentinels at dw
     * 25..31; age_match at 32..38; rgba at 39..66 (4 dw × 7);
     * scale_mul at 67..73; xyz at 74..94 (3 dw × 7).
     *
     * The engine also writes "<unknown>" to the name field at entry+0
     * via FUN_005038ff (sprintf-style).  The tick (FUN_00414929)
     * never reads the name; PFO.7's parser overwrites entry+0..24
     * from `ef/effect%d.dat`.  We leave dw 0..24 BSS-zero. */

    const int32_t one_f = 0x3f800000; /* IEEE 754 binary32 1.0f */
    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        int32_t *entry = &g_scene1_pfo_parent_table[
            i * SCENE1_PFO_PARENT_TABLE_STRIDE];
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            entry[SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0  + k]      = -1;
            entry[SCENE1_PFO_PARENT_OFF_SUB_AGE_MATCH_0 + k]      = 0;
            entry[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0      + k * 4 + 0] = 100;
            entry[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0      + k * 4 + 1] = 100;
            entry[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0      + k * 4 + 2] = 100;
            entry[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0      + k * 4 + 3] = 100;
            entry[SCENE1_PFO_PARENT_OFF_SUB_SCALE_MUL_0 + k]      = one_f;
            entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 0] = 0;
            entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 1] = 0;
            entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 2] = 0;
        }
    }
}
