/*
 * state_va.c — see state_va.h.  Add a row when a new port mirror of a retail
 * global is needed for a frame-by-frame diff; keep the mapping exact (a row is a
 * claim that the port variable IS that engine global, value for value).
 */
#include "state_va.h"

#include <stdlib.h>
#include <string.h>

#include "actor_ai.h"
#include "dungeon_party.h"
#include "scene1_combat_sm.h"
#include "scene1_records.h"
#include "record_hit.h"
#include "actor_table.h"
#include "customer_service.h"
#include "dungeon_flow.h"
#include "stage_gate.h"
#include "scene1_camera.h"        /* the shake DAT_0438cc14.. */
#include "dungeon_objects.h"
#include "map_collision.h"
#include "news_daily.h"
#include "party.h"
#include "stage_post_load.h"
#include "stage_palette.h"
#include "floor_layout.h"
#include "rng.h"
#include "save_bank.h"
#include "save_work.h"
#include "scene.h"
#include "scene_adv_guild.h"
#include "scene_town_event.h"
#include "scene_day_end.h"
#include "scene_pause.h"
#include "scene_title.h"
#include "scene1_actor_say.h"
#include "scene1_bg_npc.h"
#include "scene1_companion_ctrl.h"
#include "scene1_counter_menu.h"
#include "scene1_day_close.h"
#include "scene1_display_menu.h"
#include "scene1_intro_dialogue.h"
#include "scene1_particles_tick.h"
#include "scene1_player_ctrl.h"
#include "scene1_shop_display.h"
#include "scene1_shop_walker.h"
#include "scene1_top_hud.h"
#include "scene1_tutorial_dispatch.h"
#include "sim.h"
#include "speech_bubble.h"
#include "story_visitor.h"
#include "tick.h"
#include "title_save_dialog.h"
#include "choice_box.h"
#include "fade.h"
#include "worker_load.h"

static int32_t get_rng(void)   { return (int32_t)g_rng_seed; }
static int32_t get_scene(void) { return g_scene_state; }
static int32_t get_floor(void) { return stage_gate_get_next(); }
static int32_t get_dungeon(void) { return g_scene1_combat_stage_id; }
static int32_t get_cc08(void)  { return player_ctrl_cc08(); }
static int32_t get_b1cc(void)  { return customer_service_b1cc(); }
static int32_t get_b524(void)  { return customer_service_b524(); }
static int32_t get_b534(void)  { return customer_service_b534(); }
static int32_t get_b544(void)  { return customer_service_b544(); }
static int32_t get_b584(void)  { return customer_service_round(); }
static int32_t get_b574(void)  { return customer_service_offer(); }
static int32_t get_bb8(void)   { return customer_service_player_ask(); }
static int32_t get_b5a8(void)  { return customer_service_b5a8(); }
static int32_t get_b56c(void)  { return customer_service_b56c(); }
static int32_t get_b520(void)  { return customer_service_b520(); }
static int32_t get_db054(void) { return scene1_companion_db054(); }
static int32_t get_ttfc(void) { return (int32_t)g_scene_title_anim.frame_counter; }
static int32_t get_dm_tab(void)  { return display_menu_dbg_cur_tab(); }
static int32_t get_dm_ntab(void) { return display_menu_dbg_num_tabs(); }
static int32_t get_dm_cur0(void) { return display_menu_dbg_tab_cursor0(); }
static int32_t get_dm_cnt0(void) { return display_menu_dbg_tab_count0(); }
static int32_t get_dm_l0(void)   { return display_menu_dbg_list(0); }
static int32_t get_dm_l2(void)   { return display_menu_dbg_list(2); }
static int32_t get_dm_l4(void)   { return display_menu_dbg_list(4); }
static int32_t get_b5d8(void)  { return customer_service_b5d8(); }
static int32_t get_b548(void)  { return customer_service_b548(); }
static int32_t get_cc04(void)  { return player_ctrl_cc04(); }
static int32_t get_b318(void)  { return customer_service_b318(); }
static int32_t get_b528(void)  { return customer_service_b528(); }
static int32_t get_gsim(void)  { return (int32_t)g_sim_frame_count; }
static int32_t get_tickn(void) { return (int32_t)g_tick.frame_count; }
static int32_t get_tprev(void) { return (int32_t)g_tick.prev_ms; }
static int32_t get_b74c(void)  { return counter_menu_slide(); }
static int32_t get_b754(void)  { return counter_menu_timer(); }
static int32_t get_cc0c(void)  { return counter_menu_cursor(); }
static int32_t get_ttctr(void) { return scene1_top_hud_tooltip_ctr(); }
static int32_t get_fl_w(void)  { return g_floor_w; }
static int32_t get_fl_h(void)  { return g_floor_h; }
static int32_t get_fl_n(void)  { return g_floor_ncells; }
static int32_t get_fl_tot(void) { return g_floor_rooms_total; }
static int32_t fbits(float f) { union { float f; int32_t i; } u = { .f = f }; return u.i; }
static int32_t get_hand_anim(void) { return title_save_dialog_get_anim_counter(); }
static int32_t get_hand_x(void)    { return fbits(title_save_dialog_get_shake_pos_x()); }
static int32_t get_hand_y(void)    { return fbits(title_save_dialog_get_shake_pos_y()); }
static int32_t get_hand_vis(void)  { return title_save_dialog_cursor_get_visible(); }
static int32_t get_hand_ctr(void)  { return title_save_dialog_get_shake_counter(); }
static int32_t get_wst(void)  { return scene1_companion_wander_state(); }
static int32_t get_wt(void)   { return scene1_companion_wander_t(); }
static int32_t get_wtx(void)  { return fbits(scene1_companion_wander_tx()); }
static int32_t get_wtz(void)  { return fbits(scene1_companion_wander_tz()); }
static int32_t get_tx(void)   { return fbits(g_scene1_actor_pos[2][0]); }
static int32_t get_ty(void)   { return fbits(g_scene1_actor_pos[2][1]); }
static int32_t get_tz(void)   { return fbits(g_scene1_actor_pos[2][2]); }
static int32_t tear_rec(int k) { const int32_t *r = player_ctrl_actor_record(2); return r ? r[k] : 0; }
static int32_t get_tsel(void) { return tear_rec(5); }
static int32_t get_tfac(void) { return tear_rec(6); }
static int32_t get_tfrm(void) { return tear_rec(4); }
static int32_t get_mexp(void)
{
    const uint32_t *w = save_work_dwords_at(save_work_active_slot());
    return w ? (int32_t)w[SAVE_BANK_FIELD_MERCHANT_EXP] : 0;
}

static int32_t get_b4e0(void)  { return day_close_b4e0(); }
static int32_t get_2eec(void)  { return day_close_variant(); }
static int32_t get_b764(void)  { return actor_say_age(); }
static int32_t get_b76c(void)  { return actor_say_msg(); }
static int32_t get_bage(void)  { return speech_bubble_age(); }
static int32_t get_db048(void) { return player_ctrl_db048(); }
static int32_t prec(int k) { const int32_t *r = player_ctrl_actor_record(0); return r ? r[k] : 0; }
static int32_t get_psel(void) { return prec(5); }
static int32_t get_pfac(void) { return prec(6); }
static int32_t get_px(void)   { return fbits(g_scene1_actor_pos[0][0]); }
static int32_t get_pz(void)   { return fbits(g_scene1_actor_pos[0][2]); }
static int32_t get_camyaw(void) { return fbits(g_scene1_camera_yaw); }

static int32_t get_cbfc(void)  { return shop_display_cbfc(); }
static int32_t get_cc00(void)  { return shop_display_cc00(); }
static int32_t get_dsel(void)  { return display_menu_selected(); }
static int32_t get_cbe8(void)  { return player_ctrl_display_latch(); }
static int32_t get_b51c(void)  { return customer_service_b51c(); }
static int32_t get_b604(void)  { return customer_service_b604(); }
static int32_t get_b600(void)  { return customer_service_b600(); }
static int32_t get_b608(void)  { return customer_service_b608(); }
static int32_t get_b928(void)  { return scene1_beat_flag(); }
static int32_t get_b924(void)  { return scene1_beat_counter(); }
static int32_t get_b1c8(void)  { return scene1_intro_dialogue_gate(); }
static int32_t adv(uint32_t va) { int32_t v = 0; adv_guild_dbg(va, &v); return v; }
static int32_t town(uint32_t va) { int32_t v = 0; scene_town_event_dbg(va, &v); return v; }
static int32_t get_te_d400(void) { return town(0x6a5d400u); }
static int32_t get_money_disp(void) { return scene1_top_hud_money(); }
static int32_t get_slot(void) { return save_work_active_slot(); }
static int32_t get_te_d404(void) { return town(0x6a5d404u); }
static int32_t get_ag_b44(void) { return adv(0x6a49b44u); }
static int32_t get_ag_b48(void) { return adv(0x6a49b48u); }
static int32_t get_ag_b4c(void) { return adv(0x6a49b4cu); }
static int32_t get_ag_b50(void) { return adv(0x6a49b50u); }
static int32_t get_ag_b54(void) { return adv(0x6a49b54u); }
static int32_t get_ag_b5c(void) { return adv(0x6a49b5cu); }
static int32_t get_ag_b60(void) { return adv(0x6a49b60u); }
static int32_t get_ag_b6c(void) { return adv(0x6a49b6cu); }
static int32_t get_ag_b70(void) { return adv(0x6a49b70u); }
static int32_t get_ag_b74(void) { return adv(0x6a49b74u); }
static int32_t get_ag_a98(void) { return adv(0x5c5a98u); }
static int32_t get_ag_a9c(void) { return adv(0x5c5a9cu); }
static int32_t get_ag_aa0(void) { return adv(0x5c5aa0u); }
static int32_t dm(uint32_t va) { int32_t v = 0; display_menu_dbg_va(va, &v); return v; }
static int32_t get_dm_96c(void) { return dm(0x734b96cu); }
static int32_t get_dm_97c(void) { return dm(0x734b97cu); }
static int32_t get_dm_988(void) { return dm(0x734b988u); }
static int32_t get_dm_994(void) { return dm(0x734b994u); }
static int32_t get_dm_9a8(void) { return dm(0x734b9a8u); }
static int32_t get_dm_98c(void) { return dm(0x734b98cu); }
static int32_t get_cb_af34(void) { return choice_box_anim(); }
static int32_t get_sd_b148(void) { return title_save_dialog_get_active_counter(); }
static int32_t get_wl_995c(void) { return worker_load_busy_secondary(); }
static int32_t get_c998(void)    { return sim_get_counter_998(); }
static int32_t get_c990(void)    { return sim_get_counter_990(); }
static int32_t get_pact(void)    { return g_pause_action; }
static int32_t get_fade(void)    { return g_fade_phase; }
static int32_t get_clkph(void)  { return fbits(scene1_top_hud_clock_phase()); }
static int32_t get_de_t(void)   { return day_end_frame(); }
static int32_t get_de_p(void)   { return day_end_profit_shown(); }
static int32_t get_de_pt(void)  { return day_end_profit(); }
static int32_t get_de_s(void)   { return day_end_score_shown(); }
static int32_t get_de_a(void)   { return day_end_after(); }
static int32_t get_de_r(void)   { return day_end_rating(); }

typedef struct { uint32_t va; const char *name; int32_t (*get)(void); } state_va_t;
static const state_va_t k_map[] = {
    { 0x6023a0u,  "rng (DAT_006023a0)",               get_rng   },
    { 0x438b1c0u, "scene state (DAT_0438b1c0)",       get_scene },
    { 0x438b4ccu, "floor (DAT_0438b4cc)",             get_floor },
    { 0x438b4c8u, "dungeon (DAT_0438b4c8)",           get_dungeon },
    { 0x438cc08u, "house mode cc08 (DAT_0438cc08)",   get_cc08  },
    { 0x438b1ccu, "cs load phase b1cc (DAT_0438b1cc)", get_b1cc },
    { 0x730b524u, "cs idle counter b524 (DAT_0730b524)", get_b524 },
    { 0x730b534u, "cs sub-state b534 (DAT_0730b534)", get_b534 },
    { 0x730b544u, "cs state timer b544 (DAT_0730b544)", get_b544 },
    { 0x730b584u, "cs haggle round b584 (DAT_0730b584)", get_b584 },
    { 0x730b574u, "cs customer price b574 (DAT_0730b574)", get_b574 },
    { 0x5c6bb8u,  "cs player price bb8 (DAT_005c6bb8)", get_bb8 },
    { 0x730b5a8u, "cs transaction kind b5a8 (DAT_0730b5a8)", get_b5a8 },
    { 0x730b56cu, "cs customer b56c (DAT_0730b56c)", get_b56c },
    { 0x730b520u, "cs leave phase b520 (DAT_0730b520)", get_b520 },
    { 0x56db054u, "companion phase counter db054 (DAT_056db054)", get_db054 },
    { 0x9643518u, "title frame counter (DAT_09643518)", get_ttfc },
    { 0x734b968u, "display menu tab (DAT_0734b968)", get_dm_tab },
    { 0x731f404u, "display menu tab count (DAT_0731f404)", get_dm_ntab },
    { 0x7337850u, "display menu tab-0 cursor (DAT_07337850)", get_dm_cur0 },
    { 0x438b154u, "hand cursor bob counter (DAT_0438b154)", get_hand_anim },
    { 0x438abf4u, "hand cursor x, float bits (DAT_0438abf4)", get_hand_x },
    { 0x438abf8u, "hand cursor y, float bits (DAT_0438abf8)", get_hand_y },
    { 0x438b150u, "hand cursor visible (DAT_0438b150)", get_hand_vis },
    { 0x438ac18u, "hand cursor slide countdown (DAT_0438ac18)", get_hand_ctr },
    { 0x7337210u, "display menu tab-0 entries (DAT_07337210)", get_dm_cnt0 },
    { 0x731f598u, "display menu list[0] (DAT_0731f598)", get_dm_l0 },
    { 0x731f5a0u, "display menu list[2] (DAT_0731f5a0)", get_dm_l2 },
    { 0x731f5a8u, "display menu list[4] (DAT_0731f5a8)", get_dm_l4 },
    { 0x730b5d8u, "cs oder / want-list index b5d8 (DAT_0730b5d8)", get_b5d8 },
    { 0x730b548u, "cs text-reveal budget b548 (DAT_0730b548)", get_b548 },
    { 0x438cc04u, "house sub-mode cc04 (DAT_0438cc04)", get_cc04 },
    { 0x730b318u, "cs queue head b318 (DAT_0730b318)", get_b318 },
    { 0x730b528u, "cs customers advanced b528 (DAT_0730b528)", get_b528 },
    { 0x438b8ccu, "sim frame counter gsim (DAT_0438b8cc)", get_gsim },
    { 0x73dfcfcu, "tick frame_count (DAT_073dfcfc)",  get_tickn },
    { 0x73de61cu, "tick prev_ms (DAT_073de61c)",      get_tprev },
    { 0x438b74cu, "counter-menu slide b74c (DAT_0438b74c)", get_b74c },
    { 0x438b754u, "counter-menu timer b754 (DAT_0438b754)", get_b754 },
    { 0x438cc0cu, "counter-menu cursor cc0c (DAT_0438cc0c)", get_cc0c },
    { 0x529704u,  "top-HUD tooltip counter (DAT_00529704)", get_ttctr },
    { 0x5cc2e8u,  "floor layout W (DAT_005cc2e8)",    get_fl_w  },
    { 0x5cc2ecu,  "floor layout H (DAT_005cc2ec)",    get_fl_h  },
    { 0x73e1540u, "floor layout cells (DAT_073e1540)", get_fl_n },
    { 0x438b4b0u, "floor rooms total (DAT_0438b4b0)", get_fl_tot },
    { 0x450fb8cu, "work slot 0 merchant exp (DAT_0450fb8c)", get_mexp },
    { 0x438be6cu, "Tear wander state (DAT_0438be6c)",  get_wst },
    { 0x438be70u, "Tear wander timer (DAT_0438be70)",  get_wt },
    { 0x438be74u, "Tear wander target x bits (DAT_0438be74)", get_wtx },
    { 0x438be78u, "Tear wander target z bits (DAT_0438be78)", get_wtz },
    { 0x56da1f0u, "Tear x bits (DAT_056da1f0)",        get_tx },
    { 0x56da1f4u, "Tear y bits (DAT_056da1f4)",        get_ty },
    { 0x56da1f8u, "Tear z bits (DAT_056da1f8)",        get_tz },
    { 0x56dab54u, "Tear anim sel (DAT_056dab54)",      get_tsel },
    { 0x56dab58u, "Tear facing (DAT_056dab58)",        get_tfac },
    { 0x56dab50u, "Tear anim frame (DAT_056dab50)",    get_tfrm },
    { 0x438b4e0u, "day-close b4e0 (DAT_0438b4e0)",     get_b4e0 },
    { 0x74b2eecu, "day-close variant (DAT_074b2eec)",  get_2eec },
    { 0x438b764u, "actor-say age (DAT_0438b764)",      get_b764 },
    { 0x438b76cu, "actor-say msg (DAT_0438b76c)",      get_b76c },
    { 0x648258u,  "speech bubble age (DAT_00648258)",  get_bage },
    { 0x438b7d4u, "clock phase bits (DAT_0438b7d4)",   get_clkph },
    { 0x74b2f58u, "day-end frame (DAT_074b2f58)",      get_de_t },
    { 0x74b2f1cu, "day-end profit shown (DAT_074b2f1c)", get_de_p },
    { 0x74b2f20u, "day-end profit (DAT_074b2f20)",     get_de_pt },
    { 0x74b2f24u, "day-end score shown (DAT_074b2f24)", get_de_s },
    { 0x74b2f2cu, "day-end after-count (DAT_074b2f2c)", get_de_a },
    { 0x74b2f34u, "day-end rating (DAT_074b2f34)",     get_de_r },
    { 0x56db048u, "player pose hold db048 (DAT_056db048)", get_db048 },
    { 0x56daafcu, "player anim sel (DAT_056daafc)",    get_psel },
    { 0x56dab00u, "player facing (DAT_056dab00)",      get_pfac },
    { 0x56da1d8u, "player x bits (DAT_056da1d8)",      get_px },
    { 0x56da1e0u, "player z bits (DAT_056da1e0)",      get_pz },
    { 0x73de39cu, "camera yaw bits (DAT_073de39c)",    get_camyaw },
    { 0x438cbfcu, "faced display cell col (DAT_0438cbfc)", get_cbfc },
    { 0x438cc00u, "faced display cell row (DAT_0438cc00)", get_cc00 },
    { 0x469a9fu,  "display menu selection (FUN_00469a9f) — rhost: none", get_dsel },
    { 0x438cbe8u, "display menu open-once latch (DAT_0438cbe8)", get_cbe8 },
    { 0x438b928u, "morning / return beat b928 (DAT_0438b928)", get_b928 },
    { 0x730b51cu, "cs scripted latch b51c (DAT_0730b51c)", get_b51c },
    { 0x730b604u, "cs script PC b604 (DAT_0730b604)", get_b604 },
    { 0x730b600u, "cs script step phase b600 (DAT_0730b600)", get_b600 },
    { 0x730b608u, "cs script sub-state b608 (DAT_0730b608)", get_b608 },
    { 0x438b924u, "beat counter b924 (DAT_0438b924)", get_b924 },
    { 0x438b1c8u, "dialogue gate (DAT_0438b1c8)", get_b1c8 },
    { 0x438b918u, "money shown (DAT_0438b918)", get_money_disp },
    { 0x438b1e0u, "active slot (DAT_0438b1e0)", get_slot },
    { 0x6a5d400u, "town visit leaving (DAT_06a5d400)", get_te_d400 },
    { 0x6a5d404u, "town visit ticks (DAT_06a5d404)", get_te_d404 },
    { 0x6a49b44u, "adv guild commit (DAT_06a49b44)", get_ag_b44 },
    { 0x6a49b48u, "adv guild map open (DAT_06a49b48)", get_ag_b48 },
    { 0x6a49b4cu, "adv guild floor list (DAT_06a49b4c)", get_ag_b4c },
    { 0x6a49b50u, "adv guild dungeon (DAT_06a49b50)", get_ag_b50 },
    { 0x6a49b54u, "adv guild floor (DAT_06a49b54)", get_ag_b54 },
    { 0x6a49b5cu, "adv guild chara list (DAT_06a49b5c)", get_ag_b5c },
    { 0x6a49b60u, "adv guild bag mode (DAT_06a49b60)", get_ag_b60 },
    { 0x6a49b6cu, "adv guild ticks (DAT_06a49b6c)", get_ag_b6c },
    { 0x6a49b70u, "adv guild msg (DAT_06a49b70)", get_ag_b70 },
    { 0x6a49b74u, "adv guild dungeons (DAT_06a49b74)", get_ag_b74 },
    { 0x5c5a98u,  "adv guild bag size (DAT_005c5a98)", get_ag_a98 },
    { 0x5c5a9cu,  "adv guild chara row (DAT_005c5a9c)", get_ag_a9c },
    { 0x5c5aa0u,  "adv guild bag row (DAT_005c5aa0)", get_ag_aa0 },
    { 0x734b96cu, "item window detail popup (DAT_0734b96c)", get_dm_96c },
    { 0x734b97cu, "item window use message (DAT_0734b97c)", get_dm_97c },
    { 0x734b988u, "item window throw message (DAT_0734b988)", get_dm_988 },
    { 0x734b994u, "item window confirm countdown (DAT_0734b994)", get_dm_994 },
    { 0x734b9a8u, "item window mode (DAT_0734b9a8)", get_dm_9a8 },
    { 0x734b98cu, "item window slide (DAT_0734b98c)", get_dm_98c },
    { 0x438af34u, "choice box anim (DAT_0438af34)", get_cb_af34 },
    { 0x438b148u, "message / save box counter (DAT_0438b148)", get_sd_b148 },
    { 0x6a4995cu, "secondary load busy (DAT_06a4995c)", get_wl_995c },
    { 0x6a49998u, "pause ramp (DAT_06a49998)", get_c998 },
    { 0x6a49990u, "pause fade counter (DAT_06a49990)", get_c990 },
    { 0x6a4997cu, "pause action (DAT_06a4997c)", get_pact },
    { 0x438bf7cu, "fade phase (DAT_0438bf7c)", get_fade },
};
#define K_N ((int)(sizeof k_map / sizeof k_map[0]))

/* Any dword of the working save banks (DAT_044e3798 + slot·0x2dfc8 + off) is
 * mirrored generically: the port's save_work arena has the engine layout. */
#define WORK_BANK_VA   0x44e3798u
#define WORK_BANK_SIZE 0x2dfc8u
#define WORK_BANKS     8u
/* The window NPCs (DAT_073a7f80 + i·100, FUN_0046f2a3's records) + the
 * spawn cursor / warmup latch, mirrored generically by record offset. */
#define BG_NPC_VA 0x73a7f80u
static int32_t bg_npc_dword(uint32_t va)
{
    if (va == 0x73a8bb4u) return scene1_bg_npc_dbg_cursor();
    if (va == 0x73a8bb8u) return scene1_bg_npc_dbg_warmup_done();
    const scene1_bg_npc_t *m = &g_scene1_bg_npc[(va - BG_NPC_VA) / 100];
    const uint32_t off = (va - BG_NPC_VA) % 100;
    if (off < 0x2c) return off / 4 < BG_NPC_REC_DWORDS ? m->arec[off / 4] : 0;
    switch (off) {
    case 0x2c: return fbits(m->x);
    case 0x30: return fbits(m->y);
    case 0x34: return fbits(m->z);
    case 0x44: return m->dir;
    case 0x48: return m->visible;
    case 0x4c: return m->type;
    case 0x50: return fbits(m->speed);
    case 0x54: return m->pause;
    case 0x58: return fbits(m->vthresh);
    case 0x5c: return m->mode;
    case 0x60: return m->prob;
    default:   return 0;
    }
}
static int is_bg_npc_va(uint32_t va)
{
    return (va >= BG_NPC_VA && va < BG_NPC_VA + SCENE1_BG_NPC_COUNT * 100u && va % 4 == 0)
        || va == 0x73a8bb4u || va == 0x73a8bb8u;
}

/* the floor-flow / pause scalars the port keeps one by one */
static const struct { uint32_t va; const int32_t *p; } k_flow[] = {
    { 0x438b4b4u, &g_floor_intro },     { 0x438b8d0u, &g_floor_frames },
    { 0x438b1e8u, &g_floor_time },      { 0x438c85cu, &g_floor_timer[0] },
    { 0x438be94u, &g_floor_end_timer }, { 0x438be98u, &g_floor_end_kind },
    { 0x438be9cu, &g_portal_prompt },   { 0x438bea8u, &g_portal_return },
    { 0x438c14cu, &g_portal_cooldown }, { 0x438bea0u, &g_equip_prompt },
    { 0x438b8b0u, &g_ctrl_lock },       { 0x438b770u, &g_fall_timer },
    { 0x438b8a4u, &g_event_tick },      { 0x438b8a0u, &g_event_stage },
    { 0x438cbe0u, &g_ghost_timer },     { 0x438cbe4u, &g_ghost_count },
    { 0x438b938u, &g_floor_event_n },   { 0x438b950u, &g_dat_0438b950 },
    { 0x438b92cu, &g_news_ticker_timer }, { 0x438b1bcu, &g_actor_alloc_n },
    { 0x438b4dcu, &g_stage_b4dc },      { 0x438b4d4u, &g_dat_0438b4d4 },
    { 0x438b900u, &g_orb_pick_n },      { 0x438b90cu, &g_kill_combo },
    { 0x438b910u, &g_kill_combo_type }, { 0x438b914u, &g_kill_combo_timer },
    { 0x438b904u, &g_scene1_combat_dat_0438b904 },
    { 0x438b908u, &g_scene1_combat_dat_0438b908 },
    { 0x438bed8u, &g_scene1_combat_dat_0438bed8 },
    { 0x6a46f94u, &g_scene1_combat_dat_06a46f94 },
    { 0x648290u, &g_got_item },         { 0x648294u, &g_got_item_on },
    { 0x438b764u, &g_notice_on },       { 0x438b758u, &g_notice_item },
    { 0x438b768u, &g_notice_ticks },
    { 0x438b7acu, &g_cam_blend },      { 0x438b788u, &g_cut_follow },
    { 0x438beb4u, &g_dat_0438beb4 },   { 0x438bebcu, &g_dat_0438bebc }, { 0x438beb8u, &g_dat_0438beb8 },
    { 0x6a46fa4u, &g_dat_06a46fa4 },   { 0x438beccu, &g_dat_0438becc },
    { 0x438cc14u, &g_shake_ticks },    { 0x438cc18u, &g_shake_dx },   { 0x438cc1cu, &g_shake_dz },
    { 0x74b2878u, &g_pause_sel },       { 0x74b2870u, &g_pause_sel_anim },
    { 0x74b2880u, &g_pause_sub_anim },  { 0x74b2884u, &g_pause_sub_dir },
    { 0x73e154cu, &g_pause_count },     { 0x74b2830u, &g_pause_exit_confirm },
    { 0x74b2834u, &g_pause_save_cursor }, { 0x74b2820u, &g_pause_save_scroll },
    { 0x74b289cu, &g_pause_save_phase }, { 0x74b28a0u, &g_pause_save_warn }, { 0x74b28a4u, &g_pause_save_overwrite },
    { 0x6a499a8u, &g_pause_saved_mode }, { 0x74b2898u, &g_pause_save_vscroll },
    { 0x74b2894u, &g_pause_save_hscroll }, { 0x74b28a8u, &g_pause_items_done },
    { 0x74b28acu, &g_pause_items_opt },
};

/* Retail-layout blocks the port keeps byte for byte: the party block
 * (party.h), the actor table (actor_table.h), the stage objects
 * (dungeon_objects.h), the number popups (actor_ai.h) and the floor-flow
 * scalars (dungeon_flow.h). */
static const int32_t *layout_dword(uint32_t va)
{
    if (va % 4) return NULL;
    if (va >= PARTY_VA && va < PARTY_VA + PARTY_SIZE) return &PW(va).i;
    if (va >= 0x76b970u && va < 0x76b970u + ACTOR_COUNT * ACTOR_SIZE)
        return (const int32_t *)(const void *)((const uint8_t *)g_actor + (va - 0x76b970u));
    if (va >= 0x695ef78u && va < 0x695ef78u + DOBJ_COUNT * sizeof(dobj_t))
        return (const int32_t *)(const void *)((const uint8_t *)g_dobj + (va - 0x695ef78u));
    if (va >= 0x44e28f0u && va < 0x44e28f0u + sizeof g_popup)
        return &g_popup[0][0] + (va - 0x44e28f0u) / 4;
    if (va >= 0x69324b0u && va < 0x69324b0u + SCENE1_RECORDS_B_COUNT * SCENE1_RECORDS_B_STRIDE * 4u)
        return &g_scene1_records_b[(va - 0x69324b0u) / 4];
    if (va >= 0x6956cb0u && va < 0x6956cb0u + SCENE1_RECORDS_C_COUNT * SCENE1_RECORDS_C_STRIDE * 4u)
        return &g_scene1_records_c[(va - 0x6956cb0u) / 4];
    if (stage_post_load_va(va)) return stage_post_load_va(va);
    if (va >= 0x73a6db8u && va < 0x73a6db8u + sizeof g_story_visitor)
        return &g_story_visitor[(va - 0x73a6db8u) / 4];
    if (va >= 0x73a8b38u && va < 0x73a8b38u + sizeof g_story_visitor_pos)
        return (const int32_t *)(const void *)&g_story_visitor_pos[0][0] + (va - 0x73a8b38u) / 4;
    if (va >= 0x73a8b50u && va < 0x73a8b50u + sizeof g_story_visitor_chr)
        return &g_story_visitor_chr[0][0] + (va - 0x73a8b50u) / 4;
    if (va == 0x73a8bbcu) return &g_story_visitor_frame;
    if (va == 0x5c7df0u) return &g_story_visitor_meet;
    if (va >= 0x438bfb0u && va < 0x438bfb0u + sizeof g_mapcol_objs)
        return (const int32_t *)(const void *)((const uint8_t *)&g_mapcol_objs + (va - 0x438bfb0u));
    if (va >= 0x73a6e50u && va < 0x73a6e50u + 4u * CS_NPC_MAX * CS_NPC_STRIDE) {
        const uint32_t d = (va - 0x73a6e50u) / 4;
        return scene1_customer_npc_slot((int)(d / CS_NPC_STRIDE)) + d % CS_NPC_STRIDE;
    }
    if (va >= 0x74b2844u && va < 0x74b2844u + 4u * SCENE_PAUSE_MAX_ENTRIES)
        return &g_pause_entries[(va - 0x74b2844u) / 4];
    for (size_t k = 0; k < sizeof k_flow / sizeof k_flow[0]; k++)
        if (k_flow[k].va == va) return k_flow[k].p;
    return NULL;
}

typedef struct { uint32_t va; int32_t (*get)(void); } sel_t;
static sel_t s_sel[64];
static int s_nsel;

static int32_t bank_dword(uint32_t va)
{
    uint32_t rel = va - WORK_BANK_VA;
    const uint32_t *w = save_work_dwords_at((int)(rel / WORK_BANK_SIZE));
    return w ? (int32_t)w[(rel % WORK_BANK_SIZE) / 4] : 0;
}

int state_va_select(const char *list, uint32_t *bad)
{
    /* no strtok: the caller (main.c parse_cmdline) is mid-strtok over argv */
    const char *t = list ? list : "";
    s_nsel = 0;
    for (; *t; t += (*t == ',')) {
        char *end;
        uint32_t va = (uint32_t)strtoul(t, &end, 16);
        if (end == t) break;
        t = end;
        int k = 0;
        while (k < K_N && k_map[k].va != va) k++;
        int in_bank = va >= WORK_BANK_VA && va < WORK_BANK_VA + WORK_BANKS * WORK_BANK_SIZE &&
                      ((va - WORK_BANK_VA) % WORK_BANK_SIZE) % 4 == 0;
        if (k == K_N && !in_bank && !is_bg_npc_va(va) && layout_dword(va) == NULL) {
            /* no port mirror: skip it (the diff reports it retail-only) and tell
             * the caller — a missing mirror must not kill the whole run */
            if (bad) *bad = va;
            continue;
        }
        if (s_nsel < (int)(sizeof s_sel / sizeof s_sel[0]))
            s_sel[s_nsel++] = (sel_t){ va, k < K_N ? k_map[k].get : NULL };
    }
    return s_nsel;
}

void state_va_log(FILE *fp, uint32_t frame)
{
    if (!fp || s_nsel == 0) return;
    fprintf(fp, "post %u", (unsigned)frame);
    for (int i = 0; i < s_nsel; i++)
        fprintf(fp, " %x=%d", (unsigned)s_sel[i].va,
                (int)(s_sel[i].get ? s_sel[i].get()
                      : is_bg_npc_va(s_sel[i].va) ? bg_npc_dword(s_sel[i].va)
                      : layout_dword(s_sel[i].va) ? *layout_dword(s_sel[i].va)
                      : bank_dword(s_sel[i].va)));
    fputc('\n', fp);
#if defined(__i386__) || defined(__x86_64__)
    /* x87 control word (rhost logs retail's per frame): "cw N xxxx" on change */
    static unsigned s_last_cw = 0x10000u;
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    if (cw != s_last_cw) {
        fprintf(fp, "cw %u %04x\n", (unsigned)frame, (unsigned)cw);
        s_last_cw = cw;
    }
#endif
}

const char *state_va_known(void)
{
    static char out[2048];
    size_t o = 0;
    out[0] = '\0';
    for (int k = 0; k < K_N && o < sizeof out; k++)
        o += (size_t)snprintf(out + o, sizeof out - o, "%s%x %s", k ? ", " : "",
                              (unsigned)k_map[k].va, k_map[k].name);
    if (o < sizeof out)
        snprintf(out + o, sizeof out - o, ", any work-bank dword 44e3798.., bg-NPC 73a7f80+i*100+off, 73a8bb4/bb8");
    return out;
}

/* --block-dump: the retail-layout blocks at one frame, for rhost.py portblock:
 * "PBLK" frame n, n x (va, len, bytes) */
static const struct { uint32_t va, len; } k_blocks[] = {
    { PARTY_VA, PARTY_SIZE },
    { 0x76b970u, ACTOR_COUNT * ACTOR_SIZE },
    { 0x695ef78u, DOBJ_COUNT * (uint32_t)sizeof(dobj_t) },
    { 0x44e28f0u, (uint32_t)sizeof g_popup },
    { 0x69324b0u, SCENE1_RECORDS_B_COUNT * SCENE1_RECORDS_B_STRIDE * 4u },
    { 0x6956cb0u, SCENE1_RECORDS_C_COUNT * SCENE1_RECORDS_C_STRIDE * 4u },
    { 0x438bedcu, 0x60u },
    { 0x438bfb0u, (uint32_t)sizeof g_mapcol_objs },
};

static void put32(FILE *fp, uint32_t v) { fwrite(&v, 4, 1, fp); }

/* a port pointer into a retail-layout block → that block's retail VA (retail
 * stores e.g. a record's owner as &DAT_056da1b8; the port as &g_party) */
static uint32_t to_retail_ptr(uint32_t v)
{
    const struct { const void *p; uint32_t va, len; } k[] = {
        { &g_party, PARTY_VA, PARTY_SIZE },
        { g_actor, 0x76b970u, ACTOR_COUNT * ACTOR_SIZE },
        { g_dobj, 0x695ef78u, DOBJ_COUNT * (uint32_t)sizeof(dobj_t) },
        { g_scene1_records_b, 0x69324b0u, SCENE1_RECORDS_B_COUNT * SCENE1_RECORDS_B_STRIDE * 4u },
        { g_scene1_records_c, 0x6956cb0u, SCENE1_RECORDS_C_COUNT * SCENE1_RECORDS_C_STRIDE * 4u },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        const uint32_t base = (uint32_t)(uintptr_t)k[i].p;
        if (v >= base && v < base + k[i].len) return k[i].va + (v - base);
    }
    return v;
}

void state_va_block_dump(const char *path, uint32_t frame)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return;
    const uint32_t nb = (uint32_t)(sizeof k_blocks / sizeof k_blocks[0]);
    const uint32_t nf = (uint32_t)(sizeof k_flow / sizeof k_flow[0]);
    fwrite("PBLK", 4, 1, fp);
    put32(fp, frame);
    put32(fp, nb + nf + 1);
    for (uint32_t k = 0; k < nb; k++) {
        put32(fp, k_blocks[k].va);
        put32(fp, k_blocks[k].len);
        for (uint32_t o = 0; o < k_blocks[k].len; o += 4) {
            const int32_t *p = layout_dword(k_blocks[k].va + o);
            put32(fp, p ? to_retail_ptr((uint32_t)*p) : 0u);
        }
    }
    for (uint32_t k = 0; k < nf; k++) {
        put32(fp, k_flow[k].va);
        put32(fp, 4);
        put32(fp, (uint32_t)*k_flow[k].p);
    }
    const int slot = save_work_active_slot();
    const uint32_t *w = save_work_dwords_at(slot);
    put32(fp, WORK_BANK_VA + (uint32_t)slot * WORK_BANK_SIZE);
    put32(fp, WORK_BANK_SIZE);
    for (uint32_t o = 0; o < WORK_BANK_SIZE; o += 4) put32(fp, w ? w[o / 4] : 0u);
    fclose(fp);
}
