/*
 * test_scene_town_event.c — the town visits (modes 13..16): FUN_0045e1a5 /
 * FUN_0045e053 / FUN_0045e2dd's script → fade → world-map machine.
 */
#include "t.h"

#include <string.h>

#include "fade.h"
#include "save_bank.h"
#include "save_work.h"
#include "scene.h"
#include "scene1_intro_dialogue.h"
#include "scene_town_event.h"
#include "sim.h"
#include "worker_load.h"

static uint32_t *fresh_bank(void)
{
    save_work_set_active_slot(0);
    uint8_t *bb = save_work_bank_at(0);
    memset(bb, 0, SAVE_BANK_STRIDE_DWORDS * 4u);
    return (uint32_t *)bb;
}

static int32_t dbg(uint32_t va)
{
    int32_t v = -99;
    scene_town_event_dbg(va, &v);
    return v;
}

int test_scene_town_event_tavern_script_fade_then_world_map(void)
{
    uint32_t *w = fresh_bank();
    scene_town_event_reset();
    scene1_intro_dialogue_reset();
    fade_reset();
    worker_load_reset();
    g_scene_state = 15;
    scene_town_event_sim(15);                    /* tick 0: no event → iv16_4 */
    T_ASSERT_EQ_I(scene1_intro_dialogue_gate(), 2);
    T_ASSERT_EQ_I(dbg(0x6a5d404u), 1);
    T_ASSERT_EQ_I(dbg(0x6a5d400u), 0);
    scene1_intro_dialogue_reset();               /* (the script ran) */
    scene_town_event_sim(15);                    /* tick 1: the fade-out */
    T_ASSERT_EQ_I(dbg(0x6a5d400u), 1);
    T_ASSERT(!fade_is_done());
    for (int i = 0; i < 100 && !fade_is_done(); i++)
        fade_tick();
    T_ASSERT(fade_is_done());
    scene_town_event_sim(15);                    /* out: period flag, mode 8 */
    T_ASSERT_EQ_I(g_scene_state, 8);
    T_ASSERT_EQ_I((int32_t)w[0xb382], 1);
    g_scene_state = 0;
    fade_reset();
    worker_load_reset();
    return 0;
}

int test_scene_town_event_mode16_leaves_on_z_only(void)
{
    fresh_bank();
    scene_town_event_reset();
    scene1_intro_dialogue_reset();
    fade_reset();
    g_sim_buttons[0].pressed = 0;
    scene_town_event_sim(16);
    T_ASSERT_EQ_I(scene1_intro_dialogue_gate(), 0);   /* no script */
    T_ASSERT_EQ_I(dbg(0x6a5d400u), 0);
    g_sim_buttons[0].pressed = 0x10;
    scene_town_event_sim(16);
    T_ASSERT_EQ_I(dbg(0x6a5d400u), 1);
    T_ASSERT_EQ_I(dbg(0x6a5d404u), 2);
    g_sim_buttons[0].pressed = 0;
    fade_reset();
    return 0;
}
