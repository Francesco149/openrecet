/*
 * test_map_collision.c — src/map_collision.c on a synthetic `.x`.  Retail
 * equality is the rhost shadow (mapcol_build / mapcol_ground / mapcol_ray,
 * `rhost.py verify`); these pin the parser's state machine and the query
 * semantics, quirks included, on exactly representable values.
 */
#include "t.h"

#include <string.h>

#include "floor_layout.h"
#include "map_collision.h"
#include "stage_palette.h"

/* Root frame identity, child frame x×2, one mesh: 4 verts, 3 faces, two
 * materials (gake → type 1, nohit → dropped).  CRLF like the game files. */
static const char k_box_x[] =
    "xof 0303txt 0032\r\n"
    "Frame Frame_World {\r\n"
    "\tFrameTransformMatrix {\r\n"
    "\t\t1.000000, 0.0, 0.0, 0.0, \r\n"
    "\t\t0.0, 1.000000, 0.0, 0.0, \r\n"
    "\t\t0.0, 0.0, 1.000000, 0.0, \r\n"
    "\t\t0.0, 0.0, 0.0, 1.000000;;\r\n"
    "\t}\r\n"
    "\r\n"
    "Frame Frame_Box {\r\n"
    "\tFrameTransformMatrix {\r\n"
    "\t\t2.000000, 0.0, 0.0, 0.0, \r\n"
    "\t\t0.0, 1.000000, 0.0, 0.0, \r\n"
    "\t\t0.0, 0.0, 1.000000, 0.0, \r\n"
    "\t\t0.0, 0.0, 0.0, 1.000000;;\r\n"
    "\t}\r\n"
    "\r\n"
    "Mesh Box {\r\n"
    "\t4;\r\n"
    "\t0.000000;0.000000;0.000000;,\r\n"
    "\t10.000000;0.000000;0.000000;,\r\n"
    "\t0.000000;0.000000;10.000000;,\r\n"
    "\t10.000000;0.000000;10.000000;;\r\n"
    "\t3;\r\n"
    "\t3;0,2,1;,\r\n"
    "\t3;1,3,2;,\r\n"
    "\t3;0,2,1;;\r\n"
    "\r\n"
    "\tMeshMaterialList {\r\n"
    "\t\t2;\r\n"
    "\t\t3;\r\n"
    "\t\t0,1,0;;\r\n"
    "\t\t{gake_01}\r\n"
    "\t\t{nohit_x}\r\n"
    "\t}\r\n";

static stage_palette_t s_pal;

static void setup(int maptype, int large_pad)
{
    memset(&g_mapcol_objs, 0, sizeof g_mapcol_objs);
    memset(&g_mapcol_movers, 0, sizeof g_mapcol_movers);
    memset(g_floor_rooms, 0, sizeof(floor_room_t) * FLOOR_GRID_CELLS);
    g_floor_c148 = 0;
    g_mapcol_wall_pass = 0;
    memset(&s_pal, 0, sizeof s_pal);
    s_pal.mode = maptype;
    g_stage_palette = &s_pal;
    mapcol_clear_all();
    mapcol_load_x(0, k_box_x, sizeof k_box_x - 1, large_pad);
    g_mapcol_objs.n1 = 1;   /* house: object 0 = slot 0 at the origin */
}

int test_map_collision_parse_types_and_vertices(void)
{
    setup(0, 0);
    const mapcol_slot_t *s = &g_mapcol_slot[0];
    T_ASSERT_EQ_I(s->count, 2);            /* the nohit face is dropped */
    T_ASSERT_EQ_I(s->count2, 2);
    T_ASSERT_EQ_I(s->tris[0].type, 1);     /* gake → 1 (retail's stale edx), not 6 */
    T_ASSERT_EQ_I(s->tris[1].type, 1);
    /* face 0 = (v0, v2, v1): x = −(x·2·0.2), z = z·0.2 */
    const mapcol_tri_t *t = &s->tris[0];
    T_ASSERT(t->vx[1] == 0.0f && t->vz[1] == 2.0f);
    T_ASSERT(t->vx[2] == -4.0f && t->vz[2] == 0.0f);
    T_ASSERT(t->plane[0] == 0.0f && t->plane[1] == 8.0f && t->plane[2] == 0.0f);
    T_ASSERT(t->nlen2 == 64.0f);
    T_ASSERT(t->bmin[0] == -4.5f && t->bmax[0] == 0.5f);   /* small pad */
    T_ASSERT(t->bmin[1] == -0.5f && t->bmax[1] == 3.0f);
    T_ASSERT(s->aabb[0] == -4.0f && s->aabb[3] == 0.0f && s->aabb[2] == 0.0f && s->aabb[5] == 2.0f);
    setup(0, 1);
    t = &g_mapcol_slot[0].tris[0];
    T_ASSERT(t->bmin[0] == -5.0f && t->bmax[0] == 1.0f);     /* large pad */
    T_ASSERT(t->bmin[1] == -5.0f && t->bmax[1] == 10.0f);
    return 0;
}

int test_map_collision_ground_house(void)
{
    setup(0, 0);
    mapcol_hit_t h;
    memset(&h, 0, sizeof h);
    T_ASSERT_EQ_I(mapcol_ground(-1.0f, 1.0f, 0.5f, &h), 1);
    T_ASSERT(h.height == 0.0f);
    T_ASSERT_EQ_I(h.slot, 0);
    T_ASSERT(h.n[0] == 0.0f && h.n[1] == 1.0f && h.n[2] == 0.0f);
    T_ASSERT(h.plane[1] == 8.0f);
    /* outside the triangle (x > 0): nothing in any pass */
    T_ASSERT_EQ_I(mapcol_ground(1.0f, 1.0f, 0.5f, &h), 0);
    return 0;
}

int test_map_collision_ground_far_below_quirk(void)
{
    /* large pad: a probe 7 above the floor is inside the triangle AABB.  Pass 0
     * rejects it (7 − 5 >= 0), pass 3 accepts any ground: retail returns 1. */
    setup(0, 1);
    mapcol_hit_t h;
    memset(&h, 0, sizeof h);
    T_ASSERT_EQ_I(mapcol_ground(-1.0f, 7.0f, 0.5f, &h), 1);
    T_ASSERT(h.height == 0.0f);
    return 0;
}

int test_map_collision_ray_house(void)
{
    setup(0, 0);
    mapcol_hit_t h;
    float frac = 0.0f;
    memset(&h, 0, sizeof h);
    T_ASSERT_EQ_I(mapcol_ray(-1.0f, 1.0f, 0.5f, 0.0f, -2.0f, 0.0f, &frac, &h), 1);
    T_ASSERT(frac == 0.5f);
    T_ASSERT(h.n[1] == 1.0f);
    for (int k = 0; k < 4; k++) T_ASSERT(h.plane[k] == 0.0f);   /* plane[0] ×4 */
    /* moving away from the floor: no hit */
    T_ASSERT_EQ_I(mapcol_ray(-1.0f, 1.0f, 0.5f, 0.0f, 2.0f, 0.0f, &frac, &h), 0);
    return 0;
}

int test_map_collision_dungeon_cell_wrap(void)
{
    /* dungeon: world x −41 is cell (1, 0), wrapped to local −1; the cell's room
     * model is the slot; pattern 2 mirrors x and z (local (1, −0.5): outside) */
    setup(1, 1);
    g_floor_rooms[1].type = 0;
    g_floor_rooms[1].pat = 0;
    g_floor_rooms[0].type = -1;
    mapcol_hit_t h;
    memset(&h, 0, sizeof h);
    T_ASSERT_EQ_I(mapcol_ground(-41.0f, 1.0f, 0.5f, &h), 1);
    T_ASSERT(h.height == 0.0f);
    g_floor_rooms[1].pat = 2;
    T_ASSERT_EQ_I(mapcol_ground(-41.0f, 1.0f, 0.5f, &h), 0);
    return 0;
}
