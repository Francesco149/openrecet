/*
 * test_scene1_chr_sprite.c — Cchr.2b coverage.
 *
 * Exercises chr_sprite_build_quads (the pure per-cell geometry of engine
 * FUN_0045a56f) against synthetic descriptor + formdata, checking the
 * vertex layout, UVs, world positions, the facing flip, the color/alpha
 * gate, and the bounds guards.
 *
 * The synthetic sprite: char 0, sheet_w=256 (8 cells/row), scale=1.0,
 * y_origin=0.  Its anim-0 frame-0 LUT entry is 5 in every facing bank
 * (frame line "5,5,5,5,5,5"), so the formdata frame-entry index is 5
 * regardless of facing.  formdata is laid out with base=0:
 *
 *   +0x40a = ncells = 2          (be_u16 at base + cell*2 + 0x400, cell=5)
 *   +0x60a = start  = 3          (be_u16 at base + cell*2 + 0x600)
 *   +0x806 = sheet_pos[3] = 9    (be_u16 at base + atlas_idx*2 + 0x800)
 *   +0x808 = sheet_pos[4] = 10
 */
#include "t.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "chr_sprite_meta.h"
#include "scene1_chr_sprite.h"

#define TEX_W 512
#define TEX_H 512

static int near_f(float a, float b)
{
    return fabsf(a - b) <= 1e-4f * (1.0f + fabsf(b));
}
#define T_ASSERT_NEAR(a, b) do { \
    float _a = (a), _b = (b); \
    if (!near_f(_a, _b)) \
        T_FAIL("expected %s ≈ %s (got %.7f, want %.7f)", #a, #b, _a, _b); \
} while (0)

static const char *const IDX =
    "recette_sheet\n"
    "1,5\n"
    "256,8\n"   /* sheet_w = 256, cells_per_row = 8 */
    "0\n"       /* y_origin = 0 */
    "100\n"     /* scale_x100 = 100 → scale 1.0 */
    "extra\n"
    "/\n"
    "5,5,5,5,5,5\n";  /* anim0 frame0: cell index 5 in every facing bank */

/* be writers into the formdata blob */
static void be16(uint8_t *p, int v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

#define FD_SIZE 0x900
static uint8_t g_fd[FD_SIZE];

static int setup(void)
{
    chr_meta_shutdown();
    if (!chr_meta_alloc())
        return 0;
    chr_meta_parse_idx(0, IDX);

    memset(g_fd, 0, sizeof(g_fd));
    /* base (be_u32 at formdata[char_id*4=0]) left as 0 */
    be16(g_fd + 0 + 5 * 2 + 0x400, 2);   /* ncells */
    be16(g_fd + 0 + 5 * 2 + 0x600, 3);   /* start  */
    be16(g_fd + 0 + 3 * 2 + 0x800, 9);   /* sheet_pos for atlas_idx 3 */
    be16(g_fd + 0 + 4 * 2 + 0x800, 10);  /* sheet_pos for atlas_idx 4 */
    return 1;
}
#define SETUP() do { if (!setup()) T_FAIL("setup failed"); } while (0)

/* A standing actor: anim 0, frame 0, age 0 (no shimmer), flag7>=1. */
static void mk_actor(int32_t actor[0x11], int facing)
{
    memset(actor, 0, 0x11 * sizeof(int32_t));
    actor[CHR_ACTOR_ANIM]   = 0;
    actor[CHR_ACTOR_FRAME]  = 0;
    actor[CHR_ACTOR_FACING] = facing;
    actor[CHR_ACTOR_FLAG7]  = 1;   /* RGB forced white */
    actor[CHR_ACTOR_AGE]    = 0;
}

int test_chr_sprite_ncells_and_count(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 0);
    chr_sprite_vertex v[CHR_SPRITE_MAX_VERTS];
    int n = chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                                   0xff000000u, g_fd, FD_SIZE, TEX_W, TEX_H);
    /* 2 cells → 2 quads → 12 verts (TRILIST count = ncells*2 = 4 tris). */
    T_ASSERT_EQ_I(n, 2);
    chr_meta_shutdown();
    return 0;
}

int test_chr_sprite_flipped_geometry(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 0);              /* facing 0 → bank 0, flip 1 */
    chr_sprite_vertex v[CHR_SPRITE_MAX_VERTS];
    int n = chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                                   0xff000000u, g_fd, FD_SIZE, TEX_W, TEX_H);
    T_ASSERT_EQ_I(n, 2);

    /* Cell 0: atlas_idx=3, sheet_pos=9 → col 1, row 1. px=32, x0=-96, py=-32.
     * acol = 3%16 = 3, arow = (3/16)<<5 = 0.
     * Flipped: p = sheet_w/2 - px = 128-32 = 96 (row 1 > cpr/2=4? no → p-0).
     *   edge_a = (96-32) = 64 ;  edge_b = 96.
     * topY = -32 ; botY = -64. */
    float v_top = (0 + 0.5f) / TEX_H;
    float v_bot = (0 + 32.0f - 0.5f) / TEX_H;
    float u_left  = (3 * 32 + 0.5f) / TEX_W;
    float u_right = ((3 + 1) * 32.0f - 0.5f) / TEX_W;

    /* Emission order V0,V1,V2,V3,V0,V2. */
    /* V0 = (edge_a, topY, u_right, v_top) */
    T_ASSERT_NEAR(v[0].x, 64.0f);  T_ASSERT_NEAR(v[0].y, -32.0f);
    T_ASSERT_NEAR(v[0].z, 0.0f);
    T_ASSERT_NEAR(v[0].u, u_right); T_ASSERT_NEAR(v[0].v, v_top);
    T_ASSERT_EQ_U(v[0].diffuse, 0xffffffffu);  /* flag7 → RGB white, A kept */
    /* V1 = (edge_a, botY, u_right, v_bot) */
    T_ASSERT_NEAR(v[1].x, 64.0f);  T_ASSERT_NEAR(v[1].y, -64.0f);
    T_ASSERT_NEAR(v[1].u, u_right); T_ASSERT_NEAR(v[1].v, v_bot);
    /* V2 = (edge_b, botY, u_left, v_bot) */
    T_ASSERT_NEAR(v[2].x, 96.0f);  T_ASSERT_NEAR(v[2].y, -64.0f);
    T_ASSERT_NEAR(v[2].u, u_left);  T_ASSERT_NEAR(v[2].v, v_bot);
    /* V3 = (edge_b, topY, u_left, v_top) */
    T_ASSERT_NEAR(v[3].x, 96.0f);  T_ASSERT_NEAR(v[3].y, -32.0f);
    T_ASSERT_NEAR(v[3].u, u_left);  T_ASSERT_NEAR(v[3].v, v_top);
    /* V4 == V0, V5 == V2 (replicated) */
    T_ASSERT_NEAR(v[4].x, v[0].x);  T_ASSERT_NEAR(v[4].y, v[0].y);
    T_ASSERT_NEAR(v[4].u, v[0].u);  T_ASSERT_NEAR(v[4].v, v[0].v);
    T_ASSERT_NEAR(v[5].x, v[2].x);  T_ASSERT_NEAR(v[5].y, v[2].y);
    T_ASSERT_NEAR(v[5].u, v[2].u);  T_ASSERT_NEAR(v[5].v, v[2].v);
    chr_meta_shutdown();
    return 0;
}

int test_chr_sprite_unflipped_geometry(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 5);              /* facing 5 → bank 3, flip 0 */
    chr_sprite_vertex v[CHR_SPRITE_MAX_VERTS];
    int n = chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                                   0xff000000u, g_fd, FD_SIZE, TEX_W, TEX_H);
    T_ASSERT_EQ_I(n, 2);

    /* Cell 0: same sheet_pos=9 (cell index is 5 in every bank), so col 1,
     * row 1, x0=-96.  Non-flipped: p = x0 - shimmer = -96.
     *   edge_a = (-96+32) = -64 ;  edge_b = -96. */
    T_ASSERT_NEAR(v[0].x, -64.0f);   /* V0/V1 = right pixel edge */
    T_ASSERT_NEAR(v[1].x, -64.0f);
    T_ASSERT_NEAR(v[2].x, -96.0f);   /* V2/V3 = left pixel edge */
    T_ASSERT_NEAR(v[3].x, -96.0f);
    /* UVs unchanged by flip: right edge still samples u_right. */
    float u_right = ((3 + 1) * 32.0f - 0.5f) / TEX_W;
    float u_left  = (3 * 32 + 0.5f) / TEX_W;
    T_ASSERT_NEAR(v[0].u, u_right);
    T_ASSERT_NEAR(v[2].u, u_left);
    chr_meta_shutdown();
    return 0;
}

int test_chr_sprite_second_cell_atlas_advance(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 5);              /* non-flipped for simpler X */
    chr_sprite_vertex v[CHR_SPRITE_MAX_VERTS];
    chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                           0xff000000u, g_fd, FD_SIZE, TEX_W, TEX_H);

    /* Cell 1: atlas_idx = start+1 = 4, sheet_pos=10 → col 2, row 1.
     * px = 64, x0 = -128+64 = -64.  Non-flipped edge_a=(-64+32)=-32,
     * edge_b=-64.  acol = 4%16 = 4, arow = (4/16)<<5 = 0. */
    chr_sprite_vertex *c1 = &v[6];   /* second quad starts at vert 6 */
    float u_right = ((4 + 1) * 32.0f - 0.5f) / TEX_W;
    float u_left  = (4 * 32 + 0.5f) / TEX_W;
    T_ASSERT_NEAR(c1[0].x, -32.0f);
    T_ASSERT_NEAR(c1[2].x, -64.0f);
    T_ASSERT_NEAR(c1[0].u, u_right);
    T_ASSERT_NEAR(c1[2].u, u_left);
    chr_meta_shutdown();
    return 0;
}

int test_chr_sprite_color_gate_white(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 0);
    actor[CHR_ACTOR_FLAG7] = 1;            /* >=1 → RGB | 0xffffff */
    chr_sprite_vertex v[CHR_SPRITE_MAX_VERTS];
    chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                           0x80123456u, g_fd, FD_SIZE, TEX_W, TEX_H);
    T_ASSERT_EQ_U(v[0].diffuse, 0x80ffffffu);  /* alpha kept, RGB → white */
    chr_meta_shutdown();
    return 0;
}

int test_chr_sprite_color_gate_tint(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 0);
    actor[CHR_ACTOR_FLAG7] = 0;             /* <1 → consider [8]/[9] */
    actor[CHR_ACTOR_FLAG8] = 1;             /* >0 */
    actor[CHR_ACTOR_FLAG9] = 0;             /* ==0 → (c & 0xff9f209f)|0x9f209f */
    chr_sprite_vertex v[CHR_SPRITE_MAX_VERTS];
    chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                           0xffffffffu, g_fd, FD_SIZE, TEX_W, TEX_H);
    T_ASSERT_EQ_U(v[0].diffuse, (0xffffffffu & 0xff9f209fu) | 0x9f209fu);
    chr_meta_shutdown();
    return 0;
}

int test_chr_sprite_color_gate_unchanged(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 0);
    actor[CHR_ACTOR_FLAG7] = 0;
    actor[CHR_ACTOR_FLAG8] = 0;             /* not >0 → no change */
    actor[CHR_ACTOR_FLAG9] = 0;
    chr_sprite_vertex v[CHR_SPRITE_MAX_VERTS];
    chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                           0x12345678u, g_fd, FD_SIZE, TEX_W, TEX_H);
    T_ASSERT_EQ_U(v[0].diffuse, 0x12345678u);
    chr_meta_shutdown();
    return 0;
}

int test_chr_sprite_null_and_degenerate_safe(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 0);
    chr_sprite_vertex v[CHR_SPRITE_MAX_VERTS];
    T_ASSERT_EQ_I(chr_sprite_build_quads(NULL, CHR_SPRITE_MAX_VERTS, actor, 0,
                                         0u, g_fd, FD_SIZE, TEX_W, TEX_H), 0);
    T_ASSERT_EQ_I(chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, NULL, 0,
                                         0u, g_fd, FD_SIZE, TEX_W, TEX_H), 0);
    T_ASSERT_EQ_I(chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                                         0u, NULL, FD_SIZE, TEX_W, TEX_H), 0);
    T_ASSERT_EQ_I(chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                                         0u, g_fd, FD_SIZE, 0, TEX_H), 0);
    /* truncated formdata: the +0x600 read falls past the end → 0, no OOB. */
    T_ASSERT_EQ_I(chr_sprite_build_quads(v, CHR_SPRITE_MAX_VERTS, actor, 0,
                                         0u, g_fd, 0x100, TEX_W, TEX_H), 0);
    chr_meta_shutdown();
    return 0;
}

int test_chr_sprite_out_max_clamp(void)
{
    SETUP();
    int32_t actor[0x11];
    mk_actor(actor, 0);
    chr_sprite_vertex v[6];
    /* room for only 1 quad; ncells still reported as 2, 2nd quad dropped. */
    int n = chr_sprite_build_quads(v, 6, actor, 0, 0xff000000u,
                                   g_fd, FD_SIZE, TEX_W, TEX_H);
    T_ASSERT_EQ_I(n, 2);
    /* first quad still written intact */
    T_ASSERT_NEAR(v[0].x, 64.0f);
    chr_meta_shutdown();
    return 0;
}
