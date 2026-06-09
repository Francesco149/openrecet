/*
 * scene1_tutorial_dispatch.c — see the header.  The iv1_5/iv1_6 branches of
 * FUN_0044bd0d (all.c:45664-45688).
 */
#include "scene1_tutorial_dispatch.h"

#include "scene1_intro_dialogue.h"   /* _busy / _start_single */
#include "save_work.h"               /* save_work_dwords_at / _active_slot  */

#include <stdint.h>
#include <stddef.h>   /* NULL */

/* Shop-display tutorial flags, bank-byte offsets relative to the working record
 * base DAT_044e3798 (same base + scheme the player controller's PC_SHOP_DISPLAY_*
 * bytes use: DAT_0450f3f8 → 0x2bc60, so each +1 byte is the next DAT_0450f3xx). */
#define TUT_BACKROW_OFF        0x2bc63   /* DAT_0450f3fb — placed in row 0 (cond)   */
#define TUT_BACKROW_DONE_OFF   0x2bc64   /* DAT_0450f3fc — iv1_5 fired (done)        */
#define TUT_ALLFILLED_OFF      0x2bc65   /* DAT_0450f3fd — all stands filled (cond)  */
#define TUT_ALLFILLED_DONE_OFF 0x2bc66   /* DAT_0450f3fe — iv1_6 fired (done)        */
#define TUT_ALLFILLED_DONE2_OFF 0x2bc67  /* DAT_0450f3ff — iv1_6 fired (done, pair)  */

/* The opening-scene selector the dispatcher writes (DAT_005c7a2c = 1). */
#define TUT_SCENE 1
#define TUT_SUB_IV1_5 5
#define TUT_SUB_IV1_6 6

void scene1_tutorial_dispatch_tick(void)
{
    /* Gate: no dialogue armed/loading/active (retail DAT_0438b1c8 == 0).  Uses
     * _busy() rather than _active()||_loading() because the latter has a 1-frame
     * hole at the D_TUT_LOAD→D_TUT lazy-load seam (loading already false, active
     * not yet true) through which iv1_6 would fire and clobber iv1_5.  Blocks
     * during the prologue too; serialises iv1_5 → iv1_6 (iv1_6 only once iv1_5 has
     * fully completed and g_state returns to D_IDLE). */
    if (scene1_intro_dialogue_busy())
        return;

    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    uint8_t *bb = (uint8_t *)bank;

    /* iv1_5 (all.c:45666): the window-counter placement dialogue.  Priority over
     * iv1_6 (if/else-if), so when a single placement sets BOTH flags this fires
     * first; iv1_6 then fires once this one completes and the gate reopens. */
    if (bb[TUT_BACKROW_DONE_OFF] == 0 && bb[TUT_BACKROW_OFF] == 1) {
        scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_5);
        bb[TUT_BACKROW_DONE_OFF] = 1;        /* DAT_0450f3fc = 1 */
        return;
    }

    /* iv1_6 (all.c:45677): the "all wares displayed" dialogue. */
    if (bb[TUT_ALLFILLED_DONE_OFF] == 0 && bb[TUT_ALLFILLED_OFF] == 1) {
        scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_6);
        bb[TUT_ALLFILLED_DONE_OFF]  = 1;     /* DAT_0450f3fe = 1 */
        bb[TUT_ALLFILLED_DONE2_OFF] = 1;     /* DAT_0450f3ff = 1 */
    }
}
