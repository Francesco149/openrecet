/*
 * scene1_per_frame_open.c — see scene1_per_frame_open.h.
 *
 * Engine sources for this chip (PFO.1):
 *   - FUN_00414902 @ 0x414902 — Table A sentinel-init (Table B half
 *                              already covered by scene1_overlay_reset).
 *
 * Other halves of FUN_00414929 land in PFO.2..PFO.7 per the chip
 * ladder in docs/findings/scene1-per-frame-open.md.
 */

#include "scene1_per_frame_open.h"

int32_t g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_COUNT *
                             SCENE1_PFO_TABLE_A_STRIDE];

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
