/*
 * test_mesh.c — unit tests for src/mesh.c (pure-C build path).
 *
 * D3D8 upload is Win32-only and not unit-tested here — verified at
 * runtime when scene_walls / scene_floor wire mesh_load.
 *
 * Coverage:
 *   1. Empty xfile builds an empty mesh (no crash)
 *   2. Synthetic 1-triangle / 1-material mesh: vertex count, index
 *      count, single submesh, correct material index
 *   3. Bounds: synthetic cube → centroid at origin, radius == diagonal/2
 *   4. Vendor: ice01.x rolls up to one submesh per (mesh, material),
 *      vertex count == 3 * face_count after triangulation
 */
#define _DEFAULT_SOURCE 1
#include "t.h"
#include "mesh.h"
#include "xfile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── 1. empty xfile ──────────────────────────────────────────────────── */
int test_mesh_empty(void)
{
    const char src[] = "xof 0303txt 0032\n";
    xfile_t *xf = xfile_parse(src, sizeof src - 1, "<test>");
    T_ASSERT(xf != NULL);
    mesh_t *m = mesh_build_from_xfile(xf);
    xfile_free(xf);

    T_ASSERT(m != NULL);
    if (m->error[0]) { fprintf(stderr, "build error: %s\n", m->error); mesh_free(m); return 1; }
    T_ASSERT_EQ_I(m->vertex_count, 0);
    T_ASSERT_EQ_I(m->index_count, 0);
    T_ASSERT_EQ_I(m->submesh_count, 0);
    T_ASSERT_EQ_I(m->material_count, 0);
    mesh_free(m);
    return 0;
}

/* ─── 2. synthetic 1-triangle, 1-material ─────────────────────────────── */
int test_mesh_single_triangle(void)
{
    const char src[] =
        "xof 0303txt 0032\n"
        "Material Red {\n"
        "  1.0;0.0;0.0;1.0;;\n"
        "  10.0;\n"
        "  0.5;0.5;0.5;;\n"
        "  0.1;0.1;0.1;;\n"
        "  TextureFilename {\n"
        "    \"red.bmp\";\n"
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
        "    {Red}\n"
        "  }\n"
        "}\n";
    xfile_t *xf = xfile_parse(src, sizeof src - 1, "<test>");
    T_ASSERT(xf != NULL);
    if (xf->error[0]) { fprintf(stderr, "parse: %s\n", xf->error); xfile_free(xf); return 1; }

    mesh_t *m = mesh_build_from_xfile(xf);
    xfile_free(xf);
    T_ASSERT(m != NULL);
    if (m->error[0]) { fprintf(stderr, "build: %s\n", m->error); mesh_free(m); return 1; }

    T_ASSERT_EQ_I(m->vertex_count, 3);
    T_ASSERT_EQ_I(m->index_count, 3);
    T_ASSERT_EQ_I(m->submesh_count, 1);
    T_ASSERT_EQ_I(m->material_count, 1);
    T_ASSERT(strcmp(m->materials[0].name, "Red") == 0);
    T_ASSERT(strcmp(m->materials[0].texture, "red.bmp") == 0);

    T_ASSERT_EQ_I(m->submeshes[0].vertex_offset, 0);
    T_ASSERT_EQ_I(m->submeshes[0].vertex_count, 3);
    T_ASSERT_EQ_I(m->submeshes[0].index_offset, 0);
    T_ASSERT_EQ_I(m->submeshes[0].index_count, 3);
    T_ASSERT_EQ_I(m->submeshes[0].material_index, 0);

    T_ASSERT(fabsf(m->vertices[0].x - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(m->vertices[1].x - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(m->vertices[2].y - 1.0f) < 1e-6f);
    T_ASSERT_EQ_U(m->vertices[0].diffuse, 0xFFFFFFFFu);

    mesh_free(m);
    return 0;
}

/* ─── 3. bounds: synthetic 2x2x2 cube centred at origin ──────────────── */
int test_mesh_bounds_cube(void)
{
    /* Build a degenerate cube: 8 vertices, 1 triangle (just so the
     * mesh has SOME content). We only care about the bounds calc. */
    const char src[] =
        "xof 0303txt 0032\n"
        "Mesh Cube {\n"
        "  8;\n"
        "  -1.0;-1.0;-1.0;,\n"
        "   1.0;-1.0;-1.0;,\n"
        "   1.0; 1.0;-1.0;,\n"
        "  -1.0; 1.0;-1.0;,\n"
        "  -1.0;-1.0; 1.0;,\n"
        "   1.0;-1.0; 1.0;,\n"
        "   1.0; 1.0; 1.0;,\n"
        "  -1.0; 1.0; 1.0;;\n"
        "  2;\n"
        "  3;0,1,2;,\n"
        "  3;0,2,3;;\n"
        "}\n";
    xfile_t *xf = xfile_parse(src, sizeof src - 1, "<test>");
    T_ASSERT(xf != NULL);
    mesh_t *m = mesh_build_from_xfile(xf);
    xfile_free(xf);
    T_ASSERT(m != NULL);

    /* mesh has only 2 triangles using verts 0-3, so the bounds is over
     * those 6 expanded vertices (verts 0,1,2 / 0,2,3 — vs 4..7 unused).
     * Centroid won't be at origin in that case. Let's force ALL 8 verts
     * into the mesh by adding a covering face per vert. */
    mesh_free(m);

    /* Re-parse: cover all 8 verts with 4 triangles that hit every vert. */
    const char src2[] =
        "xof 0303txt 0032\n"
        "Mesh Cube {\n"
        "  8;\n"
        "  -1.0;-1.0;-1.0;,\n"
        "   1.0;-1.0;-1.0;,\n"
        "   1.0; 1.0;-1.0;,\n"
        "  -1.0; 1.0;-1.0;,\n"
        "  -1.0;-1.0; 1.0;,\n"
        "   1.0;-1.0; 1.0;,\n"
        "   1.0; 1.0; 1.0;,\n"
        "  -1.0; 1.0; 1.0;;\n"
        "  4;\n"
        "  3;0,1,2;,\n"
        "  3;0,2,3;,\n"
        "  3;4,5,6;,\n"
        "  3;4,6,7;;\n"
        "}\n";
    xf = xfile_parse(src2, sizeof src2 - 1, "<test>");
    m = mesh_build_from_xfile(xf);
    xfile_free(xf);
    T_ASSERT(m != NULL);
    if (m->error[0]) { fprintf(stderr, "build: %s\n", m->error); mesh_free(m); return 1; }

    mesh_compute_bounds(m);
    T_ASSERT_EQ_I(m->has_bounds, 1);
    T_ASSERT(fabsf(m->centroid[0]) < 1e-4f);
    T_ASSERT(fabsf(m->centroid[1]) < 1e-4f);
    T_ASSERT(fabsf(m->centroid[2]) < 1e-4f);
    /* Diagonal of 2-cube = 2*sqrt(3) → radius = sqrt(3) */
    T_ASSERT(fabsf(m->radius - 1.7320508f) < 1e-4f);

    mesh_free(m);
    return 0;
}

/* ─── 4. ice01.x triangulation correctness ────────────────────────────── */
static char *slurp_mesh(const char *path, size_t *out_len)
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

int test_mesh_vendor_ice01(void)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/vendor/original/xfile/etc/ice01.x", OPENRECET_ROOT);
    size_t sz = 0;
    char *buf = slurp_mesh(path, &sz);
    if (!buf) T_SKIP("vendor ice01.x missing");

    xfile_t *xf = xfile_parse(buf, sz, path);
    free(buf);
    T_ASSERT(xf != NULL);
    if (xf->error[0]) { xfile_free(xf); T_FAIL("parse error: %s", xf->error); }

    mesh_t *m = mesh_build_from_xfile(xf);
    int32_t exp_faces = xf->meshes[0].face_count;
    xfile_free(xf);
    T_ASSERT(m != NULL);
    if (m->error[0]) { fprintf(stderr, "build: %s\n", m->error); mesh_free(m); return 1; }

    /* ice01.x has 30 triangular faces. After triangulation
     * (no-op for triangles, count - 2 = 1) we expect 30 tris and 90
     * vertices (3 per tri, expanded). */
    T_ASSERT_EQ_I(exp_faces, 30);
    T_ASSERT_EQ_I(m->vertex_count, 90);
    T_ASSERT_EQ_I(m->index_count, 90);

    /* Materials used: ice01.x's MeshMaterialList references {Material__25}
     * with face_indexes all-zero, so only one material is in m->materials. */
    T_ASSERT_EQ_I(m->material_count, 1);
    T_ASSERT(strcmp(m->materials[0].name, "Material__25") == 0);
    T_ASSERT(strcmp(m->materials[0].texture, "w_ice.bmp") == 0);

    T_ASSERT_EQ_I(m->submesh_count, 1);
    T_ASSERT_EQ_I(m->submeshes[0].material_index, 0);
    T_ASSERT_EQ_I(m->submeshes[0].vertex_count, 90);
    T_ASSERT_EQ_I(m->submeshes[0].index_count, 90);

    mesh_compute_bounds(m);
    T_ASSERT_EQ_I(m->has_bounds, 1);
    /* ice01.x bbox is (-16.154, -24.7883, -14.5324) to (15.1772, 24.0897, 16.8837)
     * (from the comment at the top of the file). Centroid ≈ (-0.49, -0.35, 1.18),
     * radius ≈ 28. Loose bounds check. */
    T_ASSERT(m->radius > 20.0f && m->radius < 40.0f);

    mesh_free(m);
    return 0;
}

/* ─── 5. vendor corpus walk: every xfile/*.x builds without error ────── */
int test_mesh_vendor_corpus(void)
{
    char findcmd[1024];
    snprintf(findcmd, sizeof findcmd,
             "find %s/vendor/original/xfile -name '*.x' 2>/dev/null", OPENRECET_ROOT);
    FILE *pipe = popen(findcmd, "r");
    if (!pipe) T_SKIP("popen failed");

    int parsed = 0, failed = 0;
    long total_verts = 0, total_indices = 0, total_submeshes = 0;
    char line[1024];
    while (fgets(line, sizeof line, pipe)) {
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (l == 0) continue;
        size_t sz;
        char *buf = slurp_mesh(line, &sz);
        if (!buf) continue;
        xfile_t *xf = xfile_parse(buf, sz, line);
        free(buf);
        if (!xf) { failed++; continue; }
        if (xf->error[0]) { xfile_free(xf); failed++; continue; }

        mesh_t *m = mesh_build_from_xfile(xf);
        xfile_free(xf);
        if (!m) { failed++; continue; }
        if (m->error[0]) {
            fprintf(stderr, "FAIL build %s: %s\n", line, m->error);
            failed++;
        } else {
            parsed++;
            total_verts     += m->vertex_count;
            total_indices   += m->index_count;
            total_submeshes += m->submesh_count;
            mesh_compute_bounds(m);
        }
        mesh_free(m);
    }
    pclose(pipe);

    if (parsed == 0) T_SKIP("no vendor .x files found");
    if (failed > 0) T_FAIL("%d of %d files failed", failed, parsed + failed);
    fprintf(stderr, "  (mesh corpus: %d files, %ld verts, %ld indices, %ld submeshes)\n",
            parsed, total_verts, total_indices, total_submeshes);
    return 0;
}
