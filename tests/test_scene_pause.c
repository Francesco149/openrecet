/*
 * test_scene_pause.c — pure-C tests for the C4E secondary inner-body
 * (engine unnamed FUN @ 0x435873 pause-state FPU init + FUN_00473a3e
 * pause+adventurer-status asset loader).
 *
 * Two surfaces:
 *   - scene_pause_state_init() writes 10 named globals with exact
 *     constants extracted from .rdata @ 0x519440 / 0x519474.
 *   - scene_pause_load_with() dispatches all 20 fixed asset slots,
 *     with slot 0 toggling between pause.tga and pause_endless.tga via
 *     the per-stage selector.
 */
#include "t.h"

#include <string.h>

#include "scene_pause.h"
#include "worker_load.h"
#include "sim.h"      /* ramp counters + g_sim_buttons[0] for the SM tests */
#include "scene.h"    /* g_scene_state */
#include "save_picker.h" /* perm init + globals (the type-3 commit) */
#include "audio_fade.h"  /* Music/Sound/Voice sliders (the Options L/R nav) */
#include "settings.h"    /* Message Speed / Unread Text Skip sliders */
#include "save_io.h"     /* save_io_set_write_dir — sandbox the exit-save write */
#include "save_bank.h"   /* save_header_set_last_slot (the picker cursor seed) */
#include "save_work.h"   /* working-bank setup (M4c commit source) */
#include "choice_box.h"  /* the overwrite dialog (M4c A-confirm on an occupied slot) */
#include "stage_load_pulse.h" /* unpause teardown clears the stage-load-pulse flag */
#include "nowloading.h"  /* the unpause must NOT raise "Now Loading" (no reload) */

/* The SHARED hand-cursor snapshot/restore the pause open/close drives
 * (title_save_dialog.h is render-heavy; forward-declare the pure-C entry points). */
void title_save_dialog_cursor_set_visible(int on);
int  title_save_dialog_cursor_get_visible(void);
void title_save_dialog_cursor_snap(float x, float y);
void title_save_dialog_cursor_capture_target(float *x, float *y);

/* ─── recording scratchpad for the injected load_fn ──────────────────── */

#define MAX_RECORDED 32
static struct {
    int   n;
    char  path[MAX_RECORDED][64];
    int   slot[MAX_RECORDED];
    int   w[MAX_RECORDED];
    int   h[MAX_RECORDED];
    void *userdata[MAX_RECORDED];
} g_rec;

static void reset_recorded(void)
{
    memset(&g_rec, 0, sizeof(g_rec));
}

static int recording_load_fn(const char *path, int slot, int w, int h,
                              void *userdata)
{
    if (g_rec.n < MAX_RECORDED) {
        size_t n = strlen(path);
        if (n >= sizeof(g_rec.path[0])) n = sizeof(g_rec.path[0]) - 1;
        memcpy(g_rec.path[g_rec.n], path, n);
        g_rec.path[g_rec.n][n] = '\0';
        g_rec.slot[g_rec.n]     = slot;
        g_rec.w[g_rec.n]        = w;
        g_rec.h[g_rec.n]        = h;
        g_rec.userdata[g_rec.n] = userdata;
        g_rec.n++;
    }
    return 1;
}

/* ─── filename table tests ───────────────────────────────────────────── */

int test_scene_pause_load_count_is_twenty(void)
{
    /* 4 singletons + 8 sousa + 8 status = 20. */
    T_ASSERT_EQ_I(SCENE_PAUSE_LOAD_COUNT, 20);
    T_ASSERT_EQ_I(SCENE_PAUSE_SOUSA_COUNT, 8);
    T_ASSERT_EQ_I(SCENE_PAUSE_STATUS_COUNT, 8);
    return 0;
}

int test_scene_pause_slot0_default_is_pause_tga(void)
{
    /* Selector 0 (BSS-zero / fresh-boot) → engine takes the else branch:
     * "bmp/pause.tga". */
    scene_pause_reset();
    T_ASSERT(strcmp(scene_pause_filename(0), "bmp/pause.tga") == 0);
    return 0;
}

int test_scene_pause_slot0_selector_2_is_endless(void)
{
    scene_pause_reset();
    g_scene_pause_selector = 2;
    T_ASSERT(strcmp(scene_pause_filename(0), "bmp/pause_endless.tga") == 0);
    return 0;
}

int test_scene_pause_slot0_selector_3_is_endless(void)
{
    scene_pause_reset();
    g_scene_pause_selector = 3;
    T_ASSERT(strcmp(scene_pause_filename(0), "bmp/pause_endless.tga") == 0);
    return 0;
}

int test_scene_pause_slot0_selector_other_values_are_pause(void)
{
    /* Spot-check the cases adjacent to the endless range. */
    scene_pause_reset();
    int probes[] = {-1, 0, 1, 4, 5, 99};
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        g_scene_pause_selector = probes[i];
        const char *got = scene_pause_filename(0);
        if (strcmp(got, "bmp/pause.tga") != 0) {
            T_FAIL("selector=%d: got %s, want bmp/pause.tga",
                   probes[i], got);
        }
    }
    return 0;
}

int test_scene_pause_filename_table_full_order(void)
{
    static const char *const expected[SCENE_PAUSE_LOAD_COUNT] = {
        "bmp/pause.tga",          /* slot 0 (selector=0) */
        "bmp/pause_bg_rete.tga",
        "bmp/result_bord01.tga",
        "bmp/dungeonbord.tga",
        "bmp/sousa_lui.tga",  "bmp/sousa_sya.tga",
        "bmp/sousa_cai.tga",  "bmp/sousa_tel.tga",
        "bmp/sousa_era.tga",  "bmp/sousa_nag.tga",
        "bmp/sousa_grf.tga",  "bmp/sousa_arm.tga",
        "bmp/st_ryui.tga",    "bmp/st_sya.tga",
        "bmp/st_caillou.tga", "bmp/st_tiers.tga",
        "bmp/st_eran.tga",    "bmp/st_nagi.tga",
        "bmp/st_griffe.tga",  "bmp/st_aruma.tga",
    };
    scene_pause_reset();
    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        const char *got = scene_pause_filename(i);
        if (strcmp(got, expected[i]) != 0) {
            T_FAIL("slot %d: got %s, want %s", i, got, expected[i]);
        }
    }
    return 0;
}

int test_scene_pause_filename_out_of_range_is_null(void)
{
    T_ASSERT(scene_pause_filename(-1) == 0);
    T_ASSERT(scene_pause_filename(SCENE_PAUSE_LOAD_COUNT) == 0);
    T_ASSERT(scene_pause_filename(9999) == 0);
    return 0;
}

/* ─── dims tests ─────────────────────────────────────────────────────── */

int test_scene_pause_slot_dims_pause_and_singletons(void)
{
    /* Slot 0 (pause)              : 0x400 x 0x200
     * Slot 1 (pause_bg_rete)      : 0x400 x 0x200
     * Slot 2 (result_bord01)      : 0x200 x 0x100  ← only outlier in the
     *                                                 4 singletons.
     * Slot 3 (dungeonbord)        : 0x400 x 0x200
     */
    int w = -1, h = -1;
    T_ASSERT(scene_pause_slot_dims(0, &w, &h) == 1);
    T_ASSERT_EQ_I(w, 0x400); T_ASSERT_EQ_I(h, 0x200);
    T_ASSERT(scene_pause_slot_dims(1, &w, &h) == 1);
    T_ASSERT_EQ_I(w, 0x400); T_ASSERT_EQ_I(h, 0x200);
    T_ASSERT(scene_pause_slot_dims(2, &w, &h) == 1);
    T_ASSERT_EQ_I(w, 0x200); T_ASSERT_EQ_I(h, 0x100);
    T_ASSERT(scene_pause_slot_dims(3, &w, &h) == 1);
    T_ASSERT_EQ_I(w, 0x400); T_ASSERT_EQ_I(h, 0x200);
    return 0;
}

int test_scene_pause_slot_dims_sousa_all_400x200(void)
{
    /* Slots 4..11 are the sousa cursor portraits, all 0x400 x 0x200. */
    for (int slot = 4; slot <= 11; slot++) {
        int w = -1, h = -1;
        T_ASSERT(scene_pause_slot_dims(slot, &w, &h) == 1);
        T_ASSERT_EQ_I(w, 0x400);
        T_ASSERT_EQ_I(h, 0x200);
    }
    return 0;
}

int test_scene_pause_slot_dims_status_all_200x200(void)
{
    /* Slots 12..19 are the st_* status portraits, all 0x200 x 0x200. */
    for (int slot = 12; slot <= 19; slot++) {
        int w = -1, h = -1;
        T_ASSERT(scene_pause_slot_dims(slot, &w, &h) == 1);
        T_ASSERT_EQ_I(w, 0x200);
        T_ASSERT_EQ_I(h, 0x200);
    }
    return 0;
}

int test_scene_pause_slot_dims_out_of_range_returns_zero(void)
{
    int w = 999, h = 999;
    T_ASSERT(scene_pause_slot_dims(-1, &w, &h) == 0);
    T_ASSERT(scene_pause_slot_dims(SCENE_PAUSE_LOAD_COUNT, &w, &h) == 0);
    /* Output pointers must not be touched on failure. */
    T_ASSERT_EQ_I(w, 999);
    T_ASSERT_EQ_I(h, 999);
    return 0;
}

/* ─── load_with dispatch tests ───────────────────────────────────────── */

int test_scene_pause_load_dispatches_all_twenty(void)
{
    scene_pause_reset();
    reset_recorded();

    int loads = scene_pause_load_with(recording_load_fn, (void *)0xc0ffee);

    T_ASSERT_EQ_I(loads, SCENE_PAUSE_LOAD_COUNT);
    T_ASSERT_EQ_I(g_rec.n, SCENE_PAUSE_LOAD_COUNT);

    /* Slots must be in 0..19 order — engine FUN_00473a3e is a straight
     * call sequence with no permutation. */
    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        T_ASSERT_EQ_I(g_rec.slot[i], i);
        T_ASSERT(g_rec.userdata[i] == (void *)0xc0ffee);
    }
    return 0;
}

int test_scene_pause_load_paths_match_filename_table(void)
{
    scene_pause_reset();
    reset_recorded();

    g_scene_pause_selector = 0;  /* slot 0 = "bmp/pause.tga" */
    int loads = scene_pause_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, SCENE_PAUSE_LOAD_COUNT);

    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        const char *want = scene_pause_filename(i);
        if (strcmp(g_rec.path[i], want) != 0) {
            T_FAIL("slot %d: dispatched %s, table %s",
                   i, g_rec.path[i], want);
        }
    }
    return 0;
}

int test_scene_pause_load_dims_match_metadata(void)
{
    scene_pause_reset();
    reset_recorded();

    scene_pause_load_with(recording_load_fn, 0);

    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        int want_w = 0, want_h = 0;
        scene_pause_slot_dims(i, &want_w, &want_h);
        if (g_rec.w[i] != want_w || g_rec.h[i] != want_h) {
            T_FAIL("slot %d: dispatched %dx%d, table %dx%d",
                   i, g_rec.w[i], g_rec.h[i], want_w, want_h);
        }
    }
    return 0;
}

int test_scene_pause_load_selector_endless_swaps_slot_zero(void)
{
    scene_pause_reset();
    reset_recorded();

    g_scene_pause_selector = 3;  /* endless variant */
    scene_pause_load_with(recording_load_fn, 0);

    T_ASSERT(strcmp(g_rec.path[0], "bmp/pause_endless.tga") == 0);
    /* Slots 1..19 must not be affected. */
    T_ASSERT(strcmp(g_rec.path[1], "bmp/pause_bg_rete.tga") == 0);
    T_ASSERT(strcmp(g_rec.path[19], "bmp/st_aruma.tga") == 0);
    return 0;
}

int test_scene_pause_load_without_load_fn_returns_count_only(void)
{
    scene_pause_reset();
    int loads = scene_pause_load_with(0, 0);
    T_ASSERT_EQ_I(loads, SCENE_PAUSE_LOAD_COUNT);
    return 0;
}

/* ─── FPU init tests (engine unnamed @ 0x435873) ─────────────────────── */

int test_scene_pause_state_init_writes_constants(void)
{
    /* Constants extracted from .rdata:
     *   0x00519474 → 32.0f
     *   0x00519440 → 80.0f
     */
    scene_pause_reset();
    /* Pre-state: all zero (BSS / reset). */
    T_ASSERT_EQ_I(g_scene_pause_state_b150, 0);
    T_ASSERT(g_scene_pause_state_ac00 == 0.0f);

    scene_pause_state_init();

    T_ASSERT_EQ_I(g_scene_pause_state_b150, 1);
    T_ASSERT_EQ_I(g_scene_pause_state_b158, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_b15c, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac18, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac1c, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac20, 0);
    T_ASSERT(g_scene_pause_state_ac00 == 32.0f);
    T_ASSERT(g_scene_pause_state_abf4 == 32.0f);
    T_ASSERT(g_scene_pause_state_ac04 == 80.0f);
    T_ASSERT(g_scene_pause_state_abf8 == 80.0f);
    return 0;
}

int test_scene_pause_state_init_is_idempotent(void)
{
    /* Engine asm is straight-line writes — re-running must produce the
     * same end state without disturbing anything. */
    scene_pause_reset();
    scene_pause_state_init();
    scene_pause_state_init();
    T_ASSERT_EQ_I(g_scene_pause_state_b150, 1);
    T_ASSERT(g_scene_pause_state_ac00 == 32.0f);
    T_ASSERT(g_scene_pause_state_abf8 == 80.0f);
    return 0;
}

int test_scene_pause_state_init_overrides_dirty_state(void)
{
    /* Stamp arbitrary non-zero / non-32 values, then re-init — every
     * field must be reset to the engine constant. */
    g_scene_pause_state_b150 = 0xdead;
    g_scene_pause_state_b158 = 0xbeef;
    g_scene_pause_state_b15c = 0xcafe;
    g_scene_pause_state_ac18 = 0xfade;
    g_scene_pause_state_ac1c = 0xfeed;
    g_scene_pause_state_ac20 = 0xface;
    g_scene_pause_state_abf4 = -1.0f;
    g_scene_pause_state_abf8 = -2.0f;
    g_scene_pause_state_ac00 = -3.0f;
    g_scene_pause_state_ac04 = -4.0f;

    scene_pause_state_init();

    T_ASSERT_EQ_I(g_scene_pause_state_b150, 1);
    T_ASSERT_EQ_I(g_scene_pause_state_b158, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_b15c, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac18, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac1c, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac20, 0);
    T_ASSERT(g_scene_pause_state_ac00 == 32.0f);
    T_ASSERT(g_scene_pause_state_abf4 == 32.0f);
    T_ASSERT(g_scene_pause_state_ac04 == 80.0f);
    T_ASSERT(g_scene_pause_state_abf8 == 80.0f);
    return 0;
}

/* ─── reset + cross-sibling independence ─────────────────────────────── */

int test_scene_pause_reset_zeroes_state(void)
{
    g_scene_pause_selector = 2;
    scene_pause_state_init();
    scene_pause_reset();
    T_ASSERT_EQ_I(g_scene_pause_selector, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_b150, 0);
    T_ASSERT(g_scene_pause_state_ac00 == 0.0f);
    return 0;
}

int test_scene_pause_selector_independent_from_siblings(void)
{
    /* Wall/floor/jutan/pause selectors are independent globals even
     * though the engine stores them at distinct offsets inside the same
     * 0x2dfc8-byte stage-state record. Make sure none of the 4 share
     * storage. */
    extern int32_t g_scene_walls_selector;
    extern int32_t g_scene_floor_selector;
    extern int32_t g_scene_jutan_selector;

    scene_pause_reset();
    g_scene_walls_selector = 1;
    g_scene_floor_selector = 2;
    g_scene_jutan_selector = 4;
    g_scene_pause_selector = 3;

    T_ASSERT_EQ_I(g_scene_walls_selector, 1);
    T_ASSERT_EQ_I(g_scene_floor_selector, 2);
    T_ASSERT_EQ_I(g_scene_jutan_selector, 4);
    T_ASSERT_EQ_I(g_scene_pause_selector, 3);

    g_scene_walls_selector = 0;
    g_scene_floor_selector = 0;
    g_scene_jutan_selector = 0;
    T_ASSERT_EQ_I(g_scene_pause_selector, 3);
    return 0;
}

/* ─── state machine: menu build (FUN_0047f2f6) ───────────────────────── */

/* Shared setup: clean SM + sim + worker state, in-game at `mode`. */
static void sm_prep(int mode, int status_count, int stage_type)
{
    sim_init();
    worker_load_reset();
    pause_sm_reset();
    g_scene_state = mode;
    g_pause_saved_mode = mode;
    pause_set_menu_inputs(status_count, stage_type);
}

int test_pause_menu_setup_house_list(void)
{
    /* House: no party (status 0), stage type 0 → [1,6,2,3,4] = "Items ·
     * Encyclopedia · Options · Save · Exit Game". */
    sm_prep(1, /*status*/0, /*stage*/0);
    pause_menu_setup();
    T_ASSERT_EQ_I(g_pause_count, 5);
    int want[] = {1, 6, 2, 3, 4};
    for (int i = 0; i < 5; i++) T_ASSERT_EQ_I(g_pause_entries[i], want[i]);
    /* row pitch = (0xb - count) * 0xc */
    T_ASSERT_EQ_I(g_pause_row_spacing, (0xb - 5) * 0xc);
    /* cursor + anim reset */
    T_ASSERT_EQ_I(g_pause_sel, 0);
    T_ASSERT_EQ_I(g_pause_sel_anim, 0);
    T_ASSERT_EQ_I(g_pause_sub_anim, 0);
    return 0;
}

int test_pause_menu_setup_status_entry(void)
{
    /* Party present (DAT_0741bed8 > 0) → type-0 Status leads. */
    sm_prep(1, /*status*/2, /*stage*/0);
    pause_menu_setup();
    T_ASSERT_EQ_I(g_pause_count, 6);
    int want[] = {0, 1, 6, 2, 3, 4};
    for (int i = 0; i < 6; i++) T_ASSERT_EQ_I(g_pause_entries[i], want[i]);
    return 0;
}

int test_pause_menu_setup_dungeon_entry(void)
{
    /* In a dungeon (saved_mode 1 && stage type > 0) → type-5 after Items. */
    sm_prep(1, /*status*/0, /*stage*/1);
    pause_menu_setup();
    T_ASSERT_EQ_I(g_pause_count, 6);
    int want[] = {1, 5, 6, 2, 3, 4};
    for (int i = 0; i < 6; i++) T_ASSERT_EQ_I(g_pause_entries[i], want[i]);
    return 0;
}

int test_pause_menu_setup_status_and_dungeon(void)
{
    sm_prep(1, /*status*/1, /*stage*/3);
    pause_menu_setup();
    T_ASSERT_EQ_I(g_pause_count, 7);
    int want[] = {0, 1, 5, 6, 2, 3, 4};
    for (int i = 0; i < 7; i++) T_ASSERT_EQ_I(g_pause_entries[i], want[i]);
    return 0;
}

int test_pause_menu_setup_dungeon_entry_needs_mode1(void)
{
    /* The type-5 gate also requires saved_mode==1; from the guild (mode 6)
     * even a >0 stage type must NOT add type 5. */
    sm_prep(6, /*status*/0, /*stage*/2);
    pause_menu_setup();
    T_ASSERT_EQ_I(g_pause_count, 5);
    int want[] = {1, 6, 2, 3, 4};
    for (int i = 0; i < 5; i++) T_ASSERT_EQ_I(g_pause_entries[i], want[i]);
    return 0;
}

/* ─── state machine: trigger (FUN_00453384) ──────────────────────────── */

int test_pause_dispatch_enter_starts_ramp(void)
{
    sm_prep(1, 0, 0);
    pause_dispatch(0);
    T_ASSERT_EQ_I(sim_get_counter_998(), 1);   /* ramp armed */
    T_ASSERT_EQ_I(sim_get_counter_99c(), 1);   /* slide ramp armed */
    T_ASSERT_EQ_I(sim_get_mode_9a0(), 1);      /* direction = opening */
    T_ASSERT_EQ_I(g_pause_saved_mode, 1);      /* mode saved */
    T_ASSERT_EQ_I(g_pause_action, 0);
    /* mode does NOT flip yet — that happens at ramp==3 in the integration
     * layer, not in the dispatch. */
    T_ASSERT_EQ_I(g_scene_state, 1);
    return 0;
}

int test_pause_dispatch_rejects_unpausable_mode(void)
{
    /* Modes 2/3/7/10 are not pausable — no ramp. */
    int modes[] = {2, 3, 7, 10};
    for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); i++) {
        sm_prep(modes[i], 0, 0);
        pause_dispatch(0);
        if (sim_get_counter_998() != 0)
            T_FAIL("mode %d started a ramp", modes[i]);
    }
    return 0;
}

int test_pause_dispatch_idempotent_while_ramping(void)
{
    /* Once the ramp is armed (998>0), a second dispatch(0) with the same
     * action is a no-op (ramp != 0 so neither the enter nor the unpause
     * branch fires — mode still 1, ramp still 1). */
    sm_prep(1, 0, 0);
    pause_dispatch(0);
    pause_dispatch(0);
    T_ASSERT_EQ_I(sim_get_counter_998(), 1);
    T_ASSERT_EQ_I(g_scene_state, 1);
    return 0;
}

int test_pause_dispatch_unpause_when_open(void)
{
    /* Mode 9, ramp fully open (>0xb): dispatch(0) flips back to the saved
     * mode and sets the closing direction. */
    sm_prep(1, 0, 0);
    pause_dispatch(0);            /* enter */
    g_scene_state = 9;           /* the integration layer would do this at ramp==3 */
    g_pause_saved_mode = 1;
    sim_set_counter_998(0xc);    /* fully open */
    sim_set_mode_9a0(1);
    pause_dispatch(0);           /* unpause */
    T_ASSERT_EQ_I(g_scene_state, 1);
    T_ASSERT_EQ_I(sim_get_mode_9a0(), 0);   /* closing */
    return 0;
}

int test_pause_dispatch_no_unpause_before_open(void)
{
    /* Mode 9 but ramp not yet past 0xb → no unpause. */
    sm_prep(1, 0, 0);
    g_scene_state = 9;
    g_pause_saved_mode = 1;
    sim_set_counter_998(5);      /* still opening */
    pause_dispatch(0);
    T_ASSERT_EQ_I(g_scene_state, 9);
    return 0;
}

/* ─── unpause teardown / resume snapshot (the pause-unpause-restore fix) ── */

/* Drive a mode-9 fully-open pause to the unpause edge from sm_prep. */
static void open_pause_then(int saved_mode)
{
    g_scene_state = 9;
    g_pause_saved_mode = saved_mode;
    sim_set_counter_998(0xc);    /* fully open (> 0xb) */
    sim_set_mode_9a0(1);         /* opening (the unpause flips it to 0) */
}

int test_pause_dispatch_unpause_no_reload(void)
{
    /* The core fix: unpausing must NOT re-spawn the load worker — the old port
     * did, which re-ran the INGAME case-1 load (scene1_preload_house →
     * scene1_postload_pose_player) and re-seated the player at the scene spawn.
     * After the unpause the worker is idle, "Now Loading" is NOT raised, and the
     * stage-load-pulse flag is cleared (engine FUN_004682d0). */
    sm_prep(1, 0, 0);
    pause_dispatch(0);                  /* enter */
    open_pause_then(1);
    stage_load_pulse_set_active(1);     /* a pulse was live at pause */
    worker_load_reset();               /* clear the enter-side gates */
    nowloading_set_active(0);

    pause_dispatch(0);                  /* unpause */

    T_ASSERT_EQ_I(g_scene_state, 1);             /* resumed the saved mode */
    T_ASSERT_EQ_I(sim_get_mode_9a0(), 0);        /* closing */
    T_ASSERT_EQ_I(worker_load_busy(), 0);        /* NO reload worker spawned */
    T_ASSERT_EQ_I(nowloading_is_active(), 0);    /* no "Now Loading" overlay */
    T_ASSERT_EQ_I(stage_load_pulse_get_active(), 0); /* FUN_004682d0 cleared it */
    return 0;
}

int test_pause_dispatch_unpause_restores_cursor(void)
{
    /* When the hand cursor was VISIBLE at pause (e.g. paused from a menu), the
     * unpause snaps it back to the captured position (engine L50274 —
     * DAT_06a499ac/b0/b4 → FUN_00435693). */
    sm_prep(1, 0, 0);
    title_save_dialog_cursor_snap(120.0f, 48.0f);  /* visible + positioned */
    pause_dispatch(0);                 /* enter → snapshots vis=1, pos=(120,48) */
    open_pause_then(1);
    title_save_dialog_cursor_set_visible(0);        /* the menu hid it while open */

    pause_dispatch(0);                 /* unpause → restore */

    T_ASSERT_EQ_I(title_save_dialog_cursor_get_visible(), 1);  /* restored visible */
    float x = -1, y = -1;
    title_save_dialog_cursor_capture_target(&x, &y);
    T_ASSERT(x == 120.0f && y == 48.0f);    /* snapped back to the captured pos */
    return 0;
}

int test_pause_dispatch_unpause_cursor_stays_hidden(void)
{
    /* When the cursor was HIDDEN at pause (HOUSE free-roam), the unpause leaves
     * it hidden — the cursor-snap restore is gated on the captured visibility,
     * so there is no spurious re-show. */
    sm_prep(1, 0, 0);
    title_save_dialog_cursor_set_visible(0);
    pause_dispatch(0);                 /* enter → snapshots vis=0 */
    open_pause_then(1);

    pause_dispatch(0);                 /* unpause */

    T_ASSERT_EQ_I(title_save_dialog_cursor_get_visible(), 0);  /* still hidden */
    return 0;
}

/* ─── state machine: nav (FUN_00480614) ──────────────────────────────── */

int test_pause_nav_up_down_wrap(void)
{
    sm_prep(1, 0, 0);
    g_pause_count = 5;
    g_pause_sel = 0;
    g_pause_sel_anim = 0;

    /* up from 0 → 4 (held bit 0x4) */
    g_sim_buttons[0].pressed = 0;
    g_sim_buttons[0].held    = 0x4;
    pause_menu_nav();
    T_ASSERT_EQ_I(g_pause_sel, 4);

    /* down from 4 → 0 (held bit 0x8) */
    g_sim_buttons[0].held = 0x8;
    pause_menu_nav();
    T_ASSERT_EQ_I(g_pause_sel, 0);

    /* down from 0 → 1 */
    pause_menu_nav();
    T_ASSERT_EQ_I(g_pause_sel, 1);
    return 0;
}

int test_pause_nav_a_starts_select_anim(void)
{
    sm_prep(1, 0, 0);
    g_pause_count = 5;
    g_pause_sel = 2;
    g_pause_sel_anim = 0;
    g_sim_buttons[0].pressed = 0x10;   /* A */
    g_sim_buttons[0].held    = 0;
    pause_menu_nav();
    T_ASSERT_EQ_I(g_pause_sel_anim, 1);  /* anim started */
    T_ASSERT_EQ_I(g_pause_sel, 2);       /* selection unchanged */
    return 0;
}

int test_pause_nav_b_closes(void)
{
    /* B (bit 0x20) on a fully-open menu re-enters pause_dispatch → unpause. */
    sm_prep(1, 0, 0);
    g_scene_state = 9;
    g_pause_saved_mode = 1;
    g_pause_action = 0;
    sim_set_counter_998(0xc);
    g_pause_sel_anim = 0;
    g_sim_buttons[0].pressed = 0x20;   /* B */
    g_sim_buttons[0].held    = 0;
    pause_menu_nav();
    T_ASSERT_EQ_I(g_scene_state, 1);   /* unpaused */
    return 0;
}

/* ─── state machine: update (FUN_0047fa76) ───────────────────────────── */

int test_pause_update_runs_nav_when_no_submenu(void)
{
    /* sub_anim < 1 → update runs the nav (down moves the cursor). */
    sm_prep(1, 0, 0);
    g_pause_count = 5;
    g_pause_sel = 0;
    g_pause_sel_anim = 0;
    g_pause_sub_anim = 0;
    g_pause_exit_confirm = 0;
    g_sim_buttons[0].pressed = 0;
    g_sim_buttons[0].held    = 0x8;   /* down */
    pause_menu_update();
    T_ASSERT_EQ_I(g_pause_sel, 1);
    T_ASSERT_EQ_I(g_pause_frame, 1);  /* frame counter bumped */
    return 0;
}

int test_pause_update_ticks_submenu_anim(void)
{
    /* sub_anim > 0 → update ticks the open/close anim, NOT the nav. */
    sm_prep(1, 0, 0);
    g_pause_count = 5;
    g_pause_sel = 0;
    g_pause_sub_anim = 1;
    g_pause_sub_dir  = 1;   /* opening */
    g_pause_exit_confirm = 0;
    g_sim_buttons[0].pressed = 0;
    g_sim_buttons[0].held    = 0x8;   /* down — must be ignored */
    pause_menu_update();
    T_ASSERT_EQ_I(g_pause_sub_anim, 2);  /* climbed */
    T_ASSERT_EQ_I(g_pause_sel, 0);       /* nav did NOT run */

    /* closing direction clamps toward 0 */
    g_pause_sub_dir = 0;
    g_pause_sub_anim = 1;
    pause_menu_update();
    T_ASSERT_EQ_I(g_pause_sub_anim, 0);
    return 0;
}

/* ─── Save submenu (type 3) commit — FUN_0049b537 + FUN_00480614 L82694 ── */

int test_save_picker_perm_init_identity(void)
{
    save_picker_reset();
    save_picker_perm_init();
    T_ASSERT_EQ_I(g_save_picker_count, 100);
    T_ASSERT_EQ_I(g_save_picker_perm[0], 0);
    T_ASSERT_EQ_I(g_save_picker_perm[42], 42);
    T_ASSERT_EQ_I(g_save_picker_perm[99], 99);
    return 0;
}

/* Drive sel_anim to 0xf with Save selected → the type-3 commit seeds the
 * picker (perm + cursor=last_slot, scroll=last_slot-2) and opens the submenu. */
int test_pause_nav_save_commit_opens_picker(void)
{
    sm_prep(1, 0, 0);
    pause_menu_setup();                 /* house list [1,6,2,3,4] */
    int save_idx = -1;
    for (int i = 0; i < g_pause_count; i++)
        if (g_pause_entries[i] == 3) save_idx = i;
    T_ASSERT(save_idx >= 0);
    g_pause_sel = save_idx;

    save_bank_arena_clear();
    save_bank_init_all();
    save_header_set_last_slot(5);
    save_picker_reset();
    g_save_picker_restricted = 1;       /* must be cleared by the commit */

    /* A starts the select anim; tick to sel_anim==0xf (the commit frame). */
    g_sim_buttons[0].pressed = 0x10;    /* A */
    g_sim_buttons[0].held    = 0;
    pause_menu_nav();                   /* 0 → 1 */
    g_sim_buttons[0].pressed = 0;
    for (int i = 0; i < 14; i++)
        pause_menu_nav();              /* 1 → 15 (0xf) */

    T_ASSERT_EQ_I(g_pause_sel_anim, 0xf);
    T_ASSERT_EQ_I(g_save_picker_count, 100);     /* perm init ran */
    T_ASSERT_EQ_I(g_save_picker_perm[7], 7);
    T_ASSERT_EQ_I(g_save_picker_restricted, 0);  /* cleared */
    T_ASSERT_EQ_I(g_pause_save_cursor, 5);        /* val[0] = last_slot */
    T_ASSERT_EQ_I(g_pause_save_scroll, 3);        /* val2[0] = last_slot-2 */
    T_ASSERT_EQ_I(g_pause_save_phase, 0);
    T_ASSERT_EQ_I(g_pause_sub_anim, 1);           /* opening */
    T_ASSERT_EQ_I(g_pause_sub_dir, 1);
    return 0;
}

/* last_slot < 2 → the scroll clamps to 0 (engine L82703 `if (iVar1 < 0)`). */
int test_pause_nav_save_commit_scroll_clamps(void)
{
    sm_prep(1, 0, 0);
    pause_menu_setup();
    int save_idx = -1;
    for (int i = 0; i < g_pause_count; i++)
        if (g_pause_entries[i] == 3) save_idx = i;
    g_pause_sel = save_idx;

    save_bank_arena_clear();
    save_bank_init_all();
    save_header_set_last_slot(0);        /* 0 - 2 = -2 → clamp 0 */
    save_picker_reset();

    g_sim_buttons[0].pressed = 0x10;
    g_sim_buttons[0].held    = 0;
    pause_menu_nav();
    g_sim_buttons[0].pressed = 0;
    for (int i = 0; i < 14; i++)
        pause_menu_nav();

    T_ASSERT_EQ_I(g_pause_save_cursor, 0);
    T_ASSERT_EQ_I(g_pause_save_scroll, 0);  /* clamped, not -2 */
    return 0;
}

/* A non-Save entry (Items, type 1) does NOT open a submenu — PORT-DEBT. */
int test_pause_nav_nonsave_commit_no_submenu(void)
{
    sm_prep(1, 0, 0);
    pause_menu_setup();
    int items_idx = -1;
    for (int i = 0; i < g_pause_count; i++)
        if (g_pause_entries[i] == 1) items_idx = i;
    T_ASSERT(items_idx >= 0);
    g_pause_sel = items_idx;
    save_picker_reset();

    g_sim_buttons[0].pressed = 0x10;
    g_sim_buttons[0].held    = 0;
    pause_menu_nav();
    g_sim_buttons[0].pressed = 0;
    for (int i = 0; i < 14; i++)
        pause_menu_nav();

    T_ASSERT_EQ_I(g_pause_sel_anim, 0xf);
    T_ASSERT_EQ_I(g_pause_sub_anim, 0);    /* no submenu opened */
    T_ASSERT_EQ_I(g_save_picker_count, 0); /* perm init did NOT run */
    return 0;
}

/* ─── Options submenu (type 2) — FUN_0047fc44 + the nav-commit init ───────── */

/* Drive sel_anim to 0xf with Options selected → the type-2 init zeroes the row
 * and opens the submenu (engine L82707). */
int test_pause_nav_options_commit_inits_row(void)
{
    sm_prep(1, 0, 0);
    pause_menu_setup();                 /* house list [1,6,2,3,4] */
    int opt_idx = -1;
    for (int i = 0; i < g_pause_count; i++)
        if (g_pause_entries[i] == 2) opt_idx = i;
    T_ASSERT(opt_idx >= 0);
    g_pause_sel = opt_idx;
    g_pause_options_row = 3;            /* a stale value the commit must zero */

    g_sim_buttons[0].pressed = 0x10;    /* A */
    g_sim_buttons[0].held    = 0;
    pause_menu_nav();                   /* 0 → 1 */
    g_sim_buttons[0].pressed = 0;
    for (int i = 0; i < 14; i++)
        pause_menu_nav();              /* 1 → 15 (0xf) */

    T_ASSERT_EQ_I(g_pause_sel_anim, 0xf);
    T_ASSERT_EQ_I(g_pause_options_row, 0);   /* reset to row 0 */
    T_ASSERT_EQ_I(g_pause_sub_anim, 1);      /* opening */
    T_ASSERT_EQ_I(g_pause_sub_dir, 1);
    return 0;
}

static void opt_prep(int row)
{
    sim_init();
    pause_sm_reset();
    audio_fade_reset();
    settings_reset();
    g_pause_options_row   = row;
    g_pause_options_phase = 0;
    g_sim_buttons[0].pressed = 0;
    g_sim_buttons[0].held    = 0;
}

/* DOWN walks the cursor row 0→1→2→3→4→0 (wrap %5). */
int test_pause_options_cursor_down_wraps(void)
{
    opt_prep(0);
    for (int i = 1; i <= 5; i++) {
        g_sim_buttons[0].held = 0x8;       /* down */
        pause_options_submenu_update();
        g_sim_buttons[0].held = 0;
        T_ASSERT_EQ_I(g_pause_options_row, i % 5);
    }
    return 0;
}

/* UP from row 0 wraps to row 4. */
int test_pause_options_cursor_up_wraps(void)
{
    opt_prep(0);
    g_sim_buttons[0].held = 0x4;           /* up */
    pause_options_submenu_update();
    T_ASSERT_EQ_I(g_pause_options_row, 4);
    return 0;
}

/* Row 0 (Music/BGM): RIGHT increments to max 9, LEFT decrements to min 0; a
 * change marks the menu dirty (phase 1). */
int test_pause_options_bgm_slider_clamps(void)
{
    opt_prep(0);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 8);
    g_sim_buttons[0].held = 0x1;           /* right */
    pause_options_submenu_update();
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 9);
    T_ASSERT_EQ_I(g_pause_options_phase, 1);   /* dirty */
    pause_options_submenu_update();            /* already 9 → clamp, no change */
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 9);

    g_sim_buttons[0].held = 0x2;           /* left */
    for (int i = 0; i < 12; i++) pause_options_submenu_update();
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 0);  /* clamp 0 */
    return 0;
}

/* Row 3 (Message Speed) clamps at 2; row 4 (Unread Text Skip) clamps at 1. */
int test_pause_options_word_sliders_clamp(void)
{
    opt_prep(3);
    settings_set_slider3(0);
    g_sim_buttons[0].held = 0x1;           /* right */
    for (int i = 0; i < 5; i++) pause_options_submenu_update();
    T_ASSERT_EQ_I(settings_get_slider3(), 2);     /* SLOW→MED→FAST, clamp 2 */

    opt_prep(4);
    settings_set_slider4(0);
    g_sim_buttons[0].held = 0x1;
    for (int i = 0; i < 5; i++) pause_options_submenu_update();
    T_ASSERT_EQ_I(settings_get_slider4(), 1);     /* OFF→ON, clamp 1 */
    return 0;
}

/* Row 2 (Voice/SE-B) — the silent row still adjusts its value + marks dirty. */
int test_pause_options_voice_adjusts(void)
{
    opt_prep(2);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_B, 5);
    g_sim_buttons[0].held = 0x2;           /* left */
    pause_options_submenu_update();
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B), 4);
    T_ASSERT_EQ_I(g_pause_options_phase, 1);
    return 0;
}

/* A when DIRTY → exit-save (phase 2); the next tick commits + closes. */
int test_pause_options_exit_dirty_saves_then_closes(void)
{
    opt_prep(0);
    g_pause_options_phase = 1;             /* dirty */
    g_pause_sub_dir  = 1;                  /* submenu open */
    g_pause_sel_anim = 0xf;
    save_io_set_write_dir("/tmp");         /* sandbox the save.dat write */

    g_sim_buttons[0].pressed = 0x10;       /* A */
    pause_options_submenu_update();
    T_ASSERT_EQ_I(g_pause_options_phase, 2);   /* exit-save armed */

    g_sim_buttons[0].pressed = 0;
    pause_options_submenu_update();             /* commits + closes */
    T_ASSERT_EQ_I(g_pause_options_phase, 0);
    T_ASSERT_EQ_I(g_pause_sub_dir, 0);          /* submenu closing */
    T_ASSERT_EQ_I(g_pause_sel_anim, 0);

    save_io_set_write_dir(NULL);
    return 0;
}

/* B when CLEAN → exit-no-save (phase 3); the next tick closes (no save). */
int test_pause_options_exit_clean_closes_no_save(void)
{
    opt_prep(0);
    g_pause_options_phase = 0;             /* clean */
    g_pause_sub_dir = 1;

    g_sim_buttons[0].pressed = 0x20;       /* B */
    pause_options_submenu_update();
    T_ASSERT_EQ_I(g_pause_options_phase, 3);   /* exit-no-save armed */

    g_sim_buttons[0].pressed = 0;
    pause_options_submenu_update();             /* closes */
    T_ASSERT_EQ_I(g_pause_options_phase, 0);
    T_ASSERT_EQ_I(g_pause_sub_dir, 0);
    return 0;
}

/* The OPTIONS_READY predicate: scene 9, sub_anim==10, Options selected. */
int test_pause_options_navigable_predicate(void)
{
    sm_prep(1, 0, 0);
    pause_menu_setup();
    int opt_idx = -1;
    for (int i = 0; i < g_pause_count; i++)
        if (g_pause_entries[i] == 2) opt_idx = i;
    g_pause_sel = opt_idx;
    g_pause_sub_anim = 10;
    T_ASSERT_EQ_I(pause_options_navigable(9), 1);
    T_ASSERT_EQ_I(pause_options_navigable(1), 0);   /* wrong scene */
    g_pause_sub_anim = 9;
    T_ASSERT_EQ_I(pause_options_navigable(9), 0);    /* not fully open */
    return 0;
}

/* ─── Save submenu (type 3) NAV — FUN_0047f5bc (M4b) ──────────────────────
 * pause_save_submenu_update is the resting/nav path: U/D ±1, L/R ±3, the
 * c894/c898 slide anims, B-cancel. Seed the picker directly into the open
 * state and drive a button per call (the host nav has no auto-repeat gate). */

static void nav_prep(int cursor, int scroll)
{
    sim_init();
    pause_sm_reset();
    save_picker_reset();
    g_pause_save_cursor  = cursor;
    g_pause_save_scroll  = scroll;
    g_pause_save_vscroll = 0;
    g_pause_save_hscroll = 0;
    g_pause_save_phase   = 0;
    g_sim_buttons[0].pressed = 0;
    g_sim_buttons[0].held    = 0;
}

/* DOWN moves the cursor within the 5-row window (no slide until cursor-scroll
 * passes 2), then arms the c894 row slide that commits scroll+1 over 4 ticks. */
int test_pause_save_nav_down_scrolls(void)
{
    nav_prep(0, 0);
    for (int i = 0; i < 3; i++) {       /* DOWN ×3: cursor 0→1→2→3 */
        g_sim_buttons[0].held = 0x8;
        pause_save_submenu_update();
        g_sim_buttons[0].held = 0;
    }
    T_ASSERT_EQ_I(g_pause_save_cursor, 3);
    T_ASSERT_EQ_I(g_pause_save_scroll, 0);
    T_ASSERT_EQ_I(g_pause_save_hscroll, 1);   /* 3-0 > 2 → row slide armed */

    for (int i = 0; i < 4; i++)               /* hscroll 1→5 → scroll++ */
        pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_hscroll, 0);
    T_ASSERT_EQ_I(g_pause_save_scroll, 1);     /* committed */
    T_ASSERT_EQ_I(g_pause_save_cursor, 3);
    return 0;
}

/* The first two DOWNs stay inside the window (cursor-scroll ≤ 2) — no slide. */
int test_pause_save_nav_down_within_window_no_slide(void)
{
    nav_prep(0, 0);
    g_sim_buttons[0].held = 0x8;
    pause_save_submenu_update();              /* cursor 0→1 */
    T_ASSERT_EQ_I(g_pause_save_cursor, 1);
    T_ASSERT_EQ_I(g_pause_save_hscroll, 0);
    pause_save_submenu_update();              /* cursor 1→2 (2-0==2) */
    T_ASSERT_EQ_I(g_pause_save_cursor, 2);
    T_ASSERT_EQ_I(g_pause_save_hscroll, 0);
    return 0;
}

/* UP above the window arms a -1 row slide that commits scroll-1. */
int test_pause_save_nav_up_scrolls(void)
{
    nav_prep(3, 3);
    g_sim_buttons[0].held = 0x4;
    pause_save_submenu_update();              /* cursor 3→2; 2-3<0 → slide */
    T_ASSERT_EQ_I(g_pause_save_cursor, 2);
    T_ASSERT_EQ_I(g_pause_save_hscroll, -1);
    g_sim_buttons[0].held = 0;
    for (int i = 0; i < 4; i++)               /* hscroll -1→-5 → scroll-- */
        pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_hscroll, 0);
    T_ASSERT_EQ_I(g_pause_save_scroll, 2);
    return 0;
}

/* UP at slot 0 / DOWN at slot 99 are no-ops (engine cursor clamps). */
int test_pause_save_nav_bounds_noop(void)
{
    nav_prep(0, 0);
    g_sim_buttons[0].held = 0x4;              /* UP at top */
    pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_cursor, 0);
    T_ASSERT_EQ_I(g_pause_save_hscroll, 0);

    nav_prep(99, 97);
    g_sim_buttons[0].held = 0x8;              /* DOWN at bottom */
    pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_cursor, 99);
    T_ASSERT_EQ_I(g_pause_save_hscroll, 0);
    return 0;
}

/* RIGHT jumps the cursor +3 and arms the c898 column slide (scroll += 3). */
int test_pause_save_nav_right_pages(void)
{
    nav_prep(0, 0);
    g_sim_buttons[0].held = 0x1;
    pause_save_submenu_update();              /* cursor 0→3; 3-0>0 → slide */
    T_ASSERT_EQ_I(g_pause_save_cursor, 3);
    T_ASSERT_EQ_I(g_pause_save_vscroll, 1);
    g_sim_buttons[0].held = 0;
    for (int i = 0; i < 4; i++)               /* vscroll 1→5 → scroll += 3 */
        pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_vscroll, 0);
    T_ASSERT_EQ_I(g_pause_save_scroll, 3);
    return 0;
}

/* LEFT needs cursor>0 AND scroll>0; it jumps -3 and slides scroll -= 3
 * (clamped ≥0). Plain LEFT at slot 0 is a no-op. */
int test_pause_save_nav_left_pages(void)
{
    nav_prep(0, 0);
    g_sim_buttons[0].held = 0x2;              /* LEFT at top — no-op */
    pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_cursor, 0);
    T_ASSERT_EQ_I(g_pause_save_vscroll, 0);

    nav_prep(5, 3);
    g_sim_buttons[0].held = 0x2;
    pause_save_submenu_update();              /* cursor 5→2; 2-3<0 → slide */
    T_ASSERT_EQ_I(g_pause_save_cursor, 2);
    T_ASSERT_EQ_I(g_pause_save_vscroll, -1);
    g_sim_buttons[0].held = 0;
    for (int i = 0; i < 4; i++)
        pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_vscroll, 0);
    T_ASSERT_EQ_I(g_pause_save_scroll, 0);     /* 3-3, clamped ≥0 */
    return 0;
}

/* A running slide swallows the frame — buttons are not polled mid-anim. */
int test_pause_save_nav_anim_ignores_buttons(void)
{
    nav_prep(3, 0);
    g_pause_save_hscroll = 1;                  /* a row slide is running */
    g_sim_buttons[0].held = 0x8;              /* DOWN must be ignored */
    pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_cursor, 3);     /* unchanged */
    T_ASSERT_EQ_I(g_pause_save_hscroll, 2);    /* the anim advanced, not nav */
    return 0;
}

/* B cancels: drops sub_dir (the submenu slides closed) + clears sel_anim. */
int test_pause_save_nav_b_closes(void)
{
    nav_prep(0, 0);
    g_pause_sub_dir  = 1;                       /* submenu open */
    g_pause_sel_anim = 7;
    g_sim_buttons[0].pressed = 0x20;          /* B */
    pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_sub_dir, 0);         /* closing */
    T_ASSERT_EQ_I(g_pause_sel_anim, 0);
    return 0;
}

/* ─── Save submenu (type 3) A-COMMIT — FUN_0047f5bc A-branch (M4c) ────────
 * Seed the picker open with the save bank initialised; A on the cursor's slot
 * either commits at once (empty) or pops the "Overwriting file." box (occupied). */
static void commit_prep(int cursor, int occupied)
{
    nav_prep(cursor, 0);
    save_bank_arena_clear();
    save_bank_init_all();
    g_pause_save_overwrite = 0;
    /* perm is identity, so the cursor value IS the target bank slot. Set its
     * playtime (the OCCUPIED dword) for the empty/occupied branch. */
    save_bank_dwords_at(cursor)[SAVE_BANK_FIELD_OCCUPIED] = (uint32_t)occupied;
}

/* A on an EMPTY slot commits straight away: phase=1, no dialog. */
int test_pause_save_a_empty_commits(void)
{
    commit_prep(/*cursor=*/3, /*occupied=*/0);
    g_sim_buttons[0].pressed = 0x10;          /* A */
    pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_phase, 1);      /* commit started */
    T_ASSERT_EQ_I(g_pause_save_overwrite, 0);  /* no overwrite dialog */
    T_ASSERT_EQ_I(choice_box_active(), 0);
    return 0;
}

/* A on an OCCUPIED slot opens the "Overwriting file." box (no commit yet). */
int test_pause_save_a_occupied_opens_overwrite(void)
{
    commit_prep(/*cursor=*/3, /*occupied=*/100);
    g_sim_buttons[0].pressed = 0x10;          /* A */
    pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_phase, 0);      /* not committing yet */
    T_ASSERT_EQ_I(g_pause_save_overwrite, 1);  /* dialog armed */
    T_ASSERT(choice_box_active() != 0);        /* the box is up */
    return 0;
}

/* The overwrite box's Yes drives the commit: poll it to confirmation → phase=1.
 * (Default selection is Yes/option-0, so a single A confirms once interactive.) */
int test_pause_save_overwrite_yes_commits(void)
{
    commit_prep(/*cursor=*/3, /*occupied=*/100);
    g_sim_buttons[0].pressed = 0x10;          /* A — opens the box */
    pause_save_submenu_update();
    g_sim_buttons[0].pressed = 0;
    for (int i = 0; i < 6; i++)               /* ramp the box to interactive */
        pause_save_submenu_update();
    g_sim_buttons[0].pressed = 0x10;          /* A — confirm Yes */
    pause_save_submenu_update();
    g_sim_buttons[0].pressed = 0;
    int guard = 0;
    while (g_pause_save_phase == 0 && guard++ < 20)  /* close anim → CB_OPT0 → phase=1 */
        pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_phase, 1);      /* stop AT phase 1 (before commit_tick) */
    T_ASSERT_EQ_I(g_pause_save_overwrite, 0);  /* dialog consumed */
    return 0;
}

/* B on the overwrite box cancels: the box closes, no commit, flags cleared. */
int test_pause_save_overwrite_b_cancels(void)
{
    commit_prep(/*cursor=*/3, /*occupied=*/100);
    g_sim_buttons[0].pressed = 0x10;          /* A — opens the box */
    pause_save_submenu_update();
    g_sim_buttons[0].pressed = 0;
    for (int i = 0; i < 6; i++)               /* ramp to interactive */
        pause_save_submenu_update();
    g_sim_buttons[0].pressed = 0x20;          /* B — cancel (box is mode 1) */
    pause_save_submenu_update();
    g_sim_buttons[0].pressed = 0;
    int guard = 0;
    while (g_pause_save_overwrite == 1 && guard++ < 20)
        pause_save_submenu_update();
    T_ASSERT_EQ_I(g_pause_save_phase, 0);      /* no commit */
    T_ASSERT_EQ_I(g_pause_save_overwrite, 0);  /* dialog dismissed */
    return 0;
}

/* The commit counter runs 1→0x3c then wraps to 0 (engine 0x47f73a). Start at
 * phase 2 so commit_tick only advances the counter (the phase==1 disk write
 * fires once, at the start of a real commit — not exercised here). */
int test_pause_save_commit_counter_wraps(void)
{
    nav_prep(0, 0);
    g_pause_save_phase = 2;
    for (int p = 2; p < 0x3c; p++) {
        pause_save_submenu_update();          /* phase p → p+1 */
        T_ASSERT_EQ_I(g_pause_save_phase, p + 1 == 0x3c ? 0 : p + 1);
    }
    T_ASSERT_EQ_I(g_pause_save_phase, 0);      /* wrapped at 0x3c */
    return 0;
}

/* pause_save_picker_navigable gates the SAVE_PICKER_READY anchor: scene 9,
 * sub_anim==10, Save (type 3) selected. */
int test_pause_save_picker_navigable_pred(void)
{
    sm_prep(1, 0, 0);
    pause_menu_setup();                        /* [1,6,2,3,4] */
    int save_idx = -1, items_idx = -1;
    for (int i = 0; i < g_pause_count; i++) {
        if (g_pause_entries[i] == 3) save_idx  = i;
        if (g_pause_entries[i] == 1) items_idx = i;
    }
    g_pause_sel = save_idx;
    g_pause_sub_anim = 10;
    T_ASSERT_EQ_I(pause_save_picker_navigable(9), 1);   /* all conditions */
    T_ASSERT_EQ_I(pause_save_picker_navigable(1), 0);   /* wrong scene mode */
    g_pause_sub_anim = 9;
    T_ASSERT_EQ_I(pause_save_picker_navigable(9), 0);   /* not fully open */
    g_pause_sub_anim = 10;
    g_pause_sel = items_idx;
    T_ASSERT_EQ_I(pause_save_picker_navigable(9), 0);   /* Items, not Save */
    return 0;
}

/* pause_menu_update dispatches the Save nav only when fully open AND Save is
 * the selected entry; a non-Save type does not drive the picker. */
int test_pause_update_dispatches_save_nav(void)
{
    sm_prep(1, 0, 0);
    pause_menu_setup();                        /* [1,6,2,3,4] */
    int save_idx = -1, items_idx = -1;
    for (int i = 0; i < g_pause_count; i++) {
        if (g_pause_entries[i] == 3) save_idx  = i;
        if (g_pause_entries[i] == 1) items_idx = i;
    }
    save_picker_reset();
    g_pause_save_cursor = 0; g_pause_save_scroll = 0;
    g_pause_save_hscroll = 0; g_pause_save_vscroll = 0; g_pause_save_phase = 0;
    g_pause_sub_anim = 10; g_pause_sub_dir = 1;

    /* Save selected → DOWN moves the picker cursor. */
    g_pause_sel = save_idx;
    g_sim_buttons[0].pressed = 0;
    g_sim_buttons[0].held    = 0x8;
    pause_menu_update();
    T_ASSERT_EQ_I(g_pause_save_cursor, 1);

    /* Items selected → the picker nav does NOT run (submenu PORT-DEBT). */
    g_pause_sel = items_idx;
    g_pause_sub_anim = 10; g_pause_sub_dir = 1;
    g_sim_buttons[0].held = 0x8;
    pause_menu_update();
    T_ASSERT_EQ_I(g_pause_save_cursor, 1);      /* unchanged */
    return 0;
}
