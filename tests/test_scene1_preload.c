/*
 * test_scene1_preload.c — host coverage for the C8e.smoke post-house
 * callback hook.  The hook itself fires from scene1_preload_house
 * (Win32-only) and isn't reachable from host tests; what's testable
 * here is the setter/getter contract that lives outside the #ifdef
 * _WIN32 block in scene1_preload.c.
 *
 * Coverage:
 *   - default null
 *   - setter round-trips
 *   - setter replaces previous
 *
 * Matches the scene1_shop_walker_set_pass_d_mesh test shape.
 */

#include "t.h"

#include "scene1_preload.h"

static void noop_cb_a(void) {}
static void noop_cb_b(void) {}

int test_scene1_preload_post_house_cb_default_is_null(void)
{
    /* Reset to known state in case a prior test left a stale callback. */
    scene1_preload_set_post_house_callback(NULL);
    T_ASSERT(scene1_preload_get_post_house_callback() == NULL);
    return 0;
}

int test_scene1_preload_post_house_cb_setter_round_trips(void)
{
    scene1_preload_set_post_house_callback(noop_cb_a);
    T_ASSERT(scene1_preload_get_post_house_callback() == noop_cb_a);
    scene1_preload_set_post_house_callback(NULL);
    T_ASSERT(scene1_preload_get_post_house_callback() == NULL);
    return 0;
}

int test_scene1_preload_post_house_cb_setter_replaces_previous(void)
{
    scene1_preload_set_post_house_callback(noop_cb_a);
    T_ASSERT(scene1_preload_get_post_house_callback() == noop_cb_a);
    scene1_preload_set_post_house_callback(noop_cb_b);
    T_ASSERT(scene1_preload_get_post_house_callback() == noop_cb_b);
    scene1_preload_set_post_house_callback(NULL);
    return 0;
}
