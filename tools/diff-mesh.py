#!/usr/bin/env python3
# tools/diff-mesh.py — compare two retail-format mesh dumps.
#
# Both inputs are directories containing {vb.bin, ib.bin, info.json}
# in the schema that tools/dump-retail-meshes.py + tools/dump-our-mesh
# both produce:
#
#   vb.bin:    FVF-0x152 vertex stream, 36 bytes per vertex
#   ib.bin:    flat uint16 (or uint32) index stream, faces = len/3
#   info.json: {path, num_vertices, num_faces, fvf, options,
#               vert_size, index_size}
#
# Comparison strategy — triangles, not vertices:
#
# D3DXLoadMeshFromXof welds shared positions/normals (shop_1st.x:
# 5967 expanded triangle corners → 3005 welded verts), but our
# mesh_build_from_xfile emits a fresh vertex per face corner (no
# welding pass). Comparing flat vertex arrays therefore gives
# spurious mismatches even when the rendered geometry is identical.
#
# Walking each IB to materialise the triangle list normalises both
# representations to the same shape — a list of (pos_a, pos_b, pos_c)
# triples, position-only — and any divergence then surfaces a real
# parser issue (wrong Frame composition, dropped face, wrong vertex
# referenced, etc).
#
# Usage:
#   tools/diff-mesh.py <ours_dump_dir> <retail_dump_dir>
#   tools/diff-mesh.py runs/ours-meshes/xfile__shop__shop_1st.x \
#                      runs/retail-meshes/xfile__shop__shop_1st.x
#
# Single-arg mode (just stats for one dump):
#   tools/diff-mesh.py <dump_dir>

import argparse
import json
import struct
import sys
from pathlib import Path


def load_dump(d: Path):
    """Load a retail-format dump (vb.bin + ib.bin + info.json)."""
    info = json.loads((d / "info.json").read_text())
    vb = (d / "vb.bin").read_bytes()
    ib = (d / "ib.bin").read_bytes()

    nv = info["num_vertices"]
    nf = info["num_faces"]
    vsize = info["vert_size"]
    isize = info["index_size"]
    if len(vb) != nv * vsize:
        raise ValueError(f"{d}/vb.bin size {len(vb)} != expected {nv * vsize}")
    if len(ib) != nf * 3 * isize:
        raise ValueError(f"{d}/ib.bin size {len(ib)} != expected {nf * 3 * isize}")

    # FVF 0x152 = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1
    #          → float3 pos + float3 normal + DWORD diffuse + float2 uv
    fmt = "<3f3fI2f"
    if struct.calcsize(fmt) != vsize:
        raise ValueError(f"{d}: FVF 0x{info['fvf']:x} doesn't match expected "
                         f"36-byte layout (vsize={vsize})")

    verts = []
    for i in range(nv):
        x, y, z, nx, ny, nz, diffuse, u, v = struct.unpack_from(fmt, vb, i * vsize)
        verts.append((x, y, z, nx, ny, nz, diffuse, u, v))

    idx_fmt = "<H" if isize == 2 else "<I"
    indices = [struct.unpack_from(idx_fmt, ib, k * isize)[0]
               for k in range(nf * 3)]
    return info, verts, indices


def bbox(positions):
    if not positions:
        return ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    return ((min(xs), min(ys), min(zs)),
            (max(xs), max(ys), max(zs)))


def summarize(label, info, verts, indices):
    print(f"\n── {label} ─ {info['path']}")
    print(f"  FVF: 0x{info['fvf']:x}, options: 0x{info['options']:x}, "
          f"vert_size={info['vert_size']}, index_size={info['index_size']}")
    print(f"  vertices: {len(verts)}")
    print(f"  faces:    {len(indices)//3}")
    positions = [v[:3] for v in verts]
    (mn, mx) = bbox(positions)
    print(f"  bbox min: ({mn[0]:9.3f}, {mn[1]:9.3f}, {mn[2]:9.3f})")
    print(f"  bbox max: ({mx[0]:9.3f}, {mx[1]:9.3f}, {mx[2]:9.3f})")
    print(f"  extent:   ({mx[0]-mn[0]:9.3f}, {mx[1]-mn[1]:9.3f}, {mx[2]-mn[2]:9.3f})")
    uniq = set((round(x, 3), round(y, 3), round(z, 3))
               for (x, y, z) in positions)
    print(f"  unique positions (rounded 0.001): {len(uniq)}")


def canon_triangles(verts, indices, tol_decimals=3):
    """Materialise indexed triangles into a sortable canonical form:
    each tri → tuple of 3 (x,y,z) tuples, sorted internally so a
    different winding order doesn't surface as a mismatch."""
    tris = []
    for t in range(len(indices) // 3):
        ia, ib, ic = indices[t*3:t*3+3]
        ps = []
        for idx in (ia, ib, ic):
            x, y, z = verts[idx][:3]
            ps.append((round(x, tol_decimals),
                       round(y, tol_decimals),
                       round(z, tol_decimals)))
        # Sort vertices within each triangle so (A,B,C) and (B,C,A)
        # compare equal — we want the same geometry regardless of
        # which corner started the winding. We don't care about
        # winding direction for the parser-correctness check; render-
        # state CULLMODE catches winding inversions separately.
        tris.append(tuple(sorted(ps)))
    return tris


def diff_triangle_sets(ours, retail):
    print(f"\n── triangle-set diff (positions only, rounded 0.001) ──")
    rs = set(retail)
    os_ = set(ours)
    common      = rs & os_
    only_retail = rs - os_
    only_ours   = os_ - rs
    print(f"  ours tris (unique):   {len(os_)}")
    print(f"  retail tris (unique): {len(rs)}")
    print(f"  common:               {len(common)}")
    print(f"  only ours:            {len(only_ours)}")
    print(f"  only retail:          {len(only_retail)}")

    # Multiset comparison: how often each triangle appears.
    from collections import Counter
    cnt_ours = Counter(ours)
    cnt_retail = Counter(retail)
    all_keys = set(cnt_ours) | set(cnt_retail)
    skewed = [(k, cnt_ours[k], cnt_retail[k])
              for k in all_keys
              if cnt_ours[k] != cnt_retail[k]]
    print(f"  multiplicity skew:    {len(skewed)} triangles "
          f"differ in occurrence count")

    if only_ours:
        print(f"\n  first 5 only-in-ours triangles:")
        for t in list(only_ours)[:5]:
            print(f"    {t}")
    if only_retail:
        print(f"\n  first 5 only-in-retail triangles:")
        for t in list(only_retail)[:5]:
            print(f"    {t}")
    if skewed and not (only_ours or only_retail):
        print(f"\n  first 5 multiplicity skews (tri, ours_n, retail_n):")
        for k, no, nr in skewed[:5]:
            print(f"    {k}: ours={no} retail={nr}")

    return {
        "ours_unique_tris":   len(os_),
        "retail_unique_tris": len(rs),
        "common":             len(common),
        "only_ours":          len(only_ours),
        "only_retail":        len(only_retail),
        "multiplicity_skew":  len(skewed),
    }


def main():
    ap = argparse.ArgumentParser(
        description="Compare two retail-format mesh dumps "
                    "(triangle-set diff, position-only).")
    ap.add_argument("ours_dir",
                    help="Dump dir for our parser's output (see "
                         "tools/dump-our-mesh/).")
    ap.add_argument("retail_dir", nargs="?",
                    help="Dump dir for retail's parser output (see "
                         "tools/dump-retail-meshes.py). If omitted, "
                         "just reports stats for ours_dir.")
    ap.add_argument("--tol-decimals", type=int, default=3,
                    help="Position rounding precision for canonical "
                         "comparison (default 3 = 0.001 unit).")
    args = ap.parse_args()

    ours_dir = Path(args.ours_dir)
    info_a, verts_a, idx_a = load_dump(ours_dir)
    summarize("ours", info_a, verts_a, idx_a)

    if args.retail_dir is None:
        return

    retail_dir = Path(args.retail_dir)
    info_b, verts_b, idx_b = load_dump(retail_dir)
    summarize("retail", info_b, verts_b, idx_b)

    tris_a = canon_triangles(verts_a, idx_a, args.tol_decimals)
    tris_b = canon_triangles(verts_b, idx_b, args.tol_decimals)
    result = diff_triangle_sets(tris_a, tris_b)

    # Exit code: 0 if triangle sets match (parser is byte-equivalent
    # at the geometry level), 1 otherwise.
    matched = (result["only_ours"] == 0 and
               result["only_retail"] == 0 and
               result["multiplicity_skew"] == 0)
    print(f"\n{'PASS' if matched else 'FAIL'}: "
          f"{'triangle sets identical' if matched else 'parser divergence detected'}")
    sys.exit(0 if matched else 1)


if __name__ == "__main__":
    main()
