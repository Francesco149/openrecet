/*
 * dungeon_results.c — see dungeon_results.h.  Line refs are retail VAs.
 */
#include "dungeon_results.h"

#include <math.h>
#include <string.h>

#include "audio.h"               /* FUN_00499519 */
#include "chara_equip.h"         /* DAT_0438b7d8 */
#include "customer_service.h"    /* FUN_00460083 */
#include "d3d_pool.h"            /* FUN_00473672 = FUN_00471905(5) */
#include "dungeon_flow.h"        /* DAT_0438be94 / be98 */
#include "dungeon_party.h"       /* FUN_00484da3 */
#include "fade.h"                /* FUN_004526f5 / FUN_004528b3 / FUN_0045281c */
#include "scene.h"               /* DAT_0438b1c0 */
#include "scene_adv_guild.h"     /* the bag DAT_0438b5ec, its twin DAT_0438b684 */
#include "scene1_combat_sm.h"    /* DAT_0438b4c8 */
#include "scene1_day_close.h"    /* DAT_0438b4e0, FUN_0045e028 */
#include "scene1_display_menu.h" /* the item window */
#include "save_work.h"
#include "sim.h"                 /* DAT_073dddd4, DAT_06a499a0 */
#include "stage_gate.h"          /* DAT_0438b4cc */
#include "stage_load_pulse.h"    /* FUN_004682d0 */
#include "stage_palette.h"       /* DAT_0438b4dc */
#include "stage_post_load.h"     /* FUN_00435c98 */
#include "tables_item.h"         /* FUN_004681f6 */
#include "title_save_dialog.h"   /* the shared hand cursor */
#include "worker_load.h"         /* FUN_00452cde */
#include "x87.h"

int32_t g_results_leaving;   /* DAT_074b2f08 */
int32_t g_results_kept_all;  /* DAT_074b2f5c */
int32_t g_results_intro;     /* DAT_074b2f60 */
int32_t g_results_kind;      /* DAT_074b2f64 */
int32_t g_results_kept[4];   /* DAT_0438b718 */
int32_t g_results_row;       /* DAT_0438b744 */
int32_t g_results_picking;   /* DAT_0438b748 */

static uint8_t *work(void) { return save_work_bank_at(save_work_active_slot()); }
static int32_t *wdw(uint8_t *w, uint32_t off) { return (int32_t *)(void *)(w + off); }

/* FUN_0048f848: every chara's departure gear (+0x18..) back over its live
 * slots (+0x4..), 5 dwords each */
static void restore_departure_gear(void)
{
    uint8_t *w = work();
    if (w == NULL) return;
    for (int c = 0; c < 8; c++) {
        int32_t *g = wdw(w, 0x2ceb4 + 0x6cu * (uint32_t)c);
        for (int k = 0; k < 5; k++) g[k] = g[k + 5];
    }
}

/* FUN_00442c86 */
void dungeon_results_strip(void)
{
    int *bag = adv_guild_bag();
    const int n = adv_guild_bag_size();
    for (int k = 0; k < n; k++)
        if (bag[k] != -1 && (bag[k] & 0x20)) bag[k] = -1;
    uint8_t *w = work();
    if (w != NULL) {
        const int32_t *g = wdw(w, 0x2ceb4 + 0x6cu * (uint32_t)chara_equip_get_current_chara());
        for (int k = 0; k < 5; k++)
            if (g[k] != -1 && !(g[k] & 0x20)) (void)party_bag_add(g[k], 38);   /* FUN_00484da3 */
    }
    restore_departure_gear();
}

/* the item `item` brought from the shop (DAT_0438b684) — that entry spent, 1 —
 * or found in the dungeon (0: logged FUN_00460083(item, 2)) */
static int take_brought(int32_t item)
{
    int *brought = adv_guild_bag_brought();
    for (int k = 0; k < 38; k++) {
        if (brought[k] == item) {
            brought[k] = -1;
            return 1;
        }
    }
    return 0;
}

/* FUN_0048f7f0 */
void dungeon_results_unpack(void)
{
    uint32_t *wd = save_work_dwords_at(save_work_active_slot());
    const int *bag = adv_guild_bag();
    for (int k = 0; k < 38; k++) {
        const int32_t item = bag[k];
        if (item == -1) continue;
        const int brought = take_brought(item);
        display_menu_inventory_return(wd, item);   /* FUN_00468d22 */
        if (!brought) cs_item_log_append(item, 2);
    }
}

/* FUN_0048f8bb: + ftol(sqrt(price / 10 + 10)) to the expedition score */
static void score_item(int32_t item)
{
    uint8_t *w = work();
    if (w == NULL) return;
    const int slot = tables_item_find_slot_by_id(&g_item, item >> 6);
    const int32_t price = slot >= 0 ? g_item.records[slot].price : 0;
    const float r = spill32((long double)spill32((long double)sqrt(spill32((long double)(price / 10 + 10)))));
    int32_t *sc = wdw(w, 0x2c3e0);
    *sc = x87_ftol((long double)spill32((long double)*sc) + r);
}

/* FUN_0048f87c */
void dungeon_results_open(void)
{
    g_results_leaving = 0;
    dungeon_results_strip();                   /* FUN_00442c86 */
    g_results_kind = g_floor_end_kind;
    if (g_floor_end_kind > 1) {
        int *bag = adv_guild_bag();
        for (int k = 0; k < 38; k++)
            if (bag[k] != -1) bag[k] &= ~0x10;
    }
    g_results_intro = 1;
}

static void cursor_to_row(void)
{
    title_save_dialog_cursor_slide(32.0f, spill32((long double)spill32((long double)(36 * g_results_row)) + 100.0f));
}

/* the screen's end (once the fade is done) */
static void results_leave(void)
{
    uint8_t *w = work();
    if (w == NULL) return;
    int32_t *score = wdw(w, 0x2c3e0);
    if (g_results_kind == 1) *score -= 30;
    else if (g_results_kind == 2) *score += 10;
    else *score += 30;
    g_floor_end_kind = 0;
    *score += 5 * stage_gate_get_next() + 5;
    title_save_dialog_cursor_set_visible(0);   /* FUN_00435612 */
    stage_load_pulse_set_active(0);            /* FUN_004682d0 */
    int32_t *period = wdw(w, 0x2c3f0);
    ++*period;
    shop_period_events_clear(w);               /* FUN_0045e028 */
    if (*wdw(w, 0x2ce08) != 0) {
        *wdw(w, 0x2ce08) = 0;
        ++*period;
        shop_period_events_clear(w);
    }
    g_floor_end_timer = 0;
    /* PORT-DEBT(render-coupled, FUN_00474d92): the dungeon's D3D teardown */
    d3d_pool_release_type(5);                  /* FUN_00473672 */
    sim_set_mode_9a0(0);                       /* FUN_00453373 */
    if (g_results_kept_all != 0) {
        int32_t *deep = wdw(w, 0x2c404 + 4u * (uint32_t)g_scene1_combat_stage_id);
        if (*deep < stage_gate_get_next()) *deep = stage_gate_get_next();
    } else {
        uint32_t *wd = save_work_dwords_at(save_work_active_slot());
        for (int k = 1; k <= 3; k++) {
            if (g_results_kept[k] == -1) continue;
            const int brought = take_brought(g_results_kept[k]);
            g_results_kept[k] &= ~0x10;
            if (!brought) cs_item_log_append(g_results_kept[k], 2);
            display_menu_inventory_return(wd, g_results_kept[k]);   /* FUN_00468d22 */
            score_item(g_results_kept[k]);                         /* FUN_0048f8bb */
        }
    }
    if (w[0x2bcbb] != 0) {
        w[0x2bcbb] = 0;
        w[0x2bcbc] = 1;
        g_scene_state = 1;
        g_stage_b4dc = *wdw(w, 0x2cde0);
        stage_post_load_init();                /* FUN_00435c98 */
    } else if (*period < 4) {
        g_scene_state = 8;
    } else if (w[0x2bd02] != 0) {
        g_ctrl_lock = -1;
        *period = 3;
        *wdw(w, 0x2ce08) = 1;
        g_scene_state = 8;
    } else {
        g_stage_b4dc = *wdw(w, 0x2cde0);
        g_scene_state = 1;
        stage_post_load_init();                /* FUN_00435c98 */
        day_close_arm();                       /* DAT_0438b4e0 = 1 */
    }
    fade_phase_out_start(0, 0x11);             /* FUN_0045281c(0, 17) */
    worker_load_spawn();                       /* FUN_00452cde */
}

/* FUN_0048f931 */
void dungeon_results_tick(void)
{
    const uint32_t pressed = g_sim_buttons[0].pressed | ((uint32_t)g_sim_buttons[0].held << 16);   /* DAT_073dddd4 */
    uint8_t *w = work();
    if (g_results_leaving != 0) {
        if (fade_is_done())                    /* FUN_004528b3 */
            results_leave();
        goto tail;
    }
    if (g_results_intro != 0) {
        if (++g_floor_end_timer != 150) goto tail;
        display_menu_open(6, 1);               /* FUN_00468338(6, 1) */
        g_results_intro = 0;
        if (g_results_kind > 1) {
            g_results_kept_all = 1;
            goto tail;
        }
        g_results_kept_all = 0;
        cursor_to_row();
        goto tail;
    }
    {
        const int32_t lvl = w ? *wdw(w, 0x2c400) : 0;
        const int32_t slots = lvl >= 49 ? 3 : lvl >= 15 ? 2 : 1;
        if (g_results_kept_all == 1) {
            const int r = display_menu_update(0);   /* FUN_00469414(0) */
            int n = 0;
            const int *bag = adv_guild_bag();
            for (int k = 0; k < 38; k++) if (bag[k] != -1) ++n;
            if ((r != 4 || n == 0 || (pressed & 0x20)) && (pressed & 0x30)) {
                title_save_dialog_cursor_set_visible(0);
                audio_play_se_by_id(323);
                dungeon_results_unpack();      /* FUN_0048f7f0 */
                g_results_leaving = 1;
                fade_phase1_start(0, 0x11);    /* FUN_004526f5(0, 17) */
            }
            goto tail;
        }
        if (g_results_picking != 0) {
            const int r = display_menu_update(1);
            if (r == 1) {
                g_results_picking = 0;
                g_results_kept[g_results_row] = display_menu_swap_selected(g_results_kept[g_results_row]);   /* FUN_00468f6d */
                cursor_to_row();
            } else if (r == 3) {
                audio_play_se_by_id(323);
            } else if (r == 2) {
                audio_play_se_by_id(317);
                g_results_picking = 0;
                cursor_to_row();
            }
            goto tail;
        }
        if (display_menu_detail() != 0) {       /* FUN_004681e6 */
            if (pressed & 0x70) {
                display_menu_detail_close();   /* FUN_004681d3 */
                title_save_dialog_cursor_set_visible(1);
                audio_play_se_by_id(710);
            }
            goto tail;
        }
        if (pressed & 0x40) {
            if (g_results_row > 0 && g_results_kept[g_results_row] != -1) {
                display_menu_detail_open(0);   /* FUN_004681db(0) */
                display_menu_set_highlight(g_results_kept[g_results_row]);   /* FUN_00468286 */
                title_save_dialog_cursor_set_visible(0);
                audio_play_se_by_id(710);
            }
            goto tail;
        }
        if (pressed & 0x10) {
            if (g_results_row != 0) {
                g_results_picking = 1;
                display_menu_cursor_to_row();  /* FUN_0046939a */
            } else {
                g_results_leaving = 1;
                fade_phase1_start(0, 0x11);
            }
            audio_play_se_by_id(323);
            goto tail;
        }
        int moved = 0;
        if (pressed & 0x40000) {
            g_results_row = (g_results_row + slots) % (slots + 1);
            moved = 1;
        } else if (pressed & 0x80000) {
            g_results_row = (g_results_row + slots + 2) % (slots + 1);
            moved = 1;
        }
        if (moved) audio_play_se_by_id(326);
        display_menu_set_highlight(g_results_row != 0 ? g_results_kept[g_results_row] : -1);
        if (moved) cursor_to_row();
    }
tail:
    title_save_dialog_anim_tick();             /* FUN_004356cd */
}
