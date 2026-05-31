/*
 * test_collision_mesh.c — W4.1 collision-mesh ingestion + build.
 *
 * Pure-math tests (classification, per-triangle plane/AABB) are exact.
 * The vendor test parses the real HOUSE room mesh (shop_1st.x) and checks
 * the built collision extent against the retail HOUSE room geometry from
 * engine-quirks §62 (counter/back wall at pz≈8.9–9.5, far floor at pz≈−7.3,
 * player x-range −1.5…3.1) — confirming the ×0.2 scale + X-negation land the
 * mesh in the same world space the player walks in.
 */
#include "t.h"

#include <math.h>
#include <stdint.h>

#include "collision_mesh.h"
#include "xfile.h"

static int near_(float a, float b, float tol)
{
    return fabsf(a - b) <= tol;
}
#define NEAR(a, b) do { \
    if (!near_((a), (b), 1e-5f)) \
        T_FAIL("expected %s≈%g, got %g", #a, (double)(b), (double)(a)); \
} while (0)

static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

/* ─── 1. material-name → type classification ──────────────────────────── */
int test_collision_classify(void)
{
    /* HOUSE shop materials: numeric + nohit + xof_default. */
    T_ASSERT_EQ_I(collision_classify_material("nohit"), 4);
    T_ASSERT_EQ_I(collision_classify_material("Material__189_0"), 0);
    T_ASSERT_EQ_I(collision_classify_material("xof_default"), 0);
    T_ASSERT_EQ_I(collision_classify_material(""), 0);
    T_ASSERT_EQ_I(collision_classify_material(NULL), 0);

    /* Other-level keywords (FUN_00471d45 chain). */
    T_ASSERT_EQ_I(collision_classify_material("Plane"),    2);
    T_ASSERT_EQ_I(collision_classify_material("dame"),     2);
    T_ASSERT_EQ_I(collision_classify_material("hit"),      3);
    T_ASSERT_EQ_I(collision_classify_material("mizu"),     5);
    T_ASSERT_EQ_I(collision_classify_material("gake"),     6);
    T_ASSERT_EQ_I(collision_classify_material("kabe"),     7);
    T_ASSERT_EQ_I(collision_classify_material("toumei"),   7);
    T_ASSERT_EQ_I(collision_classify_material("hikari"),   4);
    T_ASSERT_EQ_I(collision_classify_material("crystal"), 15);
    T_ASSERT_EQ_I(collision_classify_material("taimatu"), 16);
    T_ASSERT_EQ_I(collision_classify_material("takara"),  13);
    T_ASSERT_EQ_I(collision_classify_material("taru"),    12);
    T_ASSERT_EQ_I(collision_classify_material("shokudai"),14);
    T_ASSERT_EQ_I(collision_classify_material("tree01"),   8);
    T_ASSERT_EQ_I(collision_classify_material("tree02"),   9);
    T_ASSERT_EQ_I(collision_classify_material("kusa01"),  10);
    T_ASSERT_EQ_I(collision_classify_material("kusa02"),  11);

    /* Prefix match: 'hit' must NOT match 'nohit' (different first char). */
    T_ASSERT_EQ_I(collision_classify_material("nohit_2"), 4);
    return 0;
}

/* ─── 2. per-triangle plane/AABB build (FUN_00432ac6) ─────────────────── */
int test_collision_tri_build_flat(void)
{
    /* Art-space (pre-negation) horizontal triangle in the XZ plane. */
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float c[3] = {0, 0, 1};
    collision_tri t;
    collision_tri_build(&t, a, b, c, 0, COLLISION_PAD_SMALL);

    /* Verts: X negated. */
    NEAR(t.v[0][0],  0); NEAR(t.v[1][0], -1); NEAR(t.v[2][0],  0);
    NEAR(t.v[0][2],  0); NEAR(t.v[1][2],  0); NEAR(t.v[2][2],  1);

    /* normal = (C−B)×(B−A) with negated X → (0,−1,0). */
    NEAR(t.n[0], 0); NEAR(t.n[1], -1); NEAR(t.n[2], 0);
    NEAR(t.d, 0);
    NEAR(t.nlen2, 1);

    /* AABB with SMALL pad: x[−1.5,0.5] y[−0.5,3.0] z[−0.5,1.5]. */
    NEAR(t.aabb_min[0], -1.5f); NEAR(t.aabb_max[0], 0.5f);
    NEAR(t.aabb_min[1], -0.5f); NEAR(t.aabb_max[1], 3.0f);
    NEAR(t.aabb_min[2], -0.5f); NEAR(t.aabb_max[2], 1.5f);
    T_ASSERT_EQ_I(t.type, 0);
    return 0;
}

int test_collision_tri_build_large_pad(void)
{
    float a[3] = {2, 1, 3};
    float b[3] = {2, 5, 3};
    float c[3] = {4, 1, 3};   /* a vertical-ish wall triangle */
    collision_tri t;
    collision_tri_build(&t, a, b, c, 7, COLLISION_PAD_LARGE);
    /* LARGE pad: x:±1 y:−5/+10 z:±1. Unpadded x[−4,−2] y[1,5] z[3,3]. */
    NEAR(t.aabb_min[0], -5.0f); NEAR(t.aabb_max[0], -1.0f);
    NEAR(t.aabb_min[1], -4.0f); NEAR(t.aabb_max[1], 15.0f);
    NEAR(t.aabb_min[2],  2.0f); NEAR(t.aabb_max[2],  4.0f);
    T_ASSERT_EQ_I(t.type, 7);
    return 0;
}

/* ─── 3. vendor: real HOUSE room mesh build + world-space extent ──────── */
int test_collision_vendor_shop_1st(void)
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
    if (xf->error[0]) { xfile_free(xf); T_FAIL("xfile parse error: %s", xf->error); }

    collision_object obj;
    int rc = collision_object_build(&obj, xf, COLLISION_PAD_SMALL);
    xfile_free(xf);
    T_ASSERT_EQ_I(rc, 0);

    /* The shop room is a large mesh. */
    T_ASSERT(obj.tri_count > 100);

    /* World-space extent must match the retail HOUSE room (engine-quirks
     * §62): back wall/counter near pz≈9, far floor near pz≈−7, and the
     * player x-corridor [−1.5,3.1] inside the x-extent. */
    fprintf(stderr, "  [shop_1st] tris=%d  x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n",
            obj.tri_count,
            (double)obj.aabb_min[0], (double)obj.aabb_max[0],
            (double)obj.aabb_min[1], (double)obj.aabb_max[1],
            (double)obj.aabb_min[2], (double)obj.aabb_max[2]);

    T_ASSERT(obj.aabb_max[2] > 8.0f);    /* counter / back wall */
    T_ASSERT(obj.aabb_min[2] < -6.0f);   /* far floor edge */
    T_ASSERT(obj.aabb_min[0] < -1.0f);   /* x-corridor left */
    T_ASSERT(obj.aabb_max[0] >  3.0f);   /* x-corridor right */

    collision_object_free(&obj);
    return 0;
}
