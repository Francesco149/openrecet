# Scene-1 mesh emit leaf chain — survey

**Status (2026-05-23):** Survey + a small adapter. The five engine
functions in this chain don't translate 1:1 to our flat `mesh_t`
data model; the survey calls out what does and doesn't carry over,
and the adapter (`scene1_render_emit_frame`) lands the part that
does.

## The chain

In FUN_00459dfd (C8a), each of the four mesh walkers
(FUN_00459847 / FUN_004552d0 / FUN_00458bdf / FUN_00456f56)
ultimately reaches one of these per-mesh emit helpers via a fixed
chain:

```
FUN_00404a20    (  45 B)  — "draw the scene tree (top-level entry)"
   └─ FUN_004047df  ( 135 B)  — recursive frame walker
        └─ FUN_00404757  ( 117 B)  — per-mesh dispatcher
             ├─ FUN_00403eb7  ( 108 B)  ← FFP path (static meshes)
             ├─ FUN_00403f23  ( 742 B)  ← shader pass mode 0
             ├─ FUN_00404500  ( 360 B)  ← shader pass mode 1
             ├─ FUN_00404668  ( 239 B)  ← shader pass mode 2
             └─ FUN_00404209  ( 759 B)  ← shader pass mode 3
```

Also adjacent:
- `FUN_00403832` (178 B) — shader rebind on mesh-shader-id change.
- `FUN_004a662f` (168 B) — FVF → vertex stride.

Total: ~2.5 KB of decompiled C for the full chain.

## The engine's data model

The engine assumes Direct3D's `ID3DXMesh` + `D3DXFRAME` shape — the
hand-rolled `D3DXLoadMeshFromXof` clone at `FUN_004c75e3` (4634 B,
documented in `docs/findings/mesh-loader.md`) builds them when
loading each `.x` file. Field offsets surface in the chain:

**Frame (scene-graph node, passed to `FUN_004047df`):**

| offset | type    | role                                  |
|--------|---------|---------------------------------------|
| +0x0   | mesh*   | head of mesh linked-list at this frame|
| +0xc4  | float[16] | world matrix for this frame (64 B)  |
| +0x130 | frame*  | next sibling frame (linked list)      |
| +0x134 | frame*  | first child frame                     |

**Mesh (engine wrapper around `ID3DXMesh`):**

| offset | type            | role                              |
|--------|-----------------|-----------------------------------|
| +0x0   | `ID3DXMesh*`    | the D3DX mesh interface           |
| +0x4   | material*       | per-subset material array (0x44 B/entry) |
| +0x8   | texture**       | per-subset `IDirect3DBaseTexture8*` array |
| +0xc   | int             | subset count                      |
| +0x18  | mesh*           | next mesh in this frame's list    |
| +0x20  | int             | **has_skinning** (0=FFP, !=0=shader) |
| +0x40  | int             | shader index (selects from scene+0x210 table) |
| +0x48  | int             | mesh's expected shader id (vs scene's current) |

**Scene (`this`-pointer, in ECX):**

| offset | type            | role                              |
|--------|-----------------|-----------------------------------|
| +0x108 | `IDirect3DDevice8*` | the D3D8 device                |
| +0x204 | frame*          | scene-graph root                  |
| +0x210 | int / shader_handle table | **pass mode** (FUN_00404757 dispatch key) AND base of 4-entry shader handle table |
| +0x2e8 | int             | "no override" flag (alpha-pass)   |

The pass-mode/shader-handle-table aliasing at +0x210 is the engine's
quirk: the first 4 bytes are read as an int by `FUN_00404757`'s
switch, and the same address indexes into a contiguous table of
4-byte handles read by `FUN_00404209`. Today this surfaces only in
the chr walker — the FFP path doesn't touch it.

## The FFP path — what we need for HOUSE

`mesh+0x20 == 0` for all static meshes (walls, floor, jutan, table,
sc1, items, props). `FUN_00404757` routes them to `FUN_00403eb7`:

```c
/* FUN_00403eb7 — per-mesh FFP emit. */
for (int subset = 0; subset < mesh->subset_count; subset++) {
    device->SetMaterial(&mesh->materials[subset]);      // vtable +0xa8
    device->SetTexture(0, mesh->textures[subset]);      // vtable +0xf4
    mesh->mesh->DrawSubset(subset);                     // ID3DXMesh +0xc
}
```

Three D3D calls per subset: `SetMaterial`, `SetTexture`, `DrawSubset`.
`DrawSubset` is `ID3DXMesh`'s built-in convenience method that
internally does `SetStreamSource(0, vb, fvf_stride)` +
`SetIndices(ib, 0)` + `DrawIndexedPrimitive(triangle_list, …)` using
the mesh's stored vertex buffer + index buffer + attribute table.

Our existing `mesh_draw_d3d8` (in `src/mesh_draw.c`) produces an
equivalent D3D call sequence per submesh, just inlined instead of
delegated to D3DX:

```c
/* mesh_draw_d3d8 — per-submesh FFP emit (our equivalent). */
SetStreamSource(0, m->vb, sizeof(mesh_vertex));
for each submesh sm:
    SetIndices(m->ib, sm->vertex_offset);
    SetTexture(0, resolved_sprite_for_material(sm));    // via cache
    SetMaterial({diffuse, ambient=diffuse, specular, emissive, power});
    DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0,
                         sm->vertex_count, sm->index_offset,
                         sm->index_count / 3);
```

The differences are essentially binding strategy + data layout —
both produce a valid D3D8 frame. Key alignments:

- Engine's per-subset SetMaterial = our per-submesh SetMaterial.
- Engine's per-subset SetTexture (from a per-mesh `IBaseTexture**`
  array) = our per-submesh SetTexture (resolved via material index
  → `g_mesh_tex_cache` slot → sprite).
- Engine's `DrawSubset` = our SetIndices + DrawIndexedPrimitive (the
  per-submesh `vertex_offset` we feed to `BaseVertexIndex` matches
  D3DX's attribute-table-driven offset).

We **do not** need to mirror the per-subset/per-submesh split byte-
for-byte. Our flat `mesh_t` already collapses the engine's
ID3DXMesh + AttributeTable into one struct.

## The scene-tree walker — what's deferred

`FUN_004047df` recurses the D3DXFRAME tree:

```c
/* FUN_004047df — frame walker. */
int FUN_004047df(frame *f, int *face_count_out) {
    g_call_depth++;
    if (f->first_mesh == NULL
        || device->SetTransform(D3DTS_WORLDMATRIX(0), &f->world_matrix) >= 0) {
        for (mesh *m = f->first_mesh; m; m = m->next) {
            FUN_00404757(m);
            *face_count_out += m->mesh->GetNumFaces();
        }
        for (frame *c = f->first_child; c; c = c->next_sibling) {
            FUN_004047df(c, face_count_out);
        }
    }
}
```

It needs:
- A scene tree (root + children + siblings + per-frame matrices).
- A per-frame mesh linked list.
- One `IDirect3DDevice8::SetTransform(D3DTS_WORLDMATRIX(0))` per
  frame.

We currently have **neither** the scene tree nor the per-frame
linked list. Our `mesh_t` is one flat self-contained mesh. The
shop walker (FUN_004552d0, 5210 B, next chip) is what populates
the tree from the engine's asset state — its 5 KB is largely about
building the (frame, mesh) graph from the wall/floor/jutan/table
selectors + their per-instance transforms.

The right move is **don't reimplement the tree** in this chip. The
shop walker chip decides what intermediate data model to use. This
chip just lands the per-frame *emit primitive* the walker will
call, and an adapter that maps "engine frame draw" onto our flat
mesh_t.

## The programmable-shader path — deferred to chr walker

`mesh+0x20 != 0` (skinned meshes — chr / animated NPCs) takes one of
four shader passes. `FUN_00404209` (the pass-mode-3 variant)
illustrates the shape:

1. SetVertexShaderConstant(reg=0, scale_vector, 1) — per-mesh scale
2. SetVertexShaderConstant(reg=7, lighting_vector, 1) — per-stage light
3. SetVertexShaderConstant(reg=8, material_color, 1) — per-subset
4. SetVertexShaderConstant(reg=9 + i*3, bone_matrix[i], 3) — bone palette
   (one matrix per skinning bone, three vec4 registers per matrix)
5. SetVertexShader(scene.shader_handles[mesh.shader_idx])
6. SetStreamSource(0, vb, fvf_stride)
7. SetIndices(ib, 0)
8. SetTexture(0, mesh.textures[subset])
9. DrawIndexedPrimitive(D3DPT_TRIANGLELIST, MinIndex, NumVertices,
                        StartIndex, PrimitiveCount)

The shader handles themselves are created at engine init by
`CreateVertexShader(declaration, function, &handle, 0)` — the
bytecode lives in the .exe data section. Porting this path means:

- Extract the vertex shader bytecode + declaration from the original
  .exe data section.
- Either replay them through `CreateVertexShader` at our init, OR
  reimplement equivalent shaders in DX8 shader assembly.
- Port the constant-upload + skinning-matrix-palette math.

That's its own multi-chip climb. **Not in scope until the chr walker
(FUN_004176ff, 30395 B) is on the table.**

For the HOUSE shop interior — which has zero skinned meshes — the
FFP path is sufficient. Recette + Tear + visiting NPCs only animate
when the chr walker ports.

## What this chip lands

`scene1_render.{c,h}` gains one adapter helper:

```c
void scene1_render_emit_frame(IDirect3DDevice8 *dev,
                              const float world_matrix[16],
                              const mesh_t *m);
```

It:
1. `SetTransform(D3DTS_WORLDMATRIX(0), world_matrix)` — one engine
   frame's world transform.
2. Calls our existing `mesh_draw_d3d8(dev, m)` — equivalent to
   `FUN_00403eb7`'s per-subset SetMaterial + SetTexture +
   `DrawSubset` loop.

This is the per-frame primitive the shop walker (next chip) will
call. It does not own scene-tree state — just the transform + draw.

## Where each engine function lands (or doesn't)

| Engine fn      | Bytes | Status in this chip       |
|----------------|------:|---------------------------|
| FUN_00404a20   |    45 | **deferred** — top-level entry needs scene tree |
| FUN_004047df   |   135 | **deferred** — needs scene tree |
| FUN_00404757   |   117 | **deferred** — needs `mesh+0x20` flag (we don't track skinning) |
| FUN_00403eb7   |   108 | **adapted** as `scene1_render_emit_frame` + existing `mesh_draw_d3d8` |
| FUN_00403832   |   178 | **deferred** — shader rebind (chr walker chip) |
| FUN_00403f23   |   742 | **deferred** — shader pass 0 (chr walker chip) |
| FUN_00404500   |   360 | **deferred** — shader pass 1 (chr walker chip) |
| FUN_00404668   |   239 | **deferred** — shader pass 2 (chr walker chip) |
| FUN_00404209   |   759 | **deferred** — shader pass 3 (chr walker chip) |
| FUN_004a662f   |   168 | **N/A** — FVF stride lookup (we hardcode `sizeof(mesh_vertex)`) |

Of the ~2.5 KB engine surface in this chain, ~100 B (FUN_00403eb7 +
the engine's wrapper that calls it) lands as the adapter; ~2 KB is
deferred to the chr walker chip. The remaining ~200 B (the scene-
tree walker) is owned by whatever assembles HOUSE — next chip.

## Related files

- `src/scene1_render.{c,h}` — C7f/C7g/C7h brackets + C8a mesh
  dispatcher. Will gain `scene1_render_emit_frame`.
- `src/mesh_draw.{c,h}` — per-isolated-mesh emit (preview path).
  Used by the new adapter.
- `src/mesh.{c,h}` — flat `mesh_t` (vertex buffer + indices +
  submesh table + material table).
- `docs/findings/mesh-loader.md` — C1-C6 mesh pipeline. Frame
  transforms NOT pre-applied to vertices — meshes are mesh-local.
- `docs/findings/scene1-walker.md` — FUN_0040a765 survey + key
  correction that the real 3D walker lives at FUN_00459dfd.
- `docs/decompiled/by-address/40404{a20,7df,757,209}.c` /
  `40{3eb7,3832,3f23,4500,4668}.c` — Ghidra output for each
  function above.
