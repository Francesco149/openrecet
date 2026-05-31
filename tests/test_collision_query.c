/*
 * test_collision_query.c — W4.2 point→triangle ground query (FUN_00432e50).
 *
 * Synthetic single-floor cases are exact.  The vendor case loads the real
 * HOUSE room mesh and confirms the floor is found under an interior point and
 * absent off the room — the implicit-wall behaviour the slide-resolver relies
 * on (walking off the floor edge = blocked).
 */
#include "t.h"

#include <math.h>

#include "collision_mesh.h"
#include "collision_query.h"
#include "xfile.h"

static int near_(float a, float b, float tol) { return fabsf(a - b) <= tol; }
#define NEAR(a, b) do { \
    if (!near_((a), (b), 1e-4f)) \
        T_FAIL("expected %s≈%g, got %g", #a, (double)(b), (double)(a)); \
} while (0)

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

/* Build a one-object mesh holding a single up-facing floor triangle spanning
 * XZ ⊂ {x∈[−10,0], z∈[0,10]} at y=0 (winding chosen so the stored normal is
 * +Y, i.e. the query's above-plane gate passes for a point above it). */
static void make_floor(collision_object *o, collision_tri *t)
{
    float a[3] = {0, 0, 0};
    float b[3] = {0, 0, 10};
    float c[3] = {10, 0, 0};
    collision_tri_build(t, a, b, c, 0, COLLISION_PAD_SMALL);
    o->tris = t;
    o->tri_count = 1;
}

/* ─── 1. type exclusion (FUN_00432e50 L157-160) ───────────────────────── */
int test_query_type_excluded(void)
{
    T_ASSERT_EQ_I(collision_query_type_excluded(0), 0);
    T_ASSERT_EQ_I(collision_query_type_excluded(1), 0);
    T_ASSERT_EQ_I(collision_query_type_excluded(2), 0);
    T_ASSERT_EQ_I(collision_query_type_excluded(3), 0);
    T_ASSERT_EQ_I(collision_query_type_excluded(5), 0);
    T_ASSERT_EQ_I(collision_query_type_excluded(6), 0);
    T_ASSERT_EQ_I(collision_query_type_excluded(7), 1);     /* wall-pass off */
    for (int ty = 8; ty <= 16; ty++)
        T_ASSERT_EQ_I(collision_query_type_excluded(ty), 1);
    return 0;
}

/* ─── 2. synthetic floor hit / normal / height ────────────────────────── */
int test_query_floor_hit(void)
{
    collision_object o; collision_tri t;
    make_floor(&o, &t);

    collision_hit h;
    T_ASSERT_EQ_I(collision_query_ground_object(&o, -2.0f, 1.0f, 2.0f, &h), 1);
    T_ASSERT_EQ_I(h.hit, 1);
    NEAR(h.height, 0.0f);
    NEAR(h.normal[0], 0.0f);
    NEAR(h.normal[1], 1.0f);     /* up-facing */
    NEAR(h.normal[2], 0.0f);
    return 0;
}

/* Outside the XZ projection → no ground. */
int test_query_floor_miss_outside(void)
{
    collision_object o; collision_tri t;
    make_floor(&o, &t);
    collision_hit h;
    T_ASSERT_EQ_I(collision_query_ground_object(&o, 5.0f, 1.0f, 2.0f, &h), 0);
    T_ASSERT_EQ_I(h.hit, 0);
    return 0;
}

/* Floor more than 5 units below the query Y → not counted (FUN_00432e50 L265). */
int test_query_floor_miss_too_high(void)
{
    collision_object o; collision_tri t;
    make_floor(&o, &t);
    collision_hit h;
    T_ASSERT_EQ_I(collision_query_ground_object(&o, -2.0f, 10.0f, 2.0f, &h), 0);
    return 0;
}

/* Point below the floor (negative-normal side) → above-plane gate rejects. */
int test_query_below_floor_rejected(void)
{
    collision_object o; collision_tri t;
    make_floor(&o, &t);
    collision_hit h;
    T_ASSERT_EQ_I(collision_query_ground_object(&o, -2.0f, -1.0f, 2.0f, &h), 0);
    return 0;
}

/* ─── 3. vendor: real HOUSE floor present under interior, absent off-room ─ */
int test_query_vendor_shop_floor(void)
{
    char path[512];
    snprintf(path, sizeof path, "%s/vendor/original/xfile/shop/shop_1st.x",
             OPENRECET_ROOT);
    size_t len = 0;
    char *buf = slurp(path, &len);
    if (!buf) T_SKIP("vendor file %s missing", path);
    xfile_t *xf = xfile_parse(buf, len, path);
    free(buf);
    T_ASSERT(xf != NULL);

    collision_object obj;
    int rc = collision_object_build(&obj, xf, COLLISION_PAD_SMALL);
    xfile_free(xf);
    T_ASSERT_EQ_I(rc, 0);

    /* Interior point near the room origin (within the player corridor): a floor
     * should be found close to y=0. */
    collision_hit h;
    int got = collision_query_ground_object(&obj, 0.0f, 2.0f, 0.0f, &h);
    fprintf(stderr, "  [shop floor @ (0,2,0)] hit=%d height=%.3f n=(%.2f,%.2f,%.2f)\n",
            got, (double)h.height,
            (double)h.normal[0], (double)h.normal[1], (double)h.normal[2]);
    T_ASSERT_EQ_I(got, 1);
    T_ASSERT(fabsf(h.height) < 3.0f);     /* floor near y=0 */
    T_ASSERT(h.normal[1] > 0.5f);         /* roughly upward */

    /* Far outside the room → no floor (the implicit-wall case). */
    T_ASSERT_EQ_I(collision_query_ground_object(&obj, 0.0f, 2.0f, 100.0f, &h), 0);

    collision_object_free(&obj);
    return 0;
}
