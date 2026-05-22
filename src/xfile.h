/*
 * xfile.h — pure-C parser for DirectX retained-mode `.x` text format
 * (xof 0303txt 0032), the static-mesh format Recettear ships under
 * `xfile/` (223 files, 17 MB).
 *
 * Mirrors the Python oracle at `tools/extract/xfile.py` — same templates
 * recognized, same skip-and-count behaviour for unknown templates.
 * Skinning (SkinWeights / XSkinMeshHeader) and animation (Animation /
 * AnimationSet / AnimationKey) are silently skipped; they ship only in
 * `xfile2/` for character meshes and land with the character-rendering
 * port months out.
 *
 * Spec: docs/formats/xfile.md (includes the four exporter quirks the
 * Python oracle surfaced — vertex-color separator polymorphism, mesh
 * material-list face_indexes terminator variance, reference-block
 * syntax, hyphen-in-identifier stitch).
 *
 * Strategy rationale: docs/findings/mesh-loader.md (we skip d3dxof and
 * D3DX8 entirely — mingw has no D3DX8 headers and writing the parser
 * gives us deterministic, testable output we can compare against the
 * Python oracle byte-for-byte).
 */

#ifndef OPENRECET_XFILE_H
#define OPENRECET_XFILE_H

#include <stddef.h>
#include <stdint.h>

#define XFILE_NAME_MAX       64
#define XFILE_TEXTURE_MAX    256
#define XFILE_MAX_FACE_VERTS 16   /* observed max in corpus is 3 (triangulated); spec allows >3 */

typedef struct { float x, y, z; }      xfile_vec3;
typedef struct { float u, v; }         xfile_vec2;
typedef struct { float r, g, b, a; }   xfile_rgba;
typedef struct { float r, g, b; }      xfile_rgb;

/*
 * One face: an N-gon indexing into the per-mesh vertex array.
 *
 * Recettear's exporter triangulates everything (count == 3 across all
 * 87,029 faces in the corpus), but the spec allows >3 and we keep the
 * generic shape so the parser doesn't break on data we haven't seen.
 * Overflow past XFILE_MAX_FACE_VERTS is a hard parse error.
 */
typedef struct {
    int32_t count;
    int32_t verts[XFILE_MAX_FACE_VERTS];
} xfile_face;

/*
 * One material: a copy of the on-disk Material instance.
 *
 * `texture` is empty ("") when the Material has no TextureFilename
 * sub-template. `name` is empty for anonymous inline materials (those
 * defined inside a MeshMaterialList body without an instance name).
 *
 * Layout deliberately mirrors D3DMATERIAL8 (Diffuse RGBA / Power /
 * Specular RGB / Emissive RGB = 17 floats = 68 bytes), so the C4 mesh
 * upload step can `memcpy` these into D3D8 material slots after the
 * engine's "ambient = diffuse" duplication.
 */
typedef struct {
    char       name[XFILE_NAME_MAX];
    xfile_rgba diffuse;
    float      power;
    xfile_rgb  specular;
    xfile_rgb  emissive;
    char       texture[XFILE_TEXTURE_MAX];
} xfile_material;

/*
 * One Mesh{} instance from the file.
 *
 * Frames are NOT collapsed: parented meshes preserve their parent
 * Frame's name path via `frame_path` (slash-separated). The caller is
 * responsible for applying inherited transforms if it wants the
 * D3DX-equivalent merged buffer.
 *
 * Pointer fields are owned by the parser; xfile_free() releases them.
 * NULL means the corresponding sub-template wasn't present (e.g.
 * `normals == NULL` for a Mesh without MeshNormals).
 */
typedef struct {
    char     name[XFILE_NAME_MAX];
    char     frame_path[256];        /* "/A/B/Box01"; empty for top-level meshes */

    int32_t       vertex_count;
    xfile_vec3   *vertices;

    int32_t       face_count;
    xfile_face   *faces;

    int32_t       normal_count;
    xfile_vec3   *normals;
    xfile_face   *face_normals;       /* face_count entries when present */

    int32_t       uv_count;
    xfile_vec2   *uvs;

    int32_t       vertex_color_count; /* MeshVertexColors header count; data not exposed yet */

    /* MeshMaterialList */
    int32_t       material_count;      /* nMaterials header value */
    int32_t       face_material_count; /* face_indexes[] length */
    int32_t      *face_material_indexes;
    int32_t       material_ref_count;
    char        (*material_refs)[XFILE_NAME_MAX];
    int32_t       inline_material_count;
    xfile_material *inline_materials;
} xfile_mesh;

/*
 * Frame metadata. We don't expose the recursive child hierarchy
 * directly — the flat `frames[]` array holds every Frame instance
 * (top-level + nested in DFS order), and child relationships are
 * recoverable via `children_names`.
 */
typedef struct {
    char    name[XFILE_NAME_MAX];
    float   transform[16];                       /* identity if no FrameTransformMatrix */
    int32_t has_transform;                       /* 0 = identity, 1 = explicit */
    int32_t child_count;
    char  (*children_names)[XFILE_NAME_MAX];
    int32_t mesh_count;
} xfile_frame;

typedef struct {
    char           path[512];
    size_t         size_bytes;
    char           header_version[8];
    char           header_encoding[8];
    int            header_float_size;

    int32_t        mesh_count;
    xfile_mesh    *meshes;

    int32_t        frame_count;
    xfile_frame   *frames;

    int32_t        global_material_count;
    xfile_material *global_materials;

    int32_t        texture_count;
    char         (*textures)[XFILE_TEXTURE_MAX];

    /* Empty on success; populated with "file:line: message" on first
     * parse error (parsing stops, partial data is preserved). */
    char           error[256];
} xfile_t;

/*
 * Parse a `.x` text-format file from a memory buffer.
 *
 *   data, len           — file contents (the parser never writes past `len`)
 *   path_for_errors     — used in error messages; may be NULL
 *
 * Returns:
 *   non-NULL xfile_t* always (unless OOM, in which case NULL).
 *   On parse error, `error[0]` is non-zero and partial data is in the
 *   returned struct (caller still must xfile_free it).
 */
xfile_t *xfile_parse(const char *data, size_t len, const char *path_for_errors);

void xfile_free(xfile_t *x);

#endif /* OPENRECET_XFILE_H */
