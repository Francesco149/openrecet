/*
 * mesh.h — D3D8-ready mesh built from a parsed `.x` file (xfile_t).
 *
 * Takes the per-Mesh{} block data the parser produced and flattens it
 * into a single (vertex buffer, index buffer, material list, submesh
 * descriptors) tuple ready to render. Each submesh is one (Mesh{},
 * material) pair so the renderer can SetTexture+SetMaterial then draw
 * a contiguous index range — matching the engine's D3DX attribute-table
 * model without us having to reimplement ID3DXMesh::DrawSubset.
 *
 * Frame transforms are NOT pre-applied yet — vertices are stored in
 * each Mesh's local space. Most shipping .x files in the corpus put
 * vertex positions directly in world space (Frame transforms identity
 * or just translation that the engine reads from level/stage state
 * instead), so this is fine for the AAB/C0A worker-body unblock. TODO
 * for C7 render: accumulate world_transform per Mesh by walking the
 * Frame DFS during parse and apply at mesh_build_from_xfile time.
 * Diffuse channel defaults to white (0xFFFFFFFF); per-vertex colours
 * from MeshVertexColors are not consumed yet (the corpus's 1860 vertex
 * colour blocks are all rendered as flat-colour anyway).
 *
 * Vertex layout matches FVF 0x152 = D3DFVF_XYZ | D3DFVF_NORMAL |
 * D3DFVF_DIFFUSE | D3DFVF_TEX1 — same FVF the engine's
 * D3DXLoadMeshFromXof produces in FUN_00472836 (the literal `0x152`
 * compare at engine line 350 of the decompiled function).
 *
 * D3D8 upload is Win32-only and lives in the same module behind
 * `#ifdef _WIN32`; the pure-C build + bounds path is fully unit-tested
 * under ASan on the host.
 */

#ifndef OPENRECET_MESH_H
#define OPENRECET_MESH_H

#include <stddef.h>
#include <stdint.h>

#include "xfile.h"

#define MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1  0x152u

/*
 * One vertex. 36 bytes laid out for FVF 0x152.
 */
typedef struct {
    float    x,  y,  z;
    float    nx, ny, nz;
    uint32_t diffuse;
    float    u, v;
} mesh_vertex;

/*
 * One submesh: a contiguous range of indices into the parent mesh's
 * vertex and index buffers, all sharing one material. Drawn as a
 * triangle list.
 */
typedef struct {
    int32_t vertex_offset;
    int32_t vertex_count;
    int32_t index_offset;
    int32_t index_count;
    int32_t material_index;   /* -1 if no material assigned */
} mesh_submesh;

/*
 * A renderable mesh, owning its CPU-side buffers and (optionally) the
 * D3D8 GPU resources after mesh_upload_d3d8.
 */
typedef struct {
    char           path[512];     /* source .x file, for diagnostics */

    int32_t        vertex_count;
    mesh_vertex   *vertices;

    int32_t        index_count;
    uint16_t      *indices;       /* triangle-list; 16-bit (no shipping .x exceeds 65535) */

    int32_t        submesh_count;
    mesh_submesh  *submeshes;

    int32_t        material_count;
    xfile_material *materials;     /* materials in MeshMaterialList ref order, with global lookups resolved */

    /* One slot per material, parallel to materials[]. -1 if the material
     * has no TextureFilename. Otherwise an index into the global texture
     * cache populated by mesh_load (src/mesh_load.{c,h}); see the
     * engine's param_1[1] (`texture_indices`) at FUN_00472836:301. Stays
     * NULL when the mesh is built without going through mesh_load. */
    int32_t       *texture_slots;

    float          centroid[3];
    float          radius;        /* max distance from centroid to any vertex */
    int32_t        has_bounds;    /* 0 = not yet computed by mesh_compute_bounds */

    /* D3D8 GPU resources. NULL until mesh_upload_d3d8 succeeds; set
     * back to NULL on mesh_release_d3d8. Opaque void* on non-Win32
     * builds. */
    void          *vb;            /* IDirect3DVertexBuffer8* */
    void          *ib;            /* IDirect3DIndexBuffer8*  */

    /* Empty on success; populated on build failure. */
    char           error[256];
} mesh_t;

/*
 * Build a mesh_t from a parsed .x file. Returns non-NULL on success
 * (check m->error[0]); NULL only on allocation failure.
 *
 * The xfile_t MAY be freed after this call — mesh_build_from_xfile
 * copies everything it needs.
 */
mesh_t *mesh_build_from_xfile(const xfile_t *xf);

/*
 * Compute centroid + radius across all vertices. Idempotent.
 * Mirrors the engine's FUN_004aaad7 (centroid + max-radius pass).
 */
void mesh_compute_bounds(mesh_t *m);

/*
 * Free CPU buffers + D3D8 resources (if uploaded). Safe on NULL.
 */
void mesh_free(mesh_t *m);

#ifdef _WIN32
/* Forward-decl to avoid pulling <d3d8.h> into the public header.
 * mesh.c includes it. */
struct IDirect3DDevice8;

/*
 * Create and fill VB + IB on the GPU. Sets m->vb and m->ib on success.
 * Returns 0 on success, non-zero HRESULT on failure (m->error[] also
 * populated).
 */
long mesh_upload_d3d8(mesh_t *m, struct IDirect3DDevice8 *dev);

/* Release GPU resources without freeing the CPU buffers. Safe to call
 * repeatedly. */
void mesh_release_d3d8(mesh_t *m);
#endif

#endif /* OPENRECET_MESH_H */
