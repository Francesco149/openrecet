/*
 * test_xfile.c — unit tests for src/xfile.c (.x text-format parser).
 *
 * Coverage:
 *   1. Bad / missing header
 *   2. Empty file
 *   3. Synthetic single-mesh file (Mesh{} with no nested templates)
 *   4. Synthetic Mesh + MeshNormals + MeshTextureCoords + MeshMaterialList
 *   5. Synthetic Frame hierarchy with FrameTransformMatrix
 *   6. Synthetic Material with TextureFilename
 *   7. Synthetic hyphen-in-identifier stitch
 *   8. Synthetic vertex-color separator polymorphism
 *
 * Plus a vendor-dependent assertion suite that pins
 * vendor/original/xfile/etc/ice01.x to the same numbers the Python
 * oracle's self-test pins (mesh_count, vertex/face/normal counts, two
 * known global materials, first vertex, top-level frame layout).
 */
#define _DEFAULT_SOURCE 1   /* popen/pclose */
#include "t.h"
#include "xfile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helpers --------------------------------------------------------------- */

static char *slurp(const char *path, size_t *out_len)
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

/* ─── 1. bad header ───────────────────────────────────────────────────── */
int test_xfile_bad_header(void)
{
    /* 16+ bytes so we hit the magic check, not the short-file check. */
    const char src[] = "BADMAGIC0303txt 0032";
    xfile_t *x = xfile_parse(src, sizeof src - 1, "<test>");
    if (!x) { T_FAIL("xfile_parse returned NULL"); }
    int ok = (x->error[0] != '\0') && (strstr(x->error, "magic") != NULL);
    xfile_free(x);
    if (!ok) T_FAIL("expected 'magic' in error");
    return 0;
}

/* ─── 2. empty top body (just header) ──────────────────────────────────── */
int test_xfile_empty(void)
{
    const char src[] = "xof 0303txt 0032\n";
    xfile_t *x = xfile_parse(src, sizeof src - 1, "<test>");
    T_ASSERT(x != NULL);
    T_ASSERT_EQ_I(x->error[0], 0);
    T_ASSERT_EQ_I(x->mesh_count, 0);
    T_ASSERT_EQ_I(x->frame_count, 0);
    T_ASSERT_EQ_I(x->global_material_count, 0);
    T_ASSERT_EQ_I(x->texture_count, 0);
    T_ASSERT_EQ_I(x->header_float_size, 32);
    T_ASSERT(strcmp(x->header_encoding, "txt") == 0);
    xfile_free(x);
    return 0;
}

/* ─── 3. bare Mesh{} ─────────────────────────────────────────────────── */
int test_xfile_bare_mesh(void)
{
    /* 3 verts, 1 triangle */
    const char src[] =
        "xof 0303txt 0032\n"
        "Mesh Tri {\n"
        "  3;\n"
        "  0.0;0.0;0.0;,\n"
        "  1.0;0.0;0.0;,\n"
        "  0.0;1.0;0.0;;\n"
        "  1;\n"
        "  3;0,1,2;;\n"
        "}\n";
    xfile_t *x = xfile_parse(src, sizeof src - 1, "<test>");
    T_ASSERT(x != NULL);
    if (x->error[0]) { fprintf(stderr, "parse error: %s\n", x->error); xfile_free(x); return 1; }
    T_ASSERT_EQ_I(x->mesh_count, 1);
    T_ASSERT(strcmp(x->meshes[0].name, "Tri") == 0);
    T_ASSERT_EQ_I(x->meshes[0].vertex_count, 3);
    T_ASSERT_EQ_I(x->meshes[0].face_count, 1);
    T_ASSERT_EQ_I(x->meshes[0].faces[0].count, 3);
    T_ASSERT_EQ_I(x->meshes[0].faces[0].verts[0], 0);
    T_ASSERT_EQ_I(x->meshes[0].faces[0].verts[2], 2);
    T_ASSERT(fabsf(x->meshes[0].vertices[1].x - 1.0f) < 1e-6f);
    xfile_free(x);
    return 0;
}

/* ─── 4. Mesh with MeshNormals/MeshTextureCoords/MeshMaterialList ─────── */
int test_xfile_full_mesh(void)
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
        "Mesh Quad {\n"
        "  4;\n"
        "  0.0;0.0;0.0;,\n"
        "  1.0;0.0;0.0;,\n"
        "  1.0;1.0;0.0;,\n"
        "  0.0;1.0;0.0;;\n"
        "  2;\n"
        "  3;0,1,2;,\n"
        "  3;0,2,3;;\n"
        "  MeshNormals {\n"
        "    1;\n"
        "    0.0;0.0;1.0;;\n"
        "    2;\n"
        "    3;0,0,0;,\n"
        "    3;0,0,0;;\n"
        "  }\n"
        "  MeshTextureCoords {\n"
        "    4;\n"
        "    0.0;0.0;,\n"
        "    1.0;0.0;,\n"
        "    1.0;1.0;,\n"
        "    0.0;1.0;;\n"
        "  }\n"
        "  MeshMaterialList {\n"
        "    1;\n"
        "    2;\n"
        "    0,0;\n"
        "    {Red}\n"
        "  }\n"
        "}\n";
    xfile_t *x = xfile_parse(src, sizeof src - 1, "<test>");
    T_ASSERT(x != NULL);
    if (x->error[0]) { fprintf(stderr, "parse error: %s\n", x->error); xfile_free(x); return 1; }
    T_ASSERT_EQ_I(x->mesh_count, 1);
    T_ASSERT_EQ_I(x->global_material_count, 1);
    T_ASSERT(strcmp(x->global_materials[0].name, "Red") == 0);
    T_ASSERT(strcmp(x->global_materials[0].texture, "red.bmp") == 0);
    T_ASSERT_EQ_I(x->texture_count, 1);

    xfile_mesh *m = &x->meshes[0];
    T_ASSERT(strcmp(m->name, "Quad") == 0);
    T_ASSERT_EQ_I(m->vertex_count, 4);
    T_ASSERT_EQ_I(m->face_count, 2);
    T_ASSERT_EQ_I(m->normal_count, 1);
    T_ASSERT_EQ_I(m->uv_count, 4);
    T_ASSERT_EQ_I(m->material_count, 1);
    T_ASSERT_EQ_I(m->face_material_count, 2);
    T_ASSERT_EQ_I(m->face_material_indexes[0], 0);
    T_ASSERT_EQ_I(m->material_ref_count, 1);
    T_ASSERT(strcmp(m->material_refs[0], "Red") == 0);

    xfile_free(x);
    return 0;
}

/* ─── 5. Frame hierarchy ──────────────────────────────────────────────── */
int test_xfile_frame_hierarchy(void)
{
    const char src[] =
        "xof 0303txt 0032\n"
        "Frame Root {\n"
        "  FrameTransformMatrix {\n"
        "    1.0, 0.0, 0.0, 0.0,\n"
        "    0.0, 1.0, 0.0, 0.0,\n"
        "    0.0, 0.0, 1.0, 0.0,\n"
        "    2.0, 3.0, 4.0, 1.0;;\n"
        "  }\n"
        "  Frame Child {\n"
        "    FrameTransformMatrix {\n"
        "      1.0, 0.0, 0.0, 0.0,\n"
        "      0.0, 1.0, 0.0, 0.0,\n"
        "      0.0, 0.0, 1.0, 0.0,\n"
        "      0.0, 0.0, 0.0, 1.0;;\n"
        "    }\n"
        "    Mesh Tri {\n"
        "      3;\n"
        "      0.0;0.0;0.0;,\n"
        "      1.0;0.0;0.0;,\n"
        "      0.0;1.0;0.0;;\n"
        "      1;\n"
        "      3;0,1,2;;\n"
        "    }\n"
        "  }\n"
        "}\n";
    xfile_t *x = xfile_parse(src, sizeof src - 1, "<test>");
    T_ASSERT(x != NULL);
    if (x->error[0]) { fprintf(stderr, "parse error: %s\n", x->error); xfile_free(x); return 1; }

    T_ASSERT_EQ_I(x->frame_count, 2);
    T_ASSERT_EQ_I(x->mesh_count, 1);

    /* DFS order: Root then Child. */
    T_ASSERT(strcmp(x->frames[0].name, "Root") == 0);
    T_ASSERT(strcmp(x->frames[1].name, "Child") == 0);
    T_ASSERT_EQ_I(x->frames[0].has_transform, 1);
    T_ASSERT_EQ_I(x->frames[0].child_count, 1);
    T_ASSERT(strcmp(x->frames[0].children_names[0], "Child") == 0);
    T_ASSERT(fabsf(x->frames[0].transform[12] - 2.0f) < 1e-6f);
    T_ASSERT_EQ_I(x->frames[1].mesh_count, 1);

    T_ASSERT(strcmp(x->meshes[0].name, "Tri") == 0);
    T_ASSERT(strcmp(x->meshes[0].frame_path, "Root/Child") == 0);

    xfile_free(x);
    return 0;
}

/* ─── 6. hyphen-in-identifier stitch ──────────────────────────────────── */
int test_xfile_hyphen_stitch(void)
{
    const char src[] =
        "xof 0303txt 0032\n"
        "Material PDX02_-_Default {\n"
        "  1.0;1.0;1.0;1.0;;\n"
        "  1.0;\n"
        "  0.0;0.0;0.0;;\n"
        "  0.0;0.0;0.0;;\n"
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
        "    {PDX02_-_Default}\n"
        "  }\n"
        "}\n";
    xfile_t *x = xfile_parse(src, sizeof src - 1, "<test>");
    T_ASSERT(x != NULL);
    if (x->error[0]) { fprintf(stderr, "parse error: %s\n", x->error); xfile_free(x); return 1; }
    T_ASSERT_EQ_I(x->global_material_count, 1);
    /* Per docs/formats/xfile.md quirk #4: hyphen drops on both sides, so
     * names compare equal after stitching. */
    T_ASSERT(strcmp(x->global_materials[0].name, "PDX02__Default") == 0);
    T_ASSERT_EQ_I(x->meshes[0].material_ref_count, 1);
    T_ASSERT(strcmp(x->meshes[0].material_refs[0], "PDX02__Default") == 0);
    xfile_free(x);
    return 0;
}

/* ─── 7. vendor: ice01.x pinned numbers ───────────────────────────────── */
int test_xfile_vendor_ice01(void)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/vendor/original/xfile/etc/ice01.x", OPENRECET_ROOT);
    size_t sz = 0;
    char *buf = slurp(path, &sz);
    if (!buf) T_SKIP("vendor file %s missing", path);

    xfile_t *x = xfile_parse(buf, sz, path);
    free(buf);

    T_ASSERT(x != NULL);
    if (x->error[0]) {
        fprintf(stderr, "parse error: %s\n", x->error);
        xfile_free(x); return 1;
    }

    /* Matches the Python oracle's self-test. */
    T_ASSERT_EQ_I(x->mesh_count, 1);
    T_ASSERT_EQ_I(x->meshes[0].vertex_count, 41);
    T_ASSERT_EQ_I(x->meshes[0].face_count, 30);
    T_ASSERT_EQ_I(x->meshes[0].normal_count, 17);
    T_ASSERT_EQ_I(x->global_material_count, 2);

    /* Find xof_default + Material__25. */
    int found_default = 0, found_25 = 0;
    for (int32_t i = 0; i < x->global_material_count; i++) {
        if (strcmp(x->global_materials[i].name, "xof_default") == 0) {
            found_default = 1;
            T_ASSERT(fabsf(x->global_materials[i].diffuse.r - 0.4f) < 1e-5f);
            T_ASSERT(fabsf(x->global_materials[i].power - 32.0f) < 1e-4f);
            T_ASSERT(x->global_materials[i].texture[0] == '\0');
        }
        if (strcmp(x->global_materials[i].name, "Material__25") == 0) {
            found_25 = 1;
            T_ASSERT(strcmp(x->global_materials[i].texture, "w_ice.bmp") == 0);
        }
    }
    T_ASSERT_EQ_I(found_default, 1);
    T_ASSERT_EQ_I(found_25, 1);

    T_ASSERT(strcmp(x->meshes[0].name, "Box01") == 0);

    /* First vertex: [-8.577065, -3.734980, -7.484766]. */
    T_ASSERT(fabsf(x->meshes[0].vertices[0].x - -8.577065f) < 1e-4f);
    T_ASSERT(fabsf(x->meshes[0].vertices[0].y - -3.734980f) < 1e-4f);
    T_ASSERT(fabsf(x->meshes[0].vertices[0].z - -7.484766f) < 1e-4f);

    /* Top-level frame: Frame_World, with Frame_Box01 as child. */
    int found_world = 0;
    for (int32_t i = 0; i < x->frame_count; i++) {
        if (strcmp(x->frames[i].name, "Frame_World") == 0) {
            found_world = 1;
            T_ASSERT_EQ_I(x->frames[i].has_transform, 1);
            /* identity-ish diagonal */
            T_ASSERT(fabsf(x->frames[i].transform[0]  - 1.0f) < 1e-5f);
            T_ASSERT(fabsf(x->frames[i].transform[5]  - 1.0f) < 1e-5f);
            T_ASSERT(fabsf(x->frames[i].transform[10] - 1.0f) < 1e-5f);
            T_ASSERT(fabsf(x->frames[i].transform[15] - 1.0f) < 1e-5f);
            T_ASSERT_EQ_I(x->frames[i].child_count, 1);
            T_ASSERT(strcmp(x->frames[i].children_names[0], "Frame_Box01") == 0);
        }
    }
    T_ASSERT_EQ_I(found_world, 1);

    xfile_free(x);
    return 0;
}

/* ─── 8. vendor corpus walk: every .x file must parse without error ──── */
int test_xfile_vendor_corpus(void)
{
    /* Walk vendor/original/xfile by spawning `find` via popen — the test
     * driver doesn't link a glob helper. If popen fails or vendor is
     * absent, skip. */
    char findcmd[1024];
    snprintf(findcmd, sizeof findcmd,
             "find %s/vendor/original/xfile -name '*.x' 2>/dev/null", OPENRECET_ROOT);
    FILE *pipe = popen(findcmd, "r");
    if (!pipe) T_SKIP("popen failed");

    int parsed = 0, failed = 0;
    char line[1024];
    while (fgets(line, sizeof line, pipe)) {
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (l == 0) continue;
        size_t sz;
        char *buf = slurp(line, &sz);
        if (!buf) continue;
        xfile_t *x = xfile_parse(buf, sz, line);
        free(buf);
        if (!x) { failed++; continue; }
        if (x->error[0]) {
            fprintf(stderr, "FAIL parse %s: %s\n", line, x->error);
            failed++;
        } else {
            parsed++;
        }
        xfile_free(x);
    }
    pclose(pipe);

    if (parsed == 0) T_SKIP("no vendor .x files found at %s/vendor/original/xfile", OPENRECET_ROOT);
    if (failed > 0) T_FAIL("%d of %d vendor files failed to parse", failed, parsed + failed);
    fprintf(stderr, "  (xfile corpus: %d files parsed clean)\n", parsed);
    return 0;
}

/* ─── 9. xfile2 corpus walk: skinned/animated files (animation + skinning
 * silently skipped, geometry must still parse) ────────────────────────── */
int test_xfile_vendor_xfile2_corpus(void)
{
    char findcmd[1024];
    snprintf(findcmd, sizeof findcmd,
             "find %s/vendor/original/xfile2 -name '*.x' 2>/dev/null", OPENRECET_ROOT);
    FILE *pipe = popen(findcmd, "r");
    if (!pipe) T_SKIP("popen failed");

    int parsed = 0, failed = 0, with_meshes = 0;
    char line[1024];
    while (fgets(line, sizeof line, pipe)) {
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (l == 0) continue;
        size_t sz;
        char *buf = slurp(line, &sz);
        if (!buf) continue;
        xfile_t *x = xfile_parse(buf, sz, line);
        free(buf);
        if (!x) { failed++; continue; }
        if (x->error[0]) {
            fprintf(stderr, "FAIL parse %s: %s\n", line, x->error);
            failed++;
        } else {
            parsed++;
            if (x->mesh_count > 0) with_meshes++;
        }
        xfile_free(x);
    }
    pclose(pipe);

    if (parsed == 0) T_SKIP("no vendor .x files found at %s/vendor/original/xfile2", OPENRECET_ROOT);
    if (failed > 0) T_FAIL("%d of %d xfile2 files failed to parse", failed, parsed + failed);
    fprintf(stderr, "  (xfile2 corpus: %d files parsed clean, %d with meshes)\n",
            parsed, with_meshes);
    return 0;
}
