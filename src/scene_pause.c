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
        /* UNPAUSE (L50252): only once fully open (ramp past 0xb). */
        if (sim_get_counter_998() > 0xb) {
            g_scene_state = g_pause_saved_mode;
            sim_set_mode_9a0(0);     /* direction = closing */
            /* PORT-DEBT(pause-unpause-restore): the engine resumes the
             * underlying scene + restores the cursor snapshot here. We do
             * the mode flip + re-spawn the restored scene's loader (the
             * "Now Loading" out) and defer the resume teardown. */
            worker_load_spawn();
        }
    } else if (sim_get_counter_998() == 0) {
        /* ENTER PAUSE (L50292). */
        sim_set_counter_994(0, sim_get_threshold94());  /* DAT_06a49994 = 0 */
        if (action == 0)
            audio_play_se_by_id(0x16b);                 /* pause-open chime */
        g_pause_saved_mode = g_scene_state;             /* DAT_06a499a8 */
        sim_set_counter_998(1);                         /* ramp = 1     */
        sim_set_counter_99c(1);                         /* slide ramp = 1 */
        sim_set_mode_9a0(1);                            /* dir = opening */
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
        /* Select anim running. PORT-DEBT(pause-submenu-*): at sel_anim==0xf
         * the engine commits the selection — opens a submenu (types
         * 0/1/2/3/5/6) or starts the type-4 exit confirm. Deferred; we tick
         * the anim so the press is acknowledged but the menu stays put. */
        g_pause_sel_anim++;
    }
}

/* FUN_0047fa76 — the mode-9 per-frame update. */
void pause_menu_update(void)
{
    /* FUN_0048b6ad() (portrait/clock per-frame) — PORT-DEBT. */
    if (g_pause_exit_confirm < 1) {
        if (g_pause_sub_anim < 1) {
            pause_menu_nav();
        } else {
            /* Submenu open/close anim (L82019-82030). The L82031
             * sub_anim==10 dispatch to the per-type submenu updater is
             * PORT-DEBT(pause-submenu-*). */
            if (g_pause_sub_dir == 0) {
                g_pause_sub_anim--;
                if (g_pause_sub_anim < 1) g_pause_sub_anim = 0;
            } else {
                g_pause_sub_anim++;
                if (g_pause_sub_anim > 10) g_pause_sub_anim = 10;
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
 * fade system before this function (M3, done). The sub_anim>0 submenu
 * dispatch (L83931-83952) is M3+ PORT-DEBT.
 *
 * Geometry/diffuse recovered from objdump 0x4820ba-0x482400 (the decompile
 * dropped the register-built diffuse args + some FP consts; all verified
 * 1:1 against the .rdata float table). */
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
