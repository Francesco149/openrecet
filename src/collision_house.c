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

/* HOUSE collision objects (engine FUN_00432e50 loops over every placed object,
 * subtracting its per-object world origin DAT_0438c058/0a8/0f8).  The live
 * retail table for a new-game HOUSE (tier 0), captured with
 * tools/dump_collision_objects.py: the room (slot 0, mesh 0) at origin 0, the
 * carpet (slot 1, mesh 1), and 3 display tables (phase-2 slots 5/6/7, meshes
 * 3/4).  The origins equal the already-ported render placement
 * (g_scene1_walker_phase1/phase2_pos) — see engine-quirks §67.  Hardcoded here
 * for the new-game HOUSE layout (the FUN_0044c88f writer that fills
 * DAT_0438c058 per stage/tier is not yet ported; mesh-index→.x map is
 * scene_map_meshes.h: 0=shop_1st, 1=shop_jutan, 3=shop_table01, 4=shop_table02). */
/* PORT-DEBT(synthetic-data, FUN_0044c88f): hardcoded new-game tier-0 furniture
   origins; the real per-stage/tier DAT_0438c058 writer (FUN_0044c88f /
   FUN_00436f97 stage_positions) is unported, so this can't generalise past
   tier 0. Retire = engine-quirks §65/§67, plan Step 3.4. */
typedef struct { const char *xfile; float origin[3]; float rot_y; } house_collision_obj;
static const house_collision_obj k_house_objects[] = {
    /* xfile,                        origin (DAT_0438c058),   rot_y (DAT_0438c008) */
    { "xfile/shop/shop_1st.x",      {  0.0f, 0.0f,  0.0f }, 0.0f        }, /* room   (slot 0) */
    { "xfile/jutan/shop_jutan.x",   { -2.0f, 0.0f, -1.0f }, 0.0f        }, /* carpet (slot 1) */
    { "xfile/table/shop_table01.x", { -2.0f, 0.0f,  0.0f }, 0.0f        }, /* table  (slot 5) */
    { "xfile/table/shop_table02.x", { -4.0f, 0.0f, -8.0f }, 0.0f        }, /* table  (slot 6) */
    { "xfile/table/shop_table02.x", {-10.0f, 0.0f, -2.0f }, 1.5707964f  }, /* table  (slot 7, π/2) */
};
#define HOUSE_OBJECT_COUNT ((int)(sizeof k_house_objects / sizeof k_house_objects[0]))

static collision_mesh g_house_collision;
static int            g_house_collision_ready;

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

    collision_object *objs =
        (collision_object *)calloc(HOUSE_OBJECT_COUNT, sizeof *objs);
    if (!objs) return;

    int n = 0;
    for (int i = 0; i < HOUSE_OBJECT_COUNT; i++) {
        const house_collision_obj *spec = &k_house_objects[i];
        xfile_t *xf = mesh_load_parse_xfile(spec->xfile);
        if (!xf || xf->error[0]) {
            fprintf(stderr, "collision_house: parse failed for %s%s%s\n",
                    spec->xfile, xf ? ": " : "", xf ? xf->error : "");
            if (xf) xfile_free(xf);
            continue;   /* skip a missing object rather than abort the room */
        }
        /* HOUSE uses the SMALL (indoor) AABB padding mode (collision_mesh.h). */
        if (collision_object_build(&objs[n], xf, COLLISION_PAD_SMALL) != 0) {
            fprintf(stderr, "collision_house: object build failed for %s\n",
                    spec->xfile);
            xfile_free(xf);
            continue;
        }
        xfile_free(xf);
        memcpy(objs[n].origin, spec->origin, sizeof objs[n].origin);
        objs[n].rot_y = spec->rot_y;
        fprintf(stderr, "collision_house: built %d tris from %s @ "
                "(%.1f,%.1f,%.1f) rot=%.3f\n", objs[n].tri_count, spec->xfile,
                spec->origin[0], spec->origin[1], spec->origin[2], spec->rot_y);
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
