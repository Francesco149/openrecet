/*
 * scene_gameover.h — mode 2 (DAT_0438b1c0 == 2): the scene after a failed debt
 * payment (and the day-8 / day-36 week-end story), from the day-end exit.
 *
 *   FUN_0049d2e5  scene_gameover_enter          the mode's state (DAT_09643578..584)
 *   FUN_0049d8a4  scene_gameover_tick           the steps: Tear's script (0, 1 | 3),
 *                                               the restart reset, the script (0, 2),
 *                                               then the fade and the house (or title)
 *                                               load with the morning beat
 *   FUN_0049d55c  scene_gameover_restart_reset  the work record back to day 1 (1000
 *                                               gold), the kept flags
 *
 * The tick runs only while no dialogue is up (FUN_004536cb LABEL_106).  The
 * render (FUN_0047355d's assets, the screen) is not ported.
 */
#ifndef OPENRECET_SCENE_GAMEOVER_H
#define OPENRECET_SCENE_GAMEOVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void scene_gameover_enter(void);                          /* FUN_0049d2e5 */
void scene_gameover_tick(void);                           /* FUN_0049d8a4 */
void scene_gameover_restart_reset(uint8_t *w, int keep);  /* FUN_0049d55c */

int32_t scene_gameover_step(void);     /* DAT_09643584 */
int32_t scene_gameover_exit_t(void);   /* DAT_0964357c */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE_GAMEOVER_H */
