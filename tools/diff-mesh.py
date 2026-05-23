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
# triples — and any position-level divergence surfaces a real parser
# issue (wrong Frame composition, dropped face, wrong vertex
# referenced, etc).
#
# Two phases:
#   1. position-only diff: triangles match modulo winding + welding.
#      If this fails, the parser's emitting different geometry.
#   2. per-attribute drill-in: for triangles whose positions match,
#      compare the corner-level normal / diffuse / uv attributes.
#      Tells us where the render-side bytes diverge even when the
#      geometry is shape-equivalent — e.g. MeshVertexColors not
#      parsed (ours stays 0xffffffff, retail has whatever the .x
#      block specifies), flat-vs-smooth normals, UV layout drift.
#
# Exit code:
#   0 = byte-equivalent across all channels
#   1 = positions match, non-position channel diverges
#   2 = position-level divergence (parser bug)
#
# Usage:
#   tools/diff-mesh.py <ours_dump_dir> <retail_dump_dir>
#   tools/diff-mesh.py runs/ours-meshes/xfile__shop__shop_1st.x \
#                      runs/retail-meshes/xfile__shop__shop_1st.x
#
# Position-only mode (skip the attribute drill-in):
#   tools/diff-mesh.py --no-attr <ours> <retail>
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


def canon_triangles_full(verts, indices, pos_decimals=3,
                         normal_decimals=3, uv_decimals=4):
    """Like canon_triangles but each corner is a full attribute tuple:
        (pos, normal, diffuse, uv)
    so the per-attribute diff can ask "for triangles whose positions
    match, do the other channels match too?".

    Each tri returned has shape ((corner0, corner1, corner2), key3pos)
    where corner is ((px,py,pz), (nx,ny,nz), diffuse, (u,v)) and the
    corners are sorted lexicographically (position first), and key3pos
    is the position-only tuple-of-three (same order as the corners
    after sort) — used for joining attribute-rich tris back to the
    position-only multiset.
    """
    tris = []
    for t in range(len(indices) // 3):
        ia, ib, ic = indices[t*3:t*3+3]
        corners = []
        for idx in (ia, ib, ic):
            x, y, z, nx, ny, nz, diffuse, u, v = verts[idx]
            pos = (round(x, pos_decimals),
                   round(y, pos_decimals),
                   round(z, pos_decimals))
            nrm = (round(nx, normal_decimals),
                   round(ny, normal_decimals),
                   round(nz, normal_decimals))
            uv  = (round(u, uv_decimals),
                   round(v, uv_decimals))
            corners.append((pos, nrm, int(diffuse) & 0xffffffff, uv))
        corners.sort()  # by position first, then attribute lexorder
        key3pos = tuple(c[0] for c in corners)
        tris.append((tuple(corners), key3pos))
    return tris


def diff_attributes(ours_full, retail_full):
    """For each (position-only) triangle present in both ours and
    retail, compare the per-corner non-position attributes (normal,
    diffuse, uv). Reports which channel diverges and where.

    Both inputs are output of canon_triangles_full.
    """
    print(f"\n── per-attribute diff (matched-by-position triangles) ──")

    # Group attribute-rich triangles by position-key. A given position
    # triple may occur multiple times in a mesh (props with identical
    # cloned faces) — we keep them as a list so multiplicity is
    # preserved.
    from collections import defaultdict, Counter
    by_pos_ours   = defaultdict(list)
    by_pos_retail = defaultdict(list)
    for corners, key in ours_full:
        by_pos_ours[key].append(corners)
    for corners, key in retail_full:
        by_pos_retail[key].append(corners)

    shared_keys = set(by_pos_ours) & set(by_pos_retail)
    print(f"  shared position-keys:           {len(shared_keys)}")

    tris_compared       = 0
    tris_attr_identical = 0
    normal_mismatch_tris = 0
    diffuse_mismatch_tris = 0
    uv_mismatch_tris = 0

    # Per-corner channel mismatch counters (corners, not triangles)
    n_corners = 0
    n_normal_diff = 0
    n_diffuse_diff = 0
    n_uv_diff = 0

    # Histogram of (ours_diffuse, retail_diffuse) pairs — surfaces the
    # MeshVertexColors block being unread (ours stays 0xffffffff while
    # retail has whatever the .x specifies).
    diffuse_pairs = Counter()
    # Max angle delta between paired normals (degrees, approx).
    import math
    worst_normal_angle = 0.0
    worst_normal_example = None
    # Max UV delta (max over u,v components).
    worst_uv_delta = 0.0
    worst_uv_example = None

    # Examples for the report.
    examples_normal = []
    examples_diffuse = []
    examples_uv = []

    for key in shared_keys:
        o_list = by_pos_ours[key]
        r_list = by_pos_retail[key]
        # Pair greedily — if there's multiplicity skew at this position
        # we'll still compare min(len(o), len(r)) tris and the surplus
        # surfaces via the position-only diff.
        for o, r in zip(o_list, r_list):
            tris_compared += 1
            # Corners are sorted by position; positions are equal
            # because the key matched. Walk pairs.
            tri_normal_bad = False
            tri_diffuse_bad = False
            tri_uv_bad = False
            for (op, on, od, ou), (rp, rn, rd, ru) in zip(o, r):
                n_corners += 1
                if on != rn:
                    tri_normal_bad = True
                    n_normal_diff += 1
                    # angle between normals
                    dot = on[0]*rn[0] + on[1]*rn[1] + on[2]*rn[2]
                    dot = max(-1.0, min(1.0, dot))
                    ang = math.degrees(math.acos(dot))
                    if ang > worst_normal_angle:
                        worst_normal_angle = ang
                        worst_normal_example = (op, on, rn, ang)
                    if len(examples_normal) < 5:
                        examples_normal.append((op, on, rn, ang))
                if od != rd:
                    tri_diffuse_bad = True
                    n_diffuse_diff += 1
                    diffuse_pairs[(od, rd)] += 1
                    if len(examples_diffuse) < 5:
                        examples_diffuse.append((op, od, rd))
                if ou != ru:
                    tri_uv_bad = True
                    n_uv_diff += 1
                    dmax = max(abs(ou[0]-ru[0]), abs(ou[1]-ru[1]))
                    if dmax > worst_uv_delta:
                        worst_uv_delta = dmax
                        worst_uv_example = (op, ou, ru, dmax)
                    if len(examples_uv) < 5:
                        examples_uv.append((op, ou, ru))
            if tri_normal_bad: normal_mismatch_tris += 1
            if tri_diffuse_bad: diffuse_mismatch_tris += 1
            if tri_uv_bad: uv_mismatch_tris += 1
            if not (tri_normal_bad or tri_diffuse_bad or tri_uv_bad):
                tris_attr_identical += 1

    print(f"  triangle-pairs compared:        {tris_compared}")
    print(f"  attr-identical:                 {tris_attr_identical}")
    print(f"  normal mismatch (triangles):    {normal_mismatch_tris}")
    print(f"  diffuse mismatch (triangles):   {diffuse_mismatch_tris}")
    print(f"  uv mismatch (triangles):        {uv_mismatch_tris}")
    print(f"  corners walked:                 {n_corners}")
    print(f"  normal mismatch (corners):      {n_normal_diff}")
    print(f"  diffuse mismatch (corners):     {n_diffuse_diff}")
    print(f"  uv mismatch (corners):          {n_uv_diff}")

    if n_normal_diff:
        print(f"\n  normals — max angle delta:    {worst_normal_angle:.2f}°")
        if worst_normal_example is not None:
            op, on, rn, ang = worst_normal_example
            print(f"    at {op}: ours={on} retail={rn} ({ang:.1f}°)")
        print(f"  first {len(examples_normal)} normal-mismatch corners:")
        for op, on, rn, ang in examples_normal:
            print(f"    {op}: ours={on} retail={rn} ({ang:.1f}°)")

    if n_diffuse_diff:
        print(f"\n  diffuse — top (ours, retail) pair counts:")
        for (od, rd), n in diffuse_pairs.most_common(8):
            print(f"    ours=0x{od:08x} retail=0x{rd:08x}: "
                  f"{n} corner(s)")
        print(f"  first {len(examples_diffuse)} diffuse-mismatch corners:")
        for op, od, rd in examples_diffuse:
            print(f"    {op}: ours=0x{od:08x} retail=0x{rd:08x}")

    if n_uv_diff:
        print(f"\n  uvs — max component delta:    {worst_uv_delta:.4f}")
        if worst_uv_example is not None:
            op, ou, ru, dmax = worst_uv_example
            print(f"    at {op}: ours={ou} retail={ru} (Δ={dmax:.4f})")
        print(f"  first {len(examples_uv)} uv-mismatch corners:")
        for op, ou, ru in examples_uv:
            print(f"    {op}: ours={ou} retail={ru}")

    return {
        "tris_compared":        tris_compared,
        "tris_attr_identical":  tris_attr_identical,
        "normal_mismatch_tris": normal_mismatch_tris,
        "diffuse_mismatch_tris": diffuse_mismatch_tris,
        "uv_mismatch_tris":     uv_mismatch_tris,
        "normal_mismatch_corners":  n_normal_diff,
        "diffuse_mismatch_corners": n_diffuse_diff,
        "uv_mismatch_corners":      n_uv_diff,
        "worst_normal_angle_deg":   worst_normal_angle,
        "worst_uv_delta":           worst_uv_delta,
        "top_diffuse_pairs":        diffuse_pairs.most_common(8),
    }


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
                    "(triangle-set diff + per-attribute drill-in).")
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
    ap.add_argument("--normal-decimals", type=int, default=3,
                    help="Normal rounding for per-attribute diff "
                         "(default 3).")
    ap.add_argument("--uv-decimals", type=int, default=4,
                    help="UV rounding for per-attribute diff "
                         "(default 4).")
    ap.add_argument("--no-attr", action="store_true",
                    help="Skip per-attribute (normal/diffuse/UV) "
                         "drill-in; do only the position-only diff.")
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

    attr_result = None
    if not args.no_attr:
        # Per-attribute drill-in: which triangles match by position
        # but diverge on normal / diffuse / uv. Use the same position
        # precision as the position-only diff so the position-key
        # matches consistently.
        full_a = canon_triangles_full(verts_a, idx_a,
                                      args.tol_decimals,
                                      args.normal_decimals,
                                      args.uv_decimals)
        full_b = canon_triangles_full(verts_b, idx_b,
                                      args.tol_decimals,
                                      args.normal_decimals,
                                      args.uv_decimals)
        attr_result = diff_attributes(full_a, full_b)

    # Exit code:
    #  0 = full bit-equivalence (positions + normals + diffuse + uvs)
    #  1 = positions match but some non-position channel diverges
    #  2 = position-level divergence (parser geometry bug)
    pos_matched = (result["only_ours"] == 0 and
                   result["only_retail"] == 0 and
                   result["multiplicity_skew"] == 0)
    attr_matched = (attr_result is None or
                    (attr_result["normal_mismatch_corners"] == 0 and
                     attr_result["diffuse_mismatch_corners"] == 0 and
                     attr_result["uv_mismatch_corners"] == 0))
    if pos_matched and attr_matched:
        print(f"\nPASS: meshes byte-equivalent across all attributes")
        sys.exit(0)
    elif pos_matched:
        print(f"\nFAIL: positions match but non-position channels "
              f"diverge (see per-attribute diff above)")
        sys.exit(1)
    else:
        print(f"\nFAIL: position-level divergence (parser geometry bug)")
        sys.exit(2)


if __name__ == "__main__":
    main()
