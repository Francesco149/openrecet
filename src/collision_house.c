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

/* Room mesh (HOUSE stage map slot 0).  Matches the path
 * scene_map_meshes_load_house resolves map[0] to (see scene_map_meshes.h). */
#define HOUSE_ROOM_XFILE "xfile/shop/shop_1st.x"

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

    xfile_t *xf = mesh_load_parse_xfile(HOUSE_ROOM_XFILE);
    if (!xf || xf->error[0]) {
        fprintf(stderr, "collision_house: parse failed for %s%s%s\n",
                HOUSE_ROOM_XFILE, xf ? ": " : "", xf ? xf->error : "");
        if (xf) xfile_free(xf);
        return;
    }

    collision_object *obj = (collision_object *)calloc(1, sizeof *obj);
    if (!obj) { xfile_free(xf); return; }

    /* HOUSE uses the SMALL (indoor) AABB padding mode (collision_mesh.h). */
    if (collision_object_build(obj, xf, COLLISION_PAD_SMALL) != 0) {
        fprintf(stderr, "collision_house: object build failed for %s\n",
                HOUSE_ROOM_XFILE);
        free(obj);
        xfile_free(xf);
        return;
    }
    xfile_free(xf);

    g_house_collision.objects      = obj;          /* one malloc'd object */
    g_house_collision.object_count = 1;
    memcpy(g_house_collision.aabb_min, obj->aabb_min, sizeof obj->aabb_min);
    memcpy(g_house_collision.aabb_max, obj->aabb_max, sizeof obj->aabb_max);
    g_house_collision_ready = 1;

    fprintf(stderr, "collision_house: built %d tris from %s "
            "(world x[%.2f,%.2f] z[%.2f,%.2f])\n",
            obj->tri_count, HOUSE_ROOM_XFILE,
            obj->aabb_min[0], obj->aabb_max[0],
            obj->aabb_min[2], obj->aabb_max[2]);
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
