# DirectX `.x` files (`xfile/` and `xfile2/`)

**Status:** known format (open spec, legacy DirectX SDK), full Python
parser landed 2026-05-23 at `tools/extract/xfile.py`. Used as a golden
oracle for the upcoming C port.

## Header

All Recettear `.x` files start with the 16-byte magic:

```
xof 0303txt 0032
```

- `0303` — format version 3.3
- `txt` — text encoding (not `bin`, `tzip`, `bzip` — none of those ship)
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

## Engine usage

The engine uses **d3dxof.dll** (DirectXFile, dynamic-loaded) for parsing
and **D3DX8** (statically-linked) for `D3DXLoadMeshFromXof` mesh
construction. See `docs/findings/mesh-loader.md` for the full
architecture, dependency map, and the strategy decision (custom C
parser, no D3DX) for the port.

## Corpus stats (2026-05-23, all 242 files)

|                            | `xfile/`       | `xfile2/`     |
|----------------------------|---------------:|--------------:|
| files                      | 223            | 19            |
| total bytes                | 17,490,393     | 39,990,360    |
| meshes                     | 2,347          | 86            |
| vertices                   | 118,897        | 8,747         |
| faces                      | 87,029         | —             |
| unique textures (global)   | 165            |               |
| skinned (`SkinWeights`)    | 0              | 210 instances |
| animations                 | 5 `AnimationSet` only | 485+12 |

`xfile/` is static-only; `xfile2/` adds skinning + animation for
character meshes. The custom templates the engine registers in
`FUN_004c8f74` (XSkinMeshHeader / VertexDuplicationIndices /
FaceAdjacency / SkinWeights / Patch / PatchMesh / FVFData /
PMAttributeRange / PMVSplitRecord / PMInfo) are needed by d3dxof's
typed walker but our text parser hardcodes the layouts it cares about
and skips the rest.

## Format quirks (collected from parsing the full corpus)

The format spec is loose enough that different exporters emit slightly
different separators / terminators. Anything that compiles under
d3dxof is fair game. The four major variations the corpus exercises:

### 1. `MeshVertexColors` separator polymorphism

Each item is `index; ColorRGBA` (5 numbers, 5 terminators). Different
exporters terminate items differently:

- `cave_dun` style: `index; r; g; b; a; ;`  (one extra trailing SEMI,
  making the item end in `;;`) with `,` between items — so the inner
  sequence reads `...; ;,` between two items.
- `boss_omu` / xfile2 style: `index; r; g; b; a; ;,` (SEMI SEMI COMMA
  between items).

**Parser behaviour:** after the 5 SEMIs of `r;g;b;a;`, drain any
trailing SEMIs, then consume one optional COMMA. Works for all
observed shapes.

### 2. `MeshMaterialList` `face_indexes` terminator variance

The `array DWORD face_indexes[nFaceIndexes]` block ends in different
ways:

- `ice01.x` style: `0;;`  (value + double SEMI as final terminator).
- `xfile2/` style: `0,0,...,0;` (comma-separated, single trailing SEMI).

**Parser behaviour:** read N values where each is followed by EITHER
`;` OR `,`. After the last, optionally consume a second `;`.

### 3. Material reference blocks have no interior `;`

Inside `MeshMaterialList`, references to existing Materials are
written as `{MaterialName}` — *no* semicolon between the name and the
closing brace. Some other reference patterns in the spec use
`{Name;}` (with the semi) but our corpus consistently omits it.

### 4. Hyphen-in-identifier stitch

A handful of xfile2 material names contain hyphens (e.g.
`PDX02_-_Default`). The DX `.x` IDENT grammar doesn't allow `-`, so
the tokenizer naturally splits these into two adjacent IDENT tokens
with the `-` dropped between them. Both the declaration site
(`Material PDX02_-_Default { ... }`) and the reference site
(`{PDX02_-_Default}`) split identically, so name-based lookups still
match (`"PDX02__Default" == "PDX02__Default"`). The hyphen is
permanently lost from the parsed name — no consumer needs it.

**Parser implementation:** the instance-name reader (and the
material-ref reader) stitch consecutive IDENT tokens together when
they're followed by `{` / `UUID`.

## Open questions

- The 5 `AnimationSet` instances in `xfile/` — used or dead? Likely
  particle / billboard animation rather than full skinning. Will be
  the first dive into animation if/when it surfaces.
- Top-level `Frame` ordering: the Python oracle's `insert(0, ...)`
  reverses sibling order at top level. The C parser will need to
  match for byte-equivalent JSON OR we standardise on append in both
  (deliberate oracle update).

## References

- "Reserved Templates" section of the legacy DirectX SDK docs
  (archived: https://learn.microsoft.com/en-us/windows/win32/direct3d9/reserved-templates).
- `docs/findings/mesh-loader.md` — engine architecture, port strategy.
- `tools/extract/xfile.py` — Python oracle parser (full schema + CLI).
