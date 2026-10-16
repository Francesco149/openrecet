/*
 * scene1_companion_ctrl.h — Tear's (actor 2) house state outside FUN_0048a833
 * (which is dungeon_party.c's party_follow_tick, house and floor): the shared
 * per-frame counter DAT_056db054, the wander state's resets and accessors.
 */
#ifndef OPENRECET_SCENE1_COMPANION_CTRL_H
#define OPENRECET_SCENE1_COMPANION_CTRL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FUN_0048407f's tail: DAT_056db054++ (the event arm; FUN_0048b850 counts its own) */
void scene1_companion_ctrl_advance_phase_event(void);

/* DAT_056db054 = 0 */
void scene1_companion_ctrl_reset(void);

/* FUN_00436f97: DAT_0438be6c / be70 / be74 / be78 = 0 (the wander) */
void scene1_companion_ctrl_stage_reset(void);

int32_t scene1_companion_wander_state(void);   /* DAT_0438be6c */
int32_t scene1_companion_wander_t(void);       /* DAT_0438be70 */
float   scene1_companion_wander_tx(void);      /* DAT_0438be74 */
float   scene1_companion_wander_tz(void);      /* DAT_0438be78 */

int scene1_companion_db054(void);              /* DAT_056db054 */

/* trace harness {phasepin}: db054 and Tear's anim cycle to 0 */
void scene1_companion_ctrl_phasepin(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_COMPANION_CTRL_H */
