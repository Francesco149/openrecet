/*
 * test_scene_adv_guild.c — the Adventurers' Guild (mode 11) pure logic:
 * the party list FUN_0047f1ce, the message-box open FUN_00434ceb, the scene
 * init FUN_0045bdc2, and the first-visit intro FUN_0044c7c3.
 */
#include "t.h"

#include <string.h>

#include "party_roster.h"
#include "save_bank.h"
#include "save_work.h"
#include "scene.h"
#include "scene1_intro_dialogue.h"
#include "scene_adv_guild.h"
#include "scene_worldmap.h"
#include "title_save_dialog.h"
#include "worker_load.h"

static uint8_t *fresh_bank(void)
{
    save_work_set_active_slot(0);
    uint8_t *bb = save_work_bank_at(0);
    memset(bb, 0, SAVE_BANK_STRIDE_DWORDS * 4u);
    return bb;
}

int test_party_roster_lists_recruited_in_id_order(void)
{
    uint8_t *bb = fresh_bank();
    bb[0x2bc4c + 0] = 1;      /* Louie, the first flag  */
    bb[0x2bd18 + 3] = 1;      /* Tielle, the second flag */
    party_roster_rebuild();
    T_ASSERT_EQ_I(party_roster_count(), 2);
    T_ASSERT_EQ_I(party_roster_row_id(0), 0);   /* DAT_07477e70: never written */
    T_ASSERT_EQ_I(party_roster_row_id(1), 0);
    T_ASSERT_EQ_I(party_roster_row_id(2), 3);
    return 0;
}

int test_title_save_dialog_message_open_splits_br(void)
{
    title_save_dialog_reset();
    title_save_dialog_message_open("Pick<BR>the items<BR>now");
    T_ASSERT_EQ_I(title_save_dialog_message_lines(), 2);
    T_ASSERT(strcmp(title_save_dialog_message_line(0), "Pick") == 0);
    T_ASSERT(strcmp(title_save_dialog_message_line(1), "the items") == 0);
    T_ASSERT(strcmp(title_save_dialog_message_line(2), "now") == 0);
    /* the box is up: FUN_00434d6a ramps it and blocks (-1) */
    T_ASSERT_EQ_I(title_save_dialog_gate_tick(), -1);
    T_ASSERT_EQ_I(title_save_dialog_get_active_counter(), 2);
    return 0;
}

int test_scene_adv_guild_enter_sets_bag_size_floors_and_rows(void)
{
    uint8_t *bb = fresh_bank();
    uint32_t *w = (uint32_t *)bb;
    w[SAVE_BANK_FIELD_MERCHANT_LEVEL] = 23;      /* bag 30 */
    w[0xb101 + 1] = 9;                           /* dungeon 1: (9+1)/5+1 = 3 = cap */
    w[0xb10a + 20 * 1 + 3] = 1;                  /* … +1 for the cleared group */
    w[0xb37e] = 3;                               /* last hired: Tielle */
    w[0xb37f] = 1;                               /* last dungeon */
    bb[0x2bc4c + 0] = 1;
    bb[0x2bc4c + 3] = 1;
    bb[0x2bcb9] = 1;                             /* 3 dungeons */
    scene_adv_guild_reset();
    scene_adv_guild_init(NULL);
    worker_load_get_cb(11)();                    /* FUN_0045bdc2 */
    int32_t v = 0;
    T_ASSERT(adv_guild_dbg(0x5c5a98u, &v)); T_ASSERT_EQ_I(v, 30);
    T_ASSERT(adv_guild_dbg(0x5c5a9cu, &v)); T_ASSERT_EQ_I(v, 2);   /* Tielle's row */
    T_ASSERT(adv_guild_dbg(0x6a49b50u, &v)); T_ASSERT_EQ_I(v, 1);
    T_ASSERT(adv_guild_dbg(0x6a49b74u, &v)); T_ASSERT_EQ_I(v, 3);
    T_ASSERT(adv_guild_dbg(0x6a49b5cu, &v)); T_ASSERT_EQ_I(v, 1);  /* the list is up */
    T_ASSERT_EQ_I(adv_guild_bag_slot(1), -1);
    T_ASSERT_EQ_I(adv_guild_bag_slot(ADV_GUILD_BAG_SLOTS), -1);
    T_ASSERT_EQ_U(w[SAVE_BANK_FIELD_RESUME_MODE], 1);
    return 0;
}

int test_adv_guild_continue_reset_bag_size_and_empty_bag(void)
{
    /* FUN_0049a59e's Continue arm: DAT_005c5a98 by merchant level, the bag
     * DAT_0438b5ec[] all -1 (the .data default is 38). */
    scene_adv_guild_reset();
    int32_t v = 0;
    T_ASSERT(adv_guild_dbg(0x5c5a98u, &v)); T_ASSERT_EQ_I(v, 38);
    adv_guild_continue_reset(3);
    T_ASSERT(adv_guild_dbg(0x5c5a98u, &v)); T_ASSERT_EQ_I(v, 20);
    adv_guild_continue_reset(16);
    T_ASSERT(adv_guild_dbg(0x5c5a98u, &v)); T_ASSERT_EQ_I(v, 25);
    adv_guild_continue_reset(39);
    T_ASSERT(adv_guild_dbg(0x5c5a98u, &v)); T_ASSERT_EQ_I(v, 35);
    T_ASSERT_EQ_I(adv_guild_bag_slot(1), -1);
    T_ASSERT_EQ_I(adv_guild_bag_slot(ADV_GUILD_BAG_SLOTS), -1);
    return 0;
}

int test_scene_adv_guild_intro_two_scripts_then_world_map(void)
{
    uint8_t *bb = fresh_bank();
    bb[0x2bc7c] = 1;                             /* the intro is pending */
    scene_adv_guild_reset();
    scene1_intro_dialogue_reset();
    g_scene_state = 11;
    scene_adv_guild_sim();                       /* iv2_7 */
    T_ASSERT_EQ_I(bb[0x2bc7d], 1);
    T_ASSERT_EQ_I(scene1_intro_dialogue_gate(), 2);
    scene1_intro_dialogue_reset();               /* (the script ran) */
    scene_adv_guild_sim();                       /* iv2_8: Louie joins */
    T_ASSERT_EQ_I(bb[0x2bc7e], 1);
    T_ASSERT_EQ_I(bb[0x2bc7f], 1);
    T_ASSERT_EQ_I(bb[0x2bc4c], 1);
    scene1_intro_dialogue_reset();
    worker_load_reset();
    scene_adv_guild_sim();                       /* out to the world map */
    T_ASSERT_EQ_I(g_scene_state, 8);
    T_ASSERT_EQ_I(scene_worldmap_sel_dest(), 6);
    T_ASSERT_EQ_I(bb[0x2bc7c], 0);
    T_ASSERT_EQ_I(bb[0x2bc7f], 0);
    g_scene_state = 0;
    worker_load_reset();
    return 0;
}
