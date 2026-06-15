/*
 * scene_pause.c — see scene_pause.h.
 *
 * Engine sources:
 *   - Unnamed FPU init @ 0x435873 (86 bytes; Ghidra missed it)
 *   - FUN_00473a3e         @ 0x473a3e (453 bytes)
 *
 * Both fire from the C4E secondary worker thread proc (LAB_00452c4e at
 * 0x452c4e), in that order, before the shared cleanup tail.
 */

#include "scene_pause.h"

#include "worker_load.h"
#include "sim.h"     /* the ramp counters g_sim_counter_998/99c + g_sim_mode_9a0
                      * (engine DAT_06a49998/9c/a0), g_sim_buttons[0] (input) */
#include "scene.h"   /* g_scene_state (engine DAT_0438b1c0) */
#include "audio.h"   /* audio_play_se_by_id (engine FUN_00499519) */
#include "save_picker.h" /* FUN_0049b537 perm init + the shared card-list render */
#include "save_bank.h"   /* save_header_get_last_slot = DAT_056e578c */
#include "save_work.h"   /* save_work_dwords_at / _active_slot — the commit source */
#include "encyclopedia.h" /* the type-6 Encyclopedia submenu (FUN_0049f012/f365/f8b8) */
#include "save_io.h"     /* save_io_commit_slot = FUN_004905a8(slot) (M4c) */
#include "choice_box.h"  /* the "Overwriting file." dialog (FUN_00434def/ed2) */
#include "stage_load_pulse.h" /* FUN_004682bf/d0 — DAT_0734b9a0 (stage-load-pulse active) */
#include "scene_new_game.h"   /* FUN_004682b9 — DAT_0734b9a4 (stage_load_pulse_b) */
#include "chara_equip.h"      /* FUN_004844ef — re-aggregate equip stats on unpause */
#include "d3d_pool.h"         /* FUN_00471905 / FUN_00473c03 — release pause assets (type 0xc) */

/* FUN_00435612/625/644/693 — the SHARED hand-cursor snapshot/restore used by the
 * pause open/close (and the Save-submenu B-cancel). Defined in title_save_dialog.c
 * (pure C); forward-declared here so the host-built pause_dispatch/nav can call
 * them (title_save_dialog.h is render-heavy and is only included in the _WIN32
 * block below for the render). */
void title_save_dialog_cursor_set_visible(int on);
int  title_save_dialog_cursor_get_visible(void);
void title_save_dialog_cursor_capture_target(float *x, float *y);
void title_save_dialog_cursor_snap(float x, float y);

/* ─── module state ───────────────────────────────────────────────────── */

int32_t g_scene_pause_selector = 0;

int32_t g_scene_pause_state_b150 = 0;
int32_t g_scene_pause_state_b158 = 0;
int32_t g_scene_pause_state_b15c = 0;
int32_t g_scene_pause_state_ac18 = 0;
int32_t g_scene_pause_state_ac1c = 0;
int32_t g_scene_pause_state_ac20 = 0;
float   g_scene_pause_state_abf4 = 0.0f;
float   g_scene_pause_state_abf8 = 0.0f;
float   g_scene_pause_state_ac00 = 0.0f;
float   g_scene_pause_state_ac04 = 0.0f;

/* ─── FPU init (engine unnamed @ 0x435873) ───────────────────────────── */

void scene_pause_state_init(void)
{
    /* Write order mirrors the engine asm (see scene_pause.h banner for
     * the disassembly). Polarity matters only insofar as a future
     * consumer can rely on these being the exact post-init values; the
     * end-state is the same regardless of order. */
    g_scene_pause_state_b150 = 1;     /* DAT_0438b150 = 1   */
    g_scene_pause_state_ac00 = 32.0f; /* DAT_0438ac00 = 32  */
    g_scene_pause_state_ac20 = 0;     /* DAT_0438ac20 = 0   */
    g_scene_pause_state_ac18 = 0;     /* DAT_0438ac18 = 0   */
    g_scene_pause_state_ac04 = 80.0f; /* DAT_0438ac04 = 80  */
    g_scene_pause_state_b158 = 0;     /* DAT_0438b158 = 0   */
    g_scene_pause_state_b15c = 0;     /* DAT_0438b15c = 0   */
    g_scene_pause_state_abf4 = 32.0f; /* DAT_0438abf4 = 32  */
    g_scene_pause_state_ac1c = 0;     /* DAT_0438ac1c = 0   */
    g_scene_pause_state_abf8 = 80.0f; /* DAT_0438abf8 = 80  */
}

/* ─── asset-load helpers ─────────────────────────────────────────────── */

/* Per-slot static metadata: fixed filename (NULL = use the selector) +
 * expected dims. Order matches the engine's call sequence in
 * FUN_00473a3e. */
static const struct {
    const char *fname;   /* NULL for slot 0 (selector-driven) */
    int         w, h;
} g_scene_pause_assets[SCENE_PAUSE_LOAD_COUNT] = {
    /* 0  pause / pause_endless (selector-driven)            */ { 0,                       0x400, 0x200 },
    /* 1  pause_bg_rete                                      */ { "bmp/pause_bg_rete.tga", 0x400, 0x200 },
    /* 2  result_bord01                                      */ { "bmp/result_bord01.tga", 0x200, 0x100 },
    /* 3  dungeonbord                                        */ { "bmp/dungeonbord.tga",   0x400, 0x200 },
    /* 4..11  sousa portraits (cursor headshots)             */
    { "bmp/sousa_lui.tga",     0x400, 0x200 },
    { "bmp/sousa_sya.tga",     0x400, 0x200 },
    { "bmp/sousa_cai.tga",     0x400, 0x200 },
    { "bmp/sousa_tel.tga",     0x400, 0x200 },
    { "bmp/sousa_era.tga",     0x400, 0x200 },
    { "bmp/sousa_nag.tga",     0x400, 0x200 },
    { "bmp/sousa_grf.tga",     0x400, 0x200 },
    { "bmp/sousa_arm.tga",     0x400, 0x200 },
    /* 12..19 status portraits (full character body)         */
    { "bmp/st_ryui.tga",       0x200, 0x200 },
    { "bmp/st_sya.tga",        0x200, 0x200 },
    { "bmp/st_caillou.tga",    0x200, 0x200 },
    { "bmp/st_tiers.tga",      0x200, 0x200 },
    { "bmp/st_eran.tga",       0x200, 0x200 },
    { "bmp/st_nagi.tga",       0x200, 0x200 },
    { "bmp/st_griffe.tga",     0x200, 0x200 },
    { "bmp/st_aruma.tga",      0x200, 0x200 },
};

/* Engine .rdata strings:
 *     s_bmp_pause_endless_tga_005c8b6c — "bmp/pause_endless.tga"
 *     s_bmp_pause_tga_005c8b84         — "bmp/pause.tga"
 */
static const char *const g_scene_pause_endless_path = "bmp/pause_endless.tga";
static const char *const g_scene_pause_normal_path  = "bmp/pause.tga";

static const char *scene_pause_slot0_filename(void)
{
    /* Engine polarity at FUN_00473a3e prologue: a == 2 || a == 3 →
     * endless, else normal. */
    const int32_t sel = g_scene_pause_selector;
    return (sel == 2 || sel == 3) ? g_scene_pause_endless_path
                                   : g_scene_pause_normal_path;
}

const char *scene_pause_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_PAUSE_LOAD_COUNT) return 0;
    if (slot == 0) return scene_pause_slot0_filename();
    return g_scene_pause_assets[slot].fname;
}

int scene_pause_slot_dims(int slot, int *out_w, int *out_h)
{
    if (slot < 0 || slot >= SCENE_PAUSE_LOAD_COUNT) return 0;
    if (out_w) *out_w = g_scene_pause_assets[slot].w;
    if (out_h) *out_h = g_scene_pause_assets[slot].h;
    return 1;
}

/* ─── pure-C body ────────────────────────────────────────────────────── */

int scene_pause_load_with(scene_pause_load_fn load_fn, void *userdata)
{
    /* Engine FUN_00473a3e is a straight-line 20-call sequence — no
     * loop bounds, no per-slot predicate. We iterate in slot order so
     * tests can observe deterministic dispatch ordering matching the
     * engine. */
    int loads = 0;
    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        const char *fname = (i == 0) ? scene_pause_slot0_filename()
                                     : g_scene_pause_assets[i].fname;
        if (load_fn) load_fn(fname, i,
                             g_scene_pause_assets[i].w,
                             g_scene_pause_assets[i].h,
                             userdata);
        loads++;
    }
    return loads;
}

/* ─── reset ──────────────────────────────────────────────────────────── */

static void scene_pause_state_clear(void)
{
    g_scene_pause_selector   = 0;
    g_scene_pause_state_b150 = 0;
    g_scene_pause_state_b158 = 0;
    g_scene_pause_state_b15c = 0;
    g_scene_pause_state_ac18 = 0;
    g_scene_pause_state_ac1c = 0;
    g_scene_pause_state_ac20 = 0;
    g_scene_pause_state_abf4 = 0.0f;
    g_scene_pause_state_abf8 = 0.0f;
    g_scene_pause_state_ac00 = 0.0f;
    g_scene_pause_state_ac04 = 0.0f;
    pause_sm_reset();
}

/* ─── pause state machine (mode 9) ──────────────────────────────────────
 *
 * Engine: FUN_00453384 (trigger) + FUN_0047f2f6 (menu build) + FUN_0047fa76
 * (update) + FUN_00480614 (nav). The ramp counters live in sim.c (already
 * ported as FUN_004532df, dormant); this module is the setter + consumers.
 * Pure C — host-tested. See docs/plans/pause-menu.md. */

int32_t g_pause_action      = 0;
int32_t g_pause_saved_mode  = 0;
int32_t g_pause_entries[SCENE_PAUSE_MAX_ENTRIES] = { 0 };
int32_t g_pause_count       = 0;
int32_t g_pause_sel         = 0;
int32_t g_pause_sel_anim    = 0;
int32_t g_pause_sub_anim    = 0;
int32_t g_pause_sub_dir     = 0;
int32_t g_pause_row_spacing = 0;
int32_t g_pause_exit_confirm = 0;
int32_t g_pause_frame       = 0;

/* Save submenu (type 3) picker state — the Save entry's cursor/scroll + the
 * slide/save-phase anims. Engine val[0]/val2[0]/c898/c894/c89c (DAT_074b2834/
 * 2820/2898/2894/289c); `cur` is always 0 for the Save submenu, so val[]/val2[]
 * collapse to single scalars. Seeded by the type-3 commit, read by the render
 * wrapper FUN_004812e4. */
int32_t g_pause_save_cursor  = 0;   /* DAT_074b2834 (val[0])  */
int32_t g_pause_save_scroll  = 0;   /* DAT_074b2820 (val2[0]) */
int32_t g_pause_save_vscroll = 0;   /* DAT_074b2898 (c898)    */
int32_t g_pause_save_hscroll = 0;   /* DAT_074b2894 (c894)    */
int32_t g_pause_save_phase   = 0;   /* DAT_074b289c (c89c)    */
int32_t g_pause_save_overwrite = 0; /* DAT_074b28a4 — overwrite-confirm dialog up */

/* Pause-open resume snapshot (engine DAT_06a499ac/b0/b4/b8/bc) — captured when
 * the pause opens (pause_dispatch enter), restored on unpause so the underlying
 * scene resumes EXACTLY as it was: the shared hand cursor's visibility+position
 * and the stage-load-pulse / shop-display state. The player/camera need no
 * snapshot — the engine never reloads the scene on unpause (it freezes it
 * through the open/close ramp and resumes in place), so the position survives. */
static int32_t g_pause_snap_cursor_vis = 0;  /* DAT_06a499ac = FUN_00435625 */
static float   g_pause_snap_cursor_x   = 0;  /* DAT_06a499b0 = FUN_00435644 */
static float   g_pause_snap_cursor_y   = 0;  /* DAT_06a499b4               */
static int32_t g_pause_snap_pulse      = 0;  /* DAT_06a499b8 = FUN_004682bf */
static int32_t g_pause_snap_pulse_b    = 0;  /* DAT_06a499bc = FUN_004682b9 */

/* Menu-build inputs (engine DAT_0741bed8 + *DAT_068dd2f0). */
static int g_pause_in_status_count = 0;
static int g_pause_in_stage_type   = 0;

void pause_set_menu_inputs(int status_count, int stage_type)
{
    g_pause_in_status_count = status_count;
    g_pause_in_stage_type   = stage_type;
}

void pause_sm_reset(void)
{
    g_pause_action       = 0;
    g_pause_saved_mode   = 0;
    for (int i = 0; i < SCENE_PAUSE_MAX_ENTRIES; i++) g_pause_entries[i] = 0;
    g_pause_count        = 0;
    g_pause_sel          = 0;
    g_pause_sel_anim     = 0;
    g_pause_sub_anim     = 0;
    g_pause_sub_dir      = 0;
    g_pause_row_spacing  = 0;
    g_pause_exit_confirm = 0;
    g_pause_frame        = 0;
    g_pause_save_cursor  = 0;
    g_pause_save_scroll  = 0;
    g_pause_save_vscroll = 0;
    g_pause_save_hscroll = 0;
    g_pause_save_phase   = 0;
    g_pause_save_overwrite = 0;
    g_pause_snap_cursor_vis = 0;
    g_pause_snap_cursor_x   = 0;
    g_pause_snap_cursor_y   = 0;
    g_pause_snap_pulse      = 0;
    g_pause_snap_pulse_b    = 0;
    g_pause_in_status_count = 0;
    g_pause_in_stage_type   = 0;
}

/* FUN_00453384 — the ESC trigger / pause toggle.
 *
 * PORT-DEBT(simplified, FUN_00453384): the engine has a thicket of mode-1
 * sub-gates (FUN_00434dd6 overlay, shop/dialogue state DAT_0438cc08/b928,
 * the choice-box DAT_0438b1c8 path → cursor-snapshot arm) and a multi-clause
 * toggle gate (the fade dir DAT_0438bf7c, DAT_0438be98/be94). For the resting
 * free-roam scene none of those fire; we port the plain path. The unpause
 * restore (cursor snapshot DAT_06a499ac/b0/b4, the FUN_004682d0/00473c03
 * resume teardown) is deferred — PORT-DEBT(pause-unpause-restore). */
void pause_dispatch(int action)
{
    /* Gate A (L50180): a transition is mid-flight with a DIFFERENT action
     * queued → reject with the denied beep. */
    if ((g_scene_state == 9 || sim_get_counter_998() > 0)
        && g_pause_action != action) {
        audio_play_se_by_id(0x16a);
        return;
    }
    /* Gate B (L50186): the secondary asset-load worker is busy → bail. */
    if (worker_load_busy_secondary() != 0)
        return;

    g_pause_action = action;

    /* cVar4 (L50191): pausable unless the mode is 7/2/3/10. */
    const int pausable = (g_scene_state != 7 && g_scene_state != 2
                          && g_scene_state != 3 && g_scene_state != 10);
    if (!pausable) {
        audio_play_se_by_id(0x16a);   /* L50246 — the "can't pause" beep */
        return;
    }

    /* Toggle gate (L50248): no fade in flight. PORT-DEBT(simplified): the
     * engine also checks DAT_0438bf7c==0 + DAT_0438be98==0, both 0 on a
     * resting scene; we gate on the fade sub-counter DAT_06a49990 idle. */
    if (sim_get_counter_990() != 0)
        return;

    if (g_scene_state == 9) {
        /* UNPAUSE (engine L50252): only once fully open (ramp past 0xb).
         *
         * The engine does NOT reload the underlying scene on unpause — it
         * froze it through the open ramp (the per-mode sim dispatch is skipped
         * while g_sim_counter_998 != 0) and resumes it IN PLACE once the close
         * ramp (dir=0) cycles back to 0. Re-spawning the worker (the old
         * PORT-DEBT) re-ran the INGAME case-1 load = scene1_preload_house,
         * whose scene1_postload_pose_player re-seated Recette at the scene
         * SPAWN — the user-observed bug. Run the engine teardown instead
         * (FUN_00453384 L50254-50283); the assets stay resident (the port's
         * pause sprites aren't d3d_pool entries, so the close animation keeps
         * drawing them and a re-pause reloads idempotently). */
        if (sim_get_counter_998() > 0xb) {
            g_scene_state = g_pause_saved_mode;            /* DAT_0438b1c0 = DAT_06a499a8 */
            /* (saved_mode==6 → FUN_00490e15 guild re-init — PORT-DEBT, guild-pause) */
            stage_load_pulse_set_active(0);                /* FUN_004682d0 */
            title_save_dialog_cursor_set_visible(0);       /* FUN_00435612 — hide cursor */
            chara_equip_recompute_aggregate();             /* FUN_004844ef — re-aggregate equip stats */
            if (action == 0)
                d3d_pool_release_type(0xc);                /* FUN_00473c03 — free pause assets (type 0xc) */
            /* (actions 1/2 → FUN_00473668/672 — PORT-DEBT) */
            sim_set_mode_9a0(0);                            /* DAT_06a499a0 = 0 (closing) */
            /* Restore the shared hand cursor IFF it was visible at pause
             * (engine L50274). Inert in HOUSE free-roam (cursor hidden). */
            if (g_pause_snap_cursor_vis)
                title_save_dialog_cursor_snap(g_pause_snap_cursor_x,
                                              g_pause_snap_cursor_y);  /* FUN_00435693 */
            /* Shop-display restore (engine L50277-50280): only when a shop grid
             * was active at pause (DAT_06a499b8). Inert in HOUSE/guild-menu
             * (pulse==0); the full FUN_00468338 rebuild = PORT-DEBT(pause-shop-restore).
             *   if (g_pause_snap_pulse) { FUN_00468338(g_pause_snap_pulse_b,1); FUN_004682e3(); }
             * action 0 → FUN_004681d3 (DAT_0734b96c shop-interaction reset) is also
             * unported (a shop-grid index, 0/inert in HOUSE). */
            (void)g_pause_snap_pulse; (void)g_pause_snap_pulse_b;
        }
    } else if (sim_get_counter_998() == 0) {
        /* ENTER PAUSE (engine L50292). */
        sim_set_counter_994(0, sim_get_threshold94());  /* DAT_06a49994 = 0 */
        if (action == 0)
            audio_play_se_by_id(0x16b);                 /* pause-open chime */
        g_pause_saved_mode = g_scene_state;             /* DAT_06a499a8 */
        /* Snapshot the resume state (engine L50298-50302): the shared hand
         * cursor (visibility + slide-target position) and the stage-load-pulse
         * / shop state, restored on unpause above. _DAT_06a499c0 (FUN_004681e6)
         * is write-only in the engine — skipped. */
        g_pause_snap_cursor_vis = title_save_dialog_cursor_get_visible();   /* DAT_06a499ac = FUN_00435625 */
        title_save_dialog_cursor_capture_target(&g_pause_snap_cursor_x,
                                                &g_pause_snap_cursor_y);    /* DAT_06a499b0/b4 = FUN_00435644 */
        g_pause_snap_pulse   = stage_load_pulse_get_active();               /* DAT_06a499b8 = FUN_004682bf */
        g_pause_snap_pulse_b = scene_new_game_stage_load_pulse_b_get();     /* DAT_06a499bc = FUN_004682b9 */
        sim_set_counter_998(1);                         /* ramp = 1     */
        sim_set_counter_99c(1);                         /* slide ramp = 1 */
        sim_set_mode_9a0(1);                            /* dir = opening */
        /* (engine L50306 FUN_004681d3 = DAT_0734b96c reset — unported, inert) */
    }
}

/* FUN_0047f2f6 — build the menu entry-type list. */
void pause_menu_setup(void)
{
    /* Cursor + anim reset (L81556-81560). The engine also clears ~15
     * submenu-scratch globals + the DAT_0438b554[0x14] array + calls
     * FUN_004360b6/FUN_0049f012 — all submenu/render scratch, PORT-DEBT. */
    g_pause_sel_anim     = 0;
    g_pause_frame        = 0;
    g_pause_sel          = 0;
    g_pause_sub_anim     = 0;
    g_pause_sub_dir      = 0;
    g_pause_exit_confirm = 0;

    /* FPU layout consts (engine 0x435873, C4E-only; harmless to set here —
     * the basic menu may not read them, but a future status sub-screen
     * does). */
    scene_pause_state_init();

    /* Default-fill 0..7 (L81585-81589) then overwrite with the real list. */
    for (int i = 0; i < SCENE_PAUSE_MAX_ENTRIES; i++) g_pause_entries[i] = i;

    /* Entry-type list (L81591-81606). */
    const int has_status = (g_pause_in_status_count > 0);  /* 0 < DAT_0741bed8 */
    if (has_status)
        g_pause_entries[0] = 0;                 /* adventurer Status */
    const int u = has_status ? 1 : 0;
    g_pause_entries[u] = 1;                      /* Items */
    int n = u + 1;
    if (g_pause_saved_mode == 1 && g_pause_in_stage_type > 0) {
        g_pause_entries[n] = 5;                  /* dungeon-only */
        n = u + 2;
    }
    g_pause_entries[n + 0] = 6;                  /* Encyclopedia */
    g_pause_entries[n + 1] = 2;                  /* Options */
    g_pause_entries[n + 2] = 3;                  /* Save */
    g_pause_entries[n + 3] = 4;                  /* Exit Game */
    g_pause_count = n + 4;                       /* DAT_073e154c */
    g_pause_row_spacing = (0xb - g_pause_count) * 0xc;  /* DAT_005cc678 */

    /* FUN_0049f012(0) (engine L81616) — build the Encyclopedia catalog from the
     * active bank's discovery store + the item DB.  RNG-neutral / idempotent. */
    encyclopedia_setup(0);
}

/* FUN_00480614 — the main-menu nav. */
void pause_menu_nav(void)
{
    const uint16_t pressed = g_sim_buttons[0].pressed;  /* DAT_073dddd4 */
    const uint16_t held    = g_sim_buttons[0].held;     /* DAT_073dddd6 */

    if (g_pause_sel_anim < 1) {
        uint16_t se;
        if ((pressed & 0x10u) == 0) {            /* A not pressed */
            if (pressed & 0x20u) {               /* B → close (FUN_0045337b) */
                pause_dispatch(0);
                return;
            }
            if (held & 0x4u) {                   /* up */
                g_pause_sel = (g_pause_count - 1 + g_pause_sel) % g_pause_count;
                audio_play_se_by_id(0x146);
            }
            if ((held & 0x8u) == 0)              /* not down → done */
                return;
            se = 0x146;                          /* down */
            g_pause_sel = (g_pause_count + 1 + g_pause_sel) % g_pause_count;
        } else {                                 /* A → start select anim */
            g_pause_sel_anim++;
            se = 0x143;
        }
        audio_play_se_by_id(se);
    } else {
        /* Select anim running (engine L82637). At sel_anim==0xf the engine
         * commits the selection (LAB_004806a1: shared reset + per-type init,
         * then sub_anim++ / sub_dir=1 to open the submenu). Only the Save
         * submenu (type 3) is ported here; the Status/Items/Options/
         * Encyclopedia/dungeon submenus + the type-4 exit confirm stay
         * PORT-DEBT(pause-submenu-*) — for those types we tick the anim but
         * don't open. */
        g_pause_sel_anim++;
        if (g_pause_sel_anim == 0xf) {
            const int t = g_pause_entries[g_pause_sel];
            if (t == 3) {
                /* type-3 branch (engine L82694): clear the save anims, init the
                 * 100-slot perm, seed the cursor from the last-used slot. */
                g_pause_save_phase   = 0;          /* DAT_074b289c */
                g_pause_save_vscroll = 0;          /* DAT_074b2898 (c898) */
                g_pause_save_hscroll = 0;          /* DAT_074b2894 (c894) */
                g_save_picker_restricted = 0;      /* DAT_09643564 */
                save_picker_perm_init();           /* FUN_0049b537 */
                const int last = save_header_get_last_slot();        /* DAT_056e578c */
                g_pause_save_cursor = last;                           /* val[cur]  */
                g_pause_save_scroll = (last - 2 < 0) ? 0 : last - 2;  /* val2[cur] */
                g_pause_sub_anim++;                 /* DAT_074b2880 — open */
                g_pause_sub_dir = 1;               /* DAT_074b2884 — opening */
            } else if (t == 6) {
                /* type-6 branch (engine L82671): snap the shared hand cursor to
                 * the grid's first cell (72,112) and open the submenu.  The
                 * catalog itself was built in pause_menu_setup (FUN_0049f012). */
                title_save_dialog_cursor_snap(72.0f, 112.0f);  /* FUN_00435693 */
                g_pause_sub_anim++;
                g_pause_sub_dir = 1;
            }
        }
    }
}

/* The save-card "type" by game mode (engine 0x47f659-0x47f6e0). Written into
 * the working bank at commit, copied to the slot. PIXEL-INVISIBLE here — the
 * picker render (save_picker_render) reads GAME_MODE/SCORE/…, never this
 * 0xb381 field — but kept faithful so the SAVED bytes match retail. */
static int pause_save_card_type(void)
{
    switch (g_pause_saved_mode) {            /* DAT_06a499a8 (iVar6) */
    case 1: return (g_pause_in_stage_type > 0) ? 1 : 0;  /* *DAT_068dd2f0>0 ? */
    case 2: return 2;
    case 7: return 0;
    /* mode 6 (the DAT_0963c5f0 guild-rank split → 3/4) + mode 0xb (which copies
     * the live DAT_0438b5ec/664 dungeon snapshot in) need un-modeled data and
     * are unreachable from the ported pause Save submenu;
     * PORT-DEBT(save-card-type-modes). The shared default is 1. */
    default: return 1;
    }
}

/* FUN_0047f5bc commit tail (engine 0x47f63f-0x47f73f) — the phase>=1 sequence:
 * on the first frame (phase==1) snapshot the card fields + play the jingle +
 * write the slot to disk; every frame advance the phase 1→0x3c then wrap to 0. */
static void pause_save_commit_tick(void)
{
    if (g_pause_save_phase == 1) {
        const int tslot  = g_pause_save_cursor;        /* DAT_074b2834[cur] */
        const int active = save_work_active_slot();    /* DAT_0438b1e0 */

        /* card-snapshot (engine 0x47f648-0x47f6f8): clear the 2 preview blocks
         * + stamp the card type in the WORKING bank (the commit then copies it
         * into the slot). */
        uint32_t *wb = save_work_dwords_at(active);
        if (wb) {
            for (int i = 0; i < 0x1e; i++) wb[0xb75a + i] = 0xffffffffu;  /* DAT_04511500 */
            for (int i = 0; i < 8;    i++) wb[0xb78e + i] = 0xffffffffu;  /* DAT_045115d0 */
            wb[0xb381] = (uint32_t)pause_save_card_type();                /* DAT_0451059c */
        }

        /* the streamed save jingle (engine FUN_0049933c @ 0x47f730). */
        audio_play_se_file("bin/se/01ti/system/01ti_sys04.bin");

        save_header_set_last_slot(tslot);              /* DAT_056e578c */

        /* The engine brackets the write with FUN_0047f172(1)/FUN_0047f1a0(1)
         * (a working↔scratch backup/restore) + a dungeon-only FUN_0047f1a0(0)
         * swap. In the HOUSE (non-dungeon) the bracket wraps only the
         * working→slot copy, which never mutates the working bank ⇒ a provable
         * no-op; omitted with the dungeon swap as PORT-DEBT(save-commit-dungeon). */
        save_io_commit_slot(tslot);                    /* FUN_004905a8(tslot) */
    }

    g_pause_save_phase++;                               /* engine 0x47f73a */
    if (g_pause_save_phase == 0x3c) g_pause_save_phase = 0;
}

/* FUN_0047f5bc — the Save submenu slot-picker nav + A-commit.
 *
 * Dispatched from pause_menu_update when the submenu is fully open
 * (sub_anim==10) and the selected entry is Save (type 3). `cur`
 * (DAT_074b288c) is always 0 for the pause Save submenu, so the engine's
 * 4-wide val[]/val2[] (DAT_074b2834/2820) collapse to the scalars
 * g_pause_save_cursor/scroll. Transcribed from objdump @0x47f5bc..0x47fa4f.
 *
 * Nav model (engine 0x47f85f-0x47fa4e):
 *   U / D  — cursor ±1; when the cursor crosses the 5-row window edge a
 *            c894 row-slide (g_pause_save_hscroll, ±1→±5 over 4 frames →
 *            scroll ±1) animates the catch-up.
 *   L / R  — cursor ±3 (page jump, clamped 0..99); when it crosses the
 *            window a c898 column-slide (g_pause_save_vscroll, → scroll ±3,
 *            clamped 0..97) animates it.
 *   B      — cancel: SE 0x13d, drop sub_dir (the submenu slides closed),
 *            clear sel_anim, hide the shared cursor (FUN_00435612).
 *   A      — confirm (M4c, 0x47f889): SE 0x143; an EMPTY slot commits at once
 *            (phase=1), an OCCUPIED slot pops the "Overwriting file." choice
 *            box (FUN_00434def), whose Yes/No is polled below.
 *
 * PORT-DEBT(save-commit-dungeon): the dungeon "Saving here will save your
 * data<BR>as it was prior to entering." warning (engine 0x47f5da, gated
 * saved_mode==1 && stage_type>0 — inert in the HOUSE where stage_type==0, so
 * the engine falls straight through and skipping it here is exactly
 * equivalent) + the FUN_0047f1a0(0) town-state swap; both need a dungeon
 * save trace. */
void pause_save_submenu_update(void)
{
    const uint16_t pressed = g_sim_buttons[0].pressed;  /* DAT_073dddd4 */
    const uint16_t held    = g_sim_buttons[0].held;     /* DAT_073dddd6 */

    /* phase>=1 → the commit animation is running (engine: DAT_074b289c >= 1
     * wraps the whole nav block). Tick the 60-frame sequence and bail. */
    if (g_pause_save_phase >= 1) {
        pause_save_commit_tick();
        return;
    }

    /* ── overwrite-confirm dialog response (engine 0x47f758, DAT_074b28a4==1).
     * The "Overwriting file." box is up; poll Yes/No. Yes → start the commit
     * (phase=1); No → clear the two anim flags the engine resets; busy → hold. */
    if (g_pause_save_overwrite == 1) {
        int r = choice_box_poll(pressed, 1);   /* FUN_00434ed2(1) */
        if (r == CB_OPT0) {                     /* Yes */
            g_pause_save_phase = 1;
        } else if (r == CB_OPT1) {              /* No */
            g_pause_exit_confirm = 0;           /* DAT_074b2830 */
            g_pause_sel_anim     = 0;           /* DAT_074b2870 */
        } else {
            return;                             /* CB_BUSY / CB_INACTIVE: still up */
        }
        g_pause_save_overwrite = 0;             /* DAT_074b28a4 = 0 */
        return;
    }

    /* ── c894 (hscroll) U/D row-slide anim (engine 0x47f793) ─────────────
     * Ramp the counter away from 0; at ±5 commit scroll ±1 and reset. While
     * an anim runs the buttons are not polled (matches the engine return). */
    if (g_pause_save_hscroll != 0) {
        int a = g_pause_save_hscroll;
        if (a < 0) a -= 1;
        if (a > 0) a += 1;
        g_pause_save_hscroll = a;
        if (a == -5)      { g_pause_save_scroll -= 1; g_pause_save_hscroll = 0; }
        else if (a == 5)  { g_pause_save_scroll += 1; g_pause_save_hscroll = 0; }
        return;
    }
    /* ── c898 (vscroll) L/R column-slide anim (engine 0x47f7e9) ──────────
     * At ±5 commit scroll ±3 (clamped 0..97) and reset. */
    if (g_pause_save_vscroll != 0) {
        int a = g_pause_save_vscroll;
        if (a < 0) a -= 1;
        if (a > 0) a += 1;
        g_pause_save_vscroll = a;
        if (a == -5) {
            g_pause_save_vscroll = 0;
            g_pause_save_scroll -= 3;
            if (g_pause_save_scroll < 0) g_pause_save_scroll = 0;
            return;
        }
        if (a == 5) {
            g_pause_save_scroll += 3;
            g_pause_save_vscroll = 0;
            if (g_pause_save_scroll > 0x61) g_pause_save_scroll = 0x61;
        }
        return;
    }

    /* ── buttons (only when no slide anim is in flight) ──────────────────── */
    if (pressed & 0x20u) {                 /* B → cancel / close (0x47f85f) */
        audio_play_se_by_id(0x13d);
        g_pause_sub_dir  = 0;              /* DAT_074b2884 → submenu closes  */
        g_pause_sel_anim = 0;              /* DAT_074b2870                   */
        title_save_dialog_cursor_set_visible(0);  /* FUN_00435612           */
        return;
    }
    if (pressed & 0x10u) {                 /* A → confirm (0x47f889) */
        const int tslot = g_pause_save_cursor;   /* DAT_074b2834[cur], cur=0 */
        audio_play_se_by_id(0x143);              /* FUN_00499519(0x143) */
        /* Occupancy = the target slot bank's playtime dword (engine
         * (&DAT_056e6288)[tslot*0xb7f2]). perm is identity, so val[cur] is the
         * bank slot directly. Empty → commit now; occupied → confirm box. */
        const uint32_t *tb = save_bank_dwords_at(tslot);
        if (!tb || tb[SAVE_BANK_FIELD_OCCUPIED] == 0) {
            g_pause_save_phase = 1;              /* DAT_074b289c = 1 → commit */
            return;
        }
        choice_box_open("Overwriting file. Are you sure?", /*mode=*/1, /*sel=*/0);
        g_pause_save_overwrite = 1;              /* DAT_074b28a4 = 1 */
        return;
    }

    if (held & 0x2u) {                     /* LEFT: cursor -= 3 (0x47f8da) */
        if (g_pause_save_cursor <= 0) return;
        if (g_pause_save_scroll <= 0) return;
        audio_play_se_by_id(0x146);
        g_pause_save_cursor -= 3;
        if (g_pause_save_cursor < 0) g_pause_save_cursor = 0;
        if (g_pause_save_cursor - g_pause_save_scroll >= 0) return;
        g_pause_save_vscroll = -1;         /* start the column slide */
        return;
    }
    if (held & 0x1u) {                     /* RIGHT: cursor += 3 (0x47f943) */
        if (g_pause_save_cursor >= 0x63) return;   /* >= 99 */
        if (g_pause_save_scroll >= 0x61) return;   /* >= 97 */
        audio_play_se_by_id(0x146);
        g_pause_save_cursor += 3;
        if (g_pause_save_cursor > 0x63) g_pause_save_cursor = 0x63;
        if (g_pause_save_cursor - g_pause_save_scroll <= 0) return;
        g_pause_save_vscroll = 1;
        return;
    }
    if (held & 0x4u) {                     /* UP: cursor -= 1 (0x47f9b2) */
        if (g_pause_save_cursor <= 0) return;
        audio_play_se_by_id(0x146);
        g_pause_save_cursor -= 1;
        if (g_pause_save_cursor - g_pause_save_scroll >= 0) return;
        g_pause_save_hscroll = -1;         /* start the row slide */
        return;
    }
    if (held & 0x8u) {                     /* DOWN: cursor += 1 (0x47fa00) */
        if (g_pause_save_cursor >= 0x63) return;   /* >= 99 */
        audio_play_se_by_id(0x146);
        g_pause_save_cursor += 1;
        if (g_pause_save_cursor - g_pause_save_scroll <= 2) return;
        g_pause_save_hscroll = 1;
        return;
    }
}

/* The Save submenu is open + navigable (the anchor SAVE_PICKER_READY predicate):
 * scene mode 9, the submenu fully open (sub_anim==10), Save (type 3) selected. */
int pause_save_picker_navigable(int scene_mode)
{
    return scene_mode == 9
        && g_pause_sub_anim == 10
        && g_pause_sel >= 0 && g_pause_sel < SCENE_PAUSE_MAX_ENTRIES
        && g_pause_entries[g_pause_sel] == 3;
}

/* The Encyclopedia submenu is open + navigable (the anchor ENCYCLOPEDIA_READY
 * predicate): scene mode 9, sub_anim==10, Encyclopedia (type 6) selected.
 * Rebases nav past the per-side-variable pause-open ramp (same as the save
 * picker) so the hand-cursor bob + nav inputs are picker-time-relative. */
int pause_encyclopedia_navigable(int scene_mode)
{
    return scene_mode == 9
        && g_pause_sub_anim == 10
        && g_pause_sel >= 0 && g_pause_sel < SCENE_PAUSE_MAX_ENTRIES
        && g_pause_entries[g_pause_sel] == 6;
}

/* FUN_0047fa76 — the mode-9 per-frame update. */
void pause_menu_update(void)
{
    /* FUN_0048b6ad() (portrait/clock per-frame) — PORT-DEBT. */
    if (g_pause_exit_confirm < 1) {
        if (g_pause_sub_anim < 1) {
            pause_menu_nav();
        } else {
            /* Submenu open/close anim (L82019-82030). */
            if (g_pause_sub_dir == 0) {
                g_pause_sub_anim--;
                if (g_pause_sub_anim < 1) g_pause_sub_anim = 0;
            } else {
                g_pause_sub_anim++;
                if (g_pause_sub_anim > 10) g_pause_sub_anim = 10;
            }
            /* L82031 — once fully open, dispatch to the per-type submenu
             * updater. Save (type 3 → FUN_0047f5bc) + Encyclopedia (type 6 →
             * FUN_0049f365) are ported; the dungeon (5)/Items (1)/Status (0)/
             * Options (2) updaters stay PORT-DEBT(pause-submenu-*). */
            if (g_pause_sub_anim == 10) {
                const int t = g_pause_entries[g_pause_sel];
                if (t == 3) {
                    pause_save_submenu_update();
                } else if (t == 6) {
                    /* engine L82045: close on B (returns 1). */
                    if (encyclopedia_update() == 1) {
                        g_pause_sub_dir  = 0;     /* DAT_074b2884 */
                        g_pause_sel_anim = 0;     /* DAT_074b2870 */
                        title_save_dialog_cursor_set_visible(0);  /* FUN_00435612 */
                    }
                }
            }
        }
    }
    /* else: exit-confirm (return-to-title) — PORT-DEBT(pause-exit-confirm). */

    g_pause_frame++;   /* _DAT_074b2874 */
    /* FUN_004356cd() (shared cursor bob/slide) runs from the integration
     * layer's per-frame cursor tick, as for the other menus. */
}

/* ─── Win32 worker_load wiring + sprite storage ─────────────────────── */

#ifdef _WIN32

#include <d3d8.h>
#include <math.h>          /* sinf — the selected-row flash */
#include "render_quad.h"   /* render_quad_state_setup / bind / add / flush */
#include "choice_box.h"          /* choice_box_draw = FUN_0043537e (tail) */
#include "title_save_dialog.h"   /* the shared cursor + save-dialog frame (tail) */
#include "save_work.h"           /* save_work_dwords_at / save_work_active_slot (the bank) */
#include "sysassets.h"           /* g_sysassets.item_win_tga = DAT_073d8748 (save-bar) */
#include "scene1_top_hud.h"      /* scene1_top_hud_draw_number = FUN_00406a60 (gold/quota) */
#include "scene1_merchant_hud.h" /* scene1_merchant_hud_draw_level = FUN_00481ec3 (level) */

sprite_t g_scene_pause_pause;
sprite_t g_scene_pause_bg_rete;
sprite_t g_scene_pause_result_bord01;
sprite_t g_scene_pause_dungeonbord;
sprite_t g_scene_pause_sousa[SCENE_PAUSE_SOUSA_COUNT];
sprite_t g_scene_pause_status[SCENE_PAUSE_STATUS_COUNT];

static IDirect3DDevice8 *g_scene_pause_dev = 0;

/* Slot → destination sprite_t. Mirrors the engine's BSS layout. */
static sprite_t *scene_pause_slot_dest(int slot)
{
    switch (slot) {
        case 0:  return &g_scene_pause_pause;
        case 1:  return &g_scene_pause_bg_rete;
        case 2:  return &g_scene_pause_result_bord01;
        case 3:  return &g_scene_pause_dungeonbord;
        default: break;
    }
    if (slot >= 4 && slot <= 11) return &g_scene_pause_sousa[slot - 4];
    if (slot >= 12 && slot <= 19) return &g_scene_pause_status[slot - 12];
    return 0;
}

static int win32_load_fn(const char *path, int slot, int w, int h,
                          void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    sprite_t *dst = scene_pause_slot_dest(slot);
    if (!dst) return 0;
    /* Engine: FUN_0047193c(0xc, dst, name, w, h). Format flag 0xc is
     * dropped — same as all other sprite_load call sites. */
    return sprite_load(dev, path, (uint32_t)w, (uint32_t)h, dst);
}

static void scene_pause_body(void)
{
    /* Engine LAB_00452c4e (objdump @ 0x452c4e..c53):
     *
     *     call 0x435873   ; pause-state FPU init
     *     call 0x473a3e   ; 20-asset load
     *
     * Order matters: the FPU init writes the (32,80) pause-layout
     * constants that any consumer of pause-menu rendering reads. Both
     * land in the same secondary worker tick. */
    scene_pause_state_init();
    scene_pause_load_with(win32_load_fn, g_scene_pause_dev);
}

/* Primary worker case 9 (objdump 0x4529c6 — the AUTHORITATIVE live path;
 * the C4E secondary FUN_00452e75 is unreferenced/dead). When the worker is
 * spawned with g_scene_state==9 (at ramp==3) it sub-dispatches on the pause
 * action: action 0 (ESC menu) → FUN_00473a3e (the 20-asset pause load).
 * Actions 1/2 (other entries) = PORT-DEBT. The FPU init (0x435873) is
 * C4E-only; pause_menu_setup already ran it on the main thread. */
static void pause_worker_case9(void)
{
    if (g_pause_action == 0)
        scene_pause_load_with(win32_load_fn, g_scene_pause_dev);
}

void scene_pause_init(struct IDirect3DDevice8 *dev)
{
    g_scene_pause_dev = (IDirect3DDevice8 *)dev;
    /* The C4E secondary body (dead in the engine, kept for completeness). */
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C4E, scene_pause_body);
    /* The LIVE pause-load path: primary worker case 9. */
    worker_load_set_cb(9, pause_worker_case9);
}

/* ─── calendar / quota date math (engine FUN_00482033/59, FUN_0048d997) ───
 *
 * All three read the WORKING save bank by byte offset (engine reads
 * &DAT_044e3798 + slot*0x2dfc8 + off; the port's save_work_dwords_at(slot)
 * is that same arena, dword-indexed).  Field map (dword index / byte off):
 *   gold     3      / 0xc       day      0xb0fb / 0x2c3ec
 *   pe-cache 0xb0fa / 0x2c3e8   xp-cur   0xb0fd / 0x2c3f4
 *   xp-start 0xb0fe / 0x2c3f8   xp-end   0xb0ff / 0x2c3fc
 *   level    0xb100 / 0x2c400   mode     0xb759 / 0x2dd64
 *   discount byte 0x2bc56.
 * NB the save_bank.h "DAY_INDEX"(0xb0fe)/"RANK_THRESHOLD"(0xb0ff) names are
 * the merchant-rank XP start/next-threshold here (FUN_00406xxx XP animator),
 * NOT calendar fields; the calendar day is CARD_DAY (0xb0fb). */

/* FUN_00482033 (all.c:83631) — the calendar "today" day index for `bank`. */
static int pause_day_index(const uint32_t *bank)
{
    int day  = (int32_t)bank[0xb0fb];      /* +0x2c3ec */
    int mode = (int32_t)bank[0xb759];      /* +0x2dd64 */
    if (mode != 2) {
        if (mode != 3)
            return day;                     /* normal mode: raw day */
        day -= 0x24;                        /* mode 3: day - 36 */
    }
    return day % 0x23;                      /* mode 2/3: wrap to the 35-day cycle */
}

/* FUN_00482059 (all.c:83649) — the period-end (next "payment due") day, the
 * day rounded up to the next week boundary; cached back to +0x2c3e8 (the
 * engine writes it, idempotent for a given day). */
static int pause_period_end(void)
{
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (!bank) return 0;
    int day  = pause_day_index(bank);
    int mode = (int32_t)bank[0xb759];
    int pe;
    if (mode == 2 || mode == 3) {
        pe = (day / 7) * 7 + 6;
    } else if (day == 0) {
        bank[0xb0fa] = 7;                   /* +0x2c3e8 cache */
        return 7;
    } else {
        pe = ((day - 1) / 7 + 1) * 7;       /* round up to next multiple of 7 */
    }
    bank[0xb0fa] = (uint32_t)pe;
    return pe;
}

/* FUN_0048d997 (all.c:91012) — the weekly quota number (the payment due by
 * the period end); halved-by-100 when the discount byte +0x2bc56 is set. */
static int pause_weekly_quota(void)
{
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (!bank) return 0;
    int mode = (int32_t)bank[0xb759];
    int q = 500000;
    if (mode == 2) {
        int week = (int32_t)bank[0xb0fb] / 7;   /* DAT_0450fb84 = day field +0x2c3ec */
        if (week == 0)      q = 20000;
        else if (week == 1) q = 80000;
        else if (week == 2) q = 200000;
        else if (week == 3) q = 500000;
        else if (week == 4) q = 1000000;
        else if (week == 5) q = 2000000;
        else                q = (week - 3) * 1000000;
    } else {
        int pe = pause_period_end();
        if (pe == 0)        q = 300;
        else if (pe < 9)    q = 10000;
        else if (pe < 0x10) q = 30000;          /* < 16 */
        else if (pe > 0x16) q = (pe > 0x1d) ? 500000   /* > 29: default */
                                            : 200000;  /* 23..29     */
        else                q = 80000;          /* 16..22 */
    }
    if (((const uint8_t *)bank)[0x2bc56] != 0)  /* discount halves to 1/100 */
        q /= 100;
    return q;
}

/* FUN_004820ba — the pause-menu render.
 *
 * M2  (done): the pause_bg_rete backdrop (engine L83706-83725).
 * M2b (done): the option list (L83726-83789 — COLOROP=ADD icon+label rows
 *             from pause.tga) + the "PAUSE MENU" header (L83790-83800,
 *             COLOROP=MODULATE) + the shared overlay tail (L83953-83955).
 * M2c (this): the calendar/gold/level block (L83801-83930, gated sub_anim<10
 *             → drawn at rest): pause.tga panel + merchant-rank XP bar +
 *             calendar markers (MODULATE) + item_win number glyphs (gold via
 *             FUN_00406a60, quota via FUN_00406a60, level via FUN_00481ec3).
 *             Retail draws [4]=6q panel / [5]=1q today / [6]=3q period-end /
 *             [7]=quota / [8]=gold / [9]=level.
 *
 * The pre-backdrop fx layer [0] (the captured/blurred RT) is drawn by the
 * fade system before this function (M3, done).
 * M4 (this): the sub_anim>0 submenu dispatch (L83931-83952) — Save (type 3)
 *            → FUN_004812e4 → the save-slot card list. The other submenu
 *            renders (Status/Items/Options/Encyclopedia/dungeon) stay
 *            PORT-DEBT(pause-submenu-*).
 *
 * Geometry/diffuse recovered from objdump 0x4820ba-0x482400 (the decompile
 * dropped the register-built diffuse args + some FP consts; all verified
 * 1:1 against the .rdata float table). */

/* FUN_004812e4 — the pause Save submenu render wrapper. Computes the
 * save-phase pulse (clamp(c89c-30, 0, 30)), renders the shared card list, then
 * (during a commit, c89c>0) the save-progress bar over the selected card. */
static void pause_save_picker_render(IDirect3DDevice8 *d)
{
    int phase = g_pause_save_phase - 0x1e;
    if (phase < 0)    phase = 0;
    if (phase > 0x1e) phase = 0x1e;
    save_picker_render(d, 0.0f, g_pause_save_cursor, g_pause_save_scroll,
                       g_pause_save_vscroll, g_pause_save_hscroll, phase);

    /* ── the save-progress bar (engine FUN_004812e4 @ 0x481358, gated c89c>0).
     * Two item_win.tga quads over the selected card under COLOROP=ADDSIGNED:
     * an empty-bar frame + a fill quad whose width grows with c89c/30. The grey
     * passthrough pulses with the same sin the selected card uses; alpha fades
     * out past c89c>0x34. Geometry/consts from objdump 0x481358-0x481408 (the
     * decompile dropped the −128 sin amplitude + reordered the quad args). */
    if (g_pause_save_phase > 0) {
        const sprite_t *iw = &g_sysassets.item_win_tga;   /* DAT_073d8748 */
        const uint32_t iw_w = iw->width, iw_h = iw->height;
        const int c89c = g_pause_save_phase;

        float frac = (float)c89c / 30.0f;                 /* local_8 — fill % */
        if (frac > 1.0f) frac = 1.0f;

        int grey = 0x7f;                                  /* uVar2 */
        if (c89c > 0x1e)
            grey = 0x7f - (int)(sinf((float)(c89c - 0x1e) * 3.1415927f / 30.0f)
                                * -128.0f);
        int alpha = 0xff;                                 /* iVar1 */
        if (c89c > 0x34) alpha = c89c * -0x20 + 0x77f;
        const uint32_t diffuse =
            ((uint32_t)alpha << 24) | ((uint32_t)grey << 16)
            | ((uint32_t)grey << 8) | (uint32_t)grey;

        /* y tracks the selected row: (cursor − scroll)·140 + 96 (local_c). */
        const float by = (float)((g_pause_save_cursor - g_pause_save_scroll) * 0x8c)
                         + 96.0f;

        render_quad_bind(d, iw);                          /* SetTexture(0, item_win) */
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP,
                                              D3DTOP_ADDSIGNED);  /* (0,1,8) */
        {   /* empty-bar frame: dst(180,by,280,40) src(704,896,984,936). */
            const float dst[4] = { 180.0f, by, 280.0f, 40.0f };
            const float src[4] = { 704.0f, 896.0f, 984.0f, 936.0f };
            render_quad_add(dst, src, iw_w, iw_h, diffuse);
        }
        {   /* fill: width = frac·280; dst(180,by,W,40) src(704,936,704+W,976). */
            const float w = frac * 280.0f;
            const float dst[4] = { 180.0f, by, w, 40.0f };
            const float src[4] = { 704.0f, 936.0f, 704.0f + w, 976.0f };
            render_quad_add(dst, src, iw_w, iw_h, diffuse);
        }
        render_quad_flush(d);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP,
                                              D3DTOP_MODULATE);  /* (0,1,4) */
    }
}

void pause_menu_render(struct IDirect3DDevice8 *dev)
{
    IDirect3DDevice8 *d = (IDirect3DDevice8 *)dev;

    render_quad_state_setup(d);   /* FUN_0049b425 (engine L83706) */

    /* Backdrop alpha (engine L83708-83713): full unless the selected entry
     * is Status (type 0) AND the submenu is opening, where it fades by
     * sub_anim*0x1a. At rest (sub_anim==0) it is always 0xff. */
    int bg_alpha = 0xff;
    if (g_pause_entries[g_pause_sel] == 0) {
        int a = g_pause_sub_anim * 0x1a;
        if (a > 0xff) a = 0xff;
        bg_alpha = 0xff - a;
    }

    /* pause_bg_rete full-screen (engine L83715-83724): dst {0,0,640,480},
     * src {0,0,640,480}, diffuse = alpha<<24 | 0xffffff. */
    render_quad_bind(d, &g_scene_pause_bg_rete);   /* SetTexture(0, bg_rete) */
    {
        const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        render_quad_add(dst, src,
                        g_scene_pause_bg_rete.width, g_scene_pause_bg_rete.height,
                        ((uint32_t)bg_alpha << 24) | 0xffffffu);
    }
    render_quad_flush(d);          /* FUN_00405354 (engine L83725) */

    /* ── option list (engine L83726-83789): icon+label rows from pause.tga
     * under COLOROP=ADD. Each row r (type t = g_pause_entries[r]) draws a
     * highlight icon then its text label. The selected row uses the larger
     * icon and may flash (sel_anim) / slide toward the submenu header
     * (sub_anim); unselected rows fade out by alpha as the submenu opens. */
    render_quad_bind(d, &g_scene_pause_pause);     /* SetTexture(0, pause.tga) L83726 */
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP,
                                          D3DTOP_ADD);    /* L83727 (1,7) */
    if (g_pause_count != 0) {
        const uint32_t pw = g_scene_pause_pause.width;
        const uint32_t ph = g_scene_pause_pause.height;
        for (int r = 0; r < g_pause_count; r++) {
            const int t = g_pause_entries[r];
            float x = (float)((r & 1) * 0x50 + 0x1b0);      /* 432 / 512   */
            float y = (float)(g_pause_row_spacing * r + 0x20);
            const int is_sel = (r == g_pause_sel);
            int flash = 0;                                  /* select-flash grey */
            int alpha;                                      /* diffuse alpha basis */
            float isrc[4], idst[4];

            if (is_sel) {
                if (g_pause_sel_anim > 0)                   /* L83752-83757 */
                    flash = (int)(sinf((float)g_pause_sel_anim * 3.1415927f
                                       / 15.0f) * 64.0f);
                if (g_pause_sub_anim > 0) {                 /* L83758-83762: lerp */
                    const float k = (float)g_pause_sub_anim;
                    x = ((32.0f - x) * k) / 10.0f + x;
                    y = ((-8.0f - y) * k) / 10.0f + y;
                }
                isrc[0] = 720.0f; isrc[1] = 240.0f;         /* 160×128 icon */
                isrc[2] = 880.0f; isrc[3] = 368.0f;
                idst[0] = x - 32.0f; idst[1] = y - 16.0f;
                idst[2] = 160.0f;    idst[3] = 128.0f;
                alpha = 0xff;
            } else {
                alpha = 0xff - g_pause_sub_anim * 0x20;     /* L83740 gate   */
                if (alpha < 0)                              /* row culled    */
                    continue;
                isrc[0] = 720.0f; isrc[1] = 144.0f;         /* 96×96 icon    */
                isrc[2] = 816.0f; isrc[3] = 240.0f;
                idst[0] = x;      idst[1] = y;
                idst[2] = 96.0f;  idst[3] = 96.0f;
            }
            /* icon diffuse: alpha in the high byte, the flash grey in RGB. */
            render_quad_add(idst, isrc, pw, ph,
                            ((uint32_t)alpha << 24)
                            | ((uint32_t)flash << 16)
                            | ((uint32_t)flash << 8)
                            | (uint32_t)flash);

            /* row text label (engine L83773-83782): src column selected by
             * is_sel, src row by entry type t; dst x-32, y+29, 160×39. */
            {
                const float lsrc[4] = {
                    (float)(is_sel * 0xa0 + 0x180),   /* left  384 / 544 */
                    (float)(t * 0x28 + 1),            /* top   t*40 + 1  */
                    (float)(is_sel * 0xa0 + 0x220),   /* right 544 / 704 */
                    (float)((t * 5 + 5) * 8)          /* bottom t*40 + 40 */
                };
                const float ldst[4] = { x - 32.0f, y + 29.0f, 160.0f, 39.0f };
                render_quad_add(ldst, lsrc, pw, ph,
                                ((uint32_t)alpha << 24)
                                | ((uint32_t)flash << 16)
                                | ((uint32_t)flash << 8)
                                | (uint32_t)flash);
            }
        }
    }
    render_quad_flush(d);                              /* L83789 (always)   */

    /* ── "PAUSE MENU" header (engine L83790-83800): one pause.tga quad under
     * COLOROP=MODULATE, src(64,384)-(320,424) → dst(368,428,256,40), white. */
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP,
                                          D3DTOP_MODULATE);  /* L83790 (1,4) */
    {
        const float src[4] = { 64.0f, 384.0f, 320.0f, 424.0f };
        const float dst[4] = { 368.0f, 428.0f, 256.0f, 40.0f };
        render_quad_add(dst, src,
                        g_scene_pause_pause.width, g_scene_pause_pause.height,
                        0xffffffffu);
    }
    render_quad_flush(d);                              /* L83800 */

    /* ── calendar / merchant-rank / numbers (engine L83801-83930) ──────────
     * Still pause.tga, still COLOROP=MODULATE. The whole block slides with
     * ox = -64*sub_anim (0 at rest) and is gated sub_anim<10. Retail draws
     * [4]=6q panel / [5]=1q today / [6]=3q period-end, then the item_win
     * number glyphs [7]=quota / [8]=gold / [9]=level. */
    if (g_pause_sub_anim < 10) {
        const float ox = (float)(int)(g_pause_sub_anim * -0x40);   /* local_8 */
        const uint32_t pw = g_scene_pause_pause.width;
        const uint32_t ph = g_scene_pause_pause.height;
        uint32_t *bank = save_work_dwords_at(save_work_active_slot());

        /* ── [4]: 6 quads (engine L83801-83866) ────────────────────────── */
        /* A — top-left icon: src(720,0)-(768,48) → dst(ox+32,8,48,48). */
        { const float dst[4]={ox+32.0f,8.0f,48.0f,48.0f},
                      src[4]={720.0f,0.0f,768.0f,48.0f};
          render_quad_add(dst,src,pw,ph,0xffffffffu); }
        /* B — label under it: src(720,368)-(864,416) → dst(ox+32,60,144,48). */
        { const float dst[4]={ox+32.0f,60.0f,144.0f,48.0f},
                      src[4]={720.0f,368.0f,864.0f,416.0f};
          render_quad_add(dst,src,pw,ph,0xffffffffu); }
        /* C — XP-bar track bg: src(64,432)-(264,472) → dst(ox+176,64,200,40). */
        { const float dst[4]={ox+176.0f,64.0f,200.0f,40.0f},
                      src[4]={64.0f,432.0f,264.0f,472.0f};
          render_quad_add(dst,src,pw,ph,0xffffffffu); }
        /* D — XP-bar fill (engine L83833-83847): width = (xp_cur-xp_start) /
         * (xp_end-xp_start) * 142, src(310,432)-(452,472) → dst(ox+214,64,
         * fill,40). xp_cur = _DAT_0438b91c which, with no XP animating, equals
         * the value the stage load snapped it to (bank[+0x2c3f4]); see banner. */
        if (bank) {
            float cur   = (float)(int32_t)bank[0xb0fd];                  /* +0x2c3f4 */
            float start = (float)(int32_t)bank[0xb0fe];                  /* +0x2c3f8 */
            float span  = (float)(int32_t)(bank[0xb0ff] - bank[0xb0fe]); /* +0x2c3fc */
            if (span < 1.0f) span = 1.0f;
            float fill = ((cur - start) / span) * 142.0f;
            const float dst[4]={ox+214.0f,64.0f,fill,40.0f},
                        src[4]={310.0f,432.0f,452.0f,472.0f};
            render_quad_add(dst,src,pw,ph,0xffffffffu);
        }
        /* E — XP-bar frame (over the fill): src(480,432)-(680,472) →
         * dst(ox+176,64,200,40). */
        { const float dst[4]={ox+176.0f,64.0f,200.0f,40.0f},
                      src[4]={480.0f,432.0f,680.0f,472.0f};
          render_quad_add(dst,src,pw,ph,0xffffffffu); }
        /* F — the 380×380 calendar board: src(0,0)-(380,380) → dst(ox,100,380,380). */
        { const float dst[4]={ox,100.0f,380.0f,380.0f},
                      src[4]={0.0f,0.0f,380.0f,380.0f};
          render_quad_add(dst,src,pw,ph,0xffffffffu); }
        render_quad_flush(d);                              /* L83866 */

        /* ── [5]/[6]: calendar markers (engine L83867-83923) ───────────── */
        int period_end = pause_period_end();
        if (period_end != 0 && bank) {
            /* [5] today marker: cell ((day+4)%7, (day+4)/7), 45px cols /
             * 48px rows, origin (24,132); x slides with ox, y does not.
             * src(0,384)-(64,448), 64×64. */
            int day = pause_day_index(bank);
            float tx = (float)(((day + 4) % 7) * 0x2d + 0x18) + ox;
            float ty = (float)(((day + 4) / 7) * 0x30 + 0x84);
            { const float dst[4]={tx,ty,64.0f,64.0f},
                          src[4]={0.0f,384.0f,64.0f,448.0f};
              render_quad_add(dst,src,pw,ph,0xffffffffu); }
            render_quad_flush(d);                          /* L83882 */

            int mode = (int32_t)bank[0xb759];              /* +0x2dd64 */
            if (mode != 3) {
                /* [6] period-end (payment-due) marker + two badge sprites
                 * over it (engine L83883-83923). */
                int pe_wk = period_end / 7;
                int col;
                if (mode == 2) { pe_wk += 1; col = 3; }
                else             col = 4;
                float px = (float)(col * 0x2d + 0x18) + ox;
                float py = (float)(pe_wk * 0x30 + 0x84) + ox; /* engine adds local_8 to y too (0 at rest) */
                /* H — period-end marker: src(0,448)-(64,512), 64×64. */
                { const float dst[4]={px,py,64.0f,64.0f},
                              src[4]={0.0f,448.0f,64.0f,512.0f};
                  render_quad_add(dst,src,pw,ph,0xffffffffu); }
                /* I — badge over it: src(720,48)-(880,136) → dst(px+32,py+32,160,88). */
                { const float dst[4]={px+32.0f,py+32.0f,160.0f,88.0f},
                              src[4]={720.0f,48.0f,880.0f,136.0f};
                  render_quad_add(dst,src,pw,ph,0xffffffffu); }
                /* J — sub-badge: src(864,368)-(992,424) → dst(px+56,py+56,128,56). */
                { const float dst[4]={px+56.0f,py+56.0f,128.0f,56.0f},
                              src[4]={864.0f,368.0f,992.0f,424.0f};
                  render_quad_add(dst,src,pw,ph,0xffffffffu); }
                render_quad_flush(d);                      /* L83923 */

                /* [7] quota number near the badge (icon=0, comma=1):
                 * FUN_00406a60(px+168-24, py+60, quota). */
                scene1_top_hud_draw_number(d, (px + 168.0f) - 24.0f, py + 60.0f,
                                           pause_weekly_quota(), 0,
                                           0xffffffffu, 1);
            }
        }

        /* [8] gold (icon=1 "pix", comma=1): FUN_00406a60(ox+256, 28, gold). */
        if (bank)
            scene1_top_hud_draw_number(d, ox + 256.0f, 28.0f,
                                       (int32_t)bank[3], 1, 0xffffffffu, 1);
        /* [9] merchant level badge (drawn as level+1): FUN_00481ec3(ox+192,64). */
        if (bank)
            scene1_merchant_hud_draw_level(d, ox + 192.0f, 64.0f,
                                           (int32_t)bank[0xb100], 0xffffffffu);
    }

    /* ── sub_anim>0 submenu dispatch (engine L83931-83952): the open submenu
     * renders over the (fading) option list. Save (type 3) + Encyclopedia
     * (type 6) are ported; Status (0)/Options (2)/dungeon (5)/Items (1) renders
     * stay PORT-DEBT(pause-submenu-*). ── */
    if (g_pause_sub_anim > 0) {
        const int t = g_pause_entries[g_pause_sel];
        if (t == 3) {
            pause_save_picker_render(d);     /* FUN_004812e4 */
        } else if (t == 6) {
            /* engine L83937: FUN_0049f8b8(640 - sub_anim*64, 0) — slides in
             * from the right, rests at (0,0). */
            encyclopedia_render(d, 640.0f - (float)(g_pause_sub_anim << 6), 0.0f);
        }
    }

    /* ── shared overlay tail (engine L83953-83955): the choice box, the hand
     * cursor, then the save/load dialog frame. All self-gate (no active box /
     * cursor hidden / no save dialog ⇒ no-ops in the resting pause), called
     * unconditionally to match the engine's draw program. */
    choice_box_draw(d);                  /* FUN_0043537e */
    title_save_dialog_cursor_render(d);  /* FUN_00435747 */
    title_save_dialog_render();          /* FUN_00435117 */
}

void scene_pause_reset(void)
{
    /* Win32: zero the sprite_t handles (matches the wall/floor/jutan
     * pattern; lost-device handling is deferred). */
    g_scene_pause_pause.tex = 0;
    g_scene_pause_pause.width = 0;
    g_scene_pause_pause.height = 0;
    g_scene_pause_bg_rete.tex = 0;
    g_scene_pause_bg_rete.width = 0;
    g_scene_pause_bg_rete.height = 0;
    g_scene_pause_result_bord01.tex = 0;
    g_scene_pause_result_bord01.width = 0;
    g_scene_pause_result_bord01.height = 0;
    g_scene_pause_dungeonbord.tex = 0;
    g_scene_pause_dungeonbord.width = 0;
    g_scene_pause_dungeonbord.height = 0;
    for (int i = 0; i < SCENE_PAUSE_SOUSA_COUNT; i++) {
        g_scene_pause_sousa[i].tex = 0;
        g_scene_pause_sousa[i].width = 0;
        g_scene_pause_sousa[i].height = 0;
    }
    for (int i = 0; i < SCENE_PAUSE_STATUS_COUNT; i++) {
        g_scene_pause_status[i].tex = 0;
        g_scene_pause_status[i].width = 0;
        g_scene_pause_status[i].height = 0;
    }
    scene_pause_state_clear();
}

#else  /* !_WIN32 — Linux test build */

void scene_pause_reset(void)
{
    scene_pause_state_clear();
}

#endif /* _WIN32 */
