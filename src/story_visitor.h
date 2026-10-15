/*
 * story_visitor.h — the day-8 visitor in the shop (script group 10): the scene
 * FUN_004852fb arms on the day-8 morning (work +0x2bcd8 = 1).
 *
 *   FUN_004706ea  story_visitor_start    the setup: the visitor's walker record
 *                                        (DAT_073a6db8, the customer-walker
 *                                        layout + 2 dwords), its two sprite
 *                                        records (chars 47 / 48), the meet tick
 *                                        DAT_005c7df0 = 1200, the party's pose
 *   FUN_00470a46  story_visitor_tick     the director (from FUN_0044bd0d): the
 *                                        walk FUN_0046fbee from b924 180, the
 *                                        meet (the player within 2.5 or b924 ==
 *                                        DAT_005c7df0) → iv10_1, the leave
 *                                        (+50 .. +260 → iv10_2), the fade at +290
 *   FUN_00470970  story_visitor_face     an event frame: the party faces the
 *                                        visitor
 *   FUN_004708f7  story_visitor_aux_tick the two sprite records' anim ticks
 *
 * FUN_0048670f's work +0x2bcd8 block (b924++, from 121 the bg NPCs + the party,
 * then return) lives in scene1_player_ctrl.c.
 */
#ifndef OPENRECET_STORY_VISITOR_H
#define OPENRECET_STORY_VISITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORY_SCENE_OFF   0x2bcd8   /* work byte: the day-8 scene runs (DAT_0450f470) */
#define STORY_MET_OFF     0x2bcd9   /* work byte: the visitor's first line started */

extern int32_t g_story_visitor[0x26];     /* DAT_073a6db8: [0x24] shown, [0x25] leaving */
extern int32_t g_story_visitor_chr[2][11];/* DAT_073a8b50 / DAT_073a8b7c: chars 47 / 48 */
extern float   g_story_visitor_pos[2][3]; /* DAT_073a8b38: the two sprites' positions */
extern int32_t g_story_visitor_frame;     /* DAT_073a8bbc */
extern int32_t g_story_visitor_meet;      /* DAT_005c7df0: the b924 of the meeting (1200) */

int  story_visitor_active(void);      /* work +0x2bcd8 != 0 */
void story_visitor_start(void);       /* FUN_004706ea */
void story_visitor_tick(void);        /* FUN_00470a46 */
void story_visitor_face(void);        /* FUN_00470970 */
void story_visitor_aux_tick(void);    /* FUN_004708f7 */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_STORY_VISITOR_H */
