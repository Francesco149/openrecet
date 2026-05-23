#!/usr/bin/env python3
# tools/diff-mesh.py — compare a retail mesh dump (from tools/dump-retail-meshes.py)
# against the original .x source as parsed by ../RecettearXTools (third-
# party reference parser). Reports per-mesh stats and the deltas that
# matter for openrecet's pipeline correctness.
#
# Why a triangle-based comparison instead of vertex-by-vertex:
# D3DXLoadMeshFromXof welds shared positions/normals (shop_1st.x: 5967
# expanded vertices → 3005 welded), so retail's VB has fewer rows than
# our mesh_build_from_xfile output. Comparing flat vertex arrays gives
# spurious mismatches even when the rendered geometry is identical.
# Walking the IB to materialise the triangle list normalises both
# representations to the same shape — a list of (pos_a, pos_b, pos_c)
# triples — and any divergence then surfaces a real parser issue.
#
# Usage:
#   tools/diff-mesh.py <retail_dump_dir> [--xfile <path/to/source.x>]
#   tools/diff-mesh.py runs/retail-meshes/xfile__shop__shop_1st.x \
#       --xfile vendor/original/xfile/shop/shop_1st.x

import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT.parent / "RecettearXTools"))


def load_retail_dump(d: Path):
    info = json.loads((d / "info.json").read_text())
    vb = (d / "vb.bin").read_bytes()
    ib = (d / "ib.bin").read_bytes()

    nv = info["num_vertices"]
    nf = info["num_faces"]
    vsize = info["vert_size"]
    isize = info["index_size"]
    if len(vb) != nv * vsize:
        raise ValueError(f"vb.bin size {len(vb)} != expected {nv * vsize}")
    if len(ib) != nf * 3 * isize:
        raise ValueError(f"ib.bin size {len(ib)} != expected {nf * 3 * isize}")

    # FVF 0x152 layout: float[3] pos + float[3] normal + DWORD diffuse + float[2] uv
    fmt = "<3f3fI2f"
    if struct.calcsize(fmt) != vsize:
        raise ValueError(f"FVF 0x{info['fvf']:x} doesn't match expected layout "
                         f"(vsize={vsize}, expected={struct.calcsize(fmt)})")

    verts = []
    for i in range(nv):
        x, y, z, nx, ny, nz, diffuse, u, v = struct.unpack_from(fmt, vb, i * vsize)
        verts.append((x, y, z, nx, ny, nz, diffuse, u, v))

    idx_fmt = "<H" if isize == 2 else "<I"
    indices = [struct.unpack_from(idx_fmt, ib, k * isize)[0]
               for k in range(nf * 3)]
    return info, verts, indices


def triangles_from(verts, indices):
    out = []
    for t in range(len(indices) // 3):
        ia, ib, ic = indices[t*3:t*3+3]
        out.append((verts[ia][:3], verts[ib][:3], verts[ic][:3]))
    return out


def bbox(positions):
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    return ((min(xs), min(ys), min(zs)),
            (max(xs), max(ys), max(zs)))


def summarize(label, verts, indices):
    print(f"\n── {label}")
    print(f"  vertices: {len(verts)}")
    print(f"  faces:    {len(indices)//3}")
    positions = [v[:3] for v in verts]
    (mn, mx) = bbox(positions)
    print(f"  bbox min: ({mn[0]:9.3f}, {mn[1]:9.3f}, {mn[2]:9.3f})")
    print(f"  bbox max: ({mx[0]:9.3f}, {mx[1]:9.3f}, {mx[2]:9.3f})")
    print(f"  extent:   ({mx[0]-mn[0]:9.3f}, {mx[1]-mn[1]:9.3f}, {mx[2]-mn[2]:9.3f})")

    # Unique positions (welded count): vertices with identical (x,y,z)
    # collapse — useful to compare retail (already welded) vs ours
    # (expanded per face corner).
    uniq = set((round(x, 3), round(y, 3), round(z, 3))
               for (x, y, z) in positions)
    print(f"  unique positions (rounded 0.001): {len(uniq)}")


def load_xfile_via_recettear_xtools(path: Path):
    """Parse a .x file using the third-party RecettearXTools parser, then
    flatten every Frame/Mesh into a single (verts, indices) tuple after
    applying the accumulated Frame transforms. Indices are 0..nv per
    mesh-local block — easier to compare to retail's welded indexed
    representation when normalised to per-triangle position triples."""
    from x_file_parser import XFileParser
    p = XFileParser(str(path))
    p.parse()

    # frames[*] has nested structure; flatten via the JSON tree the
    # parser builds. We reach into the parser's frames list and walk.
    def ident():
        return [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]

    def mat_mul(a, b):
        out = [[0.0]*4 for _ in range(4)]
        for i in range(4):
            for j in range(4):
                out[i][j] = sum(a[i][k] * b[k][j] for k in range(4))
        return out

    def parse_xform(m_flat_list):
        # x file stores 16 floats row-major; same layout we use.
        return [m_flat_list[i*4:(i+1)*4] for i in range(4)]

    def transform_point(p3, M):
        x, y, z = p3
        return (
            x*M[0][0] + y*M[1][0] + z*M[2][0] + M[3][0],
            x*M[0][1] + y*M[1][1] + z*M[2][1] + M[3][1],
            x*M[0][2] + y*M[1][2] + z*M[2][2] + M[3][2],
        )

    all_verts = []
    all_indices = []

    def walk(frame, accum_M):
        local_M = ident()
        if frame.get('transform_matrix'):
            local_M = parse_xform(frame['transform_matrix'])
        composed = mat_mul(local_M, accum_M)
        for mesh in frame.get('meshes', []):
            verts = mesh['vertices']  # list of [x, y, z]
            faces = mesh['faces']     # list of [count, idx0, idx1, ...]
            base = len(all_verts)
            for v in verts:
                wp = transform_point((v[0], v[1], v[2]), composed)
                all_verts.append((wp[0], wp[1], wp[2], 0.0, 0.0, 0.0, 0, 0.0, 0.0))
            for face in faces:
                if len(face) < 4:
                    continue
                fc = face[0]
                verts_idx = face[1:1+fc]
                # Triangulate fan: (0, i, i+1) for i = 1..fc-2
                for tri in range(fc - 2):
                    all_indices.extend([
                        base + verts_idx[0],
                        base + verts_idx[tri + 1],
                        base + verts_idx[tri + 2],
                    ])
        for child in frame.get('frames', []):
            walk(child, composed)

    for top in p.frames:
        walk(top, ident())
    return all_verts, all_indices


def main():
    ap = argparse.ArgumentParser(
        description="Compare retail mesh dump against the source .x file.")
    ap.add_argument("retail_dir",
                    help="A dir containing vb.bin/ib.bin/info.json from "
                         "tools/dump-retail-meshes.py.")
    ap.add_argument("--xfile",
                    help="Path to the source .x file. If omitted, only the "
                         "retail-side stats are printed.")
    args = ap.parse_args()

    rd = Path(args.retail_dir)
    info, verts, indices = load_retail_dump(rd)
    print(f"retail dump: {rd}")
    print(f"  source path: {info['path']}")
    print(f"  FVF: 0x{info['fvf']:x}, options: 0x{info['options']:x}, "
          f"vert_size={info['vert_size']}, index_size={info['index_size']}")
    summarize("retail (D3DXLoadMeshFromXof + engine post-process)", verts, indices)

    if args.xfile:
        xp = Path(args.xfile)
        try:
            ov, oi = load_xfile_via_recettear_xtools(xp)
        except Exception as e:
            print(f"\nRecettearXTools parse failed: {e}", file=sys.stderr)
            sys.exit(1)
        summarize("openrecet reference (RecettearXTools parser, frames applied)",
                  ov, oi)

        # Triangle-list comparison (positions only, sorted, rounded).
        def canon_tris(verts, indices):
            tris = []
            for t in range(len(indices) // 3):
                ia, ib, ic = indices[t*3:t*3+3]
                ps = [tuple(round(c, 3) for c in verts[i][:3])
                      for i in (ia, ib, ic)]
                tris.append(tuple(sorted(ps)))
            return sorted(tris)

        retail_tris = canon_tris(verts, indices)
        ours_tris   = canon_tris(ov, oi)
        print(f"\n── triangle-set diff (positions only, rounded 0.001)")
        print(f"  retail tris: {len(retail_tris)}")
        print(f"  ref tris:    {len(ours_tris)}")
        rs = set(retail_tris)
        os_ = set(ours_tris)
        only_retail = rs - os_
        only_ours   = os_ - rs
        common      = rs & os_
        print(f"  common:      {len(common)}")
        print(f"  only retail: {len(only_retail)}")
        print(f"  only ref:    {len(only_ours)}")
        if only_retail:
            print(f"\n  first 5 only-in-retail triangles:")
            for t in list(only_retail)[:5]:
                print(f"    {t}")
        if only_ours:
            print(f"\n  first 5 only-in-ref triangles:")
            for t in list(only_ours)[:5]:
                print(f"    {t}")


if __name__ == "__main__":
    main()
