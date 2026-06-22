/*
 * test_collision_resolve.c — W4.3 slide-resolve (raycast + radial push).
 *
 * Synthetic raycast cases are exact.  The vendor case confirms the room-wall
 * standoff works against the real mesh (the player pins instead of running
 * off).  The round-table block is SKIPPED pending the furniture world-placement
 * table (DAT_0438c058 / FUN_00436f97 stage_positions) — see PROGRESS W4.3
 * BLOCKED: the furniture meshes (table, vending machines) parse at local origin
 * and have no world placement yet, so only the room (obj 0, placed via its own
 * .x frame transforms) participates correctly.
 */
#include "t.h"

#include <math.h>

#include "collision_mesh.h"
#include "collision_resolve.h"
#include "scene1_particles_tick.h"   /* g_scene1_player_ground_y (engine daf88) */
#include "xfile.h"

static int near_(float a, float b, float tol) { return fabsf(a - b) <= tol; }
#define NEAR(a, b) do { \
    if (!near_((a), (b), 1e-4f)) \
        T_FAIL("expected %s≈%g, got %g", #a, (double)(b), (double)(a)); \
} while (0)

/* ─── 1. raycast against a single vertical wall (synthetic, exact) ─────── */
/* Wall in the YZ plane at x=2 (world, X negated): a quad as two triangles,
 * facing −X (normal points toward −X, the approaching side). */
static void make_wall(collision_object *o, collision_tri t[2])
{
    /* Art-space verts (X negated in the record).  Build a wall at world x=−2
     * spanning z∈[−5,5], y∈[0,4], normal facing +X (toward the origin). */
    float a[3] = {2, 0, -5}, b[3] = {2, 4, -5}, c[3] = {2, 0, 5};
    float d[3] = {2, 4, 5};
    collision_tri_build(&t[0], a, b, c, 0, COLLISION_PAD_SMALL);
    collision_tri_build(&t[1], c, b, d, 0, COLLISION_PAD_SMALL);
    o->tris = t; o->tri_count = 2;
}

int test_raycast_type_excluded(void)
{
    T_ASSERT_EQ_I(collision_raycast_type_excluded(0), 0);
    T_ASSERT_EQ_I(collision_raycast_type_excluded(1), 0);
    T_ASSERT_EQ_I(collision_raycast_type_excluded(3), 0);
    T_ASSERT_EQ_I(collision_raycast_type_excluded(5), 1);
    T_ASSERT_EQ_I(collision_raycast_type_excluded(6), 1);
    for (int ty = 8; ty <= 16; ty++)
        T_ASSERT_EQ_I(collision_raycast_type_excluded(ty), 1);
    return 0;
}

int test_raycast_hits_wall(void)
{
    collision_object o; collision_tri t[2];
    make_wall(&o, t);
    collision_mesh m; m.objects = &o; m.object_count = 1;

    /* From world x=−2.5, shoot +X (toward the wall at x=−2): hits at frac 0.5
     * of a length-1.0 ray. */
    float pos[3] = { -2.5f, 1.0f, 0.0f };
    float dir[3] = { 1.0f, 0.0f, 0.0f };
    float frac, n[3]; int type;
    T_ASSERT_EQ_I(collision_raycast(&m, pos, dir, &frac, n, &type), 1);
    NEAR(frac, 0.5f);
    T_ASSERT_EQ_I(type, 0);
    /* Wall normal is near-horizontal → |n.y| small (the radial-push gate). */
    T_ASSERT(fabsf(n[1]) < 0.75f);
    return 0;
}

int test_raycast_misses_short(void)
{
    collision_object o; collision_tri t[2];
    make_wall(&o, t);
    collision_mesh m; m.objects = &o; m.object_count = 1;
    /* Ray too short to reach the wall (length 0.3, wall 0.5 away). */
    float pos[3] = { -2.5f, 1.0f, 0.0f };
    float dir[3] = { 0.3f, 0.0f, 0.0f };
    float frac, n[3]; int type;
    T_ASSERT_EQ_I(collision_raycast(&m, pos, dir, &frac, n, &type), 0);
    return 0;
}

int test_raycast_misses_behind(void)
{
    collision_object o; collision_tri t[2];
    make_wall(&o, t);
    collision_mesh m; m.objects = &o; m.object_count = 1;
    /* Shoot away from the wall (−X): no hit (wall is behind). */
    float pos[3] = { -2.5f, 1.0f, 0.0f };
    float dir[3] = { -1.0f, 0.0f, 0.0f };
    float frac, n[3]; int type;
    T_ASSERT_EQ_I(collision_raycast(&m, pos, dir, &frac, n, &type), 0);
    return 0;
}

/* ─── 2. vendor: the room wall blocks the player (standoff approximate) ── */
static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

int test_resolve_room_wall_blocks(void)
{
    char path[512];
    snprintf(path, sizeof path, "%s/vendor/original/xfile/shop/shop_1st.x", OPENRECET_ROOT);
    size_t len = 0; char *buf = slurp(path, &len);
    if (!buf) T_SKIP("vendor shop_1st.x missing");
    xfile_t *xf = xfile_parse(buf, len, path);
    free(buf);
    T_ASSERT(xf != NULL);
    collision_object obj;
    collision_object_build(&obj, xf, COLLISION_PAD_SMALL);
    xfile_free(xf);
    collision_mesh m; m.objects = &obj; m.object_count = 1;

    /* Walk RIGHT (+X) into the right wall: the radial push must pin the player
     * (not run it off toward the +X room boundary at x=45).  The exact standoff
     * (retail px≈2.29) is not yet bit-exact — see PROGRESS W4.3. */
    float pos[3] = { 1.0f, 0.0f, 5.0f };
    for (int i = 0; i < 60; i++) {
        float vel[3] = { 0.175f, 0.0f, 0.0f };
        collision_resolve_player(&m, pos, vel, /*palette_mode=*/0);  /* HOUSE: 20 rays */
    }
    fprintf(stderr, "  [room wall RIGHT] px=%.4f (retail 3.102, 1:1)\n", (double)pos[0]);
    /* Penetration-scaled radial push (FUN_00483170): the player settles AGAINST
     * the wall at px≈3.102 (retail pin for this approach — runs/wall-retail),
     * not bouncing off.  Tight window around the retail value. */
    T_ASSERT(pos[0] > 3.08f && pos[0] < 3.13f);

    collision_object_free(&obj);
    return 0;
}

/* ─── 3. player ground_y (engine daf88) wiring — RE §18.4 / viewer note #1 ─
 * The companion free-roam hover, the contact shadow, and the floor-pinned
 * particles read g_scene1_player_ground_y = the floor under the player.  It was
 * never written (stuck 0) so during the post-haggle wrap-up cutscene the
 * companion hovered at the default-floor 3.0 instead of retail's frozen
 * counter-platform height ground_y(≈1.27)+3 ≈ 4.35.  Two writers now set it,
 * mirroring the engine (FUN_00483170 free-roam + the cc08 block writer). */

/* up-facing floor triangle at world y=`y`, XZ ⊂ {x∈[−10,0], z∈[0,10]}. */
static void make_floor_at(collision_object *o, collision_tri *t, float y)
{
    float a[3] = {0, y, 0};
    float b[3] = {0, y, 10};
    float c[3] = {10, y, 0};
    collision_tri_build(t, a, b, c, 0, COLLISION_PAD_SMALL);
    o->tris = t; o->tri_count = 1;
}

int test_ground_y_set_from_floor_under_stool(void)
{
    /* cc08 arrival-arm writer: the player rides the stool (py=0.5) ABOVE the
     * raised counter floor (y=1.272), yet ground_y must be the FLOOR (the +1.5
     * head-probe still sees it), NOT the player Y — this is the value the wrap-up
     * cutscene freezes so the companion hovers at ground_y+3. */
    collision_object o; collision_tri t;
    make_floor_at(&o, &t, 1.272f);
    collision_mesh m; m.objects = &o; m.object_count = 1;

    g_scene1_player_ground_y = -999.0f;
    float pos[3] = { -2.0f, 0.5f, 2.0f };          /* on the stool, over the floor */
    collision_set_player_ground_y(&m, pos);
    NEAR(g_scene1_player_ground_y, 1.272f);

    /* off the floor (the implicit-wall region) → no query hit → value unchanged
     * (the engine likewise only updates daf88 when a floor is found). */
    g_scene1_player_ground_y = 7.0f;
    float off[3] = { 5.0f, 0.5f, 2.0f };           /* x>0 = outside the floor span */
    collision_set_player_ground_y(&m, off);
    NEAR(g_scene1_player_ground_y, 7.0f);
    return 0;
}

int test_resolve_player_records_ground_y(void)
{
    /* free-roam writer: collision_resolve_player snaps Y to the floor AND records
     * it in g_scene1_player_ground_y (engine FUN_00483170 daf88 write). */
    collision_object o; collision_tri t;
    make_floor_at(&o, &t, 0.0f);
    collision_mesh m; m.objects = &o; m.object_count = 1;

    g_scene1_player_ground_y = -999.0f;
    float pos[3] = { -2.0f, 0.0f, 2.0f };
    float vel[3] = { 0.0f, 0.0f, 0.0f };
    collision_resolve_player(&m, pos, vel, /*palette_mode=*/0);
    NEAR(pos[1], 0.0f);
    NEAR(g_scene1_player_ground_y, 0.0f);
    return 0;
}

/* ─── 4. round-table block: BLOCKED on furniture placement ────────────── */
int test_resolve_table_blocks(void)
{
    T_SKIP("furniture world-placement (DAT_0438c058 / FUN_00436f97 stage_positions) "
           "unported — table mesh parses at local origin; see PROGRESS W4.3 BLOCKED");
    return 2;
}
