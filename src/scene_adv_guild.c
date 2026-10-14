/*
 * scene_adv_guild.c — see scene_adv_guild.h.
 *
 * Work-bank fields (base DAT_044e3798 + slot·0x2dfc8; the port pins the
 * location index DAT_0438b1e0 to the active save slot, as the guild does):
 *   bytes  0x2bc4c+k  adventurer k recruited (FUN_0047f1ce)
 *          0x2bc53    the 4th dungeon's unlock half (with 0x2bd00 == 1)
 *          0x2bc7c    the intro is pending (set by the story; cleared at its end)
 *          0x2bc7d/e  iv2_7 / iv2_8 played;  0x2bc7f the intro's exit latch
 *          0x2bc81    fees + the dungeon map are on (off for the first trip)
 *          0x2bcb9 / 0x2bce1 / 0x2bcf3  dungeons 3/4/5 open
 *          0x2bce2/3  the first trip into dungeon 3's top floor
 *   dwords 0xb0fc     the period (FUN_00435c98's +1 on departure)
 *          0xb100     merchant level (bag size; fees halve at 20)
 *          0xb101+i   dungeon i's deepest floor
 *          0xb10a+20i+n  dungeon i's floor-group n cleared
 *          0xb37d     seat at the door on the next house load
 *          0xb37e/f, 0xb380  the last adventurer / dungeon / start floor
 *          0xb381     DAT_0451059c, set to 1 on entry
 *          0xb3ac+27k adventurer k: [0] level, [1..10] equipment, [20] trips
 */
#include "scene_adv_guild.h"

#include <stdint.h>
#include <stddef.h>   /* NULL */
#include <stdio.h>    /* snprintf — the too-many-items box */

#include "audio.h"                  /* audio_play_se_by_id (FUN_00499519)          */
#include "chara_equip.h"            /* FUN_004844ef + DAT_0438b7d8                 */
#include "fade.h"                   /* FUN_004526f5 / 0045281c / 004528b3          */
#include "party_roster.h"           /* FUN_0047f1ce, DAT_0741bed8 / DAT_07477e70  */
#include "save_bank.h"              /* SAVE_BANK_FIELD_GOLD / CLOCK_TARGET / …     */
#include "save_work.h"              /* save_work_dwords_at / _active_slot          */
#include "scene.h"                  /* g_scene_state (DAT_0438b1c0)                */
#include "scene1_combat_sm.h"       /* g_scene1_combat_stage_id (DAT_0438b4c8)     */
#include "scene1_day_close.h"       /* day_close_clear (DAT_0438b4e0) / FUN_0045e028 */
#include "scene1_display_menu.h"    /* the shared item window (FUN_004681xx-469xxx) */
#include "scene1_intro_dialogue.h"  /* scene1_intro_dialogue_start_single          */
#include "scene1_tutorial_dispatch.h" /* scene1_beat_set_flag (DAT_0438b928)       */
#include "scene_worldmap.h"         /* scene_worldmap_set_sel_dest (FUN_0049de0e)  */
#include "stage_gate.h"             /* stage_gate_set_next (DAT_0438b4cc)          */
#include "stage_load_pulse.h"       /* FUN_004682c5 / d0 / e3                      */
#include "stage_post_load.h"        /* FUN_00435c98, DAT_0438b4d0                  */
#include "sim.h"                    /* g_sim_buttons (DAT_073dddd4)                */
#include "title_save_dialog.h"      /* the hand cursor + the message box           */
#include "worker_load.h"            /* worker_load_spawn / _set_cb (FUN_00452cde)  */

#define W_PERIOD           SAVE_BANK_FIELD_CLOCK_TARGET     /* 0xb0fc */
#define W_MERCHANT_LEVEL   SAVE_BANK_FIELD_MERCHANT_LEVEL   /* 0xb100 */
#define W_DEEPEST          0xb101u
#define W_CLEARED          0xb10au
#define W_SEAT_AT_DOOR     SAVE_BANK_FIELD_HOUSE_CAM_FLAG   /* 0xb37d */
#define W_LAST_CHARA       0xb37eu
#define W_LAST_DUNGEON     0xb37fu
#define W_LAST_FLOOR       0xb380u
#define W_RESUME_MODE      SAVE_BANK_FIELD_RESUME_MODE      /* 0xb381 */
#define W_CHARA            SAVE_BANK_CHARA_BASE_DWORD       /* 0xb3ac */
#define W_CHARA_STRIDE     SAVE_BANK_CHARA_STRIDE_DWORDS    /* 27 */
#define B_DUNGEON4_A       0x2bc53u
#define B_INTRO_PENDING    0x2bc7cu
#define B_INTRO_1          0x2bc7du
#define B_INTRO_2          0x2bc7eu
#define B_INTRO_EXIT       0x2bc7fu
#define B_LOUIE            0x2bc4cu
#define B_FEES_ON          0x2bc81u
#define B_DUNGEON2         0x2bcb9u
#define B_DUNGEON3         0x2bce1u
#define B_D3_FIRST_DONE    0x2bce3u
#define B_D3_FIRST         0x2bce2u
#define B_DUNGEON4         0x2bcf3u
#define B_DUNGEON4_B       0x2bd00u

/* FUN_00491b16's total past which the bag can't be packed (15000 − a bag). */
#define ADV_GUILD_ITEM_LIMIT 14970

/* .data (vendor/unpacked):
 *   DAT_005c64c0  start floors per dungeon (floors of 5);
 *   DAT_005cc680  the adventurer table (128 B each): +0x28 base fee,
 *                 +0x2c fee per level;
 *   DAT_005c5aa4/a8, DAT_005c5aac/b0  the adventurer / bag cursor origins;
 *   DAT_005c5ab4  the 6 dungeon markers {x, y}. */
static const int   k_floor_cap[6]  = { 1, 3, 6, 10, 20, 12 };
static const int   k_fee_base[8]   = { 50, 100, 200, 300, 500, 500, 1000, 2000 };
static const int   k_fee_level[8]  = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const float k_chara_cur_x   = 304.0f, k_chara_cur_y = 80.0f;
static const float k_bag_cur_x     = 16.0f,  k_bag_cur_y   = 124.0f;
static const float k_dungeon_xy[6][2] = {
    { -16.0f, 112.0f }, { 112.0f, 179.0f }, { 240.0f,  96.0f },
    { 408.0f, 160.0f }, { 488.0f,  64.0f }, { 272.0f, 256.0f },
};

static const char k_msg_pick_items[] =
    "Pick the items you wish to bring with you.<BR>When finished, select the "
    "\"Depart!\" option<BR>at the top of the list.";

/* ── state (BSS unless noted) ─────────────────────────────────────────── */
struct adv_guild_state {
    int floors[6];   /* DAT_06a49b2c[6] — start floors offered per dungeon */
    int commit;      /* DAT_06a49b44 — 1 depart, 2 leave (waits for the fade) */
    int map_open;    /* DAT_06a49b48 — the dungeon map                      */
    int floor_open;  /* DAT_06a49b4c — the start-floor list                 */
    int dungeon;     /* DAT_06a49b50                                        */
    int floor;       /* DAT_06a49b54                                        */
    int floor_top;   /* DAT_06a49b58 — floor-list scroll                    */
    int chara_open;  /* DAT_06a49b5c — the adventurer list                  */
    int bag_mode;    /* DAT_06a49b60 — 1 the bag, 2 the item picker        */
    int chara_top;   /* DAT_06a49b64 — adventurer-list scroll               */
    int bag_top;     /* DAT_06a49b68 — bag scroll                           */
    int ticks;       /* DAT_06a49b6c                                        */
    int msg;         /* DAT_06a49b70 — the "Pick the items" box: 1 open, 2 up */
    int dungeons;    /* DAT_06a49b74 — map entries 2..6                     */
    int equipped;    /* DAT_06a49b78 — the hired one's filled equip slots   */
    int bag_size;    /* DAT_005c5a98 (.data 38)                              */
    int chara_row;   /* DAT_005c5a9c (.data 1) — 0 = Leave                   */
    int bag_row;     /* DAT_005c5aa0 (.data 1) — 0 = Depart!                 */
};
static struct adv_guild_state s = { .bag_size = 38, .chara_row = 1, .bag_row = 1 };

/* DAT_0438b5ec[38] (the engine indexes it 1-based off DAT_0438b5e8) and its
 * twin DAT_0438b684[38] (DAT_0438b680[k]). */
static int s_bag[ADV_GUILD_BAG_SLOTS + 1];
static int s_bag2[ADV_GUILD_BAG_SLOTS + 1];

/* Dungeon-run globals with no other port owner: DAT_0438b4d4, DAT_0438b4d8. */
static int s_b4d4 = 0, s_b4d8 = 0;

int adv_guild_bag_slot(int k)
{
    return (k >= 1 && k <= ADV_GUILD_BAG_SLOTS) ? s_bag[k] : -1;
}

void scene_adv_guild_reset(void)
{
    s = (struct adv_guild_state){ .bag_size = 38, .chara_row = 1, .bag_row = 1 };
    for (int k = 0; k <= ADV_GUILD_BAG_SLOTS; k++)
        s_bag[k] = s_bag2[k] = 0;
    s_b4d4 = s_b4d8 = 0;
}

static uint32_t *work(void) { return save_work_dwords_at(save_work_active_slot()); }

static int32_t *chara_rec(uint32_t *w, int id)
{
    return (int32_t *)(w + W_CHARA + (uint32_t)(W_CHARA_STRIDE * id));
}

/* the hire fee: base + per-level · level, halved from merchant level 20. */
static int hire_fee(uint32_t *w, int id)
{
    int fee = k_fee_base[id] + k_fee_level[id] * chara_rec(w, id)[0];
    if ((int32_t)w[W_MERCHANT_LEVEL] >= 20)
        fee /= 2;
    return fee;
}

/* ── FUN_0045bdc2 — scene init (load-worker case 11) ──────────────────── */
static void adv_guild_enter(void)
{
    uint32_t *w = work();
    if (w == NULL)
        return;
    const uint8_t *bb = (const uint8_t *)w;

    title_save_dialog_gate_reset();          /* FUN_00434ce3 */
    s.ticks = 0;
    /* FUN_0047f172(0): the working bank → backup bank 0 (DAT_0741bee0), the
     * expedition's restore point (only the dungeon reads it back).
     * PORT-DEBT(stub, FUN_0047f172): no port backup bank; retire with the dungeon. */
    w[W_RESUME_MODE] = 1;
    /* FUN_0044c7bb: DAT_0438b8a4 = 0 — no port mirror (dungeon-side). */
    const int lvl = (int32_t)w[W_MERCHANT_LEVEL];
    s.bag_size = 20;
    if (lvl >= 16) s.bag_size = 25;
    if (lvl >= 23) s.bag_size = 30;
    if (lvl >= 39) s.bag_size = 35;
    for (int k = 1; k <= ADV_GUILD_BAG_SLOTS; k++)
        s_bag[k] = s_bag2[k] = -1;
    s.commit     = 0;
    s.chara_open = 1;
    s.bag_mode   = 0;
    s.map_open   = 0;
    s.floor_open = 0;
    s.dungeon    = (int32_t)w[W_LAST_DUNGEON];
    s.floor      = 0;
    s.floor_top  = 0;
    party_roster_rebuild();                  /* FUN_0047f1ce */
    /* the last adventurer's row (the last match wins) */
    int row = 0;
    for (int i = 1; i <= party_roster_count(); i++)
        if (party_roster_row_id(i) == (int32_t)w[W_LAST_CHARA])
            row = i;
    s.chara_row = row;
    s.chara_top = row > 4 ? row - 4 : 0;
    s.bag_row = 1;
    s.bag_top = 0;
    title_save_dialog_cursor_snap(k_chara_cur_x,
        (float)(43 * (s.chara_row - s.chara_top)) + k_chara_cur_y + 48.0f);
    for (int i = 0; i != 6; i++) {
        int n = ((int32_t)w[W_DEEPEST + (uint32_t)i] + 1) / 5 + 1;
        s.floors[i] = n;
        if (i == 5) {
            if (n > 4)
                s.floors[5] = 4;
        } else {
            if (n > k_floor_cap[i])
                s.floors[i] = k_floor_cap[i];
            if (i > 0 && w[W_CLEARED + (uint32_t)(20 * i + s.floors[i])] != 0)
                s.floors[i]++;
        }
    }
    g_scene1_combat_stage_id = 0;            /* DAT_0438b4c8 = 0 */
    s.dungeons = 2;
    if (bb[B_DUNGEON2] != 0) s.dungeons = 3;
    if (bb[B_DUNGEON3] != 0) s.dungeons = 4;
    if (bb[B_DUNGEON4] != 0) s.dungeons = 5;
    if (bb[B_DUNGEON4_A] != 0 && bb[B_DUNGEON4_B] == 1) s.dungeons = 6;
}

/* ── FUN_0044c7c3 — the first-visit intro.  0 none, 1 a script started, 2
 * done (out to the world map).  The caller runs only while no dialogue runs
 * (DAT_0438b1c8 == 0), so both arms always start their script: DAT_06a4706c
 * = 0, DAT_0438b1b8 = 0, DAT_005c7a2c/30 = 2/7 or 2/8, gate 2,
 * FUN_00452d07(1).  PORT-DEBT(stub, FUN_00452809): the blackout (DAT_0438bf74)
 * and the mode-1 load fades, as at the guild's iv1_3. */
static int adv_guild_intro(uint8_t *bb)
{
    if (bb[B_INTRO_PENDING] != 1)
        return 0;
    if (bb[B_INTRO_1] == 0) {
        bb[B_INTRO_1] = 1;
        scene1_intro_dialogue_start_single(2, 7);
        return 1;
    }
    if (bb[B_INTRO_2] == 0) {
        bb[B_INTRO_2]    = 1;
        bb[B_INTRO_EXIT] = 1;
        bb[B_LOUIE]      = 1;                /* Louie joins */
        scene1_intro_dialogue_start_single(2, 8);
        return 1;
    }
    if (bb[B_INTRO_EXIT] != 1)
        return 0;
    bb[B_INTRO_PENDING] = 0;
    bb[B_INTRO_EXIT]    = 0;
    return 2;
}

/* Out to the world map, the cursor on this guild (dest 6): FUN_00473969 (the
 * group-7 release — no port twin, as at the guild), the fade-in, the load. */
static void adv_guild_to_worldmap(void)
{
    g_scene_state = 8;
    scene_worldmap_set_sel_dest(6);          /* FUN_0049de0e(6) */
}

static void adv_guild_load_out(void)
{
    /* FUN_0045bcea: DAT_0438b4dc = the stage for (DAT_0438b4c8, DAT_0438b4cc)
     * via FUN_0045bc7f + FUN_0047fa4f (the house load re-derives it).
     * PORT-DEBT(stub, FUN_0045bcea): DAT_0438b4dc has no port mirror yet.
     * FUN_0044bce7: DAT_06a4707c/84/80/88/64 = 0 — no port mirrors. */
    fade_phase_out_start(0, 0x11);           /* FUN_0045281c(0,17) */
    worker_load_spawn();                     /* FUN_00452cde */
}

/* The departure / Leave commit (DAT_06a49b44 != 0, once the fade is done). */
static void adv_guild_commit(uint32_t *w)
{
    uint8_t *bb = (uint8_t *)w;
    const int chara = party_roster_row_id(s.chara_row);   /* DAT_07477e70[row] */
    chara_equip_set_current_chara(chara);    /* DAT_0438b7d8 */
    chara_equip_recompute_aggregate();       /* FUN_004844ef */
    const int dungeon = s.dungeon, floor = s.floor;
    stage_post_load_set_mode_b4d0(-1);       /* DAT_0438b4d0 */
    s_b4d4 = -1;
    scene1_beat_set_flag(0);                 /* DAT_0438b928 = 0 */
    /* DAT_0438b1e4 = 0 — no port mirror. */
    g_scene1_combat_stage_id = dungeon;      /* DAT_0438b4c8 */
    int next = stage_gate_get_next();        /* DAT_0438b4cc */
    if (dungeon == 5) {
        next = 10 * floor;
        s_b4d8 = next;
    } else if (floor < k_floor_cap[dungeon]) {
        next = 5 * floor;
    } else {
        /* past the regular start floors: the boss floor of dungeon 1..4 */
        if (dungeon == 1) next = 30;
        if (dungeon == 2) next = 33;
        if (dungeon == 3) next = 38;
        if (dungeon == 4) next = 46;
        g_scene1_combat_stage_id = 5;
        stage_post_load_set_mode_b4d0(dungeon);
        s_b4d4 = 0;
    }
    if (next < 0)
        next = 0;
    stage_gate_set_next(next);
    w[W_LAST_DUNGEON] = (uint32_t)dungeon;
    w[W_LAST_FLOOR]   = (uint32_t)floor;
    w[W_LAST_CHARA]   = (uint32_t)chara;

    if (s.commit == 2) {
        /* Leave: the packed items go back to the inventory. */
        for (int k = 1; k <= ADV_GUILD_BAG_SLOTS; k++)
            if (s_bag[k] != -1)
                display_menu_inventory_return(w, s_bag[k]);   /* FUN_00468d22 */
        adv_guild_to_worldmap();
    } else {
        if (dungeon == 3 && floor == 0 && bb[B_D3_FIRST_DONE] == 0)
            bb[B_D3_FIRST] = 1;
        /* close the bag's gaps, retail's way: bag_size passes of a
         * shift-left over the first bag_size slots */
        const int n = s.bag_size;
        for (int pass = n; pass != 0; pass--) {
            for (int i = 1; i <= n; i++) {
                if (s_bag[i] == -1) {
                    for (int j = i; j < n; j++)
                        s_bag[j] = s_bag[j + 1];
                    s_bag[n] = -1;
                }
            }
        }
        /* DAT_0438b1e8 = 0 — no port mirror. */
        day_close_clear();                   /* DAT_0438b4e0 = 0 */
        stage_post_load_init();              /* FUN_00435c98 */
        w[W_PERIOD]++;
        g_scene_state = 1;
        shop_period_events_clear(bb);        /* FUN_0045e028 */
        w[W_SEAT_AT_DOOR] = 1;
        int32_t *rec = chara_rec(w, chara);
        if (bb[B_FEES_ON] != 0) {
            int32_t gold = (int32_t)w[SAVE_BANK_FIELD_GOLD] - hire_fee(w, chara);
            w[SAVE_BANK_FIELD_GOLD] = (uint32_t)(gold < 0 ? 0 : gold);
        }
        rec[20]++;                           /* trips */
        /* The case-1 load that follows is the dungeon's (DAT_0438b4dc = its stage).
         * PORT-DEBT(stub, NONE): Depart loads the dungeon; no dungeon port yet. */
    }
    adv_guild_load_out();
}

/* ── FUN_0045c051 ─────────────────────────────────────────────────────── */
void scene_adv_guild_sim(void)
{
    uint32_t *w = work();
    if (w == NULL)
        return;
    uint8_t *bb = (uint8_t *)w;
    const uint16_t pressed = g_sim_buttons[0].pressed;   /* DAT_073dddd4 lo */
    const uint16_t held    = g_sim_buttons[0].held;      /* the repeat hi word */

    const int intro = adv_guild_intro(bb);
    if (intro == 1)
        return;
    if (intro == 2) {
        adv_guild_to_worldmap();
        fade_phase_out_start(0, 0x11);       /* FUN_0045281c(0,17) */
        worker_load_spawn();                 /* FUN_00452cde */
        return;
    }

    int slide_floor = 0;   /* v43: 2 → the floor cursor, 1 → the dungeon cursor */
    int slide_chara = 0;   /* v40 */
    int slide_bag   = 0;   /* v44 */

    if (s.msg == 1) {
        s.msg = 2;
        title_save_dialog_message_open(k_msg_pick_items);
        return;
    }
    if (s.msg == 2) {
        if (title_save_dialog_gate_tick() == 1)
            s.msg = 0;
        return;
    }
    if (title_save_dialog_gate_tick() == -1)
        return;
    if (s.ticks == 0) {
        const int owned = display_menu_owned_count(w);   /* FUN_00491b16 */
        if (owned > ADV_GUILD_ITEM_LIMIT) {
            char buf[256];
            snprintf(buf, sizeof buf, "You have too many items to enter<BR>the "
                     "dungeon. Please remove<BR>%d items.", owned - ADV_GUILD_ITEM_LIMIT);
            title_save_dialog_message_open(buf);
        }
    } else if (s.ticks == 1 && display_menu_owned_count(w) > ADV_GUILD_ITEM_LIMIT) {
        s.commit = 2;
        fade_phase1_start(0, 0x11);          /* FUN_004526f5(0,17) */
    }

    if (s.commit != 0) {
        if (fade_is_done())                  /* FUN_004528b3 */
            adv_guild_commit(w);
        goto tail;
    }

    if (s.floor_open == 1) {
        if (pressed & 0x20) {
            audio_play_se_by_id(317);
            s.floor_open = 0;
            goto slide_dungeon;
        }
        if (pressed & 0x10) {
            s.commit = 1;
            fade_phase1_start(0, 0x11);
            audio_play_se_by_id(323);
            goto tail;
        }
        const int n = s.floors[s.dungeon];
        const int vis = n < 7 ? n : 7;
        if ((held & 0x4) && s.floor > 0) {
            audio_play_se_by_id(326);
            s.floor--;
            slide_floor = 2;
            if (s.floor - s.floor_top < 0)
                s.floor_top--;
        }
        if ((held & 0x8) && s.floor < n - 1) {
            audio_play_se_by_id(326);
            slide_floor = 2;
            s.floor++;
            if (s.floor - s.floor_top > vis - 1)
                s.floor_top++;
            goto slide_floor_row;
        }
        goto slide_pending;
    }

    if (s.map_open == 1) {
        if (pressed & 0x20) {
            audio_play_se_by_id(317);
            s.map_open = 0;
            s.bag_mode = 1;
            stage_load_pulse_set_active(1);  /* FUN_004682c5 */
            goto slide_bag_row;
        }
        if (pressed & 0x10) {
            audio_play_se_by_id(323);
            s.floor_open = 1;
            slide_floor = 2;
            s.floor = (s.dungeon == (int32_t)w[W_LAST_DUNGEON]) ? (int32_t)w[W_LAST_FLOOR] : 0;
            if (s.floor > s.floors[s.dungeon] - 1)
                s.floor = s.floors[s.dungeon] - 1;
            s.floor_top = s.floor >= 7 ? s.floor - 6 : 0;
            goto slide_floor_row;
        }
        int last = s.dungeons - 1;
        if (last > 4)
            last = 4;
        if (s.dungeon == 5) {
            if (held & 0x4)      s.dungeon = 2;
            else if (held & 0x2) s.dungeon = 1;
            else if (held & 0x1) s.dungeon = 3;
            else goto tail;
            audio_play_se_by_id(326);
            goto slide_dungeon;
        }
        if (s.dungeons == 6 && (held & 0x8)) {
            s.dungeon = 5;
        } else {
            if (!(held & 0x2) || s.dungeon <= 0)
                goto map_right;
            s.dungeon--;
        }
        slide_floor = 1;
        audio_play_se_by_id(326);
    map_right:
        if ((held & 0x1) && s.dungeon < last) {
            s.dungeon++;
            audio_play_se_by_id(326);
            goto slide_dungeon;
        }
        goto slide_pending;
    }

    if (s.bag_mode != 1) {
        if (s.bag_mode == 2) {
            switch (display_menu_update(1)) {    /* FUN_00469414(1) */
            case 2:
                audio_play_se_by_id(317);
                break;
            case 3:
                audio_play_se_by_id(323);
                goto tail;
            case 1: {
                const int item = display_menu_selected();   /* FUN_00469a9f */
                if (s_bag[s.bag_row] != -1)
                    display_menu_inventory_return(w, s_bag[s.bag_row]);
                s_bag[s.bag_row]  = item;
                s_bag2[s.bag_row] = item;
                if (item != -1)
                    display_menu_inventory_remove(w, item);  /* FUN_00469241 */
                display_menu_open(0, 0);                     /* FUN_00468338(0,0) */
                stage_load_pulse_reset_counter_to_5();       /* FUN_004682e3 */
                break;
            }
            default:
                goto tail;
            }
            s.bag_mode = 1;
        } else {
            if (s.chara_open == 0)
                goto tail;
            if (pressed & 0x20) {
                s.commit = 2;
                fade_phase1_start(0, 0x11);
                audio_play_se_by_id(317);
                goto tail;
            }
            if (!(pressed & 0x10)) {
                const int rows = party_roster_count() + 1;
                const int vis = rows < 5 ? rows : 5;
                if ((held & 0x4) && s.chara_row > 0) {
                    audio_play_se_by_id(326);
                    s.chara_row--;
                    slide_chara = 1;
                    if (s.chara_row - s.chara_top < 0)
                        s.chara_top--;
                }
                if ((held & 0x8) && s.chara_row < rows - 1) {
                    audio_play_se_by_id(326);
                    s.chara_row++;
                    slide_chara = 1;
                    if (s.chara_row - s.chara_top > vis - 1)
                        s.chara_top++;
                }
                if (slide_chara == 0)
                    goto tail;
                goto slide_chara_row;
            }
            if (s.chara_row == 0) {
                s.commit = 2;
                fade_phase1_start(0, 0x11);
                title_save_dialog_cursor_set_visible(0);    /* FUN_00435612 */
                audio_play_se_by_id(323);
                goto tail;
            }
            const int id = party_roster_row_id(s.chara_row);
            if ((int32_t)w[SAVE_BANK_FIELD_GOLD] < hire_fee(w, id) &&
                bb[B_FEES_ON] != 0 && s.chara_row != 1) {
                audio_play_se_by_id(362);
                goto tail;
            }
            audio_play_se_by_id(323);
            display_menu_open(0, 1);                         /* FUN_00468338(0,1) */
            display_menu_set_compare(id, -1);                /* FUN_00468233 */
            /* FUN_004682ee(id): open on the first tab holding something `id`
             * can equip (FUN_00468ddc).
             * PORT-DEBT(stub, FUN_004682ee): the tab stays where the open put it. */
            s.bag_mode   = 1;
            s.chara_open = 0;
            s.msg        = 1;
        }
        goto slide_bag_row;
    }

    /* ── the bag (DAT_06a49b60 == 1) ── */
    if (display_menu_detail() != 0) {                        /* FUN_004681e6 */
        if (pressed & 0x70) {
            display_menu_detail_close();                     /* FUN_004681d3 */
            title_save_dialog_cursor_set_visible(1);         /* FUN_0043561a */
            audio_play_se_by_id(710);
        }
        goto tail;
    }
    if (pressed & 0x20) {
        audio_play_se_by_id(317);
        stage_load_pulse_set_active(0);                      /* FUN_004682d0 */
        s.chara_open = 1;
        s.bag_mode   = 0;
        goto slide_chara_row;
    }
    if (pressed & 0x40) {
        if (s.bag_row > 0 && s_bag[s.bag_row] != -1) {
            display_menu_detail_open(0);                     /* FUN_004681db(0) */
            display_menu_set_highlight(s_bag[s.bag_row]);    /* FUN_00468286 */
            title_save_dialog_cursor_set_visible(0);
            audio_play_se_by_id(710);
        }
        goto tail;
    }
    if (pressed & 0x10) {
        audio_play_se_by_id(323);
        if (s.bag_row != 0) {
            s.bag_mode = 2;
            display_menu_cursor_to_row();                    /* FUN_0046939a */
            goto tail;
        }
        stage_load_pulse_set_active(0);
        if (bb[B_FEES_ON] == 0) {
            /* the first trip: straight into dungeon 0 */
            s.floor     = 0;
            s.floor_top = 0;
            s.dungeon   = 0;
            s.commit    = 1;
            fade_phase1_start(0, 0x11);
            goto tail;
        }
        s.map_open = 1;
        s.dungeon  = (int32_t)w[W_LAST_DUNGEON];
        s.bag_mode = 0;
        goto slide_dungeon;
    }
    {
        const int n = s.bag_size;
        if (held & 0x4) {
            if (s.bag_row > 0) {
                audio_play_se_by_id(326);
                if (--s.bag_row - s.bag_top < 0)
                    s.bag_top--;
                slide_bag = 1;
            }
        } else if (held & 0x8) {
            if (s.bag_row < n) {
                audio_play_se_by_id(326);
                if (++s.bag_row - s.bag_top > 5)
                    s.bag_top++;
                slide_bag = 1;
            }
        } else if (held & 0x2) {
            if (s.bag_row > 0) {
                audio_play_se_by_id(326);
                s.bag_row -= 6;
                if (s.bag_row - s.bag_top < 0)
                    s.bag_top -= 6;
                slide_bag = 1;
            }
            if (s.bag_row < 0)
                s.bag_row = 0;
            if (s.bag_top < 0)
                s.bag_top = 0;
        } else if (held & 0x1) {
            if (s.bag_row < n) {
                audio_play_se_by_id(326);
                s.bag_row += 6;
                if (s.bag_row - s.bag_top > 5)
                    s.bag_top += 6;
                slide_bag = 1;
            }
            if (s.bag_row >= n)
                s.bag_row = n;
            if (s.bag_top >= n - 5)
                s.bag_top = n - 5;
        }
        /* FUN_00451874(40,20/21, "X:%d ") — the debug-text grid, unported. */
        display_menu_set_highlight(s.bag_row + s.bag_top != 0 ? s_bag[s.bag_row] : -1);
        if (slide_bag)
            goto slide_bag_row;
        goto tail;
    }

slide_chara_row:          /* LABEL_190 */
    title_save_dialog_cursor_slide(k_chara_cur_x,
        (float)(43 * (s.chara_row - s.chara_top)) + k_chara_cur_y + 48.0f);
slide_pending:            /* LABEL_191 */
    if (slide_floor == 2) {
slide_floor_row:          /* LABEL_193 */
        title_save_dialog_cursor_slide(184.0f, (float)(40 * (s.floor - s.floor_top)) + 156.0f);
    }
    if (slide_floor != 1)
        goto tail;
slide_dungeon:            /* LABEL_195 */
    title_save_dialog_cursor_slide(k_dungeon_xy[s.dungeon][0] + 32.0f,
                                   k_dungeon_xy[s.dungeon][1] + 88.0f);
    if (!slide_bag)       /* LABEL_196 */
        goto tail;
slide_bag_row:            /* LABEL_197 */
    title_save_dialog_cursor_slide(k_bag_cur_x, (float)(40 * (s.bag_row - s.bag_top)) + k_bag_cur_y);

tail:                     /* LABEL_198 */
    s.equipped = 0;
    if (s.chara_row > 0) {
        const int32_t *rec = chara_rec(w, party_roster_row_id(s.chara_row));
        for (int k = 1; k <= 10; k++)
            if (rec[k] != -1)
                s.equipped++;
    }
    s.ticks++;
}

/* ── state mirrors (--state-va) ─────────────────────────────────────── */
int adv_guild_dbg(uint32_t va, int32_t *out)
{
    switch (va) {
    case 0x6a49b44u: *out = s.commit;     return 1;
    case 0x6a49b48u: *out = s.map_open;   return 1;
    case 0x6a49b4cu: *out = s.floor_open; return 1;
    case 0x6a49b50u: *out = s.dungeon;    return 1;
    case 0x6a49b54u: *out = s.floor;      return 1;
    case 0x6a49b5cu: *out = s.chara_open; return 1;
    case 0x6a49b60u: *out = s.bag_mode;   return 1;
    case 0x6a49b6cu: *out = s.ticks;      return 1;
    case 0x6a49b70u: *out = s.msg;        return 1;
    case 0x6a49b74u: *out = s.dungeons;   return 1;
    case 0x5c5a98u:  *out = s.bag_size;   return 1;
    case 0x5c5a9cu:  *out = s.chara_row;  return 1;
    case 0x5c5aa0u:  *out = s.bag_row;    return 1;
    default:         return 0;
    }
}

#ifndef _WIN32
void scene_adv_guild_init(struct IDirect3DDevice8 *dev)
{
    (void)dev;
    worker_load_set_cb(11, adv_guild_enter);
}
void scene_adv_guild_render(struct IDirect3DDevice8 *dev) { (void)dev; }
#else

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "customer_service.h" /* cs_news_price_trend (FUN_004361b2) */
#include "font_draw.h"     /* FUN_0047ca05 / 0047d14c / 0047d2db */
#include "render_quad.h"   /* render_quad_state_setup/bind/add/flush */
#include "scene1_top_hud.h" /* FUN_00406d50 */
#include "sprite.h"        /* sprite_t, sprite_load */
#include "sysassets.h"     /* item_win.tga (DAT_073d8748), the item icon sheets */
#include "tables_item.h"   /* g_item (FUN_0046add8's record lookup) */

/* FUN_00473874 — the texture-group-7 load (kind 7 dropped by sprite_load, as
 * elsewhere).  DAT_073aa7d8 the bg, DAT_073da030 the menu sheet, DAT_073da040
 * the dungeon map, DAT_073da050 its frame, DAT_073da0a0 + 0x10·id the 8
 * adventurer portraits. */
enum {
    ADV_TEX_BG = 0, ADV_TEX_SEL, ADV_TEX_MAP, ADV_TEX_MAPBORD,
    ADV_TEX_ST0,    /* + id */
    ADV_TEX_COUNT = ADV_TEX_ST0 + 8
};
static const struct { const char *path; uint32_t w, h; } k_adv_tex[ADV_TEX_COUNT] = {
    [ADV_TEX_BG]      = { "bmp/boukenguild_bg.bmp", 0x400, 0x200 },
    [ADV_TEX_SEL]     = { "bmp/brave_sel.tga",      0x200, 0x200 },
    [ADV_TEX_MAP]     = { "bmp/dungeonmap.tga",     0x400, 0x200 },
    [ADV_TEX_MAPBORD] = { "bmp/dungeonbord.tga",    0x400, 0x200 },
    [ADV_TEX_ST0 + 0] = { "bmp/st_ryui.tga",        0x200, 0x200 },
    [ADV_TEX_ST0 + 1] = { "bmp/st_sya.tga",         0x200, 0x200 },
    [ADV_TEX_ST0 + 2] = { "bmp/st_caillou.tga",     0x200, 0x200 },
    [ADV_TEX_ST0 + 3] = { "bmp/st_tiers.tga",       0x200, 0x200 },
    [ADV_TEX_ST0 + 4] = { "bmp/st_eran.tga",        0x200, 0x200 },
    [ADV_TEX_ST0 + 5] = { "bmp/st_nagi.tga",        0x200, 0x200 },
    [ADV_TEX_ST0 + 6] = { "bmp/st_griffe.tga",      0x200, 0x200 },
    [ADV_TEX_ST0 + 7] = { "bmp/st_aruma.tga",       0x200, 0x200 },
};
static sprite_t          s_tex[ADV_TEX_COUNT];
static IDirect3DDevice8 *s_dev = NULL;

/* load-worker case 11: FUN_0045bdc2 + FUN_00473874 */
static void adv_guild_load_cb(void)
{
    adv_guild_enter();
    if (s_dev == NULL)
        return;
    for (int i = 0; i < ADV_TEX_COUNT; i++)
        sprite_load(s_dev, k_adv_tex[i].path, k_adv_tex[i].w, k_adv_tex[i].h, &s_tex[i]);
}

void scene_adv_guild_init(struct IDirect3DDevice8 *dev)
{
    s_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_cb(11, adv_guild_load_cb);
}

/* ── render helpers ────────────────────────────────────────────────── */

static void quad(IDirect3DDevice8 *d, const sprite_t *t,
                 float x, float y, float w, float h,
                 float sx0, float sy0, float sx1, float sy1, uint32_t argb)
{
    const float dst[4] = { x, y, w, h };
    const float src[4] = { sx0, sy0, sx1, sy1 };
    render_quad_add(dst, src, t->width, t->height, argb);
}

static int tex_bind(IDirect3DDevice8 *d, const sprite_t *t)
{
    if (t->tex == NULL)
        return 0;
    render_quad_bind(d, t);
    return 1;
}

/* FUN_0040a765 as mode 11 sees it: its shop block is mode-1 gated; the
 * clock/Day/money HUD (FUN_00406d50) and the hand cursor (FUN_00435747) draw. */
static void adv_hud(IDirect3DDevice8 *d)
{
    render_quad_state_setup(d);                /* FUN_0049b425 */
    scene1_top_hud_render(d);                  /* FUN_00406d50 */
    title_save_dialog_cursor_render(d);        /* FUN_00435747 */
}

/* FUN_0046add8(x, y, item, alpha) — one item row: the name (+N, or the
 * unappraised record's name for bit 4) tinted by the price trend, then the
 * 32×32 category icon; both under COLOROP=ADDSIGNED. */
static void item_row(IDirect3DDevice8 *d, float x, float y, int item, int alpha)
{
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    int rec = tables_item_find_slot_by_id(&g_item, item >> 6);
    const int unappraised = (item >> 4) & 1;
    int plus = item & 0xf;
    char buf[160];
    if (unappraised) {
        plus = 0;
        if (rec >= 0 && g_item.records[rec].price != -1) {
            while (1) {                        /* back to the "???" record */
                rec--;
                if (rec <= 0)
                    break;
                if (g_item.records[rec].price == -1)
                    break;
            }
        }
    }
    const char *nm = rec >= 0 ? g_item.records[rec].singular : "";
    if (plus > 0)
        snprintf(buf, sizeof buf, "%s+%d", nm, plus);
    else
        snprintf(buf, sizeof buf, "%s", nm);
    const uint32_t a = (uint32_t)alpha << 24;
    const uint32_t grey = a | 0x7f7f7fu;
    uint32_t col = grey;
    const int trend = (int)cs_news_price_trend(item);   /* FUN_004361b2 */
    if (trend >= 2)       col = a | 0xff0000u;
    else if (trend == 1)  col = a | 0xff4d4du;
    else if (trend <= -2) col = a | 0x0000ffu;
    else if (trend == -1) col = a | 0x4d4dffu;
    font_draw_text(d, x + 32.0f, y + 9.0f, buf, col, 0.73846161f);
    if (rec >= 0) {
        const int cat = g_item.records[rec].category;
        const int idx = unappraised ? 0 : g_item.records[rec].subindex;
        if (cat >= 0 && cat < SYSASSETS_ITEM_CATEGORIES &&
            tex_bind(d, &g_sysassets.item_icons[cat])) {
            const float u = (float)(idx % 8) * 32.0f, v = (float)(idx / 8) * 32.0f;
            quad(d, &g_sysassets.item_icons[cat], x, y, 32.0f, 32.0f,
                 u, v, u + 32.0f, v + 32.0f, grey);
            render_quad_flush(d);
        }
    }
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

/* the item_win scroll arrows (FUN_0048edee / FUN_0048ee77 geometry) */
static void arrow_up(IDirect3DDevice8 *d, float x, float y)
{
    quad(d, &g_sysassets.item_win_tga, x, y, 64.0f, 48.0f, 448.0f, 896.0f, 512.0f, 944.0f, 0xffffffffu);
}
static void arrow_dn(IDirect3DDevice8 *d, float x, float y)
{
    quad(d, &g_sysassets.item_win_tga, x, y, 64.0f, 48.0f, 512.0f, 896.0f, 576.0f, 944.0f, 0xffffffffu);
}

/* .data: DAT_005cc6xx strings, DAT_005c65d0 the list names, DAT_005c6550 the
 * 4-line profiles, DAT_005c65f4 the dungeon names, DAT_005c660c the map
 * path pieces, DAT_005c6634 the name plates. */
static const char *const k_list_name[9] = {
    "Leave", "Swordsman Louie", "Thief Charme", "Magician Caillou", "Archer Tielle",
    "Black Belt Elan", "Lancer Nagi", "Assassin Griff", "Golem Arma",
};
static const char *const k_profile[8][4] = {
    { "Louie",   "Brave but poor adventurer.",      "Balanced and easy to control.",  "Uses swords, shields and armor." },
    { "Charme",  "Mysterious, pretty thief.",       "Incredibly fast and deadly.",    "Uses knives exclusively." },
    { "Caillou", "Magician. Bit of a brat, really.", "Frail, but possesses very",     "powerful magic. Uses staves." },
    { "Tielle",  "Young, kind elven archer.",       "Able to fight well at range.",   "Uses bows, naturally." },
    { "Elan",    "Well-meaning martial artist.",    "Capable of incredible hits",     "with his fists and gloves." },
    { "Nagi",    "Eastern lance-wielder. Her lance", "gives her significant reach.",  "Bit of an airhead, however." },
    { "Griff",   "A demonic assassin. His claw-",   "based fighting style is",        "sinister... and very effective." },
    { "Arma",    "A golem created by forgotten",    "arts. The \"parts\" equipped on", "her arms change how she fights." },
};
static const char *const k_dungeon_name[6] = {
    "Hall of Trials", "Jade Way", "Amber Garden", "Obsidian Tower", "Lapis Ruins", "Crystal Nightmare",
};
static const float k_map_path[4][2]  = { { 72.0f, 160.0f }, { 200.0f, 144.0f }, { 344.0f, 136.0f }, { 496.0f, 144.0f } };
static const float k_name_plate[6][2] = {
    { -32.0f, 64.0f }, { 56.0f, 275.0f }, { 184.0f, 48.0f }, { 352.0f, 256.0f }, { 392.0f, 0.0f }, { 216.0f, 352.0f },
};

/* FUN_0045cc85 */
static void adv_guild_scene_render(IDirect3DDevice8 *d)
{
    uint32_t *w = work();
    if (w == NULL)
        return;
    const uint8_t *bb = (const uint8_t *)w;
    const sprite_t *win = &g_sysassets.item_win_tga;      /* DAT_073d8748 */
    char buf[256];

    render_quad_state_setup(d);                           /* FUN_0049b425 */
    if (tex_bind(d, &s_tex[ADV_TEX_BG])) {
        quad(d, &s_tex[ADV_TEX_BG], 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 0.0f, 640.0f, 480.0f, 0xffffffffu);
        render_quad_flush(d);
    }
    adv_hud(d);                                           /* FUN_0040a765 */

    const int sel_id = party_roster_row_id(s.chara_row);
    if (s.chara_row > 0 && tex_bind(d, &s_tex[ADV_TEX_ST0 + sel_id])) {
        quad(d, &s_tex[ADV_TEX_ST0 + sel_id], 0.0f, -31.0f, 511.0f, 511.0f, 0.0f, 0.0f, 511.0f, 511.0f, 0xffffffffu);
        render_quad_flush(d);
    }

    if (s.chara_open != 0) {
        /* the adventurer panel: the bottom band, the list frame, the
         * equipment plates */
        if (tex_bind(d, win)) {
            quad(d, win, 0.0f, 332.0f, 640.0f, 160.0f, 0.0f, 320.0f, 640.0f, 480.0f, 0xffffffffu);
            render_quad_flush(d);
        }
        const sprite_t *sel = &s_tex[ADV_TEX_SEL];
        if (tex_bind(d, sel)) {
            quad(d, sel, 300.0f, 24.0f, 340.0f, 320.0f, 0.0f, 0.0f, 340.0f, 320.0f, 0xffffffffu);
            for (int k = 0; k < s.equipped; k++)
                quad(d, sel, 0.0f, (float)(272 + 40 * k), 240.0f, 48.0f, 0.0f, 368.0f, 240.0f, 416.0f, 0xffffffffu);
            render_quad_flush(d);
        }
        if (s.chara_row > 0) {
            if (tex_bind(d, sel)) {
                quad(d, sel, 32.0f, 232.0f, 140.0f, 40.0f, 240.0f, 320.0f, 380.0f, 360.0f, 0xffffffffu);
                render_quad_flush(d);
            }
            if (bb[B_FEES_ON] != 0) {
                int fee = hire_fee(w, sel_id);
                if (s.chara_row == 1 && (int32_t)w[SAVE_BANK_FIELD_GOLD] < fee)
                    fee = (int32_t)w[SAVE_BANK_FIELD_GOLD];
                snprintf(buf, sizeof buf, "%d", fee);
            } else {
                snprintf(buf, sizeof buf, "Free");
            }
            font_draw_text_right(d, 244.0f, 240.0f, buf, 0xffffffffu, 1.2307693f);
            for (int l = 0; l < 4; l++)
                font_draw_text(d, 240.0f, (float)(368 + 24 * l), k_profile[sel_id][l], 0xffffffffu, 0.84615391f);
            /* the equipment: retail ORs the 0x20 "seen" bit into each shown
             * slot of the WORK bank from here, the render (engine quirk). */
            int32_t *rec = chara_rec(w, sel_id);
            for (int k = 0; k < s.equipped; k++) {
                if (rec[1 + k] != -1) {
                    rec[1 + k] |= 0x20;
                    item_row(d, 16.0f, (float)(40 * k) + 280.0f, rec[1 + k], 255);
                }
            }
        }
        const int rows = party_roster_count() + 1;
        const int vis = rows < 5 ? rows : 5;
        if (tex_bind(d, win)) {
            if (s.chara_top > 0)
                arrow_up(d, 296.0f, 60.0f);
            if (rows > vis && s.chara_top < rows - vis)
                arrow_dn(d, 296.0f, 308.0f);
            render_quad_flush(d);
        }
        for (int i = 0; i < vis; i++) {
            const int row = s.chara_top + i;
            uint32_t col = 0xffffffffu;
            if (row != 0) {
                const int id = party_roster_row_id(row);
                if ((int32_t)w[SAVE_BANK_FIELD_GOLD] < hire_fee(w, id) && bb[B_FEES_ON] != 0 && id != 0)
                    col = 0xff7f7f7fu;
                snprintf(buf, sizeof buf, "Level:%d %s", chara_rec(w, id)[0] + 1, k_list_name[id + 1]);
            } else {
                snprintf(buf, sizeof buf, "%s", k_list_name[0]);
            }
            font_draw_text(d, 348.0f, (float)(43 * i) + 116.0f, buf, col, 0.76923078f);
        }
    }

    if (s.bag_mode != 0) {
        /* the bag: 6 plates, the hovered one lit and pushed right */
        const sprite_t *sel = &s_tex[ADV_TEX_SEL];
        if (tex_bind(d, sel)) {
            for (int r = 0; r < 6; r++) {
                const int on = r == s.bag_row - s.bag_top;
                quad(d, sel, (on ? 16.0f : 0.0f) + 16.0f, (float)(104 + 40 * r), 240.0f, 48.0f,
                     0.0f, 320.0f, 240.0f, 368.0f, 0xffffffu | ((on ? 255u : 155u) << 24));
            }
            render_quad_flush(d);
        }
        for (int r = 0; r < 6; r++) {
            const int on = r == s.bag_row - s.bag_top;
            const float xo = on ? 16.0f : 0.0f;
            const int alpha = on ? 255 : 155;
            const uint32_t col = 0xffffffu | ((uint32_t)alpha << 24);
            const int row = s.bag_top + r;
            if (row != 0) {
                snprintf(buf, sizeof buf, "%2d:", row);
                font_draw_text(d, xo + 28.0f, (float)(40 * r) + 118.0f, buf, col, 0.76923078f);
                if (s_bag[row] != -1)
                    item_row(d, xo + 58.0f, (float)(40 * r) + 112.0f, s_bag[row], alpha);
            } else {
                font_draw_text(d, xo + 64.0f, (float)(40 * r) + 118.0f, "Depart!", col, 0.92307699f);
            }
        }
        display_menu_render(d, 0.0f);                       /* FUN_0046b00a */
    }

    if (s.map_open != 0) {
        if (tex_bind(d, &s_tex[ADV_TEX_MAP])) {
            quad(d, &s_tex[ADV_TEX_MAP], 0.0f, 24.0f, 640.0f, 480.0f, 0.0f, 0.0f, 640.0f, 480.0f, 0xffffffffu);
            render_quad_flush(d);
        }
        const sprite_t *bord = &s_tex[ADV_TEX_MAPBORD];
        if (tex_bind(d, bord)) {
            quad(d, bord, 0.0f, 416.0f, 256.0f, 64.0f, 320.0f, 0.0f, 576.0f, 64.0f, 0xffffffffu);
            int paths = s.dungeons - 1;
            if (paths > 4)
                paths = 4;
            for (int k = 0; k < paths; k++) {
                const float sx = (float)(320 + 80 * k);
                quad(d, bord, k_map_path[k][0] + 32.0f, k_map_path[k][1] + 24.0f, 80.0f, 128.0f,
                     sx, 192.0f, sx + 80.0f, 320.0f, 0xffffffffu);
            }
            render_quad_flush(d);
        }
        for (int k = 0; k < s.dungeons; k++) {
            if (tex_bind(d, bord)) {
                IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
                const float sx = k == 5 ? 832.0f : (float)(128 * k + 320);
                const float sy = k == 5 ? 192.0f : 64.0f;
                quad(d, bord, k_dungeon_xy[k][0] + 32.0f, k_dungeon_xy[k][1] + 24.0f, 128.0f, 128.0f,
                     sx, sy, sx + 128.0f, sy + 128.0f, 0xff7f7f7fu);
                render_quad_flush(d);
            }
            if (tex_bind(d, win)) {
                quad(d, win, k_name_plate[k][0] + 32.0f, k_name_plate[k][1] + 24.0f, 240.0f, 77.0f,
                     448.0f, 813.0f, 688.0f, 890.0f, 0xff7f7f7fu);
                render_quad_flush(d);
            }
            IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            font_draw_text_centered(d, k_name_plate[k][0] + 152.0f, k_name_plate[k][1] + 54.0f,
                                    k_dungeon_name[k], 0xffffffffu, 0.84615391f);
        }
    }

    if (s.floor_open != 0) {
        if (tex_bind(d, &s_tex[ADV_TEX_MAPBORD])) {
            quad(d, &s_tex[ADV_TEX_MAPBORD], 160.0f, 80.0f, 320.0f, 360.0f, 0.0f, 0.0f, 320.0f, 360.0f, 0xffffffffu);
            render_quad_flush(d);
        }
        const int n = s.floors[s.dungeon];
        const int vis = n < 7 ? n : 7;
        if (tex_bind(d, win)) {
            quad(d, win, 200.0f, 48.0f, 240.0f, 77.0f, 448.0f, 813.0f, 688.0f, 890.0f, 0xffffffffu);
            if (s.floor_top > 0)
                arrow_up(d, 184.0f, 92.0f);
            if (n > vis && s.floor_top < n - vis)
                arrow_dn(d, 184.0f, 396.0f);
            render_quad_flush(d);
        }
        font_draw_text_centered(d, 320.0f, 76.0f, k_dungeon_name[s.dungeon], 0xffffffffu, 1.0769231f);
        for (int i = 0; i < vis; i++) {
            const int g = i + s.floor_top;
            const float y = (float)(40 * i) + 140.0f;
            const int step = s.dungeon == 5 ? 10 : 5;
            const int regular = s.dungeon == 5 ? g < 3 : g < k_floor_cap[s.dungeon];
            if (regular) {
                font_draw_text(d, 220.0f, y, "Floor", 0xffffffffu, 1.0769231f);
                font_draw_text(d, 312.0f, y, "\x81\x60", 0xffffffffu, 1.0769231f);   /* SJIS ～ */
                snprintf(buf, sizeof buf, "%2d", step * g + 1);
                font_draw_text_centered(d, 296.0f, y, buf, 0xffffffffu, 1.0769231f);
                snprintf(buf, sizeof buf, "%2d", step * (g + 1));
                font_draw_text_centered(d, 344.0f, y, buf, 0xffffffffu, 1.0769231f);
            } else {
                font_draw_text_centered(d, 296.0f, y, "Boss Rush", 0xffffffffu, 1.0769231f);
            }
            /* the best time (work dword 0xb10b + 20·dungeon + group), in ticks */
            const int32_t t = (int32_t)w[0xb10bu + (uint32_t)(g + 20 * s.dungeon)];
            if (t != 0) {
                int h = t / 216000;
                if (h > 99)
                    h = 99;
                snprintf(buf, sizeof buf, "%02d:%02d:%02d", h, t / 3600 % 60, t / 60 % 60);
            } else {
                snprintf(buf, sizeof buf, "--:--:--");
            }
            font_draw_text(d, 372.0f, y + 4.0f, buf, 0xffffffffu, 0.76923078f);
        }
    }
}

/* FUN_0045cc1e — the bag's scroll arrows. */
static void adv_guild_bag_arrows(IDirect3DDevice8 *d)
{
    render_quad_state_setup(d);                           /* FUN_0049b425 */
    if (s.bag_mode == 0 || !tex_bind(d, &g_sysassets.item_win_tga))
        return;
    if (s.bag_top > 0)
        arrow_up(d, 16.0f - 32.0f, 92.0f - 24.0f);        /* FUN_0048edee(16,92) */
    if (s.bag_top < s.bag_size - 5)
        arrow_dn(d, 16.0f - 32.0f, 352.0f - 24.0f);       /* FUN_0048ee77(16,352) */
    render_quad_flush(d);
}

/* FUN_004547ab case 11 (the dialogue gate is the caller's): FUN_0045cc85,
 * FUN_0040a765, FUN_0045cc1e. */
void scene_adv_guild_render(struct IDirect3DDevice8 *dev)
{
    IDirect3DDevice8 *d = (IDirect3DDevice8 *)dev;
    adv_guild_scene_render(d);
    adv_hud(d);
    adv_guild_bag_arrows(d);
}
#endif
