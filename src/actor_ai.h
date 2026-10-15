/*
 * actor_ai.h — the actor (enemy) pass of a dungeon floor, on the actor table
 * in retail layout (actor_table.h).
 *
 *   FUN_00430c6d  actor_pass           every tick (b4b4 == 0): the stage spawns,
 *                                      the view plane, each actor's timers, its
 *                                      AI by state (1 alive, 2 dying), its
 *                                      physics; the view depths; the falls
 *   FUN_0042df55  actor_stage_spawns   the stage's static spawn list (once)
 *   FUN_0049065b  actor_view_plane     copies the camera matrices for the depths
 *   FUN_0049097c  actor_view_depth     view-space z of a point
 *   FUN_0042f28c  actor_ai_alive       the per-type AI dispatch + gravity
 *   FUN_0042f7ad  actor_dying          a dying actor (the regular enemies' arm)
 *   FUN_0042c9b3  actor_ai_slime       types 0..5
 *   FUN_004304b3  actor_physics        ground, body push vs the player, move,
 *                                      damping, facing, wall push, leash
 *   FUN_0044b1c0  popup_add            a number popup (heal / damage)
 *
 * Float code mirrors the retail disassembly (x87.h).
 */
#ifndef OPENRECET_ACTOR_AI_H
#define OPENRECET_ACTOR_AI_H

#include <stdint.h>

#include "actor_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DAT_044e28f0: 32 number popups × 7 dwords (x, y, z, ticks, ticks0, value,
 * kind); FUN_00442cef counts [3] down */
extern int32_t g_popup[32][7];
/* DAT_095d3730: the projection (16) and view (16) matrices FUN_0049065b
 * copied, and the screen scale (FUN_00490795) */
extern float g_view_proj[16], g_view_mat[16];

void  actor_pass(void);                                   /* FUN_00430c6d */
void  actor_event_anim_pump(void);                        /* FUN_00430c00 */
void  actor_stage_spawns(void);                           /* FUN_0042df55 */
void  actor_view_plane(void);                             /* FUN_0049065b */
float actor_view_depth(float x, float y, float z);        /* FUN_0049097c */
void  actor_ai_alive(actor_t *a);                         /* FUN_0042f28c */
void  actor_dying(actor_t *a);                            /* FUN_0042f7ad */
void  actor_ai_slime(actor_t *a);                         /* FUN_0042c9b3 */
void  actor_physics(actor_t *a);                          /* FUN_004304b3 */
void  popup_add(float x, float y, float z, int32_t value, int32_t kind);   /* FUN_0044b1c0 */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_ACTOR_AI_H */
