# Mesh loader — `xfile/*.x` + `xfile2/*.x`

**Status (2026-05-23):** **5 of 7 chips landed** — C1 survey, C2 Python
oracle, C3 C parser (`src/xfile.{c,h}`), C4 mesh build + bounds + D3D8
upload (`src/mesh.{c,h}`), **C5 orchestrator** (`src/mesh_load.{c,h}`).
Remaining: C6 wire AAB + C0A worker bodies, C7+ scene-1 render. 821
unit tests pass under ASan + UBSan (was 807; +14 from C5).

Triggered the port because **AAB** (FUN_0046bf38, scene_walls inner body)
and **C0A** (FUN_004748f8, scene_floor jutan-table sibling) — two of the
remaining 3 NULL secondary worker-thread bodies — both call into the
mesh loader at FUN_00472836, and the scene-1 INGAME path needs them.
See `src/worker_load.h` and the session starter for the deferred list.

## Engine architecture

The orchestrator is **`FUN_00472836`** (1609 bytes) — entered with
`(dest_struct, filename, idx_or_minus1)`. It:

1. Builds the on-disk path (`xfile/%s`, with `_s.x` variant in "quality
   1" mode at `DAT_0438b19c != 0`).
2. Calls **`FUN_004c8f74`** (704 bytes) — the DirectXFile setup +
   walk:
   - `LoadLibraryA("d3dxof.dll")` + `GetProcAddress("DirectXFileCreate")`.
   - Registers the two large custom-template blocks (XSkinMeshHeader,
     VertexDuplicationIndices, FaceAdjacency, SkinWeights, Patch,
     PatchMesh, FVFData, PMAttributeRange, PMVSplitRecord, PMInfo) —
     UUIDs match Microsoft's "Reserved Templates" + the optimized-mesh
     PM extensions.
   - `CreateEnumObject(filename, 0, &enum)` then walks
     `GetNextDataObject` calling **`FUN_004c8baa`** (970 bytes) per
     top-level template.
3. `FUN_004c8baa` is the recursive template dispatcher:
   - Compares the template UUID against three sentinels (matrix /
     mesh / frame at `&DAT_0051cc38`/`...cc58`/`...cc68`).
   - **Mesh** → calls **`FUN_004c75e3`** (4634 bytes — this is a hand
     RE'd `D3DXLoadMeshFromXof` clone, the heavy lifter that builds
     ID3DXMesh-shaped vertex/index buffers, runs material extraction,
     and returns the buffers + count via out-params).
   - **Frame** → recurses on children, accumulating the
     `FrameTransformMatrix` into an inherited transform.
   - **Matrix4x4** → multiplies into the running transform.
4. Back in FUN_00472836, **per material**:
   - Copies the 17-dword `D3DMATERIAL8` (68 B) into `param_1[2][i]`.
   - Duplicates Diffuse RGBA (offsets 0..3) into Ambient (offsets
     4..7) — "ambient = diffuse" trick.
   - If `param_3 >= 0`, zeroes a 12-byte slot at
     `&DAT_073cc950 + (param_3*200 + i)*12` (per-model dynamic bone /
     pivot scratch).
   - Recognizes special texture-name tags via prefix scans:
     - `water` → flag (additive blend? animated UV?)
     - `shop_jutan` 10-char prefix → flag
     - 2-char tokens at any position: `_a` / `_s` / etc. (~10 such)
       toggling six per-texture mode flags.
   - **Dedupes the texture name** against a global cache at
     `&DAT_073be908` (256-char strides, `DAT_073cb108` count). New
     entry → `FUN_00471b24` (467 B; sprite_load wrapper) loads it via
     the bmp/tga pipeline.
   - Writes the resolved per-mesh texture index back into
     `param_1[1][i]`. Side-tables at `&DAT_073cb10c..7cb814` (10
     parallel byte arrays keyed by `DAT_073cb108`) get the
     per-texture mode bytes.
5. Computes mesh bounds via **`FUN_004aaad7`** (278 B) — average +
   max-radius pass over the locked vertex buffer.
6. If the mesh FVF isn't `0x152` (XYZ + NORMAL + DIFFUSE + TEX1), it
   clones into that FVF and replaces the original.

### Dest struct (`param_1`)

10 dwords (~40 B):

| off | engine field            | type/use                         |
|----:|-------------------------|----------------------------------|
|   0 | `mesh`                  | `ID3DXMesh*` (cloned to FVF 0x152) |
|   1 | `texture_indices`       | `int32_t[material_count]` (-1 sentinel) |
|   2 | `materials`             | `D3DMATERIAL8[material_count]` (68 B each, ambient=diffuse) |
|   3 | `adjacency`             | `ID3DXBuffer*` adjacency           |
|   4 | `material_count`        | DWORD                              |
|   5 | `vb_min` / FVF probe    | filled by `GetFVF`-like vtable[6]  |
|   6 |   ...                   |                                    |
|   7 |   ...                   |                                    |
|   8 | `vb_max` / clone-target | filled by vtable[5] return         |
|   9 | `loaded`                | `1` flag                           |

## What's available in the toolchain (mingw-w64-i686 13.0.0)

| component               | header             | lib            | usable?     |
|-------------------------|--------------------|----------------|-------------|
| d3dxof (DirectXFile)    | `<dxfile.h>`       | `libd3dxof.a`  | **yes**     |
| D3DX **9**              | `<d3dx9*.h>`       | `libd3dx9*.a`  | yes — but wrong era |
| D3DX **8**              | **none**           | `libd3dx8d.a`  | no headers → unusable for `D3DXLoadMeshFromXof` |

So we get d3dxof for free, but **D3DX8 is effectively unavailable** to
us. The engine's binary statically links D3DX8 — that's what
FUN_004c75e3 is. We won't reach for FUN_004c75e3 byte-for-byte (4.6 KB
of D3DX8 internals); we'll write our own.

## Strategy: **custom text parser, no D3DX**

- Parse `.x` text format directly into pure C structs (vertices,
  indices, materials, texture names, frame hierarchy with transforms).
- Upload to raw D3D8 `IDirect3DVertexBuffer8` / `IDirect3DIndexBuffer8`
  (no `ID3DXMesh` wrapper).
- The engine's `param_1[0]` "mesh" pointer becomes our own
  `mesh_t*` — consumers in scene_walls/scene_floor/etc. read through
  it, not D3DX, so the ABI stays internal.
- Skinning + animation deferred: only **`xfile/`** (223 files, static
  meshes) is needed for AAB/C0A and most scene-1 geometry. The 19
  skinned character meshes in **`xfile2/`** can wait until character
  rendering ports.

### Why not D3DX9 as a shim?

We have d3dx9 headers + lib in mingw. But:
- D3DX9 mesh APIs target IDirect3DDevice9 — adapting them to D3D8
  resources is a fight that costs more than parsing 50 lines of text
  ourselves.
- D3DX9 `D3DXLoadMeshFromX` is a black box; we lose the per-template
  control that lets us match the engine's "ambient = diffuse",
  texture-name dedupe, and mode-flag side-tables.
- The custom parser is ~300 lines of C and fully tested against the
  vendor corpus.

### Why not reimplement D3DX8's `LoadMeshFromXof` directly?

That's FUN_004c75e3 — 4634 bytes of decompiled material/normal/UV
weaving + ID3DXMesh CreateMesh + Lock + fill + clone. Reproducing
behaviour requires reproducing the d3dxof iteration API consumer
side as well, plus the ID3DXMesh interface. Doing this is fine but
we'd be building a D3DX8 reimplementation along the way — a much
bigger detour than a direct parser.

## .x text format (what the corpus actually contains)

Header magic across 100% of the corpus: `xof 0303txt 0032`. No bin /
tzip / bzip variants ship.

### `xfile/` — 223 files, 17 MB

```
Material           5780      MeshVertexColors    1860
TextureFilename    2734      Frame               2578
FrameTransformMatrix 2610    Mesh                2323
MeshMaterialList   2347      MeshNormals         2347
MeshTextureCoords  2071      AnimationKey          69
```

Just the standard reserved templates. No skinning. AnimationKey
appears in a few static models (probably for animated billboards /
particle bones — defer).

### `xfile2/` — 19 files, 40 MB (character meshes)

```
AnimationKey       1455      Frame               1055
FrameTransformMatrix 1055    Animation            485
SkinWeights         210      Mesh                  86
MeshNormals          86      MeshMaterialList      86
MeshTextureCoords    86      XSkinMeshHeader       35
MaterialList…       …       AnimationSet          12
```

Adds skinning (`SkinWeights`, `XSkinMeshHeader`) + animation
(`Animation`, `AnimationKey`, `AnimationSet`). Custom template
**XSkinMeshHeader** is one of the ones FUN_004c8f74 registers — its
UUID (`3CF169CE-FF7C-44ab-93C0-F78F62D172E2`) is in the registered
block, which means d3dxof needs the template-decl block for these to
parse. Our parser can hard-code the layouts and skip the
template-registration dance.

### Token grammar (informal)

- `//` comment to EOL.
- Whitespace is liberal between any tokens (including inside arrays).
- `template Name { <UUID> ...declarations... }` — declarations describe
  field types. We ignore declared templates (they only matter to
  d3dxof's typed walker).
- `Name <optional-instance-name> { <optional-UUID> ...body... }` —
  instance.
- Bodies hold:
  - **scalars** ending in `;`
  - **strings** `"..."` ending in `;`
  - **arrays** of any element type, ending in `;;` (each item
    terminated by `;,` except the last which is `;;` — and matrix-form
    arrays use commas between row items, semicolons at end).
  - **nested template instances**
  - **reference lists** `{ name1; name2; ... }` (used inside
    `MeshMaterialList`).

## Chip plan

Smallest-first, each chip self-contained and commit-worthy:

1. **C1 — this doc.** Establishes architecture + strategy + plan.
2. **C2 — Python parser oracle.** Flesh out `tools/extract/xfile.py`
   to real-parse the corpus and emit per-file JSON (vertex count,
   face count, material count, texture filenames, transform tree).
   Run across all 242 files; pin format quirks early. Becomes our
   future C parser's golden oracle (same shape as `bmp_unpack.py`
   was for bmp_lzw).
3. **C3 — C parser skeleton `src/xfile.{c,h}`.** Tokenizer +
   template-instance walker. Pure pull API:
   - `xfile_open(buf, len) → xfile_t*`
   - `xfile_meshes(x) → mesh_data[]`
   - `xfile_textures(x) → const char *[]`
   - `xfile_close(x)`. Static meshes only (no skin/anim). Tested
   against C2's JSON oracle for all 223 `xfile/` files.
4. **C4 — `src/mesh.{c,h}` D3D8 upload.** Take `mesh_data` →
   `IDirect3DVertexBuffer8`/`IndexBuffer8` + FVF 0x152 conversion.
   Texture-loader integration via existing `sprite_load`. Bounds
   computation (`FUN_004aaad7` equivalent — small).
5. **C5 — `mesh_load` orchestrator ✓ (2026-05-23, `src/mesh_load.{c,h}`).**
   Ports FUN_00472836 (1609 B) end-to-end as `mesh_load(path, param_3)`
   + a buffer-input variant `mesh_load_from_buf` used by host tests.
   The orchestrator:
   - Resolves the on-disk path (with the easydisp `_s.x` variant when
     `g_ini.easydisp != 0` — main.c hands the value via
     `mesh_load_set_easydisp` after recet_ini_load).
   - Reads via `storage_read` with a disk-fopen fallback (Win32 only —
     host tests use `mesh_load_from_buf`).
   - Drives `xfile_parse` → `mesh_build_from_xfile` →
     `mesh_compute_bounds`.
   - Per material: runs `mesh_classify_texture_name` (10 mode flags
     mirroring lines 138..273 of the engine — water/hikari/kabe_/yuka_/
     shop_jutan + n_/w_ + u-index/v-index + ext_tga), then does a global
     `mesh_tex_cache_lookup_or_reserve`, writing the slot into
     `m->texture_slots[i]` (new field parallel to `m->materials[]`).

   Global state (engine: `&DAT_073be908` + count at `DAT_073cb108` +
   10 side-tables at `&DAT_073cb10c..&DAT_073cb814`) lives in
   `g_mesh_tex_cache` (200 entries × `mesh_tex_entry { name, flags,
   sprite }`). Flags freeze on first insert — repeat lookups with
   different flags do NOT overwrite, matching the engine semantics.
   Reset via `mesh_tex_cache_reset` on device-reload or between tests.

   Deferred:
   - 12-byte dynamic-bone scratch at `&DAT_073cc950` (param_3 >= 0
     path). All static-mesh callers pass -1; dormant.
   - `.tga → .bmp` on-disk override (FUN_00471b24's internal rewrite).
     Our `sprite_load` doesn't do this rewrite; the `ext_tga` flag is
     therefore set strictly from the .x's `TextureFilename` string.
     Re-port if/when a corpus test catches a miss.
   - Win32 finalize (`mesh_load_finalize_win32`) creates `sprite_t`s
     for any cache entry whose `sprite` is still NULL; mesh VB/IB
     upload via the existing `mesh_upload_d3d8`. Not hooked into the
     boot path yet — AAB/C0A wiring (C6) does that.
6. **C6 — wire AAB + C0A worker bodies.** Now-unblocked scene_walls
   inner-body (FUN_0046bf38) + scene_floor/jutan C0A
   (FUN_004748f8) using `mesh_load`. State arrays for the loaded
   meshes per stage selector.
7. **C7+ — actual rendering.** The mesh data exists in GPU memory
   but nothing draws it yet. Wires into the scene-1 render path
   (FUN_004547ab state==1 branch — Mt. Everest in its own right;
   ports as separate roadmap items).

Skinning + animation (xfile2/) land much later when character
rendering ports — likely months out.

## Notes / hazards

- **Texture-name dedupe is global.** `&DAT_073be908` + `DAT_073cb108`
  is a process-wide registry. Loading the same texture from multiple
  meshes returns the same engine-side handle. The side-tables (10
  byte arrays at `&DAT_073cb10c`..`&DAT_073cb814`) are keyed by this
  global index, not per-mesh. Our `mesh_load` must share state across
  all calls.
- **`param_3 >= 0` path** zeroes a 12-byte slot at `&DAT_073cc950 +
  (param_3*200 + i)*12`. That's 12 bytes per (mesh-instance, material)
  in a 200-stride table. Looks like per-instance dynamic scratch
  (animation pivots? blend weights?). Dormant for static meshes —
  scene_walls/floor/jutan all pass `0xffffffff`.
- **Filename suffix variants.** "Quality 1" (`DAT_0438b19c != 0`)
  inserts `_s` before `.x` — so `Box01.x` → `Box01_s.x`, falling back
  to the engine's `DAT_005c8400` format (likely a sibling sub-dir).
  Recettear sets this via recet.ini `screen` keys at high quality.
  Worth checking what `recet.ini`'s shipped value is — if it's never
  1 in practice we can ignore the `_s` variant.
- **Float precision.** All files are `0032` (32-bit). No
  format-conversion needed.
- **The `Original` / `Param1` / `Top` / `End` template names** that
  showed up in the Python scan are NOT custom templates — they're
  instance names that happen to be capitalized identifiers before
  `{`. Our parser distinguishes via the `template` keyword.
