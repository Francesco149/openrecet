/*
 * collision_house.c — see collision_house.h.
 *
 * The render mesh_t built by mesh_load drops the raw geometry + material
 * names collision_object_build needs, so we re-parse shop_1st.x's bytes into
 * an xfile_t (through the same storage path) and build the collision object
 * from that.  One extra parse of a ~small `.x` per HOUSE entry — negligible.
 */
#include "collision_house.h"

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesh_load.h"   /* mesh_load_parse_xfile */
#include "xfile.h"
#include "scene1_walker_pass_init.h" /* live phase-1/phase-2 placement arrays */
#include "scene_table.h"             /* scene_table_filename / format / selector */
#include "scene_map_meshes.h"        /* SCENE_MAP_STAGE_HOUSE */
#include "tables_stage.h"            /* g_stage — parsed map[] paths */

/* HOUSE collision objects (engine FUN_00432e50 loops over every placed object,
 * subtracting its per-object world origin DAT_0438c058/0a8/0f8 and applying the
 * per-object Y-rotation DAT_0438c008).  These objects + their origins are NOT a
 * separate table: the engine's collision-origin arrays (DAT_0438c058/0a8/0f8,
 * rot DAT_0438c008) are the SAME memory as the render walker's per-instance
 * placement (DAT_0438c06c/0bc/10c, rot DAT_0438c01c), offset-aliased by the
 * 5-slot phase-1/phase-2 split: phase-1 objects land in collision slots
 * 0..count1-1, phase-2 (furniture) in slots (i-count1)+5 (engine-quirks §65/§67,
 * tools/dump_collision_objects.py).  The render placement is faithfully ported
 * by scene1_postload_walker_phase2_init (engine FUN_00436f97 block 21, sourced
 * from the real save-record furniture template), so we build the collision
 * objects from the SAME live arrays — no hardcoded per-tier table, and it
 * generalises to every stage_type/tier that the writer handles.
 *
 *   phase-1 mesh_index  → the stage map[] pool path (g_stage map[]: 0=shop_1st,
 *                         1=shop_jutan, …) — same source scene_map_meshes uses.
 *   phase-2 mesh_type    → shop_table .x via scene1_walker_draw_b_mesh_index
 *                         (the engine draw-loop-B index: mesh_type-3+selector*2)
 *                         then scene_table_filename (0=shop_table01, 1=table02). */

static collision_mesh g_house_collision;
static int            g_house_collision_ready;

/* Parse `path`, build a collision object at (origin, rot_y).  Returns 0 on
 * success (obj populated), -1 on parse/build failure (obj left untouched). */
static int house_add_object(collision_object *obj, const char *path,
                            const float origin[3], float rot_y)
{
    xfile_t *xf = mesh_load_parse_xfile(path);
    if (!xf || xf->error[0]) {
        fprintf(stderr, "collision_house: parse failed for %s%s%s\n",
                path, xf ? ": " : "", xf ? xf->error : "");
        if (xf) xfile_free(xf);
        return -1;
    }
    /* HOUSE uses the SMALL (indoor) AABB padding mode (collision_mesh.h). */
    if (collision_object_build(obj, xf, COLLISION_PAD_SMALL) != 0) {
        fprintf(stderr, "collision_house: object build failed for %s\n", path);
        xfile_free(xf);
        return -1;
    }
    xfile_free(xf);
    obj->origin[0] = origin[0];
    obj->origin[1] = origin[1];
    obj->origin[2] = origin[2];
    obj->rot_y = rot_y;
    fprintf(stderr, "collision_house: built %d tris from %s @ "
            "(%.1f,%.1f,%.1f) rot=%.3f\n", obj->tri_count, path,
            origin[0], origin[1], origin[2], rot_y);
    return 0;
}

void collision_house_reset(void)
{
    if (g_house_collision_ready) {
        collision_mesh_free(&g_house_collision);
        g_house_collision_ready = 0;
    }
    memset(&g_house_collision, 0, sizeof g_house_collision);
}

void collision_house_build(void)
{
    collision_house_reset();

    /* Live placement instance counts (engine DAT_0438bfb0 / DAT_0438bfb4),
     * written by scene1_postload_walker_phase2_init before this runs. */
    int p1 = g_scene1_walker_phase1_count;
    int p2 = g_scene1_walker_phase2_count;
    if (p1 < 0) p1 = 0;
    if (p1 > SCENE1_WALKER_PHASE1_MAX) p1 = SCENE1_WALKER_PHASE1_MAX;
    if (p2 < 0) p2 = 0;
    if (p2 > SCENE1_WALKER_PHASE2_MAX) p2 = SCENE1_WALKER_PHASE2_MAX;

    int cap = p1 + p2;
    if (cap == 0) return;

    collision_object *objs = (collision_object *)calloc(cap, sizeof *objs);
    if (!objs) return;

    int n = 0;

    /* Phase 1 (collision slots 0..count1-1): room / carpet / wall meshes from
     * the stage map[] pool.  mesh_index indexes g_stage.records[stage].map[],
     * the same source scene_map_meshes_load_house draws from. */
    for (int i = 0; i < p1; i++) {
        int mi = g_scene1_walker_phase1_mesh_index[i];
        const char *path =
            (SCENE_MAP_STAGE_HOUSE < g_stage.count &&
             mi >= 0 && mi < STAGE_MAP_SLOTS)
                ? g_stage.records[SCENE_MAP_STAGE_HOUSE].map[mi]
                : NULL;
        if (!path || !path[0]) {
            fprintf(stderr, "collision_house: phase1[%d] mesh_index %d → no map path\n",
                    i, mi);
            continue;   /* skip a missing object rather than abort the room */
        }
        const float origin[3] = {
            g_scene1_walker_phase1_pos_x[i],
            g_scene1_walker_phase1_pos_y[i],
            g_scene1_walker_phase1_pos_z[i],
        };
        if (house_add_object(&objs[n], path, origin,
                             g_scene1_walker_phase1_rot_y[i]) == 0)
            n++;
    }

    /* Phase 2 (collision slots (i-count1)+5): shop_table furniture.  The
     * engine draw-loop-B index (scene1_walker_draw_b_mesh_index) maps the
     * per-instance mesh_type + the per-stage table selector to a shop_table
     * slot; scene_table_filename resolves it to the .x basename. */
    const int selector = g_scene_table_selector;
    const scene1_walker_phase2_flag_fn flag_hook =
        scene1_walker_phase2_get_flag_hook();
    for (int i = 0; i < p2; i++) {
        int use_shop_table = 0;
        int32_t flag = flag_hook ? flag_hook(i) : 0;
        int idx = scene1_walker_draw_b_mesh_index(
            g_scene1_walker_phase2_mesh_type[i], flag, selector,
            &use_shop_table);
        const char *name = (use_shop_table && idx >= 0)
                               ? scene_table_filename(idx) : NULL;
        if (!name) {
            fprintf(stderr, "collision_house: phase2[%d] not a shop_table "
                    "(mesh_type %d, idx %d, use %d) — skipped\n",
                    i, g_scene1_walker_phase2_mesh_type[i], idx, use_shop_table);
            continue;
        }
        char path[256];
        snprintf(path, sizeof path, scene_table_format_string(), name);
        const float origin[3] = {
            g_scene1_walker_phase2_pos_x[i],
            g_scene1_walker_phase2_pos_y[i],
            g_scene1_walker_phase2_pos_z[i],
        };
        if (house_add_object(&objs[n], path, origin,
                             g_scene1_walker_phase2_rot_y[i]) == 0)
            n++;
    }

    if (n == 0) { free(objs); return; }

    g_house_collision.objects      = objs;
    g_house_collision.object_count = n;
    /* Union AABB over the placed objects (each object's bounds + its origin). */
    for (int k = 0; k < 3; k++) {
        g_house_collision.aabb_min[k] =  1e30f;
        g_house_collision.aabb_max[k] = -1e30f;
    }
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 3; k++) {
            float lo = objs[i].aabb_min[k] + objs[i].origin[k];
            float hi = objs[i].aabb_max[k] + objs[i].origin[k];
            if (lo < g_house_collision.aabb_min[k]) g_house_collision.aabb_min[k] = lo;
            if (hi > g_house_collision.aabb_max[k]) g_house_collision.aabb_max[k] = hi;
        }
    }
    g_house_collision_ready = 1;
}

const collision_mesh *collision_house_get(void)
{
    return g_house_collision_ready ? &g_house_collision : NULL;
}

#else /* !_WIN32 */

void                  collision_house_build(void) { }
void                  collision_house_reset(void) { }
const collision_mesh *collision_house_get(void)   { return 0; }

#endif /* _WIN32 */
