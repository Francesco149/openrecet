/*
 * tools/dump-our-mesh/main.c — host-side mesh dumper for parser
 * bit-diff validation. Mirrors the binary format produced by
 * tools/dump-retail-meshes.py so tools/diff-mesh.py can compare the
 * two side by side.
 *
 * What it does:
 *
 *   1. Read a .x file via xfile_parse (the openrecet pure-C parser).
 *   2. Run mesh_build_from_xfile (triangulation + per-submesh
 *      grouping + Frame-transform application).
 *   3. Compute centroid + radius via mesh_compute_bounds.
 *   4. Write three files into the output dir:
 *
 *        vb.bin     — FVF-0x152 vertex stream, 36 bytes per vertex,
 *                     COUNT = m->vertex_count. Layout matches the
 *                     engine's D3DXLoadMeshFromXof output exactly
 *                     (same FVF, same struct field order — see
 *                     mesh_vertex in src/mesh.h).
 *
 *        ib.bin     — Flattened uint16 index stream. Our internal
 *                     indices are submesh-local (0..N-1 within each
 *                     submesh, BaseVertexIndex = submesh.vertex_offset
 *                     at draw time); for the dump we add the offset
 *                     so the IB becomes globally addressable into VB,
 *                     matching retail's representation.
 *
 *        info.json  — { path, num_vertices, num_faces, fvf=338,
 *                       options=0, vert_size=36, index_size=2 }.
 *                     `options` is left zero because we don't carry
 *                     D3DXMESH_* equivalents; the field is preserved
 *                     to keep info.json schema-compatible with
 *                     dump-retail-meshes.py output.
 *
 * Usage:
 *
 *   ./build/dump-our-mesh <input.x> <output_dir>
 *
 * The dumper expects the .x to live on disk (no storage/lnkdatas
 * lookup) — it's a host tool, not a Win32 build. Output dir is
 * created if missing.
 *
 * Vertex count overflow: 16-bit indices cap at 65535 vertices. Every
 * shipping .x in the corpus emits fewer than that after our expanded
 * triangulation (shop_1st.x: 5967), so we bail with an error if a
 * mesh exceeds the cap rather than silently truncating.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mesh.h"
#include "xfile.h"

static int read_file(const char *path, char **out_buf, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return -1; }
    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t put = fwrite(data, 1, len, f);
    int rc = (put == len) ? 0 : -1;
    fclose(f);
    return rc;
}

static int mkdir_p(const char *path)
{
    /* Recursive: split on each '/' and mkdir each prefix. Tolerates
     * EEXIST so re-running the dumper on the same output dir is fine. */
    char buf[1024];
    size_t n = strlen(path);
    if (n >= sizeof buf) { errno = ENAMETOOLONG; return -1; }
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char saved = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
            buf[i] = saved;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.x> <output_dir>\n", argv[0]);
        return 2;
    }
    const char *in_path  = argv[1];
    const char *out_dir  = argv[2];

    char *src = NULL;
    size_t src_len = 0;
    if (read_file(in_path, &src, &src_len) != 0) {
        fprintf(stderr, "error: cannot read %s: %s\n", in_path, strerror(errno));
        return 1;
    }

    xfile_t *xf = xfile_parse(src, src_len, in_path);
    free(src);
    if (!xf) {
        fprintf(stderr, "error: xfile_parse returned NULL\n");
        return 1;
    }
    if (xf->error[0]) {
        fprintf(stderr, "error: xfile parse: %s\n", xf->error);
        xfile_free(xf);
        return 1;
    }

    mesh_t *m = mesh_build_from_xfile(xf);
    xfile_free(xf);
    if (!m) {
        fprintf(stderr, "error: mesh_build_from_xfile returned NULL\n");
        return 1;
    }
    if (m->error[0]) {
        fprintf(stderr, "error: mesh build: %s\n", m->error);
        mesh_free(m);
        return 1;
    }
    if (m->vertex_count > 65535) {
        fprintf(stderr, "error: %d vertices exceeds uint16 index cap "
                "(retail dumps use 16-bit IBs)\n", m->vertex_count);
        mesh_free(m);
        return 1;
    }

    mesh_compute_bounds(m);

    if (mkdir_p(out_dir) != 0) {
        fprintf(stderr, "error: cannot create %s: %s\n", out_dir, strerror(errno));
        mesh_free(m);
        return 1;
    }

    /* vb.bin — our mesh_vertex is 36 bytes laid out as FVF 0x152
     * (x,y,z float×3, nx,ny,nz float×3, diffuse u32, u,v float×2),
     * the exact same shape D3DX produces. Write the array as-is. */
    char path_vb[1024], path_ib[1024], path_info[1024];
    snprintf(path_vb,   sizeof path_vb,   "%s/vb.bin",    out_dir);
    snprintf(path_ib,   sizeof path_ib,   "%s/ib.bin",    out_dir);
    snprintf(path_info, sizeof path_info, "%s/info.json", out_dir);

    size_t vb_bytes = (size_t)m->vertex_count * sizeof(mesh_vertex);
    if (write_file(path_vb, m->vertices, vb_bytes) != 0) {
        fprintf(stderr, "error: write %s: %s\n", path_vb, strerror(errno));
        mesh_free(m);
        return 1;
    }

    /* ib.bin — flatten submesh-local indices into global ones by
     * adding each submesh's vertex_offset. m->indices stores them
     * relative to submesh.vertex_offset; the engine sets
     * BaseVertexIndex = vertex_offset at draw time. For the dump we
     * pre-add so the IB matches retail's globally-addressable form. */
    uint16_t *ib_flat = (uint16_t *)malloc((size_t)m->index_count * sizeof(uint16_t));
    if (!ib_flat) {
        fprintf(stderr, "error: oom flattening IB (%d indices)\n", m->index_count);
        mesh_free(m);
        return 1;
    }
    for (int32_t s = 0; s < m->submesh_count; s++) {
        const mesh_submesh *sm = &m->submeshes[s];
        for (int32_t k = 0; k < sm->index_count; k++) {
            int32_t local = m->indices[sm->index_offset + k];
            int32_t global = local + sm->vertex_offset;
            if (global < 0 || global > 65535) {
                fprintf(stderr, "error: flattened index %d out of uint16 range "
                        "at submesh %d\n", global, s);
                free(ib_flat);
                mesh_free(m);
                return 1;
            }
            ib_flat[sm->index_offset + k] = (uint16_t)global;
        }
    }
    size_t ib_bytes = (size_t)m->index_count * sizeof(uint16_t);
    int wr_ib = write_file(path_ib, ib_flat, ib_bytes);
    free(ib_flat);
    if (wr_ib != 0) {
        fprintf(stderr, "error: write %s: %s\n", path_ib, strerror(errno));
        mesh_free(m);
        return 1;
    }

    /* info.json — match the dump-retail-meshes.py schema exactly so
     * diff-mesh.py reads both interchangeably. */
    int32_t num_faces = m->index_count / 3;
    FILE *fj = fopen(path_info, "w");
    if (!fj) {
        fprintf(stderr, "error: open %s: %s\n", path_info, strerror(errno));
        mesh_free(m);
        return 1;
    }
    fprintf(fj,
            "{\n"
            "  \"path\": \"%s\",\n"
            "  \"num_vertices\": %d,\n"
            "  \"num_faces\": %d,\n"
            "  \"fvf\": %u,\n"
            "  \"options\": 0,\n"
            "  \"vert_size\": %u,\n"
            "  \"index_size\": 2\n"
            "}\n",
            in_path, m->vertex_count, num_faces,
            (unsigned)MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1,
            (unsigned)sizeof(mesh_vertex));
    fclose(fj);

    fprintf(stdout,
            "%s → %s (verts=%d faces=%d submeshes=%d "
            "centroid=(%.3f, %.3f, %.3f) radius=%.3f)\n",
            in_path, out_dir,
            m->vertex_count, num_faces, m->submesh_count,
            m->centroid[0], m->centroid[1], m->centroid[2], m->radius);
    mesh_free(m);
    return 0;
}
