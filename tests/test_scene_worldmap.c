/*
 * test_scene_worldmap.c — pure-C tests for the C96 secondary inner-body
 * BMP-loader half (engine FUN_004735ad — world-map asset loader).
 *
 * Structural sibling of test_scene_floor / test_scene_jutan but simpler
 * (no selector, no loop — 4 fixed dispatches).
 */
#include "t.h"

#include <string.h>

#include "scene_worldmap.h"
#include "worker_load.h"
#include "save_work.h"
#include "save_bank.h"   /* SAVE_BANK_FIELD_CARD_DAY / _CLOCK_TARGET */
#include "sim.h"              /* g_sim_buttons — drive the mode-8 sim input */
#include "fade.h"             /* fade_reset/_tick — wind the exit dissolve */
#include "scene.h"            /* g_scene_state — observe the dest transition */
#include "title_save_dialog.h"/* cursor reset + position read-back */

/* Working-arena field locations FUN_0049de20 reads (base DAT_044e3798,
 * working slot 0). Mirror the (static) WM_* offsets in scene_worldmap.c. */
#define WM_T_OFF_TUTORIAL_A  0x2bc61   /* DAT_0450f3f9 (byte) */
#define WM_T_OFF_TUTORIAL_B  0x2bc70   /* DAT_0450f408 (byte) */
#define WM_T_OFF_EVENT_FLAG  0x2bc7c   /* DAT_0450f414 (byte) */
#define WM_T_DW_DAY          SAVE_BANK_FIELD_CARD_DAY     /* 0xb0fb (dword) */
#define WM_T_DW_TOD          SAVE_BANK_FIELD_CLOCK_TARGET /* 0xb0fc (dword) */

/* Recording scratchpad for the injected load_fn. */
#define MAX_RECORDED 8
static struct {
    int      n;
    char     path[MAX_RECORDED][64];
    uint32_t w[MAX_RECORDED];
    uint32_t h[MAX_RECORDED];
    int      slot[MAX_RECORDED];
    void    *userdata[MAX_RECORDED];
} g_rec;

static void reset_recorded(void)
{
    memset(&g_rec, 0, sizeof(g_rec));
}

static int recording_load_fn(const char *path,
                             uint32_t w, uint32_t h,
                             int slot, void *userdata)
{
    if (g_rec.n < MAX_RECORDED) {
        size_t n = strlen(path);
        if (n >= sizeof(g_rec.path[0])) n = sizeof(g_rec.path[0]) - 1;
        memcpy(g_rec.path[g_rec.n], path, n);
        g_rec.path[g_rec.n][n] = '\0';
        g_rec.w[g_rec.n]        = w;
        g_rec.h[g_rec.n]        = h;
        g_rec.slot[g_rec.n]     = slot;
        g_rec.userdata[g_rec.n] = userdata;
        g_rec.n++;
    }
    return 1;
}

int test_scene_worldmap_count_is_four(void)
{
    /* Engine FUN_004735ad has exactly 4 sprite_load calls. */
    T_ASSERT_EQ_I(SCENE_WORLDMAP_COUNT, 4);
    return 0;
}

int test_scene_worldmap_filename_table_full_order(void)
{
    /* Strings extracted from vendor/unpacked/recettear.unpacked.exe via
     *   tools/analyze/pe.py str 0x005c87f4 0x005c880c 0x005c8824 0x005c883c
     */
    T_ASSERT(strcmp(scene_worldmap_filename(SCENE_WORLDMAP_TEX_NOMAL),
                    "bmp/worldmap_nomal.bmp")  == 0);
    T_ASSERT(strcmp(scene_worldmap_filename(SCENE_WORLDMAP_TEX_YUGATA),
                    "bmp/worldmap_yugata.bmp") == 0);
    T_ASSERT(strcmp(scene_worldmap_filename(SCENE_WORLDMAP_TEX_NIGHT),
                    "bmp/worldmap_night.bmp")  == 0);
    T_ASSERT(strcmp(scene_worldmap_filename(SCENE_WORLDMAP_TEX_MAPPOINT),
                    "bmp/mappoint.tga")        == 0);
    return 0;
}

int test_scene_worldmap_filename_out_of_range_is_null(void)
{
    T_ASSERT(scene_worldmap_filename(-1) == 0);
    T_ASSERT(scene_worldmap_filename(SCENE_WORLDMAP_COUNT) == 0);
    T_ASSERT(scene_worldmap_filename(9999) == 0);
    return 0;
}

int test_scene_worldmap_dims_match_engine_calls(void)
{
    /* Engine FUN_0047193c args at @ 0x4735ad:
     *   nomal/yugata/night → (0x400, 0x200)
     *   mappoint           → (0x100, 0x400) */
    uint32_t w, h;

    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_TEX_NOMAL,    &w, &h), 1);
    T_ASSERT_EQ_U(w, 0x400);
    T_ASSERT_EQ_U(h, 0x200);

    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_TEX_YUGATA,   &w, &h), 1);
    T_ASSERT_EQ_U(w, 0x400);
    T_ASSERT_EQ_U(h, 0x200);

    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_TEX_NIGHT,    &w, &h), 1);
    T_ASSERT_EQ_U(w, 0x400);
    T_ASSERT_EQ_U(h, 0x200);

    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_TEX_MAPPOINT, &w, &h), 1);
    T_ASSERT_EQ_U(w, 0x100);
    T_ASSERT_EQ_U(h, 0x400);
    return 0;
}

int test_scene_worldmap_dims_out_of_range_zeroes_outputs(void)
{
    uint32_t w = 0xdeadbeefu, h = 0xcafef00du;
    T_ASSERT_EQ_I(scene_worldmap_dims(-1, &w, &h), 0);
    T_ASSERT_EQ_U(w, 0);
    T_ASSERT_EQ_U(h, 0);

    w = 0xdeadbeefu; h = 0xcafef00du;
    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_COUNT, &w, &h), 0);
    T_ASSERT_EQ_U(w, 0);
    T_ASSERT_EQ_U(h, 0);

    /* NULL out-pointers are tolerated. */
    T_ASSERT_EQ_I(scene_worldmap_dims(0, 0, 0), 1);
    return 0;
}

int test_scene_worldmap_load_dispatches_all_four(void)
{
    scene_worldmap_reset();
    reset_recorded();

    int loads = scene_worldmap_load_with(recording_load_fn,
                                          (void *)0xdecafbadu);

    T_ASSERT_EQ_I(loads,    SCENE_WORLDMAP_COUNT);
    T_ASSERT_EQ_I(g_rec.n,  SCENE_WORLDMAP_COUNT);

    /* Slot order matches the engine call order. */
    T_ASSERT_EQ_I(g_rec.slot[0], SCENE_WORLDMAP_TEX_NOMAL);
    T_ASSERT_EQ_I(g_rec.slot[1], SCENE_WORLDMAP_TEX_YUGATA);
    T_ASSERT_EQ_I(g_rec.slot[2], SCENE_WORLDMAP_TEX_NIGHT);
    T_ASSERT_EQ_I(g_rec.slot[3], SCENE_WORLDMAP_TEX_MAPPOINT);

    /* Userdata threads through verbatim on every dispatch. */
    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        T_ASSERT(g_rec.userdata[i] == (void *)0xdecafbadu);
    }
    return 0;
}

int test_scene_worldmap_load_paths_match_filename_table(void)
{
    scene_worldmap_reset();
    reset_recorded();

    (void)scene_worldmap_load_with(recording_load_fn, 0);

    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        T_ASSERT(strcmp(g_rec.path[i],
                        scene_worldmap_filename(g_rec.slot[i])) == 0);
    }
    return 0;
}

int test_scene_worldmap_load_dims_match_metadata(void)
{
    scene_worldmap_reset();
    reset_recorded();

    (void)scene_worldmap_load_with(recording_load_fn, 0);

    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        uint32_t want_w, want_h;
        T_ASSERT_EQ_I(scene_worldmap_dims(g_rec.slot[i], &want_w, &want_h), 1);
        T_ASSERT_EQ_U(g_rec.w[i], want_w);
        T_ASSERT_EQ_U(g_rec.h[i], want_h);
    }
    return 0;
}

int test_scene_worldmap_load_without_load_fn_returns_count(void)
{
    /* NULL load_fn is a counting-only dry run. */
    scene_worldmap_reset();
    int loads = scene_worldmap_load_with(0, 0);
    T_ASSERT_EQ_I(loads, SCENE_WORLDMAP_COUNT);
    return 0;
}

int test_scene_worldmap_body_slot_starts_null(void)
{
    /* Body callback is NULL until scene_worldmap_init runs (which is
     * Win32-only). On the Linux test build the C96 slot stays NULL —
     * a half-port that's dormant until the spawner gets a caller. */
    worker_load_reset();
    T_ASSERT(worker_load_get_sec_body(WORKER_LOAD_SEC_BODY_C96) == 0);
    return 0;
}

/* Worldmap mappoint slot's dims (256x1024) are unique among the 4
 * worldmap entries — the other three are all 1024x512. Anchor this
 * specifically since the mappoint engine dest (DAT_073aa7d8) is at a
 * different base address from the worldmap-BG trio. */
int test_scene_worldmap_mappoint_is_tall_unique(void)
{
    uint32_t w, h;
    (void)scene_worldmap_dims(SCENE_WORLDMAP_TEX_MAPPOINT, &w, &h);
    T_ASSERT(w < h);  /* the only tall-portrait slot of the 4 */

    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        if (i == SCENE_WORLDMAP_TEX_MAPPOINT) continue;
        uint32_t bw, bh;
        (void)scene_worldmap_dims(i, &bw, &bh);
        T_ASSERT(bw > bh);  /* the 3 BG textures are wide-landscape */
    }
    return 0;
}

/* ─── FUN_0049de20 scene-init (destination model + tutorial gating) ─────── */

/* Set up the working arena (slot 0) with the tutorial flags + day/tod the
 * world-map init reads, then run scene_worldmap_init_state(). */
static void wm_init_with(int flag_a, int flag_b, int event_flag,
                         uint32_t day, uint32_t tod)
{
    scene_worldmap_reset();
    save_work_clear();                 /* zero arena + active slot 0 */
    uint8_t  *bb = (uint8_t  *)save_work_dwords_at(0);
    uint32_t *dw = (uint32_t *)bb;
    bb[WM_T_OFF_TUTORIAL_A] = (uint8_t)flag_a;
    bb[WM_T_OFF_TUTORIAL_B] = (uint8_t)flag_b;
    bb[WM_T_OFF_EVENT_FLAG] = (uint8_t)event_flag;
    dw[WM_T_DW_DAY]         = day;
    dw[WM_T_DW_TOD]         = tod;
    scene_worldmap_init_state();
}

/* The recording's first-exit case: the door sets tutorial flag A, so the
 * init's `else` branch highlights dest 3 (Market) and leaves the rest
 * disabled. tod=2 (night) + day=1 so no time-of-day closure perturbs it. */
int test_scene_worldmap_init_tutorial_market(void)
{
    wm_init_with(/*flag_a=*/1, 0, 0, /*day=*/1, /*tod=*/2);

    T_ASSERT_EQ_I(scene_worldmap_dest_count(), 7);
    int want[7] = { 0, 0, 0, 2, 0, 0, 0 };
    for (int i = 0; i < 7; i++)
        T_ASSERT_EQ_I(scene_worldmap_dest_state(i), want[i]);
    /* dest→map-pos is the identity. */
    for (int i = 0; i < 7; i++)
        T_ASSERT_EQ_I(scene_worldmap_dest_pos(i), i);
    /* entry timer reset. */
    T_ASSERT(scene_worldmap_entry_timer() == 0.0f);
    return 0;
}

/* Tutorial complete (flag A clear, flag B clear) → every destination NORMAL
 * (state 1). tod=2 (night) avoids the tod<2 dest-4 closure + the tod==3
 * dest-1/5 closures; day=1 avoids the day%7==3 dest-6 closure. */
int test_scene_worldmap_init_all_normal(void)
{
    wm_init_with(0, 0, 0, 1, 2);
    for (int i = 0; i < 7; i++)
        T_ASSERT_EQ_I(scene_worldmap_dest_state(i), 1);
    for (int i = 0; i < 7; i++)
        T_ASSERT_EQ_I(scene_worldmap_dest_closed(i), 0);
    return 0;
}

/* Flag A clear, flag B SET → only dest 0 highlighted, the rest disabled. */
int test_scene_worldmap_init_flag_b_only_dest0(void)
{
    wm_init_with(0, /*flag_b=*/1, 0, 1, 2);
    int want[7] = { 2, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 7; i++)
        T_ASSERT_EQ_I(scene_worldmap_dest_state(i), want[i]);
    return 0;
}

/* day % 7 == 3 closes dest 6 ("Closed" label + state forced 0). The tutorial
 * highlight on dest 3 is unaffected. */
int test_scene_worldmap_init_day_closure_dest6(void)
{
    wm_init_with(/*flag_a=*/1, 0, 0, /*day=*/3, /*tod=*/2);
    T_ASSERT_EQ_I(scene_worldmap_dest_closed(6), 1);
    T_ASSERT_EQ_I(scene_worldmap_dest_state(6), 0);
    T_ASSERT_EQ_I(scene_worldmap_dest_state(3), 2);  /* Market still highlighted */
    return 0;
}

/* tod < 2 (day/evening) closes dest 4; tod==2 (night) leaves it open. Drive
 * the all-normal base so the closure is the only thing zeroing dest 4. */
int test_scene_worldmap_init_tod_closes_dest4(void)
{
    wm_init_with(0, 0, 0, 1, /*tod=*/0);   /* day */
    T_ASSERT_EQ_I(scene_worldmap_dest_state(4), 0);  /* closed at tod<2 */
    /* other dests unaffected by the tod<2 rule (dest 1/5 need tod==3, which
     * never occurs in normal play — tod is 0/1/2). */
    T_ASSERT_EQ_I(scene_worldmap_dest_state(1), 1);
    T_ASSERT_EQ_I(scene_worldmap_dest_state(5), 1);
    return 0;
}

/* The extracted .data layout table (DAT_005fd590) — spot-check the two
 * anchor destinations. */
int test_scene_worldmap_dest_layout_table(void)
{
    T_ASSERT(scene_worldmap_dest_layout[0].x == 230.0f);
    T_ASSERT(scene_worldmap_dest_layout[0].y == 400.0f);
    T_ASSERT_EQ_I(scene_worldmap_dest_layout[0].sprite_row, 0);

    T_ASSERT(scene_worldmap_dest_layout[3].x == 440.0f);   /* Market */
    T_ASSERT(scene_worldmap_dest_layout[3].y == 276.0f);
    T_ASSERT_EQ_I(scene_worldmap_dest_layout[3].sprite_row, 5);
    return 0;
}

/* The extracted .data cursor grid (DAT_005fd620, 3×5). */
int test_scene_worldmap_grid_table(void)
{
    /* row 0 empty, row 1 = {4,5,6}, row 2 = {1,2,3}. */
    T_ASSERT_EQ_I(scene_worldmap_grid[0], -1);
    T_ASSERT_EQ_I(scene_worldmap_grid[3],  4);
    T_ASSERT_EQ_I(scene_worldmap_grid[4],  5);
    T_ASSERT_EQ_I(scene_worldmap_grid[5],  6);
    T_ASSERT_EQ_I(scene_worldmap_grid[6],  1);
    T_ASSERT_EQ_I(scene_worldmap_grid[8],  3);
    return 0;
}

/* FUN_0049de0e / read-back of the selected destination. */
int test_scene_worldmap_sel_dest_roundtrip(void)
{
    scene_worldmap_reset();
    T_ASSERT_EQ_I(scene_worldmap_sel_dest(), 0);   /* reset default */
    scene_worldmap_set_sel_dest(3);
    T_ASSERT_EQ_I(scene_worldmap_sel_dest(), 3);
    scene_worldmap_reset();
    T_ASSERT_EQ_I(scene_worldmap_sel_dest(), 0);   /* reset clears it */
    return 0;
}

/* ─── FUN_0049e163 sim — entry timer + 3×5 cursor nav + Z-select ────────── */

/* Build the dest model (flag_a=1 = tutorial gate → dest 3 Market enabled,
 * rest disabled), select `sel`, clean the shared cursor + fade/worker state,
 * enter mode 8, and wind the entry timer past 10 so input is accepted. */
static void wm_sim_ready(int sel)
{
    wm_init_with(/*flag_a=*/1, 0, 0, /*day=*/1, /*tod=*/2);
    scene_worldmap_set_sel_dest(sel);     /* FUN_0049de0e */
    title_save_dialog_reset();
    fade_reset();
    worker_load_reset();
    g_scene_state = SCENE_STATE_WORLDMAP;  /* mode 8 */
    g_sim_buttons[0].held    = 0;
    g_sim_buttons[0].pressed = 0;
    /* 11 no-input ticks → entry timer 11 (> 10): the 12th tick accepts input. */
    for (int i = 0; i < 11; i++) scene_worldmap_sim();
}

/* One input tick: set held/pressed, run the sim once, return the new sel. */
static int wm_sim_press(uint16_t held, uint16_t pressed)
{
    g_sim_buttons[0].held    = held;
    g_sim_buttons[0].pressed = pressed;
    scene_worldmap_sim();
    return scene_worldmap_sel_dest();
}

/* The 3×5 grid puts dest 2 at (col 1,row 2); its four neighbours are
 * Up→5, Down→0, Left→1, Right→3 (button bits 0x04/0x08/0x02/0x01). The nav
 * reads the held mask (DAT_073dddd6). */
int test_scene_worldmap_nav_up_from_center(void)
{
    wm_sim_ready(2);
    T_ASSERT_EQ_I(wm_sim_press(0x04, 0), 5);   /* Up → center-upper */
    return 0;
}
int test_scene_worldmap_nav_down_from_center(void)
{
    wm_sim_ready(2);
    T_ASSERT_EQ_I(wm_sim_press(0x08, 0), 0);   /* Down → shop/home */
    return 0;
}
int test_scene_worldmap_nav_left_from_center(void)
{
    wm_sim_ready(2);
    T_ASSERT_EQ_I(wm_sim_press(0x02, 0), 1);   /* Left → left node */
    return 0;
}
int test_scene_worldmap_nav_right_from_center(void)
{
    wm_sim_ready(2);
    T_ASSERT_EQ_I(wm_sim_press(0x01, 0), 3);   /* Right → Market */
    return 0;
}

/* No direction held → selection holds. */
int test_scene_worldmap_nav_no_input_holds(void)
{
    wm_sim_ready(2);
    T_ASSERT_EQ_I(wm_sim_press(0, 0), 2);
    return 0;
}

/* The entry timer gates input: a direction pressed before timer > 10 is
 * ignored (the map is still easing in). */
int test_scene_worldmap_entry_timer_gates_input(void)
{
    wm_init_with(1, 0, 0, 1, 2);
    scene_worldmap_set_sel_dest(2);
    title_save_dialog_reset();
    g_scene_state = SCENE_STATE_WORLDMAP;
    g_sim_buttons[0].held = 0; g_sim_buttons[0].pressed = 0;
    for (int i = 0; i < 3; i++) scene_worldmap_sim();   /* timer → 3 (< 10) */
    T_ASSERT_EQ_I(wm_sim_press(0x01, 0), 2);            /* Right ignored */
    return 0;
}

/* Z on a DISABLED destination (dest 2 is state 0 in the tutorial gate) plays
 * the denied SE and does NOT arm the exit — the scene stays mode 8 even after
 * winding what would be the fade. */
int test_scene_worldmap_z_disabled_no_exit(void)
{
    wm_sim_ready(2);
    (void)wm_sim_press(0, 0x10);                 /* Z on disabled dest */
    T_ASSERT_EQ_I(g_scene_state, SCENE_STATE_WORLDMAP);
    for (int i = 0; i < 25; i++) { fade_tick(); scene_worldmap_sim(); }
    T_ASSERT_EQ_I(g_scene_state, SCENE_STATE_WORLDMAP);  /* never transitions */
    return 0;
}

/* Z on an ENABLED destination (dest 3 Market, state 2) arms the dissolve fade;
 * the scene holds mode 8 through the fade, then transitions to the dest's mode
 * (Market → 6). Exercises the exit state machine + the dest→mode table. */
int test_scene_worldmap_z_enabled_transitions_to_dest_mode(void)
{
    wm_sim_ready(3);
    (void)wm_sim_press(0, 0x10);                 /* Z on Market → arm exit */
    T_ASSERT_EQ_I(g_scene_state, SCENE_STATE_WORLDMAP);  /* still fading */
    g_sim_buttons[0].pressed = 0;
    int transitioned = 0;
    for (int i = 0; i < 25 && !transitioned; i++) {
        fade_tick();
        scene_worldmap_sim();
        if (g_scene_state != SCENE_STATE_WORLDMAP) transitioned = 1;
    }
    T_ASSERT_EQ_I(g_scene_state, 6);   /* Market = mode 6 (FUN_00490e16(0)) */
    return 0;
}
