/*
 * test_story_visitor.c — the day-8 visitor's setup (FUN_004706ea) and the
 * director's meet / end rules (FUN_00470a46).
 */
#include "t.h"

#include <string.h>

#include "fade.h"
#include "party.h"
#include "save_work.h"
#include "scene1_intro_dialogue.h"
#include "scene1_shop_walker.h"
#include "scene1_tutorial_dispatch.h"
#include "story_visitor.h"

static float vf(int off) { float f; memcpy(&f, &g_story_visitor[off], 4); return f; }

int test_story_visitor_start_places_the_visitor(void)
{
    save_work_clear();
    save_work_set_active_slot(0);
    story_visitor_start();
    T_ASSERT_EQ_I(g_story_visitor[CS_NPC_OFF_GRID_X], 6);    /* shop tier 0 */
    T_ASSERT_EQ_I(g_story_visitor[CS_NPC_OFF_GRID_Y], 2);
    T_ASSERT(vf(CS_NPC_OFF_POS_X) == 3.0f);                 /* 2·6 − 9 */
    T_ASSERT(vf(CS_NPC_OFF_POS_Z) == -3.0f);                /* 2·2 − 7 */
    T_ASSERT_EQ_I(g_story_visitor[CS_NPC_OFF_TYPE_IDX], -1);
    T_ASSERT_EQ_I(g_story_visitor[CS_NPC_OFF_WSTATE], -1);
    T_ASSERT_EQ_I(g_story_visitor[0x24], 1);
    T_ASSERT_EQ_I(g_story_visitor_meet, 1200);
    T_ASSERT_EQ_I(g_story_visitor_chr[0][0], 1);
    T_ASSERT_EQ_I(g_story_visitor_chr[0][6], 2);
    save_work_clear();
    return 0;
}

int test_story_visitor_meets_a_near_player_and_ends_after_the_fade(void)
{
    save_work_clear();
    save_work_set_active_slot(0);
    fade_reset();
    story_visitor_start();
    uint8_t *wb = save_work_bank_at(0);
    wb[STORY_SCENE_OFF] = 1;
    /* the player 1 from the visitor at b924 100: the meet moves to 102 */
    g_party.pos[0][0] = vf(CS_NPC_OFF_POS_X) + 1.0f;
    g_party.pos[0][2] = vf(CS_NPC_OFF_POS_Z);
    scene1_beat_set_counter(100);
    story_visitor_tick();
    T_ASSERT_EQ_I(g_story_visitor_meet, 102);
    /* past meet + 290 with the fade done: the scene ends */
    scene1_beat_set_counter(102 + 290);
    story_visitor_tick();                       /* the fade-out starts */
    for (int k = 0; k < 40 && !fade_is_done(); k++) fade_tick();
    scene1_beat_set_counter(102 + 291);
    story_visitor_tick();
    T_ASSERT_EQ_I(wb[STORY_SCENE_OFF], 0);
    T_ASSERT_EQ_I(wb[STORY_MET_OFF], 0);
    T_ASSERT_EQ_I(scene1_beat_counter(), 192);
    fade_reset();
    scene1_beat_set_counter(0);
    save_work_clear();
    memset(&g_party, 0, sizeof g_party);
    return 0;
}
