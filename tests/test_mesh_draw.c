/*
 * test_mesh_draw.c — host-side tests for the pure C7a slot resolver in
 * src/mesh_draw.c. The Win32-only DrawIndexedPrimitive walker isn't
 * unit-testable without a real device, but
 * mesh_resolve_texture_slot is — it just walks two arrays.
 *
 * The tests construct a manually-populated mesh_t + cache pair so we
 * can exercise all the resolve branches (out-of-range, missing slots,
 * stale slot past cache count, no-texture sentinel) without going
 * through mesh_load.
 */
#define _DEFAULT_SOURCE 1
#include "t.h"
#include "mesh.h"
#include "mesh_draw.h"
#include "mesh_load.h"

#include <stdlib.h>
#include <string.h>

/* Helper: build a stub mesh_t with N materials and a parallel
 * texture_slots[] array. Caller owns the returned pointer + must
 * mesh_free it. */
static mesh_t *stub_mesh_with_slots(const int32_t *slots, int n)
{
    mesh_t *m = (mesh_t *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->material_count = n;
    m->materials = (xfile_material *)calloc((size_t)n, sizeof *m->materials);
    m->texture_slots = (int32_t *)calloc((size_t)n, sizeof *m->texture_slots);
    for (int i = 0; i < n; i++) m->texture_slots[i] = slots[i];
    return m;
}

/* ─── 1. happy path: material index → cache slot ───────────────────────── */

int test_mesh_draw_resolve_slot_basic(void)
{
    mesh_tex_cache_reset();

    /* Seed the cache with 3 named entries (flags don't matter for this
     * test — we only care that mesh_resolve_texture_slot finds the row
     * by integer index, not by string lookup). */
    mesh_tex_flags zero = {0};
    int s_water = mesh_tex_cache_lookup_or_reserve("water_a.tga", &zero, NULL);
    int s_kabe  = mesh_tex_cache_lookup_or_reserve("kabe_b.bmp",  &zero, NULL);
    int s_yuka  = mesh_tex_cache_lookup_or_reserve("yuka_c.tga",  &zero, NULL);
    T_ASSERT_EQ_I(s_water, 0);
    T_ASSERT_EQ_I(s_kabe,  1);
    T_ASSERT_EQ_I(s_yuka,  2);

    /* Build a mesh with 3 materials, one per cache entry. */
    int32_t slots[3] = { s_kabe, s_water, s_yuka };
    mesh_t *m = stub_mesh_with_slots(slots, 3);
    T_ASSERT(m != NULL);

    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 0), s_kabe);
    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 1), s_water);
    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 2), s_yuka);

    mesh_free(m);
    return 0;
}

/* ─── 2. no-texture sentinel (-1) ──────────────────────────────────────── */

int test_mesh_draw_resolve_slot_no_texture(void)
{
    mesh_tex_cache_reset();
    mesh_tex_flags zero = {0};
    (void)mesh_tex_cache_lookup_or_reserve("tex.tga", &zero, NULL);

    int32_t slots[2] = { 0, -1 };       /* second material has no texture */
    mesh_t *m = stub_mesh_with_slots(slots, 2);

    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 0),  0);
    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 1), -1);

    mesh_free(m);
    return 0;
}

/* ─── 3. OOB material index → -1 ───────────────────────────────────────── */

int test_mesh_draw_resolve_slot_oob_material(void)
{
    mesh_tex_cache_reset();
    mesh_tex_flags zero = {0};
    (void)mesh_tex_cache_lookup_or_reserve("a.tga", &zero, NULL);

    int32_t slots[1] = { 0 };
    mesh_t *m = stub_mesh_with_slots(slots, 1);

    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, -1), -1);
    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m,  1), -1);   /* one past end */
    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 99), -1);

    mesh_free(m);
    return 0;
}

/* ─── 4. NULL mesh / NULL texture_slots → -1 ───────────────────────────── */

int test_mesh_draw_resolve_slot_null_inputs(void)
{
    mesh_tex_cache_reset();
    mesh_tex_flags zero = {0};
    (void)mesh_tex_cache_lookup_or_reserve("a.tga", &zero, NULL);

    T_ASSERT_EQ_I(mesh_resolve_texture_slot(NULL, 0), -1);

    /* Mesh built without going through mesh_load → texture_slots NULL. */
    mesh_t *m = (mesh_t *)calloc(1, sizeof *m);
    m->material_count = 2;
    m->materials = (xfile_material *)calloc(2, sizeof *m->materials);
    /* texture_slots intentionally left NULL */

    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 0), -1);
    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 1), -1);

    mesh_free(m);
    return 0;
}

/* ─── 5. stale slot past cache.count → -1 ──────────────────────────────── */

int test_mesh_draw_resolve_slot_stale_past_cache(void)
{
    mesh_tex_cache_reset();
    mesh_tex_flags zero = {0};
    /* Cache has 2 entries. */
    (void)mesh_tex_cache_lookup_or_reserve("a.tga", &zero, NULL);
    (void)mesh_tex_cache_lookup_or_reserve("b.tga", &zero, NULL);
    T_ASSERT_EQ_I(g_mesh_tex_cache.count, 2);

    /* Mesh claims slot 5 — which is past the live count. The resolver
     * has to clamp to -1 rather than walking off the end of the entries
     * array. */
    int32_t slots[1] = { 5 };
    mesh_t *m = stub_mesh_with_slots(slots, 1);

    T_ASSERT_EQ_I(mesh_resolve_texture_slot(m, 0), -1);

    mesh_free(m);
    return 0;
}

