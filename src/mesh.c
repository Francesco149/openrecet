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

#include "math3d.h"

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

/* ───── Frame transform accumulation ───────────────────────────────────
 *
 * .x stores FrameTransformMatrix row-major in source order. The matrix
 * represents a row-vector multiplication: world_pos = local_pos * M.
 *
 * Frames nest: a mesh inside Frame "A/B/C" lives in C's local space, C
 * is in B's space, B is in A's space, A is in world. So:
 *
 *   world_pos = local_pos * M_C * M_B * M_A
 *
 * Accumulated matrix is built innermost-first by left-multiplying each
 * frame's matrix onto an identity:
 *
 *   M_acc = M_C * M_B * M_A
 *
 * Then each vertex applies as `pos' = pos * M_acc` (and normal applies
 * with the upper 3x3 part only — see transform_normal_into below).
 *
 * Why this matters: shop_1st.x has 48 submeshes laid out via per-frame
 * translations + scales + occasional rotations (Box02 at (+23, 0, -25)
 * scaled 2.4×2.2×3.9, Box111 at (+51, +27, +38) with a column-swap
 * rotation, etc.). Without applying these, every submesh collapses to
 * origin and the rendered house is a jumble.
 */

static int find_frame_by_name(const xfile_t *xf, const char *name, int32_t name_len)
{
    if (!name || name_len <= 0) return -1;
    for (int32_t i = 0; i < xf->frame_count; i++) {
        const char *fn = xf->frames[i].name;
        if ((int32_t)strlen(fn) == name_len && memcmp(fn, name, (size_t)name_len) == 0) {
            return i;
        }
    }
    return -1;
}

static void accumulate_frame_transform(const xfile_t *xf, const char *frame_path,
                                       float out[16])
{
    static const float ident[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    memcpy(out, ident, sizeof ident);
    if (!frame_path || !frame_path[0]) return;

    /* Walk the path collecting segment offsets. Segments come outermost
     * → innermost in source order ("World/Box02/InnerThing"). We need
     * to compose innermost-first, so we record offsets then walk in
     * reverse. */
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

    /* Innermost first: start at identity, then out = out * M[seg]
     * accumulates as M_inner * M_next * ... * M_outer in row-vector
     * convention. */
    for (int k = n - 1; k >= 0; k--) {
        int fi = find_frame_by_name(xf, segs[k], segl[k]);
        if (fi < 0) continue;        /* unknown frame name → identity */
        float tmp[16];
        mat4_mul(tmp, out, xf->frames[fi].transform);
        memcpy(out, tmp, sizeof tmp);
    }
}

static void transform_point_into(float ox[3], const float in[3], const float M[16])
{
    /* v=(x,y,z,1) row-vector × row-major M. */
    ox[0] = in[0]*M[0] + in[1]*M[4] + in[2]*M[8]  + M[12];
    ox[1] = in[0]*M[1] + in[1]*M[5] + in[2]*M[9]  + M[13];
    ox[2] = in[0]*M[2] + in[1]*M[6] + in[2]*M[10] + M[14];
}

/* Pack an RGBA float quad (0..1, clamped) into a D3DCOLOR DWORD
 * (0xAARRGGBB), the on-disk diffuse layout in `mesh_vertex` (FVF 0x152
 * D3DFVF_DIFFUSE). Matches D3DXCOLORVALUETOUBYTE: round-to-nearest,
 * clamp at 0/255. */
static uint32_t pack_rgba(float r, float g, float b, float a)
{
    int ir = (int)(r * 255.0f + 0.5f);
    int ig = (int)(g * 255.0f + 0.5f);
    int ib = (int)(b * 255.0f + 0.5f);
    int ia = (int)(a * 255.0f + 0.5f);
    if (ir < 0) ir = 0;
    if (ir > 255) ir = 255;
    if (ig < 0) ig = 0;
    if (ig > 255) ig = 255;
    if (ib < 0) ib = 0;
    if (ib > 255) ib = 255;
    if (ia < 0) ia = 0;
    if (ia > 255) ia = 255;
    return ((uint32_t)ia << 24) | ((uint32_t)ir << 16)
         | ((uint32_t)ig <<  8) | (uint32_t)ib;
}

static void transform_normal_into(float ox[3], const float in[3], const float M[16])
{
    /* Upper 3x3 only (no translation). Renormalised at the end so the
     * shading magnitude survives non-uniform scales — direction is
     * approximate under shear/non-uniform scale (proper handling needs
     * the inverse-transpose of the 3x3 part). For shop_1st.x's mix of
     * pure rotations + per-frame uniform-ish scales this is good
     * enough; revisit if normal-mapped models surface in the corpus. */
    float x = in[0]*M[0] + in[1]*M[4] + in[2]*M[8];
    float y = in[0]*M[1] + in[1]*M[5] + in[2]*M[9];
    float z = in[0]*M[2] + in[1]*M[6] + in[2]*M[10];
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 1e-12f) { x /= len; y /= len; z /= len; }
    ox[0] = x; ox[1] = y; ox[2] = z;
}

/* ───── Build one submesh: (mesh, material) → expanded triangle list ──── */

static int emit_submesh(mesh_t *m, const xfile_mesh *xm,
                        int32_t mat_index, const int32_t *face_mat_map,
                        const float frame_M[16], int frame_M_is_identity,
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
                    float pin[3]  = { xm->vertices[vi].x,
                                      xm->vertices[vi].y,
                                      xm->vertices[vi].z };
                    float pout[3];
                    if (frame_M_is_identity) {
                        pout[0] = pin[0]; pout[1] = pin[1]; pout[2] = pin[2];
                    } else {
                        transform_point_into(pout, pin, frame_M);
                    }
                    out->x = pout[0];
                    out->y = pout[1];
                    out->z = pout[2];
                }
                if (xm->uvs && vi >= 0 && vi < xm->uv_count) {
                    out->u = xm->uvs[vi].u;
                    out->v = xm->uvs[vi].v;
                }
                if (xm->normals && fn) {
                    int32_t ni = fn->verts[corner];
                    if (ni >= 0 && ni < xm->normal_count) {
                        float nin[3] = { xm->normals[ni].x,
                                         xm->normals[ni].y,
                                         xm->normals[ni].z };
                        float nout[3];
                        if (frame_M_is_identity) {
                            nout[0] = nin[0]; nout[1] = nin[1]; nout[2] = nin[2];
                        } else {
                            transform_normal_into(nout, nin, frame_M);
                        }
                        out->nx = nout[0];
                        out->ny = nout[1];
                        out->nz = nout[2];
                    }
                }
                /* MeshVertexColors → D3DCOLOR (0xAARRGGBB). Per-vertex
                 * lookup by the position-index (vi); xfile parser
                 * defaults uncovered slots to (1,1,1,1) and leaves
                 * the pointer NULL only when the .x file has no
                 * MeshVertexColors block at all. */
                if (xm->vertex_colors && vi >= 0 && vi < xm->vertex_count) {
                    const xfile_rgba *c = &xm->vertex_colors[vi];
                    out->diffuse = pack_rgba(c->r, c->g, c->b, c->a);
                } else {
                    out->diffuse = 0xFFFFFFFFu;
                }
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

        /* Accumulated Frame transform for this mesh. Top-level meshes
         * (empty frame_path) skip the math entirely via the
         * is_identity flag. */
        float frame_M[16];
        int   frame_M_is_identity = 0;
        if (xm->frame_path[0]) {
            accumulate_frame_transform(xf, xm->frame_path, frame_M);
        } else {
            static const float ident[16] = {
                1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
            };
            memcpy(frame_M, ident, sizeof ident);
            frame_M_is_identity = 1;
        }

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
                              frame_M, frame_M_is_identity,
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
    free(m->texture_slots);
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
