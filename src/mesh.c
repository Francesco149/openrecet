/*
 * mesh.c — pure-C mesh build from xfile_t + Win32 D3D8 upload.
 *
 * Build flow (mesh_build_from_xfile):
 *   1. Walk xfile_t::meshes once to discover material order.
 *      Materials referenced by name resolve against xfile_t::global_materials;
 *      inline materials inside MeshMaterialList append to the local list.
 *      A material referenced from N meshes deduplicates into one slot.
 *   2. For each xfile_mesh, group faces by material index, then per
 *      (mesh, material) pair emit one mesh_submesh whose vertices are
 *      the *expanded* (pos, normal, white, uv) tuples for every face
 *      vertex (no welding pass — we emit 3 vertices per triangle,
 *      simple and visually correct).
 *   3. Indices are 0,1,2 / 3,4,5 / ... within each submesh (offsets
 *      relative to the submesh's vertex_offset).
 *   4. Normals: if MeshNormals is present, look up via face_normals.
 *      Otherwise zero. UVs: if MeshTextureCoords is present, look up
 *      by position index. Otherwise zero.
 *   5. Frame transforms ignored — see mesh.h TODO for C7.
 *
 * mesh_compute_bounds is the FUN_004aaad7 (centroid + max-radius) port.
 */

#include "mesh.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <d3d8.h>
#endif

/* ───── Error helper ──────────────────────────────────────────────────── */

static void mesh_set_error(mesh_t *m, const char *fmt, ...)
{
    if (m->error[0]) return;   /* keep first error */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->error, sizeof m->error, fmt, ap);
    va_end(ap);
}

/* ───── Material registry (per-mesh build) ────────────────────────────── */

static int find_global_material(const xfile_t *xf, const char *name)
{
    if (!name || !name[0]) return -1;
    for (int32_t i = 0; i < xf->global_material_count; i++) {
        if (strcmp(xf->global_materials[i].name, name) == 0) return i;
    }
    return -1;
}

/*
 * Append a material to mesh m's list, returning its index.
 * If `template` is non-NULL, it's the source xfile_material to copy.
 * If a material with the same .name already exists, returns the existing index.
 */
static int register_material(mesh_t *m, const xfile_material *src,
                             int32_t *cap_inout)
{
    if (!src) return -1;
    if (src->name[0]) {
        for (int32_t i = 0; i < m->material_count; i++) {
            if (strcmp(m->materials[i].name, src->name) == 0) return i;
        }
    }
    if (m->material_count >= *cap_inout) {
        int32_t nc = *cap_inout ? *cap_inout * 2 : 4;
        xfile_material *nm = (xfile_material *)realloc(m->materials, (size_t)nc * sizeof *nm);
        if (!nm) { mesh_set_error(m, "oom materials"); return -1; }
        m->materials = nm;
        *cap_inout = nc;
    }
    int idx = m->material_count++;
    m->materials[idx] = *src;
    return idx;
}

/* For one xfile_mesh, build a face → local-material-index table.
 * Length is face_count; -1 means "no material for this face".
 *
 * Strategy:
 *   - xf_mesh.material_count = nMaterials header from MeshMaterialList
 *   - face_material_indexes maps face -> [0, nMaterials)
 *   - The N materials come from material_refs[] and inline_materials[]
 *     in some order. The .x spec doesn't define ordering between refs
 *     and inlines beyond "as they appear in the file body". Our Python
 *     oracle stores them in two separate lists; we need to recover the
 *     interleaved order to map face_material_index correctly.
 *
 * Compromise: assume refs come first, then inlines (matches the spec
 * example ordering and what the ice01.x exporter does). If this proves
 * wrong for some file in the corpus, the symptom will be wrong-material
 * faces — visible during C7 render.
 */
static int build_face_material_map(mesh_t *m, const xfile_t *xf,
                                   const xfile_mesh *xm,
                                   int32_t *cap_inout,
                                   int32_t **out_map)
{
    *out_map = NULL;
    if (xm->face_count <= 0) return 1;

    /* Build local-to-mesh material index translation: position N in the
     * MeshMaterialList → mesh_t local material index. */
    int32_t nlocal = xm->material_count;
    if (nlocal <= 0) {
        /* No MeshMaterialList — all faces use material -1 */
        int32_t *map = (int32_t *)calloc((size_t)xm->face_count, sizeof *map);
        if (!map) { mesh_set_error(m, "oom face_mat map"); return 0; }
        for (int32_t i = 0; i < xm->face_count; i++) map[i] = -1;
        *out_map = map;
        return 1;
    }

    int32_t *local_to_mesh = (int32_t *)malloc((size_t)nlocal * sizeof *local_to_mesh);
    if (!local_to_mesh) { mesh_set_error(m, "oom l2m"); return 0; }
    for (int32_t i = 0; i < nlocal; i++) local_to_mesh[i] = -1;

    /* refs first */
    int32_t pos = 0;
    for (int32_t i = 0; i < xm->material_ref_count && pos < nlocal; i++, pos++) {
        int g = find_global_material(xf, xm->material_refs[i]);
        if (g >= 0) {
            local_to_mesh[pos] = register_material(m, &xf->global_materials[g], cap_inout);
        } else {
            /* Unknown reference — leave -1 */
        }
    }
    /* then inlines */
    for (int32_t i = 0; i < xm->inline_material_count && pos < nlocal; i++, pos++) {
        local_to_mesh[pos] = register_material(m, &xm->inline_materials[i], cap_inout);
    }

    /* Translate per-face index. */
    int32_t *map = (int32_t *)calloc((size_t)xm->face_count, sizeof *map);
    if (!map) { free(local_to_mesh); mesh_set_error(m, "oom face_mat map"); return 0; }
    for (int32_t i = 0; i < xm->face_count; i++) {
        int32_t idx = (i < xm->face_material_count) ? xm->face_material_indexes[i] : 0;
        if (idx < 0 || idx >= nlocal) idx = 0;
        map[i] = (nlocal > 0) ? local_to_mesh[idx] : -1;
    }
    free(local_to_mesh);
    *out_map = map;
    return 1;
}

/* ───── Vertex emit helpers ───────────────────────────────────────────── */

static int grow_vertices(mesh_t *m, int32_t need, int32_t *cap_inout)
{
    if (need <= *cap_inout) return 1;
    int32_t nc = *cap_inout ? *cap_inout : 64;
    while (nc < need) nc *= 2;
    mesh_vertex *nv = (mesh_vertex *)realloc(m->vertices, (size_t)nc * sizeof *nv);
    if (!nv) { mesh_set_error(m, "oom vertices"); return 0; }
    m->vertices = nv;
    *cap_inout = nc;
    return 1;
}

static int grow_indices(mesh_t *m, int32_t need, int32_t *cap_inout)
{
    if (need <= *cap_inout) return 1;
    int32_t nc = *cap_inout ? *cap_inout : 64;
    while (nc < need) nc *= 2;
    uint16_t *ni = (uint16_t *)realloc(m->indices, (size_t)nc * sizeof *ni);
    if (!ni) { mesh_set_error(m, "oom indices"); return 0; }
    m->indices = ni;
    *cap_inout = nc;
    return 1;
}

static int grow_submeshes(mesh_t *m, int32_t need, int32_t *cap_inout)
{
    if (need <= *cap_inout) return 1;
    int32_t nc = *cap_inout ? *cap_inout : 4;
    while (nc < need) nc *= 2;
    mesh_submesh *ns = (mesh_submesh *)realloc(m->submeshes, (size_t)nc * sizeof *ns);
    if (!ns) { mesh_set_error(m, "oom submeshes"); return 0; }
    m->submeshes = ns;
    *cap_inout = nc;
    return 1;
}

/* ───── Build one submesh: (mesh, material) → expanded triangle list ──── */

static int emit_submesh(mesh_t *m, const xfile_mesh *xm,
                        int32_t mat_index, const int32_t *face_mat_map,
                        int32_t *v_cap, int32_t *i_cap, int32_t *s_cap)
{
    /* Count matching faces (and total tris after triangulation). */
    int32_t tri_count = 0;
    for (int32_t f = 0; f < xm->face_count; f++) {
        if (face_mat_map[f] != mat_index) continue;
        int32_t fc = xm->faces[f].count;
        if (fc >= 3) tri_count += fc - 2;
    }
    if (tri_count == 0) return 1;   /* nothing to emit */

    if (!grow_submeshes(m, m->submesh_count + 1, s_cap)) return 0;
    mesh_submesh *sm = &m->submeshes[m->submesh_count];
    sm->vertex_offset  = m->vertex_count;
    sm->index_offset   = m->index_count;
    sm->material_index = mat_index;

    int32_t v0 = m->vertex_count;
    int32_t i0 = m->index_count;

    for (int32_t f = 0; f < xm->face_count; f++) {
        if (face_mat_map[f] != mat_index) continue;

        const xfile_face *face = &xm->faces[f];
        const xfile_face *fn   = xm->face_normals ? &xm->face_normals[f] : NULL;

        int32_t fc = face->count;
        if (fc < 3) continue;

        /* Triangulate fan: tris are (0, i, i+1) for i = 1 .. fc-2 */
        for (int32_t tri = 0; tri < fc - 2; tri++) {
            int32_t corners[3] = { 0, tri + 1, tri + 2 };
            if (!grow_vertices(m, m->vertex_count + 3, v_cap)) return 0;
            if (!grow_indices(m, m->index_count + 3, i_cap)) return 0;

            for (int c = 0; c < 3; c++) {
                int32_t corner = corners[c];
                int32_t vi = face->verts[corner];
                mesh_vertex *out = &m->vertices[m->vertex_count];
                memset(out, 0, sizeof *out);
                if (vi >= 0 && vi < xm->vertex_count) {
                    out->x = xm->vertices[vi].x;
                    out->y = xm->vertices[vi].y;
                    out->z = xm->vertices[vi].z;
                }
                if (xm->uvs && vi >= 0 && vi < xm->uv_count) {
                    out->u = xm->uvs[vi].u;
                    out->v = xm->uvs[vi].v;
                }
                if (xm->normals && fn) {
                    int32_t ni = fn->verts[corner];
                    if (ni >= 0 && ni < xm->normal_count) {
                        out->nx = xm->normals[ni].x;
                        out->ny = xm->normals[ni].y;
                        out->nz = xm->normals[ni].z;
                    }
                }
                out->diffuse = 0xFFFFFFFFu;
                m->indices[m->index_count] = (uint16_t)(m->vertex_count - v0);
                m->vertex_count++;
                m->index_count++;
            }
        }
    }

    sm->vertex_count = m->vertex_count - v0;
    sm->index_count  = m->index_count  - i0;
    m->submesh_count++;
    return 1;
}

/* ───── Public API ────────────────────────────────────────────────────── */

mesh_t *mesh_build_from_xfile(const xfile_t *xf)
{
    mesh_t *m = (mesh_t *)calloc(1, sizeof *m);
    if (!m) return NULL;
    if (xf->path[0]) {
        strncpy(m->path, xf->path, sizeof m->path - 1);
    }
    if (xf->error[0]) {
        snprintf(m->error, sizeof m->error, "xfile parse error: %.200s", xf->error);
        return m;
    }

    int32_t v_cap = 0, i_cap = 0, s_cap = 0, mat_cap = 0;

    /* For each Mesh{}, build a face → mesh-local material index map,
     * then emit one submesh per distinct material in that mesh. */
    for (int32_t mi = 0; mi < xf->mesh_count; mi++) {
        const xfile_mesh *xm = &xf->meshes[mi];

        int32_t *face_mat_map = NULL;
        if (!build_face_material_map(m, xf, xm, &mat_cap, &face_mat_map)) return m;

        /* Discover distinct material indexes used by this mesh's faces. */
        int32_t seen[64];
        int32_t nseen = 0;
        for (int32_t f = 0; f < xm->face_count; f++) {
            int32_t mat = face_mat_map[f];
            int found = 0;
            for (int32_t s = 0; s < nseen; s++) if (seen[s] == mat) { found = 1; break; }
            if (!found && nseen < (int32_t)(sizeof seen / sizeof seen[0])) {
                seen[nseen++] = mat;
            }
        }
        /* Stable order (sort by integer index, -1 first). */
        for (int32_t a = 0; a < nseen; a++) {
            for (int32_t b = a + 1; b < nseen; b++) {
                if (seen[b] < seen[a]) { int32_t t = seen[a]; seen[a] = seen[b]; seen[b] = t; }
            }
        }

        for (int32_t s = 0; s < nseen; s++) {
            if (!emit_submesh(m, xm, seen[s], face_mat_map,
                              &v_cap, &i_cap, &s_cap))
            {
                free(face_mat_map);
                return m;
            }
        }
        free(face_mat_map);
    }

    /* If a .x file has top-level Materials but no MeshMaterialList ever
     * referenced them, they don't end up in m->materials. That's
     * intentional — the engine likewise only retains materials that
     * faces use. */

    return m;
}

void mesh_compute_bounds(mesh_t *m)
{
    if (!m || m->vertex_count <= 0) {
        if (m) {
            m->centroid[0] = m->centroid[1] = m->centroid[2] = 0.0f;
            m->radius = 0.0f;
            m->has_bounds = 1;
        }
        return;
    }

    /* Mirrors FUN_004aaad7: centroid first, then max-distance second pass. */
    double cx = 0.0, cy = 0.0, cz = 0.0;
    for (int32_t i = 0; i < m->vertex_count; i++) {
        cx += m->vertices[i].x;
        cy += m->vertices[i].y;
        cz += m->vertices[i].z;
    }
    cx /= m->vertex_count; cy /= m->vertex_count; cz /= m->vertex_count;
    m->centroid[0] = (float)cx;
    m->centroid[1] = (float)cy;
    m->centroid[2] = (float)cz;

    double r2 = 0.0;
    for (int32_t i = 0; i < m->vertex_count; i++) {
        double dx = m->vertices[i].x - cx;
        double dy = m->vertices[i].y - cy;
        double dz = m->vertices[i].z - cz;
        double d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > r2) r2 = d2;
    }
    m->radius = (float)sqrt(r2);
    m->has_bounds = 1;
}

void mesh_free(mesh_t *m)
{
    if (!m) return;
#ifdef _WIN32
    mesh_release_d3d8(m);
#endif
    free(m->vertices);
    free(m->indices);
    free(m->submeshes);
    free(m->materials);
    free(m);
}

/* ───── Win32 D3D8 upload ─────────────────────────────────────────────── */
#ifdef _WIN32

long mesh_upload_d3d8(mesh_t *m, struct IDirect3DDevice8 *dev)
{
    if (!m || !dev) return E_INVALIDARG;
    if (m->vertex_count <= 0 || m->index_count <= 0) {
        mesh_set_error(m, "empty mesh (verts=%d, idx=%d)", m->vertex_count, m->index_count);
        return E_FAIL;
    }
    if (m->vb || m->ib) {
        /* already uploaded */
        return S_OK;
    }

    IDirect3DDevice8 *d8 = (IDirect3DDevice8 *)dev;
    DWORD vb_bytes = (DWORD)(m->vertex_count * (int32_t)sizeof(mesh_vertex));
    DWORD ib_bytes = (DWORD)(m->index_count  * (int32_t)sizeof(uint16_t));

    IDirect3DVertexBuffer8 *vb = NULL;
    HRESULT hr = IDirect3DDevice8_CreateVertexBuffer(
        d8, vb_bytes, D3DUSAGE_WRITEONLY,
        MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1, D3DPOOL_MANAGED, &vb);
    if (FAILED(hr) || !vb) {
        mesh_set_error(m, "CreateVertexBuffer failed: 0x%08lx", (unsigned long)hr);
        return hr;
    }

    BYTE *vbp = NULL;
    hr = IDirect3DVertexBuffer8_Lock(vb, 0, vb_bytes, &vbp, 0);
    if (FAILED(hr) || !vbp) {
        IDirect3DVertexBuffer8_Release(vb);
        mesh_set_error(m, "VertexBuffer Lock failed: 0x%08lx", (unsigned long)hr);
        return hr;
    }
    memcpy(vbp, m->vertices, vb_bytes);
    IDirect3DVertexBuffer8_Unlock(vb);

    IDirect3DIndexBuffer8 *ib = NULL;
    hr = IDirect3DDevice8_CreateIndexBuffer(
        d8, ib_bytes, D3DUSAGE_WRITEONLY,
        D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
    if (FAILED(hr) || !ib) {
        IDirect3DVertexBuffer8_Release(vb);
        mesh_set_error(m, "CreateIndexBuffer failed: 0x%08lx", (unsigned long)hr);
        return hr;
    }

    BYTE *ibp = NULL;
    hr = IDirect3DIndexBuffer8_Lock(ib, 0, ib_bytes, &ibp, 0);
    if (FAILED(hr) || !ibp) {
        IDirect3DIndexBuffer8_Release(ib);
        IDirect3DVertexBuffer8_Release(vb);
        mesh_set_error(m, "IndexBuffer Lock failed: 0x%08lx", (unsigned long)hr);
        return hr;
    }
    memcpy(ibp, m->indices, ib_bytes);
    IDirect3DIndexBuffer8_Unlock(ib);

    m->vb = vb;
    m->ib = ib;
    return S_OK;
}

void mesh_release_d3d8(mesh_t *m)
{
    if (!m) return;
    if (m->vb) {
        IDirect3DVertexBuffer8_Release((IDirect3DVertexBuffer8 *)m->vb);
        m->vb = NULL;
    }
    if (m->ib) {
        IDirect3DIndexBuffer8_Release((IDirect3DIndexBuffer8 *)m->ib);
        m->ib = NULL;
    }
}

#endif /* _WIN32 */
