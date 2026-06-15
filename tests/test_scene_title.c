/*
 * test_scene_title.c — title-scene module tests.
 *
 * Only the pure-C asset table is testable on Linux; the actual
 * sprite_load wiring is D3D-bound and exercised by the boot smoke.
 * What we check here:
 *   - 7 entries are defined in slot order with the expected paths
 *   - each expected_w/expected_h is a power of two (matches the
 *     engine's texture conventions)
 *   - the paths match exactly what the engine's .rdata stores
 */
#include "t.h"
#include "scene_title.h"
#include "scene.h"             /* g_scene_state */
#include "title_save_dialog.h" /* shared hand-cursor visibility */
#include "worker_load.h"       /* case-0 dispatch wiring */

#include <string.h>

static int is_pow2(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

int test_scene_title_assets_count_is_seven(void)
{
    /* Eight now — slot 7 (item_win.tga) was added when the title
     * settings submenu render landed, since the header-tab chrome
     * comes from item_win.tga and the engine loads it at boot-time
     * (context-1) rather than at title-scene load. Parked here
     * pragmatically until a boot-time texture module exists. */
    T_ASSERT_EQ_I(SCENE_TITLE_TEX_COUNT, 8);
    return 0;
}

int test_scene_title_assets_paths_match_pe(void)
{
    /* Strings extracted from the unpacked binary via
     *   `tools/analyze/pe.py str 0x005c8688 0x005c869c ...`
     * in slot order. */
    static const char *expected[] = {
        "bmp/title_bg2.bmp",
        "bmp/title01.tga",
        "bmp/title_fuki.tga",
        "bmp/title_waku.tga",
        "bmp/pause.tga",
        "bmp/result_bord01.tga",
        "bmp/dungeonbord.tga",
        "bmp/item_win.tga",
    };
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        if (strcmp(scene_title_assets[i].path, expected[i]) != 0) {
            T_FAIL("slot %d: got %s, want %s",
                   i, scene_title_assets[i].path, expected[i]);
        }
    }
    return 0;
}

int test_scene_title_assets_sizes_power_of_two(void)
{
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        const scene_title_asset_t *a = &scene_title_assets[i];
        if (!is_pow2(a->expected_w) || !is_pow2(a->expected_h)) {
            T_FAIL("slot %d (%s): non-pow2 size %ux%u",
                   i, a->path, a->expected_w, a->expected_h);
        }
    }
    return 0;
}

int test_scene_title_assets_sizes_match_engine(void)
{
    /* (w, h) pairs in the engine's FUN_0047193c call site. */
    static const uint32_t expected[][2] = {
        {1024, 1024},  /* title_bg2.bmp */
        { 512,  256},  /* title01.tga   */
        { 512, 1024},  /* title_fuki.tga */
        {1024,  512},  /* title_waku.tga */
        {1024,  512},  /* pause.tga */
        { 512,  256},  /* result_bord01.tga */
        {1024,  512},  /* dungeonbord.tga */
        {1024, 1024},  /* item_win.tga */
    };
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        T_ASSERT_EQ_U(scene_title_assets[i].expected_w, expected[i][0]);
        T_ASSERT_EQ_U(scene_title_assets[i].expected_h, expected[i][1]);
    }
    return 0;
}

/* ─── menu init ──────────────────────────────────────────────────────── */

static int items_eq(const scene_title_menu_t *m,
                    const int *expected, int n)
{
    if (m->count != n) return 0;
    for (int i = 0; i < n; i++) if (m->items[i] != expected[i]) return 0;
    return 1;
}

int test_scene_title_menu_fresh_boot_4_items(void)
{
    scene_title_menu_t m;
    scene_title_menu_init_fresh(&m);
    const int want[] = {
        SCENE_TITLE_MENU_NEW_GAME,
        SCENE_TITLE_MENU_RANKING,
        SCENE_TITLE_MENU_OPTIONS,
        SCENE_TITLE_MENU_EXIT,
    };
    if (!items_eq(&m, want, 4)) T_FAIL("unexpected menu layout");
    T_ASSERT_EQ_I(m.default_cursor, 0);
    /* count == 4 → stride 33, origin -16 (default branch). */
    if (m.y_stride != 33.0f) T_FAIL("y_stride=%g, want 33", (double)m.y_stride);
    if (m.y_origin != -16.0f) T_FAIL("y_origin=%g, want -16", (double)m.y_origin);
    return 0;
}

int test_scene_title_menu_has_save_no_adv8_6_items(void)
{
    /* uVar1 == 1: any-adv-cleared but no adv8 entries. */
    scene_title_save_t s = { .has_any_adv_cleared = 1 };
    scene_title_menu_t m;
    scene_title_menu_init(&s, &m);
    const int want[] = {
        SCENE_TITLE_MENU_CONT_HAS_SAVE,  /* 5 */
        SCENE_TITLE_MENU_NEW_HAS_SAVE,   /* 4 */
        SCENE_TITLE_MENU_RANKING,        /* 7 */
        SCENE_TITLE_MENU_HIDDEN_CHAR,    /* 8 — from (uVar1 & 1) implicit */
        SCENE_TITLE_MENU_OPTIONS,
        SCENE_TITLE_MENU_EXIT,
    };
    if (!items_eq(&m, want, 6)) T_FAIL("unexpected menu layout");
    /* count == 6 → stride 33, origin -30. */
    if (m.y_stride != 33.0f) T_FAIL("y_stride=%g, want 33", (double)m.y_stride);
    if (m.y_origin != -30.0f) T_FAIL("y_origin=%g, want -30", (double)m.y_origin);
    return 0;
}

int test_scene_title_menu_has_save_and_score_7_items(void)
{
    /* uVar1 == 1, plus a populated save bank → adds item 1 + cursor. */
    scene_title_save_t s = {
        .has_any_adv_cleared = 1,
        .has_any_score       = 1,
    };
    scene_title_menu_t m;
    scene_title_menu_init(&s, &m);
    const int want[] = {
        SCENE_TITLE_MENU_CONT_HAS_SAVE,  /* 5 */
        SCENE_TITLE_MENU_NEW_HAS_SAVE,   /* 4 */
        SCENE_TITLE_MENU_CONTINUE_ANY,   /* 1 — quick-continue slot */
        SCENE_TITLE_MENU_RANKING,        /* 7 */
        SCENE_TITLE_MENU_HIDDEN_CHAR,    /* 8 */
        SCENE_TITLE_MENU_OPTIONS,
        SCENE_TITLE_MENU_EXIT,
    };
    if (!items_eq(&m, want, 7)) T_FAIL("unexpected menu layout");
    T_ASSERT_EQ_I(m.default_cursor, 2);  /* points at item 1 */
    if (m.y_stride != 30.0f) T_FAIL("y_stride=%g, want 30", (double)m.y_stride);
    if (m.y_origin != -36.0f) T_FAIL("y_origin=%g, want -36", (double)m.y_origin);
    return 0;
}

int test_scene_title_menu_full_unlock_8_items(void)
{
    /* uVar1 == 3 + populated bank → all 8 menu items in canonical order. */
    scene_title_save_t s = {
        .has_any_adv_cleared  = 1,
        .has_any_adv8_cleared = 1,
        .has_any_score        = 1,
    };
    scene_title_menu_t m;
    scene_title_menu_init(&s, &m);
    const int want[] = {
        SCENE_TITLE_MENU_CONT_HAS_SAVE,  /* 5 */
        SCENE_TITLE_MENU_NEW_HAS_SAVE,   /* 4 */
        SCENE_TITLE_MENU_SURVIVAL,       /* 6 — adv-2 cleared */
        SCENE_TITLE_MENU_CONTINUE_ANY,   /* 1 */
        SCENE_TITLE_MENU_RANKING,        /* 7 */
        SCENE_TITLE_MENU_HIDDEN_CHAR,    /* 8 */
        SCENE_TITLE_MENU_OPTIONS,
        SCENE_TITLE_MENU_EXIT,
    };
    if (!items_eq(&m, want, 8)) T_FAIL("unexpected menu layout");
    T_ASSERT_EQ_I(m.default_cursor, 3);  /* item 1 is at index 3 */
    if (m.y_stride != 27.0f) T_FAIL("y_stride=%g, want 27", (double)m.y_stride);
    if (m.y_origin != -36.0f) T_FAIL("y_origin=%g, want -36", (double)m.y_origin);
    return 0;
}

int test_scene_title_menu_hidden_char_only_5_items(void)
{
    /* Hidden-char flag alone (no save) → adds item 8 to the fresh menu. */
    scene_title_save_t s = { .hidden_char_unlocked = 1 };
    scene_title_menu_t m;
    scene_title_menu_init(&s, &m);
    const int want[] = {
        SCENE_TITLE_MENU_NEW_GAME,
        SCENE_TITLE_MENU_RANKING,
        SCENE_TITLE_MENU_HIDDEN_CHAR,
        SCENE_TITLE_MENU_OPTIONS,
        SCENE_TITLE_MENU_EXIT,
    };
    if (!items_eq(&m, want, 5)) T_FAIL("unexpected menu layout");
    /* count == 5 → default branch: stride 33, origin -16. */
    if (m.y_stride != 33.0f) T_FAIL("y_stride=%g, want 33", (double)m.y_stride);
    if (m.y_origin != -16.0f) T_FAIL("y_origin=%g, want -16", (double)m.y_origin);
    return 0;
}

int test_scene_title_menu_survival_requires_both_flags(void)
{
    /* has_any_adv8_cleared alone (no has_any_adv_cleared) → uVar1 = 2,
     * which is NOT 3 — so no Survival entry. */
    scene_title_save_t s = { .has_any_adv8_cleared = 1 };
    scene_title_menu_t m;
    scene_title_menu_init(&s, &m);
    for (int i = 0; i < m.count; i++) {
        if (m.items[i] == SCENE_TITLE_MENU_SURVIVAL) {
            T_FAIL("Survival appeared in menu without adv-2 cleared bit");
        }
    }
    return 0;
}

/* ─── worker case-0 title re-init (FUN_0049a3a3 / scene_title_reinit) ──────
 *
 * The pause-menu Exit-to-title bug: the title resumed in a stale sub-state
 * (the boot Continue-picker, submenu_state==1) so the load-game card list
 * rendered instead of the resting main menu.  The engine re-inits the
 * title via the primary load worker case-0; these tests pin the reset. */

/* Put the title anim into a "deep in a stale sub-state" configuration —
 * what the picker/settings leave behind when a scene transitions away. */
static void seed_stale_title_anim(void)
{
    scene_title_anim_init_fresh(&g_scene_title_anim);
    g_scene_title_anim.submenu_state    = 1;   /* load-game picker open */
    g_scene_title_anim.cursor_anim      = 10;  /* fully folded out */
    g_scene_title_anim.menu_folding_out = 0;   /* folding IN (for a submenu) */
    g_scene_title_anim.continue_mode    = 1;   /* came in via Continue */
    g_scene_title_anim.frame_counter    = 0x1234;
    g_scene_title_anim.cursor_pos       = 7;
    g_scene_title_anim.select_phase     = 9;
}

int test_scene_title_reinit_clears_stale_submenu(void)
{
    seed_stale_title_anim();

    scene_title_reinit();

    /* The whole sub-state collapses back to the resting main menu. */
    T_ASSERT_EQ_I(g_scene_title_anim.submenu_state, 0);    /* DAT_09643524 */
    T_ASSERT_EQ_I(g_scene_title_anim.cursor_anim, 0);      /* DAT_09643520 */
    T_ASSERT_EQ_I(g_scene_title_anim.menu_folding_out, 1); /* DAT_09643528 */
    T_ASSERT_EQ_I(g_scene_title_anim.continue_mode, 0);    /* DAT_0438bed4 */
    T_ASSERT_EQ_I((int)g_scene_title_anim.frame_counter, 0);
    T_ASSERT_EQ_I((int)g_scene_title_anim.select_phase, 0);
    return 0;
}

int test_scene_title_reinit_rebuilds_menu_and_hides_cursor(void)
{
    /* A visible hand cursor (left over from an in-game scene) must be
     * hidden — the title menu draws its own fuki cursor glyph. */
    title_save_dialog_cursor_set_visible(1);
    seed_stale_title_anim();
    g_scene_title_menu.count = 0;   /* clobber so the rebuild is observable */

    scene_title_reinit();

    /* The menu is rebuilt from the (test-empty) save banks → the fresh
     * 4-item title list (New Game / Ranking / Options / Exit), and the
     * cursor lands on the default. */
    T_ASSERT_EQ_I(g_scene_title_menu.count, 4);
    T_ASSERT_EQ_I((int)g_scene_title_anim.cursor_pos,
                  g_scene_title_menu.default_cursor);
    T_ASSERT_EQ_I(title_save_dialog_cursor_get_visible(), 0);
    return 0;
}

int test_scene_title_reinit_runs_via_worker_case0(void)
{
    /* End-to-end wiring: registering scene_title_reinit as worker case-0
     * and dispatching with scene_state==0 (the path the pause Exit takes:
     * scene→0 + worker_load_spawn) must re-init the title. */
    worker_load_reset();
    worker_load_set_cb(0, scene_title_reinit);

    g_scene_state = 0;              /* TITLE — what the dispatch reads */
    seed_stale_title_anim();

    const int ran = worker_load_dispatch_pure(g_scene_state);

    T_ASSERT_EQ_I(ran, 1);                                 /* a cb was found */
    T_ASSERT_EQ_I(g_scene_title_anim.submenu_state, 0);    /* and it re-init'd */
    T_ASSERT_EQ_I(g_scene_title_anim.menu_folding_out, 1);
    return 0;
}
