# Skinned meshes: retail's draw path (2026-10-17)

Retail draws its skinned models (the model enemies of FUN_00459dfd / FUN_00456d48, the dungeon
objects of kind 1 — takarabako / obj_door, DAT_073cba20 — and the DAT_073da2f0 array) with
the DX8 SDK SkinnedMesh sample's **D3DINDEXEDVS** method. The port has the frame tree and the
animation (`src/model_anchor.c`, `xfile_parse_hier`) but not the meshes yet.

## Load (FUN_00403b6e → FUN_00402551)
- `D3DXLoadSkinMeshFromXof` per Mesh (FUN_00402a86, sub_4c8a92): adjacency, materials, bone
  names and bone offset matrices (+0x30 / +0x34 of the mesh container).
- With bones: FUN_00403832 converts to an indexed-blended mesh (bone-combination table,
  palette from c9). Without: the original mesh.
- Textures through FUN_00471b24 relative to the .x directory; with the object's +744 flag the
  materials' diffuse is forced to 0 and ambient to 1.
- FUN_0040393e creates eight vertex shaders from **source text embedded in the exe**
  (`vs.1.1`, assembled at runtime, file offsets 0x113cdc..), 1..4 influences × {unlit, lit}.
  The declaration: v0 position, v1 blend weights, v2 indices (D3DCOLOR, swizzled .zyxw · 765.01
  to a palette row), v3 normal, v4 diffuse, v5 uv.

## Shader math (both sets)
- `pos = Σ wᵢ · (v0 · M[iᵢ])` with the last weight `1 − Σ` of the others; normal the same with the
  3×3 part; `oPos = pos · c2..c5`.
- normal normalised; `l = lit(n · c1)` with c1 = (0, 0, 1, 0) (FUN_00403ca7);
  `L = min(l.y · c8 + c7, 1)`.
- **unlit set: `oD0 = v4`** (the lighting is computed and dropped); **lit set: `oD0 = v4 · L`**;
  `oD1 = 0`; `oT0 = v5`.

## Draw (FUN_00404a20 → FUN_004047df → FUN_00404209 per container)
- c0 = (1, 0, 0, 765.01); per bone-combination group the palette c9 + 3k = transpose of
  (offset · combined · DAT_073de29c); c7 = material diffuse · (0.25, 0.25, 0.25, 1) ambient
  (FUN_00404c36 / FUN_00404c0c); c8 = material (or DAT_06a499dc.. when +744 is 0 and
  DAT_06a49b20 is set); SetVertexShader by the group's influence count; DrawIndexedPrimitive
  over the group's faces.

## A port without D3DX
CPU-skin into the static mesh path (the same blend as the shader), colour the vertices with
`v4` (unlit) or `v4 · L` (lit, the light in view space along +z), and draw with FFP lighting off.
Needs: SkinWeights / XSkinMeshHeader in `src/xfile.c` (bone name, vertex indices, weights,
offset matrix) and the meshes attached to their frames in the hierarchy.
