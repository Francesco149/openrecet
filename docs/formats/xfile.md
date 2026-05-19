# DirectX `.x` files (`xfile/` and `xfile2/`)

**Status:** known format (open spec, legacy DirectX SDK). Extractor stub:
`tools/extract/xfile.py`.

## Header

All Recettear `.x` files start with the 16-byte magic:

```
xof 0303txt 0032
```

- `0303` — format version 3.3
- `txt` — text encoding (not `bin`, `tzip`, `bzip`)
- `0032` — 32-bit float precision

## Structure

A `.x` file is a sequence of **templates** with brace-nested bodies. Examples:

```
Mesh <uuid> {
    nVertices;
    Vertex { x; y; z; },
    ...
    MeshNormals { ... }
    MeshTextureCoords { ... }
    MeshMaterialList { ... }
}
Frame name {
    FrameTransformMatrix { ... }
    Mesh { ... }
}
```

## What the engine probably does with them

The engine likely either uses `D3DXLoadMeshFromX*` (DirectX 9 utility) or a
hand-rolled parser. We'll know which once Ghidra analysis exposes the import
table — `D3DX9_*.dll` imports are a giveaway.

## Open questions

- Any custom templates? (Engine-specific UUIDs would tell us.)
- Animation data (`AnimationKey`, `AnimationSet`) — used?
- Skinning (`SkinWeights`) — used?

`tools/extract/xfile.py --scan vendor/original/xfile/ --aggregate` will
answer these once the user has run `setup.sh`.

## References

- "Reserved Templates" section of the legacy DirectX SDK docs
  (archived: https://learn.microsoft.com/en-us/windows/win32/direct3d9/reserved-templates).
