/*
 * test_scene1_hud.c — tests for src/scene1_hud.{c,h} (C7j).
 *
 * The Win32 render body (scene1_hud_render) needs a D3D8 device, so the
 * host build only links the pure helpers: the Pass-2 letterbox dead-zone
 * clamp, the Pass-1 backdrop colour packing + active predicate, and the
 * status-screen flag round-trip.
 */

#include "t.h"
#include "scene1_hud.h"

int test_scene1_hud_letterbox_default_is_zero(void)
{
    /* DAT_0438b1dc is BSS-zero at boot → bars retracted. */
    scene1_hud_set_letterbox_height(0.0f);
    T_ASSERT(scene1_hud_letterbox_height() == 0.0f);
    return 0;
}

int test_scene1_hud_letterbox_deadzone_clamps_to_zero(void)
{
    /* |h| <= 0.1 collapses to 0 (engine `-0.1 <= h <= 0.1`). */
    scene1_hud_set_letterbox_height(0.1f);
    T_ASSERT(scene1_hud_letterbox_height() == 0.0f);
    scene1_hud_set_letterbox_height(-0.1f);
    T_ASSERT(scene1_hud_letterbox_height() == 0.0f);
    scene1_hud_set_letterbox_height(0.05f);
    T_ASSERT(scene1_hud_letterbox_height() == 0.0f);
    return 0;
}

int test_scene1_hud_letterbox_above_deadzone_passes_through(void)
{
    /* Outside the dead-zone the height is returned verbatim (it later
     * scales the bar height by *32). */
    scene1_hud_set_letterbox_height(0.5f);
    T_ASSERT(scene1_hud_letterbox_height() == 0.5f);
    scene1_hud_set_letterbox_height(1.0f);
    T_ASSERT(scene1_hud_letterbox_height() == 1.0f);
    /* Negative beyond the dead-zone passes through too, but Pass 2 only
     * draws when > 0, so a negative height stays inert. */
    scene1_hud_set_letterbox_height(-0.5f);
    T_ASSERT(scene1_hud_letterbox_height() == -0.5f);
    scene1_hud_set_letterbox_height(0.0f); /* restore */
    return 0;
}

int test_scene1_hud_backdrop_color_predicate_true(void)
{
    /* pred != 0 → uVar7 = 0xff → ((0xff|0x3700)<<8|0xff)<<8|0xff. */
    T_ASSERT_EQ_U(scene1_hud_pass1_backdrop_color(1), 0x37ffffffu);
    return 0;
}

int test_scene1_hud_backdrop_color_predicate_false(void)
{
    /* pred == 0 → uVar7 = 200 (0xc8) → 0x37c8c8c8. */
    T_ASSERT_EQ_U(scene1_hud_pass1_backdrop_color(0), 0x37c8c8c8u);
    return 0;
}

int test_scene1_hud_pass1_dormant_in_house(void)
{
    /* HOUSE: stage_type == 0 → backdrop never active, regardless of the
     * predicate. */
    T_ASSERT_EQ_I(scene1_hud_pass1_backdrop_active(1, 0, 1), 0);
    T_ASSERT_EQ_I(scene1_hud_pass1_backdrop_active(1, 0, 0), 0);
    return 0;
}

int test_scene1_hud_pass1_active_only_ingame_dungeon(void)
{
    /* DUNGEON (stage_type > 0) + INGAME + predicate true → active. */
    T_ASSERT_EQ_I(scene1_hud_pass1_backdrop_active(1, 1, 1), 1);
    /* Not INGAME → inert even in a dungeon. */
    T_ASSERT_EQ_I(scene1_hud_pass1_backdrop_active(0, 1, 1), 0);
    /* DUNGEON + INGAME but predicate false (and DAT_056db104 zero) → inert. */
    T_ASSERT_EQ_I(scene1_hud_pass1_backdrop_active(1, 1, 0), 0);
    return 0;
}

int test_scene1_hud_status_screen_flag_roundtrip(void)
{
    /* Default open == 0 (dormant Pass 3 in HOUSE). */
    scene1_hud_set_status_screen_open(0);
    T_ASSERT_EQ_I(scene1_hud_status_screen_open(), 0);
    /* Any nonzero normalises to 1. */
    scene1_hud_set_status_screen_open(5);
    T_ASSERT_EQ_I(scene1_hud_status_screen_open(), 1);
    scene1_hud_set_status_screen_open(0); /* restore */
    return 0;
}
