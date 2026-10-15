/*
 * record_hit.h — a table-B record (DAT_069324b0) hitting the actors, and
 * what a hit does to an actor: damage, the number popups, the EXP orbs, the
 * kill and its drops.  On the actor table in retail layout (actor_table.h).
 *
 *   FUN_0043865e  record_sm_tick     the record state machine (the table-B
 *                                    tick's hook): pause gates, the actor
 *                                    arm, the player arm, the object scan
 *                 record_hit_actors  the actor arm of the record state
 *                                    machine (record state 0 / 3, the player
 *                                    alive): the first actor in reach takes
 *                                    the hit (damage roll, knock-back, sparks,
 *                                    SE) — 1 on a hit, 0 when none
 *                 record_hit_objects the object arm: pots break, chests open
 *                 (the player arm: an enemy record hitting the player)
 *   FUN_00485c74  party_take_damage  HP −, the hurt voice, the HP accessories
 *   FUN_00485979  party_hurt_voice   the active chara's hurt voice
 *   FUN_004412b6  chest_trap         a trap fires: the slimes (4 / 5), the blocks (1),
 *                                    the rocks (2 / 3), the gas (6 / 8), the roller (9),
 *                                    the bomb (10)
 *   FUN_00441aab  trap_spots_tick    the floor's trap spots + Louie's trap sense
 *   FUN_0041f46d  enemy_level_mul    1 + level · (0.1 | 0.12)
 *   FUN_0042e791  actor_damage       HP −, the popups, the EXP orbs of a
 *                                    flagged hit, the kill at HP < 1
 *   FUN_0042e1a6  actor_kill         EXP orbs, the combo, the drop roll
 *   FUN_0043183e  enemy_drop_pick    enemylist drop of a rarity
 *   FUN_004385fb  orb_kind           the EXP orb's kind (1..3)
 *   FUN_0044b16c  popup_push         a damage-number popup
 *
 * Float code mirrors the retail disassembly (x87.h).
 */
#ifndef OPENRECET_RECORD_HIT_H
#define OPENRECET_RECORD_HIT_H

#include <stdint.h>

#include "actor_table.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t g_s_easydisp;        /* DAT_0438b1a0: recet.ini s_easydisp (EXP straight in) */
extern int32_t g_orb_pick_n;        /* DAT_0438b900: FUN_004385fb's call count */
extern int32_t g_kill_combo;        /* DAT_0438b90c: kills of the same type in a row */
extern int32_t g_kill_combo_type;   /* DAT_0438b910 */
extern int32_t g_kill_combo_timer;  /* DAT_0438b914 */
extern int32_t g_dat_06a46f8c;      /* the last chest roll: 1 */
extern int32_t g_dat_06a46f90;      /* … and its tier */

int         record_sm_tick(int32_t *slot);                           /* FUN_0043865e */
void        record_sm_install(void);    /* the table-B tick's state-machine hook */
int         record_hit_actors(int32_t *slot);                        /* FUN_0043865e's actor arm */
int         record_hit_objects(int32_t *slot);                       /* FUN_0043865e's object arm */
int         chest_trap(float x, float z, int32_t kind);              /* FUN_004412b6 */
void        trap_spots_tick(void);                                   /* FUN_00441aab */
void        party_take_damage(int32_t dmg);                          /* FUN_00485c74 */
void        party_hurt_voice(int down);                              /* FUN_00485979 */
long double enemy_level_mul(int32_t type);                           /* FUN_0041f46d */
void        actor_damage(actor_t *a, int32_t dmg, int32_t flags);    /* FUN_0042e791 */
void        actor_kill(actor_t *a);                                  /* FUN_0042e1a6 */
int32_t     enemy_drop_pick(int32_t type, int32_t rarity, int32_t var);   /* FUN_0043183e */
int32_t     orb_kind(void);                                          /* FUN_004385fb */
void        popup_push(float x, float y, float z, int32_t value, int32_t kind);   /* FUN_0044b16c */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_RECORD_HIT_H */
