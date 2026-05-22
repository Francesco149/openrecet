/*
 * test_mesh_load.c — unit tests for src/mesh_load.c (C5 of the mesh
 * loader chain).
 *
 * D3D8 upload + sprite_load wiring is Win32-only; covered manually
 * when scene_walls/floor/jutan light up at runtime. The pure-C bits
 * tested here:
 *
 *   1. Texture-name classifier: truth-table over the corpus's actual
 *      naming conventions (water_*, kabe_*, yuka_*, shop_jutan*,
 *      hikari*, w_*, n_*, uN_, vN_) — derived from a corpus scan, so
 *      drift in the classifier breaks the test.
 *   2. Cache reservation: dedupe semantics — same name yields the
 *      same slot, different names get different slots, flags freeze
 *      on first insert.
 *   3. mesh_load_from_buf end-to-end on a synthetic .x with a single
 *      textured material: texture_slots populated, cache count +1,
 *      flags stored.
 *   4. Vendor corpus walk: every file in xfile/ loads cleanly via
 *      mesh_load_from_buf, all texture_slots are >= -1 and < cache
 *      count, cache stays within 200 entries.
 */
#define _DEFAULT_SOURCE 1
#include "t.h"
#include "mesh.h"
#include "mesh_load.h"
#include "xfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── 1. classifier truth table ────────────────────────────────────────── */

int test_mesh_load_classify_water(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("water_01.tga", &f);
    T_ASSERT_EQ_I(f.water, 1);
    T_ASSERT_EQ_I(f.hikari, 0);
    T_ASSERT_EQ_I(f.kabe_, 0);
    T_ASSERT_EQ_I(f.yuka_, 0);
    T_ASSERT_EQ_I(f.shop_jutan, 0);
    T_ASSERT_EQ_I(f.ext_tga, 1);
    T_ASSERT_EQ_I(f.has_n_, 0);
    T_ASSERT_EQ_I(f.has_w_, 0);   /* "w_" not in "water_01.tga" */
    T_ASSERT_EQ_I(f.u_index, 0);  /* default */
    T_ASSERT_EQ_I(f.v_index, 0);
    return 0;
}

int test_mesh_load_classify_kabe(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("kabe_01.bmp", &f);
    T_ASSERT_EQ_I(f.kabe_, 1);
    T_ASSERT_EQ_I(f.water, 0);
    T_ASSERT_EQ_I(f.ext_tga, 0);
    return 0;
}

int test_mesh_load_classify_yuka_n(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("yuka_n_01.tga", &f);
    T_ASSERT_EQ_I(f.yuka_, 1);
    T_ASSERT_EQ_I(f.has_n_, 1);     /* "n_" appears at offset 5 */
    T_ASSERT_EQ_I(f.ext_tga, 1);
    return 0;
}

int test_mesh_load_classify_shop_jutan(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("shop_jutan_a.bmp", &f);
    T_ASSERT_EQ_I(f.shop_jutan, 1);
    T_ASSERT_EQ_I(f.kabe_, 0);
    return 0;
}

int test_mesh_load_classify_hikari(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("hikari_blue.tga", &f);
    T_ASSERT_EQ_I(f.hikari, 1);
    T_ASSERT_EQ_I(f.ext_tga, 1);
    return 0;
}

int test_mesh_load_classify_w_prefix(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("w_ice.bmp", &f);
    T_ASSERT_EQ_I(f.has_w_, 1);
    T_ASSERT_EQ_I(f.water, 0);     /* "w_" matches, "water" does not */
    return 0;
}

int test_mesh_load_classify_u_index(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("foo_u0_bar.tga", &f);
    T_ASSERT_EQ_I(f.u_index, 0);

    mesh_classify_texture_name("foo_u1_bar.tga", &f);
    T_ASSERT_EQ_I(f.u_index, 1);

    mesh_classify_texture_name("foo_u2_bar.tga", &f);
    T_ASSERT_EQ_I(f.u_index, 2);

    mesh_classify_texture_name("foo_u3_bar.tga", &f);
    T_ASSERT_EQ_I(f.u_index, 3);

    /* Multiple matches: last one wins. */
    mesh_classify_texture_name("u0_aaa_u3_bbb.tga", &f);
    T_ASSERT_EQ_I(f.u_index, 3);
    return 0;
}

int test_mesh_load_classify_v_index(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("foo_v2_bar.tga", &f);
    T_ASSERT_EQ_I(f.v_index, 2);
    T_ASSERT_EQ_I(f.u_index, 0);   /* unrelated */
    return 0;
}

int test_mesh_load_classify_empty(void)
{
    mesh_tex_flags f;
    mesh_classify_texture_name("", &f);
    T_ASSERT_EQ_I(f.water, 0);
    T_ASSERT_EQ_I(f.ext_tga, 0);
    T_ASSERT_EQ_I(f.u_index, 0);

    mesh_classify_texture_name(NULL, &f);
    T_ASSERT_EQ_I(f.water, 0);
    T_ASSERT_EQ_I(f.ext_tga, 0);
    return 0;
}

/* ─── 2. cache dedupe semantics ────────────────────────────────────────── */

int test_mesh_load_cache_dedupe(void)
{
    mesh_tex_cache_reset();
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, 0);

    mesh_tex_flags f1, f2;
    mesh_classify_texture_name("water_01.tga", &f1);
    mesh_classify_texture_name("kabe_b.bmp", &f2);

    int was_new = -1;
    int a = mesh_tex_cache_lookup_or_reserve("water_01.tga", &f1, &was_new);
    T_ASSERT_EQ_I(a, 0);
    T_ASSERT_EQ_I(was_new, 1);
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, 1);

    /* Repeat — should hit existing slot. */
    int b = mesh_tex_cache_lookup_or_reserve("water_01.tga", &f1, &was_new);
    T_ASSERT_EQ_I(b, 0);
    T_ASSERT_EQ_I(was_new, 0);
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, 1);

    /* Different name → next slot. */
    int c = mesh_tex_cache_lookup_or_reserve("kabe_b.bmp", &f2, &was_new);
    T_ASSERT_EQ_I(c, 1);
    T_ASSERT_EQ_I(was_new, 1);
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, 2);

    /* Flags frozen on first insert. */
    T_ASSERT_EQ_I(g_mesh_tex_cache.entries[0].flags.water, 1);
    T_ASSERT_EQ_I(g_mesh_tex_cache.entries[1].flags.kabe_, 1);

    /* Repeat lookup with different flags must NOT overwrite. */
    mesh_tex_flags forced = {0};
    forced.water = 0;
    forced.hikari = 1;
    (void)mesh_tex_cache_lookup_or_reserve("water_01.tga", &forced, &was_new);
    T_ASSERT_EQ_I(g_mesh_tex_cache.entries[0].flags.water, 1);   /* unchanged */
    T_ASSERT_EQ_I(g_mesh_tex_cache.entries[0].flags.hikari, 0);  /* unchanged */
    return 0;
}

int test_mesh_load_cache_capacity(void)
{
    mesh_tex_cache_reset();
    mesh_tex_flags zero = {0};
    char nm[32];
    for (int i = 0; i < MESH_TEX_CACHE_CAP; i++) {
        snprintf(nm, sizeof nm, "tex_%d.bmp", i);
        int slot = mesh_tex_cache_lookup_or_reserve(nm, &zero, NULL);
        T_ASSERT_EQ_I(slot, i);
    }
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, MESH_TEX_CACHE_CAP);

    /* Overflow — engine would write the overflow index anyway, but we
     * fail closed with -1. */
    int slot = mesh_tex_cache_lookup_or_reserve("overflow.bmp", &zero, NULL);
    T_ASSERT_EQ_I(slot, -1);
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, MESH_TEX_CACHE_CAP);
    return 0;
}

/* ─── 3. mesh_load_from_buf end-to-end ─────────────────────────────────── */

int test_mesh_load_from_buf_synthetic(void)
{
    mesh_tex_cache_reset();

    const char src[] =
        "xof 0303txt 0032\n"
        "Material Wall {\n"
        "  1.0;1.0;1.0;1.0;;\n"
        "  10.0;\n"
        "  0.0;0.0;0.0;;\n"
        "  0.0;0.0;0.0;;\n"
        "  TextureFilename {\n"
        "    \"kabe_01.bmp\";\n"
        "  }\n"
        "}\n"
        "Mesh Tri {\n"
        "  3;\n"
        "  0.0;0.0;0.0;,\n"
        "  1.0;0.0;0.0;,\n"
        "  0.0;1.0;0.0;;\n"
        "  1;\n"
        "  3;0,1,2;;\n"
        "  MeshMaterialList {\n"
        "    1;\n"
        "    1;\n"
        "    0;\n"
        "    {Wall}\n"
        "  }\n"
        "}\n";

    mesh_t *m = mesh_load_from_buf(src, sizeof src - 1, "xfile/test.x", -1);
    T_ASSERT(m != NULL);
    if (m->error[0]) { fprintf(stderr, "load: %s\n", m->error); mesh_free(m); return 1; }

    T_ASSERT_EQ_I(m->material_count, 1);
    T_ASSERT(m->texture_slots != NULL);
    T_ASSERT_EQ_I(m->texture_slots[0], 0);
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, 1);
    T_ASSERT(strcmp(g_mesh_tex_cache.entries[0].name, "kabe_01.bmp") == 0);
    T_ASSERT_EQ_I(g_mesh_tex_cache.entries[0].flags.kabe_, 1);
    T_ASSERT_EQ_I(g_mesh_tex_cache.entries[0].flags.ext_tga, 0);
    T_ASSERT_EQ_I(m->has_bounds, 1);

    /* Load AGAIN — same texture, same slot. Cache count must stay at 1. */
    mesh_t *m2 = mesh_load_from_buf(src, sizeof src - 1, "xfile/test.x", -1);
    T_ASSERT(m2 != NULL);
    if (m2->error[0]) { fprintf(stderr, "reload: %s\n", m2->error); mesh_free(m2); mesh_free(m); return 1; }
    T_ASSERT_EQ_I(m2->texture_slots[0], 0);
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, 1);

    mesh_free(m);
    mesh_free(m2);
    return 0;
}

int test_mesh_load_from_buf_no_texture(void)
{
    mesh_tex_cache_reset();
    const char src[] =
        "xof 0303txt 0032\n"
        "Mesh Cube {\n"
        "  3;\n"
        "  0.0;0.0;0.0;,\n"
        "  1.0;0.0;0.0;,\n"
        "  0.0;1.0;0.0;;\n"
        "  1;\n"
        "  3;0,1,2;;\n"
        "}\n";
    mesh_t *m = mesh_load_from_buf(src, sizeof src - 1, "xfile/no_tex.x", -1);
    T_ASSERT(m != NULL);
    if (m->error[0]) { fprintf(stderr, "load: %s\n", m->error); mesh_free(m); return 1; }
    /* No materials → no texture_slots allocation needed. */
    T_ASSERT_EQ_I(m->material_count, 0);
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, 0);
    mesh_free(m);
    return 0;
}

/* ─── 4. vendor corpus walk ────────────────────────────────────────────── */

static char *slurp_xfile(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

int test_mesh_load_vendor_corpus(void)
{
    mesh_tex_cache_reset();
    char findcmd[1024];
    snprintf(findcmd, sizeof findcmd,
             "find %s/vendor/original/xfile -name '*.x' 2>/dev/null", OPENRECET_ROOT);
    FILE *pipe = popen(findcmd, "r");
    if (!pipe) T_SKIP("popen failed");

    int loaded = 0, failed = 0;
    long total_slots = 0;
    char line[1024];
    while (fgets(line, sizeof line, pipe)) {
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (l == 0) continue;
        size_t sz;
        char *buf = slurp_xfile(line, &sz);
        if (!buf) continue;

        mesh_t *m = mesh_load_from_buf(buf, sz, line, -1);
        free(buf);
        if (!m) { failed++; continue; }
        if (m->error[0]) {
            fprintf(stderr, "FAIL load %s: %s\n", line, m->error);
            failed++;
            mesh_free(m);
            continue;
        }
        loaded++;
        for (int32_t i = 0; i < m->material_count; i++) {
            int32_t slot = m->texture_slots ? m->texture_slots[i] : -1;
            /* slot must be -1 (no texture) or 0 <= slot < cache count. */
            if (slot < -1 || slot >= g_mesh_tex_cache.count) {
                fprintf(stderr, "FAIL slot OOR in %s: mat %d → %d (cache=%d)\n",
                        line, i, slot, g_mesh_tex_cache.count);
                failed++;
            }
            if (slot >= 0) total_slots++;
        }
        mesh_free(m);
    }
    pclose(pipe);

    if (loaded == 0) T_SKIP("no vendor .x files found");
    if (failed > 0) T_FAIL("%d of %d files failed", failed, loaded + failed);

    /* Corpus survey reports 165 unique textures — should land below 200
     * cache cap and around the same ballpark. */
    fprintf(stderr, "  (mesh_load corpus: %d files, %d unique textures, %ld total slots)\n",
            loaded, g_mesh_tex_cache.count, total_slots);
    T_ASSERT(g_mesh_tex_cache.count <= MESH_TEX_CACHE_CAP);
    return 0;
}
