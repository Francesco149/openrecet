# C7+ — scene-1 render path (Mt. Everest)

**Status (2026-05-23):** C7a landed (`--show-mesh` CLI flag +
`src/mesh_draw.{c,h}`). C1-C7a wire the mesh pipeline end-to-end to
pixels for a single mesh; the remaining chips (C7b onward) build the
scene state, asset load chain, and the engine's per-stage scene-1
render functions.

This is the climb that takes us from "openrecet boots, plays title,
fades to a placeholder ingame screen" to "openrecet renders the
player's shop interior". Multi-chip, multi-session. Plan first; chip
small.

## What we have today

- Mesh pipeline: `xfile_parse` → `mesh_build_from_xfile` →
  `mesh_compute_bounds` → `mesh_upload_d3d8`. All 242 vendor `.x`
  files build clean.
- `mesh_load(path, -1)` orchestrator with the global texture cache
  + 10 mode-flag side-tables. Texture deduplication wired against
  `sprite_load` on Win32.
- All 9 secondary worker-thread inner-body slots registered (walls /
  floor / jutan / table / sc1 / pause / worldmap / buy×2). The cogs
  to ASK for meshes are spinning — nothing fills the asset queue
  yet because the spawner isn't called.
- `render_dispatch` in `main.c` does BeginScene / EndScene / Clear /
  Present and dispatches to `scene_title_render` or
  `scene_ingame_render`. The latter is a placeholder that draws a
  navy clear + two text labels.

## What we need

End-to-end pipeline for the shop interior:

```
[stage state] → FUN_00474a9a → worker_load_spawn → secondary spawners
                                    ↓
                              walls / floor / jutan / table loaders
                                    ↓
                              mesh_load + mesh_load_finalize_win32
                                    ↓
                              g_scene_*[i].mesh + g_mesh_tex_cache
                                    ↓                                  ↑
[camera + lighting]      ←──   FUN_0045bbf9 + FUN_0040a765 + FUN_00417504 + FUN_0045404b
                                    ↓                                  ↓
                              SetTransform + SetMaterial + SetTexture + DrawIndexedPrimitive
                                    ↓
[composition tail]    →   FUN_00453d9c + FUN_00453e8f + FUN_00453147
                                    ↓
                              EndScene + Present
```

Three families of work:

1. **Stage state population.** `DAT_0438b1c0` (scene state),
   `DAT_068dd2f0` (per-stage palette pointer), and the
   `DAT_04510578..0x4510588` selector array used by
   FUN_00474a9a are all BSS-zero today. Need a minimal "stage 0 =
   house" seed so the load chain has something to fetch.
2. **3D pipeline scaffolding.** D3D8 view + projection matrices,
   lighting state, the per-mesh draw helper. We have FVF 0x152 in
   `mesh.h` but the render loop that calls
   `IDirect3DDevice8_SetStreamSource` / `SetIndices` /
   `DrawIndexedPrimitive` per submesh doesn't exist yet.
3. **The scene-1 render functions.** `FUN_0040a765` (7558 B) is the
   main mesh walker; smaller siblings handle camera, post-process,
   FPS counter, etc.

## Function inventory (sizes from `docs/decompiled/functions.csv`)

| addr        | size  | role                                              |
|-------------|------:|---------------------------------------------------|
| `0x4547ab`  | 1670  | `FUN_004547ab` — render thread top-level (partial port in `main.c::render_dispatch`) |
| `0x474e7a`  |   153 | `FUN_00474e7a` — pre-render device setup (called from the device-recover branch) |
| `0x474a9a`  |   760 | `FUN_00474a9a` — scene-1 PRE-LOAD entry. Triggers asset loads for the current stage |
| `0x45bbf9`  |   134 | `FUN_0045bbf9` — first scene-1 render call |
| `0x40a765`  |  7558 | `FUN_0040a765` — main scene-1 render walker. **Mt. Everest within Mt. Everest** |
| `0x417504`  |   506 | `FUN_00417504` — scene-1 post-walker |
| `0x45404b`  |   326 | `FUN_0045404b` — scene-1 post-walker tail |
| `0x40c962`  |   799 | `FUN_0040c962` — conditional scene-1 helper (DAT_0438b1c8 gate) |
| `0x453d9c`  |   243 | `FUN_00453d9c` — frame composition |
| `0x453e8f`  |   444 | `FUN_00453e8f` — frame composition |
| `0x453147`  |   362 | `FUN_00453147` — frame composition |
| `0x4523e6`  |   387 | `FUN_004523e6` — FPS counter draw |
| `0x474681`  |   ?   | `FUN_00474681` — pre-load asset bundle (called from FUN_00474a9a) |
| `0x473c15`  |   ?   | `FUN_00473c15` — pre-load asset bundle (called from FUN_00474a9a) |

Total render-side scope: ~12-13 KB of decompiled C. Most of it lives
inside FUN_0040a765.

## Chip ladder (proposed)

Order is smallest beachhead first. Each chip should land independently
with tests + (where possible) a visual smoke.

### Pre-render (no scene state required)

These prove the mesh pipeline end-to-end against real vendor meshes
*without* touching the scene state machine. The point is to validate
C1-C6 visually and produce reusable drawing primitives before tackling
the harder coordination.

- **C7a — `--show-mesh <path>` CLI flag + `mesh_draw_d3d8` helper.**
  ✅ Landed 2026-05-23 (`src/mesh_draw.{c,h}` + main.c CLI hook).
  Smoke against `xfile/etc/ice01.x` produces a rotating textured ice
  crystal on the pink-blue clear color — see `runs/mesh-ice01/`.
  Camera at distance 3·radius, fov_y 60° (later corrected to 45° in
  C7b), Y-axis orbit once every 6 s at host pace. Lighting OFF.
  Pure-C `mesh_resolve_texture_slot` covered by 5 new unit tests
  (839 total from 834). title-z-press 14/14 bit-exact.

- **C7b — depth + lighting render-state.** ✅ Landed 2026-05-23
  (extends `src/mesh_draw.{c,h}`). `mesh_set_default_render_state`
  now ports `FUN_00459dfd` L86..L198 line-by-line: CULLMODE=CCW,
  LIGHTING=TRUE, COLORVERTEX + COLOR1 material sources,
  AMBIENT=0xff000000, SHADEMODE=GOURAUD, ALPHAFUNC=GREATER,
  ALPHAOP=DISABLE, MIPFILTER=NONE, ADDRESSU/V=WRAP, fov_y=45°.
  New `mesh_setup_preview_light` adds a fixed directional light +
  raises ambient to 0x404040 so the preview shows shaded geometry
  against the engine's pitch-black baseline. New `--mesh-zoom
  <factor>` flag dials in the orbital distance for meshes whose
  bound radius is inflated by outlier vertices (shop_1st: 311 unit
  bound vs 60 unit visible interior). Smokes:
  `xfile/etc/ice01.x` (1 submesh) and
  `xfile/shop/shop_1st.x --mesh-zoom 0.2` (48 submeshes, 19
  materials) both render shaded with correct Z-ordering.
  title-z-press 14/14 bit-exact.

**Caveat on per-mesh visual smoke (noted 2026-05-23):** rendering
single meshes in isolation only validates the *pipeline*, not visual
correctness. A mesh that looks broken on its own might be perfectly
fine in the assembled scene — its broken face occluded by an adjacent
prop, its "missing wall" covered by an overlay sprite, its backface-
culled side never visible from the player camera. The earlier idea of
a 242-mesh contact sheet is dropped: it would only catch
catastrophic parser/build failures (which the corpus walk in
test_mesh_load already covers). Real validation comes from scene
composition (C7c+) and Frida cross-validation against retail.
- **C7b — depth + lighting render-state.** Set up
  `D3DRS_ZENABLE` / `D3DRS_LIGHTING` / `D3DRS_AMBIENT` / FVF /
  texture stage state defaults that the mesh walker will rely on.
  Confirm meshes render correctly with depth ordering (currently the
  sprite path disables Z).

### Scene state minimum

These backfill enough state for the scene-1 load chain to fire.

- **C7c — minimal stage state seed.** ✅ Landed 2026-05-23
  (`src/stage_state.{c,h}` + main.c wire). `stage_init_house()`
  writes engine fresh-game defaults (all zero) into the four
  selector globals; called from main.c boot right after the
  scene_*_init batch. 3 new host tests (842 total). title-z-press
  14/14 bit-exact. The values are identical to BSS-zero init —
  having the explicit hook lets future stage transitions fan out
  from one place + documents the slot-0-is-starter-asset
  contract.
- **C7d — DAT_068dd2f0 stage palette stub.** ✅ Landed 2026-05-23
  (`src/stage_palette.{c,h}` + main.c wire). `stage_palette_init_house()`
  zeroes the 0x1b3c-byte HOUSE record and points `g_stage_palette` at
  it. Engine record stride confirmed from
  `DAT_068dd2f0 = &DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c` in
  FUN_00474681 / FUN_00436f97. The struct types the fields scene1
  reads up to +0x1ab0 (`mode`, `gravity_x/y/z`, `lighting_flag_1a88/8c`,
  `clear_r/g/b`) with `_Static_assert(offsetof)` on each; the rest is
  opaque padding awaiting its porter. Other fields visible in scene-1
  reads (fog start/end @ +0x1a38/3c, fog color @ +0x1a40 and
  +0x1a90/94/98, vec @ +0x1adc, lighting flag @ +0x1ae0, boot-trigger
  flag @ +0x1b28) stay typed-as-padding here; each gets named in the
  chip that ports its reader (C7g/C7h or later). 6 new host tests
  (848 total from 842). title-z-press 14/14 bit-exact.

### Pre-load trigger

- **C7e — `FUN_00474a9a` port (760 B).** Scene-1 pre-load entry.
  Port at least the `*DAT_068dd2f0 == 0` HOUSE branch:
  - foreground `_load_with(..., 0)` calls for walls/floor/jutan/table
  - the chr_NN portrait loop
  - leve_win + mood_para sprite loads
  - kick `worker_load_spawn` (the engine does this implicitly via
    the scene transition; we may need to wire it explicitly).

  The DUNGEON `else` branch (DAT_0438b7d8 switch) deferred to a
  later chip.

### Scene-1 render functions

- **C7f — `FUN_0045bbf9` (134 B) survey + port.** Likely camera /
  matrix setup for the scene-1 draw frame. Small enough to port in
  one chip.
- **C7g — `FUN_0045404b` (326 B) port.** Adjacent in the call
  pattern; pairs with C7f.
- **C7h — `FUN_00417504` (506 B) port.** Post-scene walker.
- **C7i — `FUN_0040a765` survey doc.** Before porting, write a
  `docs/findings/scene1-walker.md` mapping the 7558-byte function's
  structure. Almost certainly splits into 4-6 sub-chips of its own.
- **C7j..C7n — port `FUN_0040a765` per the survey.** Per-mesh draw
  loop; per-light setup; per-stage geometry walk; etc.

### Composition tail

- **C7o — `FUN_00453d9c` (243 B) + `FUN_00453e8f` (444 B) +
  `FUN_00453147` (362 B).** Frame composition. Probably alpha-layer
  / overlay / UI quad system. Could fan out further.
- **C7p — `FUN_004523e6` (387 B) FPS counter.** Easy with the font
  system already in place; depends on g_dispfps config + an internal
  counter we can wire to `g_tick.frame_count`.

### Polish

- **C7q — dungeon variants.** Port the `DAT_0438b7d8` switch in
  FUN_00474a9a — dungeon stage-specific asset loads.
- **C7r — stage transitions.** Re-fire `FUN_00474a9a` on stage
  change. Needs the stage-transition state machine, which is
  separate work.

## Where to start in the next session

**C7a — `--show-mesh <path>`.** Smallest possible visual smoke for
C1-C6. Build it like the existing `--show-sprite` CLI flag:

1. New CLI flag in `main.c` next to `--show-sprite`.
2. Load via `mesh_load(opt_show_mesh_path, -1)` +
   `mesh_load_finalize_win32(m, g_dev)`.
3. Compute view/projection from `mesh_compute_bounds` (centroid +
   radius). Camera orbiting on the Y axis at 1.5× radius.
4. Render in a new helper `mesh_draw_d3d8(dev, m)` — for each
   submesh: SetStreamSource (mesh-wide VB), SetIndices (mesh-wide
   IB), SetTexture (resolve via `m->texture_slots[submesh.material_index]`
   → `g_mesh_tex_cache.entries[slot].sprite`), SetMaterial (engine
   "ambient = diffuse"), DrawIndexedPrimitive (offset + count from
   submesh).
5. Wire into `render_dispatch`: if `g_show_mesh.mesh != NULL`,
   render it after the scene + before fade/nowloading overlays.

Test plan for C7a:
- Smoke: `--show-mesh xfile/etc/ice01.x --max-duration-ms 2000
  --capture-frames 0,30,60 --capture-to runs/mesh-ice01/`.
  Inspect captured PNGs.
- Contact sheet: a tools/contact-sheet-meshes.py that renders every
  vendor `.x` for 1 frame each via `--show-mesh` + `--capture-to`,
  composites the 242 results into one tiled PNG for visual review.
- Unit tests: the draw helper itself isn't unit-testable (it needs a
  device), but the texture-slot resolution from cache index → sprite
  pointer is — add a test that fills the cache + verifies the
  resolution path picks the right sprite for a given material index.

## Hazards / known unknowns

- **FUN_0040a765 is large.** 7558 bytes of decompiled C. The
  per-mesh transform fan, the light setup, and the stage geometry
  walk are all in there. Expect this to fan out into its own
  multi-chip plan once C7i (the survey) runs.
- **Frame transforms.** `mesh.c::mesh_build_from_xfile` doesn't
  pre-apply Frame transforms (vertices stay in mesh-local space).
  Static stage geometry comes through fine because positions are in
  level data, but if any vendor `.x` relies on Frame transforms for
  its visual layout, that surfaces here. Plan: add Frame
  pre-application in C7a or C7i when a corpus file requires it.
- **Per-vertex MeshVertexColors.** mesh.c defaults diffuse to white;
  the corpus has 1860 MeshVertexColors blocks. Likely visible on
  some props. Plan: extend the parser + builder when something
  noticeable lacks shading.
- **Material ref-then-inline ordering.** mesh.c assumes
  ref-first-then-inline in MeshMaterialList. Holds for ice01.x;
  unverified across the corpus. Watch for wrong-material faces.
- **Texture filtering / sampler state.** Engine uses bilinear by
  default with clamp addressing. Verify against a side-by-side with
  retail once C7a lands.
- **Light setup.** FUN_0040a765 probably configures 1-N
  IDirect3DLight8s via `SetLight` / `LightEnable`. The mesh.h FVF
  includes NORMAL so the engine clearly uses fixed-function
  lighting — we'll need to match the light count + colors.

## Related files

- `docs/findings/mesh-loader.md` — C1-C6 strategy + status (mesh
  pipeline is the input to this work).
- `src/mesh.{c,h}`, `src/mesh_load.{c,h}` — the mesh pipeline (read
  first to know what's available).
- `src/main.c::render_dispatch` — the existing scene dispatcher
  we'll extend.
- `src/scene_ingame.{c,h}` — the placeholder we'll progressively
  replace.
- `src/scene_walls.{c,h}` / `scene_floor` / `scene_jutan` /
  `scene_table` / `scene_sc1` — registered worker bodies; selector
  + state arrays we'll populate.
- `docs/decompiled/by-address/4547ab.c` / `474a9a.c` / `45bbf9.c` /
  `40a765.c` — Ghidra output for the functions to port.
