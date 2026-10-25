/*
 * scene1_hud.c — port of FUN_0040a765 (0x40a765, 7558 B), the scene-1
 * 2D HUD / overlay aggregator.  See scene1_hud.h for the pass map.
 *
 * C7j chip: entry shell + Pass 1 (stamina/HP backdrop, DUNGEON-only) +
 * Pass 2 (letterbox bars) + Pass 3 (status-screen takeover).  Every
 * pass past Pass 1's state preset is gated on a BSS-zero global in
 * HOUSE, so the wired shell is visually inert there — it lands the
 * structure + the 2D render-state preset the later passes inherit.
 *
 * Engine globals owned / referenced by this chip:
 *   DAT_005c570c   master render gate (.data = 1, const) → never early-
 *                  returns in practice; reproduced for fidelity.
 *   DAT_0438b1c0   scene mode (1 = INGAME)            → g_scene_state.
 *   *DAT_068dd2f0  stage type (0 = HOUSE, >0 DUNGEON) → stage record
 *                  field 0 (maptype) via scene1_current_stage_record().
 *   DAT_0438b1e0   save-slot index (Pass 4+ per-slot base; computed
 *                  here only as the engine does, unused until C7l).
 *   DAT_0438b1dc   letterbox-bar height (Pass 2)      → module-local.
 *   DAT_073dddb4   status-screen-open flag (Pass 3)   → module-local.
 *   DAT_056db104   Pass-1 backdrop alt-gate           → module-local.
 *   DAT_073cc8f0   shade.bmp  (Pass 1 backdrop)       → g_sysassets.shade_bmp.
 *   DAT_073aa188   system.bmp (Pass 2 bars)           → g_sysassets.system_bmp.
 *
 * Deferred sub-calls (stubbed, faithful call-count):
 *   FUN_0049065b (314 B) — Pass-1 sub-init: snapshots a 32-dword block
 *                          (DAT_073de29c..) into the DAT_095d37xx 2D-
 *                          overlay-camera arrays.  Source block + the
 *                          consumer subsystem are BSS-zero / unported,
 *                          so the call is a no-op on the HOUSE path.
 *   FUN_004141c0 (389 B) — Pass-3 status-screen render.  Reachable only
 *                          when DAT_073dddb4 != 0 (Q-menu open).
 *   FUN_0043647f          — Pass-1 dungeon backdrop predicate; only
 *                          evaluated inside the (dormant) DUNGEON gate.
 */

#include "scene1_hud.h"

/* ─── module-local engine globals (no other owner yet) ─────────────── */

/* DAT_0438b1dc — the HUD slide, 0 shown … 1 away (float): FUN_00406584's
 * tail eases it 0.1 a tick (scene1_hud_slide_tick).  Read by Pass 2's bars
 * (·32), the top HUD (·−128) and the dungeon HUD (·128). */
static float g_hud_letterbox_h = 0.0f;

/* DAT_073dddb4 — status-screen-open flag.  Set to 1 only while the
 * in-shop status/Q-menu is up (engine all.c:73798); 0 otherwise. */
static int g_hud_status_screen = 0;

/* DAT_056db104 — Pass-1 stamina-backdrop alternate gate.  BSS-zero;
 * only consulted on the DUNGEON path. */
static int g_hud_dat_056db104 = 0;

/* ─── pure helpers (host-testable) ─────────────────────────────────── */

float scene1_hud_letterbox_height(void)
{
    /* Engine: `if (-0.1 <= h && h <= 0.1) h = 0;` — a ±0.1 dead-zone
     * so the bars stay fully retracted until the animator pushes the
     * height clear of zero. */
    float h = g_hud_letterbox_h;
    if (h >= -0.1f && h <= 0.1f)
        h = 0.0f;
    return h;
}

void scene1_hud_set_letterbox_height(float h) { g_hud_letterbox_h = h; }
float scene1_hud_slide(void) { return g_hud_letterbox_h; }


int  scene1_hud_status_screen_open(void) { return g_hud_status_screen; }
void scene1_hud_set_status_screen_open(int open) { g_hud_status_screen = open ? 1 : 0; }

uint32_t scene1_hud_pass1_backdrop_color(int pred)
{
    /* uVar7 = pred ? 0xff : 200; engine packs it as
     * `((uVar7 | 0x3700) << 8 | uVar7) << 8 | uVar7` — RGB = (uVar7)³,
     * alpha byte forced to 0x37. */
    uint32_t a = pred ? 0xffu : 200u;
    return (uint32_t)((((a | 0x3700u) << 8) | a) << 8) | a;
}

int scene1_hud_pass1_backdrop_active(int scene_mode, int stage_type, int pred)
{
    return (scene_mode == 1) && (stage_type > 0) &&
           (pred != 0 || g_hud_dat_056db104 != 0);
}

#include "scene.h"                 /* g_scene_state (DAT_0438b1c0) */
#include "scene1_intro_dialogue.h"  /* DAT_0438b1c8, FUN_0046c869 */
#include "dungeon_flow.h"          /* DAT_0438b8b0, DAT_0438beb4 */
#include "map_collision.h"         /* mapcol_maptype */
#include "save_work.h"
#include "x87.h"

/* FUN_00406584 0x4068cb..0x40697c: the slide goes away (+0.1, ≤ 1) while the
 * work's +0x2bce2 / +0x2bcf8 bytes are set, the scene is ingame with the
 * player's control taken (b8b0 != -1 or beb4 > 0) and no dialogue, or a
 * dialogue runs on a floor or over a covering backdrop (FUN_0046c869); it comes
 * back (−0.1, ≥ 0) on the world map (b1c0 == 8) and otherwise. */
void scene1_hud_slide_tick(void)
{
    static const float k0p1 = 0.10000000149011612f, k0 = 0.0f, k1 = 1.0f;   /* 0x5193a0 / 0x519320 / 0x519364 */
    /* b1c8 as FUN_00406584 (0x453bad) reads it: retail clears it only at
     * 0x453c44, later in the frame, so the frame a dialogue ends still counts */
    static int s_gate_prev;
    const int gate = scene1_intro_dialogue_gate();
    const int dlg = gate != 0 || s_gate_prev != 0;
    s_gate_prev = gate;
    const uint8_t *rec = save_work_bank_at(save_work_active_slot());
    int away;
    if (rec != NULL && (rec[0x2bce2] != 0 || rec[0x2bcf8] != 0))
        away = 1;
    else if (g_scene_state == 8)
        away = 0;
    else if (!dlg)
        away = (g_ctrl_lock != -1 || g_dat_0438beb4 > 0) && g_scene_state == 1;
    else
        away = mapcol_maptype() >= 1 || scene1_intro_dialogue_bg_count() != 0;
    if (away) {
        g_hud_letterbox_h = spill32((long double)g_hud_letterbox_h + k0p1);
        if (g_hud_letterbox_h > k1) g_hud_letterbox_h = k1;
    } else {
        g_hud_letterbox_h = spill32((long double)g_hud_letterbox_h - k0p1);
        if (g_hud_letterbox_h < k0) g_hud_letterbox_h = k0;
    }
}

/* ─── Win32 render body ────────────────────────────────────────────── */

#ifdef _WIN32

#include "render_quad.h"
#include "sysassets.h"
#include "scene.h"            /* g_scene_state (DAT_0438b1c0) */
#include "scene1_maplight.h"  /* scene1_current_stage_record (DAT_068dd2f0) */
#include "scene1_top_hud.h"   /* FUN_00406d50 — persistent clock/day/money HUD */
#include "scene1_merchant_hud.h"  /* FUN_00409925 body — bottom-left Merchant Level HUD */
#include "scene1_display_menu.h"  /* display_menu_render (FUN_0046b00a) */
#include "customer_service.h"     /* customer_service_render_overlay (FUN_00466b7b) */
#include "choice_box.h"           /* choice_box_draw (FUN_0043537e) — HUD-tail pass */
#include "title_save_dialog.h"    /* title_save_dialog_cursor_render (FUN_00435747) */
#include "scene1_player_ctrl.h"   /* player_ctrl_emote_level/type (db000/db004)        */
#include "scene1_render.h"        /* scene1_project_world (FUN_00490c78)               */
#include "scene1_camera.h"        /* scene1_camera_class_off_z (b778)                  */
#include "scene1_particles_tick.h" /* g_scene1_player_pos[3]                           */
#include "scene1_intro_dialogue.h" /* the dialogue-box gate (DAT_0438b1c8) */
#include "speech_bubble.h"        /* speech_bubble_render (FUN_0040c4eb)                */
#include "dungeon_hud.h"           /* FUN_00407cac                                      */
#include "dungeon_flow.h"         /* the floor events (DAT_0438b938 / b96c), b954     */
#include "font_draw.h"            /* FUN_0047ca05 / FUN_0047d0ea                      */
#include "map_collision.h"        /* mapcol_maptype (*DAT_068dd2f0)                   */
#include "news_daily.h"           /* DAT_0438b92c                                     */
#include "save_bank.h"
#include "save_work.h"
#include "x87.h"
#include "call_trace.h"
#include "actor_table.h"          /* the bombs' countdowns (DAT_0076b970) */
#include "party.h"                /* DAT_056da1cc / 56daff4 */
#include "scene1_counter_menu.h"   /* Tear's menu: b8c0, list B */
#include "scene1_preload.h"        /* mood_para.tga */
#include "customer_roster.h"       /* DAT_0438b4b8 / b4bc */
#include "sim.h"                   /* DAT_0438b8cc */
#include "retail_image.h"          /* the level-abilities table */
#include <stdio.h>                 /* snprintf */
#include <string.h>
#include <math.h>                 /* sinf */


/* FUN_0049065b — Pass-1 sub-init (deferred; see header comment). */
static void scene1_hud_subinit_TODO(void)
{
    CALL_TRACE_ENTER_STUB(0x49065bu);
}

/* FUN_004141c0 — Pass-3 status-screen render (deferred). */
static void scene1_hud_status_screen_render_TODO(void)
{
    CALL_TRACE_ENTER_STUB(0x4141c0u);
}

/* FUN_0043647f(0x10) — Pass-1 DUNGEON backdrop predicate (deferred).
 * Only evaluated inside the DUNGEON gate, dormant in HOUSE. */
static int scene1_hud_fun_0043647f(int flag)
{
    (void)flag;
    return 0;
}

/* *DAT_068dd2f0 + 0 — stage type (0 = HOUSE, >0 = DUNGEON). */
static int scene1_hud_stage_type(void)
{
    const stage_record_t *rec = scene1_current_stage_record();
    return rec ? rec->maptype : 0;
}

/* ── the free-roam interaction-affordance emote bubble (FUN_0040a765 inline,
 *    all.c:6900-6932) — the "GO!" shop-door tooltip + the talk/pick-up prompts.
 *
 * One hpmp_base.tga cell, projected at the player's head (engine world-Y =
 * b778·0.1 + 4.0 + player_y) and scaled in by the db000 gauge: a sin overshoot
 * that pops the bubble big (level<8) then settles to 32x32 (level>=8).  The cell
 * is db004: 7 = the door "GO!" (the only path the port drives — the bVar3 NPC
 * prompts are inert with no live customers).  Drawn in mode 1 (INGAME/HOUSE),
 * right after the merchant HUD (engine order: FUN_00409925 → this), under
 * COLOROP=MODULATE (matches the retail draw).  Inert until the player-ctrl
 * door-zone ramp raises db000 (player_ctrl_cc08_proximity_detect). */
static void scene1_hud_emote_bubble(IDirect3DDevice8 *dev)
{
    int level = player_ctrl_emote_level();          /* DAT_056db000 (0..10) */
    if (level == 0)                                  /* engine: DAT_056db000 != 0 */
        return;

    const sprite_t *hp = &g_sysassets.hpmp_base_tga; /* DAT_073cc920 (512x512) */
    if (!hp->tex)
        return;

    /* Project the player head (FUN_00490c78(px, b778·0.1 + 4.0 + py, pz, &out)). */
    float sx, sy, vz;
    scene1_project_world(g_scene1_player_pos[0],
                         scene1_camera_class_off_z() * 0.1f + 4.0f
                             + g_scene1_player_pos[1],
                         g_scene1_player_pos[2], &sx, &sy, &vz);
    if (!(vz < 0.0f))                                /* engine guard: local_58 < 0 */
        return;

    /* Slide-in scale (all.c:6904-6918).  local_14/local_18 = dst w/h (32 steady);
     * local_44 = the y-lift.  The level<8 ramp overshoots via sin. */
    float w14 = 32.0f, w18 = 32.0f, yoff = 0.0f;
    if (level < 8) {
        float t = (float)level / 8.0f;
        float s = sinf((float)level * 3.1415927f / 8.0f);
        w18  = (s + t) * 48.0f;                      /* local_18 = (sin + t)·48 */
        w14  = (2.0f - s - t) * 48.0f;               /* local_14 = (2 - sin - t)·48 */
        yoff = s * 32.0f;                            /* local_44 = sin·32 */
    }

    int type = player_ctrl_emote_type();             /* DAT_056db004 */
    float src_x = (float)((type % 4) * 0x30 + 0x140);    /* (type%4)·48 + 320 */
    float src_y = (float)((type / 4) * 0x30);            /* (type/4)·48 */

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)hp->tex);
    {
        /* dst {x = sx - local_14, y = (sy - local_18·0.7) - local_44, w, h}. */
        const float dst[4] = { sx - w14, (sy - w18 * 0.7f) - yoff, w14, w18 };
        const float src[4] = { src_x, src_y, src_x + 48.0f, src_y + 48.0f };
        render_quad_add(dst, src, hp->width, hp->height, 0xffffffffu);
        render_quad_flush(dev);
    }
}

/* DAT_005291c0[30]: Tear's level abilities — the level (dword) and its 32-byte
 * name; off_00529768[3 * k]: the three description lines.  The user's exe's
 * .data (retail_image), read once. */
#define LEVEL_ABILITY_COUNT 30
static struct { int32_t level; char name[32]; char desc[3][64]; } s_lvab[LEVEL_ABILITY_COUNT];
static int s_lvab_loaded;

static void level_abilities_load(void)
{
    if (s_lvab_loaded) return;
    s_lvab_loaded = 1;
    for (uint32_t k = 0; k < LEVEL_ABILITY_COUNT; k++) {
        uint32_t lv = 0, ptr;
        retail_image_u32(0x5291c0u + 36u * k, &lv);
        s_lvab[k].level = (int32_t)lv;
        retail_image_cstr(0x5291c4u + 36u * k, s_lvab[k].name, sizeof s_lvab[k].name);
        for (uint32_t j = 0; j < 3; j++)
            if (retail_image_u32(0x529768u + 12u * k + 4u * j, &ptr))
                retail_image_cstr(ptr, s_lvab[k].desc[j], sizeof s_lvab[k].desc[j]);
    }
}

/* FUN_0048edee / FUN_0048ee77: item_win's up / down scroll arrow centred on (x, y) */
static void tear_page_arrow(IDirect3DDevice8 *dev, float x, float y, int down)
{
    const sprite_t *win = &g_sysassets.item_win_tga;
    if (!win->tex) return;
    render_quad_bind(dev, win);
    const float dst[4] = { x - 32.0f, y - 24.0f, 64.0f, 48.0f };
    const float src[4] = { down ? 512.0f : 448.0f, 896.0f, down ? 576.0f : 512.0f, 944.0f };
    render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
    render_quad_flush(dev);
}

/* LABEL_100's entry 5 (all_ida 7742..7882): leve_win.tga and item_win's lower
 * panel, the scroll arrows, "Level Abilities", five rows from b8c8 ("Level %d"
 * + the name, "???" past the first locked level above 9; yellow at the next
 * level, grey once locked), Recette's head at the merchant level's row and the
 * cursor row's (b8c4) three description lines once it is near */
static void tear_page_level_abilities(IDirect3DDevice8 *dev, float x0)
{
    level_abilities_load();
    const int32_t *bd = (const int32_t *)save_work_bank_at(save_work_active_slot());
    const int32_t cur = bd ? bd[SAVE_BANK_FIELD_MERCHANT_LEVEL] : 0;   /* dword 45312 */
    const int top = tear_lecture_top(), sel = tear_lecture_cursor();
    const sprite_t *lw = scene1_preload_leve_win();
    const sprite_t *win = &g_sysassets.item_win_tga;
    if (lw->tex) {
        render_quad_bind(dev, lw);
        const float dst[4] = { x0 + 160.0f, 80.0f, 320.0f, 256.0f };
        const float src[4] = { 0.0f, 0.0f, 320.0f, 256.0f };
        render_quad_add(dst, src, lw->width, lw->height, 0xffffffffu);
        render_quad_flush(dev);
    }
    if (win->tex) {
        render_quad_bind(dev, win);
        const float dst[4] = { x0, 332.0f, 640.0f, 160.0f };
        const float src[4] = { 0.0f, 320.0f, 640.0f, 480.0f };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
        render_quad_flush(dev);
    }
    if (top > 0)
        tear_page_arrow(dev, x0 + 190.0f, 130.0f, 0);
    if (top < 17)
        tear_page_arrow(dev, x0 + 190.0f, 318.0f, 1);
    font_draw_text_centered((struct IDirect3DDevice8 *)dev, x0 + 320.0f, 122.0f, "Level Abilities",
                            0xffffffffu, 0.89999998f);
    int locked = 0;
    for (int row = 0; row < 5; row++) {
        const int k = top + row;
        if (k < 0 || k >= LEVEL_ABILITY_COUNT) continue;
        const int32_t lv = s_lvab[k].level;
        uint32_t color = 0xffffffffu;
        if (cur + 1 == lv)
            color = 0xffffff37u;
        char buf[32];
        snprintf(buf, sizeof buf, "Level %d", (int)lv);
        int shown = cur >= lv - 2;
        if (locked > 0) shown = 0;
        if (lv <= 9) shown = 1;
        if (cur + 1 < lv) locked = 1;
        if (locked) color = 0xff9b9b9bu;
        const float y = (float)(32 * row) + 158.0f;
        font_draw_text((struct IDirect3DDevice8 *)dev, x0 + 200.0f, y, buf, color, 0.80000001f);
        font_draw_text((struct IDirect3DDevice8 *)dev, x0 + 312.0f, y, shown ? s_lvab[k].name : "???",
                       color, 0.80000001f);
    }
    int idx = 0;
    while (idx < LEVEL_ABILITY_COUNT && s_lvab[idx].level <= cur)
        idx++;
    if (idx - top <= 4 && win->tex) {
        const int f = (int)(g_sim_frame_count / 16 % 2);
        render_quad_bind(dev, win);
        const float dst[4] = { x0 + 408.0f, (float)(32 * (idx - top)) + 156.0f - 48.0f, 96.0f, 96.0f };
        const float src[4] = { (float)(96 * f + 832), 768.0f, (float)(96 * f + 928), 864.0f };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
        render_quad_flush(dev);
    }
    if (sel >= 0 && sel < LEVEL_ABILITY_COUNT && (sel < 9 || cur + 1 >= s_lvab[sel].level))
        for (int j = 0; j < 3; j++)
            font_draw_text((struct IDirect3DDevice8 *)dev, x0 + 80.0f, (float)(372 + 30 * j), s_lvab[sel].desc[j],
                           0xffffffffu, 0.85000002f);
}

/* FUN_0040a765 LABEL_100 (all_ida 7502..7924), in the shop: the page of Tear's
 * menu entry under the cursor while cc08 is 15 / 16 / 17 (18 for entry 5).
 * Entry 0, the store atmosphere: mood_para.tga sliding in with b8c0 (64 a
 * step), Recette's head (item_win, two frames alternating every 16 ticks of
 * b8cc) at the shop's attribute centroid (b4b8 / b4bc) on the chart.
 * PORT-DEBT(stub, FUN_0040a765): the pages of entries 8 (news summary) and
 * 9 (outstanding orders). */
static void scene1_hud_tear_pages(IDirect3DDevice8 *dev)
{
    if (mapcol_maptype() != 0)
        return;
    const int cc08 = player_ctrl_cc08();
    const int entry = tear_menu_entry_at_cursor();
    const float x0 = spill32((long double)spill32((float)(tear_menu_window() << 6)) - 640.0f);
    if (entry == 5 && (cc08 == 15 || cc08 == 16 || cc08 == 18)) {
        tear_page_level_abilities(dev, x0);
        return;
    }
    if (entry != 0 || (cc08 != 15 && cc08 != 16 && cc08 != 17))
        return;
    const sprite_t *mood = scene1_preload_mood_para();
    if (mood->tex) {
        render_quad_bind(dev, mood);
        const float dst[4] = { spill32((long double)x0 + 64.0f), 0.0f, 512.0f, 480.0f };
        const float src[4] = { 0.0f, 0.0f, 512.0f, 480.0f };
        render_quad_add(dst, src, mood->width, mood->height, 0xffffffffu);
        render_quad_flush(dev);
    }
    const sprite_t *win = &g_sysassets.item_win_tga;
    if (win->tex) {
        const float hx = spill32((float)(320 - 12 * roster_centroid_x()));
        const float hy = spill32((float)(12 * (20 - roster_centroid_y())));
        const int f = (int)(g_sim_frame_count / 16 % 2);
        render_quad_bind(dev, win);
        const float dst[4] = { spill32((long double)hx + x0 - 48.0f), spill32((long double)hy - 48.0f), 96.0f, 96.0f };
        const float src[4] = { (float)(96 * f + 832), 768.0f, (float)(96 * f + 928), 864.0f };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
        render_quad_flush(dev);
    }
}

/* FUN_0040a765, after the emote bubble (all_ida 7244..7313): under ADDSIGNED,
 * chara 30's ammo tier (DAT_056daff4 / 45) bobbing over the player and every
 * live bomb's countdown (types 37..40: 9 − timer/60, red at 3 or less) — 24×24
 * digit cells of hpmp_base at u 193 + 24·n, through FUN_00490c78 (the render's
 * view / projection); then MODULATE again (dungeon3-deep f176250's "5") */
static void scene1_hud_countdowns(IDirect3DDevice8 *dev)
{
    static const float kpi = 3.1415927410125732f, k16 = 16.0f, k32 = 32.0f;
    const sprite_t *hp = &g_sysassets.hpmp_base_tga;   /* DAT_073cc920 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    if (hp->tex) {
        IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)hp->tex);
        const int32_t ammo = PW(0x56daff4u).i;
        if (g_party.chr[0] == 30 && ammo > 0 && ammo / 45 > 0) {
            const int32_t n = ammo / 45;
            float sx, sy, vz;
            scene1_project_world_mat(scene1_render_view_matrix(), scene1_render_proj_matrix(),
                                     g_party.pos[0][0], g_party.pos[0][1], g_party.pos[0][2], &sx, &sy, &vz);
            if (vz < 0.0f) {
                int32_t m = ammo % 45;
                if (m > 16) m = 16;
                const float sn = spill32(x87_crt_sin((double)spill32((long double)spill32((float)m) * kpi / k16)));
                const float y = spill32((long double)sy - (long double)sn * k32 + 12.0f);
                const float dst[4] = { spill32((long double)sx - 10.0f), spill32((long double)y - 14.0f), 20.0f, 20.0f };
                const float src[4] = { (float)(24 * n + 193), 456.0f, (float)(24 * n + 217), 480.0f };
                render_quad_add(dst, src, hp->width, hp->height, 0xffffbfbfu);
                render_quad_flush(dev);
            }
        }
        for (int i = 0; i < ACTOR_COUNT; i++) {
            const uint8_t *a = g_actor[i].b;
            int32_t type, live, timer;
            memcpy(&type, a + ACT_TYPE, 4);
            memcpy(&live, a + ACT_INUSE, 4);
            memcpy(&timer, a + 0xaf0, 4);
            if (live != 2 || type < 37 || type > 40)
                continue;
            const int32_t n = 9 - timer / 60;
            if (n <= 0 || n >= 10)
                continue;
            float p[3], sx, sy, vz;
            memcpy(p, a + ACT_POS, 12);
            scene1_project_world_mat(scene1_render_view_matrix(), scene1_render_proj_matrix(),
                                     p[0], p[1], p[2], &sx, &sy, &vz);
            if (!(vz < 0.0f))
                continue;
            const float dst[4] = { spill32((long double)sx - 16.0f), spill32((long double)sy - 22.4f), 32.0f, 32.0f };
            const float src[4] = { (float)(24 * n + 193), 456.0f, (float)(24 * n + 217), 480.0f };
            render_quad_add(dst, src, hp->width, hp->height, n <= 3 ? 0xffff4040u : 0xff7f7f7fu);
            render_quad_flush(dev);
        }
    }
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

/* CRT sprintf(buf, str) with the text as the format and no arguments: "%%"
 * folds to "%" (the texts carry no other conversions). */
static void ticker_sprintf(char *buf, size_t n, const char *str)
{
    size_t o = 0;
    for (size_t k = 0; str[k] != 0 && o + 1 < n; k++) {
        if (str[k] == '%' && str[k + 1] == '%') k++;
        buf[o++] = str[k];
    }
    buf[o] = 0;
}

/* FUN_0040a765 0x40b0df..0x40b3dd — the news / floor-event ticker band (mode 1):
 * the texts (the floor events DAT_0438b96c × b938 in a dungeon, the headlines
 * at work +0x2a6c4 × work +0x2a6c0 in the house) scroll in from the right at
 * 2 px a tick ÷ scale (0.5 dungeon, 0.7 house); the band opens to 64 at 3.2 a
 * tick and, once b92c passes the texts' run (Σ (width + 8) / 2 · scale +
 * 320 · scale + 60), closes at 3.2 a tick — closed, b92c = 0 and b954 = 1 (the
 * dungeon's player waits on b92c). */
static void news_ticker_render(IDirect3DDevice8 *dev)
{
    static const float k0p5 = 0.5f, k0p7 = 0.699999988079071f, k3p2 = 3.200000047683716f;
    static const float k64 = 64.0f, k1p1 = 1.100000023841858f, k8 = 8.0f, k2 = 2.0f;
    static const float k320 = 320.0f, k60 = 60.0f, k640 = 640.0f, k52 = 52.0f, k32 = 32.0f;
    const int dungeon = mapcol_maptype() != 0;
    const float scale = dungeon ? k0p5 : k0p7;
    if (g_news_ticker_timer == 0) return;
    const sprite_t *win = &g_sysassets.item_win_tga;      /* DAT_073d8748 */
    if (win->tex) render_quad_bind(dev, win);
    float h = spill32((long double)spill32((float)g_news_ticker_timer) * k3p2);
    if (h > k64) h = k64;
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    int32_t n = bank != NULL ? (int32_t)bank[SAVE_BANK_FIELD_NEWS_HL_COUNT] : 0;
    const char *house_txt = bank != NULL ? (const char *)(bank + SAVE_BANK_FIELD_NEWS_HL_COUNT + 1) : NULL;
    if (dungeon) n = g_floor_event_n;
    if (n <= 0) return;
    char buf[256];
    float total = 0.0f;
    for (int32_t k = 0; k < n; k++) {
        ticker_sprintf(buf, sizeof buf, dungeon ? g_floor_event_text[k] : house_txt + 256 * k);
        const float w = font_measure_text((struct IDirect3DDevice8 *)dev, buf, k1p1);   /* FUN_0047d0ea */
        total = spill32(((long double)w + k8) / k2 * scale + total);
    }
    total = spill32((long double)scale * k320 + total);
    const float t = spill32((float)g_news_ticker_timer);
    const float lim = spill32((long double)total + k60);
    if (t > lim) {
        h = spill32((long double)h - ((long double)t - lim) * k3p2);
        if (h < 0.0f) {
            g_news_ticker_timer = 0;
            g_dat_0438b954 = 1;
            h = 0.0f;
        }
    }
    if (win->tex) {
        if (h > 0.0f) {
            const float dst[4] = { 0.0f, spill32(k64 - (long double)h * k0p5), k640, h };
            const float src[4] = { 0.0f, 512.0f, 64.0f, 575.0f };
            render_quad_add(dst, src, win->width, win->height, 0xffffffffu);   /* FUN_00404efc */
        }
        if (g_news_ticker_timer > 30 && g_news_ticker_timer < 60) {
            const float dst[4] = { 208.0f, 48.0f, 224.0f, 32.0f };
            const float src[4] = { 0.0f, 480.0f, 224.0f, 512.0f };
            render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
        }
        render_quad_flush(dev);                           /* FUN_00405354 */
    }
    float x = spill32(k640 - (long double)spill32((float)(2 * g_news_ticker_timer - 120)) / scale);
    for (int32_t k = 0; k < n; k++) {
        ticker_sprintf(buf, sizeof buf, dungeon ? g_floor_event_text[k] : house_txt + 256 * k);
        font_draw_text((struct IDirect3DDevice8 *)dev, x, k52, buf, 0xffffffffu, k1p1);   /* FUN_0047ca05 */
        const float w = font_measure_text((struct IDirect3DDevice8 *)dev, buf, k1p1);
        x = spill32((long double)w + x + k32);
    }
}

void scene1_hud_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* E.2 probe — FUN_0040a765 @ 0x40a765. */
    CALL_TRACE_ENTER(0x40a765u);

    /* L45 — per-save-slot record base
     *   local_48 = &DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8
     * is computed here in the engine but only consumed by Pass 4+
     * (speech bubbles, sub-menu panels).  Deferred with those passes;
     * no read happens before then. */

    /* L46 — Pass-1 sub-init (DAT_095d37xx 2D-overlay camera feed). */
    scene1_hud_subinit_TODO();

    /* L47 — master render gate (DAT_005c570c).  .data-initialised to 1
     * and never written elsewhere, so this never returns in practice;
     * reproduced for fidelity. */
    /* DAT_005c570c == 1 → proceed. */

    /* L48 — 2D render-state preset (FUN_0049b425). */
    render_quad_state_setup(dev);

    int scene_mode = g_scene_state;
    int stage_type = scene1_hud_stage_type();

    /* ── Pass 1 — stamina/HP backdrop (L49-73) ─────────────────────────
     * Gate: INGAME && DUNGEON && (FUN_0043647f(0x10) || DAT_056db104).
     * DUNGEON-only → dormant in HOUSE.  Full-screen 640x480 mask quad
     * over shade.bmp with SRCBLEND=ZERO / DESTBLEND=SRCCOLOR (dim the
     * backdrop by the sprite's per-pixel colour), then restore the
     * default SRCALPHA / INVSRCALPHA pair. */
    {
        int pred = 0;
        if (scene_mode == 1 && stage_type > 0) {
            pred = scene1_hud_fun_0043647f(0x10);
            if (scene1_hud_pass1_backdrop_active(scene_mode, stage_type, pred)) {
                const sprite_t *shade = &g_sysassets.shade_bmp;
                if (shade->tex) {
                    IDirect3DDevice8_SetTexture(dev, 0,
                        (IDirect3DBaseTexture8 *)shade->tex);
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ZERO);
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);

                    const float dst[4] = {   0.0f,   0.0f, 640.0f, 480.0f };
                    const float src[4] = {  64.0f,   0.0f, 192.0f, 128.0f };
                    render_quad_add(dst, src, shade->width, shade->height,
                                    scene1_hud_pass1_backdrop_color(pred));
                    render_quad_flush(dev);

                    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                }
            }
        }
    }

    /* ── Pass 2 — letterbox / cinema bars (L74-100) ────────────────────
     * Height keyed off DAT_0438b1dc (±0.1 dead-zone).  Two 640-wide
     * bars sampling a 7x7 patch of system.bmp: top at y=0 and bottom
     * flush against the screen edge, black (0xff000000). */
    {
        float bar_h = scene1_hud_letterbox_height();
        if (bar_h > 0.0f) {
            const sprite_t *sys = &g_sysassets.system_bmp;
            if (sys->tex) {
                float h = bar_h * 32.0f;
                IDirect3DDevice8_SetTexture(dev, 0,
                    (IDirect3DBaseTexture8 *)sys->tex);

                const float src[4] = { 0.0f, 0.0f, 7.0f, 7.0f };
                /* top bar: dst {0, 0, 640, h} */
                const float dst_top[4] = { 0.0f, 0.0f, 640.0f, h };
                render_quad_add(dst_top, src, sys->width, sys->height, 0xff000000u);
                /* bottom bar: dst {0, 480-h, 640, h} */
                const float dst_bot[4] = { 0.0f, 480.0f - h, 640.0f, h };
                render_quad_add(dst_bot, src, sys->width, sys->height, 0xff000000u);
                render_quad_flush(dev);
            }
        }
    }

    /* ── Pass 3 — status-screen takeover (L101-104) ────────────────────
     * When the in-shop status/Q-menu is open the HUD aggregator renders
     * that screen and returns before every later pass.  Dormant in
     * normal HOUSE play (DAT_073dddb4 == 0). */
    if (scene1_hud_status_screen_open()) {
        scene1_hud_status_screen_render_TODO();
        return;
    }

    /* Passes 4-9 (item tooltip, HOUSE/DUNGEON sub-walkers, speech
     * bubbles, shop terminal, chr render, dialog/sub-menu panels,
     * day-counter flash) land as later chips (C7k..C7p).  Most are
     * dormant in free-roam (shop/event-state gated).
     *
     * The one persistent member is FUN_00406d50 (decomp L6980): the
     * gold clock dial + rotating hand + "Day N" badge + money banner.
     * It is drawn unconditionally by the aggregator (the dormant passes
     * above it draw nothing), so we call it here at the equivalent
     * point. */

    /* FUN_00409925 (the HOUSE-town HUD), in engine order:
     *   - front (asm 0x409925-0x409cf0): the world-anchored item tooltip over
     *     the faced display stand (C3b) — gated on facing an occupied cell, so
     *     active in free-roam once items are placed; drawn first inside
     *     scene1_merchant_hud_render.
     *   - body L124-L179: the bottom-left "Merchant Level" badge + XP bar
     *     (scene1_merchant_hud_render); always drawn here.
     *   - tail LAB_0040a5fd: the bottom-right "Button 4: Change Camera" hint
     *     (scene1_top_hud_camera_hint); self-gates on no-dialogue-active.
     * The trailing shop/stocking UI is shop-state gated and dormant in
     * free-roam.  FUN_00406d50 (the top HUD) is emitted later by the
     * aggregator, so it draws last. */
    /* FUN_0040c4eb (all.c:7193) — the speech bubble (the day-close lines),
     * right after the status-screen takeover and before FUN_00409925.  Its
     * scene gates: the dialogue box (DAT_0438b1c8) — the rest
     * (b1d0/b1d8/bebc/be94/be9c/beb4) are 0 in the house scenes it shows in. */
    speech_bubble_render(dev_in, scene1_intro_dialogue_busy());

    /* FUN_0040a765: FUN_00409925 in the house (stage maptype 0), FUN_00407cac
     * (the floor HUD) on a floor */
    if (mapcol_maptype() == 0)
        scene1_merchant_hud_render(dev_in);
    if (mapcol_maptype() >= 1)
        dungeon_hud_render(dev_in);

    /* The interaction-affordance emote bubble (FUN_0040a765 inline @ all.c:6900):
     * engine order is FUN_00409925 → this.  The "GO!" door tooltip lives here. */
    scene1_hud_emote_bubble(dev);
    if (scene_mode == 1)
        scene1_hud_countdowns(dev);

    if (mapcol_maptype() == 0)
        scene1_top_hud_camera_hint(dev_in);   /* FUN_00409925's tail */

    scene1_top_hud_render(dev_in);

    /* FUN_0046b00a (0x40b0c3, all_ida 7402) — the item window, between the top
     * HUD and FUN_00466b7b: the customer's order bubble overlaps it (it drew
     * after FUN_00417504 in the port, over the bubble — dungeon3-deep f5000).
     * Self-gated on DAT_0734b98c. */
    display_menu_render(dev_in, 0.0f);

    /* FUN_00466b7b (all.c:7044) — the cc08==4 haggle dialogue box + typewriter
     * line + BARGAIN!! price.  Engine draws it AFTER the top HUD (FUN_00406d50
     * @6980), gated on INGAME && HOUSE; self-gated on b7b0 inside. */
    if (scene_mode == 1 && stage_type == 0)
        customer_service_render_overlay(dev_in);

    /* FUN_0043537e (all.c:7046) — the choice box, drawn UNGATED as part of the
     * HUD aggregator's tail.  When a dialogue's ESC skip prompt is armed the box
     * is drawn HERE **and** by FUN_0046c090's tail (scene1_dialogue_draw) — the
     * engine really composites the prompt block twice per frame, and the doubled
     * glyph alpha is retail's "stronger edges" look (viewer notes #7/#19; RE
     * §21.26, ret_va probe runs/probe-skipbox-callers: FUN_0043537e fires at
     * 0x40b0df + 0x46c0a8 on every modal frame).  Self-gates on cb_active. */
    choice_box_draw(dev);

    if (scene_mode == 1)
        news_ticker_render(dev);          /* 0x40b0df..0x40b3dd */
    if (scene_mode == 1)
        scene1_hud_tear_pages(dev);       /* LABEL_100 */

    /* FUN_00435747 (all.c:7498, LAB_0040c1e4) — the shared menu hand-cursor,
     * drawn LAST in the house aggregator (after the cc08 haggle overlay so the
     * hand sits on top of the Yes/No prompt).  Ungated like retail; the draw
     * self-gates on g_cursor_visible (set by the cc08 cursor snap/slide), so it
     * is a no-op in free-roam where no menu shows the cursor. */
    title_save_dialog_cursor_render(dev);

    /* FUN_00435117 (all.c:7499) — the save/load dialog frame, immediately after
     * the cursor like retail.  Self-gates (no active dialog ⇒ no-op). */
    title_save_dialog_render(dev);
}

#endif /* _WIN32 */
