/*
 * collision_mesh.c — build the runtime collision triangle mesh from a parsed
 * `.x` file.  See collision_mesh.h for the engine provenance.
 *
 * We reuse the port's oracle-validated xfile parser (xfile.h) for the `.x`
 * text and replicate only the geometry pipeline the engine runs *after*
 * parsing: per-mesh frame transform, the ×0.2 collision-world scale, the
 * material-name → type classification (FUN_00471d45), and the per-triangle
 * plane/AABB/edge build (FUN_00432ac6) gathered into objects (FUN_0043289b).
 */
#include "collision_mesh.h"

#include <stdlib.h>
#include <string.h>

#include "math3d.h"   /* mat4_mul (shared row-vector left-multiply) */

/* ───── frame transform (mirrors mesh.c's static helpers) ─────────────────
 *
 * Collision uses the *same* frame nesting + row-vector convention as the
 * render mesh build; we keep a private copy here so collision_mesh.c has no
 * link dependency on mesh.c's internals. */

static int cm_find_frame(const xfile_t *xf, const char *name, int32_t name_len)
{
    if (!name || name_len <= 0) return -1;
    for (int32_t i = 0; i < xf->frame_count; i++) {
        const char *fn = xf->frames[i].name;
        if ((int32_t)strlen(fn) == name_len && memcmp(fn, name, (size_t)name_len) == 0)
            return i;
    }
    return -1;
}

static void cm_accumulate_frame(const xfile_t *xf, const char *frame_path,
                                float out[16])
{
    static const float ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    memcpy(out, ident, sizeof ident);
    if (!frame_path || !frame_path[0]) return;

    const char *segs[16];
    int32_t     segl[16];
    int n = 0;
    const char *p = frame_path;
    while (*p && n < 16) {
        const char *slash = strchr(p, '/');
        segs[n] = p;
        segl[n] = slash ? (int32_t)(slash - p) : (int32_t)strlen(p);
        n++;
        if (!slash) break;
        p = slash + 1;
    }
    /* Innermost first: out = out * M[seg] (= M_inner * … * M_outer). */
    for (int k = n - 1; k >= 0; k--) {
        int fi = cm_find_frame(xf, segs[k], segl[k]);
        if (fi < 0) continue;
        float tmp[16];
        mat4_mul(tmp, out, xf->frames[fi].transform);
        memcpy(out, tmp, sizeof tmp);
    }
}

static void cm_transform_point(float ox[3], const float in[3], const float M[16])
{
    ox[0] = in[0]*M[0] + in[1]*M[4] + in[2]*M[8]  + M[12];
    ox[1] = in[0]*M[1] + in[1]*M[5] + in[2]*M[9]  + M[13];
    ox[2] = in[0]*M[2] + in[1]*M[6] + in[2]*M[10] + M[14];
}

/* ───── material-name → type classification (FUN_00471d45 keyword chain) ──
 *
 * Prefix match in the engine's chain order; first hit wins.  An empty/NULL
 * or unmatched name is type 0 (generic solid).  `hikari`(light) shares the
 * non-colliding code 4 with `nohit`. */
int collision_classify_material(const char *name)
{
    if (!name || !name[0]) return 0;
    static const struct { const char *kw; int code; } chain[] = {
        { "mizu",     5 },
        { "gake",     6 },
        { "toumei",   7 },
        { "kabe",     7 },
        { "yuki_",    6 },
        { "dame",     2 },
        { "Plane",    2 },
        { "hit",      3 },
        { "hikari",   4 },
        { "nohit",    4 },
        { "crystal", 15 },
        { "taimatu", 16 },
        { "takara",  13 },
        { "taru",    12 },
        { "shokudai",14 },
        { "tree01",   8 },
        { "tree02",   9 },
        { "kusa01",  10 },
        { "kusa02",  11 },
    };
    for (size_t i = 0; i < sizeof chain / sizeof chain[0]; i++) {
        size_t kl = strlen(chain[i].kw);
        if (strncmp(name, chain[i].kw, kl) == 0) return chain[i].code;
    }
    return 0;
}

/* The MeshMaterialList position `idx` → material-reference name (refs first,
 * then inline materials), matching mesh.c's build_face_material_map layout. */
static const char *cm_material_name(const xfile_mesh *xm, int32_t idx)
{
    if (xm->material_count <= 0) return NULL;
    if (idx < 0 || idx >= xm->material_count) idx = 0;
    if (idx < xm->material_ref_count) return xm->material_refs[idx];
    int32_t ii = idx - xm->material_ref_count;
    if (xm->inline_materials && ii >= 0 && ii < xm->inline_material_count)
        return xm->inline_materials[ii].name;
    return NULL;
}

static int cm_face_type(const xfile_mesh *xm, int32_t face)
{
    if (xm->material_count <= 0) return 0;
    int32_t idx = (face < xm->face_material_count) ? xm->face_material_indexes[face] : 0;
    return collision_classify_material(cm_material_name(xm, idx));
}

/* ───── per-triangle record build (FUN_00432ac6) ──────────────────────────
 *
 * Inputs a,b,c are the three art-space vertices with the ×0.2 collision scale
 * ALREADY applied (the engine's vertex pool DAT_0432a754).  The record negates
 * X so its geometry lives in player/world space.  Plane normal = (C−B)×(B−A);
 * d = −n·A; nlen2 = |n|².  AABB is padded per `pad`. */
void collision_tri_build(collision_tri *t,
                         const float a[3], const float b[3], const float c[3],
                         int type, collision_pad_mode pad)
{
    /* World-space verts: X negated, Y/Z as-is (FUN_00432ac6). */
    const float Ax = -a[0], Ay = a[1], Az = a[2];
    const float Bx = -b[0], By = b[1], Bz = b[2];
    const float Cx = -c[0], Cy = c[1], Cz = c[2];

    t->v[0][0] = Ax; t->v[0][1] = Ay; t->v[0][2] = Az;
    t->v[1][0] = Bx; t->v[1][1] = By; t->v[1][2] = Bz;
    t->v[2][0] = Cx; t->v[2][1] = Cy; t->v[2][2] = Cz;

    /* Edge vectors (record[0x12..0x1d]): AB, BC, CA. */
    const float abx = Bx - Ax, aby = By - Ay, abz = Bz - Az;
    const float bcx = Cx - Bx, bcy = Cy - By, bcz = Cz - Bz;
    t->edge[0][0] = abx; t->edge[0][1] = aby; t->edge[0][2] = abz;
    t->edge[1][0] = bcx; t->edge[1][1] = bcy; t->edge[1][2] = bcz;
    t->edge[2][0] = Ax - Cx; t->edge[2][1] = Ay - Cy; t->edge[2][2] = Az - Cz;

    /* Normal = (C−B) × (B−A) = edge_BC × edge_AB  (FUN_00432ac6 L111-115). */
    t->n[0] = bcy * abz - bcz * aby;
    t->n[1] = bcz * abx - bcx * abz;
    t->n[2] = bcx * aby - bcy * abx;
    t->d     = -(t->n[0]*Ax + t->n[1]*Ay + t->n[2]*Az);
    t->nlen2 =   t->n[0]*t->n[0] + t->n[1]*t->n[1] + t->n[2]*t->n[2];

    /* Unpadded AABB over the three verts. */
    float lo[3], hi[3];
    for (int k = 0; k < 3; k++) {
        float va = t->v[0][k], vb = t->v[1][k], vc = t->v[2][k];
        lo[k] = va < vb ? va : vb; if (vc < lo[k]) lo[k] = vc;
        hi[k] = va > vb ? va : vb; if (vc > hi[k]) hi[k] = vc;
    }
    /* Pad (FUN_00432ac6 L79-95).  Y uses an asymmetric pad (head clearance). */
    float pxz, pylo, pyhi;
    if (pad == COLLISION_PAD_SMALL) { pxz = 0.5f; pylo = 0.5f; pyhi = 3.0f; }
    else                           { pxz = 1.0f; pylo = 5.0f; pyhi = 10.0f; }
    t->aabb_min[0] = lo[0] - pxz; t->aabb_max[0] = hi[0] + pxz;
    t->aabb_min[1] = lo[1] - pylo; t->aabb_max[1] = hi[1] + pyhi;
    t->aabb_min[2] = lo[2] - pxz; t->aabb_max[2] = hi[2] + pxz;

    t->type = type;
}

/* ───── object build (FUN_0043289b): all meshes' faces → triangle records ─ */

int collision_object_build(collision_object *out, const xfile_t *xf,
                           collision_pad_mode pad)
{
    memset(out, 0, sizeof *out);
    if (!xf) return 0;

    /* Upper-bound the triangle count (all faces across all meshes). */
    int total_faces = 0;
    for (int32_t mi = 0; mi < xf->mesh_count; mi++)
        total_faces += xf->meshes[mi].face_count;
    if (total_faces <= 0) return 0;

    out->tris = (collision_tri *)malloc((size_t)total_faces * sizeof *out->tris);
    if (!out->tris) return 1;

    float vlo[3] = {  1e30f,  1e30f,  1e30f };
    float vhi[3] = { -1e30f, -1e30f, -1e30f };
    int n = 0;

    for (int32_t mi = 0; mi < xf->mesh_count; mi++) {
        const xfile_mesh *xm = &xf->meshes[mi];
        if (xm->vertex_count <= 0 || xm->face_count <= 0) continue;

        float M[16];
        cm_accumulate_frame(xf, xm->frame_path, M);

        for (int32_t fi = 0; fi < xm->face_count; fi++) {
            const xfile_face *f = &xm->faces[fi];
            if (f->count != 3) continue;        /* corpus is fully triangulated */

            int type = cm_face_type(xm, fi);
            if (type == 4) continue;            /* nohit/hikari — dropped */

            float w[3][3];                      /* art-space, ×0.2, transformed */
            int ok = 1;
            for (int k = 0; k < 3; k++) {
                int32_t vidx = f->verts[k];
                if (vidx < 0 || vidx >= xm->vertex_count) { ok = 0; break; }
                const xfile_vec3 *src = &xm->vertices[vidx];
                float in[3] = { src->x, src->y, src->z };
                float tp[3];
                cm_transform_point(tp, in, M);
                w[k][0] = tp[0] * COLLISION_MESH_SCALE;
                w[k][1] = tp[1] * COLLISION_MESH_SCALE;
                w[k][2] = tp[2] * COLLISION_MESH_SCALE;
            }
            if (!ok) continue;

            collision_tri *t = &out->tris[n++];
            collision_tri_build(t, w[0], w[1], w[2], type, pad);

            for (int vi = 0; vi < 3; vi++)
                for (int k = 0; k < 3; k++) {
                    if (t->v[vi][k] < vlo[k]) vlo[k] = t->v[vi][k];
                    if (t->v[vi][k] > vhi[k]) vhi[k] = t->v[vi][k];
                }
        }
    }

    out->tri_count = n;
    if (n == 0) {
        memset(out->aabb_min, 0, sizeof out->aabb_min);
        memset(out->aabb_max, 0, sizeof out->aabb_max);
    } else {
        memcpy(out->aabb_min, vlo, sizeof vlo);
        memcpy(out->aabb_max, vhi, sizeof vhi);
    }
    return 0;
}

void collision_object_free(collision_object *o)
{
    if (!o) return;
    free(o->tris);
    o->tris = NULL;
    o->tri_count = 0;
}

void collision_mesh_free(collision_mesh *m)
{
    if (!m) return;
    for (int i = 0; i < m->object_count; i++)
        collision_object_free(&m->objects[i]);
    free(m->objects);
    m->objects = NULL;
    m->object_count = 0;
}
